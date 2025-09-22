//===- Thumb2InstrInfo.cpp - Thumb-2 Instruction Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Thumb-2 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "Thumb2InstrInfo.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>

using namespace llvm;

static cl::opt<bool>
OldT2IfCvt("old-thumb2-ifcvt", cl::Hidden,
           cl::desc("Use old-style Thumb2 if-conversion heuristics"),
           cl::init(false));

static cl::opt<bool>
PreferNoCSEL("prefer-no-csel", cl::Hidden,
             cl::desc("Prefer predicated Move to CSEL"),
             cl::init(false));

Thumb2InstrInfo::Thumb2InstrInfo(const ARMSubtarget &STI)
    : ARMBaseInstrInfo(STI) {}

/// Return the noop instruction to use for a noop.
MCInst Thumb2InstrInfo::getNop() const {
  return MCInstBuilder(ARM::tHINT).addImm(0).addImm(ARMCC::AL).addReg(0);
}

unsigned Thumb2InstrInfo::getUnindexedOpcode(unsigned Opc) const {
  // FIXME
  return 0;
}

void
Thumb2InstrInfo::ReplaceTailWithBranchTo(MachineBasicBlock::iterator Tail,
                                         MachineBasicBlock *NewDest) const {
  MachineBasicBlock *MBB = Tail->getParent();
  ARMFunctionInfo *AFI = MBB->getParent()->getInfo<ARMFunctionInfo>();
  if (!AFI->hasITBlocks() || Tail->isBranch()) {
    TargetInstrInfo::ReplaceTailWithBranchTo(Tail, NewDest);
    return;
  }

  // If the first instruction of Tail is predicated, we may have to update
  // the IT instruction.
  Register PredReg;
  ARMCC::CondCodes CC = getInstrPredicate(*Tail, PredReg);
  MachineBasicBlock::iterator MBBI = Tail;
  if (CC != ARMCC::AL)
    // Expecting at least the t2IT instruction before it.
    --MBBI;

  // Actually replace the tail.
  TargetInstrInfo::ReplaceTailWithBranchTo(Tail, NewDest);

  // Fix up IT.
  if (CC != ARMCC::AL) {
    MachineBasicBlock::iterator E = MBB->begin();
    unsigned Count = 4; // At most 4 instructions in an IT block.
    while (Count && MBBI != E) {
      if (MBBI->isDebugInstr()) {
        --MBBI;
        continue;
      }
      if (MBBI->getOpcode() == ARM::t2IT) {
        unsigned Mask = MBBI->getOperand(1).getImm();
        if (Count == 4)
          MBBI->eraseFromParent();
        else {
          unsigned MaskOn = 1 << Count;
          unsigned MaskOff = ~(MaskOn - 1);
          MBBI->getOperand(1).setImm((Mask & MaskOff) | MaskOn);
        }
        return;
      }
      --MBBI;
      --Count;
    }

    // Ctrl flow can reach here if branch folding is run before IT block
    // formation pass.
  }
}

bool
Thumb2InstrInfo::isLegalToSplitMBBAt(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI) const {
  while (MBBI->isDebugInstr()) {
    ++MBBI;
    if (MBBI == MBB.end())
      return false;
  }

  Register PredReg;
  return getITInstrPredicate(*MBBI, PredReg) == ARMCC::AL;
}

MachineInstr *
Thumb2InstrInfo::optimizeSelect(MachineInstr &MI,
                                SmallPtrSetImpl<MachineInstr *> &SeenMIs,
                                bool PreferFalse) const {
  // Try to use the base optimizeSelect, which uses canFoldIntoMOVCC to fold the
  // MOVCC into another instruction. If that fails on 8.1-M fall back to using a
  // CSEL.
  MachineInstr *RV = ARMBaseInstrInfo::optimizeSelect(MI, SeenMIs, PreferFalse);
  if (!RV && getSubtarget().hasV8_1MMainlineOps() && !PreferNoCSEL) {
    Register DestReg = MI.getOperand(0).getReg();

    if (!DestReg.isVirtual())
      return nullptr;

    MachineInstrBuilder NewMI = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                                        get(ARM::t2CSEL), DestReg)
                                    .add(MI.getOperand(2))
                                    .add(MI.getOperand(1))
                                    .add(MI.getOperand(3));
    SeenMIs.insert(NewMI);
    return NewMI;
  }
  return RV;
}

