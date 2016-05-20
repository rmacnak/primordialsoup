// Copyright (c) 2015, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/thread.h"

#include "vm/lockers.h"
#include "vm/os_thread.h"

namespace psoup {

Thread::~Thread() {
  // We should cleanly exit any isolate before destruction.
  ASSERT(isolate_ == NULL);
}


Thread::Thread(Isolate* isolate)
    : BaseThread(false),
      isolate_(NULL),
      os_thread_(NULL),
      next_(NULL) {
}

  /*
bool Thread::EnterIsolate(Isolate* isolate) {
  const bool kIsMutatorThread = true;
  Thread* thread = isolate->ScheduleThread(kIsMutatorThread);
  if (thread != NULL) {
    ASSERT(thread->store_buffer_block_ == NULL);
    thread->task_kind_ = kMutatorTask;
    thread->StoreBufferAcquire();
    return true;
  }
  return false;
}


void Thread::ExitIsolate() {
  Thread* thread = Thread::Current();
  ASSERT(thread != NULL && thread->IsMutatorThread());
  DEBUG_ASSERT(!thread->IsAnyReusableHandleScopeActive());
  thread->task_kind_ = kUnknownTask;
  Isolate* isolate = thread->isolate();
  ASSERT(isolate != NULL);
  ASSERT(thread->execution_state() == Thread::kThreadInVM);
  // Clear since GC will not visit the thread once it is unscheduled.
  thread->ClearReusableHandles();
  thread->StoreBufferRelease();
  if (isolate->is_runnable()) {
    thread->set_vm_tag(VMTag::kIdleTagId);
  } else {
    thread->set_vm_tag(VMTag::kLoadWaitTagId);
  }
  const bool kIsMutatorThread = true;
  isolate->UnscheduleThread(thread, kIsMutatorThread);
}
  */


uword Thread::GetCurrentStackPointer() {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
  UNIMPLEMENTED();
  return 0;
#else
  int func;
  uword stack_allocated_local_address = reinterpret_cast<uword>(&func);
  return stack_allocated_local_address;
#endif
#else
  int func;
  uword stack_allocated_local_address = reinterpret_cast<uword>(&func);
  return stack_allocated_local_address;
#endif
}

}  // namespace psoup
