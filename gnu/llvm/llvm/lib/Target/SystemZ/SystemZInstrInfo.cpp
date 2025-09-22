//===-- SystemZInstrInfo.cpp - SystemZ instruction information ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the SystemZ implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "SystemZInstrInfo.h"
#include "MCTargetDesc/SystemZMCTargetDesc.h"
#include "SystemZ.h"
#include "SystemZInstrBuilder.h"
#include "SystemZSubtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <cstdint>
#include <iterator>

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#define GET_INSTRMAP_INFO
#include "SystemZGenInstrInfo.inc"

#define DEBUG_TYPE "systemz-II"

// Return a mask with Count low bits set.
static uint64_t allOnes(unsigned int Count) {
  return Count == 0 ? 0 : (uint64_t(1) << (Count - 1) << 1) - 1;
}

// Pin the vtable to this file.
void SystemZInstrInfo::anchor() {}

SystemZInstrInfo::SystemZInstrInfo(SystemZSubtarget &sti)
    : SystemZGenInstrInfo(-1, -1),
      RI(sti.getSpecialRegisters()->getReturnFunctionAddressRegister()),
      STI(sti) {}

// MI is a 128-bit load or store.  Split it into two 64-bit loads or stores,
// each having the opcode given by NewOpcode.
void SystemZInstrInfo::splitMove(MachineBasicBlock::iterator MI,
                                 unsigned NewOpcode) const {
  MachineBasicBlock *MBB = MI->getParent();
  MachineFunction &MF = *MBB->getParent();

  // Get two load or store instructions.  Use the original instruction for
  // one of them and create a clone for the other.
  MachineInstr *HighPartMI = MF.CloneMachineInstr(&*MI);
  MachineInstr *LowPartMI = &*MI;
  MBB->insert(LowPartMI, HighPartMI);

  // Set up the two 64-bit registers and remember super reg and its flags.
  MachineOperand &HighRegOp = HighPartMI->getOperand(0);
  MachineOperand &LowRegOp = LowPartMI->getOperand(0);
  Register Reg128 = LowRegOp.getReg();
  unsigned Reg128Killed = getKillRegState(LowRegOp.isKill());
  unsigned Reg128Undef  = getUndefRegState(LowRegOp.isUndef());
  HighRegOp.setReg(RI.getSubReg(HighRegOp.getReg(), SystemZ::subreg_h64));
  LowRegOp.setReg(RI.getSubReg(LowRegOp.getReg(), SystemZ::subreg_l64));

  // The address in the first (high) instruction is already correct.
  // Adjust the offset in the second (low) instruction.
  MachineOperand &HighOffsetOp = HighPartMI->getOperand(2);
  MachineOperand &LowOffsetOp = LowPartMI->getOperand(2);
  LowOffsetOp.setImm(LowOffsetOp.getImm() + 8);

  // Set the opcodes.
  unsigned HighOpcode = getOpcodeForOffset(NewOpcode, HighOffsetOp.getImm());
  unsigned LowOpcode = getOpcodeForOffset(NewOpcode, LowOffsetOp.getImm());
  assert(HighOpcode && LowOpcode && "Both offsets should be in range");
  HighPartMI->setDesc(get(HighOpcode));
  LowPartMI->setDesc(get(LowOpcode));

  MachineInstr *FirstMI = HighPartMI;
  if (MI->mayStore()) {
    FirstMI->getOperand(0).setIsKill(false);
    // Add implicit uses of the super register in case one of the subregs is
    // undefined. We could track liveness and skip storing an undefined
    // subreg, but this is hopefully rare (discovered with llvm-stress).
    // If Reg128 was killed, set kill flag on MI.
    unsigned Reg128UndefImpl = (Reg128Undef | RegState::Implicit);
    MachineInstrBuilder(MF, HighPartMI).addReg(Reg128, Reg128UndefImpl);
    MachineInstrBuilder(MF, LowPartMI).addReg(Reg128, (Reg128UndefImpl | Reg128Killed));
  } else {
    // If HighPartMI clobbers any of the address registers, it needs to come
    // after LowPartMI.
    auto overlapsAddressReg = [&](Register Reg) -> bool {
      return RI.regsOverlap(Reg, MI->getOperand(1).getReg()) ||
             RI.regsOverlap(Reg, MI->getOperand(3).getReg());
    };
    if (overlapsAddressReg(HighRegOp.getReg())) {
      assert(!overlapsAddressReg(LowRegOp.getReg()) &&
             "Both loads clobber address!");
      MBB->splice(HighPartMI, MBB, LowPartMI);
      FirstMI = LowPartMI;
    }
  }

  // Clear the kill flags on the address registers in the first instruction.
  FirstMI->getOperand(1).setIsKill(false);
  FirstMI->getOperand(3).setIsKill(false);
}

// Split ADJDYNALLOC instruction MI.
void SystemZInstrInfo::splitAdjDynAlloc(MachineBasicBlock::iterator MI) const {
  MachineBasicBlock *MBB = MI->getParent();
  MachineFunction &MF = *MBB->getParent();
  MachineFrameInfo &MFFrame = MF.getFrameInfo();
  MachineOperand &OffsetMO = MI->getOperand(2);
  SystemZCallingConventionRegisters *Regs = STI.getSpecialRegisters();

  uint64_t Offset = (MFFrame.getMaxCallFrameSize() +
                     Regs->getCallFrameSize() +
                     Regs->getStackPointerBias() +
                     OffsetMO.getImm());
  unsigned NewOpcode = getOpcodeForOffset(SystemZ::LA, Offset);
  assert(NewOpcode && "No support for huge argument lists yet");
  MI->setDesc(get(NewOpcode));
  OffsetMO.setImm(Offset);
}

// MI is an RI-style pseudo instruction.  Replace it with LowOpcode
// if the first operand is a low GR32 and HighOpcode if the first operand
// is a high GR32.  ConvertHigh is true if LowOpcode takes a signed operand
// and HighOpcode takes an unsigned 32-bit operand.  In those cases,
// MI has the same kind of operand as LowOpcode, so needs to be converted
// if HighOpcode is used.
void SystemZInstrInfo::expandRIPseudo(MachineInstr &MI, unsigned LowOpcode,
                                      unsigned HighOpcode,
                                      bool ConvertHigh) const {
  Register Reg = MI.getOperand(0).getReg();
  bool IsHigh = SystemZ::isHighReg(Reg);
  MI.setDesc(get(IsHigh ? HighOpcode : LowOpcode));
  if (IsHigh && ConvertHigh)
    MI.getOperand(1).setImm(uint32_t(MI.getOperand(1).getImm()));
}

// MI is a three-operand RIE-style pseudo instruction.  Replace it with
// LowOpcodeK if the registers are both low GR32s, otherwise use a move
// followed by HighOpcode or LowOpcode, depending on whether the target
// is a high or low GR32.
void SystemZInstrInfo::expandRIEPseudo(MachineInstr &MI, unsigned LowOpcode,
                                       unsigned LowOpcodeK,
                                       unsigned HighOpcode) const {
  Register DestReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();
  bool DestIsHigh = SystemZ::isHighReg(DestReg);
  bool SrcIsHigh = SystemZ::isHighReg(SrcReg);
  if (!DestIsHigh && !SrcIsHigh)
    MI.setDesc(get(LowOpcodeK));
  else {
    if (DestReg != SrcReg) {
      emitGRX32Move(*MI.getParent(), MI, MI.getDebugLoc(), DestReg, SrcReg,
                    SystemZ::LR, 32, MI.getOperand(1).isKill(),
                    MI.getOperand(1).isUndef());
      MI.getOperand(1).setReg(DestReg);
    }
    MI.setDesc(get(DestIsHigh ? HighOpcode : LowOpcode));
    MI.tieOperands(0, 1);
  }
}

// MI is an RXY-style pseudo instruction.  Replace it with LowOpcode
// if the first operand is a low GR32 and HighOpcode if the first operand
// is a high GR32.
void SystemZInstrInfo::expandRXYPseudo(MachineInstr &MI, unsigned LowOpcode,
                                       unsigned HighOpcode) const {
  Register Reg = MI.getOperand(0).getReg();
  unsigned Opcode = getOpcodeForOffset(
      SystemZ::isHighReg(Reg) ? HighOpcode : LowOpcode,
      MI.getOperand(2).getImm());
  MI.setDesc(get(Opcode));
}

// MI is a load-on-condition pseudo instruction with a single register
// (source or destination) operand.  Replace it with LowOpcode if the
// register is a low GR32 and HighOpcode if the register is a high GR32.
void SystemZInstrInfo::expandLOCPseudo(MachineInstr &MI, unsigned LowOpcode,
                                       unsigned HighOpcode) const {
  Register Reg = MI.getOperand(0).getReg();
  unsigned Opcode = SystemZ::isHighReg(Reg) ? HighOpcode : LowOpcode;
  MI.setDesc(get(Opcode));
}

// MI is an RR-style pseudo instruction that zero-extends the low Size bits
// of one GRX32 into another.  Replace it with LowOpcode if both operands
// are low registers, otherwise use RISB[LH]G.
void SystemZInstrInfo::expandZExtPseudo(MachineInstr &MI, unsigned LowOpcode,
                                        unsigned Size) const {
  MachineInstrBuilder MIB =
    emitGRX32Move(*MI.getParent(), MI, MI.getDebugLoc(),
               MI.getOperand(0).getReg(), MI.getOperand(1).getReg(), LowOpcode,
               Size, MI.getOperand(1).isKill(), MI.getOperand(1).isUndef());

  // Keep the remaining operands as-is.
  for (const MachineOperand &MO : llvm::drop_begin(MI.operands(), 2))
    MIB.add(MO);

  MI.eraseFromParent();
}

void SystemZInstrInfo::expandLoadStackGuard(MachineInstr *MI) const {
  MachineBasicBlock *MBB = MI->getParent();
  MachineFunction &MF = *MBB->getParent();
  const Register Reg64 = MI->getOperand(0).getReg();
  const Register Reg32 = RI.getSubReg(Reg64, SystemZ::subreg_l32);

  // EAR can only load the low subregister so us a shift for %a0 to produce
  // the GR containing %a0 and %a1.

  // ear <reg>, %a0
  BuildMI(*MBB, MI, MI->getDebugLoc(), get(SystemZ::EAR), Reg32)
    .addReg(SystemZ::A0)
    .addReg(Reg64, RegState::ImplicitDefine);

  // sllg <reg>, <reg>, 32
  BuildMI(*MBB, MI, MI->getDebugLoc(), get(SystemZ::SLLG), Reg64)
    .addReg(Reg64)
    .addReg(0)
    .addImm(32);

  // ear <reg>, %a1
  BuildMI(*MBB, MI, MI->getDebugLoc(), get(SystemZ::EAR), Reg32)
    .addReg(SystemZ::A1);

  // lg <reg>, 40(<reg>)
  MI->setDesc(get(SystemZ::LG));
  MachineInstrBuilder(MF, MI).addReg(Reg64).addImm(40).addReg(0);
}

// Emit a zero-extending move from 32-bit GPR SrcReg to 32-bit GPR
// DestReg before MBBI in MBB.  Use LowLowOpcode when both DestReg and SrcReg
// are low registers, otherwise use RISB[LH]G.  Size is the number of bits
// taken from the low end of SrcReg (8 for LLCR, 16 for LLHR and 32 for LR).
// KillSrc is true if this move is the last use of SrcReg.
MachineInstrBuilder
SystemZInstrInfo::emitGRX32Move(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MBBI,
                                const DebugLoc &DL, unsigned DestReg,
                                unsigned SrcReg, unsigned LowLowOpcode,
                                unsigned Size, bool KillSrc,
                                bool UndefSrc) const {
  unsigned Opcode;
  bool DestIsHigh = SystemZ::isHighReg(DestReg);
  bool SrcIsHigh = SystemZ::isHighReg(SrcReg);
  if (DestIsHigh && SrcIsHigh)
    Opcode = SystemZ::RISBHH;
  else if (DestIsHigh && !SrcIsHigh)
    Opcode = SystemZ::RISBHL;
  else if (!DestIsHigh && SrcIsHigh)
    Opcode = SystemZ::RISBLH;
  else {
    return BuildMI(MBB, MBBI, DL, get(LowLowOpcode), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc) | getUndefRegState(UndefSrc));
  }
  unsigned Rotate = (DestIsHigh != SrcIsHigh ? 32 : 0);
  return BuildMI(MBB, MBBI, DL, get(Opcode), DestReg)
    .addReg(DestReg, RegState::Undef)
    .addReg(SrcReg, getKillRegState(KillSrc) | getUndefRegState(UndefSrc))
    .addImm(32 - Size).addImm(128 + 31).addImm(Rotate);
}

