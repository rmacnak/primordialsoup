// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_MESSAGE_LOOP_FUCHSIA_H_
#define VM_MESSAGE_LOOP_FUCHSIA_H_

#if !defined(VM_MESSAGE_LOOP_H_)
#error Do not include message_loop_fuchsia.h directly; use message_loop.h \
  instead.
#endif

#include "lib/fsl/tasks/message_loop.h"

#include "vm/port.h"

namespace psoup {

#define PlatformMessageLoop FuchsiaMessageLoop

class FuchsiaMessageLoop : public MessageLoop, private fsl::MessageLoopHandler {
 public:
  explicit FuchsiaMessageLoop(Isolate* isolate);
  ~FuchsiaMessageLoop();

  void PostMessage(IsolateMessage* message);
  intptr_t AwaitSignal(intptr_t handle, intptr_t signals, int64_t deadline);
  void CancelSignalWait(intptr_t wait_id);
  void AdjustWakeup(int64_t new_wakeup);

  void Run();
  void Interrupt();

 private:
  void OnHandleReady(zx_handle_t handle, zx_signals_t pending, uint64_t count);
  void OnHandleError(zx_handle_t handle, zx_status_t error);

  fsl::MessageLoop* loop_;
  zx_handle_t timer_;

  DISALLOW_COPY_AND_ASSIGN(FuchsiaMessageLoop);
};

}  // namespace psoup

#endif  // VM_MESSAGE_LOOP_FUCHSIA_H_
