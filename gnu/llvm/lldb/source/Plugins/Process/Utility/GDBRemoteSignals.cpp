//===-- GDBRemoteSignals.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GDBRemoteSignals.h"

using namespace lldb_private;

GDBRemoteSignals::GDBRemoteSignals() : UnixSignals() { Reset(); }

GDBRemoteSignals::GDBRemoteSignals(const lldb::UnixSignalsSP &rhs)
    : UnixSignals(*rhs) {}

void GDBRemoteSignals::Reset() {
  m_signals.clear();
  // clang-format off
  //        SIGNO   NAME            SUPPRESS  STOP    NOTIFY  DESCRIPTION
  //        ======  ==============  ========  ======  ======  ===================================================
  AddSignal(1,      "SIGHUP",       false,    true,   true,   "hangup");
  AddSignal(2,      "SIGINT",       true,     true,   true,   "interrupt");
  AddSignal(3,      "SIGQUIT",      false,    true,   true,   "quit");
  AddSignal(4,      "SIGILL",       false,    true,   true,   "illegal instruction");
  AddSignal(5,      "SIGTRAP",      true,     true,   true,   "trace trap (not reset when caught)");
  AddSignal(6,      "SIGABRT",      false,    true,   true,   "abort()/IOT trap", "SIGIOT");
  AddSignal(7,      "SIGEMT",       false,    true,   true,   "emulation trap");
  AddSignal(8,      "SIGFPE",       false,    true,   true,   "floating point exception");
  AddSignal(9,      "SIGKILL",      false,    true,   true,   "kill");
  AddSignal(10,     "SIGBUS",       false,    true,   true,   "bus error");
  AddSignal(11,     "SIGSEGV",      false,    true,   true,   "segmentation violation");
  AddSignal(12,     "SIGSYS",       false,    true,   true,   "invalid system call");
  AddSignal(13,     "SIGPIPE",      false,    true,   true,   "write to pipe with reading end closed");
  AddSignal(14,     "SIGALRM",      false,    false,  false,  "alarm");
  AddSignal(15,     "SIGTERM",      false,    true,   true,   "termination requested");
  AddSignal(16,     "SIGURG",       false,    true,   true,   "urgent data on socket");
  AddSignal(17,     "SIGSTOP",      true,     true,   true,   "process stop");
  AddSignal(18,     "SIGTSTP",      false,    true,   true,   "tty stop");
  AddSignal(19,     "SIGCONT",      false,    false,  true,   "process continue");
  AddSignal(20,     "SIGCHLD",      false,    false,  true,   "child status has changed", "SIGCLD");
  AddSignal(21,     "SIGTTIN",      false,    true,   true,   "background tty read");
  AddSignal(22,     "SIGTTOU",      false,    true,   true,   "background tty write");
  AddSignal(23,     "SIGIO",        false,    true,   true,   "input/output ready/Pollable event");
  AddSignal(24,     "SIGXCPU",      false,    true,   true,   "CPU resource exceeded");
  AddSignal(25,     "SIGXFSZ",      false,    true,   true,   "file size limit exceeded");
  AddSignal(26,     "SIGVTALRM",    false,    true,   true,   "virtual time alarm");
  AddSignal(27,     "SIGPROF",      false,    false,  false,  "profiling time alarm");
  AddSignal(28,     "SIGWINCH",     false,    true,   true,   "window size changes");
  AddSignal(29,     "SIGLOST",      false,    true,   true,   "resource lost");
  AddSignal(30,     "SIGUSR1",      false,    true,   true,   "user defined signal 1");
  AddSignal(31,     "SIGUSR2",      false,    true,   true,   "user defined signal 2");
  AddSignal(32,     "SIGPWR",       false,    true,   true,   "power failure");
  AddSignal(33,     "SIGPOLL",      false,    true,   true,   "pollable event");
  AddSignal(34,     "SIGWIND",      false,    true,   true,   "SIGWIND");
  AddSignal(35,    "SIGPHONE",      false,    true,   true,   "SIGPHONE");
  AddSignal(36,  "SIGWAITING",      false,    true,   true,   "process's LWPs are blocked");
  AddSignal(37,      "SIGLWP",      false,    true,   true,   "signal LWP");
  AddSignal(38,   "SIGDANGER",      false,    true,   true,   "swap space dangerously low");
  AddSignal(39,    "SIGGRANT",      false,    true,   true,   "monitor mode granted");
  AddSignal(40,  "SIGRETRACT",      false,    true,   true,   "need to relinquish monitor mode");
  AddSignal(41,      "SIGMSG",      false,    true,   true,   "monitor mode data available");
  AddSignal(42,    "SIGSOUND",      false,    true,   true,   "sound completed");
  AddSignal(43,      "SIGSAK",      false,    true,   true,   "secure attention");
  AddSignal(44,     "SIGPRIO",      false,    true,   true,   "SIGPRIO");

  AddSignal(45,       "SIG33",      false,    false,  false,  "real-time event 33");
  AddSignal(46,       "SIG34",      false,    false,  false,  "real-time event 34");
  AddSignal(47,       "SIG35",      false,    false,  false,  "real-time event 35");
  AddSignal(48,       "SIG36",      false,    false,  false,  "real-time event 36");
  AddSignal(49,       "SIG37",      false,    false,  false,  "real-time event 37");
  AddSignal(50,       "SIG38",      false,    false,  false,  "real-time event 38");
  AddSignal(51,       "SIG39",      false,    false,  false,  "real-time event 39");
  AddSignal(52,       "SIG40",      false,    false,  false,  "real-time event 40");
  AddSignal(53,       "SIG41",      false,    false,  false,  "real-time event 41");
  AddSignal(54,       "SIG42",      false,    false,  false,  "real-time event 42");
  AddSignal(55,       "SIG43",      false,    false,  false,  "real-time event 43");
  AddSignal(56,       "SIG44",      false,    false,  false,  "real-time event 44");
  AddSignal(57,       "SIG45",      false,    false,  false,  "real-time event 45");
  AddSignal(58,       "SIG46",      false,    false,  false,  "real-time event 46");
  AddSignal(59,       "SIG47",      false,    false,  false,  "real-time event 47");
  AddSignal(60,       "SIG48",      false,    false,  false,  "real-time event 48");
  AddSignal(61,       "SIG49",      false,    false,  false,  "real-time event 49");
  AddSignal(62,       "SIG50",      false,    false,  false,  "real-time event 50");
  AddSignal(63,       "SIG51",      false,    false,  false,  "real-time event 51");
  AddSignal(64,       "SIG52",      false,    false,  false,  "real-time event 52");
  AddSignal(65,       "SIG53",      false,    false,  false,  "real-time event 53");
  AddSignal(66,       "SIG54",      false,    false,  false,  "real-time event 54");
  AddSignal(67,       "SIG55",      false,    false,  false,  "real-time event 55");
  AddSignal(68,       "SIG56",      false,    false,  false,  "real-time event 56");
  AddSignal(69,       "SIG57",      false,    false,  false,  "real-time event 57");
  AddSignal(70,       "SIG58",      false,    false,  false,  "real-time event 58");
  AddSignal(71,       "SIG59",      false,    false,  false,  "real-time event 59");
  AddSignal(72,       "SIG60",      false,    false,  false,  "real-time event 60");
  AddSignal(73,       "SIG61",      false,    false,  false,  "real-time event 61");
  AddSignal(74,       "SIG62",      false,    false,  false,  "real-time event 62");
  AddSignal(75,       "SIG63",      false,    false,  false,  "real-time event 63");

  AddSignal(76,   "SIGCANCEL",      false,    true,   true,   "LWP internal signal");

  AddSignal(77,       "SIG32",      false,    false,  false,  "real-time event 32");
  AddSignal(78,       "SIG64",      false,    false,  false,  "real-time event 64");
  AddSignal(79,       "SIG65",      false,    false,  false,  "real-time event 65");
  AddSignal(80,       "SIG66",      false,    false,  false,  "real-time event 66");
  AddSignal(81,       "SIG67",      false,    false,  false,  "real-time event 67");
  AddSignal(82,       "SIG68",      false,    false,  false,  "real-time event 68");
  AddSignal(83,       "SIG69",      false,    false,  false,  "real-time event 69");
  AddSignal(84,       "SIG70",      false,    false,  false,  "real-time event 70");
  AddSignal(85,       "SIG71",      false,    false,  false,  "real-time event 71");
  AddSignal(86,       "SIG72",      false,    false,  false,  "real-time event 72");
  AddSignal(87,       "SIG73",      false,    false,  false,  "real-time event 73");
  AddSignal(88,       "SIG74",      false,    false,  false,  "real-time event 74");
  AddSignal(89,       "SIG75",      false,    false,  false,  "real-time event 75");
  AddSignal(90,       "SIG76",      false,    false,  false,  "real-time event 76");
  AddSignal(91,       "SIG77",      false,    false,  false,  "real-time event 77");
  AddSignal(92,       "SIG78",      false,    false,  false,  "real-time event 78");
  AddSignal(93,       "SIG79",      false,    false,  false,  "real-time event 79");
  AddSignal(94,       "SIG80",      false,    false,  false,  "real-time event 80");
  AddSignal(95,       "SIG81",      false,    false,  false,  "real-time event 81");
  AddSignal(96,       "SIG82",      false,    false,  false,  "real-time event 82");
  AddSignal(97,       "SIG83",      false,    false,  false,  "real-time event 83");
  AddSignal(98,       "SIG84",      false,    false,  false,  "real-time event 84");
  AddSignal(99,       "SIG85",      false,    false,  false,  "real-time event 85");
  AddSignal(100,      "SIG86",      false,    false,  false,  "real-time event 86");
  AddSignal(101,      "SIG87",      false,    false,  false,  "real-time event 87");
  AddSignal(102,      "SIG88",      false,    false,  false,  "real-time event 88");
  AddSignal(103,      "SIG89",      false,    false,  false,  "real-time event 89");
  AddSignal(104,      "SIG90",      false,    false,  false,  "real-time event 90");
  AddSignal(105,      "SIG91",      false,    false,  false,  "real-time event 91");
  AddSignal(106,      "SIG92",      false,    false,  false,  "real-time event 92");
  AddSignal(107,      "SIG93",      false,    false,  false,  "real-time event 93");
  AddSignal(108,      "SIG94",      false,    false,  false,  "real-time event 94");
  AddSignal(109,      "SIG95",      false,    false,  false,  "real-time event 95");
  AddSignal(110,      "SIG96",      false,    false,  false,  "real-time event 96");
  AddSignal(111,      "SIG97",      false,    false,  false,  "real-time event 97");
  AddSignal(112,      "SIG98",      false,    false,  false,  "real-time event 98");
  AddSignal(113,      "SIG99",      false,    false,  false,  "real-time event 99");
  AddSignal(114,     "SIG100",      false,    false,  false,  "real-time event 100");
  AddSignal(115,     "SIG101",      false,    false,  false,  "real-time event 101");
  AddSignal(116,     "SIG102",      false,    false,  false,  "real-time event 102");
  AddSignal(117,     "SIG103",      false,    false,  false,  "real-time event 103");
  AddSignal(118,     "SIG104",      false,    false,  false,  "real-time event 104");
  AddSignal(119,     "SIG105",      false,    false,  false,  "real-time event 105");
  AddSignal(120,     "SIG106",      false,    false,  false,  "real-time event 106");
  AddSignal(121,     "SIG107",      false,    false,  false,  "real-time event 107");
  AddSignal(122,     "SIG108",      false,    false,  false,  "real-time event 108");
  AddSignal(123,     "SIG109",      false,    false,  false,  "real-time event 109");
  AddSignal(124,     "SIG110",      false,    false,  false,  "real-time event 110");
  AddSignal(125,     "SIG111",      false,    false,  false,  "real-time event 111");
  AddSignal(126,     "SIG112",      false,    false,  false,  "real-time event 112");
  AddSignal(127,     "SIG113",      false,    false,  false,  "real-time event 113");
  AddSignal(128,     "SIG114",      false,    false,  false,  "real-time event 114");
  AddSignal(129,     "SIG115",      false,    false,  false,  "real-time event 115");
  AddSignal(130,     "SIG116",      false,    false,  false,  "real-time event 116");
  AddSignal(131,     "SIG117",      false,    false,  false,  "real-time event 117");
  AddSignal(132,     "SIG118",      false,    false,  false,  "real-time event 118");
  AddSignal(133,     "SIG119",      false,    false,  false,  "real-time event 119");
  AddSignal(134,     "SIG120",      false,    false,  false,  "real-time event 120");
  AddSignal(135,     "SIG121",      false,    false,  false,  "real-time event 121");
  AddSignal(136,     "SIG122",      false,    false,  false,  "real-time event 122");
  AddSignal(137,     "SIG123",      false,    false,  false,  "real-time event 123");
  AddSignal(138,     "SIG124",      false,    false,  false,  "real-time event 124");
  AddSignal(139,     "SIG125",      false,    false,  false,  "real-time event 125");
  AddSignal(140,     "SIG126",      false,    false,  false,  "real-time event 126");
  AddSignal(141,     "SIG127",      false,    false,  false,  "real-time event 127");

  AddSignal(142,    "SIGINFO",      false,    true,   true,   "information request");
  AddSignal(143,    "unknown",      false,    true,   true,   "unknown signal");

  AddSignal(145,      "EXC_BAD_ACCESS",       false,  true,   true,   "could not access memory");
  AddSignal(146, "EXC_BAD_INSTRUCTION",       false,  true,   true,   "illegal instruction/operand");
  AddSignal(147,      "EXC_ARITHMETIC",       false,  true,   true,   "arithmetic exception");
  AddSignal(148,       "EXC_EMULATION",       false,  true,   true,   "emulation instruction");
  AddSignal(149,        "EXC_SOFTWARE",       false,  true,   true,   "software generated exception");
  AddSignal(150,      "EXC_BREAKPOINT",       false,  true,   true,   "breakpoint");

  AddSignal(151,   "SIGLIBRT",      false,    true,   true,   "librt internal signal");

  // clang-format on
}
