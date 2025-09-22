//===-- Platform.cpp --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// this file is only relevant for Visual C++
#if defined(_WIN32)

#include <cassert>
#include <cstdlib>
#include <process.h>

#include "Platform.h"
#include "llvm/Support/ErrorHandling.h"

int ioctl(int d, int request, ...) {
  switch (request) {
  // request the console windows size
  case (TIOCGWINSZ): {
    va_list vl;
    va_start(vl, request);
    // locate the window size structure on stack
    winsize *ws = va_arg(vl, winsize *);
    // get screen buffer information
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) ==
        TRUE)
      // fill in the columns
      ws->ws_col = info.dwMaximumWindowSize.X;
    va_end(vl);
    return 0;
  } break;
  default:
    llvm_unreachable("Not implemented!");
  }
}

int kill(pid_t pid, int sig) {
  // is the app trying to kill itself
  if (pid == getpid())
    exit(sig);
  //
  llvm_unreachable("Not implemented!");
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
  llvm_unreachable("Not implemented!");
}

int tcgetattr(int fildes, struct termios *termios_p) {
  //  assert( !"Not implemented!" );
  // error return value (0=success)
  return -1;
}

#endif
