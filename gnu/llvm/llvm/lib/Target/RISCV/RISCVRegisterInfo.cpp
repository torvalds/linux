//===-- RISCVRegisterInfo.cpp - RISC-V Register Information -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the RISC-V implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "RISCVRegisterInfo.h"
#include "RISCV.h"
#include "RISCVMachineFunctionInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_REGINFO_TARGET_DESC
#include "RISCVGenRegisterInfo.inc"

using namespace llvm;

static cl::opt<bool> DisableCostPerUse("riscv-disable-cost-per-use",
                                       cl::init(false), cl::Hidden);
static cl::opt<bool>
    DisableRegAllocHints("riscv-disable-regalloc-hints", cl::Hidden,
                         cl::init(false),
                         cl::desc("Disable two address hints for register "
                                  "allocation"));

static_assert(RISCV::X1 == RISCV::X0 + 1, "Register list not consecutive");
static_assert(RISCV::X31 == RISCV::X0 + 31, "Register list not consecutive");
static_assert(RISCV::F1_H == RISCV::F0_H + 1, "Register list not consecutive");
static_assert(RISCV::F31_H == RISCV::F0_H + 31,
              "Register list not consecutive");
static_assert(RISCV::F1_F == RISCV::F0_F + 1, "Register list not consecutive");
static_assert(RISCV::F31_F == RISCV::F0_F + 31,
              "Register list not consecutive");
static_assert(RISCV::F1_D == RISCV::F0_D + 1, "Register list not consecutive");
static_assert(RISCV::F31_D == RISCV::F0_D + 31,
              "Register list not consecutive");
static_assert(RISCV::V1 == RISCV::V0 + 1, "Register list not consecutive");
static_assert(RISCV::V31 == RISCV::V0 + 31, "Register list not consecutive");

RISCVRegisterInfo::RISCVRegisterInfo(unsigned HwMode)
    : RISCVGenRegisterInfo(RISCV::X1, /*DwarfFlavour*/0, /*EHFlavor*/0,
                           /*PC*/0, HwMode) {}

const MCPhysReg *
RISCVRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  auto &Subtarget = MF->getSubtarget<RISCVSubtarget>();
  if (MF->getFunction().getCallingConv() == CallingConv::GHC)
    return CSR_NoRegs_SaveList;
  if (MF->getFunction().hasFnAttribute("interrupt")) {
    if (Subtarget.hasStdExtD())
      return CSR_XLEN_F64_Interrupt_SaveList;
    if (Subtarget.hasStdExtF())
      return Subtarget.hasStdExtE() ? CSR_XLEN_F32_Interrupt_RVE_SaveList
                                    : CSR_XLEN_F32_Interrupt_SaveList;
    return Subtarget.hasStdExtE() ? CSR_Interrupt_RVE_SaveList
                                  : CSR_Interrupt_SaveList;
  }

  bool HasVectorCSR =
      MF->getFunction().getCallingConv() == CallingConv::RISCV_VectorCall &&
      Subtarget.hasVInstructions();

  switch (Subtarget.getTargetABI()) {
  default:
    llvm_unreachable("Unrecognized ABI");
  case RISCVABI::ABI_ILP32E:
  case RISCVABI::ABI_LP64E:
    return CSR_ILP32E_LP64E_SaveList;
  case RISCVABI::ABI_ILP32:
  case RISCVABI::ABI_LP64:
    if (HasVectorCSR)
      return CSR_ILP32_LP64_V_SaveList;
    return CSR_ILP32_LP64_SaveList;
  case RISCVABI::ABI_ILP32F:
  case RISCVABI::ABI_LP64F:
    if (HasVectorCSR)
      return CSR_ILP32F_LP64F_V_SaveList;
    return CSR_ILP32F_LP64F_SaveList;
  case RISCVABI::ABI_ILP32D:
  case RISCVABI::ABI_LP64D:
    if (HasVectorCSR)
      return CSR_ILP32D_LP64D_V_SaveList;
    return CSR_ILP32D_LP64D_SaveList;
  }
}

