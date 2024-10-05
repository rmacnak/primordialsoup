// Copyright (c) 2013, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/isolate.h"

#include "vm/heap.h"
#include "vm/interpreter.h"
#include "vm/lockers.h"
#include "vm/message_loop.h"
#include "vm/os.h"
#include "vm/snapshot.h"
#include "vm/thread.h"
#include "vm/thread_pool.h"

namespace psoup {

Monitor* Isolate::isolates_list_monitor_ = nullptr;
Isolate* Isolate::isolates_list_head_ = nullptr;
ThreadPool* Isolate::thread_pool_ = nullptr;

void Isolate::Startup() {
  isolates_list_monitor_ = new Monitor();
  thread_pool_ = new ThreadPool();
}

void Isolate::Shutdown() {
  delete thread_pool_;  // Waits for all tasks to complete.
  thread_pool_ = nullptr;
  ASSERT(isolates_list_head_ == nullptr);
  delete isolates_list_monitor_;
  isolates_list_monitor_ = nullptr;
}

void Isolate::AddIsolateToList(Isolate* isolate) {
  MonitorLocker ml(isolates_list_monitor_);
  ASSERT(isolate != nullptr);
  ASSERT(isolate->next_ == nullptr);
  isolate->next_ = isolates_list_head_;
  isolates_list_head_ = isolate;
}

void Isolate::RemoveIsolateFromList(Isolate* isolate) {
  MonitorLocker ml(isolates_list_monitor_);
  ASSERT(isolate != nullptr);
  if (isolate == isolates_list_head_) {
    isolates_list_head_ = isolate->next_;
    return;
  }
  Isolate* previous = nullptr;
  Isolate* current = isolates_list_head_;
  while (current != nullptr) {
    if (current == isolate) {
      ASSERT(previous != nullptr);
      previous->next_ = current->next_;
      return;
    }
    previous = current;
    current = current->next_;
  }
  UNREACHABLE();
}

void Isolate::InterruptAll() {
  MonitorLocker ml(isolates_list_monitor_);
  OS::PrintErr("Got SIGINT\n");
  Isolate* current = isolates_list_head_;
  while (current != nullptr) {
    OS::PrintErr("Interrupting %" Px "\n", reinterpret_cast<uword>(current));
    current->Interrupt();
    current = current->next_;
  }
}

void Isolate::Interrupt() {
  interpreter_->Interrupt(Interpreter::kInterruptSIGINT);
  loop_->Interrupt();
}

void Isolate::PrintStack() {
  MonitorLocker ml(isolates_list_monitor_);
  OS::PrintErr("%" Px " interrupted: \n", reinterpret_cast<uword>(this));
  interpreter_->PrintStack();
}

Isolate::Isolate(const void* snapshot, size_t snapshot_length)
    : heap_(nullptr),
      interpreter_(nullptr),
      loop_(nullptr),
      snapshot_(snapshot),
      snapshot_length_(snapshot_length),
      random_(),
      salt_(static_cast<uintptr_t>(random_.NextUInt64())),
      next_(nullptr) {
  heap_ = new Heap();
  interpreter_ = new Interpreter(heap_, this);
  loop_ = new PlatformMessageLoop(this);
  Deserialize(heap_, snapshot, snapshot_length);

  AddIsolateToList(this);

  ASSERT(current_ == nullptr);
  current_ = this;
}

Isolate::~Isolate() {
  ASSERT(current_ == this);
  current_ = nullptr;

  RemoveIsolateFromList(this);
  delete heap_;
  delete interpreter_;
  delete loop_;
}

void Isolate::ActivateMessage(IsolateMessage* isolate_message) {
  Object message;
  if (isolate_message->data() != nullptr) {
    intptr_t length = isolate_message->length();
    ByteArray bytes = heap_->AllocateByteArray(length);  // SAFEPOINT
    memcpy(bytes->element_addr(0), isolate_message->data(), length);
    message = bytes;
  } else {
    int argc = isolate_message->argc();
    Array strings = heap_->AllocateArray(argc);  // SAFEPOINT
    for (intptr_t i = 0; i < argc; i++) {
      strings->set_element(i, SmallInteger::New(0));
    }

    HandleScope h1(heap_, &strings);
    for (intptr_t i = 0; i < argc; i++) {
      const char* cstr = isolate_message->argv()[i];
      intptr_t length = strlen(cstr);
      String string = heap_->AllocateString(length);  // SAFEPOINT
      memcpy(string->element_addr(0), cstr, length);
      strings->set_element(i, string);
    }
    message = strings;
  }

  Port port_id = isolate_message->dest_port();
  Object port;
  if (port_id == ILLEGAL_PORT) {
    port = interpreter_->nil_obj();
  } else if (SmallInteger::IsSmiValue(port_id)) {
    port = SmallInteger::New(static_cast<intptr_t>(port_id));
  } else {
    HandleScope h1(heap_, &message);
    MediumInteger mint = heap_->AllocateMediumInteger();  // SAFEPOINT
    mint->set_value(port_id);
    port = mint;
  }

  Activate(message, port);
}

void Isolate::ActivateWakeup() {
  Object nil = interpreter_->nil_obj();
  Activate(nil, nil);
}

void Isolate::Activate(Object message, Object port) {
  Object message_loop = interpreter_->object_store()->message_loop();

  Behavior cls = message_loop->Klass(heap_);
  String selector = interpreter_->object_store()->dispatch_message();
  Method method = interpreter_->MethodAt(cls, selector);

  interpreter_->Push(message_loop);
  interpreter_->Push(message);
  interpreter_->Push(port);
  interpreter_->ActivateDispatch(method, 2);  // SAFEPOINT
}

void Isolate::ActivateSignal(intptr_t handle,
                             intptr_t status,
                             intptr_t signals,
                             intptr_t count) {
  Object message_loop = interpreter_->object_store()->message_loop();

  Behavior cls = message_loop->Klass(heap_);
  String selector = interpreter_->object_store()->dispatch_signal();
  Method method = interpreter_->MethodAt(cls, selector);

  interpreter_->Push(message_loop);
  interpreter_->Push(SmallInteger::New(handle));
  interpreter_->Push(SmallInteger::New(status));
  interpreter_->Push(SmallInteger::New(signals));
  interpreter_->Push(SmallInteger::New(count));
  interpreter_->ActivateDispatch(method, 4);  // SAFEPOINT
}

void Isolate::Interpret() {
  interpreter_->Enter();
}

class SpawnIsolateTask : public ThreadPool::Task {
 public:
  SpawnIsolateTask(const void* snapshot,
                   size_t snapshot_length,
                   IsolateMessage* initial_message)
      : snapshot_(snapshot),
        snapshot_length_(snapshot_length),
        initial_message_(initial_message) {}

  virtual void Run() {
    Isolate* child_isolate = new Isolate(snapshot_, snapshot_length_);
    child_isolate->loop()->PostMessage(initial_message_);
    initial_message_ = nullptr;
    intptr_t exit_code = child_isolate->loop()->Run();
    delete child_isolate;
    if (exit_code != 0) {
      OS::Exit(exit_code);
    }
  }

 private:
  const void* snapshot_;
  size_t snapshot_length_;
  IsolateMessage* initial_message_;

  DISALLOW_COPY_AND_ASSIGN(SpawnIsolateTask);
};

void Isolate::Spawn(IsolateMessage* initial_message) {
  thread_pool_->Run(new SpawnIsolateTask(snapshot_, snapshot_length_,
                                         initial_message));
}

}  // namespace psoup
