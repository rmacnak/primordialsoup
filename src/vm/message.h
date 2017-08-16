// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_MESSAGE_H_
#define VM_MESSAGE_H_

#include "vm/os_thread.h"
#include "vm/lockers.h"
#include "vm/port.h"

namespace psoup {

class IsolateMessage {
 public:
  IsolateMessage(Port dest, uint8_t* data, intptr_t length)
    : next_(NULL),
      dest_(dest),
      data_(data),
      length_(length) {
  }

  ~IsolateMessage() {
    free(data_);
  }

  Port dest_port() const { return dest_; }
  uint8_t* data() const { return data_; }
  intptr_t length() const { return length_; }

 private:
  friend class MessageQueue;

  IsolateMessage* next_;
  Port dest_;
  uint8_t* data_;
  intptr_t length_;

  DISALLOW_COPY_AND_ASSIGN(IsolateMessage);
};


class MessageQueue {
 public:
  MessageQueue() : monitor_(), head_(NULL), tail_(NULL) {}

  void PostMessage(IsolateMessage* msg);
  IsolateMessage* Receive(int64_t deadline_micros);
  void Interrupt();

 private:
  Monitor monitor_;
  IsolateMessage* head_;
  IsolateMessage* tail_;

  DISALLOW_COPY_AND_ASSIGN(MessageQueue);
};

}  // namespace psoup

#endif  // VM_MESSAGE_H_
