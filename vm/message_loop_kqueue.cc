// Copyright (c) 2018, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if defined(OS_MACOS)

#include "vm/message_loop.h"

#include <errno.h>
#include <signal.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "vm/lockers.h"
#include "vm/os.h"

namespace psoup {

KQueueMessageLoop::KQueueMessageLoop(Isolate* isolate)
    : MessageLoop(isolate),
      mutex_(),
      head_(nullptr),
      tail_(nullptr),
      wakeup_(0) {
  kqueue_fd_ = kqueue();
  if (kqueue_fd_ == -1) {
    FATAL("Failed to create kqueue");
  }

  struct kevent event;
  EV_SET(&event, 0, EVFILT_USER, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
  int result = kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr);
  if (result == -1) {
    FATAL("Failed to register notify event");
  }
}

KQueueMessageLoop::~KQueueMessageLoop() {
  close(kqueue_fd_);
}

intptr_t KQueueMessageLoop::AwaitSignal(intptr_t fd, intptr_t signals) {
  open_waits_++;

  static const intptr_t kMaxChanges = 2;
  struct kevent changes[kMaxChanges];
  intptr_t nchanges = 0;
  void* udata = reinterpret_cast<void*>(fd);
  if (signals & kReadEvent) {
    EV_SET(changes + nchanges, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, udata);
    nchanges++;
  }
  if (signals & kWriteEvent) {
    EV_SET(changes + nchanges, fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0,
           udata);
    nchanges++;
  }
  ASSERT(nchanges > 0);
  ASSERT(nchanges <= kMaxChanges);
  int status = kevent(kqueue_fd_, changes, nchanges, nullptr, 0, nullptr);
  if (status == -1) {
    FATAL("Failed to add to kqueue");
  }

  return fd;
}

void KQueueMessageLoop::CancelSignalWait(intptr_t wait_id) {
  open_waits_--;
  UNIMPLEMENTED();
}

void KQueueMessageLoop::MessageEpilogue(int64_t new_wakeup) {
  wakeup_ = new_wakeup;

  if ((open_ports_ == 0) && (open_waits_ == 0) && (wakeup_ == 0)) {
    Exit(0);
  }
}

void KQueueMessageLoop::Exit(intptr_t exit_code) {
  exit_code_ = exit_code;
  isolate_ = nullptr;
}

void KQueueMessageLoop::PostMessage(IsolateMessage* message) {
  MutexLocker locker(&mutex_);
  if (head_ == nullptr) {
    head_ = tail_ = message;
    Notify();
  } else {
    tail_->next_ = message;
    tail_ = message;
  }
}

void KQueueMessageLoop::Notify() {
  struct kevent event;
  EV_SET(&event, 0, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
  int result = kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr);
  if (result != 0) {
    FATAL("Failed to notify");
  }
}

IsolateMessage* KQueueMessageLoop::TakeMessages() {
  MutexLocker locker(&mutex_);
  IsolateMessage* message = head_;
  head_ = tail_ = nullptr;
  return message;
}

intptr_t KQueueMessageLoop::Run() {
  while (isolate_ != nullptr) {
    struct timespec* timeout = nullptr;
    struct timespec ts;
    if (wakeup_ == 0) {
      // nullptr timespec for infinite timeout.
    } else {
      int64_t nanos = wakeup_ - OS::CurrentMonotonicNanos();
      if (nanos < 0) {
        nanos = 0;
      }
      ts.tv_sec = nanos / kNanosecondsPerSecond;
      ts.tv_nsec = nanos % kNanosecondsPerSecond;
      timeout = &ts;
    }

    static const intptr_t kMaxEvents = 16;
    struct kevent events[kMaxEvents];
    int result = kevent(kqueue_fd_, nullptr, 0, events, kMaxEvents, timeout);
    if ((result == -1) && (errno != EINTR)) {
      FATAL("kevent failed");
    }

    if ((wakeup_ != 0) && (OS::CurrentMonotonicNanos() >= wakeup_)) {
      DispatchWakeup();
    }

    for (int i = 0; i < result; i++) {
      if ((events[i].flags & EV_ERROR) != 0) {
        FATAL("kevent failed\n");
      }
      if (events[i].filter == EVFILT_USER) {
        ASSERT(events[i].udata == nullptr);
      } else {
        intptr_t fd = events[i].ident;
        intptr_t pending = 0;
        intptr_t status = 0;
        if (events[i].filter == EVFILT_READ) {
          pending |= kReadEvent;
          if ((events[i].flags & EV_EOF) != 0) {
            if (events[i].fflags != 0) {
              pending = kErrorEvent;
              status = events[i].fflags;
            } else {
              pending |= kCloseEvent;
            }
          }
        } else if (events[i].filter == EVFILT_WRITE) {
          pending |= kWriteEvent;
          if ((events[i].flags & EV_EOF) != 0) {
            if (events[i].fflags != 0) {
              pending = kErrorEvent;
              status = events[i].fflags;
            }
          }
        } else {
          UNREACHABLE();
        }
        DispatchSignal(fd, status, pending, 0);
      }
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

void KQueueMessageLoop::Interrupt() {
  Exit(SIGINT);
  Notify();
}

}  // namespace psoup

#endif  // defined(OS_MACOS)
