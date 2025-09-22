//===-- PPCCTRLoops.cpp - Generate CTR loops ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass generates machine instructions for the CTR loops related pseudos:
// 1: MTCTRloop/DecreaseCTRloop
// 2: MTCTR8loop/DecreaseCTR8loop
//
// If a CTR loop can be generated:
// 1: MTCTRloop/MTCTR8loop will be converted to "mtctr"
// 2: DecreaseCTRloop/DecreaseCTR8loop will be converted to "bdnz/bdz" and
//    its user branch instruction can be deleted.
//
// If a CTR loop can not be generated due to clobber of CTR:
// 1: MTCTRloop/MTCTR8loop can be deleted.
// 2: DecreaseCTRloop/DecreaseCTR8loop will be converted to "addi -1" and
//    a "cmplwi/cmpldi".
//
// This pass runs just before register allocation, because we don't want
// register allocator to allocate register for DecreaseCTRloop if a CTR can be
// generated or if a CTR loop can not be generated, we don't have any condition
// register for the new added "cmplwi/cmpldi".
//
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "PPCInstrInfo.h"
#include "PPCSubtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "ppc-ctrloops"

STATISTIC(NumCTRLoops, "Number of CTR loops generated");
STATISTIC(NumNormalLoops, "Number of normal compare + branch loops generated");

namespace {
class PPCCTRLoops : public MachineFunctionPass {
public:
  static char ID;

