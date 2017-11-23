// Copyright (c) 2017, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_MESSAGE_LOOP_DEFAULT_H_
#define VM_MESSAGE_LOOP_DEFAULT_H_

#if !defined(VM_MESSAGE_LOOP_H_)
#error Do not include message_loop_default.h directly; use message_loop.h \
  instead.
#endif

#include "vm/message_loop.h"
#include "vm/thread.h"

namespace psoup {

#define PlatformMessageLoop DefaultMessageLoop

class DefaultMessageLoop : public MessageLoop {
 public:
  explicit DefaultMessageLoop(Isolate* isolate);
  ~DefaultMessageLoop();

  void PostMessage(IsolateMessage* message);
  intptr_t AwaitSignal(intptr_t handle, intptr_t signals, int64_t deadline);
  void CancelSignalWait(intptr_t wait_id);
  void AdjustWakeup(int64_t new_wakeup);

  void Run();
  void Interrupt();

 private:
  IsolateMessage* WaitMessage();

  Monitor monitor_;
  IsolateMessage* head_;
  IsolateMessage* tail_;
  int64_t wakeup_;
  bool running_;

  DISALLOW_COPY_AND_ASSIGN(DefaultMessageLoop);
};

}  // namespace psoup

#endif  // VM_MESSAGE_LOOP_DEFAULT_H_
