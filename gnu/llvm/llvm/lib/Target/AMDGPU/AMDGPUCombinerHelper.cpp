//=== lib/CodeGen/GlobalISel/AMDGPUCombinerHelper.cpp ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPUCombinerHelper.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;
using namespace MIPatternMatch;

LLVM_READNONE
static bool fnegFoldsIntoMI(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case AMDGPU::G_FADD:
  case AMDGPU::G_FSUB:
  case AMDGPU::G_FMUL:
  case AMDGPU::G_FMA:
  case AMDGPU::G_FMAD:
  case AMDGPU::G_FMINNUM:
  case AMDGPU::G_FMAXNUM:
  case AMDGPU::G_FMINNUM_IEEE:
  case AMDGPU::G_FMAXNUM_IEEE:
  case AMDGPU::G_FMINIMUM:
  case AMDGPU::G_FMAXIMUM:
  case AMDGPU::G_FSIN:
  case AMDGPU::G_FPEXT:
  case AMDGPU::G_INTRINSIC_TRUNC:
  case AMDGPU::G_FPTRUNC:
  case AMDGPU::G_FRINT:
  case AMDGPU::G_FNEARBYINT:
  case AMDGPU::G_INTRINSIC_ROUND:
  case AMDGPU::G_INTRINSIC_ROUNDEVEN:
  case AMDGPU::G_FCANONICALIZE:
  case AMDGPU::G_AMDGPU_RCP_IFLAG:
  case AMDGPU::G_AMDGPU_FMIN_LEGACY:
  case AMDGPU::G_AMDGPU_FMAX_LEGACY:
    return true;
  case AMDGPU::G_INTRINSIC: {
    Intrinsic::ID IntrinsicID = cast<GIntrinsic>(MI).getIntrinsicID();
    switch (IntrinsicID) {
    case Intrinsic::amdgcn_rcp:
    case Intrinsic::amdgcn_rcp_legacy:
    case Intrinsic::amdgcn_sin:
    case Intrinsic::amdgcn_fmul_legacy:
    case Intrinsic::amdgcn_fmed3:
    case Intrinsic::amdgcn_fma_legacy:
      return true;
    default:
      return false;
    }
  }
  default:
    return false;
  }
}

/// \p returns true if the operation will definitely need to use a 64-bit
/// encoding, and thus will use a VOP3 encoding regardless of the source
/// modifiers.
LLVM_READONLY
static bool opMustUseVOP3Encoding(const MachineInstr &MI,
                                  const MachineRegisterInfo &MRI) {
  return MI.getNumOperands() > (isa<GIntrinsic>(MI) ? 4u : 3u) ||
         MRI.getType(MI.getOperand(0).getReg()).getScalarSizeInBits() == 64;
}

// Most FP instructions support source modifiers.
LLVM_READONLY
static bool hasSourceMods(const MachineInstr &MI) {
  if (!MI.memoperands().empty())
    return false;

  switch (MI.getOpcode()) {
  case AMDGPU::COPY:
  case AMDGPU::G_SELECT:
  case AMDGPU::G_FDIV:
  case AMDGPU::G_FREM:
  case TargetOpcode::INLINEASM:
  case TargetOpcode::INLINEASM_BR:
  case AMDGPU::G_INTRINSIC_W_SIDE_EFFECTS:
  case AMDGPU::G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS:
  case AMDGPU::G_BITCAST:
  case AMDGPU::G_ANYEXT:
  case AMDGPU::G_BUILD_VECTOR:
  case AMDGPU::G_BUILD_VECTOR_TRUNC:
  case AMDGPU::G_PHI:
    return false;
  case AMDGPU::G_INTRINSIC:
  case AMDGPU::G_INTRINSIC_CONVERGENT: {
    Intrinsic::ID IntrinsicID = cast<GIntrinsic>(MI).getIntrinsicID();
    switch (IntrinsicID) {
    case Intrinsic::amdgcn_interp_p1:
    case Intrinsic::amdgcn_interp_p2:
    case Intrinsic::amdgcn_interp_mov:
    case Intrinsic::amdgcn_interp_p1_f16:
    case Intrinsic::amdgcn_interp_p2_f16:
    case Intrinsic::amdgcn_div_scale:
      return false;
    default:
      return true;
    }
  }
  default:
    return true;
  }
}

