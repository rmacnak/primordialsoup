// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if defined(OS_WINDOWS)

#include "vm/assert.h"
#include "vm/lockers.h"
#include "vm/os.h"
#include "vm/thread.h"

#include <process.h>  // NOLINT

namespace psoup {

// This flag is flipped by platform_win.cc when the process is exiting.
// TODO(zra): Remove once VM shuts down cleanly.
bool private_flag_windows_run_tls_destructors = true;

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

  // Create new Thread object and set as TLS for new thread.
  Thread* thread = new Thread();
  Thread::SetCurrent(thread);
  thread->set_name(name);

  // Call the supplied thread start function handing it its parameters.
  function(parameter);

  return 0;
}


int Thread::Start(const char* name,
                  ThreadStartFunction function,
                  uword parameter) {
  ThreadStartData* start_data = new ThreadStartData(name, function, parameter);
  uint32_t tid;
  uintptr_t thread = _beginthreadex(NULL, 0,
                                    ThreadEntry, start_data, 0, &tid);
  if (thread == -1L || thread == 0) {
#ifdef DEBUG
    fprintf(stderr, "_beginthreadex error: %d (%s)\n", errno, strerror(errno));
#endif
    return errno;
  }

  // Close the handle, so we don't leak the thread object.
  CloseHandle(reinterpret_cast<HANDLE>(thread));

  return 0;
}


const ThreadId Thread::kInvalidThreadId = 0;
const ThreadJoinId Thread::kInvalidThreadJoinId = NULL;


ThreadLocalKey Thread::CreateThreadLocal(ThreadDestructor destructor) {
  ThreadLocalKey key = TlsAlloc();
  if (key == kUnsetThreadLocalKey) {
    FATAL1("TlsAlloc failed %d", GetLastError());
  }
  ThreadLocalData::AddThreadLocal(key, destructor);
  return key;
}


void Thread::DeleteThreadLocal(ThreadLocalKey key) {
  ASSERT(key != kUnsetThreadLocalKey);
  BOOL result = TlsFree(key);
  if (!result) {
    FATAL1("TlsFree failed %d", GetLastError());
  }
  ThreadLocalData::RemoveThreadLocal(key);
}


ThreadId Thread::GetCurrentThreadId() {
  return ::GetCurrentThreadId();
}


ThreadId Thread::GetCurrentThreadTraceId() {
  return ::GetCurrentThreadId();
}


ThreadJoinId Thread::GetCurrentThreadJoinId(Thread* thread) {
  ASSERT(thread != NULL);
  // Make sure we're filling in the join id for the current thread.
  ThreadId id = GetCurrentThreadId();
  ASSERT(thread->id() == id);
  // Make sure the join_id_ hasn't been set, yet.
  DEBUG_ASSERT(thread->join_id_ == kInvalidThreadJoinId);
  HANDLE handle = OpenThread(SYNCHRONIZE, false, id);
  ASSERT(handle != NULL);
#if defined(DEBUG)
  thread->join_id_ = handle;
#endif
  return handle;
}


void Thread::Join(ThreadJoinId id) {
  HANDLE handle = static_cast<HANDLE>(id);
  ASSERT(handle != NULL);
  DWORD res = WaitForSingleObject(handle, INFINITE);
  CloseHandle(handle);
  ASSERT(res == WAIT_OBJECT_0);
}


intptr_t Thread::ThreadIdToIntPtr(ThreadId id) {
  ASSERT(sizeof(id) <= sizeof(intptr_t));
  return static_cast<intptr_t>(id);
}


ThreadId Thread::ThreadIdFromIntPtr(intptr_t id) {
  return static_cast<ThreadId>(id);
}


bool Thread::Compare(ThreadId a, ThreadId b) {
  return a == b;
}


void Thread::SetThreadLocal(ThreadLocalKey key, uword value) {
  ASSERT(key != kUnsetThreadLocalKey);
  BOOL result = TlsSetValue(key, reinterpret_cast<void*>(value));
  if (!result) {
    FATAL1("TlsSetValue failed %d", GetLastError());
  }
}


