//===-- SIShrinkInstructions.cpp - Shrink Instructions --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
/// The pass tries to use the 32-bit encoding for instructions when possible.
//===----------------------------------------------------------------------===//
//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

#define DEBUG_TYPE "si-shrink-instructions"

STATISTIC(NumInstructionsShrunk,
          "Number of 64-bit instruction reduced to 32-bit.");
STATISTIC(NumLiteralConstantsFolded,
          "Number of literal constants folded into 32-bit instructions.");

using namespace llvm;

namespace {

class SIShrinkInstructions : public MachineFunctionPass {
  MachineFunction *MF;
  MachineRegisterInfo *MRI;
  const GCNSubtarget *ST;
  const SIInstrInfo *TII;
  const SIRegisterInfo *TRI;

public:
  static char ID;

public:
  SIShrinkInstructions() : MachineFunctionPass(ID) {
  }

  bool foldImmediates(MachineInstr &MI, bool TryToCommute = true) const;
  bool shouldShrinkTrue16(MachineInstr &MI) const;
  bool isKImmOperand(const MachineOperand &Src) const;
  bool isKUImmOperand(const MachineOperand &Src) const;
  bool isKImmOrKUImmOperand(const MachineOperand &Src, bool &IsUnsigned) const;
  void copyExtraImplicitOps(MachineInstr &NewMI, MachineInstr &MI) const;
  void shrinkScalarCompare(MachineInstr &MI) const;
  void shrinkMIMG(MachineInstr &MI) const;
  void shrinkMadFma(MachineInstr &MI) const;
  bool shrinkScalarLogicOp(MachineInstr &MI) const;
  bool tryReplaceDeadSDST(MachineInstr &MI) const;
  bool instAccessReg(iterator_range<MachineInstr::const_mop_iterator> &&R,
                     Register Reg, unsigned SubReg) const;
  bool instReadsReg(const MachineInstr *MI, unsigned Reg,
                    unsigned SubReg) const;
  bool instModifiesReg(const MachineInstr *MI, unsigned Reg,
                       unsigned SubReg) const;
  TargetInstrInfo::RegSubRegPair getSubRegForIndex(Register Reg, unsigned Sub,
                                                   unsigned I) const;
  void dropInstructionKeepingImpDefs(MachineInstr &MI) const;
  MachineInstr *matchSwap(MachineInstr &MovT) const;

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "SI Shrink Instructions"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // End anonymous namespace.

INITIALIZE_PASS(SIShrinkInstructions, DEBUG_TYPE,
                "SI Shrink Instructions", false, false)

char SIShrinkInstructions::ID = 0;

FunctionPass *llvm::createSIShrinkInstructionsPass() {
  return new SIShrinkInstructions();
}

/// This function checks \p MI for operands defined by a move immediate
/// instruction and then folds the literal constant into the instruction if it
/// can. This function assumes that \p MI is a VOP1, VOP2, or VOPC instructions.
bool SIShrinkInstructions::foldImmediates(MachineInstr &MI,
                                          bool TryToCommute) const {
  assert(TII->isVOP1(MI) || TII->isVOP2(MI) || TII->isVOPC(MI));

  int Src0Idx = AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::src0);

  // Try to fold Src0
  MachineOperand &Src0 = MI.getOperand(Src0Idx);
  if (Src0.isReg()) {
    Register Reg = Src0.getReg();
    if (Reg.isVirtual()) {
      MachineInstr *Def = MRI->getUniqueVRegDef(Reg);
      if (Def && Def->isMoveImmediate()) {
        MachineOperand &MovSrc = Def->getOperand(1);
        bool ConstantFolded = false;

        if (TII->isOperandLegal(MI, Src0Idx, &MovSrc)) {
          if (MovSrc.isImm()) {
            Src0.ChangeToImmediate(MovSrc.getImm());
            ConstantFolded = true;
          } else if (MovSrc.isFI()) {
            Src0.ChangeToFrameIndex(MovSrc.getIndex());
            ConstantFolded = true;
          } else if (MovSrc.isGlobal()) {
            Src0.ChangeToGA(MovSrc.getGlobal(), MovSrc.getOffset(),
                            MovSrc.getTargetFlags());
            ConstantFolded = true;
          }
        }

        if (ConstantFolded) {
          if (MRI->use_nodbg_empty(Reg))
            Def->eraseFromParent();
          ++NumLiteralConstantsFolded;
          return true;
        }
      }
    }
  }

  // We have failed to fold src0, so commute the instruction and try again.
  if (TryToCommute && MI.isCommutable()) {
    if (TII->commuteInstruction(MI)) {
      if (foldImmediates(MI, false))
        return true;

      // Commute back.
      TII->commuteInstruction(MI);
    }
  }

