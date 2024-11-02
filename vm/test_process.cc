// Copyright (c) 2022, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>

#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <synchapi.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

void sleepms(uint64_t milliseconds) {
#if defined(_WIN32)
  Sleep(milliseconds);
#else
  usleep(milliseconds * 1000);
#endif
}

int main(int argc, char** argv, char** env) {
#ifdef _WIN32
#define STDIN_FILENO _fileno(stdin)
#define STDOUT_FILENO _fileno(stdout)
#define STDERR_FILENO _fileno(stderr)
  _setmode(STDIN_FILENO, O_BINARY);
  _setmode(STDOUT_FILENO, O_BINARY);
  _setmode(STDERR_FILENO, O_BINARY);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

  if (argc > 1 && strcmp(argv[1], "abort") == 0) {
    abort();
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "trap") == 0) {
#if defined(_MSC_VER)
    __debugbreak();
#else
    __builtin_trap();
#endif
    return 0;
  }

  if (argc > 2 && strcmp(argv[1], "exit") == 0) {
    return atoi(argv[2]);
  }

  if (argc > 2 && strcmp(argv[1], "sleep") == 0) {
    sleepms(atoi(argv[2]));
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "args-to-stdout") == 0) {
    for (int i = 0; i < argc; i++) {
      fprintf(stdout, "%s\n", argv[i]);
    }
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "args-to-stderr") == 0) {
    for (int i = 0; i < argc; i++) {
      fprintf(stderr, "%s\n", argv[i]);
    }
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "environment") == 0) {
    for (int i = 0; env[i] != nullptr; i++) {
      fprintf(stdout, "%s\n", env[i]);
    }
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "working-directory") == 0) {
    char path[4096];
    if (getcwd(path, sizeof(path)) == nullptr) {
      perror("getcwd");
      abort();
    }
    fprintf(stdout, "%s", path);
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "stdin-to-stdout") == 0) {
    for (;;) {
      char buffer[4096];
      int red = read(STDIN_FILENO, buffer, sizeof(buffer));
      if (red == 0) break;  // EOF
      if (red < 0) {
        perror("read");
        abort();
      }
      int written = 0;
      while (written < red) {
        int x = write(STDOUT_FILENO, &buffer[written], red - written);
        if (x < 0) {
          perror("write");
          abort();
        }
        written += x;
      }
    }
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "close-stdout") == 0) {
    fprintf(stdout, "closing");
    fclose(stdout);

    for (;;) {
      char buffer[32];
      int red = read(STDIN_FILENO, buffer, sizeof(buffer));
      if (red == 0) break;  // EOF
      if (red < 0) {
        perror("read");
        abort();
      }
    }
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "close-stdin") == 0) {
    fprintf(stdout, "waiting");
    close(STDIN_FILENO);

#if !defined(_WIN32)
    signal(SIGPIPE, SIG_IGN);
#endif
    for (;;) {
      char buffer[] = "waiting";
#if defined(_WIN32)
      bool ok = WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
                          buffer, sizeof(buffer), nullptr, nullptr);
      if (!ok) {
        // Weirdly, not ERROR_BROKEN_PIPE.
        if (GetLastError() == ERROR_NO_DATA) break;
        perror("WriteFile");
        abort();
      }
#else
      int written = write(STDOUT_FILENO, buffer, sizeof(buffer));
      if (written < 0) {
        if (errno == EPIPE) break;
        perror("write");
        abort();
      }
#endif
    }
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "large-write") == 0) {
    const int N = 1024 * 1024;
    uint8_t* bytes = new uint8_t[N];
    for (int i = 0; i < N; i++) {
      bytes[i] = i & 255;
    }
    int written = 0;
    while (written < N) {
      int x = write(STDOUT_FILENO, &bytes[written], N - written);
      if (x < 0) {
        perror("write");
        abort();
      }
      written += x;
    }
    delete[] bytes;
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "large-read") == 0) {
    const int N = 1024 * 1024;
    uint8_t* bytes = new uint8_t[N];
    int red = 0;
    while (red < N) {
      int x = read(STDIN_FILENO, &bytes[red], N - red);
      if (x < 0) {
        perror("read");
        abort();
      } else if (x == 0) {
        break;
      }
      red += x;
    }
    if (red != N) {
      abort();
    }
    for (int i = 0; i < N; i++) {
      if (bytes[i] != (i & 255)) {
        abort();
      }
    }
    delete[] bytes;
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "many-writes") == 0) {
    const int N = 256;
    uint8_t* bytes = new uint8_t[N];
    for (int i = 0; i < N; i++) {
      bytes[i] = i & 255;
    }
    for (int i = 0; i < 100; i++) {
      int written = 0;
      while (written < N) {
        int x = write(STDOUT_FILENO, &bytes[written], N - written);
        if (x < 0) {
          perror("write");
          abort();
        }
        written += x;
      }
      sleepms(1);
    }
    delete[] bytes;
    return 0;
  }

  if (argc > 1 && strcmp(argv[1], "many-reads") == 0) {
    const int N = 256;
    uint8_t* bytes = new uint8_t[N];
    for (int i = 0; i < 100; i++) {
      int red = 0;
      while (red < N) {
        int x = read(STDIN_FILENO, &bytes[red], N - red);
        if (x < 0) {
          perror("read");
          abort();
        } else if (x == 0) {
          break;
        }
        red += x;
      }
      if (red != N) {
        abort();
      }
      for (int i = 0; i < N; i++) {
        if (bytes[i] != (i & 255)) {
          abort();
        }
      }
      sleepms(1);
    }
    delete[] bytes;
    return 0;
  }

  return 1;
}
