//===- CGPassBuilderOption.h - Options for pass builder ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the CCState and CCValAssign classes, used for lowering
// and implementing calling conventions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_CGPASSBUILDEROPTION_H
#define LLVM_TARGET_CGPASSBUILDEROPTION_H

#include "llvm/Target/TargetOptions.h"
#include <optional>

namespace llvm {

enum class RunOutliner { TargetDefault, AlwaysOutline, NeverOutline };
enum class RegAllocType { Default, Basic, Fast, Greedy, PBQP };

// Not one-on-one but mostly corresponding to commandline options in
// TargetPassConfig.cpp.
struct CGPassBuilderOption {
  std::optional<bool> OptimizeRegAlloc;
  std::optional<bool> EnableIPRA;
  bool DebugPM = false;
  bool DisableVerify = false;
  bool EnableImplicitNullChecks = false;
  bool EnableBlockPlacementStats = false;
  bool EnableMachineFunctionSplitter = false;
  bool MISchedPostRA = false;
  bool EarlyLiveIntervals = false;
  bool GCEmptyBlocks = false;

  bool DisableLSR = false;
  bool DisableCGP = false;
  bool PrintLSR = false;
  bool DisableMergeICmps = false;
  bool DisablePartialLibcallInlining = false;
  bool DisableConstantHoisting = false;
  bool DisableSelectOptimize = true;
  bool DisableAtExitBasedGlobalDtorLowering = false;
  bool DisableExpandReductions = false;
  bool DisableRAFSProfileLoader = false;
  bool DisableCFIFixup = false;
  bool PrintAfterISel = false;
  bool PrintISelInput = false;
  bool RequiresCodeGenSCCOrder = false;

  RunOutliner EnableMachineOutliner = RunOutliner::TargetDefault;
  StringRef RegAlloc = "default";
  std::optional<GlobalISelAbortMode> EnableGlobalISelAbort;
  std::string FSProfileFile;
  std::string FSRemappingFile;

  std::optional<bool> VerifyMachineCode;
  std::optional<bool> EnableFastISelOption;
  std::optional<bool> EnableGlobalISelOption;
  std::optional<bool> DebugifyAndStripAll;
  std::optional<bool> DebugifyCheckAndStripAll;
};

CGPassBuilderOption getCGPassBuilderOption();

} // namespace llvm

#endif // LLVM_TARGET_CGPASSBUILDEROPTION_H
