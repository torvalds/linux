//===-- OpenBSDSignals.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "OpenBSDSignals.h"

using namespace lldb_private;

OpenBSDSignals::OpenBSDSignals() : UnixSignals() { Reset(); }

void OpenBSDSignals::Reset() {
  UnixSignals::Reset();

  //        SIGNO   NAME            SUPPRESS STOP   NOTIFY DESCRIPTION
  //        ======  ============    ======== ====== ======
  //        ===================================================
  AddSignal(32, "SIGTHR", false, false, false, "thread library AST");
}