MachineInstr *SystemZInstrInfo::commuteInstructionImpl(MachineInstr &MI,
                                                       bool NewMI,
                                                       unsigned OpIdx1,
                                                       unsigned OpIdx2) const {
  auto cloneIfNew = [NewMI](MachineInstr &MI) -> MachineInstr & {
    if (NewMI)
      return *MI.getParent()->getParent()->CloneMachineInstr(&MI);
    return MI;
  };

  switch (MI.getOpcode()) {
  case SystemZ::SELRMux:
  case SystemZ::SELFHR:
  case SystemZ::SELR:
  case SystemZ::SELGR:
  case SystemZ::LOCRMux:
  case SystemZ::LOCFHR:
  case SystemZ::LOCR:
  case SystemZ::LOCGR: {
    auto &WorkingMI = cloneIfNew(MI);
    // Invert condition.
    unsigned CCValid = WorkingMI.getOperand(3).getImm();
    unsigned CCMask = WorkingMI.getOperand(4).getImm();
    WorkingMI.getOperand(4).setImm(CCMask ^ CCValid);
    return TargetInstrInfo::commuteInstructionImpl(WorkingMI, /*NewMI=*/false,
                                                   OpIdx1, OpIdx2);
  }
  default:
    return TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
  }
}

// If MI is a simple load or store for a frame object, return the register
// it loads or stores and set FrameIndex to the index of the frame object.
// Return 0 otherwise.
//
// Flag is SimpleBDXLoad for loads and SimpleBDXStore for stores.
static int isSimpleMove(const MachineInstr &MI, int &FrameIndex,
                        unsigned Flag) {
  const MCInstrDesc &MCID = MI.getDesc();
  if ((MCID.TSFlags & Flag) && MI.getOperand(1).isFI() &&
      MI.getOperand(2).getImm() == 0 && MI.getOperand(3).getReg() == 0) {
    FrameIndex = MI.getOperand(1).getIndex();
    return MI.getOperand(0).getReg();
  }
  return 0;
}

Register SystemZInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                               int &FrameIndex) const {
  return isSimpleMove(MI, FrameIndex, SystemZII::SimpleBDXLoad);
}

Register SystemZInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                              int &FrameIndex) const {
  return isSimpleMove(MI, FrameIndex, SystemZII::SimpleBDXStore);
}

bool SystemZInstrInfo::isStackSlotCopy(const MachineInstr &MI,
                                       int &DestFrameIndex,
                                       int &SrcFrameIndex) const {
  // Check for MVC 0(Length,FI1),0(FI2)
  const MachineFrameInfo &MFI = MI.getParent()->getParent()->getFrameInfo();
  if (MI.getOpcode() != SystemZ::MVC || !MI.getOperand(0).isFI() ||
      MI.getOperand(1).getImm() != 0 || !MI.getOperand(3).isFI() ||
      MI.getOperand(4).getImm() != 0)
    return false;

  // Check that Length covers the full slots.
  int64_t Length = MI.getOperand(2).getImm();
  unsigned FI1 = MI.getOperand(0).getIndex();
  unsigned FI2 = MI.getOperand(3).getIndex();
  if (MFI.getObjectSize(FI1) != Length ||
      MFI.getObjectSize(FI2) != Length)
    return false;

  DestFrameIndex = FI1;
  SrcFrameIndex = FI2;
  return true;
}

bool SystemZInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *&TBB,
                                     MachineBasicBlock *&FBB,
                                     SmallVectorImpl<MachineOperand> &Cond,
                                     bool AllowModify) const {
  // Most of the code and comments here are boilerplate.

  // Start from the bottom of the block and work up, examining the
  // terminator instructions.
  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;

    // Working from the bottom, when we see a non-terminator instruction, we're
    // done.
    if (!isUnpredicatedTerminator(*I))
      break;

    // A terminator that isn't a branch can't easily be handled by this
    // analysis.
    if (!I->isBranch())
      return true;

    // Can't handle indirect branches.
    SystemZII::Branch Branch(getBranchInfo(*I));
    if (!Branch.hasMBBTarget())
      return true;

    // Punt on compound branches.
    if (Branch.Type != SystemZII::BranchNormal)
      return true;

    if (Branch.CCMask == SystemZ::CCMASK_ANY) {
      // Handle unconditional branches.
      if (!AllowModify) {
        TBB = Branch.getMBBTarget();
        continue;
      }

      // If the block has any instructions after a JMP, delete them.
      MBB.erase(std::next(I), MBB.end());

      Cond.clear();
      FBB = nullptr;

      // Delete the JMP if it's equivalent to a fall-through.
      if (MBB.isLayoutSuccessor(Branch.getMBBTarget())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        continue;
      }

      // TBB is used to indicate the unconditinal destination.
      TBB = Branch.getMBBTarget();
      continue;
    }

    // Working from the bottom, handle the first conditional branch.
    if (Cond.empty()) {
      // FIXME: add X86-style branch swap
      FBB = TBB;
      TBB = Branch.getMBBTarget();
      Cond.push_back(MachineOperand::CreateImm(Branch.CCValid));
      Cond.push_back(MachineOperand::CreateImm(Branch.CCMask));
      continue;
    }

    // Handle subsequent conditional branches.
    assert(Cond.size() == 2 && TBB && "Should have seen a conditional branch");

    // Only handle the case where all conditional branches branch to the same
    // destination.
    if (TBB != Branch.getMBBTarget())
      return true;

    // If the conditions are the same, we can leave them alone.
    unsigned OldCCValid = Cond[0].getImm();
    unsigned OldCCMask = Cond[1].getImm();
    if (OldCCValid == Branch.CCValid && OldCCMask == Branch.CCMask)
      continue;

    // FIXME: Try combining conditions like X86 does.  Should be easy on Z!
    return false;
  }

  return false;
}

unsigned SystemZInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                        int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  // Most of the code and comments here are boilerplate.
  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!I->isBranch())
      break;
    if (!getBranchInfo(*I).hasMBBTarget())
      break;
    // Remove the branch.
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  return Count;
}

bool SystemZInstrInfo::
reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 2 && "Invalid condition");
  Cond[1].setImm(Cond[1].getImm() ^ Cond[0].getImm());
  return false;
}

unsigned SystemZInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                        MachineBasicBlock *TBB,
                                        MachineBasicBlock *FBB,
                                        ArrayRef<MachineOperand> Cond,
                                        const DebugLoc &DL,
                                        int *BytesAdded) const {
  // In this function we output 32-bit branches, which should always
  // have enough range.  They can be shortened and relaxed by later code
  // in the pipeline, if desired.

  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 2 || Cond.size() == 0) &&
         "SystemZ branch conditions have one component!");
  assert(!BytesAdded && "code size not handled");

  if (Cond.empty()) {
    // Unconditional branch?
    assert(!FBB && "Unconditional branch with multiple successors!");
    BuildMI(&MBB, DL, get(SystemZ::J)).addMBB(TBB);
    return 1;
  }

  // Conditional branch.
  unsigned Count = 0;
  unsigned CCValid = Cond[0].getImm();
  unsigned CCMask = Cond[1].getImm();
  BuildMI(&MBB, DL, get(SystemZ::BRC))
    .addImm(CCValid).addImm(CCMask).addMBB(TBB);
  ++Count;

  if (FBB) {
    // Two-way Conditional branch. Insert the second branch.
    BuildMI(&MBB, DL, get(SystemZ::J)).addMBB(FBB);
    ++Count;
  }
  return Count;
}

bool SystemZInstrInfo::analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                                      Register &SrcReg2, int64_t &Mask,
                                      int64_t &Value) const {
  assert(MI.isCompare() && "Caller should have checked for a comparison");

  if (MI.getNumExplicitOperands() == 2 && MI.getOperand(0).isReg() &&
      MI.getOperand(1).isImm()) {
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = 0;
    Value = MI.getOperand(1).getImm();
    Mask = ~0;
    return true;
  }

  return false;
}

bool SystemZInstrInfo::canInsertSelect(const MachineBasicBlock &MBB,
                                       ArrayRef<MachineOperand> Pred,
                                       Register DstReg, Register TrueReg,
                                       Register FalseReg, int &CondCycles,
                                       int &TrueCycles,
                                       int &FalseCycles) const {
  // Not all subtargets have LOCR instructions.
  if (!STI.hasLoadStoreOnCond())
    return false;
  if (Pred.size() != 2)
    return false;

  // Check register classes.
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RC =
    RI.getCommonSubClass(MRI.getRegClass(TrueReg), MRI.getRegClass(FalseReg));
  if (!RC)
    return false;

  // We have LOCR instructions for 32 and 64 bit general purpose registers.
  if ((STI.hasLoadStoreOnCond2() &&
       SystemZ::GRX32BitRegClass.hasSubClassEq(RC)) ||
      SystemZ::GR32BitRegClass.hasSubClassEq(RC) ||
      SystemZ::GR64BitRegClass.hasSubClassEq(RC)) {
    CondCycles = 2;
    TrueCycles = 2;
    FalseCycles = 2;
    return true;
  }

  // Can't do anything else.
  return false;
}

void SystemZInstrInfo::insertSelect(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    const DebugLoc &DL, Register DstReg,
                                    ArrayRef<MachineOperand> Pred,
                                    Register TrueReg,
                                    Register FalseReg) const {
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RC = MRI.getRegClass(DstReg);

  assert(Pred.size() == 2 && "Invalid condition");
  unsigned CCValid = Pred[0].getImm();
  unsigned CCMask = Pred[1].getImm();

  unsigned Opc;
  if (SystemZ::GRX32BitRegClass.hasSubClassEq(RC)) {
    if (STI.hasMiscellaneousExtensions3())
      Opc = SystemZ::SELRMux;
    else if (STI.hasLoadStoreOnCond2())
      Opc = SystemZ::LOCRMux;
    else {
      Opc = SystemZ::LOCR;
      MRI.constrainRegClass(DstReg, &SystemZ::GR32BitRegClass);
      Register TReg = MRI.createVirtualRegister(&SystemZ::GR32BitRegClass);
      Register FReg = MRI.createVirtualRegister(&SystemZ::GR32BitRegClass);
      BuildMI(MBB, I, DL, get(TargetOpcode::COPY), TReg).addReg(TrueReg);
      BuildMI(MBB, I, DL, get(TargetOpcode::COPY), FReg).addReg(FalseReg);
      TrueReg = TReg;
      FalseReg = FReg;
    }
  } else if (SystemZ::GR64BitRegClass.hasSubClassEq(RC)) {
    if (STI.hasMiscellaneousExtensions3())
      Opc = SystemZ::SELGR;
    else
      Opc = SystemZ::LOCGR;
  } else
    llvm_unreachable("Invalid register class");

  BuildMI(MBB, I, DL, get(Opc), DstReg)
    .addReg(FalseReg).addReg(TrueReg)
    .addImm(CCValid).addImm(CCMask);
}

