// Copyright (c) 2018, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_MESSAGE_LOOP_IOCP_H_
#define VM_MESSAGE_LOOP_IOCP_H_

#if !defined(VM_MESSAGE_LOOP_H_)
#error Do not include message_loop_iocp.h directly; use message_loop.h instead.
#endif

#include "vm/handle.h"
#include "vm/message_loop.h"
#include "vm/thread.h"

namespace psoup {

#define PlatformMessageLoop IOCPMessageLoop

class IOCPMessageLoop : public MessageLoop {
 public:
  explicit IOCPMessageLoop(Isolate* isolate);
  ~IOCPMessageLoop();

  void PostMessage(IsolateMessage* message);
  intptr_t AwaitSignal(intptr_t handle, intptr_t signals);
  void CancelSignalWait(intptr_t wait_id);
  void MessageEpilogue(int64_t new_wakeup);
  void Exit(intptr_t exit_code);

  intptr_t Run();
  void Interrupt();

  intptr_t StartProcess(intptr_t options,
                        char** argv,
                        char** env,
                        const char* cwd,
                        intptr_t* process_out,
                        intptr_t* stdin_out,
                        intptr_t* stdout_out,
                        intptr_t* stderr_out);
  intptr_t Read(intptr_t handle,
                uint8_t* buffer,
                intptr_t buffer_size,
                intptr_t* size_out);
  intptr_t Write(intptr_t handle,
                 const uint8_t* buffer,
                 intptr_t buffer_size,
                 intptr_t* size_out);
  intptr_t Close(intptr_t handle);

 private:
  IsolateMessage* TakeMessages();
  void Notify();

  void RespondToIOCompletion(Handle* handle,
                             DWORD status,
                             DWORD bytes,
                             OVERLAPPED* overlapped);
  void RespondToRead(Handle* handle,
                     DWORD status,
                     DWORD bytes,
                     IOBuffer* buffer);
  void RespondToWrite(Handle* handle,
                      DWORD status,
                      DWORD bytes,
                      IOBuffer* buffer);
  void MaybeIssueRead(Handle* handle);
  void MaybeScheduleWakeup(Handle* handle);

  Mutex mutex_;
  IsolateMessage* head_;
  IsolateMessage* tail_;
  int64_t wakeup_;
  HANDLE completion_port_;
  HandleMap handles_;

  DISALLOW_COPY_AND_ASSIGN(IOCPMessageLoop);
};

}  // namespace psoup

#endif  // VM_MESSAGE_LOOP_IOCP_H_
