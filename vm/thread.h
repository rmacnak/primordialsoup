// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_THREAD_H_
#define VM_THREAD_H_

#include "vm/allocation.h"
#include "vm/assert.h"
#include "vm/globals.h"

#if defined(OS_ANDROID) || defined(OS_FUCHSIA) || defined(OS_LINUX) ||         \
    defined(OS_MACOS)
#include <pthread.h>
#endif

namespace psoup {

#if defined(OS_ANDROID) || defined(OS_FUCHSIA) || defined(OS_LINUX) ||         \
    defined(OS_MACOS)
typedef pthread_t ThreadId;
typedef pthread_t ThreadJoinId;
#if defined(OS_MACOS)
constexpr ThreadId kInvalidThreadId = nullptr;
constexpr ThreadJoinId kInvalidThreadJoinId = nullptr;
#else
constexpr ThreadId kInvalidThreadId = 0;
constexpr ThreadJoinId kInvalidThreadJoinId = 0;
#endif
#elif defined(OS_EMSCRIPTEN)
typedef intptr_t ThreadId;
typedef intptr_t ThreadJoinId;
constexpr ThreadId kInvalidThreadId = 0;
constexpr ThreadJoinId kInvalidThreadJoinId = 0;
#elif defined(OS_WINDOWS)
typedef DWORD ThreadId;
typedef HANDLE ThreadJoinId;
constexpr ThreadId kInvalidThreadId = 0;
const ThreadJoinId kInvalidThreadJoinId = INVALID_HANDLE_VALUE;
#endif

class Thread : public AllStatic {
 public:
  typedef void (*ThreadStartFunction)(uword parameter);

  // Start a thread running the specified function. Returns 0 if the
  // thread started successfuly and a system specific error code if
  // the thread failed to start.
  static int Start(const char* name,
                   ThreadStartFunction function,
                   uword parameter);

  static ThreadId GetCurrentThreadId();
  static void Join(ThreadJoinId id);
  static bool Compare(ThreadId a, ThreadId b);

  // This function can be called only once per Thread, and should only be
  // called when the returned id will eventually be passed to Thread::Join().
  static ThreadJoinId GetCurrentThreadJoinId();
};

class Mutex {
 public:
  Mutex();
  ~Mutex();

#if defined(DEBUG)
  bool IsOwnedByCurrentThread() const {
    return owner_ == Thread::GetCurrentThreadId();
  }
#endif

 private:
  void Lock();
  bool TryLock();  // Returns false if lock is busy and locking failed.
  void Unlock();

  void CheckHeldAndUnmark() {
#if defined(DEBUG)
    ASSERT(owner_ == Thread::GetCurrentThreadId());
    owner_ = kInvalidThreadId;
#endif
  }
  void CheckUnheldAndMark() {
#if defined(DEBUG)
    ASSERT(owner_ == kInvalidThreadId);
    owner_ = Thread::GetCurrentThreadId();
#endif
  }

#if defined(OS_ANDROID) || defined(OS_FUCHSIA) || defined(OS_LINUX) ||         \
    defined(OS_MACOS)
  pthread_mutex_t mutex_;
#elif defined(OS_WINDOWS)
  SRWLOCK lock_;
#endif

#if defined(DEBUG)
  ThreadId owner_;
#endif  // defined(DEBUG)

  friend class MutexLocker;
  DISALLOW_COPY_AND_ASSIGN(Mutex);
};

class Monitor {
 public:
  enum WaitResult { kNotified, kTimedOut };

  Monitor();
  ~Monitor();

#if defined(DEBUG)
  bool IsOwnedByCurrentThread() const {
    return owner_ == Thread::GetCurrentThreadId();
  }
#endif

 private:
  bool TryEnter();  // Returns false if lock is busy and locking failed.
  void Enter();
  void Exit();

  // Wait for notification or deadline.
  void Wait();
  WaitResult WaitUntilNanos(int64_t deadline);

  // Notify waiting threads.
  void Notify();
  void NotifyAll();

  void CheckHeldAndUnmark() {
#if defined(DEBUG)
    ASSERT(owner_ == Thread::GetCurrentThreadId());
    owner_ = kInvalidThreadId;
#endif
  }
  void CheckUnheldAndMark() {
#if defined(DEBUG)
    ASSERT(owner_ == kInvalidThreadId);
    owner_ = Thread::GetCurrentThreadId();
#endif
  }

#if defined(OS_ANDROID) || defined(OS_FUCHSIA) || defined(OS_LINUX) ||         \
    defined(OS_MACOS)
  pthread_mutex_t mutex_;
  pthread_cond_t cond_;
#elif defined(OS_WINDOWS)
  SRWLOCK lock_;
  CONDITION_VARIABLE cond_;
#endif

#if defined(DEBUG)
  ThreadId owner_;
#endif  // defined(DEBUG)

  friend class MonitorLocker;
  DISALLOW_COPY_AND_ASSIGN(Monitor);
};

}  // namespace psoup

#endif  // VM_THREAD_H_