static bool allUsesHaveSourceMods(MachineInstr &MI, MachineRegisterInfo &MRI,
                                  unsigned CostThreshold = 4) {
  // Some users (such as 3-operand FMA/MAD) must use a VOP3 encoding, and thus
  // it is truly free to use a source modifier in all cases. If there are
  // multiple users but for each one will necessitate using VOP3, there will be
  // a code size increase. Try to avoid increasing code size unless we know it
  // will save on the instruction count.
  unsigned NumMayIncreaseSize = 0;
  Register Dst = MI.getOperand(0).getReg();
  for (const MachineInstr &Use : MRI.use_nodbg_instructions(Dst)) {
    if (!hasSourceMods(Use))
      return false;

    if (!opMustUseVOP3Encoding(Use, MRI)) {
      if (++NumMayIncreaseSize > CostThreshold)
        return false;
    }
  }
  return true;
}

static bool mayIgnoreSignedZero(MachineInstr &MI) {
  const TargetOptions &Options = MI.getMF()->getTarget().Options;
  return Options.NoSignedZerosFPMath || MI.getFlag(MachineInstr::MIFlag::FmNsz);
}

static bool isInv2Pi(const APFloat &APF) {
  static const APFloat KF16(APFloat::IEEEhalf(), APInt(16, 0x3118));
  static const APFloat KF32(APFloat::IEEEsingle(), APInt(32, 0x3e22f983));
  static const APFloat KF64(APFloat::IEEEdouble(),
                            APInt(64, 0x3fc45f306dc9c882));

  return APF.bitwiseIsEqual(KF16) || APF.bitwiseIsEqual(KF32) ||
         APF.bitwiseIsEqual(KF64);
}

// 0 and 1.0 / (0.5 * pi) do not have inline immmediates, so there is an
// additional cost to negate them.
static bool isConstantCostlierToNegate(MachineInstr &MI, Register Reg,
                                       MachineRegisterInfo &MRI) {
  std::optional<FPValueAndVReg> FPValReg;
  if (mi_match(Reg, MRI, m_GFCstOrSplat(FPValReg))) {
    if (FPValReg->Value.isZero() && !FPValReg->Value.isNegative())
      return true;

    const GCNSubtarget &ST = MI.getMF()->getSubtarget<GCNSubtarget>();
    if (ST.hasInv2PiInlineImm() && isInv2Pi(FPValReg->Value))
      return true;
  }
  return false;
}

static unsigned inverseMinMax(unsigned Opc) {
  switch (Opc) {
  case AMDGPU::G_FMAXNUM:
    return AMDGPU::G_FMINNUM;
  case AMDGPU::G_FMINNUM:
    return AMDGPU::G_FMAXNUM;
  case AMDGPU::G_FMAXNUM_IEEE:
    return AMDGPU::G_FMINNUM_IEEE;
  case AMDGPU::G_FMINNUM_IEEE:
    return AMDGPU::G_FMAXNUM_IEEE;
  case AMDGPU::G_FMAXIMUM:
    return AMDGPU::G_FMINIMUM;
  case AMDGPU::G_FMINIMUM:
    return AMDGPU::G_FMAXIMUM;
  case AMDGPU::G_AMDGPU_FMAX_LEGACY:
    return AMDGPU::G_AMDGPU_FMIN_LEGACY;
  case AMDGPU::G_AMDGPU_FMIN_LEGACY:
    return AMDGPU::G_AMDGPU_FMAX_LEGACY;
  default:
    llvm_unreachable("invalid min/max opcode");
  }
}