Mutex::Mutex() {
  InitializeSRWLock(&data_.lock_);
#if defined(DEBUG)
  // When running with assertions enabled we do track the owner.
  owner_ = Thread::kInvalidThreadId;
#endif  // defined(DEBUG)
}


Mutex::~Mutex() {
#if defined(DEBUG)
  // When running with assertions enabled we do track the owner.
  ASSERT(owner_ == Thread::kInvalidThreadId);
#endif  // defined(DEBUG)
}


void Mutex::Lock() {
  AcquireSRWLockExclusive(&data_.lock_);
  CheckUnheldAndMark();
}


bool Mutex::TryLock() {
  if (TryAcquireSRWLockExclusive(&data_.lock_)) {
    CheckUnheldAndMark();
    return true;
  }
  return false;
}


void Mutex::Unlock() {
  CheckHeldAndUnmark();
  ReleaseSRWLockExclusive(&data_.lock_);
}


Monitor::Monitor() {
  InitializeSRWLock(&data_.lock_);
  InitializeConditionVariable(&data_.cond_);

#if defined(DEBUG)
  // When running with assertions enabled we track the owner.
  owner_ = Thread::kInvalidThreadId;
#endif  // defined(DEBUG)
}


Monitor::~Monitor() {
#if defined(DEBUG)
  // When running with assertions enabled we track the owner.
  ASSERT(owner_ == Thread::kInvalidThreadId);
#endif  // defined(DEBUG)
}


bool Monitor::TryEnter() {
  if (TryAcquireSRWLockExclusive(&data_.lock_)) {
    CheckUnheldAndMark();
    return true;
  }
  return false;
}


void Monitor::Enter() {
  AcquireSRWLockExclusive(&data_.lock_);
  CheckUnheldAndMark();
}


void Monitor::Exit() {
  CheckHeldAndUnmark();
  ReleaseSRWLockExclusive(&data_.lock_);
}


void Monitor::Wait() {
  CheckHeldAndUnmark();
  SleepConditionVariableSRW(&data_.cond_, &data_.lock_, INFINITE, 0);
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
  if (!SleepConditionVariableSRW(&data_.cond_, &data_.lock_, millis, 0)) {
    ASSERT(GetLastError() == ERROR_TIMEOUT);
    retval = kTimedOut;
  }

  CheckUnheldAndMark();
  return retval;
}


void Monitor::Notify() {
  // When running with assertions enabled we track the owner.
  DEBUG_ASSERT(IsOwnedByCurrentThread());
  WakeConditionVariable(&data_.cond_);
}


void Monitor::NotifyAll() {
  // When running with assertions enabled we track the owner.
  DEBUG_ASSERT(IsOwnedByCurrentThread());
  WakeAllConditionVariable(&data_.cond_);
}


void ThreadLocalData::AddThreadLocal(ThreadLocalKey key,
                                     ThreadDestructor destructor) {
  /*
  ASSERT(thread_locals_ != NULL);
  if (destructor == NULL) {
    // We only care about thread locals with destructors.
    return;
  }
  MutexLocker ml(mutex_, false);
#if defined(DEBUG)
  // Verify that we aren't added twice.
  for (intptr_t i = 0; i < thread_locals_->length(); i++) {
    const ThreadLocalEntry& entry = thread_locals_->At(i);
    ASSERT(entry.key() != key);
  }
#endif
  // Add to list.
  thread_locals_->Add(ThreadLocalEntry(key, destructor));
  */
}


void ThreadLocalData::RemoveThreadLocal(ThreadLocalKey key) {
  /*  ASSERT(thread_locals_ != NULL);
  MutexLocker ml(mutex_, false);
  intptr_t i = 0;
  for (; i < thread_locals_->length(); i++) {
    const ThreadLocalEntry& entry = thread_locals_->At(i);
    if (entry.key() == key) {
      break;
    }
  }
  if (i == thread_locals_->length()) {
    // Not found.
    return;
  }
  thread_locals_->RemoveAt(i);
  */
}


