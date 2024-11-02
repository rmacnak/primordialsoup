// Copyright (c) 2018, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if defined(OS_ANDROID) || defined(OS_LINUX)

#include "vm/message_loop.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/sched.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include "vm/flags.h"
#include "vm/lockers.h"
#include "vm/os.h"

namespace psoup {

static constexpr intptr_t kPipeReadEnd = 0;
static constexpr intptr_t kPipeWriteEnd = 1;

static pid_t fork_pidfd(int* pidfd) {
  struct clone_args args = {};
  args.flags = CLONE_PIDFD;
  args.pidfd = reinterpret_cast<uint64_t>(pidfd);
  args.exit_signal = SIGCHLD;
  return syscall(SYS_clone3, &args, sizeof(args));
}

static bool SetBlockingHelper(intptr_t fd, bool blocking) {
  intptr_t status;
  status = fcntl(fd, F_GETFL);
  if (status < 0) {
    return false;
  }
  status = blocking ? (status & ~O_NONBLOCK) : (status | O_NONBLOCK);
  if (fcntl(fd, F_SETFL, status) < 0) {
    return false;
  }
  return true;
}

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
    FATAL("Failed to create timerfd");
  }

  epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd_ == -1) {
    FATAL("Failed to create epoll");
  }

  struct epoll_event event;
  event.events = EPOLLIN;
  event.data.ptr = &event_fd_;
  int status = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &event);
  if (status == -1) {
    FATAL("Failed to add eventfd to epoll");
  }

  event.events = EPOLLIN;
  event.data.ptr = &timer_fd_;
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

intptr_t EPollMessageLoop::AwaitSignal(intptr_t handle_id, intptr_t signals) {
  Handle* handle = handles_.Lookup(handle_id);
  if (handle == nullptr) {
    return EBADF;  // No such handle.
  }
  int fd = handle->fd();
  int subscribed = handle->subscribed_signals();
  if (signals == subscribed) {
    return 0;
  }
  int op;
  if (subscribed == 0) {
    op = EPOLL_CTL_ADD;
  } else if (signals == 0) {
    op = EPOLL_CTL_DEL;
  } else {
    op = EPOLL_CTL_MOD;
  }
  struct epoll_event event;
  event.events = EPOLLRDHUP;
  // TODO(rmacnak): Should we use EPOLLET?
  if (signals & kReadEvent) {
    event.events |= EPOLLIN;
  }
  if (signals & kWriteEvent) {
    event.events |= EPOLLOUT;
  }
  event.data.ptr = handle;
  int status = epoll_ctl(epoll_fd_, op, fd, &event);
  if (status == -1) {
    return errno;
  }

  if (subscribed == 0) {
    open_waits_++;
  } else if (signals == 0) {
    open_waits_--;
  }
  handle->set_subscribed_signals(signals);
  return 0;
}

void EPollMessageLoop::CancelSignalWait(intptr_t wait_id) {
  open_waits_--;
  UNIMPLEMENTED();
}