  return false;
}

/// Do not shrink the instruction if its registers are not expressible in the
/// shrunk encoding.
bool SIShrinkInstructions::shouldShrinkTrue16(MachineInstr &MI) const {
  for (unsigned I = 0, E = MI.getNumExplicitOperands(); I != E; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (MO.isReg()) {
      Register Reg = MO.getReg();
      assert(!Reg.isVirtual() && "Prior checks should ensure we only shrink "
                                 "True16 Instructions post-RA");
      if (AMDGPU::VGPR_32RegClass.contains(Reg) &&
          !AMDGPU::VGPR_32_Lo128RegClass.contains(Reg))
        return false;
    }
  }
  return true;
}

bool SIShrinkInstructions::isKImmOperand(const MachineOperand &Src) const {
  return isInt<16>(SignExtend64(Src.getImm(), 32)) &&
         !TII->isInlineConstant(*Src.getParent(), Src.getOperandNo());
}

bool SIShrinkInstructions::isKUImmOperand(const MachineOperand &Src) const {
  return isUInt<16>(Src.getImm()) &&
         !TII->isInlineConstant(*Src.getParent(), Src.getOperandNo());
}

bool SIShrinkInstructions::isKImmOrKUImmOperand(const MachineOperand &Src,
                                                bool &IsUnsigned) const {
  if (isInt<16>(SignExtend64(Src.getImm(), 32))) {
    IsUnsigned = false;
    return !TII->isInlineConstant(Src);
  }

  if (isUInt<16>(Src.getImm())) {
    IsUnsigned = true;
    return !TII->isInlineConstant(Src);
  }

  return false;
}

/// \returns the opcode of an instruction a move immediate of the constant \p
/// Src can be replaced with if the constant is replaced with \p ModifiedImm.
/// i.e.
///
/// If the bitreverse of a constant is an inline immediate, reverse the
/// immediate and return the bitreverse opcode.
///
/// If the bitwise negation of a constant is an inline immediate, reverse the
/// immediate and return the bitwise not opcode.
static unsigned canModifyToInlineImmOp32(const SIInstrInfo *TII,
                                         const MachineOperand &Src,
                                         int32_t &ModifiedImm, bool Scalar) {
  if (TII->isInlineConstant(Src))
    return 0;
  int32_t SrcImm = static_cast<int32_t>(Src.getImm());

  if (!Scalar) {
    // We could handle the scalar case with here, but we would need to check
    // that SCC is not live as S_NOT_B32 clobbers it. It's probably not worth
    // it, as the reasonable values are already covered by s_movk_i32.
    ModifiedImm = ~SrcImm;
    if (TII->isInlineConstant(APInt(32, ModifiedImm)))
      return AMDGPU::V_NOT_B32_e32;
  }

  ModifiedImm = reverseBits<int32_t>(SrcImm);
  if (TII->isInlineConstant(APInt(32, ModifiedImm)))
    return Scalar ? AMDGPU::S_BREV_B32 : AMDGPU::V_BFREV_B32_e32;

  return 0;
}

/// Copy implicit register operands from specified instruction to this
/// instruction that are not part of the instruction definition.
void SIShrinkInstructions::copyExtraImplicitOps(MachineInstr &NewMI,
                                                MachineInstr &MI) const {
  MachineFunction &MF = *MI.getMF();
  for (unsigned i = MI.getDesc().getNumOperands() +
                    MI.getDesc().implicit_uses().size() +
                    MI.getDesc().implicit_defs().size(),
                e = MI.getNumOperands();
       i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if ((MO.isReg() && MO.isImplicit()) || MO.isRegMask())
      NewMI.addOperand(MF, MO);
  }
}

void SIShrinkInstructions::shrinkScalarCompare(MachineInstr &MI) const {
  if (!ST->hasSCmpK())
    return;

  // cmpk instructions do scc = dst <cc op> imm16, so commute the instruction to
  // get constants on the RHS.
  if (!MI.getOperand(0).isReg())
    TII->commuteInstruction(MI, false, 0, 1);

  // cmpk requires src0 to be a register
  const MachineOperand &Src0 = MI.getOperand(0);
  if (!Src0.isReg())
    return;

  MachineOperand &Src1 = MI.getOperand(1);
  if (!Src1.isImm())
    return;

  int SOPKOpc = AMDGPU::getSOPKOp(MI.getOpcode());
  if (SOPKOpc == -1)
    return;

  // eq/ne is special because the imm16 can be treated as signed or unsigned,
  // and initially selected to the unsigned versions.
  if (SOPKOpc == AMDGPU::S_CMPK_EQ_U32 || SOPKOpc == AMDGPU::S_CMPK_LG_U32) {
    bool HasUImm;
    if (isKImmOrKUImmOperand(Src1, HasUImm)) {
      if (!HasUImm) {
        SOPKOpc = (SOPKOpc == AMDGPU::S_CMPK_EQ_U32) ?
          AMDGPU::S_CMPK_EQ_I32 : AMDGPU::S_CMPK_LG_I32;
        Src1.setImm(SignExtend32(Src1.getImm(), 32));
      }

      MI.setDesc(TII->get(SOPKOpc));
    }

    return;
  }

  const MCInstrDesc &NewDesc = TII->get(SOPKOpc);

  if ((SIInstrInfo::sopkIsZext(SOPKOpc) && isKUImmOperand(Src1)) ||
      (!SIInstrInfo::sopkIsZext(SOPKOpc) && isKImmOperand(Src1))) {
    if (!SIInstrInfo::sopkIsZext(SOPKOpc))
      Src1.setImm(SignExtend64(Src1.getImm(), 32));
    MI.setDesc(NewDesc);
  }
}

// Shrink NSA encoded instructions with contiguous VGPRs to non-NSA encoding.
void SIShrinkInstructions::shrinkMIMG(MachineInstr &MI) const {
  const AMDGPU::MIMGInfo *Info = AMDGPU::getMIMGInfo(MI.getOpcode());
  if (!Info)
    return;

  uint8_t NewEncoding;
  switch (Info->MIMGEncoding) {
  case AMDGPU::MIMGEncGfx10NSA:
    NewEncoding = AMDGPU::MIMGEncGfx10Default;
    break;
  case AMDGPU::MIMGEncGfx11NSA:
    NewEncoding = AMDGPU::MIMGEncGfx11Default;
    break;
  default:
    return;
  }

  int VAddr0Idx =
      AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::vaddr0);
  unsigned NewAddrDwords = Info->VAddrDwords;
  const TargetRegisterClass *RC;