BitVector RISCVRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  const RISCVFrameLowering *TFI = getFrameLowering(MF);
  BitVector Reserved(getNumRegs());
  auto &Subtarget = MF.getSubtarget<RISCVSubtarget>();

  for (size_t Reg = 0; Reg < getNumRegs(); Reg++) {
    // Mark any GPRs requested to be reserved as such
    if (Subtarget.isRegisterReservedByUser(Reg))
      markSuperRegs(Reserved, Reg);

    // Mark all the registers defined as constant in TableGen as reserved.
    if (isConstantPhysReg(Reg))
      markSuperRegs(Reserved, Reg);
  }

  // Use markSuperRegs to ensure any register aliases are also reserved
  markSuperRegs(Reserved, RISCV::X2); // sp
  markSuperRegs(Reserved, RISCV::X3); // gp
  markSuperRegs(Reserved, RISCV::X4); // tp
  if (TFI->hasFP(MF))
    markSuperRegs(Reserved, RISCV::X8); // fp
  // Reserve the base register if we need to realign the stack and allocate
  // variable-sized objects at runtime.
  if (TFI->hasBP(MF))
    markSuperRegs(Reserved, RISCVABI::getBPReg()); // bp

  // Additionally reserve dummy register used to form the register pair
  // beginning with 'x0' for instructions that take register pairs.
  markSuperRegs(Reserved, RISCV::DUMMY_REG_PAIR_WITH_X0);

  // There are only 16 GPRs for RVE.
  if (Subtarget.hasStdExtE())
    for (MCPhysReg Reg = RISCV::X16; Reg <= RISCV::X31; Reg++)
      markSuperRegs(Reserved, Reg);

  // V registers for code generation. We handle them manually.
  markSuperRegs(Reserved, RISCV::VL);
  markSuperRegs(Reserved, RISCV::VTYPE);
  markSuperRegs(Reserved, RISCV::VXSAT);
  markSuperRegs(Reserved, RISCV::VXRM);

  // Floating point environment registers.
  markSuperRegs(Reserved, RISCV::FRM);
  markSuperRegs(Reserved, RISCV::FFLAGS);

  // SiFive VCIX state registers.
  markSuperRegs(Reserved, RISCV::VCIX_STATE);

  if (MF.getFunction().getCallingConv() == CallingConv::GRAAL) {
    if (Subtarget.hasStdExtE())
      report_fatal_error("Graal reserved registers do not exist in RVE");
    markSuperRegs(Reserved, RISCV::X23);
    markSuperRegs(Reserved, RISCV::X27);
  }

  // Shadow stack pointer.
  markSuperRegs(Reserved, RISCV::SSP);

  assert(checkAllSuperRegsMarked(Reserved));
  return Reserved;
}

bool RISCVRegisterInfo::isAsmClobberable(const MachineFunction &MF,
                                         MCRegister PhysReg) const {
  return !MF.getSubtarget<RISCVSubtarget>().isRegisterReservedByUser(PhysReg);
}

const uint32_t *RISCVRegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

void RISCVRegisterInfo::adjustReg(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator II,
                                  const DebugLoc &DL, Register DestReg,
                                  Register SrcReg, StackOffset Offset,
                                  MachineInstr::MIFlag Flag,
                                  MaybeAlign RequiredAlign) const {

  if (DestReg == SrcReg && !Offset.getFixed() && !Offset.getScalable())
    return;

  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const RISCVSubtarget &ST = MF.getSubtarget<RISCVSubtarget>();
  const RISCVInstrInfo *TII = ST.getInstrInfo();

  bool KillSrcReg = false;

  if (Offset.getScalable()) {
    unsigned ScalableAdjOpc = RISCV::ADD;
    int64_t ScalableValue = Offset.getScalable();
    if (ScalableValue < 0) {
      ScalableValue = -ScalableValue;
      ScalableAdjOpc = RISCV::SUB;
    }
    // Get vlenb and multiply vlen with the number of vector registers.
    Register ScratchReg = DestReg;
    if (DestReg == SrcReg)
      ScratchReg = MRI.createVirtualRegister(&RISCV::GPRRegClass);

    assert(ScalableValue > 0 && "There is no need to get VLEN scaled value.");
    assert(ScalableValue % 8 == 0 &&
           "Reserve the stack by the multiple of one vector size.");
    assert(isInt<32>(ScalableValue / 8) &&
           "Expect the number of vector registers within 32-bits.");
    uint32_t NumOfVReg = ScalableValue / 8;
    BuildMI(MBB, II, DL, TII->get(RISCV::PseudoReadVLENB), ScratchReg)
        .setMIFlag(Flag);

    if (ScalableAdjOpc == RISCV::ADD && ST.hasStdExtZba() &&
        (NumOfVReg == 2 || NumOfVReg == 4 || NumOfVReg == 8)) {
      unsigned Opc = NumOfVReg == 2 ? RISCV::SH1ADD :
        (NumOfVReg == 4 ? RISCV::SH2ADD : RISCV::SH3ADD);
      BuildMI(MBB, II, DL, TII->get(Opc), DestReg)
          .addReg(ScratchReg, RegState::Kill).addReg(SrcReg)
          .setMIFlag(Flag);
    } else {
      TII->mulImm(MF, MBB, II, DL, ScratchReg, NumOfVReg, Flag);
      BuildMI(MBB, II, DL, TII->get(ScalableAdjOpc), DestReg)
          .addReg(SrcReg).addReg(ScratchReg, RegState::Kill)
          .setMIFlag(Flag);
    }
    SrcReg = DestReg;
    KillSrcReg = true;
  }

  int64_t Val = Offset.getFixed();
  if (DestReg == SrcReg && Val == 0)
    return;

  const uint64_t Align = RequiredAlign.valueOrOne().value();

  if (isInt<12>(Val)) {
    BuildMI(MBB, II, DL, TII->get(RISCV::ADDI), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrcReg))
        .addImm(Val)
        .setMIFlag(Flag);
    return;
  }

  // Try to split the offset across two ADDIs. We need to keep the intermediate
  // result aligned after each ADDI.  We need to determine the maximum value we
  // can put in each ADDI. In the negative direction, we can use -2048 which is
  // always sufficiently aligned. In the positive direction, we need to find the
  // largest 12-bit immediate that is aligned.  Exclude -4096 since it can be
  // created with LUI.
  assert(Align < 2048 && "Required alignment too large");
  int64_t MaxPosAdjStep = 2048 - Align;
  if (Val > -4096 && Val <= (2 * MaxPosAdjStep)) {
    int64_t FirstAdj = Val < 0 ? -2048 : MaxPosAdjStep;
    Val -= FirstAdj;
    BuildMI(MBB, II, DL, TII->get(RISCV::ADDI), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrcReg))
        .addImm(FirstAdj)
        .setMIFlag(Flag);
    BuildMI(MBB, II, DL, TII->get(RISCV::ADDI), DestReg)
        .addReg(DestReg, RegState::Kill)
        .addImm(Val)
        .setMIFlag(Flag);
    return;
  }

  // Use shNadd if doing so lets us materialize a 12 bit immediate with a single
  // instruction.  This saves 1 instruction over the full lui/addi+add fallback
  // path.  We avoid anything which can be done with a single lui as it might
  // be compressible.  Note that the sh1add case is fully covered by the 2x addi
  // case just above and is thus ommitted.
  if (ST.hasStdExtZba() && (Val & 0xFFF) != 0) {
    unsigned Opc = 0;
    if (isShiftedInt<12, 3>(Val)) {
      Opc = RISCV::SH3ADD;
      Val = Val >> 3;
    } else if (isShiftedInt<12, 2>(Val)) {
      Opc = RISCV::SH2ADD;
      Val = Val >> 2;
    }
    if (Opc) {
      Register ScratchReg = MRI.createVirtualRegister(&RISCV::GPRRegClass);
      TII->movImm(MBB, II, DL, ScratchReg, Val, Flag);
      BuildMI(MBB, II, DL, TII->get(Opc), DestReg)
          .addReg(ScratchReg, RegState::Kill)
          .addReg(SrcReg, getKillRegState(KillSrcReg))
          .setMIFlag(Flag);
      return;
    }
  }

  unsigned Opc = RISCV::ADD;
  if (Val < 0) {
    Val = -Val;
    Opc = RISCV::SUB;
  }

  Register ScratchReg = MRI.createVirtualRegister(&RISCV::GPRRegClass);
  TII->movImm(MBB, II, DL, ScratchReg, Val, Flag);
  BuildMI(MBB, II, DL, TII->get(Opc), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrcReg))
      .addReg(ScratchReg, RegState::Kill)
      .setMIFlag(Flag);
}