void EPollMessageLoop::MessageEpilogue(int64_t new_wakeup) {
  if (wakeup_ != new_wakeup) {
    wakeup_ = new_wakeup;

    struct itimerspec it;
    memset(&it, 0, sizeof(it));
    if (new_wakeup != 0) {
      it.it_value.tv_sec = new_wakeup / kNanosecondsPerSecond;
      it.it_value.tv_nsec = new_wakeup % kNanosecondsPerSecond;
    }
    int result = timerfd_settime(timer_fd_, TFD_TIMER_ABSTIME, &it, nullptr);
    ASSERT(result == 0);
  }

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
    struct epoll_event event;
    int result = epoll_wait(epoll_fd_, &event, 1, -1);
    if (result <= 0) {
      if ((errno != EWOULDBLOCK) && (errno != EINTR)) {
        FATAL("epoll_wait failed");
      }
    } else {
      if (event.data.ptr == &event_fd_) {
        uint64_t value;
        ssize_t red;
        do {
          red = read(event_fd_, &value, sizeof(value));
        } while (red == -1 && errno == EINTR);
        ASSERT(red == sizeof(value));
        // Interrupt: will check messages below.
      } else if (event.data.ptr == &timer_fd_) {
        uint64_t value;
        ssize_t red;
        do {
          red = read(timer_fd_, &value, sizeof(value));
        } while (red == -1 && errno == EINTR);
        ASSERT(red == sizeof(value));
        ASSERT(value == 1);
        DispatchWakeup();
      } else {
        RespondToEvent(event);
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

void EPollMessageLoop::RespondToEvent(const struct epoll_event& event) {
  Handle* handle = reinterpret_cast<Handle*>(event.data.ptr);
  intptr_t handle_id = handle->id();
  intptr_t status = 0;
  intptr_t pending = 0;
  intptr_t count = 0;
  if (event.events & EPOLLERR) {
    pending |= kCloseEvent;
  }
  if (event.events & EPOLLIN) {
    pending |= kReadEvent;
  }
  if (event.events & EPOLLOUT) {
    pending |= kWriteEvent;
  }
  if (event.events & EPOLLHUP) {
    pending |= kCloseEvent;
  }
  if (event.events & EPOLLRDHUP) {
    pending |= kCloseEvent;
  }
  if (handle->type() == Handle::kProcess) {
    Process* process = static_cast<Process*>(handle);
    pid_t pid = process->pid();

    // The pidfd asserts readable when the child exits, but doesn't
    // provide anything to read.
    int wait_status = 0;
    pid_t wait_result;
    do {
      wait_result = waitpid(pid, &wait_status, 0);
    } while (wait_result == -1 && errno == EINTR);
    ASSERT(wait_result == pid);

    if (WIFEXITED(wait_status)) {
      status = WEXITSTATUS(wait_status);
      ASSERT(status >= 0);
    } else {
      ASSERT(WIFSIGNALED(wait_status));
      status = -WTERMSIG(wait_status);
      ASSERT(status < 0);
    }
    pending = kCloseEvent;

    open_waits_--;
    handles_.Remove(handle_id);
    delete handle;
  }
  if (pending & kReadEvent) {
    int available = 0;
    if (ioctl(handle->fd(), FIONREAD, &available) == 0) {
      count = available;
    }
  }
  DispatchSignal(handle_id, status, pending, count);
}

void EPollMessageLoop::Interrupt() {
  Exit(SIGINT);
  Notify();
}

intptr_t EPollMessageLoop::StartProcess(intptr_t options,
                                        char** argv,
                                        char** env,
                                        const char* cwd,
                                        intptr_t* process_out,
                                        intptr_t* stdin_out,
                                        intptr_t* stdout_out,
                                        intptr_t* stderr_out) {
  pid_t pid;
  int pipes[4][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
  int pidfd = -1;
  for (int i = 0; i < 4; i++) {
    if (pipe2(pipes[i], O_CLOEXEC) != 0) {
      goto parentError;
    }
  }
  if (!SetBlockingHelper(pipes[0][kPipeWriteEnd], false)) {
    goto parentError;
  }
  if (!SetBlockingHelper(pipes[1][kPipeReadEnd], false)) {
    goto parentError;
  }
  if (!SetBlockingHelper(pipes[2][kPipeReadEnd], false)) {
    goto parentError;
  }

  pid = fork_pidfd(&pidfd);
  if (pid == 0) {
    // The child process.

    // Setup stdio.
    if (dup2(pipes[0][kPipeReadEnd], STDIN_FILENO) == -1) {
      goto childError;
    }
    if (dup2(pipes[1][kPipeWriteEnd], STDOUT_FILENO) == -1) {
      goto childError;
    }
    if (dup2(pipes[2][kPipeWriteEnd], STDERR_FILENO) == -1) {
      goto childError;
    }

    // Setup environment.
    if (env != nullptr) {
      environ = env;
    }

    // Setup working directory.
    if (cwd != nullptr) {
      if (chdir(cwd) != 0) {
        goto childError;
      }
    }

    // Reset signals.
    for (int i = 1; i < 32; i++) {
      if (i == SIGKILL) continue;  // Can't be caught.
      if (i == SIGSTOP) continue;  // Can't be caught.
      if (signal(i, SIG_DFL) == SIG_ERR) {
        goto childError;
      }
    }
    sigset_t set;
    sigemptyset(&set);
    if (pthread_sigmask(SIG_SETMASK, &set, nullptr) != 0) {
      goto childError;
    }

    // Finally exec. On success, does not return.
    execvp(argv[0], argv);

   childError:
    int error = errno;
    ssize_t n;
    do {
      n = write(pipes[3][kPipeWriteEnd], &error, sizeof(error));
    } while (n == -1 && errno == EINTR);
    if (n == -1 && errno == EPIPE) {
      // Parent died.
    } else {
      ASSERT(n == sizeof(errno));
    }
    _exit(127);
  } else if (pid > 0) {
    // The parent process.

    // Close the child end of the stdio and status pipes.
    close(pipes[0][kPipeReadEnd]);
    pipes[0][kPipeReadEnd] = -1;
    close(pipes[1][kPipeWriteEnd]);
    pipes[1][kPipeWriteEnd] = -1;
    close(pipes[2][kPipeWriteEnd]);
    pipes[2][kPipeWriteEnd] = -1;
    close(pipes[3][kPipeWriteEnd]);
    pipes[3][kPipeWriteEnd] = -1;

    // Wait for child to either successfully exec (which closes the status pipe)
    // or report an error.
    int r, childError;
    do {
      r = read(pipes[3][kPipeReadEnd], &childError, sizeof(childError));
    } while (r == -1 && errno == EINTR);
    if (r == 0) {
      // EOF: child exec succeeded.
    } else if (r == sizeof(childError)) {
      // Child setup or exec failed and managed to report it cleanly.
      int wait_status = 0;
      pid_t wait_result;
      do {
        wait_result = waitpid(pid, &wait_status, 0);  // Reap child.
      } while (wait_result == -1 && errno == EINTR);
      ASSERT(wait_result == pid);
      errno = childError;
      goto parentError;
    } else if (r == -1 && errno == EPIPE) {
      // Some other child failure?
      int wait_status = 0;
      pid_t wait_result;
      do {
        wait_result = waitpid(pid, &wait_status, 0);  // Reap child.
      } while (wait_result == -1 && errno == EINTR);
      ASSERT(wait_result == pid);
      errno = EPIPE;
      goto parentError;
    } else {
      UNREACHABLE();
    }
    close(pipes[3][kPipeReadEnd]);

    Process* process = new Process(pid, pidfd);

    // Subscribe to process exit.
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = process;
    int status = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, pidfd, &event);
    if (status == -1) {
      FATAL("Failed to add to epoll");
    }
    open_waits_++;

    *process_out = handles_.Register(process);
    *stdin_out = handles_.Register(new Handle(Handle::kPipe,
                                              pipes[0][kPipeWriteEnd]));
    *stdout_out = handles_.Register(new Handle(Handle::kPipe,
                                               pipes[1][kPipeReadEnd]));
    *stderr_out = handles_.Register(new Handle(Handle::kPipe,
                                               pipes[2][kPipeReadEnd]));
    return 0;
  } else {
    // Fork failed.
    goto parentError;
  }

 parentError:
  int status = errno;
  for (int i = 0; i < 4; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }
  close(pidfd);
  return status;
}

intptr_t EPollMessageLoop::Read(intptr_t handle_id,
                                uint8_t* buffer,
                                intptr_t buffer_size,
                                intptr_t* size_out) {
  Handle* handle = handles_.Lookup(handle_id);
  if (handle == nullptr) {
    return EBADF;  // No such handle.
  }
  int fd = handle->fd();

 retry:
  ssize_t n = read(fd, buffer, buffer_size);
  if (n == -1) {
    if (errno == EINTR) goto retry;
    if (errno == EAGAIN) {
      *size_out = 0;
      return 0;
    }
    return errno;
  }
  *size_out = n;
  return 0;
}

intptr_t EPollMessageLoop::Write(intptr_t handle_id,
                                 const uint8_t* buffer,
                                 intptr_t buffer_size,
                                 intptr_t* size_out) {
  Handle* handle = handles_.Lookup(handle_id);
  if (handle == nullptr) {
    return EBADF;  // No such handle.
  }
  int fd = handle->fd();

 retry:
  ssize_t n = write(fd, buffer, buffer_size);
  if (n == -1) {
    if (errno == EINTR) goto retry;
    return errno;
  }
  *size_out = n;
  return 0;
}

intptr_t EPollMessageLoop::Close(intptr_t handle_id) {
  Handle* handle = handles_.Lookup(handle_id);
  if (handle == nullptr) {
    return EBADF;  // No such handle.
  }

  handles_.Remove(handle_id);
  if (handle->subscribed_signals() != 0) {
    open_waits_--;
  }
  delete handle;
  return 0;
}

}  // namespace psoup

#endif  // defined(OS_ANDROID) || defined(OS_LINUX)