  if (Info->VAddrDwords == 2) {
    RC = &AMDGPU::VReg_64RegClass;
  } else if (Info->VAddrDwords == 3) {
    RC = &AMDGPU::VReg_96RegClass;
  } else if (Info->VAddrDwords == 4) {
    RC = &AMDGPU::VReg_128RegClass;
  } else if (Info->VAddrDwords == 5) {
    RC = &AMDGPU::VReg_160RegClass;
  } else if (Info->VAddrDwords == 6) {
    RC = &AMDGPU::VReg_192RegClass;
  } else if (Info->VAddrDwords == 7) {
    RC = &AMDGPU::VReg_224RegClass;
  } else if (Info->VAddrDwords == 8) {
    RC = &AMDGPU::VReg_256RegClass;
  } else if (Info->VAddrDwords == 9) {
    RC = &AMDGPU::VReg_288RegClass;
  } else if (Info->VAddrDwords == 10) {
    RC = &AMDGPU::VReg_320RegClass;
  } else if (Info->VAddrDwords == 11) {
    RC = &AMDGPU::VReg_352RegClass;
  } else if (Info->VAddrDwords == 12) {
    RC = &AMDGPU::VReg_384RegClass;
  } else {
    RC = &AMDGPU::VReg_512RegClass;
    NewAddrDwords = 16;
  }

  unsigned VgprBase = 0;
  unsigned NextVgpr = 0;
  bool IsUndef = true;
  bool IsKill = NewAddrDwords == Info->VAddrDwords;
  const unsigned NSAMaxSize = ST->getNSAMaxSize();
  const bool IsPartialNSA = NewAddrDwords > NSAMaxSize;
  const unsigned EndVAddr = IsPartialNSA ? NSAMaxSize : Info->VAddrOperands;
  for (unsigned Idx = 0; Idx < EndVAddr; ++Idx) {
    const MachineOperand &Op = MI.getOperand(VAddr0Idx + Idx);
    unsigned Vgpr = TRI->getHWRegIndex(Op.getReg());
    unsigned Dwords = TRI->getRegSizeInBits(Op.getReg(), *MRI) / 32;
    assert(Dwords > 0 && "Un-implemented for less than 32 bit regs");

    if (Idx == 0) {
      VgprBase = Vgpr;
      NextVgpr = Vgpr + Dwords;
    } else if (Vgpr == NextVgpr) {
      NextVgpr = Vgpr + Dwords;
    } else {
      return;
    }

    if (!Op.isUndef())
      IsUndef = false;
    if (!Op.isKill())
      IsKill = false;
  }

  if (VgprBase + NewAddrDwords > 256)
    return;

  // Further check for implicit tied operands - this may be present if TFE is
  // enabled
  int TFEIdx = AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::tfe);
  int LWEIdx = AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::lwe);
  unsigned TFEVal = (TFEIdx == -1) ? 0 : MI.getOperand(TFEIdx).getImm();
  unsigned LWEVal = (LWEIdx == -1) ? 0 : MI.getOperand(LWEIdx).getImm();
  int ToUntie = -1;
  if (TFEVal || LWEVal) {
    // TFE/LWE is enabled so we need to deal with an implicit tied operand
    for (unsigned i = LWEIdx + 1, e = MI.getNumOperands(); i != e; ++i) {
      if (MI.getOperand(i).isReg() && MI.getOperand(i).isTied() &&
          MI.getOperand(i).isImplicit()) {
        // This is the tied operand
        assert(
            ToUntie == -1 &&
            "found more than one tied implicit operand when expecting only 1");
        ToUntie = i;
        MI.untieRegOperand(ToUntie);
      }
    }
  }

  unsigned NewOpcode = AMDGPU::getMIMGOpcode(Info->BaseOpcode, NewEncoding,
                                             Info->VDataDwords, NewAddrDwords);
  MI.setDesc(TII->get(NewOpcode));
  MI.getOperand(VAddr0Idx).setReg(RC->getRegister(VgprBase));
  MI.getOperand(VAddr0Idx).setIsUndef(IsUndef);
  MI.getOperand(VAddr0Idx).setIsKill(IsKill);

  for (unsigned i = 1; i < EndVAddr; ++i)
    MI.removeOperand(VAddr0Idx + 1);

  if (ToUntie >= 0) {
    MI.tieOperands(
        AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::vdata),
        ToUntie - (EndVAddr - 1));
  }
}

