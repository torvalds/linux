//===-- CSKYInstrInfo.h - CSKY Instruction Information --------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the CSKY implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "CSKYInstrInfo.h"
#include "CSKYConstantPoolValue.h"
#include "CSKYMachineFunctionInfo.h"
#include "CSKYTargetMachine.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/MC/MCContext.h"

#define DEBUG_TYPE "csky-instr-info"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "CSKYGenInstrInfo.inc"

CSKYInstrInfo::CSKYInstrInfo(CSKYSubtarget &STI)
    : CSKYGenInstrInfo(CSKY::ADJCALLSTACKDOWN, CSKY::ADJCALLSTACKUP), STI(STI) {
  v2sf = STI.hasFPUv2SingleFloat();
  v2df = STI.hasFPUv2DoubleFloat();
  v3sf = STI.hasFPUv3SingleFloat();
  v3df = STI.hasFPUv3DoubleFloat();
}

static void parseCondBranch(MachineInstr &LastInst, MachineBasicBlock *&Target,
                            SmallVectorImpl<MachineOperand> &Cond) {
  // Block ends with fall-through condbranch.
  assert(LastInst.getDesc().isConditionalBranch() &&
         "Unknown conditional branch");
  Target = LastInst.getOperand(1).getMBB();
  Cond.push_back(MachineOperand::CreateImm(LastInst.getOpcode()));
  Cond.push_back(LastInst.getOperand(0));
}

bool CSKYInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool AllowModify) const {
  TBB = FBB = nullptr;
  Cond.clear();

  // If the block has no terminators, it just falls into the block after it.
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end() || !isUnpredicatedTerminator(*I))
    return false;

  // Count the number of terminators and find the first unconditional or
  // indirect branch.
  MachineBasicBlock::iterator FirstUncondOrIndirectBr = MBB.end();
  int NumTerminators = 0;
  for (auto J = I.getReverse(); J != MBB.rend() && isUnpredicatedTerminator(*J);
       J++) {
    NumTerminators++;
    if (J->getDesc().isUnconditionalBranch() ||
        J->getDesc().isIndirectBranch()) {
      FirstUncondOrIndirectBr = J.getReverse();
    }
  }

  // If AllowModify is true, we can erase any terminators after
  // FirstUncondOrIndirectBR.
  if (AllowModify && FirstUncondOrIndirectBr != MBB.end()) {
    while (std::next(FirstUncondOrIndirectBr) != MBB.end()) {
      std::next(FirstUncondOrIndirectBr)->eraseFromParent();
      NumTerminators--;
    }
    I = FirstUncondOrIndirectBr;
  }

  // We can't handle blocks that end in an indirect branch.
  if (I->getDesc().isIndirectBranch())
    return true;

  // We can't handle blocks with more than 2 terminators.
  if (NumTerminators > 2)
    return true;

  // Handle a single unconditional branch.
  if (NumTerminators == 1 && I->getDesc().isUnconditionalBranch()) {
    TBB = getBranchDestBlock(*I);
    return false;
  }

  // Handle a single conditional branch.
  if (NumTerminators == 1 && I->getDesc().isConditionalBranch()) {
    parseCondBranch(*I, TBB, Cond);
    return false;
  }

  // Handle a conditional branch followed by an unconditional branch.
  if (NumTerminators == 2 && std::prev(I)->getDesc().isConditionalBranch() &&
      I->getDesc().isUnconditionalBranch()) {
    parseCondBranch(*std::prev(I), TBB, Cond);
    FBB = getBranchDestBlock(*I);
    return false;
  }

  // Otherwise, we can't handle this.
  return true;
}

