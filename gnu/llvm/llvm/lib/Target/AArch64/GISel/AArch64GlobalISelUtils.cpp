//===- AArch64GlobalISelUtils.cpp --------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file Implementations of AArch64-specific helper functions used in the
/// GlobalISel pipeline.
//===----------------------------------------------------------------------===//
#include "AArch64GlobalISelUtils.h"
#include "AArch64InstrInfo.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

std::optional<RegOrConstant>
AArch64GISelUtils::getAArch64VectorSplat(const MachineInstr &MI,
                                         const MachineRegisterInfo &MRI) {
  if (auto Splat = getVectorSplat(MI, MRI))
    return Splat;
  if (MI.getOpcode() != AArch64::G_DUP)
    return std::nullopt;
  Register Src = MI.getOperand(1).getReg();
  if (auto ValAndVReg =
          getAnyConstantVRegValWithLookThrough(MI.getOperand(1).getReg(), MRI))
    return RegOrConstant(ValAndVReg->Value.getSExtValue());
  return RegOrConstant(Src);
}

std::optional<int64_t>
AArch64GISelUtils::getAArch64VectorSplatScalar(const MachineInstr &MI,
                                               const MachineRegisterInfo &MRI) {
  auto Splat = getAArch64VectorSplat(MI, MRI);
  if (!Splat || Splat->isReg())
    return std::nullopt;
  return Splat->getCst();
}

bool AArch64GISelUtils::isCMN(const MachineInstr *MaybeSub,
                              const CmpInst::Predicate &Pred,
                              const MachineRegisterInfo &MRI) {
  // Match:
  //
  // %sub = G_SUB 0, %y
  // %cmp = G_ICMP eq/ne, %sub, %z
  //
  // Or
  //
  // %sub = G_SUB 0, %y
  // %cmp = G_ICMP eq/ne, %z, %sub
  if (!MaybeSub || MaybeSub->getOpcode() != TargetOpcode::G_SUB ||
      !CmpInst::isEquality(Pred))
    return false;
  auto MaybeZero =
      getIConstantVRegValWithLookThrough(MaybeSub->getOperand(1).getReg(), MRI);
  return MaybeZero && MaybeZero->Value.getZExtValue() == 0;
}

bool AArch64GISelUtils::tryEmitBZero(MachineInstr &MI,
                                     MachineIRBuilder &MIRBuilder,
                                     bool MinSize) {
  assert(MI.getOpcode() == TargetOpcode::G_MEMSET);
  MachineRegisterInfo &MRI = *MIRBuilder.getMRI();
  auto &TLI = *MIRBuilder.getMF().getSubtarget().getTargetLowering();
  if (!TLI.getLibcallName(RTLIB::BZERO))
    return false;
  auto Zero =
      getIConstantVRegValWithLookThrough(MI.getOperand(1).getReg(), MRI);
  if (!Zero || Zero->Value.getSExtValue() != 0)
    return false;

  // It's not faster to use bzero rather than memset for sizes <= 256.
  // However, it *does* save us a mov from wzr, so if we're going for
  // minsize, use bzero even if it's slower.
  if (!MinSize) {
    // If the size is known, check it. If it is not known, assume using bzero is
    // better.
    if (auto Size = getIConstantVRegValWithLookThrough(
            MI.getOperand(2).getReg(), MRI)) {
      if (Size->Value.getSExtValue() <= 256)
        return false;
    }
  }

  MIRBuilder.setInstrAndDebugLoc(MI);
  MIRBuilder
      .buildInstr(TargetOpcode::G_BZERO, {},
                  {MI.getOperand(0), MI.getOperand(2)})
      .addImm(MI.getOperand(3).getImm())
      .addMemOperand(*MI.memoperands_begin());
  MI.eraseFromParent();
  return true;
}

