// Copyright (c) 2018, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if defined(OS_WINDOWS)

#include "vm/message_loop.h"

#include <signal.h>

#include "vm/lockers.h"
#include "vm/os.h"

namespace psoup {

IOCPMessageLoop::IOCPMessageLoop(Isolate* isolate)
    : MessageLoop(isolate),
      mutex_(),
      head_(nullptr),
      tail_(nullptr),
      wakeup_(0) {
  completion_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
  if (completion_port_ == nullptr) {
    FATAL("Failed to create IOCP");
  }
}

IOCPMessageLoop::~IOCPMessageLoop() {
  CloseHandle(completion_port_);
}

intptr_t IOCPMessageLoop::AwaitSignal(intptr_t handle, intptr_t signals) {
  open_waits_++;
  UNIMPLEMENTED();
  return 0;
}

void IOCPMessageLoop::CancelSignalWait(intptr_t wait_id) {
  open_waits_--;
  UNIMPLEMENTED();
}

void IOCPMessageLoop::MessageEpilogue(int64_t new_wakeup) {
  wakeup_ = new_wakeup;

  if ((open_ports_ == 0) && (open_waits_ == 0) && (wakeup_ == 0)) {
    Exit(0);
  }
}

void IOCPMessageLoop::Exit(intptr_t exit_code) {
  exit_code_ = exit_code;
  isolate_ = nullptr;
}

void IOCPMessageLoop::PostMessage(IsolateMessage* message) {
  MutexLocker locker(&mutex_);
  if (head_ == nullptr) {
    head_ = tail_ = message;
    Notify();
  } else {
    tail_->next_ = message;
    tail_ = message;
  }
}

void IOCPMessageLoop::Notify() {
  ULONG_PTR key = reinterpret_cast<ULONG_PTR>(this);
  BOOL ok = PostQueuedCompletionStatus(completion_port_, 0, key, nullptr);
  if (!ok) {
    FATAL("PostQueuedCompletionStatus failed");
  }
}

IsolateMessage* IOCPMessageLoop::TakeMessages() {
  MutexLocker locker(&mutex_);
  IsolateMessage* message = head_;
  head_ = tail_ = nullptr;
  return message;
}

intptr_t IOCPMessageLoop::Run() {
  while (isolate_ != nullptr) {
    DWORD timeout;
    if (wakeup_ == 0) {
      timeout = INFINITE;
    } else {
      int64_t timeout64 =
          (wakeup_ - OS::CurrentMonotonicNanos()) / kNanosecondsPerMillisecond;
      if (timeout64 < 0) {
        timeout64 = 0;
      }
      if (timeout64 > kMaxInt32) {
        timeout64 = kMaxInt32;
      }
      COMPILE_ASSERT(sizeof(int32_t) == sizeof(DWORD));
      timeout = static_cast<DWORD>(timeout64);
    }

    DWORD bytes;
    ULONG_PTR key;
    OVERLAPPED* overlapped;
    BOOL ok = GetQueuedCompletionStatus(completion_port_, &bytes, &key,
                                        &overlapped, timeout);
    if (!ok && (overlapped == nullptr)) {
      // Timeout.
      if ((wakeup_ != 0) && (OS::CurrentMonotonicNanos() >= wakeup_)) {
        DispatchWakeup();
      }
    } else if (key == reinterpret_cast<ULONG_PTR>(this)) {
      // Interrupt: will check messages below.
    } else {
      UNIMPLEMENTED();
    }

    IsolateMessage* message = TakeMessages();
    while (message != nullptr) {
      IsolateMessage* next = message->next_;
      DispatchMessage(message);
      message = next;
    }
  }

  if (open_ports_ > 0) {
    PortMap::CloseAllPorts(this);
  }

  while (head_ != nullptr) {
    IsolateMessage* message = head_;
    head_ = message->next_;
    delete message;
  }

  return exit_code_;
}

void IOCPMessageLoop::Interrupt() {
  Exit(SIGINT);
  Notify();
}

}  // namespace psoup

#endif  // defined(OS_WINDOWS)
