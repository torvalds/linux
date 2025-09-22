//===- AArch64PostCoalescerPass.cpp - AArch64 Post Coalescer pass ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//

#include "AArch64InstrInfo.h"
#include "AArch64MachineFunctionInfo.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-post-coalescer-pass"

namespace {

struct AArch64PostCoalescer : public MachineFunctionPass {
  static char ID;

  AArch64PostCoalescer() : MachineFunctionPass(ID) {
    initializeAArch64PostCoalescerPass(*PassRegistry::getPassRegistry());
  }

  LiveIntervals *LIS;
  MachineRegisterInfo *MRI;

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "AArch64 Post Coalescer pass";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<LiveIntervalsWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

char AArch64PostCoalescer::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(AArch64PostCoalescer, "aarch64-post-coalescer-pass",
                      "AArch64 Post Coalescer Pass", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_END(AArch64PostCoalescer, "aarch64-post-coalescer-pass",
                    "AArch64 Post Coalescer Pass", false, false)

bool AArch64PostCoalescer::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  AArch64FunctionInfo *FuncInfo = MF.getInfo<AArch64FunctionInfo>();
  if (!FuncInfo->hasStreamingModeChanges())
    return false;

  MRI = &MF.getRegInfo();
  LIS = &getAnalysis<LiveIntervalsWrapperPass>().getLIS();
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : make_early_inc_range(MBB)) {
      switch (MI.getOpcode()) {
      default:
        break;
      case AArch64::COALESCER_BARRIER_FPR16:
      case AArch64::COALESCER_BARRIER_FPR32:
      case AArch64::COALESCER_BARRIER_FPR64:
      case AArch64::COALESCER_BARRIER_FPR128: {
        Register Src = MI.getOperand(1).getReg();
        Register Dst = MI.getOperand(0).getReg();
        if (Src != Dst)
          MRI->replaceRegWith(Dst, Src);

        // MI must be erased from the basic block before recalculating the live
        // interval.
        LIS->RemoveMachineInstrFromMaps(MI);
        MI.eraseFromParent();

        LIS->removeInterval(Src);
        LIS->createAndComputeVirtRegInterval(Src);

        Changed = true;
        break;
      }
      }
    }
  }

  return Changed;
}

FunctionPass *llvm::createAArch64PostCoalescerPass() {
  return new AArch64PostCoalescer();
}