unsigned CSKYInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                     int *BytesRemoved) const {
  if (BytesRemoved)
    *BytesRemoved = 0;
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return 0;

  if (!I->getDesc().isUnconditionalBranch() &&
      !I->getDesc().isConditionalBranch())
    return 0;

  // Remove the branch.
  if (BytesRemoved)
    *BytesRemoved += getInstSizeInBytes(*I);
  I->eraseFromParent();

  I = MBB.end();

  if (I == MBB.begin())
    return 1;
  --I;
  if (!I->getDesc().isConditionalBranch())
    return 1;

  // Remove the branch.
  if (BytesRemoved)
    *BytesRemoved += getInstSizeInBytes(*I);
  I->eraseFromParent();
  return 2;
}

MachineBasicBlock *
CSKYInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  assert(MI.getDesc().isBranch() && "Unexpected opcode!");
  // The branch target is always the last operand.
  int NumOp = MI.getNumExplicitOperands();
  assert(MI.getOperand(NumOp - 1).isMBB() && "Expected MBB!");
  return MI.getOperand(NumOp - 1).getMBB();
}

unsigned CSKYInstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
  if (BytesAdded)
    *BytesAdded = 0;

  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 2 || Cond.size() == 0) &&
         "CSKY branch conditions have two components!");

  // Unconditional branch.
  if (Cond.empty()) {
    MachineInstr &MI = *BuildMI(&MBB, DL, get(CSKY::BR32)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    return 1;
  }

  // Either a one or two-way conditional branch.
  unsigned Opc = Cond[0].getImm();
  MachineInstr &CondMI = *BuildMI(&MBB, DL, get(Opc)).add(Cond[1]).addMBB(TBB);
  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(CondMI);

  // One-way conditional branch.
  if (!FBB)
    return 1;

  // Two-way conditional branch.
  MachineInstr &MI = *BuildMI(&MBB, DL, get(CSKY::BR32)).addMBB(FBB);
  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(MI);
  return 2;
}

static unsigned getOppositeBranchOpc(unsigned Opcode) {
  switch (Opcode) {
  default:
    llvm_unreachable("Unknown conditional branch!");
  case CSKY::BT32:
    return CSKY::BF32;
  case CSKY::BT16:
    return CSKY::BF16;
  case CSKY::BF32:
    return CSKY::BT32;
  case CSKY::BF16:
    return CSKY::BT16;
  case CSKY::BHZ32:
    return CSKY::BLSZ32;
  case CSKY::BHSZ32:
    return CSKY::BLZ32;
  case CSKY::BLZ32:
    return CSKY::BHSZ32;
  case CSKY::BLSZ32:
    return CSKY::BHZ32;
  case CSKY::BNEZ32:
    return CSKY::BEZ32;
  case CSKY::BEZ32:
    return CSKY::BNEZ32;
  }
}

bool CSKYInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert((Cond.size() == 2) && "Invalid branch condition!");
  Cond[0].setImm(getOppositeBranchOpc(Cond[0].getImm()));
  return false;
}

