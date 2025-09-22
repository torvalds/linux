//===---- KCFI.cpp - Implements Kernel Control-Flow Integrity (KCFI) ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements Kernel Control-Flow Integrity (KCFI) indirect call
// check lowering. For each call instruction with a cfi-type attribute, it
// emits an arch-specific check before the call, and bundles the check and
// the call to prevent unintentional modifications.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "kcfi"
#define KCFI_PASS_NAME "Insert KCFI indirect call checks"

STATISTIC(NumKCFIChecksAdded, "Number of indirect call checks added");

namespace {
class KCFI : public MachineFunctionPass {
public:
  static char ID;

  KCFI() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return KCFI_PASS_NAME; }
  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  /// Machine instruction info used throughout the class.
  const TargetInstrInfo *TII = nullptr;

  /// Target lowering for arch-specific parts.
  const TargetLowering *TLI = nullptr;

  /// Emits a KCFI check before an indirect call.
  /// \returns true if the check was added and false otherwise.
  bool emitCheck(MachineBasicBlock &MBB,
                 MachineBasicBlock::instr_iterator I) const;
};

char KCFI::ID = 0;
} // end anonymous namespace

INITIALIZE_PASS(KCFI, DEBUG_TYPE, KCFI_PASS_NAME, false, false)

FunctionPass *llvm::createKCFIPass() { return new KCFI(); }

bool KCFI::emitCheck(MachineBasicBlock &MBB,
                     MachineBasicBlock::instr_iterator MBBI) const {
  assert(TII && "Target instruction info was not initialized");
  assert(TLI && "Target lowering was not initialized");

  // If the call instruction is bundled, we can only emit a check safely if
  // it's the first instruction in the bundle.
  if (MBBI->isBundled() && !std::prev(MBBI)->isBundle())
    report_fatal_error("Cannot emit a KCFI check for a bundled call");

  // Emit a KCFI check for the call instruction at MBBI. The implementation
  // must unfold memory operands if applicable.
  MachineInstr *Check = TLI->EmitKCFICheck(MBB, MBBI, TII);

  // Clear the original call's CFI type.
  assert(MBBI->isCall() && "Unexpected instruction type");
  MBBI->setCFIType(*MBB.getParent(), 0);

  // If not already bundled, bundle the check and the call to prevent
  // further changes.
  if (!MBBI->isBundled())
    finalizeBundle(MBB, Check->getIterator(), std::next(MBBI->getIterator()));

  ++NumKCFIChecksAdded;
  return true;
}

bool KCFI::runOnMachineFunction(MachineFunction &MF) {
  const Module *M = MF.getFunction().getParent();
  if (!M->getModuleFlag("kcfi"))
    return false;

  const auto &SubTarget = MF.getSubtarget();
  TII = SubTarget.getInstrInfo();
  TLI = SubTarget.getTargetLowering();

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    // Use instr_iterator because we don't want to skip bundles.
    for (MachineBasicBlock::instr_iterator MII = MBB.instr_begin(),
                                           MIE = MBB.instr_end();
         MII != MIE; ++MII) {
      if (MII->isCall() && MII->getCFIType())
        Changed |= emitCheck(MBB, MII);
    }
  }

  return Changed;
}
