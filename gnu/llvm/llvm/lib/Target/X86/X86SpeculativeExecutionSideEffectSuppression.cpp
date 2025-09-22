//===-- X86SpeculativeExecutionSideEffectSuppression.cpp ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file contains the X86 implementation of the speculative execution side
/// effect suppression mitigation.
///
/// This must be used with the -mlvi-cfi flag in order to mitigate indirect
/// branches and returns.
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "x86-seses"

STATISTIC(NumLFENCEsInserted, "Number of lfence instructions inserted");

static cl::opt<bool> EnableSpeculativeExecutionSideEffectSuppression(
    "x86-seses-enable-without-lvi-cfi",
    cl::desc("Force enable speculative execution side effect suppression. "
             "(Note: User must pass -mlvi-cfi in order to mitigate indirect "
             "branches and returns.)"),
    cl::init(false), cl::Hidden);

static cl::opt<bool> OneLFENCEPerBasicBlock(
    "x86-seses-one-lfence-per-bb",
    cl::desc(
        "Omit all lfences other than the first to be placed in a basic block."),
    cl::init(false), cl::Hidden);

static cl::opt<bool> OnlyLFENCENonConst(
    "x86-seses-only-lfence-non-const",
    cl::desc("Only lfence before groups of terminators where at least one "
             "branch instruction has an input to the addressing mode that is a "
             "register other than %rip."),
    cl::init(false), cl::Hidden);

static cl::opt<bool>
    OmitBranchLFENCEs("x86-seses-omit-branch-lfences",
                      cl::desc("Omit all lfences before branch instructions."),
                      cl::init(false), cl::Hidden);

namespace {

class X86SpeculativeExecutionSideEffectSuppression
    : public MachineFunctionPass {
public:
  X86SpeculativeExecutionSideEffectSuppression() : MachineFunctionPass(ID) {}

  static char ID;
  StringRef getPassName() const override {
    return "X86 Speculative Execution Side Effect Suppression";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // namespace

char X86SpeculativeExecutionSideEffectSuppression::ID = 0;

// This function returns whether the passed instruction uses a memory addressing
// mode that is constant. We treat all memory addressing modes that read
// from a register that is not %rip as non-constant. Note that the use
// of the EFLAGS register results in an addressing mode being considered
// non-constant, therefore all JCC instructions will return false from this
// function since one of their operands will always be the EFLAGS register.
static bool hasConstantAddressingMode(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.uses())
    if (MO.isReg() && X86::RIP != MO.getReg())
      return false;
  return true;
}

bool X86SpeculativeExecutionSideEffectSuppression::runOnMachineFunction(
    MachineFunction &MF) {

  const auto &OptLevel = MF.getTarget().getOptLevel();
  const X86Subtarget &Subtarget = MF.getSubtarget<X86Subtarget>();

  // Check whether SESES needs to run as the fallback for LVI at O0, whether the
  // user explicitly passed an SESES flag, or whether the SESES target feature
  // was set.
  if (!EnableSpeculativeExecutionSideEffectSuppression &&
      !(Subtarget.useLVILoadHardening() && OptLevel == CodeGenOptLevel::None) &&
      !Subtarget.useSpeculativeExecutionSideEffectSuppression())
    return false;

  LLVM_DEBUG(dbgs() << "********** " << getPassName() << " : " << MF.getName()
                    << " **********\n");
  bool Modified = false;
  const X86InstrInfo *TII = Subtarget.getInstrInfo();
  for (MachineBasicBlock &MBB : MF) {
    MachineInstr *FirstTerminator = nullptr;
    // Keep track of whether the previous instruction was an LFENCE to avoid
    // adding redundant LFENCEs.
    bool PrevInstIsLFENCE = false;
    for (auto &MI : MBB) {

      if (MI.getOpcode() == X86::LFENCE) {
        PrevInstIsLFENCE = true;
        continue;
      }
      // We want to put an LFENCE before any instruction that
      // may load or store. This LFENCE is intended to avoid leaking any secret
      // data due to a given load or store. This results in closing the cache
      // and memory timing side channels. We will treat terminators that load
      // or store separately.
      if (MI.mayLoadOrStore() && !MI.isTerminator()) {
        if (!PrevInstIsLFENCE) {
          BuildMI(MBB, MI, DebugLoc(), TII->get(X86::LFENCE));
          NumLFENCEsInserted++;
          Modified = true;
        }
        if (OneLFENCEPerBasicBlock)
          break;
      }
      // The following section will be LFENCEing before groups of terminators
      // that include branches. This will close the branch prediction side
      // channels since we will prevent code executing after misspeculation as
      // a result of the LFENCEs placed with this logic.

      // Keep track of the first terminator in a basic block since if we need
      // to LFENCE the terminators in this basic block we must add the
      // instruction before the first terminator in the basic block (as
      // opposed to before the terminator that indicates an LFENCE is
      // required). An example of why this is necessary is that the
      // X86InstrInfo::analyzeBranch method assumes all terminators are grouped
      // together and terminates it's analysis once the first non-termintor
      // instruction is found.
      if (MI.isTerminator() && FirstTerminator == nullptr)
        FirstTerminator = &MI;

      // Look for branch instructions that will require an LFENCE to be put
      // before this basic block's terminators.
      if (!MI.isBranch() || OmitBranchLFENCEs) {
        // This isn't a branch or we're not putting LFENCEs before branches.
        PrevInstIsLFENCE = false;
        continue;
      }

      if (OnlyLFENCENonConst && hasConstantAddressingMode(MI)) {
        // This is a branch, but it only has constant addressing mode and we're
        // not adding LFENCEs before such branches.
        PrevInstIsLFENCE = false;
        continue;
      }

      // This branch requires adding an LFENCE.
      if (!PrevInstIsLFENCE) {
        assert(FirstTerminator && "Unknown terminator instruction");
        BuildMI(MBB, FirstTerminator, DebugLoc(), TII->get(X86::LFENCE));
        NumLFENCEsInserted++;
        Modified = true;
      }
      break;
    }
  }

  return Modified;
}

FunctionPass *llvm::createX86SpeculativeExecutionSideEffectSuppression() {
  return new X86SpeculativeExecutionSideEffectSuppression();
}

INITIALIZE_PASS(X86SpeculativeExecutionSideEffectSuppression, "x86-seses",
                "X86 Speculative Execution Side Effect Suppression", false,
                false)
