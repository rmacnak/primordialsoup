// Copyright (c) 2015, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_THREAD_H_
#define VM_THREAD_H_

#include "vm/assert.h"
#include "vm/atomic.h"
#include "vm/bitfield.h"
#include "vm/globals.h"
#include "vm/os_thread.h"

namespace psoup {

class OSThread;

// A VM thread; may be executing Dart code or performing helper tasks like
// garbage collection or compilation. The Thread structure associated with
// a thread is allocated by EnsureInit before entering an isolate, and destroyed
// automatically when the underlying OS thread exits. NOTE: On Windows, CleanUp
// must currently be called manually (issue 23474).
class Thread : public BaseThread {
 public:
  ~Thread();

  // The currently executing thread, or NULL if not yet initialized.
  static Thread* Current() {
    BaseThread* thread = OSThread::GetCurrentTLS();
    if (thread == NULL || thread->is_os_thread()) {
      return NULL;
    }
    return reinterpret_cast<Thread*>(thread);
  }

  // Returns the current C++ stack pointer. Equivalent taking the address of a
  // stack allocated local, but plays well with AddressSanitizer.
  static uword GetCurrentStackPointer();

  // OSThread corresponding to this thread.
  OSThread* os_thread() const { return os_thread_; }
  void set_os_thread(OSThread* os_thread) {
    os_thread_ = os_thread;
  }

  // The isolate that this thread is operating on, or NULL if none.
  Isolate* isolate() const { return isolate_; }

  Thread* next() const { return next_; }

 private:
  Isolate* isolate_;

  OSThread* os_thread_;

  Thread* next_;  // Used to chain the thread structures in an isolate.

  explicit Thread(Isolate* isolate);

  static void SetCurrent(Thread* current) {
    OSThread::SetCurrentTLS(reinterpret_cast<uword>(current));
  }

  friend class Isolate;

  DISALLOW_COPY_AND_ASSIGN(Thread);
};

}  // namespace psoup

#endif  // VM_THREAD_H_
