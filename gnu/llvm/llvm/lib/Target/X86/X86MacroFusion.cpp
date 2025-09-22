//===- X86MacroFusion.cpp - X86 Macro Fusion ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file contains the X86 implementation of the DAG scheduling
/// mutation to pair instructions back to back.
//
//===----------------------------------------------------------------------===//

#include "X86MacroFusion.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MacroFusion.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

static X86::FirstMacroFusionInstKind classifyFirst(const MachineInstr &MI) {
  return X86::classifyFirstOpcodeInMacroFusion(MI.getOpcode());
}

static X86::SecondMacroFusionInstKind classifySecond(const MachineInstr &MI) {
  X86::CondCode CC = X86::getCondFromBranch(MI);
  return X86::classifySecondCondCodeInMacroFusion(CC);
}

/// Check if the instr pair, FirstMI and SecondMI, should be fused
/// together. Given SecondMI, when FirstMI is unspecified, then check if
/// SecondMI may be part of a fused pair at all.
static bool shouldScheduleAdjacent(const TargetInstrInfo &TII,
                                   const TargetSubtargetInfo &TSI,
                                   const MachineInstr *FirstMI,
                                   const MachineInstr &SecondMI) {
  const X86Subtarget &ST = static_cast<const X86Subtarget &>(TSI);

  // Check if this processor supports any kind of fusion.
  if (!(ST.hasBranchFusion() || ST.hasMacroFusion()))
    return false;

  const X86::SecondMacroFusionInstKind BranchKind = classifySecond(SecondMI);

  if (BranchKind == X86::SecondMacroFusionInstKind::Invalid)
    return false; // Second cannot be fused with anything.

  if (FirstMI == nullptr)
    return true; // We're only checking whether Second can be fused at all.

  const X86::FirstMacroFusionInstKind TestKind = classifyFirst(*FirstMI);

  if (ST.hasBranchFusion()) {
    // Branch fusion can merge CMP and TEST with all conditional jumps.
    return (TestKind == X86::FirstMacroFusionInstKind::Cmp ||
            TestKind == X86::FirstMacroFusionInstKind::Test);
  }

  if (ST.hasMacroFusion()) {
    return X86::isMacroFused(TestKind, BranchKind);
  }

  llvm_unreachable("unknown fusion type");
}

namespace llvm {

std::unique_ptr<ScheduleDAGMutation> createX86MacroFusionDAGMutation() {
  return createMacroFusionDAGMutation(shouldScheduleAdjacent,
                                      /*BranchOnly=*/true);
}

} // end namespace llvm