// Split a VSPILLx_Mx pseudo into multiple whole register stores separated by
// LMUL*VLENB bytes.
void RISCVRegisterInfo::lowerVSPILL(MachineBasicBlock::iterator II) const {
  DebugLoc DL = II->getDebugLoc();
  MachineBasicBlock &MBB = *II->getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const RISCVSubtarget &STI = MF.getSubtarget<RISCVSubtarget>();
  const TargetInstrInfo *TII = STI.getInstrInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();

  auto ZvlssegInfo = RISCV::isRVVSpillForZvlsseg(II->getOpcode());
  unsigned NF = ZvlssegInfo->first;
  unsigned LMUL = ZvlssegInfo->second;
  assert(NF * LMUL <= 8 && "Invalid NF/LMUL combinations.");
  unsigned Opcode, SubRegIdx;
  switch (LMUL) {
  default:
    llvm_unreachable("LMUL must be 1, 2, or 4.");
  case 1:
    Opcode = RISCV::VS1R_V;
    SubRegIdx = RISCV::sub_vrm1_0;
    break;
  case 2:
    Opcode = RISCV::VS2R_V;
    SubRegIdx = RISCV::sub_vrm2_0;
    break;
  case 4:
    Opcode = RISCV::VS4R_V;
    SubRegIdx = RISCV::sub_vrm4_0;
    break;
  }
  static_assert(RISCV::sub_vrm1_7 == RISCV::sub_vrm1_0 + 7,
                "Unexpected subreg numbering");
  static_assert(RISCV::sub_vrm2_3 == RISCV::sub_vrm2_0 + 3,
                "Unexpected subreg numbering");
  static_assert(RISCV::sub_vrm4_1 == RISCV::sub_vrm4_0 + 1,
                "Unexpected subreg numbering");

  Register VL = MRI.createVirtualRegister(&RISCV::GPRRegClass);
  // Optimize for constant VLEN.
  if (auto VLEN = STI.getRealVLen()) {
    const int64_t VLENB = *VLEN / 8;
    int64_t Offset = VLENB * LMUL;
    STI.getInstrInfo()->movImm(MBB, II, DL, VL, Offset);
  } else {
    BuildMI(MBB, II, DL, TII->get(RISCV::PseudoReadVLENB), VL);
    uint32_t ShiftAmount = Log2_32(LMUL);
    if (ShiftAmount != 0)
      BuildMI(MBB, II, DL, TII->get(RISCV::SLLI), VL)
          .addReg(VL)
          .addImm(ShiftAmount);
  }

  Register SrcReg = II->getOperand(0).getReg();
  Register Base = II->getOperand(1).getReg();
  bool IsBaseKill = II->getOperand(1).isKill();
  Register NewBase = MRI.createVirtualRegister(&RISCV::GPRRegClass);
  for (unsigned I = 0; I < NF; ++I) {
    // Adding implicit-use of super register to describe we are using part of
    // super register, that prevents machine verifier complaining when part of
    // subreg is undef, see comment in MachineVerifier::checkLiveness for more
    // detail.
    BuildMI(MBB, II, DL, TII->get(Opcode))
        .addReg(TRI->getSubReg(SrcReg, SubRegIdx + I))
        .addReg(Base, getKillRegState(I == NF - 1))
        .addMemOperand(*(II->memoperands_begin()))
        .addReg(SrcReg, RegState::Implicit);
    if (I != NF - 1)
      BuildMI(MBB, II, DL, TII->get(RISCV::ADD), NewBase)
          .addReg(Base, getKillRegState(I != 0 || IsBaseKill))
          .addReg(VL, getKillRegState(I == NF - 2));
    Base = NewBase;
  }
  II->eraseFromParent();
}

