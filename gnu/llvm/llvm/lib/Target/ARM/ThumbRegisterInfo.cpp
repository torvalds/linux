//===-- ThumbRegisterInfo.cpp - Thumb-1 Register Information -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Thumb-1 implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#include "ThumbRegisterInfo.h"
#include "ARMBaseInstrInfo.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
extern cl::opt<bool> ReuseFrameIndexVals;
}

using namespace llvm;

ThumbRegisterInfo::ThumbRegisterInfo() = default;

const TargetRegisterClass *
ThumbRegisterInfo::getLargestLegalSuperClass(const TargetRegisterClass *RC,
                                              const MachineFunction &MF) const {
  if (!MF.getSubtarget<ARMSubtarget>().isThumb1Only())
    return ARMBaseRegisterInfo::getLargestLegalSuperClass(RC, MF);

  if (ARM::tGPRRegClass.hasSubClassEq(RC))
    return &ARM::tGPRRegClass;
  return ARMBaseRegisterInfo::getLargestLegalSuperClass(RC, MF);
}

const TargetRegisterClass *
ThumbRegisterInfo::getPointerRegClass(const MachineFunction &MF,
                                      unsigned Kind) const {
  if (!MF.getSubtarget<ARMSubtarget>().isThumb1Only())
    return ARMBaseRegisterInfo::getPointerRegClass(MF, Kind);
  return &ARM::tGPRRegClass;
}

static void emitThumb1LoadConstPool(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator &MBBI,
                                    const DebugLoc &dl, unsigned DestReg,
                                    unsigned SubIdx, int Val,
                                    ARMCC::CondCodes Pred, unsigned PredReg,
                                    unsigned MIFlags) {
  MachineFunction &MF = *MBB.getParent();
  const ARMSubtarget &STI = MF.getSubtarget<ARMSubtarget>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  MachineConstantPool *ConstantPool = MF.getConstantPool();
  const Constant *C = ConstantInt::get(
          Type::getInt32Ty(MBB.getParent()->getFunction().getContext()), Val);
  unsigned Idx = ConstantPool->getConstantPoolIndex(C, Align(4));

  BuildMI(MBB, MBBI, dl, TII.get(ARM::tLDRpci))
    .addReg(DestReg, getDefRegState(true), SubIdx)
    .addConstantPoolIndex(Idx).addImm(Pred).addReg(PredReg)
    .setMIFlags(MIFlags);
}

static void emitThumb2LoadConstPool(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator &MBBI,
                                    const DebugLoc &dl, unsigned DestReg,
                                    unsigned SubIdx, int Val,
                                    ARMCC::CondCodes Pred, unsigned PredReg,
                                    unsigned MIFlags) {
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineConstantPool *ConstantPool = MF.getConstantPool();
  const Constant *C = ConstantInt::get(
           Type::getInt32Ty(MBB.getParent()->getFunction().getContext()), Val);
  unsigned Idx = ConstantPool->getConstantPoolIndex(C, Align(4));

  BuildMI(MBB, MBBI, dl, TII.get(ARM::t2LDRpci))
      .addReg(DestReg, getDefRegState(true), SubIdx)
      .addConstantPoolIndex(Idx)
      .add(predOps(ARMCC::AL))
      .setMIFlags(MIFlags);
}

/// emitLoadConstPool - Emits a load from constpool to materialize the
/// specified immediate.
void ThumbRegisterInfo::emitLoadConstPool(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator &MBBI,
    const DebugLoc &dl, Register DestReg, unsigned SubIdx, int Val,
    ARMCC::CondCodes Pred, Register PredReg, unsigned MIFlags) const {
  MachineFunction &MF = *MBB.getParent();
  const ARMSubtarget &STI = MF.getSubtarget<ARMSubtarget>();
  if (STI.isThumb1Only()) {
    assert((isARMLowRegister(DestReg) || DestReg.isVirtual()) &&
           "Thumb1 does not have ldr to high register");
    return emitThumb1LoadConstPool(MBB, MBBI, dl, DestReg, SubIdx, Val, Pred,
                                   PredReg, MIFlags);
  }
  return emitThumb2LoadConstPool(MBB, MBBI, dl, DestReg, SubIdx, Val, Pred,
                                 PredReg, MIFlags);
}

