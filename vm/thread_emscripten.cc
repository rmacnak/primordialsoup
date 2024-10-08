// Copyright (c) 2019, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if defined(OS_EMSCRIPTEN)

#include "vm/thread.h"

#include "vm/assert.h"

namespace psoup {

int Thread::Start(const char* name,
                  ThreadStartFunction function,
                  uword parameter) {
  UNREACHABLE();
}

ThreadId Thread::GetCurrentThreadId() {
  return 1;
}

ThreadJoinId Thread::GetCurrentThreadJoinId() {
  return 1;
}

void Thread::Join(ThreadJoinId id) {
  UNREACHABLE();
}

bool Thread::Compare(ThreadId a, ThreadId b) {
  return a == b;
}

Mutex::Mutex() {}

Mutex::~Mutex() {}

void Mutex::Lock() {}

bool Mutex::TryLock() {
  return true;
}

void Mutex::Unlock() {}

Monitor::Monitor() {}

Monitor::~Monitor() {}

bool Monitor::TryEnter() {
  return true;
}

void Monitor::Enter() {}

void Monitor::Exit() {}

void Monitor::Wait() {}

Monitor::WaitResult Monitor::WaitUntilNanos(int64_t deadline) {
  return kTimedOut;
}

void Monitor::Notify() {}

void Monitor::NotifyAll() {}

}  // namespace psoup

#endif  // defined(OS_EMSCRIPTEN)