// Split a VSPILLx_Mx pseudo into multiple whole register loads separated by
// LMUL*VLENB bytes.
void RISCVRegisterInfo::lowerVRELOAD(MachineBasicBlock::iterator II) const {
  DebugLoc DL = II->getDebugLoc();
  MachineBasicBlock &MBB = *II->getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const RISCVSubtarget &STI = MF.getSubtarget<RISCVSubtarget>();
  const TargetInstrInfo *TII = STI.getInstrInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();

  auto ZvlssegInfo = RISCV::isRVVSpillForZvlsseg(II->getOpcode());
  unsigned NF = ZvlssegInfo->first;
  unsigned LMUL = ZvlssegInfo->second;
  assert(NF * LMUL <= 8 && "Invalid NF/LMUL combinations.");
  unsigned Opcode, SubRegIdx;
  switch (LMUL) {
  default:
    llvm_unreachable("LMUL must be 1, 2, or 4.");
  case 1:
    Opcode = RISCV::VL1RE8_V;
    SubRegIdx = RISCV::sub_vrm1_0;
    break;
  case 2:
    Opcode = RISCV::VL2RE8_V;
    SubRegIdx = RISCV::sub_vrm2_0;
    break;
  case 4:
    Opcode = RISCV::VL4RE8_V;
    SubRegIdx = RISCV::sub_vrm4_0;
    break;
  }
  static_assert(RISCV::sub_vrm1_7 == RISCV::sub_vrm1_0 + 7,
                "Unexpected subreg numbering");
  static_assert(RISCV::sub_vrm2_3 == RISCV::sub_vrm2_0 + 3,
                "Unexpected subreg numbering");
  static_assert(RISCV::sub_vrm4_1 == RISCV::sub_vrm4_0 + 1,
                "Unexpected subreg numbering");

  Register VL = MRI.createVirtualRegister(&RISCV::GPRRegClass);
  // Optimize for constant VLEN.
  if (auto VLEN = STI.getRealVLen()) {
    const int64_t VLENB = *VLEN / 8;
    int64_t Offset = VLENB * LMUL;
    STI.getInstrInfo()->movImm(MBB, II, DL, VL, Offset);
  } else {
    BuildMI(MBB, II, DL, TII->get(RISCV::PseudoReadVLENB), VL);
    uint32_t ShiftAmount = Log2_32(LMUL);
    if (ShiftAmount != 0)
      BuildMI(MBB, II, DL, TII->get(RISCV::SLLI), VL)
          .addReg(VL)
          .addImm(ShiftAmount);
  }

  Register DestReg = II->getOperand(0).getReg();
  Register Base = II->getOperand(1).getReg();
  bool IsBaseKill = II->getOperand(1).isKill();
  Register NewBase = MRI.createVirtualRegister(&RISCV::GPRRegClass);
  for (unsigned I = 0; I < NF; ++I) {
    BuildMI(MBB, II, DL, TII->get(Opcode),
            TRI->getSubReg(DestReg, SubRegIdx + I))
        .addReg(Base, getKillRegState(I == NF - 1))
        .addMemOperand(*(II->memoperands_begin()));
    if (I != NF - 1)
      BuildMI(MBB, II, DL, TII->get(RISCV::ADD), NewBase)
          .addReg(Base, getKillRegState(I != 0 || IsBaseKill))
          .addReg(VL, getKillRegState(I == NF - 2));
    Base = NewBase;
  }
  II->eraseFromParent();
}

