// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if defined(OS_WINDOWS)

#include "vm/thread.h"

#include <process.h>

#include "vm/assert.h"
#include "vm/os.h"

namespace psoup {

class ThreadStartData {
 public:
  ThreadStartData(const char* name,
                  Thread::ThreadStartFunction function,
                  uword parameter)
      : name_(name), function_(function), parameter_(parameter) {}

  const char* name() const { return name_; }
  Thread::ThreadStartFunction function() const { return function_; }
  uword parameter() const { return parameter_; }

 private:
  const char* name_;
  Thread::ThreadStartFunction function_;
  uword parameter_;

  DISALLOW_COPY_AND_ASSIGN(ThreadStartData);
};

// Dispatch to the thread start function provided by the caller. This trampoline
// is used to ensure that the thread is properly destroyed if the thread just
// exits.
static unsigned int __stdcall ThreadEntry(void* data_ptr) {
  ThreadStartData* data = reinterpret_cast<ThreadStartData*>(data_ptr);

  const char* name = data->name();
  Thread::ThreadStartFunction function = data->function();
  uword parameter = data->parameter();
  delete data;

  // Call the supplied thread start function handing it its parameters.
  function(parameter);

  return 0;
}

int Thread::Start(const char* name,
                  ThreadStartFunction function,
                  uword parameter) {
  ThreadStartData* start_data = new ThreadStartData(name, function, parameter);
  uint32_t tid;
  uintptr_t thread = _beginthreadex(nullptr, 0,
                                    ThreadEntry, start_data, 0, &tid);
  if (thread == -1L || thread == 0) {
#ifdef DEBUG
    OS::PrintErr("_beginthreadex error: %d (%s)\n", errno, strerror(errno));
#endif
    return errno;
  }

  // Close the handle, so we don't leak the thread object.
  CloseHandle(reinterpret_cast<HANDLE>(thread));

  return 0;
}

ThreadId Thread::GetCurrentThreadId() {
  return ::GetCurrentThreadId();
}

ThreadJoinId Thread::GetCurrentThreadJoinId() {
  ThreadId id = GetCurrentThreadId();
  HANDLE handle = OpenThread(SYNCHRONIZE, false, id);
  ASSERT(handle != nullptr);
  return handle;
}

void Thread::Join(ThreadJoinId id) {
  HANDLE handle = static_cast<HANDLE>(id);
  ASSERT(handle != nullptr);
  DWORD res = WaitForSingleObject(handle, INFINITE);
  CloseHandle(handle);
  ASSERT(res == WAIT_OBJECT_0);
}

bool Thread::Compare(ThreadId a, ThreadId b) {
  return a == b;
}

Mutex::Mutex() {
  InitializeSRWLock(&lock_);
#if defined(DEBUG)
  owner_ = kInvalidThreadId;
#endif  // defined(DEBUG)
}

Mutex::~Mutex() {
  DEBUG_ASSERT(owner_ == kInvalidThreadId);
}

void Mutex::Lock() {
  AcquireSRWLockExclusive(&lock_);
  CheckUnheldAndMark();
}

bool Mutex::TryLock() {
  if (TryAcquireSRWLockExclusive(&lock_)) {
    CheckUnheldAndMark();
    return true;
  }
  return false;
}

void Mutex::Unlock() {
  CheckHeldAndUnmark();
  ReleaseSRWLockExclusive(&lock_);
}

Monitor::Monitor() {
  InitializeSRWLock(&lock_);
  InitializeConditionVariable(&cond_);

#if defined(DEBUG)
  owner_ = kInvalidThreadId;
#endif  // defined(DEBUG)
}

Monitor::~Monitor() {
  DEBUG_ASSERT(owner_ == kInvalidThreadId);
}

bool Monitor::TryEnter() {
  if (TryAcquireSRWLockExclusive(&lock_)) {
    CheckUnheldAndMark();
    return true;
  }
  return false;
}

void Monitor::Enter() {
  AcquireSRWLockExclusive(&lock_);
  CheckUnheldAndMark();
}

void Monitor::Exit() {
  CheckHeldAndUnmark();
  ReleaseSRWLockExclusive(&lock_);
}

void Monitor::Wait() {
  CheckHeldAndUnmark();
  SleepConditionVariableSRW(&cond_, &lock_, INFINITE, 0);
  CheckUnheldAndMark();
}

Monitor::WaitResult Monitor::WaitUntilNanos(int64_t deadline) {
  int64_t now = OS::CurrentMonotonicNanos();
  if (deadline <= now) {
    return kTimedOut;
  }

  int64_t millis = (deadline - now) / kNanosecondsPerMillisecond;

  CheckHeldAndUnmark();

  Monitor::WaitResult retval = kNotified;
  // Wait for the given period of time for a Notify or a NotifyAll
  // event.
  if (!SleepConditionVariableSRW(&cond_, &lock_, millis, 0)) {
    ASSERT(GetLastError() == ERROR_TIMEOUT);
    retval = kTimedOut;
  }

  CheckUnheldAndMark();
  return retval;
}

void Monitor::Notify() {
  DEBUG_ASSERT(IsOwnedByCurrentThread());
  WakeConditionVariable(&cond_);
}

void Monitor::NotifyAll() {
  DEBUG_ASSERT(IsOwnedByCurrentThread());
  WakeAllConditionVariable(&cond_);
}

}  // namespace psoup

#endif  // defined(OS_WINDOWS)