std::tuple<uint16_t, Register>
AArch64GISelUtils::extractPtrauthBlendDiscriminators(Register Disc,
                                                     MachineRegisterInfo &MRI) {
  Register AddrDisc = Disc;
  uint16_t ConstDisc = 0;

  if (auto ConstDiscVal = getIConstantVRegVal(Disc, MRI)) {
    if (isUInt<16>(ConstDiscVal->getZExtValue())) {
      ConstDisc = ConstDiscVal->getZExtValue();
      AddrDisc = AArch64::NoRegister;
    }
    return std::make_tuple(ConstDisc, AddrDisc);
  }

  const MachineInstr *DiscMI = MRI.getVRegDef(Disc);
  if (!DiscMI || DiscMI->getOpcode() != TargetOpcode::G_INTRINSIC ||
      DiscMI->getOperand(1).getIntrinsicID() != Intrinsic::ptrauth_blend)
    return std::make_tuple(ConstDisc, AddrDisc);

  if (auto ConstDiscVal =
          getIConstantVRegVal(DiscMI->getOperand(3).getReg(), MRI)) {
    if (isUInt<16>(ConstDiscVal->getZExtValue())) {
      ConstDisc = ConstDiscVal->getZExtValue();
      AddrDisc = DiscMI->getOperand(2).getReg();
    }
  }
  return std::make_tuple(ConstDisc, AddrDisc);
}

void AArch64GISelUtils::changeFCMPPredToAArch64CC(
    const CmpInst::Predicate P, AArch64CC::CondCode &CondCode,
    AArch64CC::CondCode &CondCode2) {
  CondCode2 = AArch64CC::AL;
  switch (P) {
  default:
    llvm_unreachable("Unknown FP condition!");
  case CmpInst::FCMP_OEQ:
    CondCode = AArch64CC::EQ;
    break;
  case CmpInst::FCMP_OGT:
    CondCode = AArch64CC::GT;
    break;
  case CmpInst::FCMP_OGE:
    CondCode = AArch64CC::GE;
    break;
  case CmpInst::FCMP_OLT:
    CondCode = AArch64CC::MI;
    break;
  case CmpInst::FCMP_OLE:
    CondCode = AArch64CC::LS;
    break;
  case CmpInst::FCMP_ONE:
    CondCode = AArch64CC::MI;
    CondCode2 = AArch64CC::GT;
    break;
  case CmpInst::FCMP_ORD:
    CondCode = AArch64CC::VC;
    break;
  case CmpInst::FCMP_UNO:
    CondCode = AArch64CC::VS;
    break;
  case CmpInst::FCMP_UEQ:
    CondCode = AArch64CC::EQ;
    CondCode2 = AArch64CC::VS;
    break;
  case CmpInst::FCMP_UGT:
    CondCode = AArch64CC::HI;
    break;
  case CmpInst::FCMP_UGE:
    CondCode = AArch64CC::PL;
    break;
  case CmpInst::FCMP_ULT:
    CondCode = AArch64CC::LT;
    break;
  case CmpInst::FCMP_ULE:
    CondCode = AArch64CC::LE;
    break;
  case CmpInst::FCMP_UNE:
    CondCode = AArch64CC::NE;
    break;
  case CmpInst::FCMP_TRUE:
    CondCode = AArch64CC::AL;
    break;
  case CmpInst::FCMP_FALSE:
    CondCode = AArch64CC::NV;
    break;
  }
}

void AArch64GISelUtils::changeVectorFCMPPredToAArch64CC(
    const CmpInst::Predicate P, AArch64CC::CondCode &CondCode,
    AArch64CC::CondCode &CondCode2, bool &Invert) {
  Invert = false;
  switch (P) {
  default:
    // Mostly the scalar mappings work fine.
    changeFCMPPredToAArch64CC(P, CondCode, CondCode2);
    break;
  case CmpInst::FCMP_UNO:
    Invert = true;
    [[fallthrough]];
  case CmpInst::FCMP_ORD:
    CondCode = AArch64CC::MI;
    CondCode2 = AArch64CC::GE;
    break;
  case CmpInst::FCMP_UEQ:
  case CmpInst::FCMP_ULT:
  case CmpInst::FCMP_ULE:
  case CmpInst::FCMP_UGT:
  case CmpInst::FCMP_UGE:
    // All of the compare-mask comparisons are ordered, but we can switch
    // between the two by a double inversion. E.g. ULE == !OGT.
    Invert = true;
    changeFCMPPredToAArch64CC(CmpInst::getInversePredicate(P), CondCode,
                              CondCode2);
    break;
  }
}
