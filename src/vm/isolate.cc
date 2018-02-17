// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
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

Monitor* Isolate::isolates_list_monitor_ = NULL;
Isolate* Isolate::isolates_list_head_ = NULL;
ThreadPool* Isolate::thread_pool_ = NULL;


void Isolate::Startup() {
  isolates_list_monitor_ = new Monitor();
  thread_pool_ = new ThreadPool();
}


void Isolate::Shutdown() {
  delete thread_pool_;  // Waits for all tasks to complete.
  thread_pool_ = NULL;
  ASSERT(isolates_list_head_ == NULL);
  delete isolates_list_monitor_;
  isolates_list_monitor_ = NULL;
}


void Isolate::AddIsolateToList(Isolate* isolate) {
  MonitorLocker ml(isolates_list_monitor_);
  ASSERT(isolate != NULL);
  ASSERT(isolate->next_ == NULL);
  isolate->next_ = isolates_list_head_;
  isolates_list_head_ = isolate;
}


void Isolate::RemoveIsolateFromList(Isolate* isolate) {
  MonitorLocker ml(isolates_list_monitor_);
  ASSERT(isolate != NULL);
  if (isolate == isolates_list_head_) {
    isolates_list_head_ = isolate->next_;
    return;
  }
  Isolate* previous = NULL;
  Isolate* current = isolates_list_head_;
  while (current != NULL) {
    if (current == isolate) {
      ASSERT(previous != NULL);
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
  while (current != NULL) {
    OS::PrintErr("Interrupting %" Px "\n", reinterpret_cast<uword>(current));
    current->Interrupt();
    current = current->next_;
  }
}


void Isolate::Interrupt() {
  interpreter_->Interrupt();
  loop_->Interrupt();
}


void Isolate::PrintStack() {
  MonitorLocker ml(isolates_list_monitor_);
  OS::PrintErr("%" Px " interrupted: \n", reinterpret_cast<uword>(this));
  heap_->PrintStack();
}


Isolate::Isolate(void* snapshot, size_t snapshot_length, uint64_t seed) :
    heap_(NULL),
    interpreter_(NULL),
    loop_(NULL),
    snapshot_(snapshot),
    snapshot_length_(snapshot_length),
    salt_(static_cast<uintptr_t>(seed)),
    random_(seed),
    next_(NULL) {
  heap_ = new Heap();
  interpreter_ = new Interpreter(heap_, this);
  loop_ = new PlatformMessageLoop(this);
  {
    Deserializer deserializer(heap_, snapshot, snapshot_length);
    deserializer.Deserialize();
  }

  AddIsolateToList(this);
}


Isolate::~Isolate() {
  RemoveIsolateFromList(this);
  delete heap_;
  delete interpreter_;
  delete loop_;
}


void Isolate::ActivateMessage(IsolateMessage* isolate_message) {
  Object* message;
  if (isolate_message->data() != NULL) {
    intptr_t length = isolate_message->length();
    ByteArray* bytes = heap_->AllocateByteArray(length);  // SAFEPOINT
    memcpy(bytes->element_addr(0), isolate_message->data(), length);
    message = bytes;
  } else {
    int argc = isolate_message->argc();
    Array* strings = heap_->AllocateArray(argc);  // SAFEPOINT
    for (intptr_t i = 0; i < argc; i++) {
      strings->set_element(i, SmallInteger::New(0));
    }

    HandleScope h1(heap_, reinterpret_cast<Object**>(&strings));
    for (intptr_t i = 0; i < argc; i++) {
      const char* cstr = isolate_message->argv()[i];
      intptr_t length = strlen(cstr);
      String* string = heap_->AllocateString(length);  // SAFEPOINT
      memcpy(string->element_addr(0), cstr, length);
      strings->set_element(i, string);
    }
    message = strings;
  }

  Port port_id = isolate_message->dest_port();
  Object* port;
  if (port_id == ILLEGAL_PORT) {
    port = heap_->object_store()->nil_obj();
  } else if (SmallInteger::IsSmiValue(port_id)) {
    port = SmallInteger::New(port_id);
  } else {
    HandleScope h1(heap_, reinterpret_cast<Object**>(&message));
    MediumInteger* mint = heap_->AllocateMediumInteger();  // SAFEPOINT
    mint->set_value(port_id);
    port = mint;
  }

  Activate(message, port);
}


void Isolate::ActivateWakeup() {
  Object* nil = heap_->object_store()->nil_obj();
  Activate(nil, nil);
}


void Isolate::Activate(Object* message, Object* port) {
  Scheduler* scheduler = heap_->object_store()->scheduler();

  Behavior* cls = scheduler->Klass(heap_);
  String* selector = heap_->object_store()->dispatch_message();
  Method* method = interpreter_->MethodAt(cls, selector);

  HandleScope h1(heap_, reinterpret_cast<Object**>(&message));
  HandleScope h2(heap_, reinterpret_cast<Object**>(&port));
  HandleScope h3(heap_, reinterpret_cast<Object**>(&scheduler));
  HandleScope h4(heap_, reinterpret_cast<Object**>(&method));

  Activation* new_activation = heap_->AllocateActivation();  // SAFEPOINT

  Object* nil = heap_->object_store()->nil_obj();
  new_activation->set_sender(static_cast<Activation*>(nil));
  new_activation->set_bci(SmallInteger::New(1));
  new_activation->set_method(method);
  new_activation->set_closure(static_cast<Closure*>(nil));
  new_activation->set_receiver(scheduler);
  new_activation->set_stack_depth(0);

  ASSERT(method->NumArgs() == 2);
  new_activation->Push(message);
  new_activation->Push(port);

  intptr_t num_temps = method->NumTemps();
  for (intptr_t i = 0; i < num_temps; i++) {
    new_activation->Push(nil);
  }

  heap_->set_activation(new_activation);
}


void Isolate::ActivateSignal(intptr_t handle,
                             intptr_t status,
                             intptr_t signals,
                             intptr_t count) {
  Scheduler* scheduler = heap_->object_store()->scheduler();

  Behavior* cls = scheduler->Klass(heap_);
  String* selector = heap_->object_store()->dispatch_signal();
  Method* method = interpreter_->MethodAt(cls, selector);

  HandleScope h1(heap_, reinterpret_cast<Object**>(&scheduler));
  HandleScope h2(heap_, reinterpret_cast<Object**>(&method));

  Activation* new_activation = heap_->AllocateActivation();  // SAFEPOINT

  Object* nil = heap_->object_store()->nil_obj();
  new_activation->set_sender(static_cast<Activation*>(nil));
  new_activation->set_bci(SmallInteger::New(1));
  new_activation->set_method(method);
  new_activation->set_closure(static_cast<Closure*>(nil));
  new_activation->set_receiver(scheduler);
  new_activation->set_stack_depth(0);

  ASSERT(method->NumArgs() == 4);
  new_activation->Push(SmallInteger::New(handle));
  new_activation->Push(SmallInteger::New(status));
  new_activation->Push(SmallInteger::New(signals));
  new_activation->Push(SmallInteger::New(count));

  intptr_t num_temps = method->NumTemps();
  for (intptr_t i = 0; i < num_temps; i++) {
    new_activation->Push(nil);
  }

  heap_->set_activation(new_activation);
}


void Isolate::Interpret() {
  interpreter_->Enter();
}


class SpawnIsolateTask : public ThreadPool::Task {
 public:
  SpawnIsolateTask(void* snapshot,
                   size_t snapshot_length,
                   IsolateMessage* initial_message) :
    snapshot_(snapshot),
    snapshot_length_(snapshot_length),
    initial_message_(initial_message) {
  }

  virtual void Run() {
    uint64_t seed = OS::CurrentMonotonicNanos();
    Isolate* child_isolate = new Isolate(snapshot_, snapshot_length_, seed);
    child_isolate->loop()->PostMessage(initial_message_);
    initial_message_ = NULL;
    intptr_t exit_code = child_isolate->loop()->Run();
    delete child_isolate;
    if (exit_code != 0) {
      OS::Exit(exit_code);
    }
  }

 private:
  void* snapshot_;
  size_t snapshot_length_;
  IsolateMessage* initial_message_;

  DISALLOW_COPY_AND_ASSIGN(SpawnIsolateTask);
};


void Isolate::Spawn(IsolateMessage* initial_message) {
  thread_pool_->Run(new SpawnIsolateTask(snapshot_, snapshot_length_,
                                         initial_message));
}

}  // namespace psoup
