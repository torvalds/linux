//==-- AArch64DeadRegisterDefinitions.cpp - Replace dead defs w/ zero reg --==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file When allowed by the instruction, replace a dead definition of a GPR
/// with the zero register. This makes the code a bit friendlier towards the
/// hardware's register renamer.
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64RegisterInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "aarch64-dead-defs"

STATISTIC(NumDeadDefsReplaced, "Number of dead definitions replaced");

#define AARCH64_DEAD_REG_DEF_NAME "AArch64 Dead register definitions"

namespace {
class AArch64DeadRegisterDefinitions : public MachineFunctionPass {
private:
  const TargetRegisterInfo *TRI;
  const MachineRegisterInfo *MRI;
  const TargetInstrInfo *TII;
  bool Changed;
  void processMachineBasicBlock(MachineBasicBlock &MBB);
public:
  static char ID; // Pass identification, replacement for typeid.
  AArch64DeadRegisterDefinitions() : MachineFunctionPass(ID) {
    initializeAArch64DeadRegisterDefinitionsPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;

  StringRef getPassName() const override { return AARCH64_DEAD_REG_DEF_NAME; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
char AArch64DeadRegisterDefinitions::ID = 0;
} // end anonymous namespace

INITIALIZE_PASS(AArch64DeadRegisterDefinitions, "aarch64-dead-defs",
                AARCH64_DEAD_REG_DEF_NAME, false, false)

static bool usesFrameIndex(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.uses())
    if (MO.isFI())
      return true;
  return false;
}

// Instructions that lose their 'read' operation for a subesquent fence acquire
// (DMB LD) once the zero register is used.
//
// WARNING: The aquire variants of the instructions are also affected, but they
// are split out into `atomicBarrierDroppedOnZero()` to support annotations on
// assembly.
static bool atomicReadDroppedOnZero(unsigned Opcode) {
  switch (Opcode) {
    case AArch64::LDADDB:     case AArch64::LDADDH:
    case AArch64::LDADDW:     case AArch64::LDADDX:
    case AArch64::LDADDLB:    case AArch64::LDADDLH:
    case AArch64::LDADDLW:    case AArch64::LDADDLX:
    case AArch64::LDCLRB:     case AArch64::LDCLRH:
    case AArch64::LDCLRW:     case AArch64::LDCLRX:
    case AArch64::LDCLRLB:    case AArch64::LDCLRLH:
    case AArch64::LDCLRLW:    case AArch64::LDCLRLX:
    case AArch64::LDEORB:     case AArch64::LDEORH:
    case AArch64::LDEORW:     case AArch64::LDEORX:
    case AArch64::LDEORLB:    case AArch64::LDEORLH:
    case AArch64::LDEORLW:    case AArch64::LDEORLX:
    case AArch64::LDSETB:     case AArch64::LDSETH:
    case AArch64::LDSETW:     case AArch64::LDSETX:
    case AArch64::LDSETLB:    case AArch64::LDSETLH:
    case AArch64::LDSETLW:    case AArch64::LDSETLX:
    case AArch64::LDSMAXB:    case AArch64::LDSMAXH:
    case AArch64::LDSMAXW:    case AArch64::LDSMAXX:
    case AArch64::LDSMAXLB:   case AArch64::LDSMAXLH:
    case AArch64::LDSMAXLW:   case AArch64::LDSMAXLX:
    case AArch64::LDSMINB:    case AArch64::LDSMINH:
    case AArch64::LDSMINW:    case AArch64::LDSMINX:
    case AArch64::LDSMINLB:   case AArch64::LDSMINLH:
    case AArch64::LDSMINLW:   case AArch64::LDSMINLX:
    case AArch64::LDUMAXB:    case AArch64::LDUMAXH:
    case AArch64::LDUMAXW:    case AArch64::LDUMAXX:
    case AArch64::LDUMAXLB:   case AArch64::LDUMAXLH:
    case AArch64::LDUMAXLW:   case AArch64::LDUMAXLX:
    case AArch64::LDUMINB:    case AArch64::LDUMINH:
    case AArch64::LDUMINW:    case AArch64::LDUMINX:
    case AArch64::LDUMINLB:   case AArch64::LDUMINLH:
    case AArch64::LDUMINLW:   case AArch64::LDUMINLX:
    case AArch64::SWPB:       case AArch64::SWPH:
    case AArch64::SWPW:       case AArch64::SWPX:
    case AArch64::SWPLB:      case AArch64::SWPLH:
    case AArch64::SWPLW:      case AArch64::SWPLX:
    return true;
  }
  return false;
}

void AArch64DeadRegisterDefinitions::processMachineBasicBlock(
    MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();
  for (MachineInstr &MI : MBB) {
    if (usesFrameIndex(MI)) {
      // We need to skip this instruction because while it appears to have a
      // dead def it uses a frame index which might expand into a multi
      // instruction sequence during EPI.
      LLVM_DEBUG(dbgs() << "    Ignoring, operand is frame index\n");
      continue;
    }
    if (MI.definesRegister(AArch64::XZR, /*TRI=*/nullptr) ||
        MI.definesRegister(AArch64::WZR, /*TRI=*/nullptr)) {
      // It is not allowed to write to the same register (not even the zero
      // register) twice in a single instruction.
      LLVM_DEBUG(
          dbgs()
          << "    Ignoring, XZR or WZR already used by the instruction\n");
      continue;
    }

    if (atomicBarrierDroppedOnZero(MI.getOpcode()) || atomicReadDroppedOnZero(MI.getOpcode())) {
      LLVM_DEBUG(dbgs() << "    Ignoring, semantics change with xzr/wzr.\n");
      continue;
    }

    const MCInstrDesc &Desc = MI.getDesc();
    for (int I = 0, E = Desc.getNumDefs(); I != E; ++I) {
      MachineOperand &MO = MI.getOperand(I);
      if (!MO.isReg() || !MO.isDef())
        continue;
      // We should not have any relevant physreg defs that are replacable by
      // zero before register allocation. So we just check for dead vreg defs.
      Register Reg = MO.getReg();
      if (!Reg.isVirtual() || (!MO.isDead() && !MRI->use_nodbg_empty(Reg)))
        continue;
      assert(!MO.isImplicit() && "Unexpected implicit def!");
      LLVM_DEBUG(dbgs() << "  Dead def operand #" << I << " in:\n    ";
                 MI.print(dbgs()));
      // Be careful not to change the register if it's a tied operand.
      if (MI.isRegTiedToUseOperand(I)) {
        LLVM_DEBUG(dbgs() << "    Ignoring, def is tied operand.\n");
        continue;
      }
      const TargetRegisterClass *RC = TII->getRegClass(Desc, I, TRI, MF);
      unsigned NewReg;
      if (RC == nullptr) {
        LLVM_DEBUG(dbgs() << "    Ignoring, register is not a GPR.\n");
        continue;
      } else if (RC->contains(AArch64::WZR))
        NewReg = AArch64::WZR;
      else if (RC->contains(AArch64::XZR))
        NewReg = AArch64::XZR;
      else {
        LLVM_DEBUG(dbgs() << "    Ignoring, register is not a GPR.\n");
        continue;
      }
      LLVM_DEBUG(dbgs() << "    Replacing with zero register. New:\n      ");
      MO.setReg(NewReg);
      MO.setIsDead();
      LLVM_DEBUG(MI.print(dbgs()));
      ++NumDeadDefsReplaced;
      Changed = true;
      // Only replace one dead register, see check for zero register above.
      break;
    }
  }
}

// Scan the function for instructions that have a dead definition of a
// register. Replace that register with the zero register when possible.
bool AArch64DeadRegisterDefinitions::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  TRI = MF.getSubtarget().getRegisterInfo();
  TII = MF.getSubtarget().getInstrInfo();
  MRI = &MF.getRegInfo();
  LLVM_DEBUG(dbgs() << "***** AArch64DeadRegisterDefinitions *****\n");
  Changed = false;
  for (auto &MBB : MF)
    processMachineBasicBlock(MBB);
  return Changed;
}

FunctionPass *llvm::createAArch64DeadRegisterDefinitions() {
  return new AArch64DeadRegisterDefinitions();
}