// This function is executed on the thread that is exiting. It is invoked
// by |OnDartThreadExit| (see below for notes on TLS destructors on Windows).
void ThreadLocalData::RunDestructors() {
  /*
  ASSERT(thread_locals_ != NULL);
  ASSERT(mutex_ != NULL);
  MutexLocker ml(mutex_, false);
  for (intptr_t i = 0; i < thread_locals_->length(); i++) {
    const ThreadLocalEntry& entry = thread_locals_->At(i);
    // We access the exiting thread's TLS variable here.
    void* p = reinterpret_cast<void*>(Thread::GetThreadLocal(entry.key()));
    // We invoke the constructor here.
    entry.destructor()(p);
  }
  */
}


Mutex* ThreadLocalData::mutex_ = NULL;
MallocGrowableArray<ThreadLocalEntry>* ThreadLocalData::thread_locals_ = NULL;


void ThreadLocalData::Startup() {
  mutex_ = new Mutex();
  /*
  thread_locals_ = new MallocGrowableArray<ThreadLocalEntry>();
  */
}


void ThreadLocalData::Shutdown() {
  if (mutex_ != NULL) {
    delete mutex_;
    mutex_ = NULL;
  }
  /*
  if (thread_locals_ != NULL) {
    delete thread_locals_;
    thread_locals_ = NULL;
  }
  */
}

}  // namespace psoup

// The following was adapted from Chromium:
// src/base/threading/thread_local_storage_win.cc

// Thread Termination Callbacks.
// Windows doesn't support a per-thread destructor with its
// TLS primitives.  So, we build it manually by inserting a
// function to be called on each thread's exit.
// This magic is from http://www.codeproject.com/threads/tls.asp
// and it works for VC++ 7.0 and later.

// Force a reference to _tls_used to make the linker create the TLS directory
// if it's not already there.  (e.g. if __declspec(thread) is not used).
// Force a reference to p_thread_callback_psoup to prevent whole program
// optimization from discarding the variable.
#ifdef _WIN64

#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:p_thread_callback_psoup")

#else  // _WIN64

#pragma comment(linker, "/INCLUDE:__tls_used")
#pragma comment(linker, "/INCLUDE:_p_thread_callback_psoup")

#endif  // _WIN64

// Static callback function to call with each thread termination.
void NTAPI OnDartThreadExit(PVOID module, DWORD reason, PVOID reserved) {
  if (!psoup::private_flag_windows_run_tls_destructors) {
    return;
  }
  // On XP SP0 & SP1, the DLL_PROCESS_ATTACH is never seen. It is sent on SP2+
  // and on W2K and W2K3. So don't assume it is sent.
  if (DLL_THREAD_DETACH == reason || DLL_PROCESS_DETACH == reason) {
    psoup::ThreadLocalData::RunDestructors();
  }
}

// .CRT$XLA to .CRT$XLZ is an array of PIMAGE_TLS_CALLBACK pointers that are
// called automatically by the OS loader code (not the CRT) when the module is
// loaded and on thread creation. They are NOT called if the module has been
// loaded by a LoadLibrary() call. It must have implicitly been loaded at
// process startup.
// By implicitly loaded, I mean that it is directly referenced by the main EXE
// or by one of its dependent DLLs. Delay-loaded DLL doesn't count as being
// implicitly loaded.
//
// See VC\crt\src\tlssup.c for reference.

// extern "C" suppresses C++ name mangling so we know the symbol name for the
// linker /INCLUDE:symbol pragma above.
extern "C" {
// The linker must not discard p_thread_callback_psoup.  (We force a reference
// to this variable with a linker /INCLUDE:symbol pragma to ensure that.) If
// this variable is discarded, the OnDartThreadExit function will never be
// called.
#ifdef _WIN64

// .CRT section is merged with .rdata on x64 so it must be constant data.
#pragma const_seg(".CRT$XLB")
// When defining a const variable, it must have external linkage to be sure the
// linker doesn't discard it.
extern const PIMAGE_TLS_CALLBACK p_thread_callback_psoup;
const PIMAGE_TLS_CALLBACK p_thread_callback_psoup = OnDartThreadExit;

// Reset the default section.
#pragma const_seg()

#else  // _WIN64

#pragma data_seg(".CRT$XLB")
PIMAGE_TLS_CALLBACK p_thread_callback_psoup = OnDartThreadExit;

// Reset the default section.
#pragma data_seg()

#endif  // _WIN64
}  // extern "C"

#endif  // defined(OS_WINDOWS)
