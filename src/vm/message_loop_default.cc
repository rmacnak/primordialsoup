// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if !defined(OS_FUCHSIA)

#include "vm/message_loop.h"

#include <signal.h>

#include "vm/lockers.h"
#include "vm/os.h"

namespace psoup {

DefaultMessageLoop::DefaultMessageLoop(Isolate* isolate)
    : MessageLoop(isolate),
      monitor_(),
      head_(NULL),
      tail_(NULL),
      wakeup_(0) {}

DefaultMessageLoop::~DefaultMessageLoop() {}

intptr_t DefaultMessageLoop::AwaitSignal(intptr_t handle,
                                         intptr_t signals,
                                         int64_t deadline) {
  UNIMPLEMENTED();
  return 0;
}

void DefaultMessageLoop::CancelSignalWait(intptr_t wait_id) {
  UNIMPLEMENTED();
}

void DefaultMessageLoop::MessageEpilogue(int64_t new_wakeup) {
  wakeup_ = new_wakeup;

  if ((open_ports_ == 0) && (wakeup_ == 0)) {
    Exit(0);
  }
}

void DefaultMessageLoop::Exit(intptr_t exit_code) {
  exit_code_ = exit_code;
  isolate_ = NULL;
}

void DefaultMessageLoop::PostMessage(IsolateMessage* message) {
  MonitorLocker locker(&monitor_);

  if (head_ == NULL) {
    head_ = tail_ = message;
    locker.Notify();
  } else {
    tail_->next_ = message;
    tail_ = message;
  }
}

IsolateMessage* DefaultMessageLoop::WaitMessage() {
  MonitorLocker locker(&monitor_);

  if (head_ == NULL) {
    if (wakeup_ == 0) {
      locker.Wait();
    } else {
      locker.WaitUntilNanos(wakeup_);
    }
    if (head_ == NULL) {
      return NULL;
    }
  }

  IsolateMessage* message = head_;
  head_ = message->next_;

  if (head_ == NULL) {
    tail_ = NULL;
  }

  return message;
}

intptr_t DefaultMessageLoop::Run() {
  while (isolate_ != NULL) {
    IsolateMessage* message = WaitMessage();
    if (message == NULL) {
      DispatchWakeup();
    } else {
      DispatchMessage(message);
    }
  }

  if (open_ports_ > 0) {
    PortMap::CloseAllPorts(this);
  }

  while (head_ != NULL) {
    IsolateMessage* message = head_;
    head_ = message->next_;
    delete message;
  }

  return exit_code_;
}

void DefaultMessageLoop::Interrupt() {
  MonitorLocker locker(&monitor_);
  Exit(SIGINT);
  locker.Notify();
}

}  // namespace psoup

#endif  // !defined(OS_FUCHSIA)
