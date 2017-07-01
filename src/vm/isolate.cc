// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/isolate.h"

#include "vm/heap.h"
#include "vm/interpreter.h"
#include "vm/lockers.h"
#include "vm/message.h"
#include "vm/os_thread.h"
#include "vm/snapshot.h"
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
  queue_->Interrupt();
}


void Isolate::PrintStack() {
  MonitorLocker ml(isolates_list_monitor_);
  OS::PrintErr("%" Px " interrupted: \n", reinterpret_cast<uword>(this));
  heap_->PrintStack();
}


Isolate::Isolate() :
    heap_(NULL),
    interpreter_(NULL),
    queue_(NULL),
    next_(NULL) {
  heap_ = new Heap(this, OS::CurrentMonotonicMicros());
  {
    Deserializer deserializer(heap_);
    deserializer.Deserialize();
  }
  interpreter_ = new Interpreter(heap_, this);
  queue_ = new MessageQueue();

  AddIsolateToList(this);
}


Isolate::~Isolate() {
  RemoveIsolateFromList(this);
  delete heap_;
  delete interpreter_;
  delete queue_;
}


void Isolate::InitWithStringArray(int argc, const char** argv) {
  Array* message = heap_->AllocateArray(argc);  // SAFEPOINT
  for (intptr_t i = 0; i < argc; i++) {
    message->set_element(i, SmallInteger::New(0));
  }

  {
    HandleScope h1(heap_, reinterpret_cast<Object**>(&message));
    for (intptr_t i = 0; i < argc; i++) {
      const char* cstr = argv[i];
      intptr_t len = strlen(cstr);
      ByteString* string = heap_->AllocateByteString(len);  // SAFEPOINT
      memcpy(string->element_addr(0), cstr, len);
      message->set_element(i, string);
    }
  }

  InitMessage(message);
}


void Isolate::InitWithByteArray(uint8_t* data, intptr_t length) {
  ByteArray* message = heap_->AllocateByteArray(length);  // SAFEPOINT
  memcpy(message->element_addr(0), data, length);

  InitMessage(message);
}


void Isolate::InitMessage(Object* message) {
  HandleScope h1(heap_, reinterpret_cast<Object**>(&message));
  Scheduler* scheduler = heap_->object_store()->scheduler();
  HandleScope h2(heap_, reinterpret_cast<Object**>(&scheduler));

  Behavior* cls = scheduler->Klass(heap_);
  ByteString* selector = heap_->object_store()->start();
  Method* method = interpreter_->MethodAt(cls, selector);

  HandleScope h3(heap_, reinterpret_cast<Object**>(&method));

  Activation* new_activation = heap_->AllocateActivation();  // SAFEPOINT

  Object* nil = heap_->object_store()->nil_obj();
  new_activation->set_sender(static_cast<Activation*>(nil));
  new_activation->set_bci(SmallInteger::New(1));
  new_activation->set_method(method);
  new_activation->set_closure(static_cast<Closure*>(nil));
  new_activation->set_receiver(scheduler);
  new_activation->set_stack_depth(0);

  new_activation->Push(message);  // Argument.

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
  SpawnIsolateTask(uint8_t* data, intptr_t length) :
    data_(data), length_(length) {
  }

  virtual void Run() {
    if (TRACE_ISOLATES) {
      intptr_t id = OSThread::ThreadIdToIntPtr(OSThread::Current()->trace_id());
      OS::PrintErr("Starting isolate on thread %" Pd "\n", id);
    }
    Isolate* child_isolate = new Isolate();

    child_isolate->InitWithByteArray(data_, length_);
    free(data_);
    data_ = NULL;
    length_ = 0;

    child_isolate->Interpret();

    if (TRACE_ISOLATES) {
      intptr_t id = OSThread::ThreadIdToIntPtr(OSThread::Current()->trace_id());
      OS::PrintErr("Finishing isolate on thread %" Pd "\n", id);
    }
    delete child_isolate;
  }

 private:
  uint8_t* data_;
  intptr_t length_;

  DISALLOW_COPY_AND_ASSIGN(SpawnIsolateTask);
};


void Isolate::Spawn(uint8_t* data, intptr_t length) {
  thread_pool_->Run(new SpawnIsolateTask(data, length));
}

}  // namespace psoup
