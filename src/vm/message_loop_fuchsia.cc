// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if defined(OS_FUCHSIA)

#include "vm/message_loop.h"

#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "vm/os.h"

namespace psoup {

FuchsiaMessageLoop::FuchsiaMessageLoop(Isolate* isolate)
    : MessageLoop(isolate),
      loop_(fsl::MessageLoop::GetCurrent()),
      timer_(ZX_HANDLE_INVALID),
      wakeup_(0) {
  zx_status_t result =
      zx_timer_create(ZX_TIMER_SLACK_LATE, ZX_CLOCK_MONOTONIC, &timer_);
  ASSERT(result == ZX_OK);
  loop_->AddHandler(this, timer_, ZX_TIMER_SIGNALED);
}

FuchsiaMessageLoop::~FuchsiaMessageLoop() {
  zx_status_t result = zx_timer_cancel(timer_);
  ASSERT(result == ZX_OK);
  result = zx_handle_close(timer_);
  ASSERT(result == ZX_OK);
}

intptr_t FuchsiaMessageLoop::AwaitSignal(intptr_t handle,
                                         intptr_t signals,
                                         int64_t deadline) {
  // It seems odd that fsl should take a timeout here instead of deadline,
  // since the underlying zx_port_wait operates in terms of a deadline.
  // This is probably a straggler from the conversion.
  open_waits_++;
  int64_t timeout = deadline - OS::CurrentMonotonicNanos();
  return loop_->AddHandler(this, handle, signals,
                           fxl::TimeDelta::FromNanoseconds(timeout));
}

void FuchsiaMessageLoop::OnHandleReady(zx_handle_t handle,
                                       zx_signals_t pending,
                                       uint64_t count) {
  if (handle == timer_) {
    DispatchWakeup();
  } else {
    DispatchSignal(handle, ZX_OK, pending, count);
  }
}

void FuchsiaMessageLoop::OnHandleError(zx_handle_t handle, zx_status_t error) {
  DispatchSignal(handle, error, 0, 0);
}

void FuchsiaMessageLoop::CancelSignalWait(intptr_t wait_id) {
  loop_->RemoveHandler(wait_id);
  open_waits_--;
}

void FuchsiaMessageLoop::MessageEpilogue(int64_t new_wakeup) {
  wakeup_ = new_wakeup;
  if (new_wakeup == 0) {
    zx_status_t result = zx_timer_cancel(timer_);
    ASSERT(result == ZX_OK);
  } else {
    zx_duration_t slack = ZX_MSEC(1);
    zx_status_t result = zx_timer_set(timer_, new_wakeup, slack);
    ASSERT(result == ZX_OK);
  }

  if ((open_ports_ == 0) && (open_waits_ == 0) && (wakeup_ == 0)) {
    Exit(0);
  }
}

void FuchsiaMessageLoop::Exit(intptr_t exit_code) {
  exit_code_ = exit_code;
  isolate_ = NULL;
  loop_->QuitNow();
}

void FuchsiaMessageLoop::PostMessage(IsolateMessage* message) {
  loop_->task_runner()->PostTask([this, message] { DispatchMessage(message); });
}

intptr_t FuchsiaMessageLoop::Run() {
  loop_->Run();

  if (open_ports_ > 0) {
    PortMap::CloseAllPorts(this);
  }

  return exit_code_;
}

void FuchsiaMessageLoop::Interrupt() {
  const intptr_t SIGINT = 2;
  Exit(SIGINT);
}

}  // namespace psoup

#endif  // defined(OS_FUCHSIA)