bool RISCVRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                            int SPAdj, unsigned FIOperandNum,
                                            RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected non-zero SPAdj value");

  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const RISCVSubtarget &ST = MF.getSubtarget<RISCVSubtarget>();
  DebugLoc DL = MI.getDebugLoc();

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  Register FrameReg;
  StackOffset Offset =
      getFrameLowering(MF)->getFrameIndexReference(MF, FrameIndex, FrameReg);
  bool IsRVVSpill = RISCV::isRVVSpill(MI);
  if (!IsRVVSpill)
    Offset += StackOffset::getFixed(MI.getOperand(FIOperandNum + 1).getImm());

  if (Offset.getScalable() &&
      ST.getRealMinVLen() == ST.getRealMaxVLen()) {
    // For an exact VLEN value, scalable offsets become constant and thus
    // can be converted entirely into fixed offsets.
    int64_t FixedValue = Offset.getFixed();
    int64_t ScalableValue = Offset.getScalable();
    assert(ScalableValue % 8 == 0 &&
           "Scalable offset is not a multiple of a single vector size.");
    int64_t NumOfVReg = ScalableValue / 8;
    int64_t VLENB = ST.getRealMinVLen() / 8;
    Offset = StackOffset::getFixed(FixedValue + NumOfVReg * VLENB);
  }

  if (!isInt<32>(Offset.getFixed())) {
    report_fatal_error(
        "Frame offsets outside of the signed 32-bit range not supported");
  }

  if (!IsRVVSpill) {
    int64_t Val = Offset.getFixed();
    int64_t Lo12 = SignExtend64<12>(Val);
    unsigned Opc = MI.getOpcode();
    if (Opc == RISCV::ADDI && !isInt<12>(Val)) {
      // We chose to emit the canonical immediate sequence rather than folding
      // the offset into the using add under the theory that doing so doesn't
      // save dynamic instruction count and some target may fuse the canonical
      // 32 bit immediate sequence.  We still need to clear the portion of the
      // offset encoded in the immediate.
      MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
    } else if ((Opc == RISCV::PREFETCH_I || Opc == RISCV::PREFETCH_R ||
                Opc == RISCV::PREFETCH_W) &&
               (Lo12 & 0b11111) != 0) {
      // Prefetch instructions require the offset to be 32 byte aligned.
      MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
    } else if ((Opc == RISCV::PseudoRV32ZdinxLD ||
                Opc == RISCV::PseudoRV32ZdinxSD) &&
               Lo12 >= 2044) {
      // This instruction will be split into 2 instructions. The second
      // instruction will add 4 to the immediate. If that would overflow 12
      // bits, we can't fold the offset.
      MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
    } else {
      // We can encode an add with 12 bit signed immediate in the immediate
      // operand of our user instruction.  As a result, the remaining
      // offset can by construction, at worst, a LUI and a ADD.
      MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Lo12);
      Offset = StackOffset::get((uint64_t)Val - (uint64_t)Lo12,
                                Offset.getScalable());
    }
  }

  if (Offset.getScalable() || Offset.getFixed()) {
    Register DestReg;
    if (MI.getOpcode() == RISCV::ADDI)
      DestReg = MI.getOperand(0).getReg();
    else
      DestReg = MRI.createVirtualRegister(&RISCV::GPRRegClass);
    adjustReg(*II->getParent(), II, DL, DestReg, FrameReg, Offset,
              MachineInstr::NoFlags, std::nullopt);
    MI.getOperand(FIOperandNum).ChangeToRegister(DestReg, /*IsDef*/false,
                                                 /*IsImp*/false,
                                                 /*IsKill*/true);
  } else {
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, /*IsDef*/false,
                                                 /*IsImp*/false,
                                                 /*IsKill*/false);
  }

  // If after materializing the adjustment, we have a pointless ADDI, remove it
  if (MI.getOpcode() == RISCV::ADDI &&
      MI.getOperand(0).getReg() == MI.getOperand(1).getReg() &&
      MI.getOperand(2).getImm() == 0) {
    MI.eraseFromParent();
    return true;
  }

  // Handle spill/fill of synthetic register classes for segment operations to
  // ensure correctness in the edge case one gets spilled. There are many
  // possible optimizations here, but given the extreme rarity of such spills,
  // we prefer simplicity of implementation for now.
  switch (MI.getOpcode()) {
  case RISCV::PseudoVSPILL2_M1:
  case RISCV::PseudoVSPILL2_M2:
  case RISCV::PseudoVSPILL2_M4:
  case RISCV::PseudoVSPILL3_M1:
  case RISCV::PseudoVSPILL3_M2:
  case RISCV::PseudoVSPILL4_M1:
  case RISCV::PseudoVSPILL4_M2:
  case RISCV::PseudoVSPILL5_M1:
  case RISCV::PseudoVSPILL6_M1:
  case RISCV::PseudoVSPILL7_M1:
  case RISCV::PseudoVSPILL8_M1:
    lowerVSPILL(II);
    return true;
  case RISCV::PseudoVRELOAD2_M1:
  case RISCV::PseudoVRELOAD2_M2:
  case RISCV::PseudoVRELOAD2_M4:
  case RISCV::PseudoVRELOAD3_M1:
  case RISCV::PseudoVRELOAD3_M2:
  case RISCV::PseudoVRELOAD4_M1:
  case RISCV::PseudoVRELOAD4_M2:
  case RISCV::PseudoVRELOAD5_M1:
  case RISCV::PseudoVRELOAD6_M1:
  case RISCV::PseudoVRELOAD7_M1:
  case RISCV::PseudoVRELOAD8_M1:
    lowerVRELOAD(II);
    return true;
  }

  return false;
}

