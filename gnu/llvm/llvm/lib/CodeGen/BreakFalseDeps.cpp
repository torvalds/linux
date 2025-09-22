//==- llvm/CodeGen/BreakFalseDeps.cpp - Break False Dependency Fix -*- C++ -*==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Break False Dependency pass.
///
/// Some instructions have false dependencies which cause unnecessary stalls.
/// For example, instructions may write part of a register and implicitly
/// need to read the other parts of the register. This may cause unwanted
/// stalls preventing otherwise unrelated instructions from executing in
/// parallel in an out-of-order CPU.
/// This pass is aimed at identifying and avoiding these dependencies.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/ReachingDefAnalysis.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

namespace llvm {

class BreakFalseDeps : public MachineFunctionPass {
private:
  MachineFunction *MF = nullptr;
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  RegisterClassInfo RegClassInfo;

  /// List of undefined register reads in this block in forward order.
  std::vector<std::pair<MachineInstr *, unsigned>> UndefReads;

  /// Storage for register unit liveness.
  LivePhysRegs LiveRegSet;

  ReachingDefAnalysis *RDA = nullptr;

public:
  static char ID; // Pass identification, replacement for typeid

  BreakFalseDeps() : MachineFunctionPass(ID) {
    initializeBreakFalseDepsPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<ReachingDefAnalysis>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
      MachineFunctionProperties::Property::NoVRegs);
  }

private:
  /// Process he given basic block.
  void processBasicBlock(MachineBasicBlock *MBB);

  /// Update def-ages for registers defined by MI.
  /// Also break dependencies on partial defs and undef uses.
  void processDefs(MachineInstr *MI);

  /// Helps avoid false dependencies on undef registers by updating the
  /// machine instructions' undef operand to use a register that the instruction
  /// is truly dependent on, or use a register with clearance higher than Pref.
  /// Returns true if it was able to find a true dependency, thus not requiring
  /// a dependency breaking instruction regardless of clearance.
  bool pickBestRegisterForUndef(MachineInstr *MI, unsigned OpIdx,
    unsigned Pref);

  /// Return true to if it makes sense to break dependence on a partial
  /// def or undef use.
  bool shouldBreakDependence(MachineInstr *, unsigned OpIdx, unsigned Pref);

  /// Break false dependencies on undefined register reads.
  /// Walk the block backward computing precise liveness. This is expensive, so
  /// we only do it on demand. Note that the occurrence of undefined register
  /// reads that should be broken is very rare, but when they occur we may have
  /// many in a single block.
  void processUndefReads(MachineBasicBlock *);
};

} // namespace llvm

#define DEBUG_TYPE "break-false-deps"

char BreakFalseDeps::ID = 0;
INITIALIZE_PASS_BEGIN(BreakFalseDeps, DEBUG_TYPE, "BreakFalseDeps", false, false)
INITIALIZE_PASS_DEPENDENCY(ReachingDefAnalysis)
INITIALIZE_PASS_END(BreakFalseDeps, DEBUG_TYPE, "BreakFalseDeps", false, false)

FunctionPass *llvm::createBreakFalseDeps() { return new BreakFalseDeps(); }

bool BreakFalseDeps::pickBestRegisterForUndef(MachineInstr *MI, unsigned OpIdx,
  unsigned Pref) {

  // We can't change tied operands.
  if (MI->isRegTiedToDefOperand(OpIdx))
    return false;

  MachineOperand &MO = MI->getOperand(OpIdx);
  assert(MO.isUndef() && "Expected undef machine operand");

  // We can't change registers that aren't renamable.
  if (!MO.isRenamable())
    return false;

  MCRegister OriginalReg = MO.getReg().asMCReg();

  // Update only undef operands that have reg units that are mapped to one root.
  for (MCRegUnit Unit : TRI->regunits(OriginalReg)) {
    unsigned NumRoots = 0;
    for (MCRegUnitRootIterator Root(Unit, TRI); Root.isValid(); ++Root) {
      NumRoots++;
      if (NumRoots > 1)
        return false;
    }
  }

  // Get the undef operand's register class
  const TargetRegisterClass *OpRC =
    TII->getRegClass(MI->getDesc(), OpIdx, TRI, *MF);
  assert(OpRC && "Not a valid register class");

  // If the instruction has a true dependency, we can hide the false depdency
  // behind it.
  for (MachineOperand &CurrMO : MI->all_uses()) {
    if (CurrMO.isUndef() || !OpRC->contains(CurrMO.getReg()))
      continue;
    // We found a true dependency - replace the undef register with the true
    // dependency.
    MO.setReg(CurrMO.getReg());
    return true;
  }

  // Go over all registers in the register class and find the register with
  // max clearance or clearance higher than Pref.
  unsigned MaxClearance = 0;
  unsigned MaxClearanceReg = OriginalReg;
  ArrayRef<MCPhysReg> Order = RegClassInfo.getOrder(OpRC);
  for (MCPhysReg Reg : Order) {
    unsigned Clearance = RDA->getClearance(MI, Reg);
    if (Clearance <= MaxClearance)
      continue;
    MaxClearance = Clearance;
    MaxClearanceReg = Reg;

    if (MaxClearance > Pref)
      break;
  }

  // Update the operand if we found a register with better clearance.
  if (MaxClearanceReg != OriginalReg)
    MO.setReg(MaxClearanceReg);

  return false;
}

