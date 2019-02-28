// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_THREAD_H_
#define VM_THREAD_H_

#include "vm/allocation.h"
#include "vm/globals.h"

// Declare the OS-specific types ahead of defining the generic classes.
#if defined(OS_ANDROID)
#include "vm/thread_android.h"
#elif defined(OS_FUCHSIA)
#include "vm/thread_fuchsia.h"
#elif defined(OS_LINUX)
#include "vm/thread_linux.h"
#elif defined(OS_MACOS)
#include "vm/thread_macos.h"
#elif defined(OS_WINDOWS)
#include "vm/thread_win.h"
#else
#error Unknown OS.
#endif

namespace psoup {

// Forward declarations.
class Mutex;
class Isolate;

class Thread {
 public:
  Thread();
  ~Thread();

  ThreadId id() const {
    ASSERT(id_ != Thread::kInvalidThreadId);
    return id_;
  }

  ThreadId trace_id() const {
    ASSERT(trace_id_ != Thread::kInvalidThreadId);
    return trace_id_;
  }

  const char* name() const { return name_; }
  void set_name(const char* name) {
    ASSERT(Thread::Current() == this);
    ASSERT(name_ == NULL);
    ASSERT(name != NULL);
    name_ = strdup(name);
  }

  Isolate* isolate() const { return isolate_; }
  void set_isolate(Isolate* isolate) {
    isolate_ = isolate;
  }

  // The currently executing thread, or NULL if not yet initialized.
  static Thread* Current() {
    Thread* thread = GetCurrentTLS();
    if (thread == NULL) {
      thread = CreateAndSetUnknownThread();
    }
    return thread;
  }
  static void SetCurrent(Thread* current);

  static Thread* GetCurrentTLS() {
    return reinterpret_cast<Thread*>(Thread::GetThreadLocal(thread_key_));
  }
  static void SetCurrentTLS(uword value) { SetThreadLocal(thread_key_, value); }

  typedef void (*ThreadStartFunction)(uword parameter);
  typedef void (*ThreadDestructor)(void* parameter);

  // Start a thread running the specified function. Returns 0 if the
  // thread started successfuly and a system specific error code if
  // the thread failed to start.
  static int Start(const char* name,
                   ThreadStartFunction function,
                   uword parameter);

  static ThreadLocalKey CreateThreadLocal(ThreadDestructor destructor = NULL);
  static void DeleteThreadLocal(ThreadLocalKey key);
  static uword GetThreadLocal(ThreadLocalKey key) {
    return ThreadInlineImpl::GetThreadLocal(key);
  }
  static ThreadId GetCurrentThreadId();
  static void SetThreadLocal(ThreadLocalKey key, uword value);
  static void Join(ThreadJoinId id);
  static intptr_t ThreadIdToIntPtr(ThreadId id);
  static ThreadId ThreadIdFromIntPtr(intptr_t id);
  static bool Compare(ThreadId a, ThreadId b);

  // This function can be called only once per Thread, and should only be
  // called when the retunred id will eventually be passed to Thread::Join().
  static ThreadJoinId GetCurrentThreadJoinId(Thread* thread);

  // Called at VM startup and shutdown.
  static void Startup();
  static void Shutdown();

  static void DisableThreadCreation();
  static void EnableThreadCreation();

  static const ThreadId kInvalidThreadId;
  static const ThreadJoinId kInvalidThreadJoinId;

  static ThreadId GetCurrentThreadTraceId();

 private:
  static Thread* CreateAndSetUnknownThread();

  static ThreadLocalKey thread_key_;

  const ThreadId id_;
#if defined(DEBUG)
  // In DEBUG mode we use this field to ensure that GetCurrentThreadJoinId is
  // only called once per Thread.
  ThreadJoinId join_id_;
#endif
  const ThreadId trace_id_;  // Used to interface with tracing tools.
  char* name_;  // A name for this thread.
  Isolate* isolate_;
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
    owner_ = Thread::kInvalidThreadId;
#endif
  }
  void CheckUnheldAndMark() {
#if defined(DEBUG)
    ASSERT(owner_ == Thread::kInvalidThreadId);
    owner_ = Thread::GetCurrentThreadId();
#endif
  }

  MutexData data_;
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
    owner_ = Thread::kInvalidThreadId;
#endif
  }
  void CheckUnheldAndMark() {
#if defined(DEBUG)
    ASSERT(owner_ == Thread::kInvalidThreadId);
    owner_ = Thread::GetCurrentThreadId();
#endif
  }

  MonitorData data_;  // OS-specific data.
#if defined(DEBUG)
  ThreadId owner_;
#endif  // defined(DEBUG)

  friend class MonitorLocker;
  DISALLOW_COPY_AND_ASSIGN(Monitor);
};


}  // namespace psoup


#endif  // VM_THREAD_H_
