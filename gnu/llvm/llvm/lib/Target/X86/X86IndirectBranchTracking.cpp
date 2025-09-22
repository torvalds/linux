//===---- X86IndirectBranchTracking.cpp - Enables CET IBT mechanism -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass that enables Indirect Branch Tracking (IBT) as part
// of Control-Flow Enforcement Technology (CET).
// The pass adds ENDBR (End Branch) machine instructions at the beginning of
// each basic block or function that is referenced by an indrect jump/call
// instruction.
// The ENDBR instructions have a NOP encoding and as such are ignored in
// targets that do not support CET IBT mechanism.
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "X86TargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Module.h"

using namespace llvm;

#define DEBUG_TYPE "x86-indirect-branch-tracking"

cl::opt<bool> IndirectBranchTracking(
    "x86-indirect-branch-tracking", cl::init(false), cl::Hidden,
    cl::desc("Enable X86 indirect branch tracking pass."));

STATISTIC(NumEndBranchAdded, "Number of ENDBR instructions added");

namespace {
class X86IndirectBranchTrackingPass : public MachineFunctionPass {
public:
  X86IndirectBranchTrackingPass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "X86 Indirect Branch Tracking";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  static char ID;

  /// Machine instruction info used throughout the class.
  const X86InstrInfo *TII = nullptr;

  /// Endbr opcode for the current machine function.
  unsigned int EndbrOpcode = 0;

  /// Adds a new ENDBR instruction to the beginning of the MBB.
  /// The function will not add it if already exists.
  /// It will add ENDBR32 or ENDBR64 opcode, depending on the target.
  /// \returns true if the ENDBR was added and false otherwise.
  bool addENDBR(MachineBasicBlock &MBB, MachineBasicBlock::iterator I) const;
};

} // end anonymous namespace

char X86IndirectBranchTrackingPass::ID = 0;

FunctionPass *llvm::createX86IndirectBranchTrackingPass() {
  return new X86IndirectBranchTrackingPass();
}

bool X86IndirectBranchTrackingPass::addENDBR(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I) const {
  assert(TII && "Target instruction info was not initialized");
  assert((X86::ENDBR64 == EndbrOpcode || X86::ENDBR32 == EndbrOpcode) &&
         "Unexpected Endbr opcode");

  // If the MBB/I is empty or the current instruction is not ENDBR,
  // insert ENDBR instruction to the location of I.
  if (I == MBB.end() || I->getOpcode() != EndbrOpcode) {
    BuildMI(MBB, I, MBB.findDebugLoc(I), TII->get(EndbrOpcode));
    ++NumEndBranchAdded;
    return true;
  }
  return false;
}

static bool IsCallReturnTwice(llvm::MachineOperand &MOp) {
  if (!MOp.isGlobal())
    return false;
  auto *CalleeFn = dyn_cast<Function>(MOp.getGlobal());
  if (!CalleeFn)
    return false;
  AttributeList Attrs = CalleeFn->getAttributes();
  return Attrs.hasFnAttr(Attribute::ReturnsTwice);
}

// Checks if function should have an ENDBR in its prologue
static bool needsPrologueENDBR(MachineFunction &MF, const Module *M) {
  Function &F = MF.getFunction();

  if (F.doesNoCfCheck())
    return false;

  switch (MF.getTarget().getCodeModel()) {
  // Large code model functions always reachable through indirect calls.
  case CodeModel::Large:
    return true;
  // Address taken or externally linked functions may be reachable.
  default:
    return (F.hasAddressTaken() || !F.hasLocalLinkage());
  }
}

bool X86IndirectBranchTrackingPass::runOnMachineFunction(MachineFunction &MF) {
  const X86Subtarget &SubTarget = MF.getSubtarget<X86Subtarget>();

  const Module *M = MF.getFunction().getParent();
  // Check that the cf-protection-branch is enabled.
  Metadata *isCFProtectionSupported = M->getModuleFlag("cf-protection-branch");

  //  NB: We need to enable IBT in jitted code if JIT compiler is CET
  //  enabled.
  const X86TargetMachine *TM =
      static_cast<const X86TargetMachine *>(&MF.getTarget());
#ifdef __CET__
  bool isJITwithCET = TM->isJIT();
#else
  bool isJITwithCET = false;
#endif
  if (!isCFProtectionSupported && !IndirectBranchTracking && !isJITwithCET)
    return false;

  // True if the current MF was changed and false otherwise.
  bool Changed = false;

  TII = SubTarget.getInstrInfo();
  EndbrOpcode = SubTarget.is64Bit() ? X86::ENDBR64 : X86::ENDBR32;

  // If function is reachable indirectly, mark the first BB with ENDBR.
  if (needsPrologueENDBR(MF, M)) {
    auto MBB = MF.begin();
    Changed |= addENDBR(*MBB, MBB->begin());
  }

  for (auto &MBB : MF) {
    // Find all basic blocks that their address was taken (for example
    // in the case of indirect jump) and add ENDBR instruction.
    if (MBB.hasAddressTaken())
      Changed |= addENDBR(MBB, MBB.begin());

    for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end(); ++I) {
      if (I->isCall() && I->getNumOperands() > 0 &&
          IsCallReturnTwice(I->getOperand(0))) {
        Changed |= addENDBR(MBB, std::next(I));
      }
    }

    // Exception handle may indirectly jump to catch pad, So we should add
    // ENDBR before catch pad instructions. For SjLj exception model, it will
    // create a new BB(new landingpad) indirectly jump to the old landingpad.
    if (TM->Options.ExceptionModel == ExceptionHandling::SjLj) {
      for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end(); ++I) {
        // New Landingpad BB without EHLabel.
        if (MBB.isEHPad()) {
          if (I->isDebugInstr())
            continue;
          Changed |= addENDBR(MBB, I);
          break;
        } else if (I->isEHLabel()) {
          // Old Landingpad BB (is not Landingpad now) with
          // the old "callee" EHLabel.
          MCSymbol *Sym = I->getOperand(0).getMCSymbol();
          if (!MF.hasCallSiteLandingPad(Sym))
            continue;
          Changed |= addENDBR(MBB, std::next(I));
          break;
        }
      }
    } else if (MBB.isEHPad()){
      for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end(); ++I) {
        if (!I->isEHLabel())
          continue;
        Changed |= addENDBR(MBB, std::next(I));
        break;
      }
    }
  }
  return Changed;
}