void Thumb2InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I,
                                  const DebugLoc &DL, MCRegister DestReg,
                                  MCRegister SrcReg, bool KillSrc) const {
  // Handle SPR, DPR, and QPR copies.
  if (!ARM::GPRRegClass.contains(DestReg, SrcReg))
    return ARMBaseInstrInfo::copyPhysReg(MBB, I, DL, DestReg, SrcReg, KillSrc);

  BuildMI(MBB, I, DL, get(ARM::tMOVr), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc))
      .add(predOps(ARMCC::AL));
}

void Thumb2InstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator I,
                                          Register SrcReg, bool isKill, int FI,
                                          const TargetRegisterClass *RC,
                                          const TargetRegisterInfo *TRI,
                                          Register VReg) const {
  DebugLoc DL;
  if (I != MBB.end()) DL = I->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOStore,
      MFI.getObjectSize(FI), MFI.getObjectAlign(FI));

  if (ARM::GPRRegClass.hasSubClassEq(RC)) {
    BuildMI(MBB, I, DL, get(ARM::t2STRi12))
        .addReg(SrcReg, getKillRegState(isKill))
        .addFrameIndex(FI)
        .addImm(0)
        .addMemOperand(MMO)
        .add(predOps(ARMCC::AL));
    return;
  }

  if (ARM::GPRPairRegClass.hasSubClassEq(RC)) {
    // Thumb2 STRD expects its dest-registers to be in rGPR. Not a problem for
    // gsub_0, but needs an extra constraint for gsub_1 (which could be sp
    // otherwise).
    if (SrcReg.isVirtual()) {
      MachineRegisterInfo *MRI = &MF.getRegInfo();
      MRI->constrainRegClass(SrcReg, &ARM::GPRPairnospRegClass);
    }

    MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(ARM::t2STRDi8));
    AddDReg(MIB, SrcReg, ARM::gsub_0, getKillRegState(isKill), TRI);
    AddDReg(MIB, SrcReg, ARM::gsub_1, 0, TRI);
    MIB.addFrameIndex(FI).addImm(0).addMemOperand(MMO).add(predOps(ARMCC::AL));
    return;
  }

  ARMBaseInstrInfo::storeRegToStackSlot(MBB, I, SrcReg, isKill, FI, RC, TRI,
                                        Register());
}

void Thumb2InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator I,
                                           Register DestReg, int FI,
                                           const TargetRegisterClass *RC,
                                           const TargetRegisterInfo *TRI,
                                           Register VReg) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FI), MachineMemOperand::MOLoad,
      MFI.getObjectSize(FI), MFI.getObjectAlign(FI));
  DebugLoc DL;
  if (I != MBB.end()) DL = I->getDebugLoc();

  if (ARM::GPRRegClass.hasSubClassEq(RC)) {
    BuildMI(MBB, I, DL, get(ARM::t2LDRi12), DestReg)
        .addFrameIndex(FI)
        .addImm(0)
        .addMemOperand(MMO)
        .add(predOps(ARMCC::AL));
    return;
  }

  if (ARM::GPRPairRegClass.hasSubClassEq(RC)) {
    // Thumb2 LDRD expects its dest-registers to be in rGPR. Not a problem for
    // gsub_0, but needs an extra constraint for gsub_1 (which could be sp
    // otherwise).
    if (DestReg.isVirtual()) {
      MachineRegisterInfo *MRI = &MF.getRegInfo();
      MRI->constrainRegClass(DestReg, &ARM::GPRPairnospRegClass);
    }

    MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(ARM::t2LDRDi8));
    AddDReg(MIB, DestReg, ARM::gsub_0, RegState::DefineNoRead, TRI);
    AddDReg(MIB, DestReg, ARM::gsub_1, RegState::DefineNoRead, TRI);
    MIB.addFrameIndex(FI).addImm(0).addMemOperand(MMO).add(predOps(ARMCC::AL));

    if (DestReg.isPhysical())
      MIB.addReg(DestReg, RegState::ImplicitDefine);
    return;
  }

  ARMBaseInstrInfo::loadRegFromStackSlot(MBB, I, DestReg, FI, RC, TRI,
                                         Register());
}

