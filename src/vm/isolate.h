// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_ISOLATE_H_
#define VM_ISOLATE_H_

#include "vm/allocation.h"
#include "vm/globals.h"
#include "vm/port.h"

namespace psoup {

class Heap;
class Interpreter;
class MessageLoop;
class Monitor;
class Object;
class ThreadPool;

class Isolate {
 public:
  Isolate(void* snapshot, size_t snapshot_length);
  ~Isolate();

  MessageLoop* loop() const { return loop_; }

  void ActivateMessage(IsolateMessage* message);
  void ActivateWakeup();

  void Interpret();

  void Spawn(IsolateMessage* initial_message);

  static void Startup();
  static void Shutdown();

  static void InterruptAll();
  void Interrupt();
  void PrintStack();

 private:
  void Activate(Object* message, Object* port);

  Heap* heap_;
  Interpreter* interpreter_;
  MessageLoop* loop_;
  void* snapshot_;
  size_t snapshot_length_;
  Isolate* next_;

  void AddIsolateToList(Isolate* isolate);
  void RemoveIsolateFromList(Isolate* isolate);

  static Monitor* isolates_list_monitor_;
  static Isolate* isolates_list_head_;
  static ThreadPool* thread_pool_;

  DISALLOW_COPY_AND_ASSIGN(Isolate);
};

}  // namespace psoup

#endif  // VM_ISOLATE_H_
