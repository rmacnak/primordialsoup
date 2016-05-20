// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include <signal.h>

#include "vm/assert.h"
#include "vm/globals.h"
#include "vm/heap.h"
#include "vm/interpreter.h"
#include "vm/isolate.h"
#include "vm/object.h"
#include "vm/os.h"
#include "vm/port.h"
#include "vm/primitives.h"
#include "vm/snapshot.h"
#include "vm/thread_pool.h"

namespace psoup {

static void SIGINT_handler(int sig) {
  Isolate::InterruptAll();
}

class PrimordialSoup {
 public:
  static int main(int argc, const char** argv) {
    if (argc < 2) {
      OS::PrintErr("Usage: %s <program.vfuel>\n", argv[0]);
      OS::Exit(-1);
    }

    if (false) {
      OS::PrintErr("sizeof(Heap) %" Pd "\n", sizeof(Heap));
      OS::PrintErr("sizeof(Isolate) %" Pd "\n", sizeof(Isolate));
      OS::PrintErr("sizeof(Interpreter) %" Pd "\n", sizeof(Interpreter));
      OS::PrintErr("sizeof(LookupCache) %" Pd "\n", sizeof(LookupCache));
      OS::PrintErr("sizeof(ThreadPool) %" Pd "\n", sizeof(ThreadPool));
      OS::PrintErr("sizeof(pthread_mutex_t) %" Pd "\n",
                   sizeof(pthread_mutex_t));
      OS::PrintErr("sizeof(pthread_cond_t) %" Pd "\n", sizeof(pthread_cond_t));
      OS::PrintErr("sizeof(jmp_buf) %" Pd "\n", sizeof(jmp_buf));
      OS::PrintErr("sizeof(Monitor) %" Pd "\n", sizeof(Monitor));
      OS::PrintErr("sizeof(OSThread) %" Pd "\n", sizeof(OSThread));
      OS::PrintErr("sizeof(Thread) %" Pd "\n", sizeof(Thread));
    }

    OS::InitOnce();
    OSThread::InitOnce();
    Primitives::InitOnce();
    ByteString::hash_random_ = OS::CurrentMonotonicMicros();
    PortMap::InitOnce();
    Snapshot::InitOnce(argv[1]);
    Isolate::InitOnce();
    void (*defaultSIGINT)(int) = signal(SIGINT, SIGINT_handler);

    ThreadPool* pool = new ThreadPool();

    if (TRACE_ISOLATES) {
      intptr_t id = OSThread::ThreadIdToIntPtr(OSThread::Current()->trace_id());
      OS::Print("Starting main isolate on thread %" Pd "\n", id);
    }

    Isolate* main_isolate = new Isolate(pool);
    main_isolate->InitMain(argc, argv);
    main_isolate->Interpret();

    if (TRACE_ISOLATES) {
      intptr_t id = OSThread::ThreadIdToIntPtr(OSThread::Current()->trace_id());
      OS::Print("Finishing main isolate on thread %" Pd "\n", id);
    }
    delete main_isolate;

    // Kill any other isolates now?

    pool->Shutdown();  // Wait for no other isolates.
    delete pool;

    signal(SIGINT, defaultSIGINT);
    Isolate::Cleanup();
    Snapshot::Shutdown();
    PortMap::Shutdown();
    OSThread::Cleanup();

    return 0;
  }
};

}  // namespace psoup

int main(int argc, const char** argv) {
  return psoup::PrimordialSoup::main(argc, argv);
}