bool RISCVRegisterInfo::requiresVirtualBaseRegisters(
    const MachineFunction &MF) const {
  return true;
}

// Returns true if the instruction's frame index reference would be better
// served by a base register other than FP or SP.
// Used by LocalStackSlotAllocation pass to determine which frame index
// references it should create new base registers for.
bool RISCVRegisterInfo::needsFrameBaseReg(MachineInstr *MI,
                                          int64_t Offset) const {
  unsigned FIOperandNum = 0;
  for (; !MI->getOperand(FIOperandNum).isFI(); FIOperandNum++)
    assert(FIOperandNum < MI->getNumOperands() &&
           "Instr doesn't have FrameIndex operand");

  // For RISC-V, The machine instructions that include a FrameIndex operand
  // are load/store, ADDI instructions.
  unsigned MIFrm = RISCVII::getFormat(MI->getDesc().TSFlags);
  if (MIFrm != RISCVII::InstFormatI && MIFrm != RISCVII::InstFormatS)
    return false;
  // We only generate virtual base registers for loads and stores, so
  // return false for everything else.
  if (!MI->mayLoad() && !MI->mayStore())
    return false;

  const MachineFunction &MF = *MI->getMF();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const RISCVFrameLowering *TFI = getFrameLowering(MF);
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  if (TFI->hasFP(MF) && !shouldRealignStack(MF)) {
    auto &Subtarget = MF.getSubtarget<RISCVSubtarget>();
    // Estimate the stack size used to store callee saved registers(
    // excludes reserved registers).
    unsigned CalleeSavedSize = 0;
    for (const MCPhysReg *R = MRI.getCalleeSavedRegs(); MCPhysReg Reg = *R;
         ++R) {
      if (Subtarget.isRegisterReservedByUser(Reg))
        continue;

      if (RISCV::GPRRegClass.contains(Reg))
        CalleeSavedSize += getSpillSize(RISCV::GPRRegClass);
      else if (RISCV::FPR64RegClass.contains(Reg))
        CalleeSavedSize += getSpillSize(RISCV::FPR64RegClass);
      else if (RISCV::FPR32RegClass.contains(Reg))
        CalleeSavedSize += getSpillSize(RISCV::FPR32RegClass);
      // Ignore vector registers.
    }

    int64_t MaxFPOffset = Offset - CalleeSavedSize;
    return !isFrameOffsetLegal(MI, RISCV::X8, MaxFPOffset);
  }

  // Assume 128 bytes spill slots size to estimate the maximum possible
  // offset relative to the stack pointer.
  // FIXME: The 128 is copied from ARM. We should run some statistics and pick a
  // real one for RISC-V.
  int64_t MaxSPOffset = Offset + 128;
  MaxSPOffset += MFI.getLocalFrameSize();
  return !isFrameOffsetLegal(MI, RISCV::X2, MaxSPOffset);
}

// Determine whether a given base register plus offset immediate is
// encodable to resolve a frame index.
bool RISCVRegisterInfo::isFrameOffsetLegal(const MachineInstr *MI,
                                           Register BaseReg,
                                           int64_t Offset) const {
  unsigned FIOperandNum = 0;
  while (!MI->getOperand(FIOperandNum).isFI()) {
    FIOperandNum++;
    assert(FIOperandNum < MI->getNumOperands() &&
           "Instr does not have a FrameIndex operand!");
  }

  Offset += getFrameIndexInstrOffset(MI, FIOperandNum);
  return isInt<12>(Offset);
}

// Insert defining instruction(s) for a pointer to FrameIdx before
// insertion point I.
// Return materialized frame pointer.
Register RISCVRegisterInfo::materializeFrameBaseRegister(MachineBasicBlock *MBB,
                                                         int FrameIdx,
                                                         int64_t Offset) const {
  MachineBasicBlock::iterator MBBI = MBB->begin();
  DebugLoc DL;
  if (MBBI != MBB->end())
    DL = MBBI->getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo &MFI = MF->getRegInfo();
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();

  Register BaseReg = MFI.createVirtualRegister(&RISCV::GPRRegClass);
  BuildMI(*MBB, MBBI, DL, TII->get(RISCV::ADDI), BaseReg)
      .addFrameIndex(FrameIdx)
      .addImm(Offset);
  return BaseReg;
}

// Resolve a frame index operand of an instruction to reference the
// indicated base register plus offset instead.
void RISCVRegisterInfo::resolveFrameIndex(MachineInstr &MI, Register BaseReg,
                                          int64_t Offset) const {
  unsigned FIOperandNum = 0;
  while (!MI.getOperand(FIOperandNum).isFI()) {
    FIOperandNum++;
    assert(FIOperandNum < MI.getNumOperands() &&
           "Instr does not have a FrameIndex operand!");
  }

  Offset += getFrameIndexInstrOffset(&MI, FIOperandNum);
  // FrameIndex Operands are always represented as a
  // register followed by an immediate.
  MI.getOperand(FIOperandNum).ChangeToRegister(BaseReg, false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
}

// Get the offset from the referenced frame index in the instruction,
// if there is one.
int64_t RISCVRegisterInfo::getFrameIndexInstrOffset(const MachineInstr *MI,
                                                    int Idx) const {
  assert((RISCVII::getFormat(MI->getDesc().TSFlags) == RISCVII::InstFormatI ||
          RISCVII::getFormat(MI->getDesc().TSFlags) == RISCVII::InstFormatS) &&
         "The MI must be I or S format.");
  assert(MI->getOperand(Idx).isFI() && "The Idx'th operand of MI is not a "
                                       "FrameIndex operand");
  return MI->getOperand(Idx + 1).getImm();
}

Register RISCVRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? RISCV::X8 : RISCV::X2;
}