void Thumb2InstrInfo::expandLoadStackGuard(
    MachineBasicBlock::iterator MI) const {
  MachineFunction &MF = *MI->getParent()->getParent();
  Module &M = *MF.getFunction().getParent();

  if (M.getStackProtectorGuard() == "tls") {
    expandLoadStackGuardBase(MI, ARM::t2MRC, ARM::t2LDRi12);
    return;
  }

  const auto *GV = cast<GlobalValue>((*MI->memoperands_begin())->getValue());
  if (MF.getSubtarget<ARMSubtarget>().isTargetELF() && !GV->isDSOLocal())
    expandLoadStackGuardBase(MI, ARM::t2LDRLIT_ga_pcrel, ARM::t2LDRi12);
  else if (MF.getTarget().isPositionIndependent())
    expandLoadStackGuardBase(MI, ARM::t2MOV_ga_pcrel, ARM::t2LDRi12);
  else
    expandLoadStackGuardBase(MI, ARM::t2MOVi32imm, ARM::t2LDRi12);
}

MachineInstr *Thumb2InstrInfo::commuteInstructionImpl(MachineInstr &MI,
                                                      bool NewMI,
                                                      unsigned OpIdx1,
                                                      unsigned OpIdx2) const {
  switch (MI.getOpcode()) {
  case ARM::MVE_VMAXNMAf16:
  case ARM::MVE_VMAXNMAf32:
  case ARM::MVE_VMINNMAf16:
  case ARM::MVE_VMINNMAf32:
    // Don't allow predicated instructions to be commuted.
    if (getVPTInstrPredicate(MI) != ARMVCC::None)
      return nullptr;
  }
  return ARMBaseInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
}

bool Thumb2InstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                           const MachineBasicBlock *MBB,
                                           const MachineFunction &MF) const {
  // BTI clearing instructions shall not take part in scheduling regions as
  // they must stay in their intended place. Although PAC isn't BTI clearing,
  // it can be transformed into PACBTI after the pre-RA Machine Scheduling
  // has taken place, so its movement must also be restricted.
  switch (MI.getOpcode()) {
  case ARM::t2BTI:
  case ARM::t2PAC:
  case ARM::t2PACBTI:
  case ARM::t2SG:
    return true;
  default:
    break;
  }
  return ARMBaseInstrInfo::isSchedulingBoundary(MI, MBB, MF);
}