// Shrink MAD to MADAK/MADMK and FMA to FMAAK/FMAMK.
void SIShrinkInstructions::shrinkMadFma(MachineInstr &MI) const {
  // Pre-GFX10 VOP3 instructions like MAD/FMA cannot take a literal operand so
  // there is no reason to try to shrink them.
  if (!ST->hasVOP3Literal())
    return;

  // There is no advantage to doing this pre-RA.
  if (!MF->getProperties().hasProperty(
          MachineFunctionProperties::Property::NoVRegs))
    return;

  if (TII->hasAnyModifiersSet(MI))
    return;

  const unsigned Opcode = MI.getOpcode();
  MachineOperand &Src0 = *TII->getNamedOperand(MI, AMDGPU::OpName::src0);
  MachineOperand &Src1 = *TII->getNamedOperand(MI, AMDGPU::OpName::src1);
  MachineOperand &Src2 = *TII->getNamedOperand(MI, AMDGPU::OpName::src2);
  unsigned NewOpcode = AMDGPU::INSTRUCTION_LIST_END;

  bool Swap;

  // Detect "Dst = VSrc * VGPR + Imm" and convert to AK form.
  if (Src2.isImm() && !TII->isInlineConstant(Src2)) {
    if (Src1.isReg() && TRI->isVGPR(*MRI, Src1.getReg()))
      Swap = false;
    else if (Src0.isReg() && TRI->isVGPR(*MRI, Src0.getReg()))
      Swap = true;
    else
      return;

    switch (Opcode) {
    default:
      llvm_unreachable("Unexpected mad/fma opcode!");
    case AMDGPU::V_MAD_F32_e64:
      NewOpcode = AMDGPU::V_MADAK_F32;
      break;
    case AMDGPU::V_FMA_F32_e64:
      NewOpcode = AMDGPU::V_FMAAK_F32;
      break;
    case AMDGPU::V_MAD_F16_e64:
      NewOpcode = AMDGPU::V_MADAK_F16;
      break;
    case AMDGPU::V_FMA_F16_e64:
    case AMDGPU::V_FMA_F16_gfx9_e64:
      NewOpcode = ST->hasTrue16BitInsts() ? AMDGPU::V_FMAAK_F16_t16
                                          : AMDGPU::V_FMAAK_F16;
      break;
    }
  }

  // Detect "Dst = VSrc * Imm + VGPR" and convert to MK form.
  if (Src2.isReg() && TRI->isVGPR(*MRI, Src2.getReg())) {
    if (Src1.isImm() && !TII->isInlineConstant(Src1))
      Swap = false;
    else if (Src0.isImm() && !TII->isInlineConstant(Src0))
      Swap = true;
    else
      return;

    switch (Opcode) {
    default:
      llvm_unreachable("Unexpected mad/fma opcode!");
    case AMDGPU::V_MAD_F32_e64:
      NewOpcode = AMDGPU::V_MADMK_F32;
      break;
    case AMDGPU::V_FMA_F32_e64:
      NewOpcode = AMDGPU::V_FMAMK_F32;
      break;
    case AMDGPU::V_MAD_F16_e64:
      NewOpcode = AMDGPU::V_MADMK_F16;
      break;
    case AMDGPU::V_FMA_F16_e64:
    case AMDGPU::V_FMA_F16_gfx9_e64:
      NewOpcode = ST->hasTrue16BitInsts() ? AMDGPU::V_FMAMK_F16_t16
                                          : AMDGPU::V_FMAMK_F16;
      break;
    }
  }

  if (NewOpcode == AMDGPU::INSTRUCTION_LIST_END)
    return;

  if (AMDGPU::isTrue16Inst(NewOpcode) && !shouldShrinkTrue16(MI))
    return;

  if (Swap) {
    // Swap Src0 and Src1 by building a new instruction.
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII->get(NewOpcode),
            MI.getOperand(0).getReg())
        .add(Src1)
        .add(Src0)
        .add(Src2)
        .setMIFlags(MI.getFlags());
    MI.eraseFromParent();
  } else {
    TII->removeModOperands(MI);
    MI.setDesc(TII->get(NewOpcode));
  }
}

