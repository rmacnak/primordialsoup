// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if defined(OS_ANDROID)  || defined(OS_FUCHSIA) || defined(OS_LINUX) ||        \
    defined(OS_MACOS)

#include "vm/thread.h"

#include <errno.h>
#include <sys/time.h>

#include "vm/assert.h"
#include "vm/os.h"

namespace psoup {

#define VALIDATE_PTHREAD_RESULT(result)                                        \
  if (result != 0) {                                                           \
    char buffer[64];                                                           \
    FATAL("pthread error: %d (%s)", result,                                    \
          OS::StrError(result, buffer, sizeof(buffer)));                       \
  }

#if defined(DEBUG)
#define ASSERT_PTHREAD_SUCCESS(result) VALIDATE_PTHREAD_RESULT(result)
#else
#define ASSERT_PTHREAD_SUCCESS(result) ASSERT(result == 0)
#endif

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
static void* ThreadStart(void* data_ptr) {
  ThreadStartData* data = reinterpret_cast<ThreadStartData*>(data_ptr);

  const char* name = data->name();
  Thread::ThreadStartFunction function = data->function();
  uword parameter = data->parameter();
  delete data;

  // Set the thread name.
#if defined(OS_MACOS)
  pthread_setname_np(name);
#else
  pthread_setname_np(pthread_self(), name);
#endif

  // Call the supplied thread start function handing it its parameters.
  function(parameter);

  return nullptr;
}

int Thread::Start(const char* name,
                  ThreadStartFunction function,
                  uword parameter) {
  ThreadStartData* data = new ThreadStartData(name, function, parameter);

  pthread_t tid;
  int result = pthread_create(&tid, nullptr, ThreadStart, data);
  if (result != 0) {
    delete data;
  }
  return result;
}

ThreadId Thread::GetCurrentThreadId() {
  return pthread_self();
}

ThreadJoinId Thread::GetCurrentThreadJoinId() {
  return pthread_self();
}

void Thread::Join(ThreadJoinId id) {
  int result = pthread_join(id, nullptr);
  VALIDATE_PTHREAD_RESULT(result);
}

bool Thread::Compare(ThreadId a, ThreadId b) {
  return pthread_equal(a, b) != 0;
}

Mutex::Mutex() {
  int result = pthread_mutex_init(&mutex_, nullptr);
  VALIDATE_PTHREAD_RESULT(result);

#if defined(DEBUG)
  owner_ = kInvalidThreadId;
#endif  // defined(DEBUG)
}

Mutex::~Mutex() {
  DEBUG_ASSERT(owner_ == kInvalidThreadId);

  int result = pthread_mutex_destroy(&mutex_);
  VALIDATE_PTHREAD_RESULT(result);
}

void Mutex::Lock() {
  int result = pthread_mutex_lock(&mutex_);
  ASSERT_PTHREAD_SUCCESS(result);
  CheckUnheldAndMark();
}

bool Mutex::TryLock() {
  int result = pthread_mutex_trylock(&mutex_);
  // Return false if the lock is busy and locking failed.
  if (result == EBUSY) {
    return false;
  }
  ASSERT_PTHREAD_SUCCESS(result);
  CheckUnheldAndMark();
  return true;
}

void Mutex::Unlock() {
  CheckHeldAndUnmark();
  int result = pthread_mutex_unlock(&mutex_);
  ASSERT_PTHREAD_SUCCESS(result);
}

Monitor::Monitor() {
  int result = pthread_mutex_init(&mutex_, nullptr);
  VALIDATE_PTHREAD_RESULT(result);

#if defined(OS_MACOS)
  result = pthread_cond_init(&cond_, nullptr);
  VALIDATE_PTHREAD_RESULT(result);
#else
  pthread_condattr_t cond_attr;
  result = pthread_condattr_init(&cond_attr);
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_cond_init(&cond_, &cond_attr);
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_condattr_destroy(&cond_attr);
  VALIDATE_PTHREAD_RESULT(result);
#endif

#if defined(DEBUG)
  owner_ = kInvalidThreadId;
#endif  // defined(DEBUG)
}

Monitor::~Monitor() {
  DEBUG_ASSERT(owner_ == kInvalidThreadId);

  int result = pthread_mutex_destroy(&mutex_);
  VALIDATE_PTHREAD_RESULT(result);

  result = pthread_cond_destroy(&cond_);
  VALIDATE_PTHREAD_RESULT(result);
}

bool Monitor::TryEnter() {
  int result = pthread_mutex_trylock(&mutex_);
  // Return false if the lock is busy and locking failed.
  if (result == EBUSY) {
    return false;
  }
  ASSERT_PTHREAD_SUCCESS(result);
  CheckUnheldAndMark();
  return true;
}

void Monitor::Enter() {
  int result = pthread_mutex_lock(&mutex_);
  VALIDATE_PTHREAD_RESULT(result);
  CheckUnheldAndMark();
}

void Monitor::Exit() {
  CheckHeldAndUnmark();
  int result = pthread_mutex_unlock(&mutex_);
  VALIDATE_PTHREAD_RESULT(result);
}

void Monitor::Wait() {
  CheckHeldAndUnmark();
  int result = pthread_cond_wait(&cond_, &mutex_);
  VALIDATE_PTHREAD_RESULT(result);
  CheckUnheldAndMark();
}

Monitor::WaitResult Monitor::WaitUntilNanos(int64_t deadline) {
  CheckHeldAndUnmark();

  Monitor::WaitResult retval = kNotified;
  struct timespec ts;
#if defined(OS_MACOS)
  // Convert to timeout since pthread_cond_timedwait uses the wrong clock.
  int64_t now = OS::CurrentMonotonicNanos();
  if (now >= deadline) {
    return kTimedOut;
  }
  int64_t timeout = deadline - now;
  int64_t secs = timeout / kNanosecondsPerSecond;
  int64_t nanos = timeout % kNanosecondsPerSecond;
#else
  int64_t secs = deadline / kNanosecondsPerSecond;
  int64_t nanos = deadline % kNanosecondsPerSecond;
#endif
  if (secs > kMaxInt32) {
    // Avoid truncation of overly large timeout values.
    secs = kMaxInt32;
  }
  ts.tv_sec = static_cast<int32_t>(secs);
  ts.tv_nsec = static_cast<long>(nanos);  // NOLINT (long used in timespec).
#if defined(OS_MACOS)
  int result =
      pthread_cond_timedwait_relative_np(&cond_, &mutex_, &ts);
#else
  int result = pthread_cond_timedwait(&cond_, &mutex_, &ts);
#endif
  ASSERT((result == 0) || (result == ETIMEDOUT));
  if (result == ETIMEDOUT) {
    retval = kTimedOut;
  }

  CheckUnheldAndMark();
  return retval;
}

void Monitor::Notify() {
  DEBUG_ASSERT(IsOwnedByCurrentThread());
  int result = pthread_cond_signal(&cond_);
  VALIDATE_PTHREAD_RESULT(result);
}

void Monitor::NotifyAll() {
  DEBUG_ASSERT(IsOwnedByCurrentThread());
  int result = pthread_cond_broadcast(&cond_);
  VALIDATE_PTHREAD_RESULT(result);
}

}  // namespace psoup

#endif  // defined(OS_ANDROID)  || defined(OS_FUCHSIA) || defined(OS_LINUX) ||
        // defined(OS_MACOS)
