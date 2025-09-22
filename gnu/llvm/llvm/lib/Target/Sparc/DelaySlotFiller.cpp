//===-- DelaySlotFiller.cpp - SPARC delay slot filler ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a simple local pass that attempts to fill delay slots with useful
// instructions. If no instructions can be moved into the delay slot, then a
// NOP is placed.
//===----------------------------------------------------------------------===//

#include "Sparc.h"
#include "SparcSubtarget.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "delay-slot-filler"

STATISTIC(FilledSlots, "Number of delay slots filled");

static cl::opt<bool> DisableDelaySlotFiller(
  "disable-sparc-delay-filler",
  cl::init(false),
  cl::desc("Disable the Sparc delay slot filler."),
  cl::Hidden);

namespace {
  struct Filler : public MachineFunctionPass {
    const SparcSubtarget *Subtarget = nullptr;

    static char ID;
    Filler() : MachineFunctionPass(ID) {}

    StringRef getPassName() const override { return "SPARC Delay Slot Filler"; }

    bool runOnMachineBasicBlock(MachineBasicBlock &MBB);
    bool runOnMachineFunction(MachineFunction &F) override {
      bool Changed = false;
      Subtarget = &F.getSubtarget<SparcSubtarget>();

      // This pass invalidates liveness information when it reorders
      // instructions to fill delay slot.
      F.getRegInfo().invalidateLiveness();

      for (MachineBasicBlock &MBB : F)
        Changed |= runOnMachineBasicBlock(MBB);
      return Changed;
    }

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    void insertCallDefsUses(MachineBasicBlock::iterator MI,
                            SmallSet<unsigned, 32>& RegDefs,
                            SmallSet<unsigned, 32>& RegUses);

    void insertDefsUses(MachineBasicBlock::iterator MI,
                        SmallSet<unsigned, 32>& RegDefs,
                        SmallSet<unsigned, 32>& RegUses);

    bool IsRegInSet(SmallSet<unsigned, 32>& RegSet,
                    unsigned Reg);

    bool delayHasHazard(MachineBasicBlock::iterator candidate,
                        bool &sawLoad, bool &sawStore,
                        SmallSet<unsigned, 32> &RegDefs,
                        SmallSet<unsigned, 32> &RegUses);

    MachineBasicBlock::iterator
    findDelayInstr(MachineBasicBlock &MBB, MachineBasicBlock::iterator slot);

    bool needsUnimp(MachineBasicBlock::iterator I, unsigned &StructSize);

    bool tryCombineRestoreWithPrevInst(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI);

  };
  char Filler::ID = 0;
} // end of anonymous namespace

/// createSparcDelaySlotFillerPass - Returns a pass that fills in delay
/// slots in Sparc MachineFunctions
///
FunctionPass *llvm::createSparcDelaySlotFillerPass() {
  return new Filler;
}


/// runOnMachineBasicBlock - Fill in delay slots for the given basic block.
/// We assume there is only one delay slot per delayed instruction.
///
bool Filler::runOnMachineBasicBlock(MachineBasicBlock &MBB) {
  bool Changed = false;
  Subtarget = &MBB.getParent()->getSubtarget<SparcSubtarget>();
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();

  for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end(); ) {
    MachineBasicBlock::iterator MI = I;
    ++I;

    // If MI is restore, try combining it with previous inst.
    if (!DisableDelaySlotFiller &&
        (MI->getOpcode() == SP::RESTORErr
         || MI->getOpcode() == SP::RESTOREri)) {
      Changed |= tryCombineRestoreWithPrevInst(MBB, MI);
      continue;
    }

    // TODO: If we ever want to support v7, this needs to be extended
    // to cover all floating point operations.
    if (!Subtarget->isV9() &&
        (MI->getOpcode() == SP::FCMPS || MI->getOpcode() == SP::FCMPD
         || MI->getOpcode() == SP::FCMPQ)) {
      BuildMI(MBB, I, MI->getDebugLoc(), TII->get(SP::NOP));
      Changed = true;
      continue;
    }

    // If MI has no delay slot, skip.
    if (!MI->hasDelaySlot())
      continue;

    MachineBasicBlock::iterator D = MBB.end();

    if (!DisableDelaySlotFiller)
      D = findDelayInstr(MBB, MI);

    ++FilledSlots;
    Changed = true;

    if (D == MBB.end())
      BuildMI(MBB, I, MI->getDebugLoc(), TII->get(SP::NOP));
    else
      MBB.splice(I, &MBB, D);

    unsigned structSize = 0;
    if (needsUnimp(MI, structSize)) {
      MachineBasicBlock::iterator J = MI;
      ++J; // skip the delay filler.
      assert (J != MBB.end() && "MI needs a delay instruction.");
      BuildMI(MBB, ++J, MI->getDebugLoc(),
              TII->get(SP::UNIMP)).addImm(structSize);
      // Bundle the delay filler and unimp with the instruction.
      MIBundleBuilder(MBB, MachineBasicBlock::iterator(MI), J);
    } else {
      MIBundleBuilder(MBB, MachineBasicBlock::iterator(MI), I);
    }
  }
  return Changed;
}