bool BreakFalseDeps::shouldBreakDependence(MachineInstr *MI, unsigned OpIdx,
                                           unsigned Pref) {
  MCRegister Reg = MI->getOperand(OpIdx).getReg().asMCReg();
  unsigned Clearance = RDA->getClearance(MI, Reg);
  LLVM_DEBUG(dbgs() << "Clearance: " << Clearance << ", want " << Pref);

  if (Pref > Clearance) {
    LLVM_DEBUG(dbgs() << ": Break dependency.\n");
    return true;
  }
  LLVM_DEBUG(dbgs() << ": OK .\n");
  return false;
}

void BreakFalseDeps::processDefs(MachineInstr *MI) {
  assert(!MI->isDebugInstr() && "Won't process debug values");

  const MCInstrDesc &MCID = MI->getDesc();

  // Break dependence on undef uses. Do this before updating LiveRegs below.
  // This can remove a false dependence with no additional instructions.
  for (unsigned i = MCID.getNumDefs(), e = MCID.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg() || !MO.getReg() || !MO.isUse() || !MO.isUndef())
      continue;

    unsigned Pref = TII->getUndefRegClearance(*MI, i, TRI);
    if (Pref) {
      bool HadTrueDependency = pickBestRegisterForUndef(MI, i, Pref);
      // We don't need to bother trying to break a dependency if this
      // instruction has a true dependency on that register through another
      // operand - we'll have to wait for it to be available regardless.
      if (!HadTrueDependency && shouldBreakDependence(MI, i, Pref))
        UndefReads.push_back(std::make_pair(MI, i));
    }
  }

  // The code below allows the target to create a new instruction to break the
  // dependence. That opposes the goal of minimizing size, so bail out now.
  if (MF->getFunction().hasMinSize())
    return;

  for (unsigned i = 0,
    e = MI->isVariadic() ? MI->getNumOperands() : MCID.getNumDefs();
    i != e; ++i) {
    MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg() || !MO.getReg())
      continue;
    if (MO.isUse())
      continue;
    // Check clearance before partial register updates.
    unsigned Pref = TII->getPartialRegUpdateClearance(*MI, i, TRI);
    if (Pref && shouldBreakDependence(MI, i, Pref))
      TII->breakPartialRegDependency(*MI, i, TRI);
  }
}

void BreakFalseDeps::processUndefReads(MachineBasicBlock *MBB) {
  if (UndefReads.empty())
    return;

  // The code below allows the target to create a new instruction to break the
  // dependence. That opposes the goal of minimizing size, so bail out now.
  if (MF->getFunction().hasMinSize())
    return;

  // Collect this block's live out register units.
  LiveRegSet.init(*TRI);
  // We do not need to care about pristine registers as they are just preserved
  // but not actually used in the function.
  LiveRegSet.addLiveOutsNoPristines(*MBB);

  MachineInstr *UndefMI = UndefReads.back().first;
  unsigned OpIdx = UndefReads.back().second;

  for (MachineInstr &I : llvm::reverse(*MBB)) {
    // Update liveness, including the current instruction's defs.
    LiveRegSet.stepBackward(I);

    if (UndefMI == &I) {
      if (!LiveRegSet.contains(UndefMI->getOperand(OpIdx).getReg()))
        TII->breakPartialRegDependency(*UndefMI, OpIdx, TRI);

      UndefReads.pop_back();
      if (UndefReads.empty())
        return;

      UndefMI = UndefReads.back().first;
      OpIdx = UndefReads.back().second;
    }
  }
}

void BreakFalseDeps::processBasicBlock(MachineBasicBlock *MBB) {
  UndefReads.clear();
  // If this block is not done, it makes little sense to make any decisions
  // based on clearance information. We need to make a second pass anyway,
  // and by then we'll have better information, so we can avoid doing the work
  // to try and break dependencies now.
  for (MachineInstr &MI : *MBB) {
    if (!MI.isDebugInstr())
      processDefs(&MI);
  }
  processUndefReads(MBB);
}

bool BreakFalseDeps::runOnMachineFunction(MachineFunction &mf) {
  if (skipFunction(mf.getFunction()))
    return false;
  MF = &mf;
  TII = MF->getSubtarget().getInstrInfo();
  TRI = MF->getSubtarget().getRegisterInfo();
  RDA = &getAnalysis<ReachingDefAnalysis>();

  RegClassInfo.runOnMachineFunction(mf);

  LLVM_DEBUG(dbgs() << "********** BREAK FALSE DEPENDENCIES **********\n");

  // Skip Dead blocks due to ReachingDefAnalysis has no idea about instructions
  // in them.
  df_iterator_default_set<MachineBasicBlock *> Reachable;
  for (MachineBasicBlock *MBB : depth_first_ext(&mf, Reachable))
    (void)MBB /* Mark all reachable blocks */;

  // Traverse the basic blocks.
  for (MachineBasicBlock &MBB : mf)
    if (Reachable.count(&MBB))
      processBasicBlock(&MBB);

  return false;
}
