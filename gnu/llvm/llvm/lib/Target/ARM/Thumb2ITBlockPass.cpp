//===-- Thumb2ITBlockPass.cpp - Insert Thumb-2 IT blocks ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "Thumb2InstrInfo.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include <cassert>
#include <new>

using namespace llvm;

#define DEBUG_TYPE "thumb2-it"
#define PASS_NAME "Thumb IT blocks insertion pass"

STATISTIC(NumITs,        "Number of IT blocks inserted");
STATISTIC(NumMovedInsts, "Number of predicated instructions moved");

using RegisterSet = SmallSet<unsigned, 4>;

namespace {

  class Thumb2ITBlock : public MachineFunctionPass {
  public:
    static char ID;

    bool restrictIT;
    const Thumb2InstrInfo *TII;
    const TargetRegisterInfo *TRI;
    ARMFunctionInfo *AFI;

    Thumb2ITBlock() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &Fn) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    StringRef getPassName() const override {
      return PASS_NAME;
    }

  private:
    bool MoveCopyOutOfITBlock(MachineInstr *MI,
                              ARMCC::CondCodes CC, ARMCC::CondCodes OCC,
                              RegisterSet &Defs, RegisterSet &Uses);
    bool InsertITInstructions(MachineBasicBlock &Block);
  };

  char Thumb2ITBlock::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(Thumb2ITBlock, DEBUG_TYPE, PASS_NAME, false, false)

/// TrackDefUses - Tracking what registers are being defined and used by
/// instructions in the IT block. This also tracks "dependencies", i.e. uses
/// in the IT block that are defined before the IT instruction.
static void TrackDefUses(MachineInstr *MI, RegisterSet &Defs, RegisterSet &Uses,
                         const TargetRegisterInfo *TRI) {
  using RegList = SmallVector<unsigned, 4>;
  RegList LocalDefs;
  RegList LocalUses;

  for (auto &MO : MI->operands()) {
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg || Reg == ARM::ITSTATE || Reg == ARM::SP)
      continue;
    if (MO.isUse())
      LocalUses.push_back(Reg);
    else
      LocalDefs.push_back(Reg);
  }

  auto InsertUsesDefs = [&](RegList &Regs, RegisterSet &UsesDefs) {
    for (unsigned Reg : Regs)
      for (MCPhysReg Subreg : TRI->subregs_inclusive(Reg))
        UsesDefs.insert(Subreg);
  };

  InsertUsesDefs(LocalDefs, Defs);
  InsertUsesDefs(LocalUses, Uses);
}

/// Clear kill flags for any uses in the given set.  This will likely
/// conservatively remove more kill flags than are necessary, but removing them
/// is safer than incorrect kill flags remaining on instructions.
static void ClearKillFlags(MachineInstr *MI, RegisterSet &Uses) {
  for (MachineOperand &MO : MI->operands()) {
    if (!MO.isReg() || MO.isDef() || !MO.isKill())
      continue;
    if (!Uses.count(MO.getReg()))
      continue;
    MO.setIsKill(false);
  }
}

static bool isCopy(MachineInstr *MI) {
  switch (MI->getOpcode()) {
  default:
    return false;
  case ARM::MOVr:
  case ARM::MOVr_TC:
  case ARM::tMOVr:
  case ARM::t2MOVr:
    return true;
  }
}

bool
Thumb2ITBlock::MoveCopyOutOfITBlock(MachineInstr *MI,
                                    ARMCC::CondCodes CC, ARMCC::CondCodes OCC,
                                    RegisterSet &Defs, RegisterSet &Uses) {
  if (!isCopy(MI))
    return false;
  // llvm models select's as two-address instructions. That means a copy
  // is inserted before a t2MOVccr, etc. If the copy is scheduled in
  // between selects we would end up creating multiple IT blocks.
  assert(MI->getOperand(0).getSubReg() == 0 &&
         MI->getOperand(1).getSubReg() == 0 &&
         "Sub-register indices still around?");

  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();

  // First check if it's safe to move it.
  if (Uses.count(DstReg) || Defs.count(SrcReg))
    return false;

  // If the CPSR is defined by this copy, then we don't want to move it. E.g.,
  // if we have:
  //
  //   movs  r1, r1
  //   rsb   r1, 0
  //   movs  r2, r2
  //   rsb   r2, 0
  //
  // we don't want this to be converted to:
  //
  //   movs  r1, r1
  //   movs  r2, r2
  //   itt   mi
  //   rsb   r1, 0
  //   rsb   r2, 0
  //
  const MCInstrDesc &MCID = MI->getDesc();
  if (MI->hasOptionalDef() &&
      MI->getOperand(MCID.getNumOperands() - 1).getReg() == ARM::CPSR)
    return false;

  // Then peek at the next instruction to see if it's predicated on CC or OCC.
  // If not, then there is nothing to be gained by moving the copy.
  MachineBasicBlock::iterator I = MI;
  ++I;
  MachineBasicBlock::iterator E = MI->getParent()->end();

  while (I != E && I->isDebugInstr())
    ++I;

  if (I != E) {
    Register NPredReg;
    ARMCC::CondCodes NCC = getITInstrPredicate(*I, NPredReg);
    if (NCC == CC || NCC == OCC)
      return true;
  }
  return false;
}

