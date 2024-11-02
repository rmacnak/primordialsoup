// Copyright (c) 2022, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/handle.h"

#if defined(OS_ANDROID) || defined(OS_LINUX) || defined(OS_MACOS)
#include <errno.h>
#include <unistd.h>
#elif defined(OS_WINDOWS)
#include "vm/io_buffer.h"
#endif

#include "vm/utils.h"

namespace psoup {

Handle::~Handle() {
#if defined(OS_ANDROID) || defined(OS_LINUX) || defined(OS_MACOS)
  if (fd_ != -1) {
    int r;
    do {
      r = close(fd_);
    } while (r == -1 && errno == EINTR);
    fd_ = -1;
  }
#elif defined(OS_WINDOWS)
  if (pending_read_ != nullptr || pending_write_ != nullptr) {
    ASSERT(handle_ != INVALID_HANDLE_VALUE);
    CancelIo(handle_);
    // The IOCP event will still happen, so we can't delete the IOBuffer yet.
    if (pending_read_ != nullptr) {
      pending_read_->Cancel();
      pending_read_ = nullptr;
    }
    if (pending_write_ != nullptr) {
      pending_write_->Cancel();
      pending_write_ = nullptr;
    }
  }
  if (completed_read_ != nullptr) {
    IOBuffer::Dispose(completed_read_);
    completed_read_ = nullptr;
  }

  // Dubious. We might need to ref-count handles to safely deal with pending
  // wakeup packets in the completion port queue.
  ASSERT(pending_wakeup_ == 0);

  if (handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }
#endif
}

Process::~Process() {
#if defined(OS_ANDROID) || defined(OS_LINUX)
  if (pidfd_ != -1) {
    int r;
    do {
      r = close(pidfd_);
    } while (r == -1 && errno == EINTR);
    pidfd_ = -1;
  }
#elif defined(OS_WINDOWS)
  if (wait_handle_ != INVALID_HANDLE_VALUE) {
    // Still need to destroy wait even with WT_EXECUTEONLYONCE.
    bool ok = UnregisterWait(wait_handle_);
    if (!ok && (GetLastError() != ERROR_IO_PENDING)) {
      FATAL("UnregisterWait failed: %d", GetLastError());
    }
    wait_handle_ = INVALID_HANDLE_VALUE;
  }
#endif
}

HandleMap::HandleMap() {
  Rehash(8);
}

HandleMap::~HandleMap() {
  for (int32_t i = 0; i < capacity_; i++) {
    delete entries_[i].handle;
  }
  delete[] entries_;
}

int32_t HandleMap::Register(Handle* handle) {
  int32_t id = next_id_++;
  handle->set_id(id);
  Insert(id, handle);
  return id;
}

void HandleMap::Insert(int32_t id, Handle* handle) {
  ASSERT(id != kEmpty);
  ASSERT(id != kDeleted);
  int32_t mask = capacity_ - 1;
  int32_t i = id & mask;
  for (;;) {
    if (entries_[i].id == kEmpty || entries_[i].id == kDeleted) {
      if (entries_[i].id == kDeleted) {
        deleted_--;
      }
      used_++;
      entries_[i].id = id;
      entries_[i].handle = handle;
      int32_t empty = capacity_ - used_ - deleted_;
      if (used_ > ((capacity_ / 4) * 3)) {
        Rehash(capacity_ * 2);
      } else if (empty < deleted_) {
        Rehash(capacity_);
      }
      return;
    }
    i = (i + 1) & mask;
  }
}

bool HandleMap::Remove(int32_t id) {
  ASSERT(id != kEmpty);
  ASSERT(id != kDeleted);
  int32_t mask = capacity_ - 1;
  int32_t i = id & mask;
  for (;;) {
    if (entries_[i].id == kEmpty) {
      return false;
    }
    if (entries_[i].id == id) {
      entries_[i].id = kDeleted;
      entries_[i].handle = nullptr;
      deleted_++;
      used_--;
      return true;
    }
    i = (i + 1) & mask;
  }
}

Handle* HandleMap::Lookup(int32_t id) {
  ASSERT(id != kEmpty);
  ASSERT(id != kDeleted);
  int32_t mask = capacity_ - 1;
  int32_t i = id & mask;
  for (;;) {
    if (entries_[i].id == id) {
      return entries_[i].handle;
    }
    if (entries_[i].id == kEmpty) {
      return nullptr;
    }
    i = (i + 1) & mask;
  }
}

void HandleMap::Rehash(int32_t new_capacity) {
  ASSERT(Utils::IsPowerOfTwo(new_capacity));
  int32_t old_capacity = capacity_;
  Entry* old_entries = entries_;
  entries_ = new Entry[new_capacity];
  for (int32_t i = 0; i < new_capacity; i++) {
    entries_[i].id = kEmpty;
    entries_[i].handle = nullptr;
  }
  capacity_ = new_capacity;
  deleted_ = 0;
  for (int32_t i = 0; i < old_capacity; i++) {
    if (old_entries[i].id != kEmpty &&
        old_entries[i].id != kDeleted) {
      Insert(old_entries[i].id,
             old_entries[i].handle);
    }
  }
  delete[] old_entries;
}

}  // namespace psoup
