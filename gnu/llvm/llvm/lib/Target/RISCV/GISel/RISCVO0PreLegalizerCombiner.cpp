//=== RISCVO0PreLegalizerCombiner.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does combining of machine instructions at the generic MI level,
// before the legalizer.
//
//===----------------------------------------------------------------------===//

#include "RISCVSubtarget.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"

#define GET_GICOMBINER_DEPS
#include "RISCVGenO0PreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_DEPS

#define DEBUG_TYPE "riscv-O0-prelegalizer-combiner"

using namespace llvm;

namespace {
#define GET_GICOMBINER_TYPES
#include "RISCVGenO0PreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_TYPES

class RISCVO0PreLegalizerCombinerImpl : public Combiner {
protected:
  // TODO: Make CombinerHelper methods const.
  mutable CombinerHelper Helper;
  const RISCVO0PreLegalizerCombinerImplRuleConfig &RuleConfig;
  const RISCVSubtarget &STI;

public:
  RISCVO0PreLegalizerCombinerImpl(
      MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
      GISelKnownBits &KB, GISelCSEInfo *CSEInfo,
      const RISCVO0PreLegalizerCombinerImplRuleConfig &RuleConfig,
      const RISCVSubtarget &STI);

  static const char *getName() { return "RISCVO0PreLegalizerCombiner"; }

  bool tryCombineAll(MachineInstr &I) const override;

private:
#define GET_GICOMBINER_CLASS_MEMBERS
#include "RISCVGenO0PreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CLASS_MEMBERS
};

#define GET_GICOMBINER_IMPL
#include "RISCVGenO0PreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_IMPL

RISCVO0PreLegalizerCombinerImpl::RISCVO0PreLegalizerCombinerImpl(
    MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
    GISelKnownBits &KB, GISelCSEInfo *CSEInfo,
    const RISCVO0PreLegalizerCombinerImplRuleConfig &RuleConfig,
    const RISCVSubtarget &STI)
    : Combiner(MF, CInfo, TPC, &KB, CSEInfo),
      Helper(Observer, B, /*IsPreLegalize*/ true, &KB), RuleConfig(RuleConfig),
      STI(STI),
#define GET_GICOMBINER_CONSTRUCTOR_INITS
#include "RISCVGenO0PreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CONSTRUCTOR_INITS
{
}

// Pass boilerplate
// ================

class RISCVO0PreLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  RISCVO0PreLegalizerCombiner();

  StringRef getPassName() const override {
    return "RISCVO0PreLegalizerCombiner";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  RISCVO0PreLegalizerCombinerImplRuleConfig RuleConfig;
};
} // end anonymous namespace

void RISCVO0PreLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.setPreservesCFG();
  getSelectionDAGFallbackAnalysisUsage(AU);
  AU.addRequired<GISelKnownBitsAnalysis>();
  AU.addPreserved<GISelKnownBitsAnalysis>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

RISCVO0PreLegalizerCombiner::RISCVO0PreLegalizerCombiner()
    : MachineFunctionPass(ID) {
  initializeRISCVO0PreLegalizerCombinerPass(*PassRegistry::getPassRegistry());

  if (!RuleConfig.parseCommandLineOption())
    report_fatal_error("Invalid rule identifier");
}

bool RISCVO0PreLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;
  auto &TPC = getAnalysis<TargetPassConfig>();

  const Function &F = MF.getFunction();
  GISelKnownBits *KB = &getAnalysis<GISelKnownBitsAnalysis>().get(MF);

  const RISCVSubtarget &ST = MF.getSubtarget<RISCVSubtarget>();

  CombinerInfo CInfo(/*AllowIllegalOps*/ true, /*ShouldLegalizeIllegal*/ false,
                     /*LegalizerInfo*/ nullptr, /*EnableOpt*/ false,
                     F.hasOptSize(), F.hasMinSize());
  RISCVO0PreLegalizerCombinerImpl Impl(MF, CInfo, &TPC, *KB,
                                       /*CSEInfo*/ nullptr, RuleConfig, ST);
  return Impl.combineMachineInstrs();
}

char RISCVO0PreLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(RISCVO0PreLegalizerCombiner, DEBUG_TYPE,
                      "Combine RISC-V machine instrs before legalization", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelKnownBitsAnalysis)
INITIALIZE_PASS_DEPENDENCY(GISelCSEAnalysisWrapperPass)
INITIALIZE_PASS_END(RISCVO0PreLegalizerCombiner, DEBUG_TYPE,
                    "Combine RISC-V machine instrs before legalization", false,
                    false)

namespace llvm {
FunctionPass *createRISCVO0PreLegalizerCombiner() {
  return new RISCVO0PreLegalizerCombiner();
}
} // end namespace llvm