/// emitThumbRegPlusImmInReg - Emits a series of instructions to materialize a
/// destreg = basereg + immediate in Thumb code. Materialize the immediate in a
/// register using mov / mvn (armv6-M >) sequences, movs / lsls / adds / lsls /
/// adds / lsls / adds sequences (armv6-M) or load the immediate from a
/// constpool entry.
static void emitThumbRegPlusImmInReg(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator &MBBI,
    const DebugLoc &dl, Register DestReg, Register BaseReg, int NumBytes,
    bool CanChangeCC, const TargetInstrInfo &TII,
    const ARMBaseRegisterInfo &MRI, unsigned MIFlags = MachineInstr::NoFlags) {
  MachineFunction &MF = *MBB.getParent();
  const ARMSubtarget &ST = MF.getSubtarget<ARMSubtarget>();

  // Use a single sp-relative add if the immediate is small enough.
  if (BaseReg == ARM::SP &&
      (DestReg.isVirtual() || isARMLowRegister(DestReg)) && NumBytes >= 0 &&
      NumBytes <= 1020 && (NumBytes % 4) == 0) {
    BuildMI(MBB, MBBI, dl, TII.get(ARM::tADDrSPi), DestReg)
        .addReg(ARM::SP)
        .addImm(NumBytes / 4)
        .add(predOps(ARMCC::AL))
        .setMIFlags(MIFlags);
    return;
  }

  bool isHigh = !isARMLowRegister(DestReg) ||
                (BaseReg != 0 && !isARMLowRegister(BaseReg));
  bool isSub = false;
  // Subtract doesn't have high register version. Load the negative value
  // if either base or dest register is a high register. Also, if do not
  // issue sub as part of the sequence if condition register is to be
  // preserved.
  if (NumBytes < 0 && !isHigh && CanChangeCC) {
    isSub = true;
    NumBytes = -NumBytes;
  }
  Register LdReg = DestReg;
  if (DestReg == ARM::SP)
    assert(BaseReg == ARM::SP && "Unexpected!");
  if (!isARMLowRegister(DestReg) && !DestReg.isVirtual())
    LdReg = MF.getRegInfo().createVirtualRegister(&ARM::tGPRRegClass);

  if (NumBytes <= 255 && NumBytes >= 0 && CanChangeCC) {
    BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVi8), LdReg)
        .add(t1CondCodeOp())
        .addImm(NumBytes)
        .setMIFlags(MIFlags);
  } else if (NumBytes < 0 && NumBytes >= -255 && CanChangeCC) {
    BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVi8), LdReg)
        .add(t1CondCodeOp())
        .addImm(NumBytes)
        .setMIFlags(MIFlags);
    BuildMI(MBB, MBBI, dl, TII.get(ARM::tRSB), LdReg)
        .add(t1CondCodeOp())
        .addReg(LdReg, RegState::Kill)
        .setMIFlags(MIFlags);
  } else if (ST.genExecuteOnly()) {
    if (ST.useMovt()) {
      BuildMI(MBB, MBBI, dl, TII.get(ARM::t2MOVi32imm ), LdReg)
          .addImm(NumBytes)
          .setMIFlags(MIFlags);
    } else if (!CanChangeCC) {
      // tMOVi32imm is lowered to a sequence of flag-setting instructions, so
      // if CPSR is live we need to save and restore CPSR around it.
      // TODO Try inserting the tMOVi32imm at an earlier point, where CPSR is
      // dead.
      bool LiveCpsr = false, CpsrWrite = false;
      auto isCpsr = [](auto &MO) { return MO.getReg() == ARM::CPSR; };
      for (auto Iter = MBBI; Iter != MBB.instr_end(); ++Iter) {
        // If CPSR is used after this instruction (and there's not a def before
        // that) then CPSR is live.
        if (any_of(Iter->all_uses(), isCpsr)) {
          LiveCpsr = true;
          break;
        }
        if (any_of(Iter->all_defs(), isCpsr)) {
          CpsrWrite = true;
          break;
        }
      }
      // If there's no use or def of CPSR then it may be live if it's a
      // live-out value.
      auto liveOutIsCpsr = [](auto &Out) { return Out.PhysReg == ARM::CPSR; };
      if (!LiveCpsr && !CpsrWrite)
        LiveCpsr = any_of(MBB.liveouts(), liveOutIsCpsr);

      Register CPSRSaveReg;
      unsigned APSREncoding;
      if (LiveCpsr) {
        CPSRSaveReg = MF.getRegInfo().createVirtualRegister(&ARM::tGPRRegClass);
        APSREncoding =
            ARMSysReg::lookupMClassSysRegByName("apsr_nzcvq")->Encoding;
        BuildMI(MBB, MBBI, dl, TII.get(ARM::t2MRS_M), CPSRSaveReg)
            .addImm(APSREncoding)
            .add(predOps(ARMCC::AL))
            .addReg(ARM::CPSR, RegState::Implicit);
      }
      BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVi32imm), LdReg)
          .addImm(NumBytes)
          .setMIFlags(MIFlags);
      if (LiveCpsr) {
        BuildMI(MBB, MBBI, dl, TII.get(ARM::t2MSR_M))
            .addImm(APSREncoding)
            .addReg(CPSRSaveReg, RegState::Kill)
            .add(predOps(ARMCC::AL));
      }
    } else {
      BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVi32imm), LdReg)
          .addImm(NumBytes)
          .setMIFlags(MIFlags);
    }
  } else
    MRI.emitLoadConstPool(MBB, MBBI, dl, LdReg, 0, NumBytes, ARMCC::AL, 0,
                          MIFlags);

  // Emit add / sub.
  int Opc = (isSub) ? ARM::tSUBrr
                    : ((isHigh || !CanChangeCC) ? ARM::tADDhirr : ARM::tADDrr);
  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, dl, TII.get(Opc), DestReg);
  if (Opc != ARM::tADDhirr)
    MIB = MIB.add(t1CondCodeOp());
  if (DestReg == ARM::SP || isSub)
    MIB.addReg(BaseReg).addReg(LdReg, RegState::Kill);
  else
    MIB.addReg(LdReg).addReg(BaseReg, RegState::Kill);
  MIB.add(predOps(ARMCC::AL));
}