MachineBasicBlock::iterator
Filler::findDelayInstr(MachineBasicBlock &MBB,
                       MachineBasicBlock::iterator slot)
{
  SmallSet<unsigned, 32> RegDefs;
  SmallSet<unsigned, 32> RegUses;
  bool sawLoad = false;
  bool sawStore = false;

  if (slot == MBB.begin())
    return MBB.end();

  unsigned Opc = slot->getOpcode();

  if (Opc == SP::RET || Opc == SP::TLS_CALL)
    return MBB.end();

  if (Opc == SP::RETL || Opc == SP::TAIL_CALL || Opc == SP::TAIL_CALLri) {
    MachineBasicBlock::iterator J = slot;
    --J;

    if (J->getOpcode() == SP::RESTORErr
        || J->getOpcode() == SP::RESTOREri) {
      // change retl to ret.
      if (Opc == SP::RETL)
        slot->setDesc(Subtarget->getInstrInfo()->get(SP::RET));
      return J;
    }
  }

  // Call's delay filler can def some of call's uses.
  if (slot->isCall())
    insertCallDefsUses(slot, RegDefs, RegUses);
  else
    insertDefsUses(slot, RegDefs, RegUses);

  bool done = false;

  MachineBasicBlock::iterator I = slot;

  while (!done) {
    done = (I == MBB.begin());

    if (!done)
      --I;

    // skip debug instruction
    if (I->isDebugInstr())
      continue;

    if (I->hasUnmodeledSideEffects() || I->isInlineAsm() || I->isPosition() ||
        I->hasDelaySlot() || I->isBundledWithSucc())
      break;

    if (delayHasHazard(I, sawLoad, sawStore, RegDefs, RegUses)) {
      insertDefsUses(I, RegDefs, RegUses);
      continue;
    }

    return I;
  }
  return MBB.end();
}

bool Filler::delayHasHazard(MachineBasicBlock::iterator candidate,
                            bool &sawLoad,
                            bool &sawStore,
                            SmallSet<unsigned, 32> &RegDefs,
                            SmallSet<unsigned, 32> &RegUses)
{

  if (candidate->isImplicitDef() || candidate->isKill())
    return true;

  if (candidate->mayLoad()) {
    sawLoad = true;
    if (sawStore)
      return true;
  }

  if (candidate->mayStore()) {
    if (sawStore)
      return true;
    sawStore = true;
    if (sawLoad)
      return true;
  }

  for (const MachineOperand &MO : candidate->operands()) {
    if (!MO.isReg())
      continue; // skip

    Register Reg = MO.getReg();

    if (MO.isDef()) {
      // check whether Reg is defined or used before delay slot.
      if (IsRegInSet(RegDefs, Reg) || IsRegInSet(RegUses, Reg))
        return true;
    }
    if (MO.isUse()) {
      // check whether Reg is defined before delay slot.
      if (IsRegInSet(RegDefs, Reg))
        return true;
    }
  }

  unsigned Opcode = candidate->getOpcode();
  // LD and LDD may have NOPs inserted afterwards in the case of some LEON
  // processors, so we can't use the delay slot if this feature is switched-on.
  if (Subtarget->insertNOPLoad()
      &&
      Opcode >=  SP::LDDArr && Opcode <= SP::LDrr)
    return true;

  // Same as above for FDIV and FSQRT on some LEON processors.
  if (Subtarget->fixAllFDIVSQRT()
      &&
      Opcode >=  SP::FDIVD && Opcode <= SP::FSQRTD)
    return true;


  return false;
}


