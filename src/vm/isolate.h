// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_ISOLATE_H_
#define VM_ISOLATE_H_

#include "vm/allocation.h"
#include "vm/globals.h"

namespace psoup {

class Heap;
class Interpreter;
class MessageQueue;
class Monitor;
class Object;
class ThreadPool;

class Isolate {
 public:
  Isolate();
  ~Isolate();

  MessageQueue* queue() const { return queue_; }

  // Create the initial activation from either the command line arguments or
  // spawn message.
  void InitWithStringArray(int argc, const char** argv);
  void InitWithByteArray(uint8_t* data, intptr_t length);

  void Interpret();

  static void Spawn(uint8_t* data, intptr_t length);

  static void Startup();
  static void Shutdown();

  static void InterruptAll();
  void Interrupt();
  void PrintStack();

 private:
  void InitMessage(Object* message);

  Heap* heap_;
  Interpreter* interpreter_;
  MessageQueue* queue_;
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
