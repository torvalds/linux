//===- PPCRegisterBankInfo.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the targeting of the RegisterBankInfo class for
/// PowerPC.
//===----------------------------------------------------------------------===//

#include "PPCRegisterBankInfo.h"
#include "PPCRegisterInfo.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "ppc-reg-bank-info"

#define GET_TARGET_REGBANK_IMPL
#include "PPCGenRegisterBank.inc"

// This file will be TableGen'ed at some point.
#include "PPCGenRegisterBankInfo.def"

using namespace llvm;

PPCRegisterBankInfo::PPCRegisterBankInfo(const TargetRegisterInfo &TRI) {}

const RegisterBank &
PPCRegisterBankInfo::getRegBankFromRegClass(const TargetRegisterClass &RC,
                                            LLT Ty) const {
  switch (RC.getID()) {
  case PPC::G8RCRegClassID:
  case PPC::G8RC_NOX0RegClassID:
  case PPC::G8RC_and_G8RC_NOX0RegClassID:
  case PPC::GPRCRegClassID:
  case PPC::GPRC_NOR0RegClassID:
  case PPC::GPRC_and_GPRC_NOR0RegClassID:
    return getRegBank(PPC::GPRRegBankID);
  case PPC::VSFRCRegClassID:
  case PPC::SPILLTOVSRRC_and_VSFRCRegClassID:
  case PPC::SPILLTOVSRRC_and_VFRCRegClassID:
  case PPC::SPILLTOVSRRC_and_F4RCRegClassID:
  case PPC::F8RCRegClassID:
  case PPC::VFRCRegClassID:
  case PPC::VSSRCRegClassID:
  case PPC::F4RCRegClassID:
    return getRegBank(PPC::FPRRegBankID);
  case PPC::VSRCRegClassID:
  case PPC::VRRCRegClassID:
  case PPC::VRRC_with_sub_64_in_SPILLTOVSRRCRegClassID:
  case PPC::VSRC_with_sub_64_in_SPILLTOVSRRCRegClassID:
  case PPC::SPILLTOVSRRCRegClassID:
  case PPC::VSLRCRegClassID:
  case PPC::VSLRC_with_sub_64_in_SPILLTOVSRRCRegClassID:
    return getRegBank(PPC::VECRegBankID);
  case PPC::CRRCRegClassID:
  case PPC::CRBITRCRegClassID:
    return getRegBank(PPC::CRRegBankID);
  default:
    llvm_unreachable("Unexpected register class");
  }
}