void Filler::insertCallDefsUses(MachineBasicBlock::iterator MI,
                                SmallSet<unsigned, 32>& RegDefs,
                                SmallSet<unsigned, 32>& RegUses)
{
  // Call defines o7, which is visible to the instruction in delay slot.
  RegDefs.insert(SP::O7);

  switch(MI->getOpcode()) {
  default: llvm_unreachable("Unknown opcode.");
  case SP::CALL: break;
  case SP::CALLrr:
  case SP::CALLri:
    assert(MI->getNumOperands() >= 2);
    const MachineOperand &Reg = MI->getOperand(0);
    assert(Reg.isReg() && "CALL first operand is not a register.");
    assert(Reg.isUse() && "CALL first operand is not a use.");
    RegUses.insert(Reg.getReg());

    const MachineOperand &Operand1 = MI->getOperand(1);
    if (Operand1.isImm() || Operand1.isGlobal())
        break;
    assert(Operand1.isReg() && "CALLrr second operand is not a register.");
    assert(Operand1.isUse() && "CALLrr second operand is not a use.");
    RegUses.insert(Operand1.getReg());
    break;
  }
}

// Insert Defs and Uses of MI into the sets RegDefs and RegUses.
void Filler::insertDefsUses(MachineBasicBlock::iterator MI,
                            SmallSet<unsigned, 32>& RegDefs,
                            SmallSet<unsigned, 32>& RegUses)
{
  for (const MachineOperand &MO : MI->operands()) {
    if (!MO.isReg())
      continue;

    Register Reg = MO.getReg();
    if (Reg == 0)
      continue;
    if (MO.isDef())
      RegDefs.insert(Reg);
    if (MO.isUse()) {
      // Implicit register uses of retl are return values and
      // retl does not use them.
      if (MO.isImplicit() && MI->getOpcode() == SP::RETL)
        continue;
      RegUses.insert(Reg);
    }
  }
}

// returns true if the Reg or its alias is in the RegSet.
bool Filler::IsRegInSet(SmallSet<unsigned, 32>& RegSet, unsigned Reg)
{
  // Check Reg and all aliased Registers.
  for (MCRegAliasIterator AI(Reg, Subtarget->getRegisterInfo(), true);
       AI.isValid(); ++AI)
    if (RegSet.count(*AI))
      return true;
  return false;
}

bool Filler::needsUnimp(MachineBasicBlock::iterator I, unsigned &StructSize)
{
  if (!I->isCall())
    return false;

  unsigned structSizeOpNum = 0;
  switch (I->getOpcode()) {
  default: llvm_unreachable("Unknown call opcode.");
  case SP::CALL: structSizeOpNum = 1; break;
  case SP::CALLrr:
  case SP::CALLri: structSizeOpNum = 2; break;
  case SP::TLS_CALL: return false;
  case SP::TAIL_CALLri:
  case SP::TAIL_CALL: return false;
  }

  const MachineOperand &MO = I->getOperand(structSizeOpNum);
  if (!MO.isImm())
    return false;
  StructSize = MO.getImm();
  return true;
}

static bool combineRestoreADD(MachineBasicBlock::iterator RestoreMI,
                              MachineBasicBlock::iterator AddMI,
                              const TargetInstrInfo *TII)
{
  // Before:  add  <op0>, <op1>, %i[0-7]
  //          restore %g0, %g0, %i[0-7]
  //
  // After :  restore <op0>, <op1>, %o[0-7]

  Register reg = AddMI->getOperand(0).getReg();
  if (reg < SP::I0 || reg > SP::I7)
    return false;

  // Erase RESTORE.
  RestoreMI->eraseFromParent();

  // Change ADD to RESTORE.
  AddMI->setDesc(TII->get((AddMI->getOpcode() == SP::ADDrr)
                          ? SP::RESTORErr
                          : SP::RESTOREri));

  // Map the destination register.
  AddMI->getOperand(0).setReg(reg - SP::I0 + SP::O0);

  return true;
}