const uint32_t *
RISCVRegisterInfo::getCallPreservedMask(const MachineFunction & MF,
                                        CallingConv::ID CC) const {
  auto &Subtarget = MF.getSubtarget<RISCVSubtarget>();

  if (CC == CallingConv::GHC)
    return CSR_NoRegs_RegMask;
  switch (Subtarget.getTargetABI()) {
  default:
    llvm_unreachable("Unrecognized ABI");
  case RISCVABI::ABI_ILP32E:
  case RISCVABI::ABI_LP64E:
    return CSR_ILP32E_LP64E_RegMask;
  case RISCVABI::ABI_ILP32:
  case RISCVABI::ABI_LP64:
    if (CC == CallingConv::RISCV_VectorCall)
      return CSR_ILP32_LP64_V_RegMask;
    return CSR_ILP32_LP64_RegMask;
  case RISCVABI::ABI_ILP32F:
  case RISCVABI::ABI_LP64F:
    if (CC == CallingConv::RISCV_VectorCall)
      return CSR_ILP32F_LP64F_V_RegMask;
    return CSR_ILP32F_LP64F_RegMask;
  case RISCVABI::ABI_ILP32D:
  case RISCVABI::ABI_LP64D:
    if (CC == CallingConv::RISCV_VectorCall)
      return CSR_ILP32D_LP64D_V_RegMask;
    return CSR_ILP32D_LP64D_RegMask;
  }
}

const TargetRegisterClass *
RISCVRegisterInfo::getLargestLegalSuperClass(const TargetRegisterClass *RC,
                                             const MachineFunction &) const {
  if (RC == &RISCV::VMV0RegClass)
    return &RISCV::VRRegClass;
  if (RC == &RISCV::VRNoV0RegClass)
    return &RISCV::VRRegClass;
  if (RC == &RISCV::VRM2NoV0RegClass)
    return &RISCV::VRM2RegClass;
  if (RC == &RISCV::VRM4NoV0RegClass)
    return &RISCV::VRM4RegClass;
  if (RC == &RISCV::VRM8NoV0RegClass)
    return &RISCV::VRM8RegClass;
  return RC;
}

void RISCVRegisterInfo::getOffsetOpcodes(const StackOffset &Offset,
                                         SmallVectorImpl<uint64_t> &Ops) const {
  // VLENB is the length of a vector register in bytes. We use <vscale x 8 x i8>
  // to represent one vector register. The dwarf offset is
  // VLENB * scalable_offset / 8.
  assert(Offset.getScalable() % 8 == 0 && "Invalid frame offset");

  // Add fixed-sized offset using existing DIExpression interface.
  DIExpression::appendOffset(Ops, Offset.getFixed());

  unsigned VLENB = getDwarfRegNum(RISCV::VLENB, true);
  int64_t VLENBSized = Offset.getScalable() / 8;
  if (VLENBSized > 0) {
    Ops.push_back(dwarf::DW_OP_constu);
    Ops.push_back(VLENBSized);
    Ops.append({dwarf::DW_OP_bregx, VLENB, 0ULL});
    Ops.push_back(dwarf::DW_OP_mul);
    Ops.push_back(dwarf::DW_OP_plus);
  } else if (VLENBSized < 0) {
    Ops.push_back(dwarf::DW_OP_constu);
    Ops.push_back(-VLENBSized);
    Ops.append({dwarf::DW_OP_bregx, VLENB, 0ULL});
    Ops.push_back(dwarf::DW_OP_mul);
    Ops.push_back(dwarf::DW_OP_minus);
  }
}

unsigned
RISCVRegisterInfo::getRegisterCostTableIndex(const MachineFunction &MF) const {
  return MF.getSubtarget<RISCVSubtarget>().hasStdExtCOrZca() &&
                 !DisableCostPerUse
             ? 1
             : 0;
}

