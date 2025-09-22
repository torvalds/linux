//===-- NetBSDSignals.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NetBSDSignals.h"

#ifdef __NetBSD__
#include <csignal>

#define ADD_SIGCODE(signal_name, signal_value, code_name, code_value, ...)     \
  static_assert(signal_name == signal_value,                                   \
                "Value mismatch for signal number " #signal_name);             \
  static_assert(code_name == code_value,                                       \
                "Value mismatch for signal code " #code_name);                 \
  AddSignalCode(signal_value, code_value, __VA_ARGS__)
#else
#define ADD_SIGCODE(signal_name, signal_value, code_name, code_value, ...)     \
  AddSignalCode(signal_value, code_value, __VA_ARGS__)
#endif /* ifdef __NetBSD */

using namespace lldb_private;

NetBSDSignals::NetBSDSignals() : UnixSignals() { Reset(); }

void NetBSDSignals::Reset() {
  UnixSignals::Reset();

  // clang-format off
  // SIGILL
  ADD_SIGCODE(SIGILL, 4, ILL_ILLOPC, 1, "illegal opcode");
  ADD_SIGCODE(SIGILL, 4, ILL_ILLOPN, 2, "illegal operand");
  ADD_SIGCODE(SIGILL, 4, ILL_ILLADR, 3, "illegal addressing mode");
  ADD_SIGCODE(SIGILL, 4, ILL_ILLTRP, 4, "illegal trap");
  ADD_SIGCODE(SIGILL, 4, ILL_PRVOPC, 5, "privileged opcode");
  ADD_SIGCODE(SIGILL, 4, ILL_PRVREG, 6, "privileged register");
  ADD_SIGCODE(SIGILL, 4, ILL_COPROC, 7, "coprocessor error");
  ADD_SIGCODE(SIGILL, 4, ILL_BADSTK, 8, "internal stack error");

  // SIGFPE
  ADD_SIGCODE(SIGFPE, 8, FPE_INTDIV, 1, "integer divide by zero");
  ADD_SIGCODE(SIGFPE, 8, FPE_INTOVF, 2, "integer overflow");
  ADD_SIGCODE(SIGFPE, 8, FPE_FLTDIV, 3, "floating point divide by zero");
  ADD_SIGCODE(SIGFPE, 8, FPE_FLTOVF, 4, "floating point overflow");
  ADD_SIGCODE(SIGFPE, 8, FPE_FLTUND, 5, "floating point underflow");
  ADD_SIGCODE(SIGFPE, 8, FPE_FLTRES, 6, "floating point inexact result");
  ADD_SIGCODE(SIGFPE, 8, FPE_FLTINV, 7, "invalid floating point operation");
  ADD_SIGCODE(SIGFPE, 8, FPE_FLTSUB, 8, "subscript out of range");

  // SIGBUS
  ADD_SIGCODE(SIGBUS, 10, BUS_ADRALN, 1, "invalid address alignment");
  ADD_SIGCODE(SIGBUS, 10, BUS_ADRERR, 2, "non-existent physical address");
  ADD_SIGCODE(SIGBUS, 10, BUS_OBJERR, 3, "object specific hardware error");

  // SIGSEGV
  ADD_SIGCODE(SIGSEGV, 11, SEGV_MAPERR, 1, "address not mapped to object",
                SignalCodePrintOption::Address);
  ADD_SIGCODE(SIGSEGV, 11, SEGV_ACCERR, 2, "invalid permissions for mapped object",
                SignalCodePrintOption::Address);

  //        SIGNO  NAME          SUPPRESS STOP   NOTIFY DESCRIPTION
  //        ===== ============== ======== ====== ====== ========================
  AddSignal(32,   "SIGPWR",      false,   true,  true,  "power fail/restart (not reset when caught)");
  AddSignal(33,   "SIGRTMIN",    false,   false, false, "real time signal 0");
  AddSignal(34,   "SIGRTMIN+1",  false,   false, false, "real time signal 1");
  AddSignal(35,   "SIGRTMIN+2",  false,   false, false, "real time signal 2");
  AddSignal(36,   "SIGRTMIN+3",  false,   false, false, "real time signal 3");
  AddSignal(37,   "SIGRTMIN+4",  false,   false, false, "real time signal 4");
  AddSignal(38,   "SIGRTMIN+5",  false,   false, false, "real time signal 5");
  AddSignal(39,   "SIGRTMIN+6",  false,   false, false, "real time signal 6");
  AddSignal(40,   "SIGRTMIN+7",  false,   false, false, "real time signal 7");
  AddSignal(41,   "SIGRTMIN+8",  false,   false, false, "real time signal 8");
  AddSignal(42,   "SIGRTMIN+9",  false,   false, false, "real time signal 9");
  AddSignal(43,   "SIGRTMIN+10", false,   false, false, "real time signal 10");
  AddSignal(44,   "SIGRTMIN+11", false,   false, false, "real time signal 11");
  AddSignal(45,   "SIGRTMIN+12", false,   false, false, "real time signal 12");
  AddSignal(46,   "SIGRTMIN+13", false,   false, false, "real time signal 13");
  AddSignal(47,   "SIGRTMIN+14", false,   false, false, "real time signal 14");
  AddSignal(48,   "SIGRTMIN+15", false,   false, false, "real time signal 15");
  AddSignal(49,   "SIGRTMIN-14", false,   false, false, "real time signal 16");
  AddSignal(50,   "SIGRTMAX-13", false,   false, false, "real time signal 17");
  AddSignal(51,   "SIGRTMAX-12", false,   false, false, "real time signal 18");
  AddSignal(52,   "SIGRTMAX-11", false,   false, false, "real time signal 19");
  AddSignal(53,   "SIGRTMAX-10", false,   false, false, "real time signal 20");
  AddSignal(54,   "SIGRTMAX-9",  false,   false, false, "real time signal 21");
  AddSignal(55,   "SIGRTMAX-8",  false,   false, false, "real time signal 22");
  AddSignal(56,   "SIGRTMAX-7",  false,   false, false, "real time signal 23");
  AddSignal(57,   "SIGRTMAX-6",  false,   false, false, "real time signal 24");
  AddSignal(58,   "SIGRTMAX-5",  false,   false, false, "real time signal 25");
  AddSignal(59,   "SIGRTMAX-4",  false,   false, false, "real time signal 26");
  AddSignal(60,   "SIGRTMAX-3",  false,   false, false, "real time signal 27");
  AddSignal(61,   "SIGRTMAX-2",  false,   false, false, "real time signal 28");
  AddSignal(62,   "SIGRTMAX-1",  false,   false, false, "real time signal 29");
  AddSignal(63,   "SIGRTMAX",    false,   false, false, "real time signal 30");
  // clang-format on
}