MachineInstr *SystemZInstrInfo::optimizeLoadInstr(MachineInstr &MI,
                                                  const MachineRegisterInfo *MRI,
                                                  Register &FoldAsLoadDefReg,
                                                  MachineInstr *&DefMI) const {
  // Check whether we can move the DefMI load, and that it only has one use.
  DefMI = MRI->getVRegDef(FoldAsLoadDefReg);
  assert(DefMI);
  bool SawStore = false;
  if (!DefMI->isSafeToMove(nullptr, SawStore) ||
      !MRI->hasOneNonDBGUse(FoldAsLoadDefReg))
    return nullptr;

  int UseOpIdx =
      MI.findRegisterUseOperandIdx(FoldAsLoadDefReg, /*TRI=*/nullptr);
  assert(UseOpIdx != -1 && "Expected FoldAsLoadDefReg to be used by MI.");

  // Check whether we can fold the load.
  if (MachineInstr *FoldMI =
          foldMemoryOperand(MI, {((unsigned)UseOpIdx)}, *DefMI)) {
    FoldAsLoadDefReg = 0;
    return FoldMI;
  }

  return nullptr;
}

bool SystemZInstrInfo::foldImmediate(MachineInstr &UseMI, MachineInstr &DefMI,
                                     Register Reg,
                                     MachineRegisterInfo *MRI) const {
  unsigned DefOpc = DefMI.getOpcode();

  if (DefOpc == SystemZ::VGBM) {
    int64_t ImmVal = DefMI.getOperand(1).getImm();
    if (ImmVal != 0) // TODO: Handle other values
      return false;

    // Fold gr128 = COPY (vr128 VGBM imm)
    //
    // %tmp:gr64 = LGHI 0
    // to  gr128 = REG_SEQUENCE %tmp, %tmp
    assert(DefMI.getOperand(0).getReg() == Reg);

    if (!UseMI.isCopy())
      return false;

    Register CopyDstReg = UseMI.getOperand(0).getReg();
    if (CopyDstReg.isVirtual() &&
        MRI->getRegClass(CopyDstReg) == &SystemZ::GR128BitRegClass &&
        MRI->hasOneNonDBGUse(Reg)) {
      // TODO: Handle physical registers
      // TODO: Handle gr64 uses with subregister indexes
      // TODO: Should this multi-use cases?
      Register TmpReg = MRI->createVirtualRegister(&SystemZ::GR64BitRegClass);
      MachineBasicBlock &MBB = *UseMI.getParent();

      loadImmediate(MBB, UseMI.getIterator(), TmpReg, ImmVal);

      UseMI.setDesc(get(SystemZ::REG_SEQUENCE));
      UseMI.getOperand(1).setReg(TmpReg);
      MachineInstrBuilder(*MBB.getParent(), &UseMI)
          .addImm(SystemZ::subreg_h64)
          .addReg(TmpReg)
          .addImm(SystemZ::subreg_l64);

      if (MRI->use_nodbg_empty(Reg))
        DefMI.eraseFromParent();
      return true;
    }

    return false;
  }

  if (DefOpc != SystemZ::LHIMux && DefOpc != SystemZ::LHI &&
      DefOpc != SystemZ::LGHI)
    return false;
  if (DefMI.getOperand(0).getReg() != Reg)
    return false;
  int32_t ImmVal = (int32_t)DefMI.getOperand(1).getImm();

  unsigned UseOpc = UseMI.getOpcode();
  unsigned NewUseOpc;
  unsigned UseIdx;
  int CommuteIdx = -1;
  bool TieOps = false;
  switch (UseOpc) {
  case SystemZ::SELRMux:
    TieOps = true;
    [[fallthrough]];
  case SystemZ::LOCRMux:
    if (!STI.hasLoadStoreOnCond2())
      return false;
    NewUseOpc = SystemZ::LOCHIMux;
    if (UseMI.getOperand(2).getReg() == Reg)
      UseIdx = 2;
    else if (UseMI.getOperand(1).getReg() == Reg)
      UseIdx = 2, CommuteIdx = 1;
    else
      return false;
    break;
  case SystemZ::SELGR:
    TieOps = true;
    [[fallthrough]];
  case SystemZ::LOCGR:
    if (!STI.hasLoadStoreOnCond2())
      return false;
    NewUseOpc = SystemZ::LOCGHI;
    if (UseMI.getOperand(2).getReg() == Reg)
      UseIdx = 2;
    else if (UseMI.getOperand(1).getReg() == Reg)
      UseIdx = 2, CommuteIdx = 1;
    else
      return false;
    break;
  default:
    return false;
  }

  if (CommuteIdx != -1)
    if (!commuteInstruction(UseMI, false, CommuteIdx, UseIdx))
      return false;

  bool DeleteDef = MRI->hasOneNonDBGUse(Reg);
  UseMI.setDesc(get(NewUseOpc));
  if (TieOps)
    UseMI.tieOperands(0, 1);
  UseMI.getOperand(UseIdx).ChangeToImmediate(ImmVal);
  if (DeleteDef)
    DefMI.eraseFromParent();

  return true;
}

bool SystemZInstrInfo::isPredicable(const MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();
  if (Opcode == SystemZ::Return ||
      Opcode == SystemZ::Return_XPLINK ||
      Opcode == SystemZ::Trap ||
      Opcode == SystemZ::CallJG ||
      Opcode == SystemZ::CallBR)
    return true;
  return false;
}

bool SystemZInstrInfo::
isProfitableToIfCvt(MachineBasicBlock &MBB,
                    unsigned NumCycles, unsigned ExtraPredCycles,
                    BranchProbability Probability) const {
  // Avoid using conditional returns at the end of a loop (since then
  // we'd need to emit an unconditional branch to the beginning anyway,
  // making the loop body longer).  This doesn't apply for low-probability
  // loops (eg. compare-and-swap retry), so just decide based on branch
  // probability instead of looping structure.
  // However, since Compare and Trap instructions cost the same as a regular
  // Compare instruction, we should allow the if conversion to convert this
  // into a Conditional Compare regardless of the branch probability.
  if (MBB.getLastNonDebugInstr()->getOpcode() != SystemZ::Trap &&
      MBB.succ_empty() && Probability < BranchProbability(1, 8))
    return false;
  // For now only convert single instructions.
  return NumCycles == 1;
}

bool SystemZInstrInfo::
isProfitableToIfCvt(MachineBasicBlock &TMBB,
                    unsigned NumCyclesT, unsigned ExtraPredCyclesT,
                    MachineBasicBlock &FMBB,
                    unsigned NumCyclesF, unsigned ExtraPredCyclesF,
                    BranchProbability Probability) const {
  // For now avoid converting mutually-exclusive cases.
  return false;
}

bool SystemZInstrInfo::
isProfitableToDupForIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                          BranchProbability Probability) const {
  // For now only duplicate single instructions.
  return NumCycles == 1;
}

bool SystemZInstrInfo::PredicateInstruction(
    MachineInstr &MI, ArrayRef<MachineOperand> Pred) const {
  assert(Pred.size() == 2 && "Invalid condition");
  unsigned CCValid = Pred[0].getImm();
  unsigned CCMask = Pred[1].getImm();
  assert(CCMask > 0 && CCMask < 15 && "Invalid predicate");
  unsigned Opcode = MI.getOpcode();
  if (Opcode == SystemZ::Trap) {
    MI.setDesc(get(SystemZ::CondTrap));
    MachineInstrBuilder(*MI.getParent()->getParent(), MI)
      .addImm(CCValid).addImm(CCMask)
      .addReg(SystemZ::CC, RegState::Implicit);
    return true;
  }
  if (Opcode == SystemZ::Return || Opcode == SystemZ::Return_XPLINK) {
    MI.setDesc(get(Opcode == SystemZ::Return ? SystemZ::CondReturn
                                             : SystemZ::CondReturn_XPLINK));
    MachineInstrBuilder(*MI.getParent()->getParent(), MI)
        .addImm(CCValid)
        .addImm(CCMask)
        .addReg(SystemZ::CC, RegState::Implicit);
    return true;
  }
  if (Opcode == SystemZ::CallJG) {
    MachineOperand FirstOp = MI.getOperand(0);
    const uint32_t *RegMask = MI.getOperand(1).getRegMask();
    MI.removeOperand(1);
    MI.removeOperand(0);
    MI.setDesc(get(SystemZ::CallBRCL));
    MachineInstrBuilder(*MI.getParent()->getParent(), MI)
        .addImm(CCValid)
        .addImm(CCMask)
        .add(FirstOp)
        .addRegMask(RegMask)
        .addReg(SystemZ::CC, RegState::Implicit);
    return true;
  }
  if (Opcode == SystemZ::CallBR) {
    MachineOperand Target = MI.getOperand(0);
    const uint32_t *RegMask = MI.getOperand(1).getRegMask();
    MI.removeOperand(1);
    MI.removeOperand(0);
    MI.setDesc(get(SystemZ::CallBCR));
    MachineInstrBuilder(*MI.getParent()->getParent(), MI)
      .addImm(CCValid).addImm(CCMask)
      .add(Target)
      .addRegMask(RegMask)
      .addReg(SystemZ::CC, RegState::Implicit);
    return true;
  }
  return false;
}

void SystemZInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   const DebugLoc &DL, MCRegister DestReg,
                                   MCRegister SrcReg, bool KillSrc) const {
  // Split 128-bit GPR moves into two 64-bit moves. Add implicit uses of the
  // super register in case one of the subregs is undefined.
  // This handles ADDR128 too.
  if (SystemZ::GR128BitRegClass.contains(DestReg, SrcReg)) {
    copyPhysReg(MBB, MBBI, DL, RI.getSubReg(DestReg, SystemZ::subreg_h64),
                RI.getSubReg(SrcReg, SystemZ::subreg_h64), KillSrc);
    MachineInstrBuilder(*MBB.getParent(), std::prev(MBBI))
      .addReg(SrcReg, RegState::Implicit);
    copyPhysReg(MBB, MBBI, DL, RI.getSubReg(DestReg, SystemZ::subreg_l64),
                RI.getSubReg(SrcReg, SystemZ::subreg_l64), KillSrc);
    MachineInstrBuilder(*MBB.getParent(), std::prev(MBBI))
      .addReg(SrcReg, (getKillRegState(KillSrc) | RegState::Implicit));
    return;
  }

  if (SystemZ::GRX32BitRegClass.contains(DestReg, SrcReg)) {
    emitGRX32Move(MBB, MBBI, DL, DestReg, SrcReg, SystemZ::LR, 32, KillSrc,
                  false);
    return;
  }

  // Move 128-bit floating-point values between VR128 and FP128.
  if (SystemZ::VR128BitRegClass.contains(DestReg) &&
      SystemZ::FP128BitRegClass.contains(SrcReg)) {
    MCRegister SrcRegHi =
        RI.getMatchingSuperReg(RI.getSubReg(SrcReg, SystemZ::subreg_h64),
                               SystemZ::subreg_h64, &SystemZ::VR128BitRegClass);
    MCRegister SrcRegLo =
        RI.getMatchingSuperReg(RI.getSubReg(SrcReg, SystemZ::subreg_l64),
                               SystemZ::subreg_h64, &SystemZ::VR128BitRegClass);

    BuildMI(MBB, MBBI, DL, get(SystemZ::VMRHG), DestReg)
      .addReg(SrcRegHi, getKillRegState(KillSrc))
      .addReg(SrcRegLo, getKillRegState(KillSrc));
    return;
  }
  if (SystemZ::FP128BitRegClass.contains(DestReg) &&
      SystemZ::VR128BitRegClass.contains(SrcReg)) {
    MCRegister DestRegHi =
        RI.getMatchingSuperReg(RI.getSubReg(DestReg, SystemZ::subreg_h64),
                               SystemZ::subreg_h64, &SystemZ::VR128BitRegClass);
    MCRegister DestRegLo =
        RI.getMatchingSuperReg(RI.getSubReg(DestReg, SystemZ::subreg_l64),
                               SystemZ::subreg_h64, &SystemZ::VR128BitRegClass);

    if (DestRegHi != SrcReg)
      copyPhysReg(MBB, MBBI, DL, DestRegHi, SrcReg, false);
    BuildMI(MBB, MBBI, DL, get(SystemZ::VREPG), DestRegLo)
      .addReg(SrcReg, getKillRegState(KillSrc)).addImm(1);
    return;
  }

  if (SystemZ::FP128BitRegClass.contains(DestReg) &&
      SystemZ::GR128BitRegClass.contains(SrcReg)) {
    MCRegister DestRegHi = RI.getSubReg(DestReg, SystemZ::subreg_h64);
    MCRegister DestRegLo = RI.getSubReg(DestReg, SystemZ::subreg_l64);
    MCRegister SrcRegHi = RI.getSubReg(SrcReg, SystemZ::subreg_h64);
    MCRegister SrcRegLo = RI.getSubReg(SrcReg, SystemZ::subreg_l64);

    BuildMI(MBB, MBBI, DL, get(SystemZ::LDGR), DestRegHi)
        .addReg(SrcRegHi)
        .addReg(DestReg, RegState::ImplicitDefine);

    BuildMI(MBB, MBBI, DL, get(SystemZ::LDGR), DestRegLo)
        .addReg(SrcRegLo, getKillRegState(KillSrc));
    return;
  }

  // Move CC value from a GR32.
  if (DestReg == SystemZ::CC) {
    unsigned Opcode =
      SystemZ::GR32BitRegClass.contains(SrcReg) ? SystemZ::TMLH : SystemZ::TMHH;
    BuildMI(MBB, MBBI, DL, get(Opcode))
      .addReg(SrcReg, getKillRegState(KillSrc))
      .addImm(3 << (SystemZ::IPM_CC - 16));
    return;
  }

  if (SystemZ::GR128BitRegClass.contains(DestReg) &&
      SystemZ::VR128BitRegClass.contains(SrcReg)) {
    MCRegister DestH64 = RI.getSubReg(DestReg, SystemZ::subreg_h64);
    MCRegister DestL64 = RI.getSubReg(DestReg, SystemZ::subreg_l64);

    BuildMI(MBB, MBBI, DL, get(SystemZ::VLGVG), DestH64)
        .addReg(SrcReg)
        .addReg(SystemZ::NoRegister)
        .addImm(0)
        .addDef(DestReg, RegState::Implicit);
    BuildMI(MBB, MBBI, DL, get(SystemZ::VLGVG), DestL64)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .addReg(SystemZ::NoRegister)
        .addImm(1);
    return;
  }

  if (SystemZ::VR128BitRegClass.contains(DestReg) &&
      SystemZ::GR128BitRegClass.contains(SrcReg)) {
    BuildMI(MBB, MBBI, DL, get(SystemZ::VLVGP), DestReg)
        .addReg(RI.getSubReg(SrcReg, SystemZ::subreg_h64))
        .addReg(RI.getSubReg(SrcReg, SystemZ::subreg_l64));
    return;
  }

  // Everything else needs only one instruction.
  unsigned Opcode;
  if (SystemZ::GR64BitRegClass.contains(DestReg, SrcReg))
    Opcode = SystemZ::LGR;
  else if (SystemZ::FP32BitRegClass.contains(DestReg, SrcReg))
    // For z13 we prefer LDR over LER to avoid partial register dependencies.
    Opcode = STI.hasVector() ? SystemZ::LDR32 : SystemZ::LER;
  else if (SystemZ::FP64BitRegClass.contains(DestReg, SrcReg))
    Opcode = SystemZ::LDR;
  else if (SystemZ::FP128BitRegClass.contains(DestReg, SrcReg))
    Opcode = SystemZ::LXR;
  else if (SystemZ::VR32BitRegClass.contains(DestReg, SrcReg))
    Opcode = SystemZ::VLR32;
  else if (SystemZ::VR64BitRegClass.contains(DestReg, SrcReg))
    Opcode = SystemZ::VLR64;
  else if (SystemZ::VR128BitRegClass.contains(DestReg, SrcReg))
    Opcode = SystemZ::VLR;
  else if (SystemZ::AR32BitRegClass.contains(DestReg, SrcReg))
    Opcode = SystemZ::CPYA;
  else
    llvm_unreachable("Impossible reg-to-reg copy");

  BuildMI(MBB, MBBI, DL, get(Opcode), DestReg)
    .addReg(SrcReg, getKillRegState(KillSrc));
}

void SystemZInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, Register SrcReg,
    bool isKill, int FrameIdx, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI, Register VReg) const {
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Callers may expect a single instruction, so keep 128-bit moves
  // together for now and lower them after register allocation.
  unsigned LoadOpcode, StoreOpcode;
  getLoadStoreOpcodes(RC, LoadOpcode, StoreOpcode);
  addFrameReference(BuildMI(MBB, MBBI, DL, get(StoreOpcode))
                        .addReg(SrcReg, getKillRegState(isKill)),
                    FrameIdx);
}

void SystemZInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator MBBI,
                                            Register DestReg, int FrameIdx,
                                            const TargetRegisterClass *RC,
                                            const TargetRegisterInfo *TRI,
                                            Register VReg) const {
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Callers may expect a single instruction, so keep 128-bit moves
  // together for now and lower them after register allocation.
  unsigned LoadOpcode, StoreOpcode;
  getLoadStoreOpcodes(RC, LoadOpcode, StoreOpcode);
  addFrameReference(BuildMI(MBB, MBBI, DL, get(LoadOpcode), DestReg),
                    FrameIdx);
}

// Return true if MI is a simple load or store with a 12-bit displacement
// and no index.  Flag is SimpleBDXLoad for loads and SimpleBDXStore for stores.
static bool isSimpleBD12Move(const MachineInstr *MI, unsigned Flag) {
  const MCInstrDesc &MCID = MI->getDesc();
  return ((MCID.TSFlags & Flag) &&
          isUInt<12>(MI->getOperand(2).getImm()) &&
          MI->getOperand(3).getReg() == 0);
}

namespace {

struct LogicOp {
  LogicOp() = default;
  LogicOp(unsigned regSize, unsigned immLSB, unsigned immSize)
    : RegSize(regSize), ImmLSB(immLSB), ImmSize(immSize) {}

  explicit operator bool() const { return RegSize; }

  unsigned RegSize = 0;
  unsigned ImmLSB = 0;
  unsigned ImmSize = 0;
};

} // end anonymous namespace

static LogicOp interpretAndImmediate(unsigned Opcode) {
  switch (Opcode) {
  case SystemZ::NILMux: return LogicOp(32,  0, 16);
  case SystemZ::NIHMux: return LogicOp(32, 16, 16);
  case SystemZ::NILL64: return LogicOp(64,  0, 16);
  case SystemZ::NILH64: return LogicOp(64, 16, 16);
  case SystemZ::NIHL64: return LogicOp(64, 32, 16);
  case SystemZ::NIHH64: return LogicOp(64, 48, 16);
  case SystemZ::NIFMux: return LogicOp(32,  0, 32);
  case SystemZ::NILF64: return LogicOp(64,  0, 32);
  case SystemZ::NIHF64: return LogicOp(64, 32, 32);
  default:              return LogicOp();
  }
}

static void transferDeadCC(MachineInstr *OldMI, MachineInstr *NewMI) {
  if (OldMI->registerDefIsDead(SystemZ::CC, /*TRI=*/nullptr)) {
    MachineOperand *CCDef =
        NewMI->findRegisterDefOperand(SystemZ::CC, /*TRI=*/nullptr);
    if (CCDef != nullptr)
      CCDef->setIsDead(true);
  }
}

static void transferMIFlag(MachineInstr *OldMI, MachineInstr *NewMI,
                           MachineInstr::MIFlag Flag) {
  if (OldMI->getFlag(Flag))
    NewMI->setFlag(Flag);
}

MachineInstr *
SystemZInstrInfo::convertToThreeAddress(MachineInstr &MI, LiveVariables *LV,
                                        LiveIntervals *LIS) const {
  MachineBasicBlock *MBB = MI.getParent();

  // Try to convert an AND into an RISBG-type instruction.
  // TODO: It might be beneficial to select RISBG and shorten to AND instead.
  if (LogicOp And = interpretAndImmediate(MI.getOpcode())) {
    uint64_t Imm = MI.getOperand(2).getImm() << And.ImmLSB;
    // AND IMMEDIATE leaves the other bits of the register unchanged.
    Imm |= allOnes(And.RegSize) & ~(allOnes(And.ImmSize) << And.ImmLSB);
    unsigned Start, End;
    if (isRxSBGMask(Imm, And.RegSize, Start, End)) {
      unsigned NewOpcode;
      if (And.RegSize == 64) {
        NewOpcode = SystemZ::RISBG;
        // Prefer RISBGN if available, since it does not clobber CC.
        if (STI.hasMiscellaneousExtensions())
          NewOpcode = SystemZ::RISBGN;
      } else {
        NewOpcode = SystemZ::RISBMux;
        Start &= 31;
        End &= 31;
      }
      MachineOperand &Dest = MI.getOperand(0);
      MachineOperand &Src = MI.getOperand(1);
      MachineInstrBuilder MIB =
          BuildMI(*MBB, MI, MI.getDebugLoc(), get(NewOpcode))
              .add(Dest)
              .addReg(0)
              .addReg(Src.getReg(), getKillRegState(Src.isKill()),
                      Src.getSubReg())
              .addImm(Start)
              .addImm(End + 128)
              .addImm(0);
      if (LV) {
        unsigned NumOps = MI.getNumOperands();
        for (unsigned I = 1; I < NumOps; ++I) {
          MachineOperand &Op = MI.getOperand(I);
          if (Op.isReg() && Op.isKill())
            LV->replaceKillInstruction(Op.getReg(), MI, *MIB);
        }
      }
      if (LIS)
        LIS->ReplaceMachineInstrInMaps(MI, *MIB);
      transferDeadCC(&MI, MIB);
      return MIB;
    }
  }
  return nullptr;
}

bool SystemZInstrInfo::isAssociativeAndCommutative(const MachineInstr &Inst,
                                                   bool Invert) const {
  unsigned Opc = Inst.getOpcode();
  if (Invert) {
    auto InverseOpcode = getInverseOpcode(Opc);
    if (!InverseOpcode)
      return false;
    Opc = *InverseOpcode;
  }

  switch (Opc) {
  default:
    break;
  // Adds and multiplications.
  case SystemZ::WFADB:
  case SystemZ::WFASB:
  case SystemZ::WFAXB:
  case SystemZ::VFADB:
  case SystemZ::VFASB:
  case SystemZ::WFMDB:
  case SystemZ::WFMSB:
  case SystemZ::WFMXB:
  case SystemZ::VFMDB:
  case SystemZ::VFMSB:
    return (Inst.getFlag(MachineInstr::MIFlag::FmReassoc) &&
            Inst.getFlag(MachineInstr::MIFlag::FmNsz));
  }

  return false;
}

std::optional<unsigned>
SystemZInstrInfo::getInverseOpcode(unsigned Opcode) const {
  // fadd => fsub
  switch (Opcode) {
  case SystemZ::WFADB:
    return SystemZ::WFSDB;
  case SystemZ::WFASB:
    return SystemZ::WFSSB;
  case SystemZ::WFAXB:
    return SystemZ::WFSXB;
  case SystemZ::VFADB:
    return SystemZ::VFSDB;
  case SystemZ::VFASB:
    return SystemZ::VFSSB;
  // fsub => fadd
  case SystemZ::WFSDB:
    return SystemZ::WFADB;
  case SystemZ::WFSSB:
    return SystemZ::WFASB;
  case SystemZ::WFSXB:
    return SystemZ::WFAXB;
  case SystemZ::VFSDB:
    return SystemZ::VFADB;
  case SystemZ::VFSSB:
    return SystemZ::VFASB;
  default:
    return std::nullopt;
  }
}