static bool combineRestoreOR(MachineBasicBlock::iterator RestoreMI,
                             MachineBasicBlock::iterator OrMI,
                             const TargetInstrInfo *TII)
{
  // Before:  or  <op0>, <op1>, %i[0-7]
  //          restore %g0, %g0, %i[0-7]
  //    and <op0> or <op1> is zero,
  //
  // After :  restore <op0>, <op1>, %o[0-7]

  Register reg = OrMI->getOperand(0).getReg();
  if (reg < SP::I0 || reg > SP::I7)
    return false;

  // check whether it is a copy.
  if (OrMI->getOpcode() == SP::ORrr
      && OrMI->getOperand(1).getReg() != SP::G0
      && OrMI->getOperand(2).getReg() != SP::G0)
    return false;

  if (OrMI->getOpcode() == SP::ORri
      && OrMI->getOperand(1).getReg() != SP::G0
      && (!OrMI->getOperand(2).isImm() || OrMI->getOperand(2).getImm() != 0))
    return false;

  // Erase RESTORE.
  RestoreMI->eraseFromParent();

  // Change OR to RESTORE.
  OrMI->setDesc(TII->get((OrMI->getOpcode() == SP::ORrr)
                         ? SP::RESTORErr
                         : SP::RESTOREri));

  // Map the destination register.
  OrMI->getOperand(0).setReg(reg - SP::I0 + SP::O0);

  return true;
}

static bool combineRestoreSETHIi(MachineBasicBlock::iterator RestoreMI,
                                 MachineBasicBlock::iterator SetHiMI,
                                 const TargetInstrInfo *TII)
{
  // Before:  sethi imm3, %i[0-7]
  //          restore %g0, %g0, %g0
  //
  // After :  restore %g0, (imm3<<10), %o[0-7]

  Register reg = SetHiMI->getOperand(0).getReg();
  if (reg < SP::I0 || reg > SP::I7)
    return false;

  if (!SetHiMI->getOperand(1).isImm())
    return false;

  int64_t imm = SetHiMI->getOperand(1).getImm();

  // Is it a 3 bit immediate?
  if (!isInt<3>(imm))
    return false;

  // Make it a 13 bit immediate.
  imm = (imm << 10) & 0x1FFF;

  assert(RestoreMI->getOpcode() == SP::RESTORErr);

  RestoreMI->setDesc(TII->get(SP::RESTOREri));

  RestoreMI->getOperand(0).setReg(reg - SP::I0 + SP::O0);
  RestoreMI->getOperand(1).setReg(SP::G0);
  RestoreMI->getOperand(2).ChangeToImmediate(imm);


  // Erase the original SETHI.
  SetHiMI->eraseFromParent();

  return true;
}

bool Filler::tryCombineRestoreWithPrevInst(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI)
{
  // No previous instruction.
  if (MBBI == MBB.begin())
    return false;

  // assert that MBBI is a "restore %g0, %g0, %g0".
  assert(MBBI->getOpcode() == SP::RESTORErr
         && MBBI->getOperand(0).getReg() == SP::G0
         && MBBI->getOperand(1).getReg() == SP::G0
         && MBBI->getOperand(2).getReg() == SP::G0);

  MachineBasicBlock::iterator PrevInst = std::prev(MBBI);

  // It cannot be combined with a bundled instruction.
  if (PrevInst->isBundledWithSucc())
    return false;

  const TargetInstrInfo *TII = Subtarget->getInstrInfo();

  switch (PrevInst->getOpcode()) {
  default: break;
  case SP::ADDrr:
  case SP::ADDri: return combineRestoreADD(MBBI, PrevInst, TII); break;
  case SP::ORrr:
  case SP::ORri:  return combineRestoreOR(MBBI, PrevInst, TII); break;
  case SP::SETHIi: return combineRestoreSETHIi(MBBI, PrevInst, TII); break;
  }
  // It cannot combine with the previous instruction.
  return false;
}
