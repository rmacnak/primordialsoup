// Copyright (c) 2015, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/thread.h"

namespace psoup {

// The single thread local key which stores all the thread local data
// for a thread.
ThreadLocalKey Thread::thread_key_ = kUnsetThreadLocalKey;


Thread::Thread()
    : id_(Thread::GetCurrentThreadId()),
#if defined(DEBUG)
      join_id_(kInvalidThreadJoinId),
#endif
      trace_id_(Thread::GetCurrentThreadTraceId()),
      name_(NULL),
      isolate_(NULL) {
}


Thread::~Thread() {
  ASSERT(isolate_ == NULL);
  free(name_);
}


static void DeleteThread(void* thread) {
  delete reinterpret_cast<Thread*>(thread);
}


void Thread::Startup() {
  // Create the thread local key.
  ASSERT(thread_key_ == kUnsetThreadLocalKey);
  thread_key_ = CreateThreadLocal(DeleteThread);
  ASSERT(thread_key_ != kUnsetThreadLocalKey);

  // Create a new Thread strcture and set it as the TLS.
  Thread* thread = new Thread();
  Thread::SetCurrent(thread);
  thread->set_name("PrimordialSoup_Startup");
}


void Thread::Shutdown() {
  // Delete the thread local key.
  ASSERT(thread_key_ != kUnsetThreadLocalKey);
  DeleteThreadLocal(thread_key_);
  thread_key_ = kUnsetThreadLocalKey;
}


Thread* Thread::CreateAndSetUnknownThread() {
  ASSERT(Thread::GetCurrentTLS() == NULL);
  Thread* thread = new Thread();
  Thread::SetCurrent(thread);
  thread->set_name("Unknown");
  return thread;
}


void Thread::SetCurrent(Thread* current) {
  Thread::SetThreadLocal(thread_key_, reinterpret_cast<uword>(current));
}

}  // namespace psoup
