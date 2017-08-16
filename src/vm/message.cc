// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/message.h"

#include "vm/lockers.h"
#include "vm/os.h"

namespace psoup {

void MessageQueue::PostMessage(IsolateMessage* message) {
  MonitorLocker locker(&monitor_);

  if (head_ == NULL) {
    head_ = tail_ = message;
    locker.Notify();
  } else {
    tail_->next_ = message;
    tail_ = message;
  }
}


IsolateMessage* MessageQueue::Receive(int64_t deadline_micros) {
  IsolateMessage* message = NULL;
  MonitorLocker locker(&monitor_);

  if (head_ == NULL) {
    if (deadline_micros == 0) {
      locker.Wait();
    } else {
      locker.WaitUntilMicros(deadline_micros);
    }
  }
  if (head_ == NULL) {
    return NULL;
  }

  message = head_;
  head_ = message->next_;

  if (head_ == NULL) {
    tail_ = NULL;
  }

  return message;
}


void MessageQueue::Interrupt() {
  MonitorLocker locker(&monitor_);
  locker.Notify();
}

}  // namespace psoup