bool Thumb2ITBlock::InsertITInstructions(MachineBasicBlock &MBB) {
  bool Modified = false;
  RegisterSet Defs, Uses;
  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();

  while (MBBI != E) {
    MachineInstr *MI = &*MBBI;
    DebugLoc dl = MI->getDebugLoc();
    Register PredReg;
    ARMCC::CondCodes CC = getITInstrPredicate(*MI, PredReg);
    if (CC == ARMCC::AL) {
      ++MBBI;
      continue;
    }

    Defs.clear();
    Uses.clear();
    TrackDefUses(MI, Defs, Uses, TRI);

    // Insert an IT instruction.
    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, dl, TII->get(ARM::t2IT))
      .addImm(CC);

    // Add implicit use of ITSTATE to IT block instructions.
    MI->addOperand(MachineOperand::CreateReg(ARM::ITSTATE, false/*ifDef*/,
                                             true/*isImp*/, false/*isKill*/));

    MachineInstr *LastITMI = MI;
    MachineBasicBlock::iterator InsertPos = MIB.getInstr();
    ++MBBI;

    // Form IT block.
    ARMCC::CondCodes OCC = ARMCC::getOppositeCondition(CC);
    unsigned Mask = 0, Pos = 3;

    // IT blocks are limited to one conditional op if -arm-restrict-it
    // is set: skip the loop
    if (!restrictIT) {
      LLVM_DEBUG(dbgs() << "Allowing complex IT block\n";);
      // Branches, including tricky ones like LDM_RET, need to end an IT
      // block so check the instruction we just put in the block.
      for (; MBBI != E && Pos &&
             (!MI->isBranch() && !MI->isReturn()) ; ++MBBI) {
        if (MBBI->isDebugInstr())
          continue;

        MachineInstr *NMI = &*MBBI;
        MI = NMI;

        Register NPredReg;
        ARMCC::CondCodes NCC = getITInstrPredicate(*NMI, NPredReg);
        if (NCC == CC || NCC == OCC) {
          Mask |= ((NCC ^ CC) & 1) << Pos;
          // Add implicit use of ITSTATE.
          NMI->addOperand(MachineOperand::CreateReg(ARM::ITSTATE, false/*ifDef*/,
                                                 true/*isImp*/, false/*isKill*/));
          LastITMI = NMI;
        } else {
          if (NCC == ARMCC::AL &&
              MoveCopyOutOfITBlock(NMI, CC, OCC, Defs, Uses)) {
            --MBBI;
            MBB.remove(NMI);
            MBB.insert(InsertPos, NMI);
            ClearKillFlags(MI, Uses);
            ++NumMovedInsts;
            continue;
          }
          break;
        }
        TrackDefUses(NMI, Defs, Uses, TRI);
        --Pos;
      }
    }

    // Finalize IT mask.
    Mask |= (1 << Pos);
    MIB.addImm(Mask);

    // Last instruction in IT block kills ITSTATE.
    LastITMI->findRegisterUseOperand(ARM::ITSTATE, /*TRI=*/nullptr)
        ->setIsKill();

    // Finalize the bundle.
    finalizeBundle(MBB, InsertPos.getInstrIterator(),
                   ++LastITMI->getIterator());

    Modified = true;
    ++NumITs;
  }

  return Modified;
}

bool Thumb2ITBlock::runOnMachineFunction(MachineFunction &Fn) {
  const ARMSubtarget &STI = Fn.getSubtarget<ARMSubtarget>();
  if (!STI.isThumb2())
    return false;
  AFI = Fn.getInfo<ARMFunctionInfo>();
  TII = static_cast<const Thumb2InstrInfo *>(STI.getInstrInfo());
  TRI = STI.getRegisterInfo();
  restrictIT = STI.restrictIT();

  if (!AFI->isThumbFunction())
    return false;

  bool Modified = false;
  for (auto &MBB : Fn )
    Modified |= InsertITInstructions(MBB);

  if (Modified)
    AFI->setHasITBlocks(true);

  return Modified;
}

/// createThumb2ITBlockPass - Returns an instance of the Thumb2 IT blocks
/// insertion pass.
FunctionPass *llvm::createThumb2ITBlockPass() { return new Thumb2ITBlock(); }