bool AMDGPUCombinerHelper::matchFoldableFneg(MachineInstr &MI,
                                             MachineInstr *&MatchInfo) {
  Register Src = MI.getOperand(1).getReg();
  MatchInfo = MRI.getVRegDef(Src);

  // If the input has multiple uses and we can either fold the negate down, or
  // the other uses cannot, give up. This both prevents unprofitable
  // transformations and infinite loops: we won't repeatedly try to fold around
  // a negate that has no 'good' form.
  if (MRI.hasOneNonDBGUse(Src)) {
    if (allUsesHaveSourceMods(MI, MRI, 0))
      return false;
  } else {
    if (fnegFoldsIntoMI(*MatchInfo) &&
        (allUsesHaveSourceMods(MI, MRI) ||
         !allUsesHaveSourceMods(*MatchInfo, MRI)))
      return false;
  }

  switch (MatchInfo->getOpcode()) {
  case AMDGPU::G_FMINNUM:
  case AMDGPU::G_FMAXNUM:
  case AMDGPU::G_FMINNUM_IEEE:
  case AMDGPU::G_FMAXNUM_IEEE:
  case AMDGPU::G_FMINIMUM:
  case AMDGPU::G_FMAXIMUM:
  case AMDGPU::G_AMDGPU_FMIN_LEGACY:
  case AMDGPU::G_AMDGPU_FMAX_LEGACY:
    // 0 doesn't have a negated inline immediate.
    return !isConstantCostlierToNegate(*MatchInfo,
                                       MatchInfo->getOperand(2).getReg(), MRI);
  case AMDGPU::G_FADD:
  case AMDGPU::G_FSUB:
  case AMDGPU::G_FMA:
  case AMDGPU::G_FMAD:
    return mayIgnoreSignedZero(*MatchInfo);
  case AMDGPU::G_FMUL:
  case AMDGPU::G_FPEXT:
  case AMDGPU::G_INTRINSIC_TRUNC:
  case AMDGPU::G_FPTRUNC:
  case AMDGPU::G_FRINT:
  case AMDGPU::G_FNEARBYINT:
  case AMDGPU::G_INTRINSIC_ROUND:
  case AMDGPU::G_INTRINSIC_ROUNDEVEN:
  case AMDGPU::G_FSIN:
  case AMDGPU::G_FCANONICALIZE:
  case AMDGPU::G_AMDGPU_RCP_IFLAG:
    return true;
  case AMDGPU::G_INTRINSIC:
  case AMDGPU::G_INTRINSIC_CONVERGENT: {
    Intrinsic::ID IntrinsicID = cast<GIntrinsic>(MatchInfo)->getIntrinsicID();
    switch (IntrinsicID) {
    case Intrinsic::amdgcn_rcp:
    case Intrinsic::amdgcn_rcp_legacy:
    case Intrinsic::amdgcn_sin:
    case Intrinsic::amdgcn_fmul_legacy:
    case Intrinsic::amdgcn_fmed3:
      return true;
    case Intrinsic::amdgcn_fma_legacy:
      return mayIgnoreSignedZero(*MatchInfo);
    default:
      return false;
    }
  }
  default:
    return false;
  }
}