/// emitThumbRegPlusImmediate - Emits a series of instructions to materialize
/// a destreg = basereg + immediate in Thumb code. Tries a series of ADDs or
/// SUBs first, and uses a constant pool value if the instruction sequence would
/// be too long. This is allowed to modify the condition flags.
void llvm::emitThumbRegPlusImmediate(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator &MBBI,
                                     const DebugLoc &dl, Register DestReg,
                                     Register BaseReg, int NumBytes,
                                     const TargetInstrInfo &TII,
                                     const ARMBaseRegisterInfo &MRI,
                                     unsigned MIFlags) {
  bool isSub = NumBytes < 0;
  unsigned Bytes = (unsigned)NumBytes;
  if (isSub) Bytes = -NumBytes;

  int CopyOpc = 0;
  unsigned CopyBits = 0;
  unsigned CopyScale = 1;
  bool CopyNeedsCC = false;
  int ExtraOpc = 0;
  unsigned ExtraBits = 0;
  unsigned ExtraScale = 1;
  bool ExtraNeedsCC = false;

  // Strategy:
  // We need to select two types of instruction, maximizing the available
  // immediate range of each. The instructions we use will depend on whether
  // DestReg and BaseReg are low, high or the stack pointer.
  // * CopyOpc  - DestReg = BaseReg + imm
  //              This will be emitted once if DestReg != BaseReg, and never if
  //              DestReg == BaseReg.
  // * ExtraOpc - DestReg = DestReg + imm
  //              This will be emitted as many times as necessary to add the
  //              full immediate.
  // If the immediate ranges of these instructions are not large enough to cover
  // NumBytes with a reasonable number of instructions, we fall back to using a
  // value loaded from a constant pool.
  if (DestReg == ARM::SP) {
    if (BaseReg == ARM::SP) {
      // sp -> sp
      // Already in right reg, no copy needed
    } else {
      // low -> sp or high -> sp
      CopyOpc = ARM::tMOVr;
      CopyBits = 0;
    }
    ExtraOpc = isSub ? ARM::tSUBspi : ARM::tADDspi;
    ExtraBits = 7;
    ExtraScale = 4;
  } else if (isARMLowRegister(DestReg)) {
    if (BaseReg == ARM::SP) {
      // sp -> low
      assert(!isSub && "Thumb1 does not have tSUBrSPi");
      CopyOpc = ARM::tADDrSPi;
      CopyBits = 8;
      CopyScale = 4;
    } else if (DestReg == BaseReg) {
      // low -> same low
      // Already in right reg, no copy needed
    } else if (isARMLowRegister(BaseReg)) {
      // low -> different low
      CopyOpc = isSub ? ARM::tSUBi3 : ARM::tADDi3;
      CopyBits = 3;
      CopyNeedsCC = true;
    } else {
      // high -> low
      CopyOpc = ARM::tMOVr;
      CopyBits = 0;
    }
    ExtraOpc = isSub ? ARM::tSUBi8 : ARM::tADDi8;
    ExtraBits = 8;
    ExtraNeedsCC = true;
  } else /* DestReg is high */ {
    if (DestReg == BaseReg) {
      // high -> same high
      // Already in right reg, no copy needed
    } else {
      // {low,high,sp} -> high
      CopyOpc = ARM::tMOVr;
      CopyBits = 0;
    }
    ExtraOpc = 0;
  }

  // We could handle an unaligned immediate with an unaligned copy instruction
  // and an aligned extra instruction, but this case is not currently needed.
  assert(((Bytes & 3) == 0 || ExtraScale == 1) &&
         "Unaligned offset, but all instructions require alignment");

  unsigned CopyRange = ((1 << CopyBits) - 1) * CopyScale;
  // If we would emit the copy with an immediate of 0, just use tMOVr.
  if (CopyOpc && Bytes < CopyScale) {
    CopyOpc = ARM::tMOVr;
    CopyScale = 1;
    CopyNeedsCC = false;
    CopyRange = 0;
  }
  unsigned ExtraRange = ((1 << ExtraBits) - 1) * ExtraScale; // per instruction
  unsigned RequiredCopyInstrs = CopyOpc ? 1 : 0;
  unsigned RangeAfterCopy = (CopyRange > Bytes) ? 0 : (Bytes - CopyRange);

  // We could handle this case when the copy instruction does not require an
  // aligned immediate, but we do not currently do this.
  assert(RangeAfterCopy % ExtraScale == 0 &&
         "Extra instruction requires immediate to be aligned");

  unsigned RequiredExtraInstrs;
  if (ExtraRange)
    RequiredExtraInstrs = alignTo(RangeAfterCopy, ExtraRange) / ExtraRange;
  else if (RangeAfterCopy > 0)
    // We need an extra instruction but none is available
    RequiredExtraInstrs = 1000000;
  else
    RequiredExtraInstrs = 0;
  unsigned RequiredInstrs = RequiredCopyInstrs + RequiredExtraInstrs;
  unsigned Threshold = (DestReg == ARM::SP) ? 3 : 2;

  // Use a constant pool, if the sequence of ADDs/SUBs is too expensive.
  if (RequiredInstrs > Threshold) {
    emitThumbRegPlusImmInReg(MBB, MBBI, dl,
                             DestReg, BaseReg, NumBytes, true,
                             TII, MRI, MIFlags);
    return;
  }

  // Emit zero or one copy instructions
  if (CopyOpc) {
    unsigned CopyImm = std::min(Bytes, CopyRange) / CopyScale;
    Bytes -= CopyImm * CopyScale;

    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, dl, TII.get(CopyOpc), DestReg);
    if (CopyNeedsCC)
      MIB = MIB.add(t1CondCodeOp());
    MIB.addReg(BaseReg, RegState::Kill);
    if (CopyOpc != ARM::tMOVr) {
      MIB.addImm(CopyImm);
    }
    MIB.setMIFlags(MIFlags).add(predOps(ARMCC::AL));

    BaseReg = DestReg;
  }

  // Emit zero or more in-place add/sub instructions
  while (Bytes) {
    unsigned ExtraImm = std::min(Bytes, ExtraRange) / ExtraScale;
    Bytes -= ExtraImm * ExtraScale;

    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, dl, TII.get(ExtraOpc), DestReg);
    if (ExtraNeedsCC)
      MIB = MIB.add(t1CondCodeOp());
    MIB.addReg(BaseReg)
       .addImm(ExtraImm)
       .add(predOps(ARMCC::AL))
       .setMIFlags(MIFlags);
  }
}