void llvm::emitT2RegPlusImmediate(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator &MBBI,
                                  const DebugLoc &dl, Register DestReg,
                                  Register BaseReg, int NumBytes,
                                  ARMCC::CondCodes Pred, Register PredReg,
                                  const ARMBaseInstrInfo &TII,
                                  unsigned MIFlags) {
  if (NumBytes == 0 && DestReg != BaseReg) {
    BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr), DestReg)
      .addReg(BaseReg, RegState::Kill)
      .addImm((unsigned)Pred).addReg(PredReg).setMIFlags(MIFlags);
    return;
  }

  bool isSub = NumBytes < 0;
  if (isSub) NumBytes = -NumBytes;

  // If profitable, use a movw or movt to materialize the offset.
  // FIXME: Use the scavenger to grab a scratch register.
  if (DestReg != ARM::SP && DestReg != BaseReg &&
      NumBytes >= 4096 &&
      ARM_AM::getT2SOImmVal(NumBytes) == -1) {
    bool Fits = false;
    if (NumBytes < 65536) {
      // Use a movw to materialize the 16-bit constant.
      BuildMI(MBB, MBBI, dl, TII.get(ARM::t2MOVi16), DestReg)
        .addImm(NumBytes)
        .addImm((unsigned)Pred).addReg(PredReg).setMIFlags(MIFlags);
      Fits = true;
    } else if ((NumBytes & 0xffff) == 0) {
      // Use a movt to materialize the 32-bit constant.
      BuildMI(MBB, MBBI, dl, TII.get(ARM::t2MOVTi16), DestReg)
        .addReg(DestReg)
        .addImm(NumBytes >> 16)
        .addImm((unsigned)Pred).addReg(PredReg).setMIFlags(MIFlags);
      Fits = true;
    }

    if (Fits) {
      if (isSub) {
        BuildMI(MBB, MBBI, dl, TII.get(ARM::t2SUBrr), DestReg)
            .addReg(BaseReg)
            .addReg(DestReg, RegState::Kill)
            .add(predOps(Pred, PredReg))
            .add(condCodeOp())
            .setMIFlags(MIFlags);
      } else {
        // Here we know that DestReg is not SP but we do not
        // know anything about BaseReg. t2ADDrr is an invalid
        // instruction is SP is used as the second argument, but
        // is fine if SP is the first argument. To be sure we
        // do not generate invalid encoding, put BaseReg first.
        BuildMI(MBB, MBBI, dl, TII.get(ARM::t2ADDrr), DestReg)
            .addReg(BaseReg)
            .addReg(DestReg, RegState::Kill)
            .add(predOps(Pred, PredReg))
            .add(condCodeOp())
            .setMIFlags(MIFlags);
      }
      return;
    }
  }

  while (NumBytes) {
    unsigned ThisVal = NumBytes;
    unsigned Opc = 0;
    if (DestReg == ARM::SP && BaseReg != ARM::SP) {
      // mov sp, rn. Note t2MOVr cannot be used.
      BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr), DestReg)
          .addReg(BaseReg)
          .setMIFlags(MIFlags)
          .add(predOps(ARMCC::AL));
      BaseReg = ARM::SP;
      continue;
    }

    assert((DestReg != ARM::SP || BaseReg == ARM::SP) &&
           "Writing to SP, from other register.");

    // Try to use T1, as it smaller
    if ((DestReg == ARM::SP) && (ThisVal < ((1 << 7) - 1) * 4)) {
      assert((ThisVal & 3) == 0 && "Stack update is not multiple of 4?");
      Opc = isSub ? ARM::tSUBspi : ARM::tADDspi;
      BuildMI(MBB, MBBI, dl, TII.get(Opc), DestReg)
          .addReg(BaseReg)
          .addImm(ThisVal / 4)
          .setMIFlags(MIFlags)
          .add(predOps(ARMCC::AL));
      break;
    }
    bool HasCCOut = true;
    int ImmIsT2SO = ARM_AM::getT2SOImmVal(ThisVal);
    bool ToSP = DestReg == ARM::SP;
    unsigned t2SUB = ToSP ? ARM::t2SUBspImm : ARM::t2SUBri;
    unsigned t2ADD = ToSP ? ARM::t2ADDspImm : ARM::t2ADDri;
    unsigned t2SUBi12 = ToSP ? ARM::t2SUBspImm12 : ARM::t2SUBri12;
    unsigned t2ADDi12 = ToSP ? ARM::t2ADDspImm12 : ARM::t2ADDri12;
    Opc = isSub ? t2SUB : t2ADD;
    // Prefer T2: sub rd, rn, so_imm | sub sp, sp, so_imm
    if (ImmIsT2SO != -1) {
      NumBytes = 0;
    } else if (ThisVal < 4096) {
      // Prefer T3 if can make it in a single go: subw rd, rn, imm12 | subw sp,
      // sp, imm12
      Opc = isSub ? t2SUBi12 : t2ADDi12;
      HasCCOut = false;
      NumBytes = 0;
    } else {
      // Use one T2 instruction to reduce NumBytes
      // FIXME: Move this to ARMAddressingModes.h?
      unsigned RotAmt = llvm::countl_zero(ThisVal);
      ThisVal = ThisVal & llvm::rotr<uint32_t>(0xff000000U, RotAmt);
      NumBytes &= ~ThisVal;
      assert(ARM_AM::getT2SOImmVal(ThisVal) != -1 &&
             "Bit extraction didn't work?");
    }

    // Build the new ADD / SUB.
    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, dl, TII.get(Opc), DestReg)
                                  .addReg(BaseReg, RegState::Kill)
                                  .addImm(ThisVal)
                                  .add(predOps(ARMCC::AL))
                                  .setMIFlags(MIFlags);
    if (HasCCOut)
      MIB.add(condCodeOp());

    BaseReg = DestReg;
  }
}

static unsigned
negativeOffsetOpcode(unsigned opcode)
{
  switch (opcode) {
  case ARM::t2LDRi12:   return ARM::t2LDRi8;
  case ARM::t2LDRHi12:  return ARM::t2LDRHi8;
  case ARM::t2LDRBi12:  return ARM::t2LDRBi8;
  case ARM::t2LDRSHi12: return ARM::t2LDRSHi8;
  case ARM::t2LDRSBi12: return ARM::t2LDRSBi8;
  case ARM::t2STRi12:   return ARM::t2STRi8;
  case ARM::t2STRBi12:  return ARM::t2STRBi8;
  case ARM::t2STRHi12:  return ARM::t2STRHi8;
  case ARM::t2PLDi12:   return ARM::t2PLDi8;
  case ARM::t2PLDWi12:  return ARM::t2PLDWi8;
  case ARM::t2PLIi12:   return ARM::t2PLIi8;

  case ARM::t2LDRi8:
  case ARM::t2LDRHi8:
  case ARM::t2LDRBi8:
  case ARM::t2LDRSHi8:
  case ARM::t2LDRSBi8:
  case ARM::t2STRi8:
  case ARM::t2STRBi8:
  case ARM::t2STRHi8:
  case ARM::t2PLDi8:
  case ARM::t2PLDWi8:
  case ARM::t2PLIi8:
    return opcode;

  default:
    llvm_unreachable("unknown thumb2 opcode.");
  }
}