void AMDGPUCombinerHelper::applyFoldableFneg(MachineInstr &MI,
                                             MachineInstr *&MatchInfo) {
  // Transform:
  // %A = inst %Op1, ...
  // %B = fneg %A
  //
  // into:
  //
  // (if %A has one use, specifically fneg above)
  // %B = inst (maybe fneg %Op1), ...
  //
  // (if %A has multiple uses)
  // %B = inst (maybe fneg %Op1), ...
  // %A = fneg %B

  // Replace register in operand with a register holding negated value.
  auto NegateOperand = [&](MachineOperand &Op) {
    Register Reg = Op.getReg();
    if (!mi_match(Reg, MRI, m_GFNeg(m_Reg(Reg))))
      Reg = Builder.buildFNeg(MRI.getType(Reg), Reg).getReg(0);
    replaceRegOpWith(MRI, Op, Reg);
  };

  // Replace either register in operands with a register holding negated value.
  auto NegateEitherOperand = [&](MachineOperand &X, MachineOperand &Y) {
    Register XReg = X.getReg();
    Register YReg = Y.getReg();
    if (mi_match(XReg, MRI, m_GFNeg(m_Reg(XReg))))
      replaceRegOpWith(MRI, X, XReg);
    else if (mi_match(YReg, MRI, m_GFNeg(m_Reg(YReg))))
      replaceRegOpWith(MRI, Y, YReg);
    else {
      YReg = Builder.buildFNeg(MRI.getType(YReg), YReg).getReg(0);
      replaceRegOpWith(MRI, Y, YReg);
    }
  };

  Builder.setInstrAndDebugLoc(*MatchInfo);

  // Negate appropriate operands so that resulting value of MatchInfo is
  // negated.
  switch (MatchInfo->getOpcode()) {
  case AMDGPU::G_FADD:
  case AMDGPU::G_FSUB:
    NegateOperand(MatchInfo->getOperand(1));
    NegateOperand(MatchInfo->getOperand(2));
    break;
  case AMDGPU::G_FMUL:
    NegateEitherOperand(MatchInfo->getOperand(1), MatchInfo->getOperand(2));
    break;
  case AMDGPU::G_FMINNUM:
  case AMDGPU::G_FMAXNUM:
  case AMDGPU::G_FMINNUM_IEEE:
  case AMDGPU::G_FMAXNUM_IEEE:
  case AMDGPU::G_FMINIMUM:
  case AMDGPU::G_FMAXIMUM:
  case AMDGPU::G_AMDGPU_FMIN_LEGACY:
  case AMDGPU::G_AMDGPU_FMAX_LEGACY: {
    NegateOperand(MatchInfo->getOperand(1));
    NegateOperand(MatchInfo->getOperand(2));
    unsigned Opposite = inverseMinMax(MatchInfo->getOpcode());
    replaceOpcodeWith(*MatchInfo, Opposite);
    break;
  }
  case AMDGPU::G_FMA:
  case AMDGPU::G_FMAD:
    NegateEitherOperand(MatchInfo->getOperand(1), MatchInfo->getOperand(2));
    NegateOperand(MatchInfo->getOperand(3));
    break;
  case AMDGPU::G_FPEXT:
  case AMDGPU::G_INTRINSIC_TRUNC:
  case AMDGPU::G_FRINT:
  case AMDGPU::G_FNEARBYINT:
  case AMDGPU::G_INTRINSIC_ROUND:
  case AMDGPU::G_INTRINSIC_ROUNDEVEN:
  case AMDGPU::G_FSIN:
  case AMDGPU::G_FCANONICALIZE:
  case AMDGPU::G_AMDGPU_RCP_IFLAG:
  case AMDGPU::G_FPTRUNC:
    NegateOperand(MatchInfo->getOperand(1));
    break;
  case AMDGPU::G_INTRINSIC:
  case AMDGPU::G_INTRINSIC_CONVERGENT: {
    Intrinsic::ID IntrinsicID = cast<GIntrinsic>(MatchInfo)->getIntrinsicID();
    switch (IntrinsicID) {
    case Intrinsic::amdgcn_rcp:
    case Intrinsic::amdgcn_rcp_legacy:
    case Intrinsic::amdgcn_sin:
      NegateOperand(MatchInfo->getOperand(2));
      break;
    case Intrinsic::amdgcn_fmul_legacy:
      NegateEitherOperand(MatchInfo->getOperand(2), MatchInfo->getOperand(3));
      break;
    case Intrinsic::amdgcn_fmed3:
      NegateOperand(MatchInfo->getOperand(2));
      NegateOperand(MatchInfo->getOperand(3));
      NegateOperand(MatchInfo->getOperand(4));
      break;
    case Intrinsic::amdgcn_fma_legacy:
      NegateEitherOperand(MatchInfo->getOperand(2), MatchInfo->getOperand(3));
      NegateOperand(MatchInfo->getOperand(4));
      break;
    default:
      llvm_unreachable("folding fneg not supported for this intrinsic");
    }
    break;
  }
  default:
    llvm_unreachable("folding fneg not supported for this instruction");
  }

  Register Dst = MI.getOperand(0).getReg();
  Register MatchInfoDst = MatchInfo->getOperand(0).getReg();

  if (MRI.hasOneNonDBGUse(MatchInfoDst)) {
    // MatchInfo now has negated value so use that instead of old Dst.
    replaceRegWith(MRI, Dst, MatchInfoDst);
  } else {
    // We want to swap all uses of Dst with uses of MatchInfoDst and vice versa
    // but replaceRegWith will replace defs as well. It is easier to replace one
    // def with a new register.
    LLT Type = MRI.getType(Dst);
    Register NegatedMatchInfo = MRI.createGenericVirtualRegister(Type);
    replaceRegOpWith(MRI, MatchInfo->getOperand(0), NegatedMatchInfo);

    // MatchInfo now has negated value so use that instead of old Dst.
    replaceRegWith(MRI, Dst, NegatedMatchInfo);

    // Recreate non negated value for other uses of old MatchInfoDst
    auto NextInst = ++MatchInfo->getIterator();
    Builder.setInstrAndDebugLoc(*NextInst);
    Builder.buildFNeg(MatchInfoDst, NegatedMatchInfo, MI.getFlags());
  }

  MI.eraseFromParent();
}

