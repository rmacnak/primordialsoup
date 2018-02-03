// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/message_loop.h"

#include "vm/isolate.h"
#include "vm/os.h"

namespace psoup {

MessageLoop::MessageLoop(Isolate* isolate)
    : isolate_(isolate), open_ports_(0), exit_code_(0) {}

MessageLoop::~MessageLoop() {}

void MessageLoop::DispatchMessage(IsolateMessage* message) {
  if (isolate_ == NULL) {
    delete message;
    return;
  }

  isolate_->ActivateMessage(message);
  delete message;
  isolate_->Interpret();
}

void MessageLoop::DispatchWakeup() {
  if (isolate_ == NULL) {
    return;
  }

  isolate_->ActivateWakeup();
  isolate_->Interpret();
}

Port MessageLoop::OpenPort() {
  open_ports_++;
  return PortMap::CreatePort(this);
}

void MessageLoop::ClosePort(Port port) {
  if (PortMap::ClosePort(port)) {
    open_ports_--;
  }
}

void MessageLoop::DispatchSignal(intptr_t handle,
                                 intptr_t status,
                                 intptr_t signals,
                                 intptr_t count) {
#if defined(OS_FUCHSIA)
  // Ignore signals from other users of the message loop.
#else
  UNIMPLEMENTED();
#endif
}

}  // namespace psoup