  PPCCTRLoops() : MachineFunctionPass(ID) {
    initializePPCCTRLoopsPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  const PPCInstrInfo *TII = nullptr;
  MachineRegisterInfo *MRI = nullptr;

  bool processLoop(MachineLoop *ML);
  bool isCTRClobber(MachineInstr *MI, bool CheckReads) const;
  void expandNormalLoops(MachineLoop *ML, MachineInstr *Start,
                         MachineInstr *Dec);
  void expandCTRLoops(MachineLoop *ML, MachineInstr *Start, MachineInstr *Dec);
};
} // namespace

char PPCCTRLoops::ID = 0;

INITIALIZE_PASS_BEGIN(PPCCTRLoops, DEBUG_TYPE, "PowerPC CTR loops generation",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(PPCCTRLoops, DEBUG_TYPE, "PowerPC CTR loops generation",
                    false, false)

FunctionPass *llvm::createPPCCTRLoopsPass() { return new PPCCTRLoops(); }

bool PPCCTRLoops::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;

  auto &MLI = getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  TII = static_cast<const PPCInstrInfo *>(MF.getSubtarget().getInstrInfo());
  MRI = &MF.getRegInfo();

  for (auto *ML : MLI) {
    if (ML->isOutermost())
      Changed |= processLoop(ML);
  }

#ifndef NDEBUG
  for (const MachineBasicBlock &BB : MF) {
    for (const MachineInstr &I : BB)
      assert((I.getOpcode() != PPC::DecreaseCTRloop &&
              I.getOpcode() != PPC::DecreaseCTR8loop) &&
             "CTR loop pseudo is not expanded!");
  }
#endif

  return Changed;
}

bool PPCCTRLoops::isCTRClobber(MachineInstr *MI, bool CheckReads) const {
  if (!CheckReads) {
    // If we are only checking for defs, that is we are going to find
    // definitions before MTCTRloop, for this case:
    // CTR defination inside the callee of a call instruction will not impact
    // the defination of MTCTRloop, so we can use definesRegister() for the
    // check, no need to check the regmask.
    return MI->definesRegister(PPC::CTR, /*TRI=*/nullptr) ||
           MI->definesRegister(PPC::CTR8, /*TRI=*/nullptr);
  }

  if (MI->modifiesRegister(PPC::CTR, /*TRI=*/nullptr) ||
      MI->modifiesRegister(PPC::CTR8, /*TRI=*/nullptr))
    return true;

  if (MI->getDesc().isCall())
    return true;

  // We define the CTR in the loop preheader, so if there is any CTR reader in
  // the loop, we also can not use CTR loop form.
  if (MI->readsRegister(PPC::CTR, /*TRI=*/nullptr) ||
      MI->readsRegister(PPC::CTR8, /*TRI=*/nullptr))
    return true;

  return false;
}

bool PPCCTRLoops::processLoop(MachineLoop *ML) {
  bool Changed = false;

  // Align with HardwareLoop pass, process inner loops first.
  for (MachineLoop *I : *ML)
    Changed |= processLoop(I);

  // If any inner loop is changed, outter loop must be without hardware loop
  // intrinsics.
  if (Changed)
    return true;

  auto IsLoopStart = [](MachineInstr &MI) {
    return MI.getOpcode() == PPC::MTCTRloop ||
           MI.getOpcode() == PPC::MTCTR8loop;
  };

  auto SearchForStart =
      [&IsLoopStart](MachineBasicBlock *MBB) -> MachineInstr * {
    for (auto &MI : *MBB) {
      if (IsLoopStart(MI))
        return &MI;
    }
    return nullptr;
  };

  MachineInstr *Start = nullptr;
  MachineInstr *Dec = nullptr;
  bool InvalidCTRLoop = false;

  MachineBasicBlock *Preheader = ML->getLoopPreheader();
  // If there is no preheader for this loop, there must be no MTCTRloop
  // either.
  if (!Preheader)
    return false;

  Start = SearchForStart(Preheader);
  // This is not a CTR loop candidate.
  if (!Start)
    return false;

  // If CTR is live to the preheader, we can not redefine the CTR register.
  if (Preheader->isLiveIn(PPC::CTR) || Preheader->isLiveIn(PPC::CTR8))
    InvalidCTRLoop = true;

  // Make sure there is also no CTR clobber in the block preheader between the
  // begin and MTCTR.
  for (MachineBasicBlock::reverse_instr_iterator I =
           std::next(Start->getReverseIterator());
       I != Preheader->instr_rend(); ++I)
    // Only check the definitions of CTR. If there is non-dead definition for
    // the CTR, we conservatively don't generate a CTR loop.
    if (isCTRClobber(&*I, /* CheckReads */ false)) {
      InvalidCTRLoop = true;
      break;
    }

  // Make sure there is also no CTR clobber/user in the block preheader between
  // MTCTR and the end.
  for (MachineBasicBlock::instr_iterator I = std::next(Start->getIterator());
       I != Preheader->instr_end(); ++I)
    if (isCTRClobber(&*I, /* CheckReads */ true)) {
      InvalidCTRLoop = true;
      break;
    }

  // Find the CTR loop components and decide whether or not to fall back to a
  // normal loop.
  for (auto *MBB : reverse(ML->getBlocks())) {
    for (auto &MI : *MBB) {
      if (MI.getOpcode() == PPC::DecreaseCTRloop ||
          MI.getOpcode() == PPC::DecreaseCTR8loop)
        Dec = &MI;
      else if (!InvalidCTRLoop)
        // If any instruction clobber CTR, then we can not generate a CTR loop.
        InvalidCTRLoop |= isCTRClobber(&MI, /* CheckReads */ true);
    }
    if (Dec && InvalidCTRLoop)
      break;
  }

  assert(Dec && "CTR loop is not complete!");

  if (InvalidCTRLoop) {
    expandNormalLoops(ML, Start, Dec);
    ++NumNormalLoops;
  }
  else {
    expandCTRLoops(ML, Start, Dec);
    ++NumCTRLoops;
  }
  return true;
}

void PPCCTRLoops::expandNormalLoops(MachineLoop *ML, MachineInstr *Start,
                                    MachineInstr *Dec) {
  bool Is64Bit =
      Start->getParent()->getParent()->getSubtarget<PPCSubtarget>().isPPC64();

  MachineBasicBlock *Preheader = Start->getParent();
  MachineBasicBlock *Exiting = Dec->getParent();
  assert((Preheader && Exiting) &&
         "Preheader and exiting should exist for CTR loop!");

  assert(Dec->getOperand(1).getImm() == 1 &&
         "Loop decrement stride must be 1");

  unsigned ADDIOpcode = Is64Bit ? PPC::ADDI8 : PPC::ADDI;
  unsigned CMPOpcode = Is64Bit ? PPC::CMPLDI : PPC::CMPLWI;

  Register PHIDef =
      MRI->createVirtualRegister(Is64Bit ? &PPC::G8RC_and_G8RC_NOX0RegClass
                                         : &PPC::GPRC_and_GPRC_NOR0RegClass);

  Start->getParent()->getParent()->getProperties().reset(
      MachineFunctionProperties::Property::NoPHIs);

  // Generate "PHI" in the header block.
  auto PHIMIB = BuildMI(*ML->getHeader(), ML->getHeader()->getFirstNonPHI(),
                        DebugLoc(), TII->get(TargetOpcode::PHI), PHIDef);
  PHIMIB.addReg(Start->getOperand(0).getReg()).addMBB(Preheader);

  Register ADDIDef =
      MRI->createVirtualRegister(Is64Bit ? &PPC::G8RC_and_G8RC_NOX0RegClass
                                         : &PPC::GPRC_and_GPRC_NOR0RegClass);
  // Generate "addi -1" in the exiting block.
  BuildMI(*Exiting, Dec, Dec->getDebugLoc(), TII->get(ADDIOpcode), ADDIDef)
      .addReg(PHIDef)
      .addImm(-1);

  // Add other inputs for the PHI node.
  if (ML->isLoopLatch(Exiting)) {
    // There must be only two predecessors for the loop header, one is the
    // Preheader and the other one is loop latch Exiting. In hardware loop
    // insertion pass, the block containing DecreaseCTRloop must dominate all
    // loop latches. So there must be only one latch.
    assert(ML->getHeader()->pred_size() == 2 &&
           "Loop header predecessor is not right!");
    PHIMIB.addReg(ADDIDef).addMBB(Exiting);
  } else {
    // If the block containing DecreaseCTRloop is not a loop latch, we can use
    // ADDIDef as the value for all other blocks for the PHI. In hardware loop
    // insertion pass, the block containing DecreaseCTRloop must dominate all
    // loop latches.
    for (MachineBasicBlock *P : ML->getHeader()->predecessors()) {
      if (ML->contains(P)) {
        assert(ML->isLoopLatch(P) &&
               "Loop's header in-loop predecessor is not loop latch!");
        PHIMIB.addReg(ADDIDef).addMBB(P);
      } else
        assert(P == Preheader &&
               "CTR loop should not be generated for irreducible loop!");
    }
  }

  // Generate the compare in the exiting block.
  Register CMPDef = MRI->createVirtualRegister(&PPC::CRRCRegClass);
  auto CMPMIB =
      BuildMI(*Exiting, Dec, Dec->getDebugLoc(), TII->get(CMPOpcode), CMPDef)
          .addReg(ADDIDef)
          .addImm(0);

  BuildMI(*Exiting, Dec, Dec->getDebugLoc(), TII->get(TargetOpcode::COPY),
          Dec->getOperand(0).getReg())
      .addReg(CMPMIB->getOperand(0).getReg(), 0, PPC::sub_gt);

  // Remove the pseudo instructions.
  Start->eraseFromParent();
  Dec->eraseFromParent();
}

void PPCCTRLoops::expandCTRLoops(MachineLoop *ML, MachineInstr *Start,
                                 MachineInstr *Dec) {
  bool Is64Bit =
      Start->getParent()->getParent()->getSubtarget<PPCSubtarget>().isPPC64();

  MachineBasicBlock *Preheader = Start->getParent();
  MachineBasicBlock *Exiting = Dec->getParent();

  (void)Preheader;
  assert((Preheader && Exiting) &&
         "Preheader and exiting should exist for CTR loop!");

  assert(Dec->getOperand(1).getImm() == 1 && "Loop decrement must be 1!");

  unsigned BDNZOpcode = Is64Bit ? PPC::BDNZ8 : PPC::BDNZ;
  unsigned BDZOpcode = Is64Bit ? PPC::BDZ8 : PPC::BDZ;
  auto BrInstr = MRI->use_instr_begin(Dec->getOperand(0).getReg());
  assert(MRI->hasOneUse(Dec->getOperand(0).getReg()) &&
         "There should be only one user for loop decrement pseudo!");

  unsigned Opcode = 0;
  switch (BrInstr->getOpcode()) {
  case PPC::BC:
    Opcode = BDNZOpcode;
    (void) ML;
    assert(ML->contains(BrInstr->getOperand(1).getMBB()) &&
           "Invalid ctr loop!");
    break;
  case PPC::BCn:
    Opcode = BDZOpcode;
    assert(!ML->contains(BrInstr->getOperand(1).getMBB()) &&
           "Invalid ctr loop!");
    break;
  default:
    llvm_unreachable("Unhandled branch user for DecreaseCTRloop.");
  }

  // Generate "bdnz/bdz" in the exiting block just before the terminator.
  BuildMI(*Exiting, &*BrInstr, BrInstr->getDebugLoc(), TII->get(Opcode))
      .addMBB(BrInstr->getOperand(1).getMBB());

  // Remove the pseudo instructions.
  BrInstr->eraseFromParent();
  Dec->eraseFromParent();
}