static unsigned
positiveOffsetOpcode(unsigned opcode)
{
  switch (opcode) {
  case ARM::t2LDRi8:   return ARM::t2LDRi12;
  case ARM::t2LDRHi8:  return ARM::t2LDRHi12;
  case ARM::t2LDRBi8:  return ARM::t2LDRBi12;
  case ARM::t2LDRSHi8: return ARM::t2LDRSHi12;
  case ARM::t2LDRSBi8: return ARM::t2LDRSBi12;
  case ARM::t2STRi8:   return ARM::t2STRi12;
  case ARM::t2STRBi8:  return ARM::t2STRBi12;
  case ARM::t2STRHi8:  return ARM::t2STRHi12;
  case ARM::t2PLDi8:   return ARM::t2PLDi12;
  case ARM::t2PLDWi8:  return ARM::t2PLDWi12;
  case ARM::t2PLIi8:   return ARM::t2PLIi12;

  case ARM::t2LDRi12:
  case ARM::t2LDRHi12:
  case ARM::t2LDRBi12:
  case ARM::t2LDRSHi12:
  case ARM::t2LDRSBi12:
  case ARM::t2STRi12:
  case ARM::t2STRBi12:
  case ARM::t2STRHi12:
  case ARM::t2PLDi12:
  case ARM::t2PLDWi12:
  case ARM::t2PLIi12:
    return opcode;

  default:
    llvm_unreachable("unknown thumb2 opcode.");
  }
}

static unsigned
immediateOffsetOpcode(unsigned opcode)
{
  switch (opcode) {
  case ARM::t2LDRs:   return ARM::t2LDRi12;
  case ARM::t2LDRHs:  return ARM::t2LDRHi12;
  case ARM::t2LDRBs:  return ARM::t2LDRBi12;
  case ARM::t2LDRSHs: return ARM::t2LDRSHi12;
  case ARM::t2LDRSBs: return ARM::t2LDRSBi12;
  case ARM::t2STRs:   return ARM::t2STRi12;
  case ARM::t2STRBs:  return ARM::t2STRBi12;
  case ARM::t2STRHs:  return ARM::t2STRHi12;
  case ARM::t2PLDs:   return ARM::t2PLDi12;
  case ARM::t2PLDWs:  return ARM::t2PLDWi12;
  case ARM::t2PLIs:   return ARM::t2PLIi12;

  case ARM::t2LDRi12:
  case ARM::t2LDRHi12:
  case ARM::t2LDRBi12:
  case ARM::t2LDRSHi12:
  case ARM::t2LDRSBi12:
  case ARM::t2STRi12:
  case ARM::t2STRBi12:
  case ARM::t2STRHi12:
  case ARM::t2PLDi12:
  case ARM::t2PLDWi12:
  case ARM::t2PLIi12:
  case ARM::t2LDRi8:
  case ARM::t2LDRHi8:
  case ARM::t2LDRBi8:
  case ARM::t2LDRSHi8:
  case ARM::t2LDRSBi8:
  case ARM::t2STRi8:
  case ARM::t2STRBi8:
  case ARM::t2STRHi8:
  case ARM::t2PLDi8:
  case ARM::t2PLDWi8:
  case ARM::t2PLIi8:
    return opcode;

  default:
    llvm_unreachable("unknown thumb2 opcode.");
  }
}