MachineInstr *SystemZInstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
    MachineBasicBlock::iterator InsertPt, int FrameIndex,
    LiveIntervals *LIS, VirtRegMap *VRM) const {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned Size = MFI.getObjectSize(FrameIndex);
  unsigned Opcode = MI.getOpcode();

  // Check CC liveness if new instruction introduces a dead def of CC.
  SlotIndex MISlot = SlotIndex();
  LiveRange *CCLiveRange = nullptr;
  bool CCLiveAtMI = true;
  if (LIS) {
    MISlot = LIS->getSlotIndexes()->getInstructionIndex(MI).getRegSlot();
    auto CCUnits = TRI->regunits(MCRegister::from(SystemZ::CC));
    assert(range_size(CCUnits) == 1 && "CC only has one reg unit.");
    CCLiveRange = &LIS->getRegUnit(*CCUnits.begin());
    CCLiveAtMI = CCLiveRange->liveAt(MISlot);
  }

  if (Ops.size() == 2 && Ops[0] == 0 && Ops[1] == 1) {
    if (!CCLiveAtMI && (Opcode == SystemZ::LA || Opcode == SystemZ::LAY) &&
        isInt<8>(MI.getOperand(2).getImm()) && !MI.getOperand(3).getReg()) {
      // LA(Y) %reg, CONST(%reg) -> AGSI %mem, CONST
      MachineInstr *BuiltMI = BuildMI(*InsertPt->getParent(), InsertPt,
                                      MI.getDebugLoc(), get(SystemZ::AGSI))
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .addImm(MI.getOperand(2).getImm());
      BuiltMI->findRegisterDefOperand(SystemZ::CC, /*TRI=*/nullptr)
          ->setIsDead(true);
      CCLiveRange->createDeadDef(MISlot, LIS->getVNInfoAllocator());
      return BuiltMI;
    }
    return nullptr;
  }

  // All other cases require a single operand.
  if (Ops.size() != 1)
    return nullptr;

  unsigned OpNum = Ops[0];
  assert(Size * 8 ==
           TRI->getRegSizeInBits(*MF.getRegInfo()
                               .getRegClass(MI.getOperand(OpNum).getReg())) &&
         "Invalid size combination");

  if ((Opcode == SystemZ::AHI || Opcode == SystemZ::AGHI) && OpNum == 0 &&
      isInt<8>(MI.getOperand(2).getImm())) {
    // A(G)HI %reg, CONST -> A(G)SI %mem, CONST
    Opcode = (Opcode == SystemZ::AHI ? SystemZ::ASI : SystemZ::AGSI);
    MachineInstr *BuiltMI =
        BuildMI(*InsertPt->getParent(), InsertPt, MI.getDebugLoc(), get(Opcode))
            .addFrameIndex(FrameIndex)
            .addImm(0)
            .addImm(MI.getOperand(2).getImm());
    transferDeadCC(&MI, BuiltMI);
    transferMIFlag(&MI, BuiltMI, MachineInstr::NoSWrap);
    return BuiltMI;
  }

  if ((Opcode == SystemZ::ALFI && OpNum == 0 &&
       isInt<8>((int32_t)MI.getOperand(2).getImm())) ||
      (Opcode == SystemZ::ALGFI && OpNum == 0 &&
       isInt<8>((int64_t)MI.getOperand(2).getImm()))) {
    // AL(G)FI %reg, CONST -> AL(G)SI %mem, CONST
    Opcode = (Opcode == SystemZ::ALFI ? SystemZ::ALSI : SystemZ::ALGSI);
    MachineInstr *BuiltMI =
        BuildMI(*InsertPt->getParent(), InsertPt, MI.getDebugLoc(), get(Opcode))
            .addFrameIndex(FrameIndex)
            .addImm(0)
            .addImm((int8_t)MI.getOperand(2).getImm());
    transferDeadCC(&MI, BuiltMI);
    return BuiltMI;
  }

  if ((Opcode == SystemZ::SLFI && OpNum == 0 &&
       isInt<8>((int32_t)-MI.getOperand(2).getImm())) ||
      (Opcode == SystemZ::SLGFI && OpNum == 0 &&
       isInt<8>((int64_t)-MI.getOperand(2).getImm()))) {
    // SL(G)FI %reg, CONST -> AL(G)SI %mem, -CONST
    Opcode = (Opcode == SystemZ::SLFI ? SystemZ::ALSI : SystemZ::ALGSI);
    MachineInstr *BuiltMI =
        BuildMI(*InsertPt->getParent(), InsertPt, MI.getDebugLoc(), get(Opcode))
            .addFrameIndex(FrameIndex)
            .addImm(0)
            .addImm((int8_t)-MI.getOperand(2).getImm());
    transferDeadCC(&MI, BuiltMI);
    return BuiltMI;
  }

  unsigned MemImmOpc = 0;
  switch (Opcode) {
  case SystemZ::LHIMux:
  case SystemZ::LHI:    MemImmOpc = SystemZ::MVHI;  break;
  case SystemZ::LGHI:   MemImmOpc = SystemZ::MVGHI; break;
  case SystemZ::CHIMux:
  case SystemZ::CHI:    MemImmOpc = SystemZ::CHSI;  break;
  case SystemZ::CGHI:   MemImmOpc = SystemZ::CGHSI; break;
  case SystemZ::CLFIMux:
  case SystemZ::CLFI:
    if (isUInt<16>(MI.getOperand(1).getImm()))
      MemImmOpc = SystemZ::CLFHSI;
    break;
  case SystemZ::CLGFI:
    if (isUInt<16>(MI.getOperand(1).getImm()))
      MemImmOpc = SystemZ::CLGHSI;
    break;
  default: break;
  }
  if (MemImmOpc)
    return BuildMI(*InsertPt->getParent(), InsertPt, MI.getDebugLoc(),
                   get(MemImmOpc))
               .addFrameIndex(FrameIndex)
               .addImm(0)
               .addImm(MI.getOperand(1).getImm());

  if (Opcode == SystemZ::LGDR || Opcode == SystemZ::LDGR) {
    bool Op0IsGPR = (Opcode == SystemZ::LGDR);
    bool Op1IsGPR = (Opcode == SystemZ::LDGR);
    // If we're spilling the destination of an LDGR or LGDR, store the
    // source register instead.
    if (OpNum == 0) {
      unsigned StoreOpcode = Op1IsGPR ? SystemZ::STG : SystemZ::STD;
      return BuildMI(*InsertPt->getParent(), InsertPt, MI.getDebugLoc(),
                     get(StoreOpcode))
          .add(MI.getOperand(1))
          .addFrameIndex(FrameIndex)
          .addImm(0)
          .addReg(0);
    }
    // If we're spilling the source of an LDGR or LGDR, load the
    // destination register instead.
    if (OpNum == 1) {
      unsigned LoadOpcode = Op0IsGPR ? SystemZ::LG : SystemZ::LD;
      return BuildMI(*InsertPt->getParent(), InsertPt, MI.getDebugLoc(),
                     get(LoadOpcode))
        .add(MI.getOperand(0))
        .addFrameIndex(FrameIndex)
        .addImm(0)
        .addReg(0);
    }
  }

  // Look for cases where the source of a simple store or the destination
  // of a simple load is being spilled.  Try to use MVC instead.
  //
  // Although MVC is in practice a fast choice in these cases, it is still
  // logically a bytewise copy.  This means that we cannot use it if the
  // load or store is volatile.  We also wouldn't be able to use MVC if
  // the two memories partially overlap, but that case cannot occur here,
  // because we know that one of the memories is a full frame index.
  //
  // For performance reasons, we also want to avoid using MVC if the addresses
  // might be equal.  We don't worry about that case here, because spill slot
  // coloring happens later, and because we have special code to remove
  // MVCs that turn out to be redundant.
  if (OpNum == 0 && MI.hasOneMemOperand()) {
    MachineMemOperand *MMO = *MI.memoperands_begin();
    if (MMO->getSize() == Size && !MMO->isVolatile() && !MMO->isAtomic()) {
      // Handle conversion of loads.
      if (isSimpleBD12Move(&MI, SystemZII::SimpleBDXLoad)) {
        return BuildMI(*InsertPt->getParent(), InsertPt, MI.getDebugLoc(),
                       get(SystemZ::MVC))
            .addFrameIndex(FrameIndex)
            .addImm(0)
            .addImm(Size)
            .add(MI.getOperand(1))
            .addImm(MI.getOperand(2).getImm())
            .addMemOperand(MMO);
      }
      // Handle conversion of stores.
      if (isSimpleBD12Move(&MI, SystemZII::SimpleBDXStore)) {
        return BuildMI(*InsertPt->getParent(), InsertPt, MI.getDebugLoc(),
                       get(SystemZ::MVC))
            .add(MI.getOperand(1))
            .addImm(MI.getOperand(2).getImm())
            .addImm(Size)
            .addFrameIndex(FrameIndex)
            .addImm(0)
            .addMemOperand(MMO);
      }
    }
  }

  // If the spilled operand is the final one or the instruction is
  // commutable, try to change <INSN>R into <INSN>.  Don't introduce a def of
  // CC if it is live and MI does not define it.
  unsigned NumOps = MI.getNumExplicitOperands();
  int MemOpcode = SystemZ::getMemOpcode(Opcode);
  if (MemOpcode == -1 ||
      (CCLiveAtMI && !MI.definesRegister(SystemZ::CC, /*TRI=*/nullptr) &&
       get(MemOpcode).hasImplicitDefOfPhysReg(SystemZ::CC)))
    return nullptr;

  // Check if all other vregs have a usable allocation in the case of vector
  // to FP conversion.
  const MCInstrDesc &MCID = MI.getDesc();
  for (unsigned I = 0, E = MCID.getNumOperands(); I != E; ++I) {
    const MCOperandInfo &MCOI = MCID.operands()[I];
    if (MCOI.OperandType != MCOI::OPERAND_REGISTER || I == OpNum)
      continue;
    const TargetRegisterClass *RC = TRI->getRegClass(MCOI.RegClass);
    if (RC == &SystemZ::VR32BitRegClass || RC == &SystemZ::VR64BitRegClass) {
      Register Reg = MI.getOperand(I).getReg();
      Register PhysReg = Reg.isVirtual()
                             ? (VRM ? Register(VRM->getPhys(Reg)) : Register())
                             : Reg;
      if (!PhysReg ||
          !(SystemZ::FP32BitRegClass.contains(PhysReg) ||
            SystemZ::FP64BitRegClass.contains(PhysReg) ||
            SystemZ::VF128BitRegClass.contains(PhysReg)))
        return nullptr;
    }
  }
  // Fused multiply and add/sub need to have the same dst and accumulator reg.
  bool FusedFPOp = (Opcode == SystemZ::WFMADB || Opcode == SystemZ::WFMASB ||
                    Opcode == SystemZ::WFMSDB || Opcode == SystemZ::WFMSSB);
  if (FusedFPOp) {
    Register DstReg = VRM->getPhys(MI.getOperand(0).getReg());
    Register AccReg = VRM->getPhys(MI.getOperand(3).getReg());
    if (OpNum == 0 || OpNum == 3 || DstReg != AccReg)
      return nullptr;
  }

  // Try to swap compare operands if possible.
  bool NeedsCommute = false;
  if ((MI.getOpcode() == SystemZ::CR || MI.getOpcode() == SystemZ::CGR ||
       MI.getOpcode() == SystemZ::CLR || MI.getOpcode() == SystemZ::CLGR ||
       MI.getOpcode() == SystemZ::WFCDB || MI.getOpcode() == SystemZ::WFCSB ||
       MI.getOpcode() == SystemZ::WFKDB || MI.getOpcode() == SystemZ::WFKSB) &&
      OpNum == 0 && prepareCompareSwapOperands(MI))
    NeedsCommute = true;

  bool CCOperands = false;
  if (MI.getOpcode() == SystemZ::LOCRMux || MI.getOpcode() == SystemZ::LOCGR ||
      MI.getOpcode() == SystemZ::SELRMux || MI.getOpcode() == SystemZ::SELGR) {
    assert(MI.getNumOperands() == 6 && NumOps == 5 &&
           "LOCR/SELR instruction operands corrupt?");
    NumOps -= 2;
    CCOperands = true;
  }

  // See if this is a 3-address instruction that is convertible to 2-address
  // and suitable for folding below.  Only try this with virtual registers
  // and a provided VRM (during regalloc).
  if (NumOps == 3 && SystemZ::getTargetMemOpcode(MemOpcode) != -1) {
    if (VRM == nullptr)
      return nullptr;
    else {
      Register DstReg = MI.getOperand(0).getReg();
      Register DstPhys =
          (DstReg.isVirtual() ? Register(VRM->getPhys(DstReg)) : DstReg);
      Register SrcReg = (OpNum == 2 ? MI.getOperand(1).getReg()
                                    : ((OpNum == 1 && MI.isCommutable())
                                           ? MI.getOperand(2).getReg()
                                           : Register()));
      if (DstPhys && !SystemZ::GRH32BitRegClass.contains(DstPhys) && SrcReg &&
          SrcReg.isVirtual() && DstPhys == VRM->getPhys(SrcReg))
        NeedsCommute = (OpNum == 1);
      else
        return nullptr;
    }
  }

  if ((OpNum == NumOps - 1) || NeedsCommute || FusedFPOp) {
    const MCInstrDesc &MemDesc = get(MemOpcode);
    uint64_t AccessBytes = SystemZII::getAccessSize(MemDesc.TSFlags);
    assert(AccessBytes != 0 && "Size of access should be known");
    assert(AccessBytes <= Size && "Access outside the frame index");
    uint64_t Offset = Size - AccessBytes;
    MachineInstrBuilder MIB = BuildMI(*InsertPt->getParent(), InsertPt,
                                      MI.getDebugLoc(), get(MemOpcode));
    if (MI.isCompare()) {
      assert(NumOps == 2 && "Expected 2 register operands for a compare.");
      MIB.add(MI.getOperand(NeedsCommute ? 1 : 0));
    }
    else if (FusedFPOp) {
      MIB.add(MI.getOperand(0));
      MIB.add(MI.getOperand(3));
      MIB.add(MI.getOperand(OpNum == 1 ? 2 : 1));
    }
    else {
      MIB.add(MI.getOperand(0));
      if (NeedsCommute)
        MIB.add(MI.getOperand(2));
      else
        for (unsigned I = 1; I < OpNum; ++I)
          MIB.add(MI.getOperand(I));
    }
    MIB.addFrameIndex(FrameIndex).addImm(Offset);
    if (MemDesc.TSFlags & SystemZII::HasIndex)
      MIB.addReg(0);
    if (CCOperands) {
      unsigned CCValid = MI.getOperand(NumOps).getImm();
      unsigned CCMask = MI.getOperand(NumOps + 1).getImm();
      MIB.addImm(CCValid);
      MIB.addImm(NeedsCommute ? CCMask ^ CCValid : CCMask);
    }
    if (MIB->definesRegister(SystemZ::CC, /*TRI=*/nullptr) &&
        (!MI.definesRegister(SystemZ::CC, /*TRI=*/nullptr) ||
         MI.registerDefIsDead(SystemZ::CC, /*TRI=*/nullptr))) {
      MIB->addRegisterDead(SystemZ::CC, TRI);
      if (CCLiveRange)
        CCLiveRange->createDeadDef(MISlot, LIS->getVNInfoAllocator());
    }
    // Constrain the register classes if converted from a vector opcode. The
    // allocated regs are in an FP reg-class per previous check above.
    for (const MachineOperand &MO : MIB->operands())
      if (MO.isReg() && MO.getReg().isVirtual()) {
        Register Reg = MO.getReg();
        if (MRI.getRegClass(Reg) == &SystemZ::VR32BitRegClass)
          MRI.setRegClass(Reg, &SystemZ::FP32BitRegClass);
        else if (MRI.getRegClass(Reg) == &SystemZ::VR64BitRegClass)
          MRI.setRegClass(Reg, &SystemZ::FP64BitRegClass);
        else if (MRI.getRegClass(Reg) == &SystemZ::VR128BitRegClass)
          MRI.setRegClass(Reg, &SystemZ::VF128BitRegClass);
      }

    transferDeadCC(&MI, MIB);
    transferMIFlag(&MI, MIB, MachineInstr::NoSWrap);
    transferMIFlag(&MI, MIB, MachineInstr::NoFPExcept);
    return MIB;
  }

  return nullptr;
}

