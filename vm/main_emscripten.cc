// Copyright (c) 2019, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(OS_EMSCRIPTEN)

#include "vm/isolate.h"
#include "vm/message_loop.h"
#include "vm/os.h"
#include "vm/port.h"
#include "vm/primordial_soup.h"

static psoup::Isolate* isolate;
extern "C" void load_snapshot(void* snapshot, size_t snapshot_length) {
  PrimordialSoup_Startup();

  uint64_t seed = psoup::OS::CurrentMonotonicNanos();
  isolate = new psoup::Isolate(snapshot, snapshot_length, seed);
  int argc = 0;
  const char** argv = NULL;
  isolate->loop()->PostMessage(new psoup::IsolateMessage(ILLEGAL_PORT,
                                                         argc, argv));
}

extern "C" int handle_one_message() {
  return static_cast<psoup::EmscriptenMessageLoop*>(isolate->loop())
      ->HandleOneMessage();
}

#endif  // defined(OS_EMSCRIPTEN)