/// Attempt to shrink AND/OR/XOR operations requiring non-inlineable literals.
/// For AND or OR, try using S_BITSET{0,1} to clear or set bits.
/// If the inverse of the immediate is legal, use ANDN2, ORN2 or
/// XNOR (as a ^ b == ~(a ^ ~b)).
/// \returns true if the caller should continue the machine function iterator
bool SIShrinkInstructions::shrinkScalarLogicOp(MachineInstr &MI) const {
  unsigned Opc = MI.getOpcode();
  const MachineOperand *Dest = &MI.getOperand(0);
  MachineOperand *Src0 = &MI.getOperand(1);
  MachineOperand *Src1 = &MI.getOperand(2);
  MachineOperand *SrcReg = Src0;
  MachineOperand *SrcImm = Src1;

  if (!SrcImm->isImm() ||
      AMDGPU::isInlinableLiteral32(SrcImm->getImm(), ST->hasInv2PiInlineImm()))
    return false;

  uint32_t Imm = static_cast<uint32_t>(SrcImm->getImm());
  uint32_t NewImm = 0;

  if (Opc == AMDGPU::S_AND_B32) {
    if (isPowerOf2_32(~Imm)) {
      NewImm = llvm::countr_one(Imm);
      Opc = AMDGPU::S_BITSET0_B32;
    } else if (AMDGPU::isInlinableLiteral32(~Imm, ST->hasInv2PiInlineImm())) {
      NewImm = ~Imm;
      Opc = AMDGPU::S_ANDN2_B32;
    }
  } else if (Opc == AMDGPU::S_OR_B32) {
    if (isPowerOf2_32(Imm)) {
      NewImm = llvm::countr_zero(Imm);
      Opc = AMDGPU::S_BITSET1_B32;
    } else if (AMDGPU::isInlinableLiteral32(~Imm, ST->hasInv2PiInlineImm())) {
      NewImm = ~Imm;
      Opc = AMDGPU::S_ORN2_B32;
    }
  } else if (Opc == AMDGPU::S_XOR_B32) {
    if (AMDGPU::isInlinableLiteral32(~Imm, ST->hasInv2PiInlineImm())) {
      NewImm = ~Imm;
      Opc = AMDGPU::S_XNOR_B32;
    }
  } else {
    llvm_unreachable("unexpected opcode");
  }

  if (NewImm != 0) {
    if (Dest->getReg().isVirtual() && SrcReg->isReg()) {
      MRI->setRegAllocationHint(Dest->getReg(), 0, SrcReg->getReg());
      MRI->setRegAllocationHint(SrcReg->getReg(), 0, Dest->getReg());
      return true;
    }

    if (SrcReg->isReg() && SrcReg->getReg() == Dest->getReg()) {
      const bool IsUndef = SrcReg->isUndef();
      const bool IsKill = SrcReg->isKill();
      MI.setDesc(TII->get(Opc));
      if (Opc == AMDGPU::S_BITSET0_B32 ||
          Opc == AMDGPU::S_BITSET1_B32) {
        Src0->ChangeToImmediate(NewImm);
        // Remove the immediate and add the tied input.
        MI.getOperand(2).ChangeToRegister(Dest->getReg(), /*IsDef*/ false,
                                          /*isImp*/ false, IsKill,
                                          /*isDead*/ false, IsUndef);
        MI.tieOperands(0, 2);
      } else {
        SrcImm->setImm(NewImm);
      }
    }
  }

  return false;
}

// This is the same as MachineInstr::readsRegister/modifiesRegister except
// it takes subregs into account.
bool SIShrinkInstructions::instAccessReg(
    iterator_range<MachineInstr::const_mop_iterator> &&R, Register Reg,
    unsigned SubReg) const {
  for (const MachineOperand &MO : R) {
    if (!MO.isReg())
      continue;

    if (Reg.isPhysical() && MO.getReg().isPhysical()) {
      if (TRI->regsOverlap(Reg, MO.getReg()))
        return true;
    } else if (MO.getReg() == Reg && Reg.isVirtual()) {
      LaneBitmask Overlap = TRI->getSubRegIndexLaneMask(SubReg) &
                            TRI->getSubRegIndexLaneMask(MO.getSubReg());
      if (Overlap.any())
        return true;
    }
  }
  return false;
}

bool SIShrinkInstructions::instReadsReg(const MachineInstr *MI, unsigned Reg,
                                        unsigned SubReg) const {
  return instAccessReg(MI->uses(), Reg, SubReg);
}

bool SIShrinkInstructions::instModifiesReg(const MachineInstr *MI, unsigned Reg,
                                           unsigned SubReg) const {
  return instAccessReg(MI->defs(), Reg, SubReg);
}

TargetInstrInfo::RegSubRegPair
SIShrinkInstructions::getSubRegForIndex(Register Reg, unsigned Sub,
                                        unsigned I) const {
  if (TRI->getRegSizeInBits(Reg, *MRI) != 32) {
    if (Reg.isPhysical()) {
      Reg = TRI->getSubReg(Reg, TRI->getSubRegFromChannel(I));
    } else {
      Sub = TRI->getSubRegFromChannel(I + TRI->getChannelFromSubReg(Sub));
    }
  }
  return TargetInstrInfo::RegSubRegPair(Reg, Sub);
}

void SIShrinkInstructions::dropInstructionKeepingImpDefs(
    MachineInstr &MI) const {
  for (unsigned i = MI.getDesc().getNumOperands() +
                    MI.getDesc().implicit_uses().size() +
                    MI.getDesc().implicit_defs().size(),
                e = MI.getNumOperands();
       i != e; ++i) {
    const MachineOperand &Op = MI.getOperand(i);
    if (!Op.isDef())
      continue;
    BuildMI(*MI.getParent(), MI.getIterator(), MI.getDebugLoc(),
            TII->get(AMDGPU::IMPLICIT_DEF), Op.getReg());
  }

  MI.eraseFromParent();
}

