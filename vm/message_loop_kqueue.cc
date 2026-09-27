// Copyright (c) 2018, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if defined(OS_MACOS)

#include "vm/message_loop.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "vm/lockers.h"
#include "vm/os.h"

namespace psoup {

static constexpr intptr_t kPipeReadEnd = 0;
static constexpr intptr_t kPipeWriteEnd = 1;

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

static bool SetCloseOnExec(int fd) {
  intptr_t status;
  status = fcntl(fd, F_GETFD);
  if (status < 0) {
    return false;
  }
  if ((status & FD_CLOEXEC) != 0) {
    return true;
  }
  status |= FD_CLOEXEC;
  if (fcntl(fd, F_SETFD, status) < 0) {
    return false;
  }
  return true;
}

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

intptr_t KQueueMessageLoop::AwaitSignal(intptr_t handle_id, intptr_t signals) {
  Handle* handle = handles_.Lookup(handle_id);
  if (handle == nullptr) {
    return EBADF;  // No such handle.
  }
  int fd = handle->fd();
  int subscribed = handle->subscribed_signals();
  if (signals == subscribed) {
    return 0;
  }

  static const intptr_t kMaxChanges = 2;
  struct kevent changes[kMaxChanges];
  intptr_t nchanges = 0;
  if ((signals & kReadEvent) != (subscribed & kReadEvent)) {
    EV_SET(changes + nchanges, fd, EVFILT_READ,
           (signals & kReadEvent) != 0 ? EV_ADD | EV_CLEAR : EV_DELETE,
           0, 0, handle);
    nchanges++;
  }
  if ((signals & kWriteEvent) != (subscribed & kWriteEvent)) {
    EV_SET(changes + nchanges, fd, EVFILT_WRITE,
           (signals & kWriteEvent) != 0 ? EV_ADD | EV_CLEAR : EV_DELETE,
           0, 0, handle);
    nchanges++;
  }
  ASSERT(nchanges > 0);
  ASSERT(nchanges <= kMaxChanges);
  int status = kevent(kqueue_fd_, changes, nchanges, nullptr, 0, nullptr);
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

    struct kevent event;
    int result = kevent(kqueue_fd_, nullptr, 0, &event, 1, timeout);
    if ((result == -1) && (errno != EINTR)) {
      FATAL("kevent failed");
    }

    if (result == 0) {
      // Timeout.
      DispatchWakeup();
    } else if (result == 1) {
      if (event.udata == nullptr) {
        ASSERT(event.filter == EVFILT_USER);
        // Interrupt: will check messages below.
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

void KQueueMessageLoop::RespondToEvent(const struct kevent& event) {
  Handle* handle = reinterpret_cast<Handle*>(event.udata);
  intptr_t handle_id = handle->id();
  ASSERT(handle_id != 0);
  intptr_t status = 0;
  intptr_t pending = 0;
  intptr_t count = 0;
  if (event.filter == EVFILT_READ) {
    pending |= kReadEvent;
    count = event.data;
    if ((event.flags & EV_EOF) != 0) {
      if (event.fflags != 0) {
        pending = kErrorEvent;
        status = event.fflags;
      } else {
        pending |= kCloseEvent;
        if (count == 0) {
          pending &= ~kReadEvent;
        }
      }
    }
  } else if (event.filter == EVFILT_WRITE) {
    pending |= kWriteEvent;
    count = event.data;
    if ((event.flags & EV_EOF) != 0) {
      if (event.fflags != 0) {
        pending = kErrorEvent;
        status = event.fflags;
      } else {
        pending = kCloseEvent;
      }
    }
  } else if (event.filter == EVFILT_PROC ||
             event.filter == EVFILT_USER) {
    ASSERT(handle->type() == Handle::kProcess);
    pid_t pid = event.ident;
    ASSERT(static_cast<Process*>(handle)->pid() == pid);

    // The NOTE_EXIT filter doesn't reap the process, so we need to wait
    // for it or we'll accumulate zombie processes.
    // It would seem we could use WNOHANG here, but that sometimes returns
    // 0 (no exited children), so apparently the NOTE_EXIT filter triggers
    // before the child is fully exited and available to waitpid.
    int wait_status;
    pid_t wait_result;
    do {
      wait_result = waitpid(pid, &wait_status, 0);
    } while (wait_result == -1 && errno == EINTR);
    ASSERT(wait_result == pid);

    if (event.filter == EVFILT_PROC) {
      ASSERT(wait_status == event.data);
    } else {
      ASSERT(status == 0);
    }

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
  } else {
    UNREACHABLE();
  }
  DispatchSignal(handle_id, status, pending, count);
}

void KQueueMessageLoop::Interrupt() {
  Exit(SIGINT);
  Notify();
}

intptr_t KQueueMessageLoop::StartProcess(intptr_t options,
                                         char** argv,
                                         char** env,
                                         const char* cwd,
                                         intptr_t* process_out,
                                         intptr_t* stdin_out,
                                         intptr_t* stdout_out,
                                         intptr_t* stderr_out) {
  int status;
  pid_t pid;
  int pipes[3][2] = {{-1, -1}, {-1, -1}, {-1, -1}};
  posix_spawn_file_actions_t facts = {};
  posix_spawnattr_t attr = {};
  for (int i = 0; i < 3; i++) {
    if (pipe(pipes[i]) != 0) goto failErrno;
    if (!SetCloseOnExec(pipes[i][0])) goto failErrno;
    if (!SetCloseOnExec(pipes[i][1])) goto failErrno;
  }
  if (!SetBlockingHelper(pipes[0][kPipeWriteEnd], false)) goto failErrno;
  if (!SetBlockingHelper(pipes[1][kPipeReadEnd], false)) goto failErrno;
  if (!SetBlockingHelper(pipes[2][kPipeReadEnd], false)) goto failErrno;

  status = posix_spawn_file_actions_init(&facts);
  if (status != 0) goto failStatus;
  status = posix_spawn_file_actions_adddup2(&facts,
                                            pipes[0][kPipeReadEnd],
                                            STDIN_FILENO);
  if (status != 0) goto failStatus;
  status = posix_spawn_file_actions_adddup2(&facts,
                                            pipes[1][kPipeWriteEnd],
                                            STDOUT_FILENO);
  if (status != 0) goto failStatus;
  status = posix_spawn_file_actions_adddup2(&facts,
                                            pipes[2][kPipeWriteEnd],
                                            STDERR_FILENO);
  if (status != 0) goto failStatus;
  if (cwd != nullptr) {
    status = posix_spawn_file_actions_addchdir(&facts, cwd);
    if (status != 0) goto failStatus;
  }

  status = posix_spawnattr_init(&attr);
  if (status != 0) goto failStatus;
  status = posix_spawnattr_setflags(&attr,
                                    POSIX_SPAWN_SETSIGMASK |
                                    POSIX_SPAWN_SETSIGDEF |
                                    POSIX_SPAWN_CLOEXEC_DEFAULT);
  if (status != 0) goto failStatus;
  sigset_t signals;
  sigemptyset(&signals);
  status = posix_spawnattr_setsigmask(&attr, &signals);
  if (status != 0) goto failStatus;
  sigfillset(&signals);
  status = posix_spawnattr_setsigdefault(&attr, &signals);
  if (status != 0) goto failStatus;

  status = posix_spawnp(&pid, argv[0], &facts, &attr, argv, env);
  if (status != 0) goto failStatus;
  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&facts);

  // Close the child end of the stdio pipes.
  close(pipes[0][kPipeReadEnd]);
  pipes[0][kPipeReadEnd] = -1;
  close(pipes[1][kPipeWriteEnd]);
  pipes[1][kPipeWriteEnd] = -1;
  close(pipes[2][kPipeWriteEnd]);
  pipes[2][kPipeWriteEnd] = -1;

  {
    Process* process = new Process(pid);

    // Subscribe to process exit.
    struct kevent event;
    EV_SET(&event, pid, EVFILT_PROC, EV_ADD | EV_ONESHOT,
           NOTE_EXIT | NOTE_EXITSTATUS, 0, process);
    int status = kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr);
    if (status == -1) {
      if (errno == ESRCH) {
        // Process already exited.
        EV_SET(&event, pid, EVFILT_USER, EV_ADD | EV_ENABLE | EV_ONESHOT,
               NOTE_TRIGGER, 0, process);
        status = kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr);
        if (status == -1) {
          FATAL("Failed to add to kqueue");
        }
      } else {
        FATAL("Failed to add to kqueue");
      }
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
  }

 failErrno:
  status = errno;
 failStatus:
  for (int i = 0; i < 3; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }
  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&facts);
  return status;
}

intptr_t KQueueMessageLoop::Read(intptr_t handle_id,
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

intptr_t KQueueMessageLoop::Write(intptr_t handle_id,
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

intptr_t KQueueMessageLoop::Close(intptr_t handle_id) {
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

#endif  // defined(OS_MACOS)
