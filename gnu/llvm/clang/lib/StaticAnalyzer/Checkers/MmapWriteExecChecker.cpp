// MmapWriteExecChecker.cpp - Check for the prot argument -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This checker tests the 3rd argument of mmap's calls to check if
// it is writable and executable in the same time. It's somehow
// an optional checker since for example in JIT libraries it is pretty common.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"

#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {
class MmapWriteExecChecker : public Checker<check::PreCall> {
  CallDescription MmapFn{CDM::CLibrary, {"mmap"}, 6};
  CallDescription MprotectFn{CDM::CLibrary, {"mprotect"}, 3};
  static int ProtWrite;
  static int ProtExec;
  static int ProtRead;
  const BugType BT{this, "W^X check fails, Write Exec prot flags set",
                   "Security"};

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  int ProtExecOv;
  int ProtReadOv;
};
}

int MmapWriteExecChecker::ProtWrite = 0x02;
int MmapWriteExecChecker::ProtExec  = 0x04;
int MmapWriteExecChecker::ProtRead  = 0x01;

void MmapWriteExecChecker::checkPreCall(const CallEvent &Call,
                                         CheckerContext &C) const {
  if (matchesAny(Call, MmapFn, MprotectFn)) {
    SVal ProtVal = Call.getArgSVal(2);
    auto ProtLoc = ProtVal.getAs<nonloc::ConcreteInt>();
    if (!ProtLoc)
      return;
    int64_t Prot = ProtLoc->getValue().getSExtValue();
    if (ProtExecOv != ProtExec)
      ProtExec = ProtExecOv;
    if (ProtReadOv != ProtRead)
      ProtRead = ProtReadOv;

    // Wrong settings
    if (ProtRead == ProtExec)
      return;

    if ((Prot & (ProtWrite | ProtExec)) == (ProtWrite | ProtExec)) {
      ExplodedNode *N = C.generateNonFatalErrorNode();
      if (!N)
        return;

      auto Report = std::make_unique<PathSensitiveBugReport>(
          BT,
          "Both PROT_WRITE and PROT_EXEC flags are set. This can "
          "lead to exploitable memory regions, which could be overwritten "
          "with malicious code",
          N);
      Report->addRange(Call.getArgSourceRange(2));
      C.emitReport(std::move(Report));
    }
  }
}

void ento::registerMmapWriteExecChecker(CheckerManager &mgr) {
  MmapWriteExecChecker *Mwec =
      mgr.registerChecker<MmapWriteExecChecker>();
  Mwec->ProtExecOv =
    mgr.getAnalyzerOptions()
      .getCheckerIntegerOption(Mwec, "MmapProtExec");
  Mwec->ProtReadOv =
    mgr.getAnalyzerOptions()
      .getCheckerIntegerOption(Mwec, "MmapProtRead");
}

bool ento::shouldRegisterMmapWriteExecChecker(const CheckerManager &mgr) {
  return true;
}
