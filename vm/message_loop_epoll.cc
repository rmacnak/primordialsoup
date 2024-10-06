// Copyright (c) 2018, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if defined(OS_ANDROID) || defined(OS_LINUX)

#include "vm/message_loop.h"

#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "vm/lockers.h"
#include "vm/os.h"

namespace psoup {

EPollMessageLoop::EPollMessageLoop(Isolate* isolate)
    : MessageLoop(isolate),
      mutex_(),
      head_(nullptr),
      tail_(nullptr),
      wakeup_(0) {
  event_fd_ = eventfd(0, EFD_CLOEXEC);
  if (event_fd_ == -1) {
    FATAL("Failed to create eventfd");
  }

  timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  if (timer_fd_ == -1) {
    FATAL("Failed to creater timerfd");
  }

  epoll_fd_ = epoll_create(64);
  if (epoll_fd_ == -1) {
    FATAL("Failed to create epoll");
  }

  struct epoll_event event;
  event.events = EPOLLIN;
  event.data.fd = event_fd_;
  int status = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &event);
  if (status == -1) {
    FATAL("Failed to add eventfd to epoll");
  }

  event.events = EPOLLIN;
  event.data.fd = timer_fd_;
  status = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_fd_, &event);
  if (status == -1) {
    FATAL("Failed to add timerfd to epoll");
  }
}

EPollMessageLoop::~EPollMessageLoop() {
  close(epoll_fd_);
  close(timer_fd_);
  close(event_fd_);
}

intptr_t EPollMessageLoop::AwaitSignal(intptr_t fd, intptr_t signals) {
  open_waits_++;

  struct epoll_event event;
  event.events = EPOLLRDHUP | EPOLLET;
  if (signals & kReadEvent) {
    event.events |= EPOLLIN;
  }
  if (signals & kWriteEvent) {
    event.events |= EPOLLOUT;
  }
  event.data.fd = fd;

  int status = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
  if (status == -1) {
    FATAL("Failed to add to epoll");
  }
  return fd;
}

void EPollMessageLoop::CancelSignalWait(intptr_t wait_id) {
  open_waits_--;
  UNIMPLEMENTED();
}

void EPollMessageLoop::MessageEpilogue(int64_t new_wakeup) {
  wakeup_ = new_wakeup;

  struct itimerspec it;
  memset(&it, 0, sizeof(it));
  if (new_wakeup != 0) {
    it.it_value.tv_sec = new_wakeup / kNanosecondsPerSecond;
    it.it_value.tv_nsec = new_wakeup % kNanosecondsPerSecond;
  }
  timerfd_settime(timer_fd_, TFD_TIMER_ABSTIME, &it, nullptr);

  if ((open_ports_ == 0) && (open_waits_ == 0) && (wakeup_ == 0)) {
    Exit(0);
  }
}

void EPollMessageLoop::Exit(intptr_t exit_code) {
  exit_code_ = exit_code;
  isolate_ = nullptr;
}

void EPollMessageLoop::PostMessage(IsolateMessage* message) {
  MutexLocker locker(&mutex_);
  if (head_ == nullptr) {
    head_ = tail_ = message;
    Notify();
  } else {
    tail_->next_ = message;
    tail_ = message;
  }
}

void EPollMessageLoop::Notify() {
  uint64_t value = 1;
  ssize_t written = write(event_fd_, &value, sizeof(value));
  if (written != sizeof(value)) {
    FATAL("Failed to notify");
  }
}

IsolateMessage* EPollMessageLoop::TakeMessages() {
  MutexLocker locker(&mutex_);
  IsolateMessage* message = head_;
  head_ = tail_ = nullptr;
  return message;
}

intptr_t EPollMessageLoop::Run() {
  while (isolate_ != nullptr) {
    static const intptr_t kMaxEvents = 16;
    struct epoll_event events[kMaxEvents];

    int result = epoll_wait(epoll_fd_, events, kMaxEvents, -1);
    if (result <= 0) {
      if ((errno != EWOULDBLOCK) && (errno != EINTR)) {
        FATAL("epoll_wait failed");
      }
    } else {
      for (int i = 0; i < result; i++) {
        if (events[i].data.fd == event_fd_) {
          uint64_t value;
          ssize_t red = read(event_fd_, &value, sizeof(value));
          if (red != sizeof(value)) {
            FATAL("Failed to read eventfd");
          }
        } else if (events[i].data.fd == timer_fd_) {
          uint64_t value;
          ssize_t red = read(timer_fd_, &value, sizeof(value));
          if (red != sizeof(value)) {
            FATAL("Failed to read timerfd");
          }
          DispatchWakeup();
        } else {
          intptr_t fd = events[i].data.fd;
          intptr_t pending = 0;
          if (events[i].events & EPOLLERR) {
            pending |= kErrorEvent;
          }
          if (events[i].events & EPOLLIN) {
            pending |= kReadEvent;
          }
          if (events[i].events & EPOLLOUT) {
            pending |= kWriteEvent;
          }
          if (events[i].events & EPOLLHUP) {
            pending |= kCloseEvent;
          }
          if (events[i].events & EPOLLRDHUP) {
            pending |= kCloseEvent;
          }
          DispatchSignal(fd, 0, pending, 0);
        }
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

void EPollMessageLoop::Interrupt() {
  Exit(SIGINT);
  Notify();
}

}  // namespace psoup

#endif  // defined(OS_ANDROID) || defined(OS_LINUX)
