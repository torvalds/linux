//===- MachineCycleAnalysis.cpp - Compute CycleInfo for Machine IR --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineCycleAnalysis.h"
#include "llvm/ADT/GenericCycleImpl.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineSSAContext.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

template class llvm::GenericCycleInfo<llvm::MachineSSAContext>;
template class llvm::GenericCycle<llvm::MachineSSAContext>;

char MachineCycleInfoWrapperPass::ID = 0;

MachineCycleInfoWrapperPass::MachineCycleInfoWrapperPass()
    : MachineFunctionPass(ID) {
  initializeMachineCycleInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

INITIALIZE_PASS_BEGIN(MachineCycleInfoWrapperPass, "machine-cycles",
                      "Machine Cycle Info Analysis", true, true)
INITIALIZE_PASS_END(MachineCycleInfoWrapperPass, "machine-cycles",
                    "Machine Cycle Info Analysis", true, true)

void MachineCycleInfoWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool MachineCycleInfoWrapperPass::runOnMachineFunction(MachineFunction &Func) {
  CI.clear();

  F = &Func;
  CI.compute(Func);
  return false;
}

void MachineCycleInfoWrapperPass::print(raw_ostream &OS, const Module *) const {
  OS << "MachineCycleInfo for function: " << F->getName() << "\n";
  CI.print(OS);
}

void MachineCycleInfoWrapperPass::releaseMemory() {
  CI.clear();
  F = nullptr;
}

namespace {
class MachineCycleInfoPrinterPass : public MachineFunctionPass {
public:
  static char ID;

  MachineCycleInfoPrinterPass();

  bool runOnMachineFunction(MachineFunction &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};
} // namespace

char MachineCycleInfoPrinterPass::ID = 0;

MachineCycleInfoPrinterPass::MachineCycleInfoPrinterPass()
    : MachineFunctionPass(ID) {
  initializeMachineCycleInfoPrinterPassPass(*PassRegistry::getPassRegistry());
}

INITIALIZE_PASS_BEGIN(MachineCycleInfoPrinterPass, "print-machine-cycles",
                      "Print Machine Cycle Info Analysis", true, true)
INITIALIZE_PASS_DEPENDENCY(MachineCycleInfoWrapperPass)
INITIALIZE_PASS_END(MachineCycleInfoPrinterPass, "print-machine-cycles",
                    "Print Machine Cycle Info Analysis", true, true)

void MachineCycleInfoPrinterPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MachineCycleInfoWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool MachineCycleInfoPrinterPass::runOnMachineFunction(MachineFunction &F) {
  auto &CI = getAnalysis<MachineCycleInfoWrapperPass>();
  CI.print(errs());
  return false;
}

bool llvm::isCycleInvariant(const MachineCycle *Cycle, MachineInstr &I) {
  MachineFunction *MF = I.getParent()->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  const TargetSubtargetInfo &ST = MF->getSubtarget();
  const TargetRegisterInfo *TRI = ST.getRegisterInfo();
  const TargetInstrInfo *TII = ST.getInstrInfo();

  // The instruction is cycle invariant if all of its operands are.
  for (const MachineOperand &MO : I.operands()) {
    if (!MO.isReg())
      continue;

    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;

    // An instruction that uses or defines a physical register can't e.g. be
    // hoisted, so mark this as not invariant.
    if (Reg.isPhysical()) {
      if (MO.isUse()) {
        // If the physreg has no defs anywhere, it's just an ambient register
        // and we can freely move its uses. Alternatively, if it's allocatable,
        // it could get allocated to something with a def during allocation.
        // However, if the physreg is known to always be caller saved/restored
        // then this use is safe to hoist.
        if (!MRI->isConstantPhysReg(Reg) &&
            !(TRI->isCallerPreservedPhysReg(Reg.asMCReg(), *I.getMF())) &&
            !TII->isIgnorableUse(MO))
          return false;
        // Otherwise it's safe to move.
        continue;
      } else if (!MO.isDead()) {
        // A def that isn't dead can't be moved.
        return false;
      } else if (any_of(Cycle->getEntries(),
                        [&](const MachineBasicBlock *Block) {
                          return Block->isLiveIn(Reg);
                        })) {
        // If the reg is live into any header of the cycle we can't hoist an
        // instruction which would clobber it.
        return false;
      }
    }

    if (!MO.isUse())
      continue;

    assert(MRI->getVRegDef(Reg) && "Machine instr not mapped for this vreg?!");

    // If the cycle contains the definition of an operand, then the instruction
    // isn't cycle invariant.
    if (Cycle->contains(MRI->getVRegDef(Reg)->getParent()))
      return false;
  }

  // If we got this far, the instruction is cycle invariant!
  return true;
}