static void removeOperands(MachineInstr &MI, unsigned i) {
  unsigned Op = i;
  for (unsigned e = MI.getNumOperands(); i != e; ++i)
    MI.removeOperand(Op);
}

/// convertToNonSPOpcode - Change the opcode to the non-SP version, because
/// we're replacing the frame index with a non-SP register.
static unsigned convertToNonSPOpcode(unsigned Opcode) {
  switch (Opcode) {
  case ARM::tLDRspi:
    return ARM::tLDRi;

  case ARM::tSTRspi:
    return ARM::tSTRi;
  }

  return Opcode;
}

bool ThumbRegisterInfo::rewriteFrameIndex(MachineBasicBlock::iterator II,
                                          unsigned FrameRegIdx,
                                          Register FrameReg, int &Offset,
                                          const ARMBaseInstrInfo &TII) const {
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  assert(MBB.getParent()->getSubtarget<ARMSubtarget>().isThumb1Only() &&
         "This isn't needed for thumb2!");
  DebugLoc dl = MI.getDebugLoc();
  MachineInstrBuilder MIB(*MBB.getParent(), &MI);
  unsigned Opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MI.getDesc();
  unsigned AddrMode = (Desc.TSFlags & ARMII::AddrModeMask);

  if (Opcode == ARM::tADDframe) {
    Offset += MI.getOperand(FrameRegIdx+1).getImm();
    Register DestReg = MI.getOperand(0).getReg();

    emitThumbRegPlusImmediate(MBB, II, dl, DestReg, FrameReg, Offset, TII,
                              *this);
    MBB.erase(II);
    return true;
  } else {
    if (AddrMode != ARMII::AddrModeT1_s)
      llvm_unreachable("Unsupported addressing mode!");

    unsigned ImmIdx = FrameRegIdx + 1;
    int InstrOffs = MI.getOperand(ImmIdx).getImm();
    unsigned NumBits = (FrameReg == ARM::SP) ? 8 : 5;
    unsigned Scale = 4;

    Offset += InstrOffs * Scale;
    assert((Offset & (Scale - 1)) == 0 && "Can't encode this offset!");

    // Common case: small offset, fits into instruction.
    MachineOperand &ImmOp = MI.getOperand(ImmIdx);
    int ImmedOffset = Offset / Scale;
    unsigned Mask = (1 << NumBits) - 1;

    if ((unsigned)Offset <= Mask * Scale) {
      // Replace the FrameIndex with the frame register (e.g., sp).
      Register DestReg = FrameReg;

      // In case FrameReg is a high register, move it to a low reg to ensure it
      // can be used as an operand.
      if (ARM::hGPRRegClass.contains(FrameReg) && FrameReg != ARM::SP) {
        DestReg = MF.getRegInfo().createVirtualRegister(&ARM::tGPRRegClass);
        BuildMI(MBB, II, dl, TII.get(ARM::tMOVr), DestReg)
            .addReg(FrameReg)
            .add(predOps(ARMCC::AL));
      }

      MI.getOperand(FrameRegIdx).ChangeToRegister(DestReg, false);
      ImmOp.ChangeToImmediate(ImmedOffset);

      // If we're using a register where sp was stored, convert the instruction
      // to the non-SP version.
      unsigned NewOpc = convertToNonSPOpcode(Opcode);
      if (NewOpc != Opcode && FrameReg != ARM::SP)
        MI.setDesc(TII.get(NewOpc));

      return true;
    }

    // The offset doesn't fit, but we may be able to put some of the offset into
    // the ldr to simplify the generation of the rest of it.
    NumBits = 5;
    Mask = (1 << NumBits) - 1;
    InstrOffs = 0;
    auto &ST = MF.getSubtarget<ARMSubtarget>();
    // If using the maximum ldr offset will put the rest into the range of a
    // single sp-relative add then do so.
    if (FrameReg == ARM::SP && Offset - (Mask * Scale) <= 1020) {
      InstrOffs = Mask;
    } else if (ST.genExecuteOnly()) {
      // With execute-only the offset is generated either with movw+movt or an
      // add+lsl sequence. If subtracting an offset will make the top half zero
      // then that saves a movt or lsl+add. Otherwise if we don't have movw then
      // we may be able to subtract a value such that it makes the bottom byte
      // zero, saving an add.
      unsigned BottomBits = (Offset / Scale) & Mask;
      bool CanMakeBottomByteZero = ((Offset - BottomBits * Scale) & 0xff) == 0;
      bool TopHalfZero = (Offset & 0xffff0000) == 0;
      bool CanMakeTopHalfZero = ((Offset - Mask * Scale) & 0xffff0000) == 0;
      if (!TopHalfZero && CanMakeTopHalfZero)
        InstrOffs = Mask;
      else if (!ST.useMovt() && CanMakeBottomByteZero)
        InstrOffs = BottomBits;
    }
    ImmOp.ChangeToImmediate(InstrOffs);
    Offset -= InstrOffs * Scale;
  }

  return Offset == 0;
}