MachineInstr *SystemZInstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
    MachineBasicBlock::iterator InsertPt, MachineInstr &LoadMI,
    LiveIntervals *LIS) const {
  MachineRegisterInfo *MRI = &MF.getRegInfo();
  MachineBasicBlock *MBB = MI.getParent();

  // For reassociable FP operations, any loads have been purposefully left
  // unfolded so that MachineCombiner can do its work on reg/reg
  // opcodes. After that, as many loads as possible are now folded.
  // TODO: This may be beneficial with other opcodes as well as machine-sink
  // can move loads close to their user in a different MBB, which the isel
  // matcher did not see.
  unsigned LoadOpc = 0;
  unsigned RegMemOpcode = 0;
  const TargetRegisterClass *FPRC = nullptr;
  RegMemOpcode = MI.getOpcode() == SystemZ::WFADB   ? SystemZ::ADB
                 : MI.getOpcode() == SystemZ::WFSDB ? SystemZ::SDB
                 : MI.getOpcode() == SystemZ::WFMDB ? SystemZ::MDB
                                                    : 0;
  if (RegMemOpcode) {
    LoadOpc = SystemZ::VL64;
    FPRC = &SystemZ::FP64BitRegClass;
  } else {
    RegMemOpcode = MI.getOpcode() == SystemZ::WFASB   ? SystemZ::AEB
                   : MI.getOpcode() == SystemZ::WFSSB ? SystemZ::SEB
                   : MI.getOpcode() == SystemZ::WFMSB ? SystemZ::MEEB
                                                      : 0;
    if (RegMemOpcode) {
      LoadOpc = SystemZ::VL32;
      FPRC = &SystemZ::FP32BitRegClass;
    }
  }
  if (!RegMemOpcode || LoadMI.getOpcode() != LoadOpc)
    return nullptr;

  // If RegMemOpcode clobbers CC, first make sure CC is not live at this point.
  if (get(RegMemOpcode).hasImplicitDefOfPhysReg(SystemZ::CC)) {
    assert(LoadMI.getParent() == MI.getParent() && "Assuming a local fold.");
    assert(LoadMI != InsertPt && "Assuming InsertPt not to be first in MBB.");
    for (MachineBasicBlock::iterator MII = std::prev(InsertPt);;
         --MII) {
      if (MII->definesRegister(SystemZ::CC, /*TRI=*/nullptr)) {
        if (!MII->registerDefIsDead(SystemZ::CC, /*TRI=*/nullptr))
          return nullptr;
        break;
      }
      if (MII == MBB->begin()) {
        if (MBB->isLiveIn(SystemZ::CC))
          return nullptr;
        break;
      }
    }
  }

  Register FoldAsLoadDefReg = LoadMI.getOperand(0).getReg();
  if (Ops.size() != 1 || FoldAsLoadDefReg != MI.getOperand(Ops[0]).getReg())
    return nullptr;
  Register DstReg = MI.getOperand(0).getReg();
  MachineOperand LHS = MI.getOperand(1);
  MachineOperand RHS = MI.getOperand(2);
  MachineOperand &RegMO = RHS.getReg() == FoldAsLoadDefReg ? LHS : RHS;
  if ((RegMemOpcode == SystemZ::SDB || RegMemOpcode == SystemZ::SEB) &&
      FoldAsLoadDefReg != RHS.getReg())
    return nullptr;

  MachineOperand &Base = LoadMI.getOperand(1);
  MachineOperand &Disp = LoadMI.getOperand(2);
  MachineOperand &Indx = LoadMI.getOperand(3);
  MachineInstrBuilder MIB =
      BuildMI(*MI.getParent(), InsertPt, MI.getDebugLoc(), get(RegMemOpcode), DstReg)
          .add(RegMO)
          .add(Base)
          .add(Disp)
          .add(Indx);
  MIB->addRegisterDead(SystemZ::CC, &RI);
  MRI->setRegClass(DstReg, FPRC);
  MRI->setRegClass(RegMO.getReg(), FPRC);
  transferMIFlag(&MI, MIB, MachineInstr::NoFPExcept);

  return MIB;
}

bool SystemZInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case SystemZ::L128:
    splitMove(MI, SystemZ::LG);
    return true;

  case SystemZ::ST128:
    splitMove(MI, SystemZ::STG);
    return true;

  case SystemZ::LX:
    splitMove(MI, SystemZ::LD);
    return true;

  case SystemZ::STX:
    splitMove(MI, SystemZ::STD);
    return true;

  case SystemZ::LBMux:
    expandRXYPseudo(MI, SystemZ::LB, SystemZ::LBH);
    return true;

  case SystemZ::LHMux:
    expandRXYPseudo(MI, SystemZ::LH, SystemZ::LHH);
    return true;

  case SystemZ::LLCRMux:
    expandZExtPseudo(MI, SystemZ::LLCR, 8);
    return true;

  case SystemZ::LLHRMux:
    expandZExtPseudo(MI, SystemZ::LLHR, 16);
    return true;

  case SystemZ::LLCMux:
    expandRXYPseudo(MI, SystemZ::LLC, SystemZ::LLCH);
    return true;

  case SystemZ::LLHMux:
    expandRXYPseudo(MI, SystemZ::LLH, SystemZ::LLHH);
    return true;

  case SystemZ::LMux:
    expandRXYPseudo(MI, SystemZ::L, SystemZ::LFH);
    return true;

  case SystemZ::LOCMux:
    expandLOCPseudo(MI, SystemZ::LOC, SystemZ::LOCFH);
    return true;

  case SystemZ::LOCHIMux:
    expandLOCPseudo(MI, SystemZ::LOCHI, SystemZ::LOCHHI);
    return true;

  case SystemZ::STCMux:
    expandRXYPseudo(MI, SystemZ::STC, SystemZ::STCH);
    return true;

  case SystemZ::STHMux:
    expandRXYPseudo(MI, SystemZ::STH, SystemZ::STHH);
    return true;

  case SystemZ::STMux:
    expandRXYPseudo(MI, SystemZ::ST, SystemZ::STFH);
    return true;

  case SystemZ::STOCMux:
    expandLOCPseudo(MI, SystemZ::STOC, SystemZ::STOCFH);
    return true;

  case SystemZ::LHIMux:
    expandRIPseudo(MI, SystemZ::LHI, SystemZ::IIHF, true);
    return true;

  case SystemZ::IIFMux:
    expandRIPseudo(MI, SystemZ::IILF, SystemZ::IIHF, false);
    return true;

  case SystemZ::IILMux:
    expandRIPseudo(MI, SystemZ::IILL, SystemZ::IIHL, false);
    return true;

  case SystemZ::IIHMux:
    expandRIPseudo(MI, SystemZ::IILH, SystemZ::IIHH, false);
    return true;

  case SystemZ::NIFMux:
    expandRIPseudo(MI, SystemZ::NILF, SystemZ::NIHF, false);
    return true;

  case SystemZ::NILMux:
    expandRIPseudo(MI, SystemZ::NILL, SystemZ::NIHL, false);
    return true;

  case SystemZ::NIHMux:
    expandRIPseudo(MI, SystemZ::NILH, SystemZ::NIHH, false);
    return true;

  case SystemZ::OIFMux:
    expandRIPseudo(MI, SystemZ::OILF, SystemZ::OIHF, false);
    return true;

  case SystemZ::OILMux:
    expandRIPseudo(MI, SystemZ::OILL, SystemZ::OIHL, false);
    return true;

  case SystemZ::OIHMux:
    expandRIPseudo(MI, SystemZ::OILH, SystemZ::OIHH, false);
    return true;

  case SystemZ::XIFMux:
    expandRIPseudo(MI, SystemZ::XILF, SystemZ::XIHF, false);
    return true;

  case SystemZ::TMLMux:
    expandRIPseudo(MI, SystemZ::TMLL, SystemZ::TMHL, false);
    return true;

  case SystemZ::TMHMux:
    expandRIPseudo(MI, SystemZ::TMLH, SystemZ::TMHH, false);
    return true;

  case SystemZ::AHIMux:
    expandRIPseudo(MI, SystemZ::AHI, SystemZ::AIH, false);
    return true;

  case SystemZ::AHIMuxK:
    expandRIEPseudo(MI, SystemZ::AHI, SystemZ::AHIK, SystemZ::AIH);
    return true;

  case SystemZ::AFIMux:
    expandRIPseudo(MI, SystemZ::AFI, SystemZ::AIH, false);
    return true;

  case SystemZ::CHIMux:
    expandRIPseudo(MI, SystemZ::CHI, SystemZ::CIH, false);
    return true;

  case SystemZ::CFIMux:
    expandRIPseudo(MI, SystemZ::CFI, SystemZ::CIH, false);
    return true;

  case SystemZ::CLFIMux:
    expandRIPseudo(MI, SystemZ::CLFI, SystemZ::CLIH, false);
    return true;

  case SystemZ::CMux:
    expandRXYPseudo(MI, SystemZ::C, SystemZ::CHF);
    return true;

  case SystemZ::CLMux:
    expandRXYPseudo(MI, SystemZ::CL, SystemZ::CLHF);
    return true;

  case SystemZ::RISBMux: {
    bool DestIsHigh = SystemZ::isHighReg(MI.getOperand(0).getReg());
    bool SrcIsHigh = SystemZ::isHighReg(MI.getOperand(2).getReg());
    if (SrcIsHigh == DestIsHigh)
      MI.setDesc(get(DestIsHigh ? SystemZ::RISBHH : SystemZ::RISBLL));
    else {
      MI.setDesc(get(DestIsHigh ? SystemZ::RISBHL : SystemZ::RISBLH));
      MI.getOperand(5).setImm(MI.getOperand(5).getImm() ^ 32);
    }
    return true;
  }

  case SystemZ::ADJDYNALLOC:
    splitAdjDynAlloc(MI);
    return true;

  case TargetOpcode::LOAD_STACK_GUARD:
    expandLoadStackGuard(&MI);
    return true;

  default:
    return false;
  }
}

