// Copyright (c) 2019, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(OS_EMSCRIPTEN)

#include <emscripten.h>

#include "vm/isolate.h"
#include "vm/message_loop.h"
#include "vm/os.h"
#include "vm/port.h"
#include "vm/primitives.h"

EM_JS(void, _JS_initializeAliens, (), {
  var aliens = new Array();
  aliens.push(undefined);  // 0
  aliens.push(null);       // 1
  aliens.push(false);      // 2
  aliens.push(true);       // 3
  aliens.push(window);     // 4
  Module.aliens = aliens;
});

static psoup::Isolate* isolate;
extern "C" void load_snapshot(const void* snapshot, size_t snapshot_length) {
  psoup::OS::Startup();
  psoup::Primitives::Startup();
  psoup::PortMap::Startup();
  psoup::Isolate::Startup();
  _JS_initializeAliens();

  uint64_t seed = psoup::OS::CurrentMonotonicNanos();
  isolate = new psoup::Isolate(snapshot, snapshot_length, seed);
  int argc = 0;
  const char** argv = nullptr;
  isolate->loop()->PostMessage(new psoup::IsolateMessage(ILLEGAL_PORT,
                                                         argc, argv));
}

extern "C" int handle_message() {
  return static_cast<psoup::EmscriptenMessageLoop*>(isolate->loop())
      ->HandleMessage();
}

extern "C" int handle_signal(int handle, int status, int signals, int count) {
  return static_cast<psoup::EmscriptenMessageLoop*>(isolate->loop())
      ->HandleSignal(handle, status, signals, count);
}

#endif  // defined(OS_EMSCRIPTEN)