bool llvm::rewriteT2FrameIndex(MachineInstr &MI, unsigned FrameRegIdx,
                               Register FrameReg, int &Offset,
                               const ARMBaseInstrInfo &TII,
                               const TargetRegisterInfo *TRI) {
  unsigned Opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MI.getDesc();
  unsigned AddrMode = (Desc.TSFlags & ARMII::AddrModeMask);
  bool isSub = false;

  MachineFunction &MF = *MI.getParent()->getParent();
  const TargetRegisterClass *RegClass =
      TII.getRegClass(Desc, FrameRegIdx, TRI, MF);

  // Memory operands in inline assembly always use AddrModeT2_i12.
  if (Opcode == ARM::INLINEASM || Opcode == ARM::INLINEASM_BR)
    AddrMode = ARMII::AddrModeT2_i12; // FIXME. mode for thumb2?

  const bool IsSP = Opcode == ARM::t2ADDspImm12 || Opcode == ARM::t2ADDspImm;
  if (IsSP || Opcode == ARM::t2ADDri || Opcode == ARM::t2ADDri12) {
    Offset += MI.getOperand(FrameRegIdx+1).getImm();

    Register PredReg;
    if (Offset == 0 && getInstrPredicate(MI, PredReg) == ARMCC::AL &&
        !MI.definesRegister(ARM::CPSR, /*TRI=*/nullptr)) {
      // Turn it into a move.
      MI.setDesc(TII.get(ARM::tMOVr));
      MI.getOperand(FrameRegIdx).ChangeToRegister(FrameReg, false);
      // Remove offset and remaining explicit predicate operands.
      do MI.removeOperand(FrameRegIdx+1);
      while (MI.getNumOperands() > FrameRegIdx+1);
      MachineInstrBuilder MIB(*MI.getParent()->getParent(), &MI);
      MIB.add(predOps(ARMCC::AL));
      return true;
    }

    bool HasCCOut = (Opcode != ARM::t2ADDspImm12 && Opcode != ARM::t2ADDri12);

    if (Offset < 0) {
      Offset = -Offset;
      isSub = true;
      MI.setDesc(IsSP ? TII.get(ARM::t2SUBspImm) : TII.get(ARM::t2SUBri));
    } else {
      MI.setDesc(IsSP ? TII.get(ARM::t2ADDspImm) : TII.get(ARM::t2ADDri));
    }

    // Common case: small offset, fits into instruction.
    if (ARM_AM::getT2SOImmVal(Offset) != -1) {
      MI.getOperand(FrameRegIdx).ChangeToRegister(FrameReg, false);
      MI.getOperand(FrameRegIdx+1).ChangeToImmediate(Offset);
      // Add cc_out operand if the original instruction did not have one.
      if (!HasCCOut)
        MI.addOperand(MachineOperand::CreateReg(0, false));
      Offset = 0;
      return true;
    }
    // Another common case: imm12.
    if (Offset < 4096 &&
        (!HasCCOut || MI.getOperand(MI.getNumOperands()-1).getReg() == 0)) {
      unsigned NewOpc = isSub ? IsSP ? ARM::t2SUBspImm12 : ARM::t2SUBri12
                              : IsSP ? ARM::t2ADDspImm12 : ARM::t2ADDri12;
      MI.setDesc(TII.get(NewOpc));
      MI.getOperand(FrameRegIdx).ChangeToRegister(FrameReg, false);
      MI.getOperand(FrameRegIdx+1).ChangeToImmediate(Offset);
      // Remove the cc_out operand.
      if (HasCCOut)
        MI.removeOperand(MI.getNumOperands()-1);
      Offset = 0;
      return true;
    }

    // Otherwise, extract 8 adjacent bits from the immediate into this
    // t2ADDri/t2SUBri.
    unsigned RotAmt = llvm::countl_zero<unsigned>(Offset);
    unsigned ThisImmVal = Offset & llvm::rotr<uint32_t>(0xff000000U, RotAmt);

    // We will handle these bits from offset, clear them.
    Offset &= ~ThisImmVal;

    assert(ARM_AM::getT2SOImmVal(ThisImmVal) != -1 &&
           "Bit extraction didn't work?");
    MI.getOperand(FrameRegIdx+1).ChangeToImmediate(ThisImmVal);
    // Add cc_out operand if the original instruction did not have one.
    if (!HasCCOut)
      MI.addOperand(MachineOperand::CreateReg(0, false));
  } else {
    // AddrMode4 and AddrMode6 cannot handle any offset.
    if (AddrMode == ARMII::AddrMode4 || AddrMode == ARMII::AddrMode6)
      return false;

    // AddrModeT2_so cannot handle any offset. If there is no offset
    // register then we change to an immediate version.
    unsigned NewOpc = Opcode;
    if (AddrMode == ARMII::AddrModeT2_so) {
      Register OffsetReg = MI.getOperand(FrameRegIdx + 1).getReg();
      if (OffsetReg != 0) {
        MI.getOperand(FrameRegIdx).ChangeToRegister(FrameReg, false);
        return Offset == 0;
      }

      MI.removeOperand(FrameRegIdx+1);
      MI.getOperand(FrameRegIdx+1).ChangeToImmediate(0);
      NewOpc = immediateOffsetOpcode(Opcode);
      AddrMode = ARMII::AddrModeT2_i12;
    }

    unsigned NumBits = 0;
    unsigned Scale = 1;
    if (AddrMode == ARMII::AddrModeT2_i8neg ||
        AddrMode == ARMII::AddrModeT2_i12) {
      // i8 supports only negative, and i12 supports only positive, so
      // based on Offset sign convert Opcode to the appropriate
      // instruction
      Offset += MI.getOperand(FrameRegIdx+1).getImm();
      if (Offset < 0) {
        NewOpc = negativeOffsetOpcode(Opcode);
        NumBits = 8;
        isSub = true;
        Offset = -Offset;
      } else {
        NewOpc = positiveOffsetOpcode(Opcode);
        NumBits = 12;
      }
    } else if (AddrMode == ARMII::AddrMode5) {
      // VFP address mode.
      const MachineOperand &OffOp = MI.getOperand(FrameRegIdx+1);
      int InstrOffs = ARM_AM::getAM5Offset(OffOp.getImm());
      if (ARM_AM::getAM5Op(OffOp.getImm()) == ARM_AM::sub)
        InstrOffs *= -1;
      NumBits = 8;
      Scale = 4;
      Offset += InstrOffs * 4;
      assert((Offset & (Scale-1)) == 0 && "Can't encode this offset!");
      if (Offset < 0) {
        Offset = -Offset;
        isSub = true;
      }
    } else if (AddrMode == ARMII::AddrMode5FP16) {
      // VFP address mode.
      const MachineOperand &OffOp = MI.getOperand(FrameRegIdx+1);
      int InstrOffs = ARM_AM::getAM5FP16Offset(OffOp.getImm());
      if (ARM_AM::getAM5FP16Op(OffOp.getImm()) == ARM_AM::sub)
        InstrOffs *= -1;
      NumBits = 8;
      Scale = 2;
      Offset += InstrOffs * 2;
      assert((Offset & (Scale-1)) == 0 && "Can't encode this offset!");
      if (Offset < 0) {
        Offset = -Offset;
        isSub = true;
      }
    } else if (AddrMode == ARMII::AddrModeT2_i7s4 ||
               AddrMode == ARMII::AddrModeT2_i7s2 ||
               AddrMode == ARMII::AddrModeT2_i7) {
      Offset += MI.getOperand(FrameRegIdx + 1).getImm();
      unsigned OffsetMask;
      switch (AddrMode) {
      case ARMII::AddrModeT2_i7s4: NumBits = 9; OffsetMask = 0x3; break;
      case ARMII::AddrModeT2_i7s2: NumBits = 8; OffsetMask = 0x1; break;
      default:                     NumBits = 7; OffsetMask = 0x0; break;
      }
      // MCInst operand expects already scaled value.
      Scale = 1;
      assert((Offset & OffsetMask) == 0 && "Can't encode this offset!");
      (void)OffsetMask; // squash unused-variable warning at -NDEBUG
    } else if (AddrMode == ARMII::AddrModeT2_i8s4) {
      Offset += MI.getOperand(FrameRegIdx + 1).getImm();
      NumBits = 8 + 2;
      // MCInst operand expects already scaled value.
      Scale = 1;
      assert((Offset & 3) == 0 && "Can't encode this offset!");
    } else if (AddrMode == ARMII::AddrModeT2_ldrex) {
      Offset += MI.getOperand(FrameRegIdx + 1).getImm() * 4;
      NumBits = 8; // 8 bits scaled by 4
      Scale = 4;
      assert((Offset & 3) == 0 && "Can't encode this offset!");
    } else {
      llvm_unreachable("Unsupported addressing mode!");
    }

    if (NewOpc != Opcode)
      MI.setDesc(TII.get(NewOpc));

    MachineOperand &ImmOp = MI.getOperand(FrameRegIdx+1);

    // Attempt to fold address computation
    // Common case: small offset, fits into instruction. We need to make sure
    // the register class is correct too, for instructions like the MVE
    // VLDRH.32, which only accepts low tGPR registers.
    int ImmedOffset = Offset / Scale;
    unsigned Mask = (1 << NumBits) - 1;
    if ((unsigned)Offset <= Mask * Scale &&
        (FrameReg.isVirtual() || RegClass->contains(FrameReg))) {
      if (FrameReg.isVirtual()) {
        // Make sure the register class for the virtual register is correct
        MachineRegisterInfo *MRI = &MF.getRegInfo();
        if (!MRI->constrainRegClass(FrameReg, RegClass))
          llvm_unreachable("Unable to constrain virtual register class.");
      }

      // Replace the FrameIndex with fp/sp
      MI.getOperand(FrameRegIdx).ChangeToRegister(FrameReg, false);
      if (isSub) {
        if (AddrMode == ARMII::AddrMode5 || AddrMode == ARMII::AddrMode5FP16)
          // FIXME: Not consistent.
          ImmedOffset |= 1 << NumBits;
        else
          ImmedOffset = -ImmedOffset;
      }
      ImmOp.ChangeToImmediate(ImmedOffset);
      Offset = 0;
      return true;
    }

    // Otherwise, offset doesn't fit. Pull in what we can to simplify
    ImmedOffset = ImmedOffset & Mask;
    if (isSub) {
      if (AddrMode == ARMII::AddrMode5 || AddrMode == ARMII::AddrMode5FP16)
        // FIXME: Not consistent.
        ImmedOffset |= 1 << NumBits;
      else {
        ImmedOffset = -ImmedOffset;
        if (ImmedOffset == 0)
          // Change the opcode back if the encoded offset is zero.
          MI.setDesc(TII.get(positiveOffsetOpcode(NewOpc)));
      }
    }
    ImmOp.ChangeToImmediate(ImmedOffset);
    Offset &= ~(Mask*Scale);
  }

  Offset = (isSub) ? -Offset : Offset;
  return Offset == 0 && (FrameReg.isVirtual() || RegClass->contains(FrameReg));
}