Register CSKYInstrInfo::movImm(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI,
                               const DebugLoc &DL, uint64_t Val,
                               MachineInstr::MIFlag Flag) const {
  if (!isInt<32>(Val))
    report_fatal_error("Should only materialize 32-bit constants.");

  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  Register DstReg;
  if (STI.hasE2()) {
    DstReg = MRI.createVirtualRegister(&CSKY::GPRRegClass);

    if (isUInt<16>(Val)) {
      BuildMI(MBB, MBBI, DL, get(CSKY::MOVI32), DstReg)
          .addImm(Val & 0xFFFF)
          .setMIFlags(Flag);
    } else if (isShiftedUInt<16, 16>(Val)) {
      BuildMI(MBB, MBBI, DL, get(CSKY::MOVIH32), DstReg)
          .addImm((Val >> 16) & 0xFFFF)
          .setMIFlags(Flag);
    } else {
      BuildMI(MBB, MBBI, DL, get(CSKY::MOVIH32), DstReg)
          .addImm((Val >> 16) & 0xFFFF)
          .setMIFlags(Flag);
      BuildMI(MBB, MBBI, DL, get(CSKY::ORI32), DstReg)
          .addReg(DstReg)
          .addImm(Val & 0xFFFF)
          .setMIFlags(Flag);
    }

  } else {
    DstReg = MRI.createVirtualRegister(&CSKY::mGPRRegClass);
    if (isUInt<8>(Val)) {
      BuildMI(MBB, MBBI, DL, get(CSKY::MOVI16), DstReg)
          .addImm(Val & 0xFF)
          .setMIFlags(Flag);
    } else if (isUInt<16>(Val)) {
      BuildMI(MBB, MBBI, DL, get(CSKY::MOVI16), DstReg)
          .addImm((Val >> 8) & 0xFF)
          .setMIFlags(Flag);
      BuildMI(MBB, MBBI, DL, get(CSKY::LSLI16), DstReg)
          .addReg(DstReg)
          .addImm(8)
          .setMIFlags(Flag);
      if ((Val & 0xFF) != 0)
        BuildMI(MBB, MBBI, DL, get(CSKY::ADDI16), DstReg)
            .addReg(DstReg)
            .addImm(Val & 0xFF)
            .setMIFlags(Flag);
    } else if (isUInt<24>(Val)) {
      BuildMI(MBB, MBBI, DL, get(CSKY::MOVI16), DstReg)
          .addImm((Val >> 16) & 0xFF)
          .setMIFlags(Flag);
      BuildMI(MBB, MBBI, DL, get(CSKY::LSLI16), DstReg)
          .addReg(DstReg)
          .addImm(8)
          .setMIFlags(Flag);
      if (((Val >> 8) & 0xFF) != 0)
        BuildMI(MBB, MBBI, DL, get(CSKY::ADDI16), DstReg)
            .addReg(DstReg)
            .addImm((Val >> 8) & 0xFF)
            .setMIFlags(Flag);
      BuildMI(MBB, MBBI, DL, get(CSKY::LSLI16), DstReg)
          .addReg(DstReg)
          .addImm(8)
          .setMIFlags(Flag);
      if ((Val & 0xFF) != 0)
        BuildMI(MBB, MBBI, DL, get(CSKY::ADDI16), DstReg)
            .addReg(DstReg)
            .addImm(Val & 0xFF)
            .setMIFlags(Flag);
    } else {
      BuildMI(MBB, MBBI, DL, get(CSKY::MOVI16), DstReg)
          .addImm((Val >> 24) & 0xFF)
          .setMIFlags(Flag);
      BuildMI(MBB, MBBI, DL, get(CSKY::LSLI16), DstReg)
          .addReg(DstReg)
          .addImm(8)
          .setMIFlags(Flag);
      if (((Val >> 16) & 0xFF) != 0)
        BuildMI(MBB, MBBI, DL, get(CSKY::ADDI16), DstReg)
            .addReg(DstReg)
            .addImm((Val >> 16) & 0xFF)
            .setMIFlags(Flag);
      BuildMI(MBB, MBBI, DL, get(CSKY::LSLI16), DstReg)
          .addReg(DstReg)
          .addImm(8)
          .setMIFlags(Flag);
      if (((Val >> 8) & 0xFF) != 0)
        BuildMI(MBB, MBBI, DL, get(CSKY::ADDI16), DstReg)
            .addReg(DstReg)
            .addImm((Val >> 8) & 0xFF)
            .setMIFlags(Flag);
      BuildMI(MBB, MBBI, DL, get(CSKY::LSLI16), DstReg)
          .addReg(DstReg)
          .addImm(8)
          .setMIFlags(Flag);
      if ((Val & 0xFF) != 0)
        BuildMI(MBB, MBBI, DL, get(CSKY::ADDI16), DstReg)
            .addReg(DstReg)
            .addImm(Val & 0xFF)
            .setMIFlags(Flag);
    }
  }

  return DstReg;
}

