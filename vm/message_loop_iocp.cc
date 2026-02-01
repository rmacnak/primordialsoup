// Copyright (c) 2018, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // NOLINT
#if defined(OS_WINDOWS)

#include "vm/message_loop.h"

#include <signal.h>

#include <atomic>

#include "vm/io_buffer.h"
#include "vm/lockers.h"
#include "vm/os.h"

namespace psoup {

IOCPMessageLoop::IOCPMessageLoop(Isolate* isolate)
    : MessageLoop(isolate),
      mutex_(),
      head_(nullptr),
      tail_(nullptr),
      wakeup_(0) {
  completion_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
  if (completion_port_ == nullptr) {
    FATAL("Failed to create IOCP");
  }
}

IOCPMessageLoop::~IOCPMessageLoop() {
  CloseHandle(completion_port_);
}

intptr_t IOCPMessageLoop::AwaitSignal(intptr_t handle_id, intptr_t signals) {
  Handle* handle = handles_.Lookup(handle_id);
  if (handle == nullptr) {
    return EBADF;  // No such handle.
  }

  if (signals != 0) {
    signals |= kCloseEvent;
    signals |= kErrorEvent;
  }

  int subscribed = handle->subscribed_signals();
  if (signals == subscribed) {
    return 0;
  }

  if (subscribed == 0) {
    open_waits_++;
  } else if (signals == 0) {
    open_waits_--;
  }

  handle->set_subscribed_signals(signals);
  if ((handle->pending_write_ == nullptr) &&
      (handle->pending_signals_ & (kErrorEvent | kCloseEvent)) == 0) {
    handle->pending_signals_ |= kWriteEvent;
  }

  MaybeIssueRead(handle);
  MaybeScheduleWakeup(handle);

  return 0;
}

void IOCPMessageLoop::MaybeScheduleWakeup(Handle* handle) {
  ASSERT(handle->pending_wakeup_ >= 0);
  if ((handle->pending_wakeup_ == 0) &&
      ((handle->subscribed_signals_ & handle->pending_signals_) != 0)) {
    handle->pending_wakeup_++;
    ULONG_PTR key = reinterpret_cast<ULONG_PTR>(handle);
    PostQueuedCompletionStatus(completion_port_, 0, key, nullptr);
  }
}

void IOCPMessageLoop::CancelSignalWait(intptr_t wait_id) {
  open_waits_--;
  UNIMPLEMENTED();
}

void IOCPMessageLoop::MessageEpilogue(int64_t new_wakeup) {
  wakeup_ = new_wakeup;

  if ((open_ports_ == 0) && (open_waits_ == 0) && (wakeup_ == 0)) {
    Exit(0);
  }
}

void IOCPMessageLoop::Exit(intptr_t exit_code) {
  exit_code_ = exit_code;
  isolate_ = nullptr;
}

void IOCPMessageLoop::PostMessage(IsolateMessage* message) {
  MutexLocker locker(&mutex_);
  if (head_ == nullptr) {
    head_ = tail_ = message;
    Notify();
  } else {
    tail_->next_ = message;
    tail_ = message;
  }
}

void IOCPMessageLoop::Notify() {
  ULONG_PTR key = reinterpret_cast<ULONG_PTR>(this);
  BOOL ok = PostQueuedCompletionStatus(completion_port_, 0, key, nullptr);
  if (!ok) {
    FATAL("PostQueuedCompletionStatus failed: %d", GetLastError());
  }
}

IsolateMessage* IOCPMessageLoop::TakeMessages() {
  MutexLocker locker(&mutex_);
  IsolateMessage* message = head_;
  head_ = tail_ = nullptr;
  return message;
}

intptr_t IOCPMessageLoop::Run() {
  while (isolate_ != nullptr) {
    DWORD timeout;
    if (wakeup_ == 0) {
      timeout = INFINITE;
    } else {
      int64_t timeout64 =
          (wakeup_ - OS::CurrentMonotonicNanos()) / kNanosecondsPerMillisecond;
      if (timeout64 < 0) {
        timeout = 0;
      } else if (timeout64 > kMaxUint32) {
        timeout = kMaxUint32;
      } else {
        timeout = static_cast<DWORD>(timeout64);
      }
    }

    DWORD bytes;
    ULONG_PTR key;
    OVERLAPPED* overlapped;
    BOOL ok = GetQueuedCompletionStatus(completion_port_, &bytes, &key,
                                        &overlapped, timeout);
    if (!ok && (overlapped == nullptr)) {
      // Timeout.
      DispatchWakeup();
    } else if (key == reinterpret_cast<ULONG_PTR>(this)) {
      // Interrupt: will check messages below.
    } else {
      Handle* handle = reinterpret_cast<Handle*>(key);
      DWORD status = 0;
      if (!ok) {
        status = GetLastError();
      }
      RespondToIOCompletion(handle, status, bytes, overlapped);
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

void IOCPMessageLoop::RespondToIOCompletion(Handle* handle,
                                            DWORD status,
                                            DWORD bytes,
                                            OVERLAPPED* overlapped) {
  if (overlapped != nullptr) {
    IOBuffer* buffer = IOBuffer::FromOverlapped(overlapped);
    if (buffer->operation() == kCancelledOperation) {
      IOBuffer::Dispose(buffer);
      return;
    }
  }

  if (handle->type() == Handle::kProcess) {
    Process* process = static_cast<Process*>(handle);

    DWORD exit_code;
    if (!GetExitCodeProcess(process->handle(), &exit_code)) {
      FATAL("GetExitCodeProcess failed: %d", GetLastError());
    }

    intptr_t handle_id = handle->id();
    intptr_t status = exit_code;
    intptr_t signals = kCloseEvent;
    intptr_t count = 0;

    open_waits_--;
    handles_.Remove(handle_id);
    delete handle;

    DispatchSignal(handle_id, status, signals, count);
    return;
  }

  if (status != 0 && status != ERROR_BROKEN_PIPE) {
    char buffer[64];
    FATAL("overlapped error: %d (%s)", status,
          OS::StrError(status, buffer, sizeof(buffer)));
  }

  if (overlapped != nullptr) {
    handle->pending_wakeup_++;
    IOBuffer* buffer = IOBuffer::FromOverlapped(overlapped);
    switch (buffer->operation()) {
      case kReadOperation:
        RespondToRead(handle, status, bytes, buffer);
        break;
      case kWriteOperation:
        RespondToWrite(handle, status, bytes, buffer);
        break;
      default:
        UNREACHABLE();
    }
  }
  handle->pending_wakeup_--;
  ASSERT(handle->pending_wakeup_ >= 0);

  intptr_t signals = handle->subscribed_signals_ & handle->pending_signals_;
  handle->pending_signals_ &= ~signals;
  if (signals != 0) {
    DispatchSignal(handle->id(), handle->status_, signals, bytes);
  }
}

void IOCPMessageLoop::RespondToRead(Handle* handle,
                                    DWORD status,
                                    DWORD bytes,
                                    IOBuffer* buffer) {
  ASSERT(handle->pending_read_ == buffer);
  ASSERT(handle->completed_read_ == nullptr);

  if (status == ERROR_BROKEN_PIPE) {
    ASSERT(bytes == 0);
    IOBuffer::Dispose(buffer);
    handle->pending_read_ = nullptr;
    handle->pending_signals_ |= kCloseEvent;
  } else {
    buffer->ReadComplete(bytes);
    handle->completed_read_ = buffer;
    handle->pending_read_ = nullptr;
    handle->pending_signals_ |= kReadEvent;
  }
}

void IOCPMessageLoop::RespondToWrite(Handle* handle,
                                     DWORD status,
                                     DWORD bytes,
                                     IOBuffer* buffer) {
  ASSERT(handle->pending_write_ == buffer);
  IOBuffer::Dispose(buffer);
  handle->pending_write_ = nullptr;
  if (status == 0) {
    handle->pending_signals_ |= kWriteEvent;
  } else if (status == ERROR_BROKEN_PIPE) {
    handle->pending_signals_ |= kCloseEvent;
  } else {
    handle->status_ = status;
    handle->pending_signals_ |= kErrorEvent;
  }
}

void IOCPMessageLoop::Interrupt() {
  Exit(SIGINT);
  Notify();
}

static constexpr intptr_t kPipeReadEnd = 0;
static constexpr intptr_t kPipeWriteEnd = 1;

static char* AsBlock(char** array, char separator) {
  size_t size = 1;
  for (int i = 0; array[i] != nullptr; i++) {
    size += strlen(array[i]);
    size += 1;
  }
  char* block = reinterpret_cast<char*>(malloc(size + 1));
  char* cursor = block;
  for (int i = 0; array[i] != nullptr; i++) {
    size_t n = strlen(array[i]);
    memcpy(cursor, array[i], n);
    cursor += n;
    *cursor = separator;
    cursor += 1;
  }
  cursor[-1] = '\0';
  cursor[0] = '\0';
  return block;
}

static void CALLBACK ExitCallback(PVOID data, BOOLEAN timed_out) {
  if (timed_out) {
    return;
  }
  Process* process = reinterpret_cast<Process*>(data);
  ULONG_PTR key = reinterpret_cast<ULONG_PTR>(process);
  if (!PostQueuedCompletionStatus(process->completion_port(),
                                  0, key, nullptr)) {
    FATAL("PostQueuedCompletionStatus failed: %d", GetLastError());
  }
}

// Unfortunately, anonymous pipes do not support overlapped I/O, so we need to
// create a named pipe and be careful to avoid name conflicts.
static bool CreateProcessPipe(HANDLE handles[2]) {
  SECURITY_ATTRIBUTES inherit_handle;
  ZeroMemory(&inherit_handle, sizeof(inherit_handle));
  inherit_handle.nLength = sizeof(SECURITY_ATTRIBUTES);
  inherit_handle.bInheritHandle = TRUE;
  inherit_handle.lpSecurityDescriptor = nullptr;

  static std::atomic<uint64_t> pipe_seq = 0;
  char name[64];
  int len = snprintf(name, sizeof(name), "\\\\.\\pipe\\psoup.%d.%" Pd64,
                     GetCurrentProcessId(),
                     pipe_seq.fetch_add(1, std::memory_order_relaxed));
  ASSERT(len < sizeof(name));

  handles[kPipeWriteEnd] =
      CreateNamedPipeA(name,
                       PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
                       PIPE_TYPE_BYTE | PIPE_WAIT,
                       1,  // max instances
                       16 * KB,  // out buffer size
                       16 * KB,  // in buffer size
                       0,  // timeout
                       &inherit_handle);
  if (handles[kPipeWriteEnd] == INVALID_HANDLE_VALUE) {
    return false;
  }
  handles[kPipeReadEnd] =
      CreateFileA(name,
                  GENERIC_READ,
                  0,
                  &inherit_handle,
                  OPEN_EXISTING,
                  FILE_READ_ATTRIBUTES | FILE_FLAG_OVERLAPPED,
                  nullptr);
  if (handles[kPipeReadEnd] == INVALID_HANDLE_VALUE) {
    return false;
  }
  return true;
}

intptr_t IOCPMessageLoop::StartProcess(intptr_t options,
                                       char** argv,
                                       char** env,
                                       const char* cwd,
                                       intptr_t* process_out,
                                       intptr_t* stdin_out,
                                       intptr_t* stdout_out,
                                       intptr_t* stderr_out) {
  char* command_block = nullptr;
  char* environment_block = nullptr;
  HANDLE pipes[3][2] = {{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE},
                        {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE},
                        {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE}};
  LPPROC_THREAD_ATTRIBUTE_LIST attribute_list = nullptr;

  command_block = AsBlock(argv, ' ');
  if (env != nullptr) {
    environment_block = AsBlock(env, '\0');
  }

  for (int i = 0; i < 3; i++) {
    if (!CreateProcessPipe(pipes[i])) {
      goto error;
    }
  }

  SIZE_T size = 0;
  if (!InitializeProcThreadAttributeList(nullptr, 1, 0, &size) &&
      (GetLastError() != ERROR_INSUFFICIENT_BUFFER)) {
    goto error;
  }
  attribute_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(malloc(size));
  ZeroMemory(attribute_list, size);
  if (!InitializeProcThreadAttributeList(attribute_list, 1, 0, &size)) {
    goto error;
  }
  HANDLE inherited_handles[3] = {
    pipes[0][kPipeReadEnd],
    pipes[1][kPipeWriteEnd],
    pipes[2][kPipeWriteEnd],
  };
  if (!UpdateProcThreadAttribute(attribute_list, 0,
                                 PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                 inherited_handles, sizeof(inherited_handles),
                                 nullptr, nullptr)) {
    goto error;
  }

  STARTUPINFOEXA startup_info;
  ZeroMemory(&startup_info, sizeof(STARTUPINFOEXA));
  startup_info.StartupInfo.cb = sizeof(STARTUPINFOEXA);
  startup_info.StartupInfo.hStdInput = pipes[0][kPipeReadEnd];
  startup_info.StartupInfo.hStdOutput = pipes[1][kPipeWriteEnd];
  startup_info.StartupInfo.hStdError = pipes[2][kPipeWriteEnd];
  startup_info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup_info.lpAttributeList = attribute_list;

  PROCESS_INFORMATION process_info;
  ZeroMemory(&process_info, sizeof(PROCESS_INFORMATION));

  if (!CreateProcessA(nullptr,  // application name
                      command_block,
                      nullptr,  // resulting process handle not inheritable
                      nullptr,  // resulting thread handle not inheritable
                      TRUE,     // child inherits handles
                      EXTENDED_STARTUPINFO_PRESENT,
                      environment_block,
                      cwd,
                      &startup_info.StartupInfo,
                      &process_info)) {
    goto error;
  }

  DeleteProcThreadAttributeList(attribute_list);
  free(attribute_list);
  CloseHandle(pipes[0][kPipeReadEnd]);
  CloseHandle(pipes[1][kPipeWriteEnd]);
  CloseHandle(pipes[2][kPipeWriteEnd]);
  CloseHandle(process_info.hThread);
  free(command_block);
  free(environment_block);

  Process* process = new Process(process_info.dwProcessId);
  process->handle_ = process_info.hProcess;
  process->completion_port_ = completion_port_;
  process->wait_handle_ = INVALID_HANDLE_VALUE;
  if (!RegisterWaitForSingleObject(&process->wait_handle_,
                                   process->handle_,
                                   &ExitCallback,
                                   process,
                                   INFINITE,
                                   WT_EXECUTEONLYONCE)) {
    FATAL("RegisterWaitForSingleObject failed: %d", GetLastError());
  }
  open_waits_++;
  *process_out = handles_.Register(process);

  auto& BindPipe = [&](HANDLE pipeh) {
    Handle* pipe = new Handle(Handle::kPipe, pipeh);
    ULONG_PTR key = reinterpret_cast<ULONG_PTR>(pipe);
    if (CreateIoCompletionPort(pipeh, completion_port_, key, 0) == nullptr) {
      FATAL("Failed to bind to IOCP: %d", GetLastError());
    }
    return handles_.Register(pipe);
  };
  *stdin_out = BindPipe(pipes[0][kPipeWriteEnd]);
  *stdout_out = BindPipe(pipes[1][kPipeReadEnd]);
  *stderr_out = BindPipe(pipes[2][kPipeReadEnd]);
  return 0;

 error:
  int status = GetLastError();
  if (attribute_list != nullptr) {
    DeleteProcThreadAttributeList(attribute_list);
    free(attribute_list);
  }
  for (int i = 0; i < 3; i++) {
    CloseHandle(pipes[i][0]);
    CloseHandle(pipes[i][1]);
  }
  free(command_block);
  free(environment_block);
  return status;
}

intptr_t IOCPMessageLoop::Read(intptr_t handle_id,
                               uint8_t* buffer,
                               intptr_t buffer_size,
                               intptr_t* size_out) {
  Handle* handle = handles_.Lookup(handle_id);
  if (handle == nullptr) {
    return EBADF;  // No such handle.
  }

  IOBuffer* completed_read = handle->completed_read_;
  if (completed_read == nullptr) {
    *size_out = 0;
    return 0;
  }

  *size_out = completed_read->Read(buffer, buffer_size);

  if (completed_read->read_available() == 0) {
    IOBuffer::Dispose(handle->completed_read_);
    handle->completed_read_ = nullptr;

    MaybeIssueRead(handle);
  }

  return 0;
}

void IOCPMessageLoop::MaybeIssueRead(Handle* handle) {
  if (handle->pending_read_ != nullptr) return;
  if ((handle->subscribed_signals_ & kReadEvent) == 0) return;

  handle->pending_read_ = IOBuffer::Allocate(kReadOperation);
  if (!ReadFile(handle->handle_,
                handle->pending_read_->buffer(),
                static_cast<DWORD>(handle->pending_read_->buffer_size()),
                nullptr,
                handle->pending_read_->GetCleanOverlapped())) {
    int error = GetLastError();
    if (error == ERROR_IO_PENDING) return;

    if (error == ERROR_BROKEN_PIPE) {
      IOBuffer::Dispose(handle->pending_read_);
      handle->pending_read_ = nullptr;
      handle->pending_signals_ |= kCloseEvent;
      MaybeScheduleWakeup(handle);
      return;
    }

    UNIMPLEMENTED();
    IOBuffer::Dispose(handle->pending_read_);
    handle->pending_read_ = nullptr;
    handle->pending_signals_ |= kErrorEvent;
    handle->status_ = error;
    MaybeScheduleWakeup(handle);
  }
}

intptr_t IOCPMessageLoop::Write(intptr_t handle_id,
                                const uint8_t* buffer,
                                intptr_t buffer_size,
                                intptr_t* size_out) {
  Handle* handle = handles_.Lookup(handle_id);
  if (handle == nullptr) {
    return EBADF;  // No such handle.
  }

  ASSERT(handle->pending_write_ == nullptr);
  handle->pending_write_ = IOBuffer::Allocate(kWriteOperation, buffer_size);
  memcpy(handle->pending_write_->buffer(), buffer, buffer_size);

  BOOL ok = WriteFile(handle->handle_,
                      handle->pending_write_->buffer(),
                      buffer_size,
                      nullptr,
                      handle->pending_write_->GetCleanOverlapped());
  if (ok) {
    *size_out = buffer_size;
    return 0;
  }
  DWORD status = GetLastError();
  if (status == ERROR_IO_PENDING) {
    *size_out = buffer_size;
    return 0;
  }
  return status;
}

intptr_t IOCPMessageLoop::Close(intptr_t handle_id) {
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

#endif  // defined(OS_WINDOWS)