ARMCC::CondCodes llvm::getITInstrPredicate(const MachineInstr &MI,
                                           Register &PredReg) {
  unsigned Opc = MI.getOpcode();
  if (Opc == ARM::tBcc || Opc == ARM::t2Bcc)
    return ARMCC::AL;
  return getInstrPredicate(MI, PredReg);
}

int llvm::findFirstVPTPredOperandIdx(const MachineInstr &MI) {
  const MCInstrDesc &MCID = MI.getDesc();

  for (unsigned i = 0, e = MCID.getNumOperands(); i != e; ++i)
    if (ARM::isVpred(MCID.operands()[i].OperandType))
      return i;

  return -1;
}

ARMVCC::VPTCodes llvm::getVPTInstrPredicate(const MachineInstr &MI,
                                            Register &PredReg) {
  int PIdx = findFirstVPTPredOperandIdx(MI);
  if (PIdx == -1) {
    PredReg = 0;
    return ARMVCC::None;
  }

  PredReg = MI.getOperand(PIdx+1).getReg();
  return (ARMVCC::VPTCodes)MI.getOperand(PIdx).getImm();
}

void llvm::recomputeVPTBlockMask(MachineInstr &Instr) {
  assert(isVPTOpcode(Instr.getOpcode()) && "Not a VPST or VPT Instruction!");

  MachineOperand &MaskOp = Instr.getOperand(0);
  assert(MaskOp.isImm() && "Operand 0 is not the block mask of the VPT/VPST?!");

  MachineBasicBlock::iterator Iter = ++Instr.getIterator(),
                              End = Instr.getParent()->end();

  while (Iter != End && Iter->isDebugInstr())
    ++Iter;

  // Verify that the instruction after the VPT/VPST is predicated (it should
  // be), and skip it.
  assert(Iter != End && "Expected some instructions in any VPT block");
  assert(
      getVPTInstrPredicate(*Iter) == ARMVCC::Then &&
      "VPT/VPST should be followed by an instruction with a 'then' predicate!");
  ++Iter;

  // Iterate over the predicated instructions, updating the BlockMask as we go.
  ARM::PredBlockMask BlockMask = ARM::PredBlockMask::T;
  while (Iter != End) {
    if (Iter->isDebugInstr()) {
      ++Iter;
      continue;
    }
    ARMVCC::VPTCodes Pred = getVPTInstrPredicate(*Iter);
    if (Pred == ARMVCC::None)
      break;
    BlockMask = expandPredBlockMask(BlockMask, Pred);
    ++Iter;
  }

  // Rewrite the BlockMask.
  MaskOp.setImm((int64_t)(BlockMask));
}