unsigned SystemZInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  if (MI.isInlineAsm()) {
    const MachineFunction *MF = MI.getParent()->getParent();
    const char *AsmStr = MI.getOperand(0).getSymbolName();
    return getInlineAsmLength(AsmStr, *MF->getTarget().getMCAsmInfo());
  }
  else if (MI.getOpcode() == SystemZ::PATCHPOINT)
    return PatchPointOpers(&MI).getNumPatchBytes();
  else if (MI.getOpcode() == SystemZ::STACKMAP)
    return MI.getOperand(1).getImm();
  else if (MI.getOpcode() == SystemZ::FENTRY_CALL)
    return 6;

  return MI.getDesc().getSize();
}

SystemZII::Branch
SystemZInstrInfo::getBranchInfo(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case SystemZ::BR:
  case SystemZ::BI:
  case SystemZ::J:
  case SystemZ::JG:
    return SystemZII::Branch(SystemZII::BranchNormal, SystemZ::CCMASK_ANY,
                             SystemZ::CCMASK_ANY, &MI.getOperand(0));

  case SystemZ::BRC:
  case SystemZ::BRCL:
    return SystemZII::Branch(SystemZII::BranchNormal, MI.getOperand(0).getImm(),
                             MI.getOperand(1).getImm(), &MI.getOperand(2));

  case SystemZ::BRCT:
  case SystemZ::BRCTH:
    return SystemZII::Branch(SystemZII::BranchCT, SystemZ::CCMASK_ICMP,
                             SystemZ::CCMASK_CMP_NE, &MI.getOperand(2));

  case SystemZ::BRCTG:
    return SystemZII::Branch(SystemZII::BranchCTG, SystemZ::CCMASK_ICMP,
                             SystemZ::CCMASK_CMP_NE, &MI.getOperand(2));

  case SystemZ::CIJ:
  case SystemZ::CRJ:
    return SystemZII::Branch(SystemZII::BranchC, SystemZ::CCMASK_ICMP,
                             MI.getOperand(2).getImm(), &MI.getOperand(3));

  case SystemZ::CLIJ:
  case SystemZ::CLRJ:
    return SystemZII::Branch(SystemZII::BranchCL, SystemZ::CCMASK_ICMP,
                             MI.getOperand(2).getImm(), &MI.getOperand(3));

  case SystemZ::CGIJ:
  case SystemZ::CGRJ:
    return SystemZII::Branch(SystemZII::BranchCG, SystemZ::CCMASK_ICMP,
                             MI.getOperand(2).getImm(), &MI.getOperand(3));

  case SystemZ::CLGIJ:
  case SystemZ::CLGRJ:
    return SystemZII::Branch(SystemZII::BranchCLG, SystemZ::CCMASK_ICMP,
                             MI.getOperand(2).getImm(), &MI.getOperand(3));

  case SystemZ::INLINEASM_BR:
    // Don't try to analyze asm goto, so pass nullptr as branch target argument.
    return SystemZII::Branch(SystemZII::AsmGoto, 0, 0, nullptr);

  default:
    llvm_unreachable("Unrecognized branch opcode");
  }
}

void SystemZInstrInfo::getLoadStoreOpcodes(const TargetRegisterClass *RC,
                                           unsigned &LoadOpcode,
                                           unsigned &StoreOpcode) const {
  if (RC == &SystemZ::GR32BitRegClass || RC == &SystemZ::ADDR32BitRegClass) {
    LoadOpcode = SystemZ::L;
    StoreOpcode = SystemZ::ST;
  } else if (RC == &SystemZ::GRH32BitRegClass) {
    LoadOpcode = SystemZ::LFH;
    StoreOpcode = SystemZ::STFH;
  } else if (RC == &SystemZ::GRX32BitRegClass) {
    LoadOpcode = SystemZ::LMux;
    StoreOpcode = SystemZ::STMux;
  } else if (RC == &SystemZ::GR64BitRegClass ||
             RC == &SystemZ::ADDR64BitRegClass) {
    LoadOpcode = SystemZ::LG;
    StoreOpcode = SystemZ::STG;
  } else if (RC == &SystemZ::GR128BitRegClass ||
             RC == &SystemZ::ADDR128BitRegClass) {
    LoadOpcode = SystemZ::L128;
    StoreOpcode = SystemZ::ST128;
  } else if (RC == &SystemZ::FP32BitRegClass) {
    LoadOpcode = SystemZ::LE;
    StoreOpcode = SystemZ::STE;
  } else if (RC == &SystemZ::FP64BitRegClass) {
    LoadOpcode = SystemZ::LD;
    StoreOpcode = SystemZ::STD;
  } else if (RC == &SystemZ::FP128BitRegClass) {
    LoadOpcode = SystemZ::LX;
    StoreOpcode = SystemZ::STX;
  } else if (RC == &SystemZ::VR32BitRegClass) {
    LoadOpcode = SystemZ::VL32;
    StoreOpcode = SystemZ::VST32;
  } else if (RC == &SystemZ::VR64BitRegClass) {
    LoadOpcode = SystemZ::VL64;
    StoreOpcode = SystemZ::VST64;
  } else if (RC == &SystemZ::VF128BitRegClass ||
             RC == &SystemZ::VR128BitRegClass) {
    LoadOpcode = SystemZ::VL;
    StoreOpcode = SystemZ::VST;
  } else
    llvm_unreachable("Unsupported regclass to load or store");
}

unsigned SystemZInstrInfo::getOpcodeForOffset(unsigned Opcode,
                                              int64_t Offset,
                                              const MachineInstr *MI) const {
  const MCInstrDesc &MCID = get(Opcode);
  int64_t Offset2 = (MCID.TSFlags & SystemZII::Is128Bit ? Offset + 8 : Offset);
  if (isUInt<12>(Offset) && isUInt<12>(Offset2)) {
    // Get the instruction to use for unsigned 12-bit displacements.
    int Disp12Opcode = SystemZ::getDisp12Opcode(Opcode);
    if (Disp12Opcode >= 0)
      return Disp12Opcode;

    // All address-related instructions can use unsigned 12-bit
    // displacements.
    return Opcode;
  }
  if (isInt<20>(Offset) && isInt<20>(Offset2)) {
    // Get the instruction to use for signed 20-bit displacements.
    int Disp20Opcode = SystemZ::getDisp20Opcode(Opcode);
    if (Disp20Opcode >= 0)
      return Disp20Opcode;

    // Check whether Opcode allows signed 20-bit displacements.
    if (MCID.TSFlags & SystemZII::Has20BitOffset)
      return Opcode;

    // If a VR32/VR64 reg ended up in an FP register, use the FP opcode.
    if (MI && MI->getOperand(0).isReg()) {
      Register Reg = MI->getOperand(0).getReg();
      if (Reg.isPhysical() && SystemZMC::getFirstReg(Reg) < 16) {
        switch (Opcode) {
        case SystemZ::VL32:
          return SystemZ::LEY;
        case SystemZ::VST32:
          return SystemZ::STEY;
        case SystemZ::VL64:
          return SystemZ::LDY;
        case SystemZ::VST64:
          return SystemZ::STDY;
        default: break;
        }
      }
    }
  }
  return 0;
}

bool SystemZInstrInfo::hasDisplacementPairInsn(unsigned Opcode) const {
  const MCInstrDesc &MCID = get(Opcode);
  if (MCID.TSFlags & SystemZII::Has20BitOffset)
    return SystemZ::getDisp12Opcode(Opcode) >= 0;
  return SystemZ::getDisp20Opcode(Opcode) >= 0;
}

unsigned SystemZInstrInfo::getLoadAndTest(unsigned Opcode) const {
  switch (Opcode) {
  case SystemZ::L:      return SystemZ::LT;
  case SystemZ::LY:     return SystemZ::LT;
  case SystemZ::LG:     return SystemZ::LTG;
  case SystemZ::LGF:    return SystemZ::LTGF;
  case SystemZ::LR:     return SystemZ::LTR;
  case SystemZ::LGFR:   return SystemZ::LTGFR;
  case SystemZ::LGR:    return SystemZ::LTGR;
  case SystemZ::LCDFR:  return SystemZ::LCDBR;
  case SystemZ::LPDFR:  return SystemZ::LPDBR;
  case SystemZ::LNDFR:  return SystemZ::LNDBR;
  case SystemZ::LCDFR_32:  return SystemZ::LCEBR;
  case SystemZ::LPDFR_32:  return SystemZ::LPEBR;
  case SystemZ::LNDFR_32:  return SystemZ::LNEBR;
  // On zEC12 we prefer to use RISBGN.  But if there is a chance to
  // actually use the condition code, we may turn it back into RISGB.
  // Note that RISBG is not really a "load-and-test" instruction,
  // but sets the same condition code values, so is OK to use here.
  case SystemZ::RISBGN: return SystemZ::RISBG;
  default:              return 0;
  }
}

bool SystemZInstrInfo::isRxSBGMask(uint64_t Mask, unsigned BitSize,
                                   unsigned &Start, unsigned &End) const {
  // Reject trivial all-zero masks.
  Mask &= allOnes(BitSize);
  if (Mask == 0)
    return false;

  // Handle the 1+0+ or 0+1+0* cases.  Start then specifies the index of
  // the msb and End specifies the index of the lsb.
  unsigned LSB, Length;
  if (isShiftedMask_64(Mask, LSB, Length)) {
    Start = 63 - (LSB + Length - 1);
    End = 63 - LSB;
    return true;
  }

  // Handle the wrap-around 1+0+1+ cases.  Start then specifies the msb
  // of the low 1s and End specifies the lsb of the high 1s.
  if (isShiftedMask_64(Mask ^ allOnes(BitSize), LSB, Length)) {
    assert(LSB > 0 && "Bottom bit must be set");
    assert(LSB + Length < BitSize && "Top bit must be set");
    Start = 63 - (LSB - 1);
    End = 63 - (LSB + Length);
    return true;
  }

  return false;
}