// Match:
// mov t, x
// mov x, y
// mov y, t
//
// =>
//
// mov t, x (t is potentially dead and move eliminated)
// v_swap_b32 x, y
//
// Returns next valid instruction pointer if was able to create v_swap_b32.
//
// This shall not be done too early not to prevent possible folding which may
// remove matched moves, and this should preferably be done before RA to
// release saved registers and also possibly after RA which can insert copies
// too.
//
// This is really just a generic peephole that is not a canonical shrinking,
// although requirements match the pass placement and it reduces code size too.
MachineInstr *SIShrinkInstructions::matchSwap(MachineInstr &MovT) const {
  assert(MovT.getOpcode() == AMDGPU::V_MOV_B32_e32 ||
         MovT.getOpcode() == AMDGPU::COPY);

  Register T = MovT.getOperand(0).getReg();
  unsigned Tsub = MovT.getOperand(0).getSubReg();
  MachineOperand &Xop = MovT.getOperand(1);

  if (!Xop.isReg())
    return nullptr;
  Register X = Xop.getReg();
  unsigned Xsub = Xop.getSubReg();

  unsigned Size = TII->getOpSize(MovT, 0) / 4;

  if (!TRI->isVGPR(*MRI, X))
    return nullptr;

  const unsigned SearchLimit = 16;
  unsigned Count = 0;
  bool KilledT = false;
  for (auto Iter = std::next(MovT.getIterator()),
            E = MovT.getParent()->instr_end();
       Iter != E && Count < SearchLimit && !KilledT; ++Iter, ++Count) {

    MachineInstr *MovY = &*Iter;
    KilledT = MovY->killsRegister(T, TRI);

    if ((MovY->getOpcode() != AMDGPU::V_MOV_B32_e32 &&
         MovY->getOpcode() != AMDGPU::COPY) ||
        !MovY->getOperand(1).isReg()        ||
        MovY->getOperand(1).getReg() != T   ||
        MovY->getOperand(1).getSubReg() != Tsub)
      continue;

    Register Y = MovY->getOperand(0).getReg();
    unsigned Ysub = MovY->getOperand(0).getSubReg();

    if (!TRI->isVGPR(*MRI, Y))
      continue;

    MachineInstr *MovX = nullptr;
    for (auto IY = MovY->getIterator(), I = std::next(MovT.getIterator());
         I != IY; ++I) {
      if (instReadsReg(&*I, X, Xsub) || instModifiesReg(&*I, Y, Ysub) ||
          instModifiesReg(&*I, T, Tsub) ||
          (MovX && instModifiesReg(&*I, X, Xsub))) {
        MovX = nullptr;
        break;
      }
      if (!instReadsReg(&*I, Y, Ysub)) {
        if (!MovX && instModifiesReg(&*I, X, Xsub)) {
          MovX = nullptr;
          break;
        }
        continue;
      }
      if (MovX ||
          (I->getOpcode() != AMDGPU::V_MOV_B32_e32 &&
           I->getOpcode() != AMDGPU::COPY) ||
          I->getOperand(0).getReg() != X ||
          I->getOperand(0).getSubReg() != Xsub) {
        MovX = nullptr;
        break;
      }

      if (Size > 1 && (I->getNumImplicitOperands() > (I->isCopy() ? 0U : 1U)))
        continue;

      MovX = &*I;
    }

    if (!MovX)
      continue;

    LLVM_DEBUG(dbgs() << "Matched v_swap_b32:\n" << MovT << *MovX << *MovY);

    for (unsigned I = 0; I < Size; ++I) {
      TargetInstrInfo::RegSubRegPair X1, Y1;
      X1 = getSubRegForIndex(X, Xsub, I);
      Y1 = getSubRegForIndex(Y, Ysub, I);
      MachineBasicBlock &MBB = *MovT.getParent();
      auto MIB = BuildMI(MBB, MovX->getIterator(), MovT.getDebugLoc(),
                         TII->get(AMDGPU::V_SWAP_B32))
        .addDef(X1.Reg, 0, X1.SubReg)
        .addDef(Y1.Reg, 0, Y1.SubReg)
        .addReg(Y1.Reg, 0, Y1.SubReg)
        .addReg(X1.Reg, 0, X1.SubReg).getInstr();
      if (MovX->hasRegisterImplicitUseOperand(AMDGPU::EXEC)) {
        // Drop implicit EXEC.
        MIB->removeOperand(MIB->getNumExplicitOperands());
        MIB->copyImplicitOps(*MBB.getParent(), *MovX);
      }
    }
    MovX->eraseFromParent();
    dropInstructionKeepingImpDefs(*MovY);
    MachineInstr *Next = &*std::next(MovT.getIterator());

    if (T.isVirtual() && MRI->use_nodbg_empty(T)) {
      dropInstructionKeepingImpDefs(MovT);
    } else {
      Xop.setIsKill(false);
      for (int I = MovT.getNumImplicitOperands() - 1; I >= 0; --I ) {
        unsigned OpNo = MovT.getNumExplicitOperands() + I;
        const MachineOperand &Op = MovT.getOperand(OpNo);
        if (Op.isKill() && TRI->regsOverlap(X, Op.getReg()))
          MovT.removeOperand(OpNo);
      }
    }

    return Next;
  }

  return nullptr;
}

// If an instruction has dead sdst replace it with NULL register on gfx1030+
bool SIShrinkInstructions::tryReplaceDeadSDST(MachineInstr &MI) const {
  if (!ST->hasGFX10_3Insts())
    return false;

  MachineOperand *Op = TII->getNamedOperand(MI, AMDGPU::OpName::sdst);
  if (!Op)
    return false;
  Register SDstReg = Op->getReg();
  if (SDstReg.isPhysical() || !MRI->use_nodbg_empty(SDstReg))
    return false;

  Op->setReg(ST->isWave32() ? AMDGPU::SGPR_NULL : AMDGPU::SGPR_NULL64);
  return true;
}

