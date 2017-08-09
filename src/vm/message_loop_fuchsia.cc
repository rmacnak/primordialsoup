// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if defined(TARGET_OS_FUCHSIA)

#include "vm/message_loop.h"

#include <magenta/status.h>
#include <magenta/syscalls.h>

#include "vm/os.h"

namespace psoup {

FuchsiaMessageLoop::FuchsiaMessageLoop(Isolate* isolate)
    : MessageLoop(isolate),
      loop_(mtl::MessageLoop::GetCurrent()),
      timer_(MX_HANDLE_INVALID) {
  mx_status_t result = mx_timer_create(0, MX_CLOCK_MONOTONIC, &timer_);
  ASSERT(result == MX_OK);
  loop_->AddHandler(this, timer_, MX_TIMER_SIGNALED);
}

FuchsiaMessageLoop::~FuchsiaMessageLoop() {
  mx_status_t result = mx_timer_cancel(timer_);
  ASSERT(result == MX_OK);
  mx_handle_close(timer_);
  ASSERT(result == MX_OK);
}

intptr_t FuchsiaMessageLoop::AwaitSignal(intptr_t handle,
                                         intptr_t signals,
                                         int64_t deadline) {
  // It seems odd that mtl should take a timeout here instead of deadline,
  // since the underlying mx_port_wait operates in terms of a deadline.
  // This is probably a straggler from the conversion.
  int64_t timeout = deadline - OS::CurrentMonotonicMicros();
  return loop_->AddHandler(this, handle, signals,
                           ftl::TimeDelta::FromMicroseconds(timeout));
}

void FuchsiaMessageLoop::OnHandleReady(mx_handle_t handle,
                                       mx_signals_t pending,
                                       uint64_t count) {
  if (handle == timer_) {
    DispatchWakeup();
  } else {
    DispatchSignal(handle, MX_OK, pending, count);
  }
}

void FuchsiaMessageLoop::OnHandleError(mx_handle_t handle, mx_status_t error) {
  DispatchSignal(handle, error, 0, 0);
}

void FuchsiaMessageLoop::CancelSignalWait(intptr_t wait_id) {
  loop_->RemoveHandler(wait_id);
}

void FuchsiaMessageLoop::AdjustWakeup(int64_t new_wakeup_micros) {
  mx_time_t new_wakeup = new_wakeup_micros * 1000;
  if (new_wakeup == 0) {
    mx_status_t result = mx_timer_cancel(timer_);
    ASSERT(result == MX_OK);
  } else {
    mx_status_t result = mx_timer_start(timer_, new_wakeup, 0, 0);
    ASSERT(result == MX_OK);
  }
}

void FuchsiaMessageLoop::PostMessage(IsolateMessage* message) {
  loop_->task_runner()->PostTask([this, message] { DispatchMessage(message); });
}

void FuchsiaMessageLoop::Run() {
  loop_->Run();

  if (open_ports_ > 0) {
    PortMap::CloseAllPorts(this);
  }
}

void FuchsiaMessageLoop::Interrupt() {
  loop_->QuitNow();
}

}  // namespace psoup

#endif  // defined(TARGET_OS_FUCHSIA)
