// Copyright (c) 2022, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_HANDLE_H_
#define VM_HANDLE_H_

#include "vm/globals.h"
#include "vm/allocation.h"

namespace psoup {

class IOBuffer;

class Handle {
 public:
  enum Type : int32_t {
    kNone = 0,

    kClientSocket,
    kDNSLookup,
    kDatagramSocket,
    kDirectory,
    kFile,
    kPipe,
    kProcess,
    kServerSocket,
    kSignal,
    kTTY,
    kTimer,
  };

  static const char* TypeToCString(Type type) {
    switch (type) {
      case kClientSocket: return "client-socket";
      case kDNSLookup: return "dns-lookup";
      case kDatagramSocket: return "datagram-socket";
      case kDirectory: return "directory";
      case kFile: return "file";
      case kPipe: return "pipe";
      case kProcess: return "process";
      case kServerSocket: return "server-socket";
      case kTTY: return "tty";
      case kTimer: return "timer";
      default: return "invalid-type";
    }
  }

#if defined(OS_WINDOWS)
  explicit Handle(Type type, HANDLE handle)
    : type_(type),
      handle_(handle) {}
#else
  explicit Handle(Type type, int fd)
    : type_(type),
      fd_(fd) {}
#endif

  virtual ~Handle();

  Type type() const { return type_; }

  int32_t id() const { return id_; }
  void set_id(int32_t id) { id_ = id; }

#if defined(OS_WINDOWS)
  HANDLE handle() const { return handle_; }
  void set_handle(HANDLE h) { handle_ = h; }
#else
  int fd() const { return fd_; }
  void set_fd(int fd) { fd_ = fd; }
#endif

  int subscribed_signals() const { return subscribed_signals_; }
  void set_subscribed_signals(int v) { subscribed_signals_ = v; }

 private:
  const Type type_;
  int32_t id_ = 0;
#if !defined(OS_WINDOWS)
  int fd_;
  int subscribed_signals_ = 0;
#else
  HANDLE handle_;
  int subscribed_signals_ = 0;
  int pending_signals_ = 0;
  DWORD status_ = 0;
  int pending_wakeup_ = 0;
  IOBuffer* pending_read_ = nullptr;
  IOBuffer* completed_read_ = nullptr;
  IOBuffer* pending_write_ = nullptr;
  friend class IOCPMessageLoop;
#endif
};

class Process : public Handle {
 public:
  explicit Process(int pid, int pidfd = -1)
#if defined(OS_WINDOWS)
    : Handle(kProcess, INVALID_HANDLE_VALUE),
#else
    : Handle(kProcess, -1),
#endif
      pid_(pid),
      pidfd_(pidfd) {}
  virtual ~Process();

  intptr_t pid() const { return pid_; }

#if defined(OS_WINDOWS)
  HANDLE completion_port() const { return completion_port_; }
#endif

 protected:
  int pid_;
  int pidfd_;
#if defined(OS_WINDOWS)
  HANDLE completion_port_ = INVALID_HANDLE_VALUE;
  HANDLE wait_handle_ = INVALID_HANDLE_VALUE;
  friend class IOCPMessageLoop;
#endif
};

class HandleMap {
 public:
  HandleMap();
  ~HandleMap();

  int32_t Register(Handle* handle);
  bool Remove(int32_t id);
  Handle* Lookup(int32_t id);

 private:
  void Insert(int32_t id, Handle* handle);
  void Rehash(int32_t new_capacity);

  typedef struct {
    intptr_t id;
    Handle* handle;
  } Entry;
  static constexpr int32_t kEmpty = 0;
  static constexpr int32_t kDeleted = 1;
  static constexpr int32_t kFirstRegularId = 2;

  Entry* entries_ = nullptr;
  int32_t capacity_ = 0;
  int32_t used_ = 0;
  int32_t deleted_ = 0;
  int32_t next_id_ = kFirstRegularId;
};

}  // namespace psoup

#endif  // VM_HANDLE_H_
