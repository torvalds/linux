//== WebAssemblyMemIntrinsicResults.cpp - Optimize memory intrinsic results ==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements an optimization pass using memory intrinsic results.
///
/// Calls to memory intrinsics (memcpy, memmove, memset) return the destination
/// address. They are in the form of
///   %dst_new = call @memcpy %dst, %src, %len
/// where %dst and %dst_new registers contain the same value.
///
/// This is to enable an optimization wherein uses of the %dst register used in
/// the parameter can be replaced by uses of the %dst_new register used in the
/// result, making the %dst register more likely to be single-use, thus more
/// likely to be useful to register stackifying, and potentially also exposing
/// the call instruction itself to register stackifying. These both can reduce
/// local.get/local.set traffic.
///
/// The LLVM intrinsics for these return void so they can't use the returned
/// attribute and consequently aren't handled by the OptimizeReturned pass.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-mem-intrinsic-results"

namespace {
class WebAssemblyMemIntrinsicResults final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyMemIntrinsicResults() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "WebAssembly Memory Intrinsic Results";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
    AU.addPreserved<MachineBlockFrequencyInfoWrapperPass>();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    AU.addRequired<LiveIntervalsWrapperPass>();
    AU.addPreserved<SlotIndexesWrapperPass>();
    AU.addPreserved<LiveIntervalsWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
};
} // end anonymous namespace

char WebAssemblyMemIntrinsicResults::ID = 0;
INITIALIZE_PASS(WebAssemblyMemIntrinsicResults, DEBUG_TYPE,
                "Optimize memory intrinsic result values for WebAssembly",
                false, false)

FunctionPass *llvm::createWebAssemblyMemIntrinsicResults() {
  return new WebAssemblyMemIntrinsicResults();
}

// Replace uses of FromReg with ToReg if they are dominated by MI.
static bool replaceDominatedUses(MachineBasicBlock &MBB, MachineInstr &MI,
                                 unsigned FromReg, unsigned ToReg,
                                 const MachineRegisterInfo &MRI,
                                 MachineDominatorTree &MDT,
                                 LiveIntervals &LIS) {
  bool Changed = false;

  LiveInterval *FromLI = &LIS.getInterval(FromReg);
  LiveInterval *ToLI = &LIS.getInterval(ToReg);

  SlotIndex FromIdx = LIS.getInstructionIndex(MI).getRegSlot();
  VNInfo *FromVNI = FromLI->getVNInfoAt(FromIdx);

  SmallVector<SlotIndex, 4> Indices;

  for (MachineOperand &O :
       llvm::make_early_inc_range(MRI.use_nodbg_operands(FromReg))) {
    MachineInstr *Where = O.getParent();

    // Check that MI dominates the instruction in the normal way.
    if (&MI == Where || !MDT.dominates(&MI, Where))
      continue;

    // If this use gets a different value, skip it.
    SlotIndex WhereIdx = LIS.getInstructionIndex(*Where);
    VNInfo *WhereVNI = FromLI->getVNInfoAt(WhereIdx);
    if (WhereVNI && WhereVNI != FromVNI)
      continue;

    // Make sure ToReg isn't clobbered before it gets there.
    VNInfo *ToVNI = ToLI->getVNInfoAt(WhereIdx);
    if (ToVNI && ToVNI != FromVNI)
      continue;

    Changed = true;
    LLVM_DEBUG(dbgs() << "Setting operand " << O << " in " << *Where << " from "
                      << MI << "\n");
    O.setReg(ToReg);

    // If the store's def was previously dead, it is no longer.
    if (!O.isUndef()) {
      MI.getOperand(0).setIsDead(false);

      Indices.push_back(WhereIdx.getRegSlot());
    }
  }

  if (Changed) {
    // Extend ToReg's liveness.
    LIS.extendToIndices(*ToLI, Indices);

    // Shrink FromReg's liveness.
    LIS.shrinkToUses(FromLI);

    // If we replaced all dominated uses, FromReg is now killed at MI.
    if (!FromLI->liveAt(FromIdx.getDeadSlot()))
      MI.addRegisterKilled(FromReg, MBB.getParent()
                                        ->getSubtarget<WebAssemblySubtarget>()
                                        .getRegisterInfo());
  }

  return Changed;
}

static bool optimizeCall(MachineBasicBlock &MBB, MachineInstr &MI,
                         const MachineRegisterInfo &MRI,
                         MachineDominatorTree &MDT, LiveIntervals &LIS,
                         const WebAssemblyTargetLowering &TLI,
                         const TargetLibraryInfo &LibInfo) {
  MachineOperand &Op1 = MI.getOperand(1);
  if (!Op1.isSymbol())
    return false;

  StringRef Name(Op1.getSymbolName());
  bool CallReturnsInput = Name == TLI.getLibcallName(RTLIB::MEMCPY) ||
                          Name == TLI.getLibcallName(RTLIB::MEMMOVE) ||
                          Name == TLI.getLibcallName(RTLIB::MEMSET);
  if (!CallReturnsInput)
    return false;

  LibFunc Func;
  if (!LibInfo.getLibFunc(Name, Func))
    return false;

  Register FromReg = MI.getOperand(2).getReg();
  Register ToReg = MI.getOperand(0).getReg();
  if (MRI.getRegClass(FromReg) != MRI.getRegClass(ToReg))
    report_fatal_error("Memory Intrinsic results: call to builtin function "
                       "with wrong signature, from/to mismatch");
  return replaceDominatedUses(MBB, MI, FromReg, ToReg, MRI, MDT, LIS);
}

bool WebAssemblyMemIntrinsicResults::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Memory Intrinsic Results **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  MachineRegisterInfo &MRI = MF.getRegInfo();
  auto &MDT = getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  const WebAssemblyTargetLowering &TLI =
      *MF.getSubtarget<WebAssemblySubtarget>().getTargetLowering();
  const auto &LibInfo =
      getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(MF.getFunction());
  auto &LIS = getAnalysis<LiveIntervalsWrapperPass>().getLIS();
  bool Changed = false;

  // We don't preserve SSA form.
  MRI.leaveSSA();

  assert(MRI.tracksLiveness() &&
         "MemIntrinsicResults expects liveness tracking");

  for (auto &MBB : MF) {
    LLVM_DEBUG(dbgs() << "Basic Block: " << MBB.getName() << '\n');
    for (auto &MI : MBB)
      switch (MI.getOpcode()) {
      default:
        break;
      case WebAssembly::CALL:
        Changed |= optimizeCall(MBB, MI, MRI, MDT, LIS, TLI, LibInfo);
        break;
      }
  }

  return Changed;
}