const RegisterBankInfo::InstructionMapping &
PPCRegisterBankInfo::getInstrMapping(const MachineInstr &MI) const {
  const unsigned Opc = MI.getOpcode();

  // Try the default logic for non-generic instructions that are either copies
  // or already have some operands assigned to banks.
  if (!isPreISelGenericOpcode(Opc) || Opc == TargetOpcode::G_PHI) {
    const RegisterBankInfo::InstructionMapping &Mapping =
        getInstrMappingImpl(MI);
    if (Mapping.isValid())
      return Mapping;
  }

  const MachineFunction &MF = *MI.getParent()->getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetSubtargetInfo &STI = MF.getSubtarget();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();

  unsigned NumOperands = MI.getNumOperands();
  const ValueMapping *OperandsMapping = nullptr;
  unsigned Cost = 1;
  unsigned MappingID = DefaultMappingID;

  switch (Opc) {
    // Arithmetic ops.
  case TargetOpcode::G_ADD:
  case TargetOpcode::G_SUB:
    // Bitwise ops.
  case TargetOpcode::G_AND:
  case TargetOpcode::G_OR:
  case TargetOpcode::G_XOR:
    // Extension ops.
  case TargetOpcode::G_SEXT:
  case TargetOpcode::G_ZEXT:
  case TargetOpcode::G_ANYEXT: {
    assert(NumOperands <= 3 &&
           "This code is for instructions with 3 or less operands");
    LLT Ty = MRI.getType(MI.getOperand(0).getReg());
    unsigned Size = Ty.getSizeInBits();
    switch (Size) {
    case 128:
      OperandsMapping = getValueMapping(PMI_VEC128);
      break;
    default:
      OperandsMapping = getValueMapping(PMI_GPR64);
      break;
    }
    break;
  }
  case TargetOpcode::G_FADD:
  case TargetOpcode::G_FSUB:
  case TargetOpcode::G_FMUL:
  case TargetOpcode::G_FDIV: {
    Register SrcReg = MI.getOperand(1).getReg();
    unsigned Size = getSizeInBits(SrcReg, MRI, TRI);

    assert((Size == 32 || Size == 64 || Size == 128) &&
           "Unsupported floating point types!\n");
    switch (Size) {
    case 32:
      OperandsMapping = getValueMapping(PMI_FPR32);
      break;
    case 64:
      OperandsMapping = getValueMapping(PMI_FPR64);
      break;
    case 128:
      OperandsMapping = getValueMapping(PMI_VEC128);
      break;
    }
    break;
  }
  case TargetOpcode::G_FCMP: {
    unsigned CmpSize = MRI.getType(MI.getOperand(2).getReg()).getSizeInBits();

    OperandsMapping = getOperandsMapping(
        {getValueMapping(PMI_CR), nullptr,
         getValueMapping(CmpSize == 32 ? PMI_FPR32 : PMI_FPR64),
         getValueMapping(CmpSize == 32 ? PMI_FPR32 : PMI_FPR64)});
    break;
  }
  case TargetOpcode::G_CONSTANT:
    OperandsMapping = getOperandsMapping({getValueMapping(PMI_GPR64), nullptr});
    break;
  case TargetOpcode::G_CONSTANT_POOL:
    OperandsMapping = getOperandsMapping({getValueMapping(PMI_GPR64), nullptr});
    break;
  case TargetOpcode::G_FPTOUI:
  case TargetOpcode::G_FPTOSI: {
    Register SrcReg = MI.getOperand(1).getReg();
    unsigned Size = getSizeInBits(SrcReg, MRI, TRI);

    OperandsMapping = getOperandsMapping(
        {getValueMapping(PMI_GPR64),
         getValueMapping(Size == 32 ? PMI_FPR32 : PMI_FPR64)});
    break;
  }
  case TargetOpcode::G_UITOFP:
  case TargetOpcode::G_SITOFP: {
    Register SrcReg = MI.getOperand(0).getReg();
    unsigned Size = getSizeInBits(SrcReg, MRI, TRI);

    OperandsMapping =
        getOperandsMapping({getValueMapping(Size == 32 ? PMI_FPR32 : PMI_FPR64),
                            getValueMapping(PMI_GPR64)});
    break;
  }
  case TargetOpcode::G_LOAD: {
    unsigned Size = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
    // Check if that load feeds fp instructions.
    if (any_of(MRI.use_nodbg_instructions(MI.getOperand(0).getReg()),
               [&](const MachineInstr &UseMI) {
                 // If we have at least one direct use in a FP instruction,
                 // assume this was a floating point load in the IR. If it was
                 // not, we would have had a bitcast before reaching that
                 // instruction.
                 //
                 // Int->FP conversion operations are also captured in
                 // onlyDefinesFP().
                 return onlyUsesFP(UseMI, MRI, TRI);
               }))
      OperandsMapping = getOperandsMapping(
          {getValueMapping(Size == 64 ? PMI_FPR64 : PMI_FPR32),
           getValueMapping(PMI_GPR64)});
    else
      OperandsMapping = getOperandsMapping(
          {getValueMapping(Size == 64 ? PMI_GPR64 : PMI_GPR32),
           getValueMapping(PMI_GPR64)});
    break;
  }
  case TargetOpcode::G_STORE: {
    // Check if the store is fed by fp instructions.
    MachineInstr *DefMI = MRI.getVRegDef(MI.getOperand(0).getReg());
    unsigned Size = MRI.getType(MI.getOperand(0).getReg()).getSizeInBits();
    if (onlyDefinesFP(*DefMI, MRI, TRI))
      OperandsMapping = getOperandsMapping(
          {getValueMapping(Size == 64 ? PMI_FPR64 : PMI_FPR32),
           getValueMapping(PMI_GPR64)});
    else
      OperandsMapping = getOperandsMapping(
          {getValueMapping(Size == 64 ? PMI_GPR64 : PMI_GPR32),
           getValueMapping(PMI_GPR64)});
    break;
  }
  case TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS: {
    // FIXME: We have to check every operand in this MI and compute value
    // mapping accordingly.
    SmallVector<const ValueMapping *, 8> OpdsMapping(NumOperands);
    OperandsMapping = getOperandsMapping(OpdsMapping);
    break;
  }
  case TargetOpcode::G_BITCAST: {
    LLT DstTy = MRI.getType(MI.getOperand(0).getReg());
    LLT SrcTy = MRI.getType(MI.getOperand(1).getReg());
    unsigned DstSize = DstTy.getSizeInBits();

    bool DstIsGPR = !DstTy.isVector();
    bool SrcIsGPR = !SrcTy.isVector();
    // TODO: Currently, only vector and GPR register banks are handled.
    //       This needs to be extended to handle floating point register
    //       banks in the future.
    const RegisterBank &DstRB = DstIsGPR ? PPC::GPRRegBank : PPC::VECRegBank;
    const RegisterBank &SrcRB = SrcIsGPR ? PPC::GPRRegBank : PPC::VECRegBank;

    return getInstructionMapping(
        MappingID, Cost, getCopyMapping(DstRB.getID(), SrcRB.getID(), DstSize),
        NumOperands);
  }
  default:
    return getInvalidInstructionMapping();
  }

  return getInstructionMapping(MappingID, Cost, OperandsMapping, NumOperands);
}

