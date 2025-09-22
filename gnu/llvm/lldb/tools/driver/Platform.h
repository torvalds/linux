//===-- Platform.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DRIVER_PLATFORM_H
#define LLDB_TOOLS_DRIVER_PLATFORM_H

#if defined(_WIN32)

#include <io.h>
#if defined(_MSC_VER)
#include <csignal>
#endif

#include "lldb/Host/windows/windows.h"
#include <cinttypes>
#include <sys/types.h>

struct winsize {
  long ws_col;
};

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

// fcntl.h
#define O_NOCTTY 0400

// ioctls.h
#define TIOCGWINSZ 0x5413

// signal.h
#define SIGPIPE 13
#define SIGCONT 18
#define SIGTSTP 20
#define SIGWINCH 28

// tcsetattr arguments
#define TCSANOW 0

#define NCCS 32
struct termios {
  tcflag_t c_iflag; // input mode flags
  tcflag_t c_oflag; // output mode flags
  tcflag_t c_cflag; // control mode flags
  tcflag_t c_lflag; // local mode flags
  cc_t c_line;      // line discipline
  cc_t c_cc[NCCS];  // control characters
  speed_t c_ispeed; // input speed
  speed_t c_ospeed; // output speed
};

#ifdef _MSC_VER
struct timeval {
  long tv_sec;
  long tv_usec;
};
typedef long pid_t;
#define PATH_MAX MAX_PATH
#endif

#define STDIN_FILENO 0

extern int ioctl(int d, int request, ...);
extern int kill(pid_t pid, int sig);
extern int tcsetattr(int fd, int optional_actions,
                     const struct termios *termios_p);
extern int tcgetattr(int fildes, struct termios *termios_p);

#else
#include <cinttypes>

#include <libgen.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/time.h>
#endif

#endif // LLDB_TOOLS_DRIVER_PLATFORM_H
