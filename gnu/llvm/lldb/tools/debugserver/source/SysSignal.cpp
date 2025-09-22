//===-- SysSignal.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/18/07.
//
//===----------------------------------------------------------------------===//

#include "SysSignal.h"
#include <csignal>
#include <cstddef>

const char *SysSignal::Name(int signal) {
  switch (signal) {
  case SIGHUP:
    return "SIGHUP"; // 1    hangup
  case SIGINT:
    return "SIGINT"; // 2    interrupt
  case SIGQUIT:
    return "SIGQUIT"; // 3    quit
  case SIGILL:
    return "SIGILL"; // 4    illegal instruction (not reset when caught)
  case SIGTRAP:
    return "SIGTRAP"; // 5    trace trap (not reset when caught)
  case SIGABRT:
    return "SIGABRT"; // 6    abort()
#if defined(_POSIX_C_SOURCE)
  case SIGPOLL:
    return "SIGPOLL"; // 7    pollable event ([XSR] generated, not supported)
#else                 // !_POSIX_C_SOURCE
  case SIGEMT:
    return "SIGEMT"; // 7    EMT instruction
#endif                // !_POSIX_C_SOURCE
  case SIGFPE:
    return "SIGFPE"; // 8    floating point exception
  case SIGKILL:
    return "SIGKILL"; // 9    kill (cannot be caught or ignored)
  case SIGBUS:
    return "SIGBUS"; // 10    bus error
  case SIGSEGV:
    return "SIGSEGV"; // 11    segmentation violation
  case SIGSYS:
    return "SIGSYS"; // 12    bad argument to system call
  case SIGPIPE:
    return "SIGPIPE"; // 13    write on a pipe with no one to read it
  case SIGALRM:
    return "SIGALRM"; // 14    alarm clock
  case SIGTERM:
    return "SIGTERM"; // 15    software termination signal from kill
  case SIGURG:
    return "SIGURG"; // 16    urgent condition on IO channel
  case SIGSTOP:
    return "SIGSTOP"; // 17    sendable stop signal not from tty
  case SIGTSTP:
    return "SIGTSTP"; // 18    stop signal from tty
  case SIGCONT:
    return "SIGCONT"; // 19    continue a stopped process
  case SIGCHLD:
    return "SIGCHLD"; // 20    to parent on child stop or exit
  case SIGTTIN:
    return "SIGTTIN"; // 21    to readers pgrp upon background tty read
  case SIGTTOU:
    return "SIGTTOU"; // 22    like TTIN for output if (tp->t_local&LTOSTOP)
#if !defined(_POSIX_C_SOURCE)
  case SIGIO:
    return "SIGIO"; // 23    input/output possible signal
#endif
  case SIGXCPU:
    return "SIGXCPU"; // 24    exceeded CPU time limit
  case SIGXFSZ:
    return "SIGXFSZ"; // 25    exceeded file size limit
  case SIGVTALRM:
    return "SIGVTALRM"; // 26    virtual time alarm
  case SIGPROF:
    return "SIGPROF"; // 27    profiling time alarm
#if !defined(_POSIX_C_SOURCE)
  case SIGWINCH:
    return "SIGWINCH"; // 28    window size changes
  case SIGINFO:
    return "SIGINFO"; // 29    information request
#endif
  case SIGUSR1:
    return "SIGUSR1"; // 30    user defined signal 1
  case SIGUSR2:
    return "SIGUSR2"; // 31    user defined signal 2
  default:
    break;
  }
  return NULL;
}
