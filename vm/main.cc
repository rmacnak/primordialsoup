// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if !defined(OS_EMSCRIPTEN)

#include <signal.h>

#include "vm/os.h"
#include "vm/primordial_soup.h"
#include "vm/virtual_memory.h"

static void SIGINT_handler(int sig) {
  PrimordialSoup_InterruptAll();
}

int main(int argc, const char** argv) {
  if (argc < 2) {
    psoup::OS::PrintErr("Usage: %s <program.vfuel>\n", argv[0]);
    return -1;
  }

  psoup::MappedMemory snapshot = psoup::MappedMemory::MapReadOnly(argv[1]);
  PrimordialSoup_Startup();
  void (*defaultSIGINT)(int) = signal(SIGINT, SIGINT_handler);

  intptr_t exit_code =
      PrimordialSoup_RunIsolate(snapshot.address(), snapshot.size(),
                                argc - 2, &argv[2]);

  signal(SIGINT, defaultSIGINT);
  PrimordialSoup_Shutdown();

  snapshot.Free();

  return exit_code;
}

#endif  // !defined(OS_EMSCRIPTEN)