// TODO: Should return converted value / extension source and avoid introducing
// intermediate fptruncs in the apply function.
static bool isFPExtFromF16OrConst(const MachineRegisterInfo &MRI,
                                  Register Reg) {
  const MachineInstr *Def = MRI.getVRegDef(Reg);
  if (Def->getOpcode() == TargetOpcode::G_FPEXT) {
    Register SrcReg = Def->getOperand(1).getReg();
    return MRI.getType(SrcReg) == LLT::scalar(16);
  }

  if (Def->getOpcode() == TargetOpcode::G_FCONSTANT) {
    APFloat Val = Def->getOperand(1).getFPImm()->getValueAPF();
    bool LosesInfo = true;
    Val.convert(APFloat::IEEEhalf(), APFloat::rmNearestTiesToEven, &LosesInfo);
    return !LosesInfo;
  }

  return false;
}

bool AMDGPUCombinerHelper::matchExpandPromotedF16FMed3(MachineInstr &MI,
                                                       Register Src0,
                                                       Register Src1,
                                                       Register Src2) {
  assert(MI.getOpcode() == TargetOpcode::G_FPTRUNC);
  Register SrcReg = MI.getOperand(1).getReg();
  if (!MRI.hasOneNonDBGUse(SrcReg) || MRI.getType(SrcReg) != LLT::scalar(32))
    return false;

  return isFPExtFromF16OrConst(MRI, Src0) && isFPExtFromF16OrConst(MRI, Src1) &&
         isFPExtFromF16OrConst(MRI, Src2);
}

void AMDGPUCombinerHelper::applyExpandPromotedF16FMed3(MachineInstr &MI,
                                                       Register Src0,
                                                       Register Src1,
                                                       Register Src2) {
  // We expect fptrunc (fpext x) to fold out, and to constant fold any constant
  // sources.
  Src0 = Builder.buildFPTrunc(LLT::scalar(16), Src0).getReg(0);
  Src1 = Builder.buildFPTrunc(LLT::scalar(16), Src1).getReg(0);
  Src2 = Builder.buildFPTrunc(LLT::scalar(16), Src2).getReg(0);

  LLT Ty = MRI.getType(Src0);
  auto A1 = Builder.buildFMinNumIEEE(Ty, Src0, Src1);
  auto B1 = Builder.buildFMaxNumIEEE(Ty, Src0, Src1);
  auto C1 = Builder.buildFMaxNumIEEE(Ty, A1, Src2);
  Builder.buildFMinNumIEEE(MI.getOperand(0), B1, C1);
  MI.eraseFromParent();
}