unsigned SystemZInstrInfo::getFusedCompare(unsigned Opcode,
                                           SystemZII::FusedCompareType Type,
                                           const MachineInstr *MI) const {
  switch (Opcode) {
  case SystemZ::CHI:
  case SystemZ::CGHI:
    if (!(MI && isInt<8>(MI->getOperand(1).getImm())))
      return 0;
    break;
  case SystemZ::CLFI:
  case SystemZ::CLGFI:
    if (!(MI && isUInt<8>(MI->getOperand(1).getImm())))
      return 0;
    break;
  case SystemZ::CL:
  case SystemZ::CLG:
    if (!STI.hasMiscellaneousExtensions())
      return 0;
    if (!(MI && MI->getOperand(3).getReg() == 0))
      return 0;
    break;
  }
  switch (Type) {
  case SystemZII::CompareAndBranch:
    switch (Opcode) {
    case SystemZ::CR:
      return SystemZ::CRJ;
    case SystemZ::CGR:
      return SystemZ::CGRJ;
    case SystemZ::CHI:
      return SystemZ::CIJ;
    case SystemZ::CGHI:
      return SystemZ::CGIJ;
    case SystemZ::CLR:
      return SystemZ::CLRJ;
    case SystemZ::CLGR:
      return SystemZ::CLGRJ;
    case SystemZ::CLFI:
      return SystemZ::CLIJ;
    case SystemZ::CLGFI:
      return SystemZ::CLGIJ;
    default:
      return 0;
    }
  case SystemZII::CompareAndReturn:
    switch (Opcode) {
    case SystemZ::CR:
      return SystemZ::CRBReturn;
    case SystemZ::CGR:
      return SystemZ::CGRBReturn;
    case SystemZ::CHI:
      return SystemZ::CIBReturn;
    case SystemZ::CGHI:
      return SystemZ::CGIBReturn;
    case SystemZ::CLR:
      return SystemZ::CLRBReturn;
    case SystemZ::CLGR:
      return SystemZ::CLGRBReturn;
    case SystemZ::CLFI:
      return SystemZ::CLIBReturn;
    case SystemZ::CLGFI:
      return SystemZ::CLGIBReturn;
    default:
      return 0;
    }
  case SystemZII::CompareAndSibcall:
    switch (Opcode) {
    case SystemZ::CR:
      return SystemZ::CRBCall;
    case SystemZ::CGR:
      return SystemZ::CGRBCall;
    case SystemZ::CHI:
      return SystemZ::CIBCall;
    case SystemZ::CGHI:
      return SystemZ::CGIBCall;
    case SystemZ::CLR:
      return SystemZ::CLRBCall;
    case SystemZ::CLGR:
      return SystemZ::CLGRBCall;
    case SystemZ::CLFI:
      return SystemZ::CLIBCall;
    case SystemZ::CLGFI:
      return SystemZ::CLGIBCall;
    default:
      return 0;
    }
  case SystemZII::CompareAndTrap:
    switch (Opcode) {
    case SystemZ::CR:
      return SystemZ::CRT;
    case SystemZ::CGR:
      return SystemZ::CGRT;
    case SystemZ::CHI:
      return SystemZ::CIT;
    case SystemZ::CGHI:
      return SystemZ::CGIT;
    case SystemZ::CLR:
      return SystemZ::CLRT;
    case SystemZ::CLGR:
      return SystemZ::CLGRT;
    case SystemZ::CLFI:
      return SystemZ::CLFIT;
    case SystemZ::CLGFI:
      return SystemZ::CLGIT;
    case SystemZ::CL:
      return SystemZ::CLT;
    case SystemZ::CLG:
      return SystemZ::CLGT;
    default:
      return 0;
    }
  }
  return 0;
}

bool SystemZInstrInfo::
prepareCompareSwapOperands(MachineBasicBlock::iterator const MBBI) const {
  assert(MBBI->isCompare() && MBBI->getOperand(0).isReg() &&
         MBBI->getOperand(1).isReg() && !MBBI->mayLoad() &&
         "Not a compare reg/reg.");

  MachineBasicBlock *MBB = MBBI->getParent();
  bool CCLive = true;
  SmallVector<MachineInstr *, 4> CCUsers;
  for (MachineInstr &MI : llvm::make_range(std::next(MBBI), MBB->end())) {
    if (MI.readsRegister(SystemZ::CC, /*TRI=*/nullptr)) {
      unsigned Flags = MI.getDesc().TSFlags;
      if ((Flags & SystemZII::CCMaskFirst) || (Flags & SystemZII::CCMaskLast))
        CCUsers.push_back(&MI);
      else
        return false;
    }
    if (MI.definesRegister(SystemZ::CC, /*TRI=*/nullptr)) {
      CCLive = false;
      break;
    }
  }
  if (CCLive) {
    LiveRegUnits LiveRegs(*MBB->getParent()->getSubtarget().getRegisterInfo());
    LiveRegs.addLiveOuts(*MBB);
    if (!LiveRegs.available(SystemZ::CC))
      return false;
  }

  // Update all CC users.
  for (unsigned Idx = 0; Idx < CCUsers.size(); ++Idx) {
    unsigned Flags = CCUsers[Idx]->getDesc().TSFlags;
    unsigned FirstOpNum = ((Flags & SystemZII::CCMaskFirst) ?
                           0 : CCUsers[Idx]->getNumExplicitOperands() - 2);
    MachineOperand &CCMaskMO = CCUsers[Idx]->getOperand(FirstOpNum + 1);
    unsigned NewCCMask = SystemZ::reverseCCMask(CCMaskMO.getImm());
    CCMaskMO.setImm(NewCCMask);
  }

  return true;
}

unsigned SystemZ::reverseCCMask(unsigned CCMask) {
  return ((CCMask & SystemZ::CCMASK_CMP_EQ) |
          ((CCMask & SystemZ::CCMASK_CMP_GT) ? SystemZ::CCMASK_CMP_LT : 0) |
          ((CCMask & SystemZ::CCMASK_CMP_LT) ? SystemZ::CCMASK_CMP_GT : 0) |
          (CCMask & SystemZ::CCMASK_CMP_UO));
}

MachineBasicBlock *SystemZ::emitBlockAfter(MachineBasicBlock *MBB) {
  MachineFunction &MF = *MBB->getParent();
  MachineBasicBlock *NewMBB = MF.CreateMachineBasicBlock(MBB->getBasicBlock());
  MF.insert(std::next(MachineFunction::iterator(MBB)), NewMBB);
  return NewMBB;
}

MachineBasicBlock *SystemZ::splitBlockAfter(MachineBasicBlock::iterator MI,
                                            MachineBasicBlock *MBB) {
  MachineBasicBlock *NewMBB = emitBlockAfter(MBB);
  NewMBB->splice(NewMBB->begin(), MBB,
                 std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  NewMBB->transferSuccessorsAndUpdatePHIs(MBB);
  return NewMBB;
}

MachineBasicBlock *SystemZ::splitBlockBefore(MachineBasicBlock::iterator MI,
                                             MachineBasicBlock *MBB) {
  MachineBasicBlock *NewMBB = emitBlockAfter(MBB);
  NewMBB->splice(NewMBB->begin(), MBB, MI, MBB->end());
  NewMBB->transferSuccessorsAndUpdatePHIs(MBB);
  return NewMBB;
}

unsigned SystemZInstrInfo::getLoadAndTrap(unsigned Opcode) const {
  if (!STI.hasLoadAndTrap())
    return 0;
  switch (Opcode) {
  case SystemZ::L:
  case SystemZ::LY:
    return SystemZ::LAT;
  case SystemZ::LG:
    return SystemZ::LGAT;
  case SystemZ::LFH:
    return SystemZ::LFHAT;
  case SystemZ::LLGF:
    return SystemZ::LLGFAT;
  case SystemZ::LLGT:
    return SystemZ::LLGTAT;
  }
  return 0;
}

void SystemZInstrInfo::loadImmediate(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI,
                                     unsigned Reg, uint64_t Value) const {
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  unsigned Opcode = 0;
  if (isInt<16>(Value))
    Opcode = SystemZ::LGHI;
  else if (SystemZ::isImmLL(Value))
    Opcode = SystemZ::LLILL;
  else if (SystemZ::isImmLH(Value)) {
    Opcode = SystemZ::LLILH;
    Value >>= 16;
  }
  else if (isInt<32>(Value))
    Opcode = SystemZ::LGFI;
  if (Opcode) {
    BuildMI(MBB, MBBI, DL, get(Opcode), Reg).addImm(Value);
    return;
  }

  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  assert (MRI.isSSA() &&  "Huge values only handled before reg-alloc .");
  Register Reg0 = MRI.createVirtualRegister(&SystemZ::GR64BitRegClass);
  Register Reg1 = MRI.createVirtualRegister(&SystemZ::GR64BitRegClass);
  BuildMI(MBB, MBBI, DL, get(SystemZ::IMPLICIT_DEF), Reg0);
  BuildMI(MBB, MBBI, DL, get(SystemZ::IIHF64), Reg1)
    .addReg(Reg0).addImm(Value >> 32);
  BuildMI(MBB, MBBI, DL, get(SystemZ::IILF64), Reg)
    .addReg(Reg1).addImm(Value & ((uint64_t(1) << 32) - 1));
}

bool SystemZInstrInfo::verifyInstruction(const MachineInstr &MI,
                                         StringRef &ErrInfo) const {
  const MCInstrDesc &MCID = MI.getDesc();
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    if (I >= MCID.getNumOperands())
      break;
    const MachineOperand &Op = MI.getOperand(I);
    const MCOperandInfo &MCOI = MCID.operands()[I];
    // Addressing modes have register and immediate operands. Op should be a
    // register (or frame index) operand if MCOI.RegClass contains a valid
    // register class, or an immediate otherwise.
    if (MCOI.OperandType == MCOI::OPERAND_MEMORY &&
        ((MCOI.RegClass != -1 && !Op.isReg() && !Op.isFI()) ||
         (MCOI.RegClass == -1 && !Op.isImm()))) {
      ErrInfo = "Addressing mode operands corrupt!";
      return false;
    }
  }

  return true;
}

bool SystemZInstrInfo::
areMemAccessesTriviallyDisjoint(const MachineInstr &MIa,
                                const MachineInstr &MIb) const {

  if (!MIa.hasOneMemOperand() || !MIb.hasOneMemOperand())
    return false;

  // If mem-operands show that the same address Value is used by both
  // instructions, check for non-overlapping offsets and widths. Not
  // sure if a register based analysis would be an improvement...

  MachineMemOperand *MMOa = *MIa.memoperands_begin();
  MachineMemOperand *MMOb = *MIb.memoperands_begin();
  const Value *VALa = MMOa->getValue();
  const Value *VALb = MMOb->getValue();
  bool SameVal = (VALa && VALb && (VALa == VALb));
  if (!SameVal) {
    const PseudoSourceValue *PSVa = MMOa->getPseudoValue();
    const PseudoSourceValue *PSVb = MMOb->getPseudoValue();
    if (PSVa && PSVb && (PSVa == PSVb))
      SameVal = true;
  }
  if (SameVal) {
    int OffsetA = MMOa->getOffset(), OffsetB = MMOb->getOffset();
    LocationSize WidthA = MMOa->getSize(), WidthB = MMOb->getSize();
    int LowOffset = OffsetA < OffsetB ? OffsetA : OffsetB;
    int HighOffset = OffsetA < OffsetB ? OffsetB : OffsetA;
    LocationSize LowWidth = (LowOffset == OffsetA) ? WidthA : WidthB;
    if (LowWidth.hasValue() &&
        LowOffset + (int)LowWidth.getValue() <= HighOffset)
      return true;
  }

  return false;
}

bool SystemZInstrInfo::getConstValDefinedInReg(const MachineInstr &MI,
                                               const Register Reg,
                                               int64_t &ImmVal) const {

  if (MI.getOpcode() == SystemZ::VGBM && Reg == MI.getOperand(0).getReg()) {
    ImmVal = MI.getOperand(1).getImm();
    // TODO: Handle non-0 values
    return ImmVal == 0;
  }

  return false;
}