void ThumbRegisterInfo::resolveFrameIndex(MachineInstr &MI, Register BaseReg,
                                          int64_t Offset) const {
  const MachineFunction &MF = *MI.getParent()->getParent();
  const ARMSubtarget &STI = MF.getSubtarget<ARMSubtarget>();
  if (!STI.isThumb1Only())
    return ARMBaseRegisterInfo::resolveFrameIndex(MI, BaseReg, Offset);

  const ARMBaseInstrInfo &TII = *STI.getInstrInfo();
  int Off = Offset; // ARM doesn't need the general 64-bit offsets
  unsigned i = 0;

  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }
  bool Done = rewriteFrameIndex(MI, i, BaseReg, Off, TII);
  assert (Done && "Unable to resolve frame index!");
  (void)Done;
}

bool ThumbRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const ARMSubtarget &STI = MF.getSubtarget<ARMSubtarget>();
  if (!STI.isThumb1Only())
    return ARMBaseRegisterInfo::eliminateFrameIndex(II, SPAdj, FIOperandNum,
                                                    RS);

  Register VReg;
  const ARMBaseInstrInfo &TII = *STI.getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  MachineInstrBuilder MIB(*MBB.getParent(), &MI);

  Register FrameReg;
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  const ARMFrameLowering *TFI = getFrameLowering(MF);
  int Offset = TFI->ResolveFrameIndexReference(MF, FrameIndex, FrameReg, SPAdj);

  // PEI::scavengeFrameVirtualRegs() cannot accurately track SPAdj because the
  // call frame setup/destroy instructions have already been eliminated.  That
  // means the stack pointer cannot be used to access the emergency spill slot
  // when !hasReservedCallFrame().