/// \returns true if a given intrinsic \p ID only uses and defines FPRs.
static bool isFPIntrinsic(unsigned ID) {
  // TODO: Add more intrinsics.
  return false;
}

/// FIXME: this is copied from target AArch64. Needs some code refactor here to
/// put this function in class RegisterBankInfo.
bool PPCRegisterBankInfo::hasFPConstraints(const MachineInstr &MI,
                                           const MachineRegisterInfo &MRI,
                                           const TargetRegisterInfo &TRI,
                                           unsigned Depth) const {
  unsigned Op = MI.getOpcode();

  if (auto *GI = dyn_cast<GIntrinsic>(&MI)) {
    if (isFPIntrinsic(GI->getIntrinsicID()))
      return true;
  }

  // Do we have an explicit floating point instruction?
  if (isPreISelGenericFloatingPointOpcode(Op))
    return true;

  // No. Check if we have a copy-like instruction. If we do, then we could
  // still be fed by floating point instructions.
  if (Op != TargetOpcode::COPY && !MI.isPHI() &&
      !isPreISelGenericOptimizationHint(Op))
    return false;

  // Check if we already know the register bank.
  auto *RB = getRegBank(MI.getOperand(0).getReg(), MRI, TRI);
  if (RB == &PPC::FPRRegBank)
    return true;
  if (RB == &PPC::GPRRegBank)
    return false;

  // We don't know anything.
  //
  // If we have a phi, we may be able to infer that it will be assigned a FPR
  // based off of its inputs.
  if (!MI.isPHI() || Depth > MaxFPRSearchDepth)
    return false;

  return any_of(MI.explicit_uses(), [&](const MachineOperand &Op) {
    return Op.isReg() &&
           onlyDefinesFP(*MRI.getVRegDef(Op.getReg()), MRI, TRI, Depth + 1);
  });
}

/// FIXME: this is copied from target AArch64. Needs some code refactor here to
/// put this function in class RegisterBankInfo.
bool PPCRegisterBankInfo::onlyUsesFP(const MachineInstr &MI,
                                     const MachineRegisterInfo &MRI,
                                     const TargetRegisterInfo &TRI,
                                     unsigned Depth) const {
  switch (MI.getOpcode()) {
  case TargetOpcode::G_FPTOSI:
  case TargetOpcode::G_FPTOUI:
  case TargetOpcode::G_FCMP:
  case TargetOpcode::G_LROUND:
  case TargetOpcode::G_LLROUND:
    return true;
  default:
    break;
  }
  return hasFPConstraints(MI, MRI, TRI, Depth);
}

/// FIXME: this is copied from target AArch64. Needs some code refactor here to
/// put this function in class RegisterBankInfo.
bool PPCRegisterBankInfo::onlyDefinesFP(const MachineInstr &MI,
                                        const MachineRegisterInfo &MRI,
                                        const TargetRegisterInfo &TRI,
                                        unsigned Depth) const {
  switch (MI.getOpcode()) {
  case TargetOpcode::G_SITOFP:
  case TargetOpcode::G_UITOFP:
    return true;
  default:
    break;
  }
  return hasFPConstraints(MI, MRI, TRI, Depth);
}

RegisterBankInfo::InstructionMappings
PPCRegisterBankInfo::getInstrAlternativeMappings(const MachineInstr &MI) const {
  // TODO Implement.
  return RegisterBankInfo::getInstrAlternativeMappings(MI);
}
