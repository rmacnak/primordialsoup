// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_MESSAGE_LOOP_H_
#define VM_MESSAGE_LOOP_H_

#include "vm/port.h"

namespace psoup {

class Isolate;

class IsolateMessage {
 public:
  IsolateMessage(Port dest, uint8_t* data, intptr_t length)
      : next_(NULL), dest_(dest), data_(data), length_(length) {}

  ~IsolateMessage() { free(data_); }

  Port dest_port() const { return dest_; }
  uint8_t* data() const { return data_; }
  intptr_t length() const { return length_; }

 private:
  friend class MessageLoop;
  friend class DefaultMessageLoop;
  friend class FuchsiaMessageLoop;

  IsolateMessage* next_;
  Port dest_;
  uint8_t* data_;
  intptr_t length_;

  DISALLOW_COPY_AND_ASSIGN(IsolateMessage);
};

class MessageLoop {
 public:
  explicit MessageLoop(Isolate* isolate);
  virtual ~MessageLoop();

  virtual void PostMessage(IsolateMessage* message) = 0;
  virtual intptr_t AwaitSignal(intptr_t handle,
                               intptr_t signals,
                               int64_t deadline) = 0;
  virtual void CancelSignalWait(intptr_t wait_id) = 0;
  virtual void AdjustWakeup(int64_t new_wakeup) = 0;

  virtual void Run() = 0;
  virtual void Interrupt() = 0;

  Port OpenPort();
  void ClosePort(Port p);

 protected:
  void DispatchMessage(IsolateMessage* message);
  void DispatchWakeup();
  void DispatchSignal(intptr_t handle,
                      intptr_t status,
                      intptr_t signals,
                      intptr_t count);

  Isolate* isolate_;
  intptr_t open_ports_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MessageLoop);
};

}  // namespace psoup

#if defined(TARGET_OS_FUCHSIA)
#include "vm/message_loop_fuchsia.h"
#else
#include "vm/message_loop_default.h"
#endif

#endif  // VM_MESSAGE_LOOP_H_
