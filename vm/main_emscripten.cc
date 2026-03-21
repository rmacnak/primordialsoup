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

// NOLINTBEGIN(*) - Not C++
EM_JS(void, _JS_main, (), {
  var path = Module["snapshot"];
  var request = new XMLHttpRequest();
  request.open("GET", path, true);
  request.responseType = "arraybuffer";
  request.onload = function (event) {
    if (request.status != 200) {
      Module["printErr"]("Failed to load " + path + ": " +
                         request.status + " " + request.statusText);
      return;
    }
    var jsBuffer = new Uint8Array(request.response);
    var cBuffer = Module["_malloc"](jsBuffer.length);
    Module["HEAPU8"].set(jsBuffer, cBuffer);
    Module["_load_snapshot"](cBuffer, jsBuffer.length);
    Module["_free"](cBuffer);
    Module["scheduleTurn"](0);
  };
  request.send();

  Module["aliens"] = [
    undefined,   // 0
    null,        // 1
    false,       // 2
    true,        // 3
    globalThis,  // 4
  ];
  Module["scheduleTurn"] = function (timeout) {
    if (timeout >= 0) {
      setTimeout(function () {
        var timeout = Module["_handle_message"]();
        Module["scheduleTurn"](timeout);
      }, timeout);
    }
  };
});
// NOLINTEND

int main(int argc, char** argv) {
  _JS_main();
  psoup::OS::Startup();
  psoup::Primitives::Startup();
  psoup::PortMap::Startup();
  psoup::Isolate::Startup();
  return 0;
}

static psoup::Isolate* isolate;
extern "C" void load_snapshot(const void* snapshot, size_t snapshot_length) {
  isolate = new psoup::Isolate(snapshot, snapshot_length);
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
