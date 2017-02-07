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
  static int Main(int argc, const char** argv) {
    if (argc < 2) {
      OS::PrintErr("Usage: %s <program.vfuel>\n", argv[0]);
      return -1;
    }

    OS::Startup();
    OSThread::Startup();
    Primitives::Startup();
    PortMap::Startup();
    Snapshot::Startup(argv[1]);
    Isolate::Startup();
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

    // TODO(rmacnak): Kill any other isolates.

    delete pool;  // Waits for all tasks to complete.

    signal(SIGINT, defaultSIGINT);
    Isolate::Shutdown();
    Snapshot::Shutdown();
    PortMap::Shutdown();
    Primitives::Shutdown();
    OSThread::Shutdown();
    OS::Shutdown();

    return 0;
  }
};

}  // namespace psoup

int main(int argc, const char** argv) {
  return psoup::PrimordialSoup::Main(argc, argv);
}