// Add two address hints to improve chances of being able to use a compressed
// instruction.
bool RISCVRegisterInfo::getRegAllocationHints(
    Register VirtReg, ArrayRef<MCPhysReg> Order,
    SmallVectorImpl<MCPhysReg> &Hints, const MachineFunction &MF,
    const VirtRegMap *VRM, const LiveRegMatrix *Matrix) const {
  const MachineRegisterInfo *MRI = &MF.getRegInfo();
  auto &Subtarget = MF.getSubtarget<RISCVSubtarget>();

  bool BaseImplRetVal = TargetRegisterInfo::getRegAllocationHints(
      VirtReg, Order, Hints, MF, VRM, Matrix);

  if (!VRM || DisableRegAllocHints)
    return BaseImplRetVal;

  // Add any two address hints after any copy hints.
  SmallSet<Register, 4> TwoAddrHints;

  auto tryAddHint = [&](const MachineOperand &VRRegMO, const MachineOperand &MO,
                        bool NeedGPRC) -> void {
    Register Reg = MO.getReg();
    Register PhysReg = Reg.isPhysical() ? Reg : Register(VRM->getPhys(Reg));
    // TODO: Support GPRPair subregisters? Need to be careful with even/odd
    // registers. If the virtual register is an odd register of a pair and the
    // physical register is even (or vice versa), we should not add the hint.
    if (PhysReg && (!NeedGPRC || RISCV::GPRCRegClass.contains(PhysReg)) &&
        !MO.getSubReg() && !VRRegMO.getSubReg()) {
      if (!MRI->isReserved(PhysReg) && !is_contained(Hints, PhysReg))
        TwoAddrHints.insert(PhysReg);
    }
  };

  // This is all of the compressible binary instructions. If an instruction
  // needs GPRC register class operands \p NeedGPRC will be set to true.
  auto isCompressible = [&Subtarget](const MachineInstr &MI, bool &NeedGPRC) {
    NeedGPRC = false;
    switch (MI.getOpcode()) {
    default:
      return false;
    case RISCV::AND:
    case RISCV::OR:
    case RISCV::XOR:
    case RISCV::SUB:
    case RISCV::ADDW:
    case RISCV::SUBW:
      NeedGPRC = true;
      return true;
    case RISCV::ANDI: {
      NeedGPRC = true;
      if (!MI.getOperand(2).isImm())
        return false;
      int64_t Imm = MI.getOperand(2).getImm();
      if (isInt<6>(Imm))
        return true;
      // c.zext.b
      return Subtarget.hasStdExtZcb() && Imm == 255;
    }
    case RISCV::SRAI:
    case RISCV::SRLI:
      NeedGPRC = true;
      return true;
    case RISCV::ADD:
    case RISCV::SLLI:
      return true;
    case RISCV::ADDI:
    case RISCV::ADDIW:
      return MI.getOperand(2).isImm() && isInt<6>(MI.getOperand(2).getImm());
    case RISCV::MUL:
    case RISCV::SEXT_B:
    case RISCV::SEXT_H:
    case RISCV::ZEXT_H_RV32:
    case RISCV::ZEXT_H_RV64:
      // c.mul, c.sext.b, c.sext.h, c.zext.h
      NeedGPRC = true;
      return Subtarget.hasStdExtZcb();
    case RISCV::ADD_UW:
      // c.zext.w
      NeedGPRC = true;
      return Subtarget.hasStdExtZcb() && MI.getOperand(2).isReg() &&
             MI.getOperand(2).getReg() == RISCV::X0;
    case RISCV::XORI:
      // c.not
      NeedGPRC = true;
      return Subtarget.hasStdExtZcb() && MI.getOperand(2).isImm() &&
             MI.getOperand(2).getImm() == -1;
    }
  };

  // Returns true if this operand is compressible. For non-registers it always
  // returns true. Immediate range was already checked in isCompressible.
  // For registers, it checks if the register is a GPRC register. reg-reg
  // instructions that require GPRC need all register operands to be GPRC.
  auto isCompressibleOpnd = [&](const MachineOperand &MO) {
    if (!MO.isReg())
      return true;
    Register Reg = MO.getReg();
    Register PhysReg = Reg.isPhysical() ? Reg : Register(VRM->getPhys(Reg));
    return PhysReg && RISCV::GPRCRegClass.contains(PhysReg);
  };

  for (auto &MO : MRI->reg_nodbg_operands(VirtReg)) {
    const MachineInstr &MI = *MO.getParent();
    unsigned OpIdx = MO.getOperandNo();
    bool NeedGPRC;
    if (isCompressible(MI, NeedGPRC)) {
      if (OpIdx == 0 && MI.getOperand(1).isReg()) {
        if (!NeedGPRC || MI.getNumExplicitOperands() < 3 ||
            MI.getOpcode() == RISCV::ADD_UW ||
            isCompressibleOpnd(MI.getOperand(2)))
          tryAddHint(MO, MI.getOperand(1), NeedGPRC);
        if (MI.isCommutable() && MI.getOperand(2).isReg() &&
            (!NeedGPRC || isCompressibleOpnd(MI.getOperand(1))))
          tryAddHint(MO, MI.getOperand(2), NeedGPRC);
      } else if (OpIdx == 1 && (!NeedGPRC || MI.getNumExplicitOperands() < 3 ||
                                isCompressibleOpnd(MI.getOperand(2)))) {
        tryAddHint(MO, MI.getOperand(0), NeedGPRC);
      } else if (MI.isCommutable() && OpIdx == 2 &&
                 (!NeedGPRC || isCompressibleOpnd(MI.getOperand(1)))) {
        tryAddHint(MO, MI.getOperand(0), NeedGPRC);
      }
    }
  }

  for (MCPhysReg OrderReg : Order)
    if (TwoAddrHints.count(OrderReg))
      Hints.push_back(OrderReg);

  return BaseImplRetVal;
}