Register CSKYInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                            int &FrameIndex) const {
  switch (MI.getOpcode()) {
  default:
    return 0;
  case CSKY::LD16B:
  case CSKY::LD16H:
  case CSKY::LD16W:
  case CSKY::LD32B:
  case CSKY::LD32BS:
  case CSKY::LD32H:
  case CSKY::LD32HS:
  case CSKY::LD32W:
  case CSKY::FLD_S:
  case CSKY::FLD_D:
  case CSKY::f2FLD_S:
  case CSKY::f2FLD_D:
  case CSKY::RESTORE_CARRY:
    break;
  }

  if (MI.getOperand(1).isFI() && MI.getOperand(2).isImm() &&
      MI.getOperand(2).getImm() == 0) {
    FrameIndex = MI.getOperand(1).getIndex();
    return MI.getOperand(0).getReg();
  }

  return 0;
}

Register CSKYInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                           int &FrameIndex) const {
  switch (MI.getOpcode()) {
  default:
    return 0;
  case CSKY::ST16B:
  case CSKY::ST16H:
  case CSKY::ST16W:
  case CSKY::ST32B:
  case CSKY::ST32H:
  case CSKY::ST32W:
  case CSKY::FST_S:
  case CSKY::FST_D:
  case CSKY::f2FST_S:
  case CSKY::f2FST_D:
  case CSKY::SPILL_CARRY:
    break;
  }

  if (MI.getOperand(1).isFI() && MI.getOperand(2).isImm() &&
      MI.getOperand(2).getImm() == 0) {
    FrameIndex = MI.getOperand(1).getIndex();
    return MI.getOperand(0).getReg();
  }

  return 0;
}

void CSKYInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator I,
                                        Register SrcReg, bool IsKill, int FI,
                                        const TargetRegisterClass *RC,
                                        const TargetRegisterInfo *TRI,
                                        Register VReg) const {
  DebugLoc DL;
  if (I != MBB.end())
    DL = I->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  CSKYMachineFunctionInfo *CFI = MF.getInfo<CSKYMachineFunctionInfo>();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  unsigned Opcode = 0;

  if (CSKY::GPRRegClass.hasSubClassEq(RC)) {
    Opcode = CSKY::ST32W; // Optimize for 16bit
  } else if (CSKY::CARRYRegClass.hasSubClassEq(RC)) {
    Opcode = CSKY::SPILL_CARRY;
    CFI->setSpillsCR();
  } else if (v2sf && CSKY::sFPR32RegClass.hasSubClassEq(RC))
    Opcode = CSKY::FST_S;
  else if (v2df && CSKY::sFPR64RegClass.hasSubClassEq(RC))
    Opcode = CSKY::FST_D;
  else if (v3sf && CSKY::FPR32RegClass.hasSubClassEq(RC))
    Opcode = CSKY::f2FST_S;
  else if (v3df && CSKY::FPR64RegClass.hasSubClassEq(RC))
    Opcode = CSKY::f2FST_D;
  else {
    llvm_unreachable("Unknown RegisterClass");
  }

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOStore,
      MFI.getObjectSize(FI), MFI.getObjectAlign(FI));

  BuildMI(MBB, I, DL, get(Opcode))
      .addReg(SrcReg, getKillRegState(IsKill))
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(MMO);
}

void CSKYInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator I,
                                         Register DestReg, int FI,
                                         const TargetRegisterClass *RC,
                                         const TargetRegisterInfo *TRI,
                                         Register VReg) const {
  DebugLoc DL;
  if (I != MBB.end())
    DL = I->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  CSKYMachineFunctionInfo *CFI = MF.getInfo<CSKYMachineFunctionInfo>();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  unsigned Opcode = 0;

  if (CSKY::GPRRegClass.hasSubClassEq(RC)) {
    Opcode = CSKY::LD32W;
  } else if (CSKY::CARRYRegClass.hasSubClassEq(RC)) {
    Opcode = CSKY::RESTORE_CARRY;
    CFI->setSpillsCR();
  } else if (v2sf && CSKY::sFPR32RegClass.hasSubClassEq(RC))
    Opcode = CSKY::FLD_S;
  else if (v2df && CSKY::sFPR64RegClass.hasSubClassEq(RC))
    Opcode = CSKY::FLD_D;
  else if (v3sf && CSKY::FPR32RegClass.hasSubClassEq(RC))
    Opcode = CSKY::f2FLD_S;
  else if (v3df && CSKY::FPR64RegClass.hasSubClassEq(RC))
    Opcode = CSKY::f2FLD_D;
  else {
    llvm_unreachable("Unknown RegisterClass");
  }

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOLoad,
      MFI.getObjectSize(FI), MFI.getObjectAlign(FI));

  BuildMI(MBB, I, DL, get(Opcode), DestReg)
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(MMO);
}

void CSKYInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I,
                                const DebugLoc &DL, MCRegister DestReg,
                                MCRegister SrcReg, bool KillSrc) const {
  if (CSKY::GPRRegClass.contains(SrcReg) &&
      CSKY::CARRYRegClass.contains(DestReg)) {
    if (STI.hasE2()) {
      BuildMI(MBB, I, DL, get(CSKY::BTSTI32), DestReg)
          .addReg(SrcReg, getKillRegState(KillSrc))
          .addImm(0);
    } else {
      assert(SrcReg < CSKY::R8);
      BuildMI(MBB, I, DL, get(CSKY::BTSTI16), DestReg)
          .addReg(SrcReg, getKillRegState(KillSrc))
          .addImm(0);
    }
    return;
  }

  if (CSKY::CARRYRegClass.contains(SrcReg) &&
      CSKY::GPRRegClass.contains(DestReg)) {

    if (STI.hasE2()) {
      BuildMI(MBB, I, DL, get(CSKY::MVC32), DestReg)
          .addReg(SrcReg, getKillRegState(KillSrc));
    } else {
      assert(DestReg < CSKY::R16);
      assert(DestReg < CSKY::R8);
      BuildMI(MBB, I, DL, get(CSKY::MOVI16), DestReg).addImm(0);
      BuildMI(MBB, I, DL, get(CSKY::ADDC16))
          .addReg(DestReg, RegState::Define)
          .addReg(SrcReg, RegState::Define)
          .addReg(DestReg, getKillRegState(true))
          .addReg(DestReg, getKillRegState(true))
          .addReg(SrcReg, getKillRegState(true));
      BuildMI(MBB, I, DL, get(CSKY::BTSTI16))
          .addReg(SrcReg, RegState::Define | getDeadRegState(KillSrc))
          .addReg(DestReg)
          .addImm(0);
    }
    return;
  }

  unsigned Opcode = 0;
  if (CSKY::GPRRegClass.contains(DestReg, SrcReg))
    Opcode = STI.hasE2() ? CSKY::MOV32 : CSKY::MOV16;
  else if (v2sf && CSKY::sFPR32RegClass.contains(DestReg, SrcReg))
    Opcode = CSKY::FMOV_S;
  else if (v3sf && CSKY::FPR32RegClass.contains(DestReg, SrcReg))
    Opcode = CSKY::f2FMOV_S;
  else if (v2df && CSKY::sFPR64RegClass.contains(DestReg, SrcReg))
    Opcode = CSKY::FMOV_D;
  else if (v3df && CSKY::FPR64RegClass.contains(DestReg, SrcReg))
    Opcode = CSKY::f2FMOV_D;
  else if (v2sf && CSKY::sFPR32RegClass.contains(SrcReg) &&
           CSKY::GPRRegClass.contains(DestReg))
    Opcode = CSKY::FMFVRL;
  else if (v3sf && CSKY::FPR32RegClass.contains(SrcReg) &&
           CSKY::GPRRegClass.contains(DestReg))
    Opcode = CSKY::f2FMFVRL;
  else if (v2df && CSKY::sFPR64RegClass.contains(SrcReg) &&
           CSKY::GPRRegClass.contains(DestReg))
    Opcode = CSKY::FMFVRL_D;
  else if (v3df && CSKY::FPR64RegClass.contains(SrcReg) &&
           CSKY::GPRRegClass.contains(DestReg))
    Opcode = CSKY::f2FMFVRL_D;
  else if (v2sf && CSKY::GPRRegClass.contains(SrcReg) &&
           CSKY::sFPR32RegClass.contains(DestReg))
    Opcode = CSKY::FMTVRL;
  else if (v3sf && CSKY::GPRRegClass.contains(SrcReg) &&
           CSKY::FPR32RegClass.contains(DestReg))
    Opcode = CSKY::f2FMTVRL;
  else if (v2df && CSKY::GPRRegClass.contains(SrcReg) &&
           CSKY::sFPR64RegClass.contains(DestReg))
    Opcode = CSKY::FMTVRL_D;
  else if (v3df && CSKY::GPRRegClass.contains(SrcReg) &&
           CSKY::FPR64RegClass.contains(DestReg))
    Opcode = CSKY::f2FMTVRL_D;
  else {
    LLVM_DEBUG(dbgs() << "src = " << SrcReg << ", dst = " << DestReg);
    LLVM_DEBUG(I->dump());
    llvm_unreachable("Unknown RegisterClass");
  }

  BuildMI(MBB, I, DL, get(Opcode), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

Register CSKYInstrInfo::getGlobalBaseReg(MachineFunction &MF) const {
  CSKYMachineFunctionInfo *CFI = MF.getInfo<CSKYMachineFunctionInfo>();
  MachineConstantPool *MCP = MF.getConstantPool();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  Register GlobalBaseReg = CFI->getGlobalBaseReg();
  if (GlobalBaseReg != 0)
    return GlobalBaseReg;

  // Insert a pseudo instruction to set the GlobalBaseReg into the first
  // MBB of the function
  MachineBasicBlock &FirstMBB = MF.front();
  MachineBasicBlock::iterator MBBI = FirstMBB.begin();
  DebugLoc DL;

  CSKYConstantPoolValue *CPV = CSKYConstantPoolSymbol::Create(
      Type::getInt32Ty(MF.getFunction().getContext()), "_GLOBAL_OFFSET_TABLE_",
      0, CSKYCP::ADDR);

  unsigned CPI = MCP->getConstantPoolIndex(CPV, Align(4));

  MachineMemOperand *MO =
      MF.getMachineMemOperand(MachinePointerInfo::getConstantPool(MF),
                              MachineMemOperand::MOLoad, 4, Align(4));
  BuildMI(FirstMBB, MBBI, DL, get(CSKY::LRW32), CSKY::R28)
      .addConstantPoolIndex(CPI)
      .addMemOperand(MO);

  GlobalBaseReg = MRI.createVirtualRegister(&CSKY::GPRRegClass);
  BuildMI(FirstMBB, MBBI, DL, get(TargetOpcode::COPY), GlobalBaseReg)
      .addReg(CSKY::R28);

  CFI->setGlobalBaseReg(GlobalBaseReg);
  return GlobalBaseReg;
}

unsigned CSKYInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default:
    return MI.getDesc().getSize();
  case CSKY::CONSTPOOL_ENTRY:
    return MI.getOperand(2).getImm();
  case CSKY::SPILL_CARRY:
  case CSKY::RESTORE_CARRY:
  case CSKY::PseudoTLSLA32:
    return 8;
  case TargetOpcode::INLINEASM_BR:
  case TargetOpcode::INLINEASM: {
    const MachineFunction *MF = MI.getParent()->getParent();
    const char *AsmStr = MI.getOperand(0).getSymbolName();
    return getInlineAsmLength(AsmStr, *MF->getTarget().getMCAsmInfo());
  }
  }
}