#ifndef NDEBUG
  if (RS && FrameReg == ARM::SP && RS->isScavengingFrameIndex(FrameIndex)){
    assert(STI.getFrameLowering()->hasReservedCallFrame(MF) &&
           "Cannot use SP to access the emergency spill slot in "
           "functions without a reserved call frame");
    assert(!MF.getFrameInfo().hasVarSizedObjects() &&
           "Cannot use SP to access the emergency spill slot in "
           "functions with variable sized frame objects");
  }
#endif // NDEBUG

  // Special handling of dbg_value instructions.
  if (MI.isDebugValue()) {
    MI.getOperand(FIOperandNum).  ChangeToRegister(FrameReg, false /*isDef*/);
    MI.getOperand(FIOperandNum+1).ChangeToImmediate(Offset);
    return false;
  }

  // Modify MI as necessary to handle as much of 'Offset' as possible
  assert(MF.getInfo<ARMFunctionInfo>()->isThumbFunction() &&
         "This eliminateFrameIndex only supports Thumb1!");
  if (rewriteFrameIndex(MI, FIOperandNum, FrameReg, Offset, TII))
    return true;

  // If we get here, the immediate doesn't fit into the instruction.  We folded
  // as much as possible above, handle the rest, providing a register that is
  // SP+LargeImm.
  assert(Offset && "This code isn't needed if offset already handled!");

  unsigned Opcode = MI.getOpcode();

  // Remove predicate first.
  int PIdx = MI.findFirstPredOperandIdx();
  if (PIdx != -1)
    removeOperands(MI, PIdx);

  if (MI.mayLoad()) {
    // Use the destination register to materialize sp + offset.
    Register TmpReg = MI.getOperand(0).getReg();
    bool UseRR = false;
    if (Opcode == ARM::tLDRspi) {
      if (FrameReg == ARM::SP || STI.genExecuteOnly())
        emitThumbRegPlusImmInReg(MBB, II, dl, TmpReg, FrameReg,
                                 Offset, false, TII, *this);
      else {
        emitLoadConstPool(MBB, II, dl, TmpReg, 0, Offset);
        if (!ARM::hGPRRegClass.contains(FrameReg)) {
          UseRR = true;
        } else {
          // If FrameReg is a high register, add the reg values in a separate
          // instruction as the load won't be able to access it.
          BuildMI(MBB, II, dl, TII.get(ARM::tADDhirr), TmpReg)
              .addReg(TmpReg)
              .addReg(FrameReg)
              .add(predOps(ARMCC::AL));
        }
      }
    } else {
      emitThumbRegPlusImmediate(MBB, II, dl, TmpReg, FrameReg, Offset, TII,
                                *this);
    }

    MI.setDesc(TII.get(UseRR ? ARM::tLDRr : ARM::tLDRi));
    MI.getOperand(FIOperandNum).ChangeToRegister(TmpReg, false, false, true);
    if (UseRR) {
      assert(!ARM::hGPRRegClass.contains(FrameReg) &&
             "Thumb1 loads can't use high register");
      // Use [reg, reg] addrmode. Replace the immediate operand w/ the frame
      // register. The offset is already handled in the vreg value.
      MI.getOperand(FIOperandNum+1).ChangeToRegister(FrameReg, false, false,
                                                     false);
    }
  } else if (MI.mayStore()) {
      VReg = MF.getRegInfo().createVirtualRegister(&ARM::tGPRRegClass);
      bool UseRR = false;

      if (Opcode == ARM::tSTRspi) {
        if (FrameReg == ARM::SP || STI.genExecuteOnly())
          emitThumbRegPlusImmInReg(MBB, II, dl, VReg, FrameReg,
                                   Offset, false, TII, *this);
        else {
          emitLoadConstPool(MBB, II, dl, VReg, 0, Offset);
          if (!ARM::hGPRRegClass.contains(FrameReg)) {
            UseRR = true;
          } else {
            // If FrameReg is a high register, add the reg values in a separate
            // instruction as the load won't be able to access it.
            BuildMI(MBB, II, dl, TII.get(ARM::tADDhirr), VReg)
                .addReg(VReg)
                .addReg(FrameReg)
                .add(predOps(ARMCC::AL));
          }
        }
      } else
        emitThumbRegPlusImmediate(MBB, II, dl, VReg, FrameReg, Offset, TII,
                                  *this);
      MI.setDesc(TII.get(UseRR ? ARM::tSTRr : ARM::tSTRi));
      MI.getOperand(FIOperandNum).ChangeToRegister(VReg, false, false, true);
      if (UseRR) {
        assert(!ARM::hGPRRegClass.contains(FrameReg) &&
               "Thumb1 stores can't use high register");
        // Use [reg, reg] addrmode. Replace the immediate operand w/ the frame
        // register. The offset is already handled in the vreg value.
        MI.getOperand(FIOperandNum+1).ChangeToRegister(FrameReg, false, false,
                                                       false);
      }
  } else {
    llvm_unreachable("Unexpected opcode!");
  }

  // Add predicate back if it's needed.
  if (MI.isPredicable())
    MIB.add(predOps(ARMCC::AL));
  return false;
}

bool
ThumbRegisterInfo::useFPForScavengingIndex(const MachineFunction &MF) const {
  if (MF.getSubtarget<ARMSubtarget>().isThumb1Only()) {
    // For Thumb1, the emergency spill slot must be some small positive
    // offset from the base/stack pointer.
    return false;
  }
  // For Thumb2, put the emergency spill slot next to FP.
  return true;
}
