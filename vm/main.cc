// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if !defined(OS_EMSCRIPTEN)

#include <signal.h>

#include "vm/isolate.h"
#include "vm/message_loop.h"
#include "vm/os.h"
#include "vm/port.h"
#include "vm/primitives.h"
#include "vm/virtual_memory.h"

static void SIGINT_handler(int sig) {
  psoup::Isolate::InterruptAll();
}

int main(int argc, const char** argv) {
  if (argc < 2) {
    psoup::OS::PrintErr("Usage: %s <program.vfuel>\n", argv[0]);
    return -1;
  }

  psoup::MappedMemory snapshot = psoup::MappedMemory::MapReadOnly(argv[1]);
  psoup::OS::Startup();
  psoup::Primitives::Startup();
  psoup::PortMap::Startup();
  psoup::Isolate::Startup();
  void (*defaultSIGINT)(int) = signal(SIGINT, SIGINT_handler);

  psoup::Isolate* isolate = new psoup::Isolate(snapshot.address(),
                                               snapshot.size());
  isolate->loop()->PostMessage(new psoup::IsolateMessage(ILLEGAL_PORT,
                                                         argc - 2, &argv[2]));
  intptr_t exit_code = isolate->loop()->Run();
  delete isolate;

  signal(SIGINT, defaultSIGINT);
  psoup::Isolate::Shutdown();
  psoup::PortMap::Shutdown();
  psoup::Primitives::Shutdown();
  psoup::OS::Shutdown();

  snapshot.Free();

  return exit_code;
}

#endif  // !defined(OS_EMSCRIPTEN)
