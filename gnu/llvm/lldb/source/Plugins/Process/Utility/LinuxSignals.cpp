//===-- LinuxSignals.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinuxSignals.h"

#ifdef __linux__
#include <csignal>

#ifndef SEGV_BNDERR
#define SEGV_BNDERR 3
#endif
#ifndef SEGV_MTEAERR
#define SEGV_MTEAERR 8
#endif
#ifndef SEGV_MTESERR
#define SEGV_MTESERR 9
#endif

#define ADD_SIGCODE(signal_name, signal_value, code_name, code_value, ...)     \
  static_assert(signal_name == signal_value,                                   \
                "Value mismatch for signal number " #signal_name);             \
  static_assert(code_name == code_value,                                       \
                "Value mismatch for signal code " #code_name);                 \
  AddSignalCode(signal_value, code_value, __VA_ARGS__)
#else
#define ADD_SIGCODE(signal_name, signal_value, code_name, code_value, ...)     \
  AddSignalCode(signal_value, code_value, __VA_ARGS__)
#endif /* ifdef __linux__ */

using namespace lldb_private;

LinuxSignals::LinuxSignals() : UnixSignals() { Reset(); }

void LinuxSignals::Reset() {
  m_signals.clear();
  // clang-format off
  //        SIGNO   NAME            SUPPRESS  STOP    NOTIFY  DESCRIPTION
  //        ======  ==============  ========  ======  ======  ===================================================
  AddSignal(1,      "SIGHUP",       false,    true,   true,   "hangup");
  AddSignal(2,      "SIGINT",       true,     true,   true,   "interrupt");
  AddSignal(3,      "SIGQUIT",      false,    true,   true,   "quit");

  AddSignal(4,      "SIGILL",       false,    true,   true,   "illegal instruction");
  ADD_SIGCODE(SIGILL, 4, ILL_ILLOPC, 1, "illegal opcode");
  ADD_SIGCODE(SIGILL, 4, ILL_ILLOPN, 2, "illegal operand");
  ADD_SIGCODE(SIGILL, 4, ILL_ILLADR, 3, "illegal addressing mode");
  ADD_SIGCODE(SIGILL, 4, ILL_ILLTRP, 4, "illegal trap");
  ADD_SIGCODE(SIGILL, 4, ILL_PRVOPC, 5, "privileged opcode");
  ADD_SIGCODE(SIGILL, 4, ILL_PRVREG, 6, "privileged register");
  ADD_SIGCODE(SIGILL, 4, ILL_COPROC, 7, "coprocessor error");
  ADD_SIGCODE(SIGILL, 4, ILL_BADSTK, 8, "internal stack error");

  AddSignal(5,      "SIGTRAP",      true,     true,   true,   "trace trap (not reset when caught)");
  AddSignal(6,      "SIGABRT",      false,    true,   true,   "abort()/IOT trap", "SIGIOT");

  AddSignal(7,      "SIGBUS",       false,    true,   true,   "bus error");
  ADD_SIGCODE(SIGBUS, 7, BUS_ADRALN, 1, "illegal alignment");
  ADD_SIGCODE(SIGBUS, 7, BUS_ADRERR, 2, "illegal address");
  ADD_SIGCODE(SIGBUS, 7, BUS_OBJERR, 3, "hardware error");

  AddSignal(8,      "SIGFPE",       false,    true,   true,   "floating point exception");
  ADD_SIGCODE(SIGFPE, 8, FPE_INTDIV, 1, "integer divide by zero");
  ADD_SIGCODE(SIGFPE, 8, FPE_INTOVF, 2, "integer overflow");
  ADD_SIGCODE(SIGFPE, 8, FPE_FLTDIV, 3, "floating point divide by zero");
  ADD_SIGCODE(SIGFPE, 8, FPE_FLTOVF, 4, "floating point overflow");
  ADD_SIGCODE(SIGFPE, 8, FPE_FLTUND, 5, "floating point underflow");
  ADD_SIGCODE(SIGFPE, 8, FPE_FLTRES, 6, "floating point inexact result");
  ADD_SIGCODE(SIGFPE, 8, FPE_FLTINV, 7, "floating point invalid operation");
  ADD_SIGCODE(SIGFPE, 8, FPE_FLTSUB, 8, "subscript out of range");

  AddSignal(9,      "SIGKILL",      false,    true,   true,   "kill");
  AddSignal(10,     "SIGUSR1",      false,    true,   true,   "user defined signal 1");

  AddSignal(11,     "SIGSEGV",      false,    true,   true,   "segmentation violation");
  ADD_SIGCODE(SIGSEGV, 11, SEGV_MAPERR,  1, "address not mapped to object", SignalCodePrintOption::Address);
  ADD_SIGCODE(SIGSEGV, 11, SEGV_ACCERR,  2, "invalid permissions for mapped object", SignalCodePrintOption::Address);
  ADD_SIGCODE(SIGSEGV, 11, SEGV_BNDERR,  3, "failed address bounds checks", SignalCodePrintOption::Bounds);
  ADD_SIGCODE(SIGSEGV, 11, SEGV_MTEAERR, 8, "async tag check fault");
  ADD_SIGCODE(SIGSEGV, 11, SEGV_MTESERR, 9, "sync tag check fault", SignalCodePrintOption::Address);
  // Some platforms will occasionally send nonstandard spurious SI_KERNEL
  // codes. One way to get this is via unaligned SIMD loads. Treat it as invalid address.
  ADD_SIGCODE(SIGSEGV, 11, SI_KERNEL, 0x80, "invalid address", SignalCodePrintOption::Address);

  AddSignal(12,     "SIGUSR2",      false,    true,   true,   "user defined signal 2");
  AddSignal(13,     "SIGPIPE",      false,    true,   true,   "write to pipe with reading end closed");
  AddSignal(14,     "SIGALRM",      false,    false,  false,  "alarm");
  AddSignal(15,     "SIGTERM",      false,    true,   true,   "termination requested");
  AddSignal(16,     "SIGSTKFLT",    false,    true,   true,   "stack fault");
  AddSignal(17,     "SIGCHLD",      false,    false,  true,   "child status has changed", "SIGCLD");
  AddSignal(18,     "SIGCONT",      false,    false,  true,   "process continue");
  AddSignal(19,     "SIGSTOP",      true,     true,   true,   "process stop");
  AddSignal(20,     "SIGTSTP",      false,    true,   true,   "tty stop");
  AddSignal(21,     "SIGTTIN",      false,    true,   true,   "background tty read");
  AddSignal(22,     "SIGTTOU",      false,    true,   true,   "background tty write");
  AddSignal(23,     "SIGURG",       false,    true,   true,   "urgent data on socket");
  AddSignal(24,     "SIGXCPU",      false,    true,   true,   "CPU resource exceeded");
  AddSignal(25,     "SIGXFSZ",      false,    true,   true,   "file size limit exceeded");
  AddSignal(26,     "SIGVTALRM",    false,    true,   true,   "virtual time alarm");
  AddSignal(27,     "SIGPROF",      false,    false,  false,  "profiling time alarm");
  AddSignal(28,     "SIGWINCH",     false,    true,   true,   "window size changes");
  AddSignal(29,     "SIGIO",        false,    true,   true,   "input/output ready/Pollable event", "SIGPOLL");
  AddSignal(30,     "SIGPWR",       false,    true,   true,   "power failure");
  AddSignal(31,     "SIGSYS",       false,    true,   true,   "invalid system call");
  AddSignal(32,     "SIG32",        false,    false,  false,  "threading library internal signal 1");
  AddSignal(33,     "SIG33",        false,    false,  false,  "threading library internal signal 2");
  AddSignal(34,     "SIGRTMIN",     false,    false,  false,  "real time signal 0");
  AddSignal(35,     "SIGRTMIN+1",   false,    false,  false,  "real time signal 1");
  AddSignal(36,     "SIGRTMIN+2",   false,    false,  false,  "real time signal 2");
  AddSignal(37,     "SIGRTMIN+3",   false,    false,  false,  "real time signal 3");
  AddSignal(38,     "SIGRTMIN+4",   false,    false,  false,  "real time signal 4");
  AddSignal(39,     "SIGRTMIN+5",   false,    false,  false,  "real time signal 5");
  AddSignal(40,     "SIGRTMIN+6",   false,    false,  false,  "real time signal 6");
  AddSignal(41,     "SIGRTMIN+7",   false,    false,  false,  "real time signal 7");
  AddSignal(42,     "SIGRTMIN+8",   false,    false,  false,  "real time signal 8");
  AddSignal(43,     "SIGRTMIN+9",   false,    false,  false,  "real time signal 9");
  AddSignal(44,     "SIGRTMIN+10",  false,    false,  false,  "real time signal 10");
  AddSignal(45,     "SIGRTMIN+11",  false,    false,  false,  "real time signal 11");
  AddSignal(46,     "SIGRTMIN+12",  false,    false,  false,  "real time signal 12");
  AddSignal(47,     "SIGRTMIN+13",  false,    false,  false,  "real time signal 13");
  AddSignal(48,     "SIGRTMIN+14",  false,    false,  false,  "real time signal 14");
  AddSignal(49,     "SIGRTMIN+15",  false,    false,  false,  "real time signal 15");
  AddSignal(50,     "SIGRTMAX-14",  false,    false,  false,  "real time signal 16"); // switching to SIGRTMAX-xxx to match "kill -l" output
  AddSignal(51,     "SIGRTMAX-13",  false,    false,  false,  "real time signal 17");
  AddSignal(52,     "SIGRTMAX-12",  false,    false,  false,  "real time signal 18");
  AddSignal(53,     "SIGRTMAX-11",  false,    false,  false,  "real time signal 19");
  AddSignal(54,     "SIGRTMAX-10",  false,    false,  false,  "real time signal 20");
  AddSignal(55,     "SIGRTMAX-9",   false,    false,  false,  "real time signal 21");
  AddSignal(56,     "SIGRTMAX-8",   false,    false,  false,  "real time signal 22");
  AddSignal(57,     "SIGRTMAX-7",   false,    false,  false,  "real time signal 23");
  AddSignal(58,     "SIGRTMAX-6",   false,    false,  false,  "real time signal 24");
  AddSignal(59,     "SIGRTMAX-5",   false,    false,  false,  "real time signal 25");
  AddSignal(60,     "SIGRTMAX-4",   false,    false,  false,  "real time signal 26");
  AddSignal(61,     "SIGRTMAX-3",   false,    false,  false,  "real time signal 27");
  AddSignal(62,     "SIGRTMAX-2",   false,    false,  false,  "real time signal 28");
  AddSignal(63,     "SIGRTMAX-1",   false,    false,  false,  "real time signal 29");
  AddSignal(64,     "SIGRTMAX",     false,    false,  false,  "real time signal 30");
  // clang-format on
}