bool SIShrinkInstructions::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  this->MF = &MF;
  MRI = &MF.getRegInfo();
  ST = &MF.getSubtarget<GCNSubtarget>();
  TII = ST->getInstrInfo();
  TRI = &TII->getRegisterInfo();

  unsigned VCCReg = ST->isWave32() ? AMDGPU::VCC_LO : AMDGPU::VCC;

  std::vector<unsigned> I1Defs;

  for (MachineFunction::iterator BI = MF.begin(), BE = MF.end();
                                                  BI != BE; ++BI) {

    MachineBasicBlock &MBB = *BI;
    MachineBasicBlock::iterator I, Next;
    for (I = MBB.begin(); I != MBB.end(); I = Next) {
      Next = std::next(I);
      MachineInstr &MI = *I;

      if (MI.getOpcode() == AMDGPU::V_MOV_B32_e32) {
        // If this has a literal constant source that is the same as the
        // reversed bits of an inline immediate, replace with a bitreverse of
        // that constant. This saves 4 bytes in the common case of materializing
        // sign bits.

        // Test if we are after regalloc. We only want to do this after any
        // optimizations happen because this will confuse them.
        // XXX - not exactly a check for post-regalloc run.
        MachineOperand &Src = MI.getOperand(1);
        if (Src.isImm() && MI.getOperand(0).getReg().isPhysical()) {
          int32_t ModImm;
          unsigned ModOpcode =
              canModifyToInlineImmOp32(TII, Src, ModImm, /*Scalar=*/false);
          if (ModOpcode != 0) {
            MI.setDesc(TII->get(ModOpcode));
            Src.setImm(static_cast<int64_t>(ModImm));
            continue;
          }
        }
      }

      if (ST->hasSwap() && (MI.getOpcode() == AMDGPU::V_MOV_B32_e32 ||
                            MI.getOpcode() == AMDGPU::COPY)) {
        if (auto *NextMI = matchSwap(MI)) {
          Next = NextMI->getIterator();
          continue;
        }
      }

      // Try to use S_ADDK_I32 and S_MULK_I32.
      if (MI.getOpcode() == AMDGPU::S_ADD_I32 ||
          MI.getOpcode() == AMDGPU::S_MUL_I32) {
        const MachineOperand *Dest = &MI.getOperand(0);
        MachineOperand *Src0 = &MI.getOperand(1);
        MachineOperand *Src1 = &MI.getOperand(2);

        if (!Src0->isReg() && Src1->isReg()) {
          if (TII->commuteInstruction(MI, false, 1, 2))
            std::swap(Src0, Src1);
        }

        // FIXME: This could work better if hints worked with subregisters. If
        // we have a vector add of a constant, we usually don't get the correct
        // allocation due to the subregister usage.
        if (Dest->getReg().isVirtual() && Src0->isReg()) {
          MRI->setRegAllocationHint(Dest->getReg(), 0, Src0->getReg());
          MRI->setRegAllocationHint(Src0->getReg(), 0, Dest->getReg());
          continue;
        }

        if (Src0->isReg() && Src0->getReg() == Dest->getReg()) {
          if (Src1->isImm() && isKImmOperand(*Src1)) {
            unsigned Opc = (MI.getOpcode() == AMDGPU::S_ADD_I32) ?
              AMDGPU::S_ADDK_I32 : AMDGPU::S_MULK_I32;

            Src1->setImm(SignExtend64(Src1->getImm(), 32));
            MI.setDesc(TII->get(Opc));
            MI.tieOperands(0, 1);
          }
        }
      }

      // Try to use s_cmpk_*
      if (MI.isCompare() && TII->isSOPC(MI)) {
        shrinkScalarCompare(MI);
        continue;
      }

      // Try to use S_MOVK_I32, which will save 4 bytes for small immediates.
      if (MI.getOpcode() == AMDGPU::S_MOV_B32) {
        const MachineOperand &Dst = MI.getOperand(0);
        MachineOperand &Src = MI.getOperand(1);

        if (Src.isImm() && Dst.getReg().isPhysical()) {
          unsigned ModOpc;
          int32_t ModImm;
          if (isKImmOperand(Src)) {
            MI.setDesc(TII->get(AMDGPU::S_MOVK_I32));
            Src.setImm(SignExtend64(Src.getImm(), 32));
          } else if ((ModOpc = canModifyToInlineImmOp32(TII, Src, ModImm,
                                                        /*Scalar=*/true))) {
            MI.setDesc(TII->get(ModOpc));
            Src.setImm(static_cast<int64_t>(ModImm));
          }
        }

        continue;
      }

      // Shrink scalar logic operations.
      if (MI.getOpcode() == AMDGPU::S_AND_B32 ||
          MI.getOpcode() == AMDGPU::S_OR_B32 ||
          MI.getOpcode() == AMDGPU::S_XOR_B32) {
        if (shrinkScalarLogicOp(MI))
          continue;
      }

      if (TII->isMIMG(MI.getOpcode()) &&
          ST->getGeneration() >= AMDGPUSubtarget::GFX10 &&
          MF.getProperties().hasProperty(
              MachineFunctionProperties::Property::NoVRegs)) {
        shrinkMIMG(MI);
        continue;
      }

      if (!TII->isVOP3(MI))
        continue;

      if (MI.getOpcode() == AMDGPU::V_MAD_F32_e64 ||
          MI.getOpcode() == AMDGPU::V_FMA_F32_e64 ||
          MI.getOpcode() == AMDGPU::V_MAD_F16_e64 ||
          MI.getOpcode() == AMDGPU::V_FMA_F16_e64 ||
          MI.getOpcode() == AMDGPU::V_FMA_F16_gfx9_e64) {
        shrinkMadFma(MI);
        continue;
      }

      if (!TII->hasVALU32BitEncoding(MI.getOpcode())) {
        // If there is no chance we will shrink it and use VCC as sdst to get
        // a 32 bit form try to replace dead sdst with NULL.
        tryReplaceDeadSDST(MI);
        continue;
      }

      if (!TII->canShrink(MI, *MRI)) {
        // Try commuting the instruction and see if that enables us to shrink
        // it.
        if (!MI.isCommutable() || !TII->commuteInstruction(MI) ||
            !TII->canShrink(MI, *MRI)) {
          tryReplaceDeadSDST(MI);
          continue;
        }
      }

      int Op32 = AMDGPU::getVOPe32(MI.getOpcode());

      if (TII->isVOPC(Op32)) {
        MachineOperand &Op0 = MI.getOperand(0);
        if (Op0.isReg()) {
          // Exclude VOPCX instructions as these don't explicitly write a
          // dst.
          Register DstReg = Op0.getReg();
          if (DstReg.isVirtual()) {
            // VOPC instructions can only write to the VCC register. We can't
            // force them to use VCC here, because this is only one register and
            // cannot deal with sequences which would require multiple copies of
            // VCC, e.g. S_AND_B64 (vcc = V_CMP_...), (vcc = V_CMP_...)
            //
            // So, instead of forcing the instruction to write to VCC, we
            // provide a hint to the register allocator to use VCC and then we
            // will run this pass again after RA and shrink it if it outputs to
            // VCC.
            MRI->setRegAllocationHint(DstReg, 0, VCCReg);
            continue;
          }
          if (DstReg != VCCReg)
            continue;
        }
      }

      if (Op32 == AMDGPU::V_CNDMASK_B32_e32) {
        // We shrink V_CNDMASK_B32_e64 using regalloc hints like we do for VOPC
        // instructions.
        const MachineOperand *Src2 =
            TII->getNamedOperand(MI, AMDGPU::OpName::src2);
        if (!Src2->isReg())
          continue;
        Register SReg = Src2->getReg();
        if (SReg.isVirtual()) {
          MRI->setRegAllocationHint(SReg, 0, VCCReg);
          continue;
        }
        if (SReg != VCCReg)
          continue;
      }

      // Check for the bool flag output for instructions like V_ADD_I32_e64.
      const MachineOperand *SDst = TII->getNamedOperand(MI,
                                                        AMDGPU::OpName::sdst);

      if (SDst) {
        bool Next = false;

        if (SDst->getReg() != VCCReg) {
          if (SDst->getReg().isVirtual())
            MRI->setRegAllocationHint(SDst->getReg(), 0, VCCReg);
          Next = true;
        }

        // All of the instructions with carry outs also have an SGPR input in
        // src2.
        const MachineOperand *Src2 = TII->getNamedOperand(MI,
                                                          AMDGPU::OpName::src2);
        if (Src2 && Src2->getReg() != VCCReg) {
          if (Src2->getReg().isVirtual())
            MRI->setRegAllocationHint(Src2->getReg(), 0, VCCReg);
          Next = true;
        }

        if (Next)
          continue;
      }

      // Pre-GFX10, shrinking VOP3 instructions pre-RA gave us the chance to
      // fold an immediate into the shrunk instruction as a literal operand. In
      // GFX10 VOP3 instructions can take a literal operand anyway, so there is
      // no advantage to doing this.
      if (ST->hasVOP3Literal() &&
          !MF.getProperties().hasProperty(
              MachineFunctionProperties::Property::NoVRegs))
        continue;

      if (ST->hasTrue16BitInsts() && AMDGPU::isTrue16Inst(MI.getOpcode()) &&
          !shouldShrinkTrue16(MI))
        continue;

      // We can shrink this instruction
      LLVM_DEBUG(dbgs() << "Shrinking " << MI);

      MachineInstr *Inst32 = TII->buildShrunkInst(MI, Op32);
      ++NumInstructionsShrunk;

      // Copy extra operands not present in the instruction definition.
      copyExtraImplicitOps(*Inst32, MI);

      // Copy deadness from the old explicit vcc def to the new implicit def.
      if (SDst && SDst->isDead())
        Inst32->findRegisterDefOperand(VCCReg, /*TRI=*/nullptr)->setIsDead();

      MI.eraseFromParent();
      foldImmediates(*Inst32);

      LLVM_DEBUG(dbgs() << "e32 MI = " << *Inst32 << '\n');
    }
  }
  return false;
}
