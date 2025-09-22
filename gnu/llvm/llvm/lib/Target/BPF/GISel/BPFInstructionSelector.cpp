//===- BPFInstructionSelector.cpp --------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the targeting of the InstructionSelector class for BPF.
//===----------------------------------------------------------------------===//

#include "BPFInstrInfo.h"
#include "BPFRegisterBankInfo.h"
#include "BPFSubtarget.h"
#include "BPFTargetMachine.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/IntrinsicsBPF.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "bpf-gisel"

using namespace llvm;

namespace {

#define GET_GLOBALISEL_PREDICATE_BITSET
#include "BPFGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATE_BITSET

class BPFInstructionSelector : public InstructionSelector {
public:
  BPFInstructionSelector(const BPFTargetMachine &TM, const BPFSubtarget &STI,
                         const BPFRegisterBankInfo &RBI);

  bool select(MachineInstr &I) override;
  static const char *getName() { return DEBUG_TYPE; }

private:
  /// tblgen generated 'select' implementation that is used as the initial
  /// selector for the patterns that do not require complex C++.
  bool selectImpl(MachineInstr &I, CodeGenCoverage &CoverageInfo) const;

  const BPFInstrInfo &TII;
  const BPFRegisterInfo &TRI;
  const BPFRegisterBankInfo &RBI;

#define GET_GLOBALISEL_PREDICATES_DECL
#include "BPFGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_DECL

#define GET_GLOBALISEL_TEMPORARIES_DECL
#include "BPFGenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_DECL
};

} // namespace

#define GET_GLOBALISEL_IMPL
#include "BPFGenGlobalISel.inc"
#undef GET_GLOBALISEL_IMPL

BPFInstructionSelector::BPFInstructionSelector(const BPFTargetMachine &TM,
                                               const BPFSubtarget &STI,
                                               const BPFRegisterBankInfo &RBI)
    : TII(*STI.getInstrInfo()), TRI(*STI.getRegisterInfo()), RBI(RBI),
#define GET_GLOBALISEL_PREDICATES_INIT
#include "BPFGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_INIT
#define GET_GLOBALISEL_TEMPORARIES_INIT
#include "BPFGenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_INIT
{
}

bool BPFInstructionSelector::select(MachineInstr &I) {
  if (!isPreISelGenericOpcode(I.getOpcode()))
    return true;
  if (selectImpl(I, *CoverageInfo))
    return true;
  return false;
}

namespace llvm {
InstructionSelector *
createBPFInstructionSelector(const BPFTargetMachine &TM,
                             const BPFSubtarget &Subtarget,
                             const BPFRegisterBankInfo &RBI) {
  return new BPFInstructionSelector(TM, Subtarget, RBI);
}
} // namespace llvm
