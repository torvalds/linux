//===- AArch64RegisterInfo.cpp - AArch64 Register Information -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the AArch64 implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#include "AArch64RegisterInfo.h"
#include "AArch64FrameLowering.h"
#include "AArch64InstrInfo.h"
#include "AArch64MachineFunctionInfo.h"
#include "AArch64Subtarget.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "MCTargetDesc/AArch64InstPrinter.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

#define GET_CC_REGISTER_LISTS
#include "AArch64GenCallingConv.inc"
#define GET_REGINFO_TARGET_DESC
#include "AArch64GenRegisterInfo.inc"

AArch64RegisterInfo::AArch64RegisterInfo(const Triple &TT)
    : AArch64GenRegisterInfo(AArch64::LR), TT(TT) {
  AArch64_MC::initLLVMToCVRegMapping(this);
}

/// Return whether the register needs a CFI entry. Not all unwinders may know
/// about SVE registers, so we assume the lowest common denominator, i.e. the
/// callee-saves required by the base ABI. For the SVE registers z8-z15 only the
/// lower 64-bits (d8-d15) need to be saved. The lower 64-bits subreg is
/// returned in \p RegToUseForCFI.
bool AArch64RegisterInfo::regNeedsCFI(unsigned Reg,
                                      unsigned &RegToUseForCFI) const {
  if (AArch64::PPRRegClass.contains(Reg))
    return false;

  if (AArch64::ZPRRegClass.contains(Reg)) {
    RegToUseForCFI = getSubReg(Reg, AArch64::dsub);
    for (int I = 0; CSR_AArch64_AAPCS_SaveList[I]; ++I) {
      if (CSR_AArch64_AAPCS_SaveList[I] == RegToUseForCFI)
        return true;
    }
    return false;
  }

  RegToUseForCFI = Reg;
  return true;
}

const MCPhysReg *
AArch64RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  assert(MF && "Invalid MachineFunction pointer.");

  if (MF->getFunction().getCallingConv() == CallingConv::GHC)
    // GHC set of callee saved regs is empty as all those regs are
    // used for passing STG regs around
    return CSR_AArch64_NoRegs_SaveList;
  if (MF->getFunction().getCallingConv() == CallingConv::PreserveNone)
    return CSR_AArch64_NoneRegs_SaveList;
  if (MF->getFunction().getCallingConv() == CallingConv::AnyReg)
    return CSR_AArch64_AllRegs_SaveList;

  if (MF->getFunction().getCallingConv() == CallingConv::ARM64EC_Thunk_X64)
    return CSR_Win_AArch64_Arm64EC_Thunk_SaveList;

  // Darwin has its own CSR_AArch64_AAPCS_SaveList, which means most CSR save
  // lists depending on that will need to have their Darwin variant as well.
  if (MF->getSubtarget<AArch64Subtarget>().isTargetDarwin())
    return getDarwinCalleeSavedRegs(MF);

  if (MF->getFunction().getCallingConv() == CallingConv::CFGuard_Check)
    return CSR_Win_AArch64_CFGuard_Check_SaveList;
  if (MF->getSubtarget<AArch64Subtarget>().isTargetWindows()) {
    if (MF->getSubtarget<AArch64Subtarget>().getTargetLowering()
            ->supportSwiftError() &&
        MF->getFunction().getAttributes().hasAttrSomewhere(
            Attribute::SwiftError))
      return CSR_Win_AArch64_AAPCS_SwiftError_SaveList;
    if (MF->getFunction().getCallingConv() == CallingConv::SwiftTail)
      return CSR_Win_AArch64_AAPCS_SwiftTail_SaveList;
    return CSR_Win_AArch64_AAPCS_SaveList;
  }
  if (MF->getFunction().getCallingConv() == CallingConv::AArch64_VectorCall)
    return CSR_AArch64_AAVPCS_SaveList;
  if (MF->getFunction().getCallingConv() == CallingConv::AArch64_SVE_VectorCall)
    return CSR_AArch64_SVE_AAPCS_SaveList;
  if (MF->getFunction().getCallingConv() ==
          CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0)
    report_fatal_error(
        "Calling convention "
        "AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0 is only "
        "supported to improve calls to SME ACLE save/restore/disable-za "
        "functions, and is not intended to be used beyond that scope.");
  if (MF->getFunction().getCallingConv() ==
      CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X1)
    report_fatal_error(
        "Calling convention "
        "AArch64_SME_ABI_Support_Routines_PreserveMost_From_X1 is "
        "only supported to improve calls to SME ACLE __arm_get_current_vg "
        "function, and is not intended to be used beyond that scope.");
  if (MF->getFunction().getCallingConv() ==
          CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2)
    report_fatal_error(
        "Calling convention "
        "AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2 is "
        "only supported to improve calls to SME ACLE __arm_sme_state "
        "and is not intended to be used beyond that scope.");
  if (MF->getSubtarget<AArch64Subtarget>().getTargetLowering()
          ->supportSwiftError() &&
      MF->getFunction().getAttributes().hasAttrSomewhere(
          Attribute::SwiftError))
    return CSR_AArch64_AAPCS_SwiftError_SaveList;
  if (MF->getFunction().getCallingConv() == CallingConv::SwiftTail)
    return CSR_AArch64_AAPCS_SwiftTail_SaveList;
  if (MF->getFunction().getCallingConv() == CallingConv::PreserveMost)
    return CSR_AArch64_RT_MostRegs_SaveList;
  if (MF->getFunction().getCallingConv() == CallingConv::PreserveAll)
    return CSR_AArch64_RT_AllRegs_SaveList;
  if (MF->getFunction().getCallingConv() == CallingConv::Win64)
    // This is for OSes other than Windows; Windows is a separate case further
    // above.
    return CSR_AArch64_AAPCS_X18_SaveList;
  if (MF->getInfo<AArch64FunctionInfo>()->isSVECC())
    return CSR_AArch64_SVE_AAPCS_SaveList;
  return CSR_AArch64_AAPCS_SaveList;
}

const MCPhysReg *
AArch64RegisterInfo::getDarwinCalleeSavedRegs(const MachineFunction *MF) const {
  assert(MF && "Invalid MachineFunction pointer.");
  assert(MF->getSubtarget<AArch64Subtarget>().isTargetDarwin() &&
         "Invalid subtarget for getDarwinCalleeSavedRegs");

  if (MF->getFunction().getCallingConv() == CallingConv::CFGuard_Check)
    report_fatal_error(
        "Calling convention CFGuard_Check is unsupported on Darwin.");
  if (MF->getFunction().getCallingConv() == CallingConv::AArch64_VectorCall)
    return CSR_Darwin_AArch64_AAVPCS_SaveList;
  if (MF->getFunction().getCallingConv() == CallingConv::AArch64_SVE_VectorCall)
    report_fatal_error(
        "Calling convention SVE_VectorCall is unsupported on Darwin.");
  if (MF->getFunction().getCallingConv() ==
          CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0)
    report_fatal_error(
        "Calling convention "
        "AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0 is "
        "only supported to improve calls to SME ACLE save/restore/disable-za "
        "functions, and is not intended to be used beyond that scope.");
  if (MF->getFunction().getCallingConv() ==
      CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X1)
    report_fatal_error(
        "Calling convention "
        "AArch64_SME_ABI_Support_Routines_PreserveMost_From_X1 is "
        "only supported to improve calls to SME ACLE __arm_get_current_vg "
        "function, and is not intended to be used beyond that scope.");
  if (MF->getFunction().getCallingConv() ==
          CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2)
    report_fatal_error(
        "Calling convention "
        "AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2 is "
        "only supported to improve calls to SME ACLE __arm_sme_state "
        "and is not intended to be used beyond that scope.");
  if (MF->getFunction().getCallingConv() == CallingConv::CXX_FAST_TLS)
    return MF->getInfo<AArch64FunctionInfo>()->isSplitCSR()
               ? CSR_Darwin_AArch64_CXX_TLS_PE_SaveList
               : CSR_Darwin_AArch64_CXX_TLS_SaveList;
  if (MF->getSubtarget<AArch64Subtarget>().getTargetLowering()
          ->supportSwiftError() &&
      MF->getFunction().getAttributes().hasAttrSomewhere(
          Attribute::SwiftError))
    return CSR_Darwin_AArch64_AAPCS_SwiftError_SaveList;
  if (MF->getFunction().getCallingConv() == CallingConv::SwiftTail)
    return CSR_Darwin_AArch64_AAPCS_SwiftTail_SaveList;
  if (MF->getFunction().getCallingConv() == CallingConv::PreserveMost)
    return CSR_Darwin_AArch64_RT_MostRegs_SaveList;
  if (MF->getFunction().getCallingConv() == CallingConv::PreserveAll)
    return CSR_Darwin_AArch64_RT_AllRegs_SaveList;
  if (MF->getFunction().getCallingConv() == CallingConv::Win64)
    return CSR_Darwin_AArch64_AAPCS_Win64_SaveList;
  return CSR_Darwin_AArch64_AAPCS_SaveList;
}

const MCPhysReg *AArch64RegisterInfo::getCalleeSavedRegsViaCopy(
    const MachineFunction *MF) const {
  assert(MF && "Invalid MachineFunction pointer.");
  if (MF->getFunction().getCallingConv() == CallingConv::CXX_FAST_TLS &&
      MF->getInfo<AArch64FunctionInfo>()->isSplitCSR())
    return CSR_Darwin_AArch64_CXX_TLS_ViaCopy_SaveList;
  return nullptr;
}

void AArch64RegisterInfo::UpdateCustomCalleeSavedRegs(
    MachineFunction &MF) const {
  const MCPhysReg *CSRs = getCalleeSavedRegs(&MF);
  SmallVector<MCPhysReg, 32> UpdatedCSRs;
  for (const MCPhysReg *I = CSRs; *I; ++I)
    UpdatedCSRs.push_back(*I);

  for (size_t i = 0; i < AArch64::GPR64commonRegClass.getNumRegs(); ++i) {
    if (MF.getSubtarget<AArch64Subtarget>().isXRegCustomCalleeSaved(i)) {
      UpdatedCSRs.push_back(AArch64::GPR64commonRegClass.getRegister(i));
    }
  }
  // Register lists are zero-terminated.
  UpdatedCSRs.push_back(0);
  MF.getRegInfo().setCalleeSavedRegs(UpdatedCSRs);
}

const TargetRegisterClass *
AArch64RegisterInfo::getSubClassWithSubReg(const TargetRegisterClass *RC,
                                       unsigned Idx) const {
  // edge case for GPR/FPR register classes
  if (RC == &AArch64::GPR32allRegClass && Idx == AArch64::hsub)
    return &AArch64::FPR32RegClass;
  else if (RC == &AArch64::GPR64allRegClass && Idx == AArch64::hsub)
    return &AArch64::FPR64RegClass;

  // Forward to TableGen's default version.
  return AArch64GenRegisterInfo::getSubClassWithSubReg(RC, Idx);
}

const uint32_t *
AArch64RegisterInfo::getDarwinCallPreservedMask(const MachineFunction &MF,
                                                CallingConv::ID CC) const {
  assert(MF.getSubtarget<AArch64Subtarget>().isTargetDarwin() &&
         "Invalid subtarget for getDarwinCallPreservedMask");

  if (CC == CallingConv::CXX_FAST_TLS)
    return CSR_Darwin_AArch64_CXX_TLS_RegMask;
  if (CC == CallingConv::AArch64_VectorCall)
    return CSR_Darwin_AArch64_AAVPCS_RegMask;
  if (CC == CallingConv::AArch64_SVE_VectorCall)
    report_fatal_error(
        "Calling convention SVE_VectorCall is unsupported on Darwin.");
  if (CC == CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0)
    return CSR_AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0_RegMask;
  if (CC == CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X1)
    return CSR_AArch64_SME_ABI_Support_Routines_PreserveMost_From_X1_RegMask;
  if (CC == CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2)
    return CSR_AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2_RegMask;
  if (CC == CallingConv::CFGuard_Check)
    report_fatal_error(
        "Calling convention CFGuard_Check is unsupported on Darwin.");
  if (MF.getSubtarget<AArch64Subtarget>()
          .getTargetLowering()
          ->supportSwiftError() &&
      MF.getFunction().getAttributes().hasAttrSomewhere(Attribute::SwiftError))
    return CSR_Darwin_AArch64_AAPCS_SwiftError_RegMask;
  if (CC == CallingConv::SwiftTail)
    return CSR_Darwin_AArch64_AAPCS_SwiftTail_RegMask;
  if (CC == CallingConv::PreserveMost)
    return CSR_Darwin_AArch64_RT_MostRegs_RegMask;
  if (CC == CallingConv::PreserveAll)
    return CSR_Darwin_AArch64_RT_AllRegs_RegMask;
  return CSR_Darwin_AArch64_AAPCS_RegMask;
}

const uint32_t *
AArch64RegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                          CallingConv::ID CC) const {
  bool SCS = MF.getFunction().hasFnAttribute(Attribute::ShadowCallStack);
  if (CC == CallingConv::GHC)
    // This is academic because all GHC calls are (supposed to be) tail calls
    return SCS ? CSR_AArch64_NoRegs_SCS_RegMask : CSR_AArch64_NoRegs_RegMask;
  if (CC == CallingConv::PreserveNone)
    return SCS ? CSR_AArch64_NoneRegs_SCS_RegMask
               : CSR_AArch64_NoneRegs_RegMask;
  if (CC == CallingConv::AnyReg)
    return SCS ? CSR_AArch64_AllRegs_SCS_RegMask : CSR_AArch64_AllRegs_RegMask;

  // All the following calling conventions are handled differently on Darwin.
  if (MF.getSubtarget<AArch64Subtarget>().isTargetDarwin()) {
    if (SCS)
      report_fatal_error("ShadowCallStack attribute not supported on Darwin.");
    return getDarwinCallPreservedMask(MF, CC);
  }

  if (CC == CallingConv::AArch64_VectorCall)
    return SCS ? CSR_AArch64_AAVPCS_SCS_RegMask : CSR_AArch64_AAVPCS_RegMask;
  if (CC == CallingConv::AArch64_SVE_VectorCall)
    return SCS ? CSR_AArch64_SVE_AAPCS_SCS_RegMask
               : CSR_AArch64_SVE_AAPCS_RegMask;
  if (CC == CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0)
    return CSR_AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0_RegMask;
  if (CC == CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X1)
    return CSR_AArch64_SME_ABI_Support_Routines_PreserveMost_From_X1_RegMask;
  if (CC == CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2)
    return CSR_AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2_RegMask;
  if (CC == CallingConv::CFGuard_Check)
    return CSR_Win_AArch64_CFGuard_Check_RegMask;
  if (MF.getSubtarget<AArch64Subtarget>().getTargetLowering()
          ->supportSwiftError() &&
      MF.getFunction().getAttributes().hasAttrSomewhere(Attribute::SwiftError))
    return SCS ? CSR_AArch64_AAPCS_SwiftError_SCS_RegMask
               : CSR_AArch64_AAPCS_SwiftError_RegMask;
  if (CC == CallingConv::SwiftTail) {
    if (SCS)
      report_fatal_error("ShadowCallStack attribute not supported with swifttail");
    return CSR_AArch64_AAPCS_SwiftTail_RegMask;
  }
  if (CC == CallingConv::PreserveMost)
    return SCS ? CSR_AArch64_RT_MostRegs_SCS_RegMask
               : CSR_AArch64_RT_MostRegs_RegMask;
  if (CC == CallingConv::PreserveAll)
    return SCS ? CSR_AArch64_RT_AllRegs_SCS_RegMask
               : CSR_AArch64_RT_AllRegs_RegMask;

  return SCS ? CSR_AArch64_AAPCS_SCS_RegMask : CSR_AArch64_AAPCS_RegMask;
}

const uint32_t *AArch64RegisterInfo::getCustomEHPadPreservedMask(
    const MachineFunction &MF) const {
  if (MF.getSubtarget<AArch64Subtarget>().isTargetLinux())
    return CSR_AArch64_AAPCS_RegMask;

  return nullptr;
}

const uint32_t *AArch64RegisterInfo::getTLSCallPreservedMask() const {
  if (TT.isOSDarwin())
    return CSR_Darwin_AArch64_TLS_RegMask;

  assert(TT.isOSBinFormatELF() && "Invalid target");
  return CSR_AArch64_TLS_ELF_RegMask;
}

void AArch64RegisterInfo::UpdateCustomCallPreservedMask(MachineFunction &MF,
                                                 const uint32_t **Mask) const {
  uint32_t *UpdatedMask = MF.allocateRegMask();
  unsigned RegMaskSize = MachineOperand::getRegMaskSize(getNumRegs());
  memcpy(UpdatedMask, *Mask, sizeof(UpdatedMask[0]) * RegMaskSize);

  for (size_t i = 0; i < AArch64::GPR64commonRegClass.getNumRegs(); ++i) {
    if (MF.getSubtarget<AArch64Subtarget>().isXRegCustomCalleeSaved(i)) {
      for (MCPhysReg SubReg :
           subregs_inclusive(AArch64::GPR64commonRegClass.getRegister(i))) {
        // See TargetRegisterInfo::getCallPreservedMask for how to interpret the
        // register mask.
        UpdatedMask[SubReg / 32] |= 1u << (SubReg % 32);
      }
    }
  }
  *Mask = UpdatedMask;
}

const uint32_t *AArch64RegisterInfo::getSMStartStopCallPreservedMask() const {
  return CSR_AArch64_SMStartStop_RegMask;
}

const uint32_t *
AArch64RegisterInfo::SMEABISupportRoutinesCallPreservedMaskFromX0() const {
  return CSR_AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0_RegMask;
}

const uint32_t *AArch64RegisterInfo::getNoPreservedMask() const {
  return CSR_AArch64_NoRegs_RegMask;
}

const uint32_t *
AArch64RegisterInfo::getThisReturnPreservedMask(const MachineFunction &MF,
                                                CallingConv::ID CC) const {
  // This should return a register mask that is the same as that returned by
  // getCallPreservedMask but that additionally preserves the register used for
  // the first i64 argument (which must also be the register used to return a
  // single i64 return value)
  //
  // In case that the calling convention does not use the same register for
  // both, the function should return NULL (does not currently apply)
  assert(CC != CallingConv::GHC && "should not be GHC calling convention.");
  if (MF.getSubtarget<AArch64Subtarget>().isTargetDarwin())
    return CSR_Darwin_AArch64_AAPCS_ThisReturn_RegMask;
  return CSR_AArch64_AAPCS_ThisReturn_RegMask;
}

const uint32_t *AArch64RegisterInfo::getWindowsStackProbePreservedMask() const {
  return CSR_AArch64_StackProbe_Windows_RegMask;
}

std::optional<std::string>
AArch64RegisterInfo::explainReservedReg(const MachineFunction &MF,
                                        MCRegister PhysReg) const {
  if (hasBasePointer(MF) && MCRegisterInfo::regsOverlap(PhysReg, AArch64::X19))
    return std::string("X19 is used as the frame base pointer register.");

  if (MF.getSubtarget<AArch64Subtarget>().isWindowsArm64EC()) {
    bool warn = false;
    if (MCRegisterInfo::regsOverlap(PhysReg, AArch64::X13) ||
        MCRegisterInfo::regsOverlap(PhysReg, AArch64::X14) ||
        MCRegisterInfo::regsOverlap(PhysReg, AArch64::X23) ||
        MCRegisterInfo::regsOverlap(PhysReg, AArch64::X24) ||
        MCRegisterInfo::regsOverlap(PhysReg, AArch64::X28))
      warn = true;

    for (unsigned i = AArch64::B16; i <= AArch64::B31; ++i)
      if (MCRegisterInfo::regsOverlap(PhysReg, i))
        warn = true;

    if (warn)
      return std::string(AArch64InstPrinter::getRegisterName(PhysReg)) +
             " is clobbered by asynchronous signals when using Arm64EC.";
  }

  return {};
}

BitVector
AArch64RegisterInfo::getStrictlyReservedRegs(const MachineFunction &MF) const {
  const AArch64FrameLowering *TFI = getFrameLowering(MF);

  // FIXME: avoid re-calculating this every time.
  BitVector Reserved(getNumRegs());
  markSuperRegs(Reserved, AArch64::WSP);
  markSuperRegs(Reserved, AArch64::WZR);

  if (TFI->hasFP(MF) || TT.isOSDarwin())
    markSuperRegs(Reserved, AArch64::W29);

  if (MF.getSubtarget<AArch64Subtarget>().isWindowsArm64EC()) {
    // x13, x14, x23, x24, x28, and v16-v31 are clobbered by asynchronous
    // signals, so we can't ever use them.
    markSuperRegs(Reserved, AArch64::W13);
    markSuperRegs(Reserved, AArch64::W14);
    markSuperRegs(Reserved, AArch64::W23);
    markSuperRegs(Reserved, AArch64::W24);
    markSuperRegs(Reserved, AArch64::W28);
    for (unsigned i = AArch64::B16; i <= AArch64::B31; ++i)
      markSuperRegs(Reserved, i);
  }

  for (size_t i = 0; i < AArch64::GPR32commonRegClass.getNumRegs(); ++i) {
    if (MF.getSubtarget<AArch64Subtarget>().isXRegisterReserved(i))
      markSuperRegs(Reserved, AArch64::GPR32commonRegClass.getRegister(i));
  }

  if (hasBasePointer(MF))
    markSuperRegs(Reserved, AArch64::W19);

  // SLH uses register W16/X16 as the taint register.
  if (MF.getFunction().hasFnAttribute(Attribute::SpeculativeLoadHardening))
    markSuperRegs(Reserved, AArch64::W16);

  // FFR is modelled as global state that cannot be allocated.
  if (MF.getSubtarget<AArch64Subtarget>().hasSVE())
    Reserved.set(AArch64::FFR);

  // SME tiles are not allocatable.
  if (MF.getSubtarget<AArch64Subtarget>().hasSME()) {
    for (MCPhysReg SubReg : subregs_inclusive(AArch64::ZA))
      Reserved.set(SubReg);
  }

  // VG cannot be allocated
  Reserved.set(AArch64::VG);

  if (MF.getSubtarget<AArch64Subtarget>().hasSME2()) {
    for (MCSubRegIterator SubReg(AArch64::ZT0, this, /*self=*/true);
         SubReg.isValid(); ++SubReg)
      Reserved.set(*SubReg);
  }

  markSuperRegs(Reserved, AArch64::FPCR);
  markSuperRegs(Reserved, AArch64::FPSR);

  if (MF.getFunction().getCallingConv() == CallingConv::GRAAL) {
    markSuperRegs(Reserved, AArch64::X27);
    markSuperRegs(Reserved, AArch64::X28);
    markSuperRegs(Reserved, AArch64::W27);
    markSuperRegs(Reserved, AArch64::W28);
  }

  assert(checkAllSuperRegsMarked(Reserved));
  return Reserved;
}

BitVector
AArch64RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved = getStrictlyReservedRegs(MF);

  for (size_t i = 0; i < AArch64::GPR32commonRegClass.getNumRegs(); ++i) {
    if (MF.getSubtarget<AArch64Subtarget>().isXRegisterReservedForRA(i))
      markSuperRegs(Reserved, AArch64::GPR32commonRegClass.getRegister(i));
  }

  if (MF.getSubtarget<AArch64Subtarget>().isLRReservedForRA()) {
    // In order to prevent the register allocator from using LR, we need to
    // mark it as reserved. However we don't want to keep it reserved throughout
    // the pipeline since it prevents other infrastructure from reasoning about
    // it's liveness. We use the NoVRegs property instead of IsSSA because
    // IsSSA is removed before VirtRegRewriter runs.
    if (!MF.getProperties().hasProperty(
            MachineFunctionProperties::Property::NoVRegs))
      markSuperRegs(Reserved, AArch64::LR);
  }

  assert(checkAllSuperRegsMarked(Reserved));
  return Reserved;
}

bool AArch64RegisterInfo::isReservedReg(const MachineFunction &MF,
                                        MCRegister Reg) const {
  return getReservedRegs(MF)[Reg];
}

bool AArch64RegisterInfo::isStrictlyReservedReg(const MachineFunction &MF,
                                                MCRegister Reg) const {
  return getStrictlyReservedRegs(MF)[Reg];
}

bool AArch64RegisterInfo::isAnyArgRegReserved(const MachineFunction &MF) const {
  return llvm::any_of(*AArch64::GPR64argRegClass.MC, [this, &MF](MCPhysReg r) {
    return isStrictlyReservedReg(MF, r);
  });
}

void AArch64RegisterInfo::emitReservedArgRegCallError(
    const MachineFunction &MF) const {
  const Function &F = MF.getFunction();
  F.getContext().diagnose(DiagnosticInfoUnsupported{F, ("AArch64 doesn't support"
    " function calls if any of the argument registers is reserved.")});
}

bool AArch64RegisterInfo::isAsmClobberable(const MachineFunction &MF,
                                          MCRegister PhysReg) const {
  // SLH uses register X16 as the taint register but it will fallback to a different
  // method if the user clobbers it. So X16 is not reserved for inline asm but is
  // for normal codegen.
  if (MF.getFunction().hasFnAttribute(Attribute::SpeculativeLoadHardening) &&
        MCRegisterInfo::regsOverlap(PhysReg, AArch64::X16))
    return true;

  // ZA/ZT0 registers are reserved but may be permitted in the clobber list.
  if (PhysReg == AArch64::ZA || PhysReg == AArch64::ZT0)
    return true;

  return !isReservedReg(MF, PhysReg);
}

const TargetRegisterClass *
AArch64RegisterInfo::getPointerRegClass(const MachineFunction &MF,
                                      unsigned Kind) const {
  return &AArch64::GPR64spRegClass;
}

const TargetRegisterClass *
AArch64RegisterInfo::getCrossCopyRegClass(const TargetRegisterClass *RC) const {
  if (RC == &AArch64::CCRRegClass)
    return &AArch64::GPR64RegClass; // Only MSR & MRS copy NZCV.
  return RC;
}

unsigned AArch64RegisterInfo::getBaseRegister() const { return AArch64::X19; }

bool AArch64RegisterInfo::hasBasePointer(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // In the presence of variable sized objects or funclets, if the fixed stack
  // size is large enough that referencing from the FP won't result in things
  // being in range relatively often, we can use a base pointer to allow access
  // from the other direction like the SP normally works.
  //
  // Furthermore, if both variable sized objects are present, and the
  // stack needs to be dynamically re-aligned, the base pointer is the only
  // reliable way to reference the locals.
  if (MFI.hasVarSizedObjects() || MF.hasEHFunclets()) {
    if (hasStackRealignment(MF))
      return true;

    auto &ST = MF.getSubtarget<AArch64Subtarget>();
    if (ST.hasSVE() || ST.isStreaming()) {
      const AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();
      // Frames that have variable sized objects and scalable SVE objects,
      // should always use a basepointer.
      if (!AFI->hasCalculatedStackSizeSVE() || AFI->getStackSizeSVE())
        return true;
    }

    // Conservatively estimate whether the negative offset from the frame
    // pointer will be sufficient to reach. If a function has a smallish
    // frame, it's less likely to have lots of spills and callee saved
    // space, so it's all more likely to be within range of the frame pointer.
    // If it's wrong, we'll materialize the constant and still get to the
    // object; it's just suboptimal. Negative offsets use the unscaled
    // load/store instructions, which have a 9-bit signed immediate.
    return MFI.getLocalFrameSize() >= 256;
  }

  return false;
}

bool AArch64RegisterInfo::isArgumentRegister(const MachineFunction &MF,
                                             MCRegister Reg) const {
  CallingConv::ID CC = MF.getFunction().getCallingConv();
  const AArch64Subtarget &STI = MF.getSubtarget<AArch64Subtarget>();
  bool IsVarArg = STI.isCallingConvWin64(MF.getFunction().getCallingConv(),
                                         MF.getFunction().isVarArg());

  auto HasReg = [](ArrayRef<MCRegister> RegList, MCRegister Reg) {
    return llvm::is_contained(RegList, Reg);
  };

  switch (CC) {
  default:
    report_fatal_error("Unsupported calling convention.");
  case CallingConv::GHC:
    return HasReg(CC_AArch64_GHC_ArgRegs, Reg);
  case CallingConv::PreserveNone:
    if (!MF.getFunction().isVarArg())
      return HasReg(CC_AArch64_Preserve_None_ArgRegs, Reg);
    [[fallthrough]];
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::PreserveMost:
  case CallingConv::PreserveAll:
  case CallingConv::CXX_FAST_TLS:
  case CallingConv::Swift:
  case CallingConv::SwiftTail:
  case CallingConv::Tail:
    if (STI.isTargetWindows()) {
      if (IsVarArg)
        return HasReg(CC_AArch64_Win64_VarArg_ArgRegs, Reg);
      switch (CC) {
      default:
        return HasReg(CC_AArch64_Win64PCS_ArgRegs, Reg);
      case CallingConv::Swift:
      case CallingConv::SwiftTail:
        return HasReg(CC_AArch64_Win64PCS_Swift_ArgRegs, Reg) ||
               HasReg(CC_AArch64_Win64PCS_ArgRegs, Reg);
      }
    }
    if (!STI.isTargetDarwin()) {
      switch (CC) {
      default:
        return HasReg(CC_AArch64_AAPCS_ArgRegs, Reg);
      case CallingConv::Swift:
      case CallingConv::SwiftTail:
        return HasReg(CC_AArch64_AAPCS_ArgRegs, Reg) ||
               HasReg(CC_AArch64_AAPCS_Swift_ArgRegs, Reg);
      }
    }
    if (!IsVarArg) {
      switch (CC) {
      default:
        return HasReg(CC_AArch64_DarwinPCS_ArgRegs, Reg);
      case CallingConv::Swift:
      case CallingConv::SwiftTail:
        return HasReg(CC_AArch64_DarwinPCS_ArgRegs, Reg) ||
               HasReg(CC_AArch64_DarwinPCS_Swift_ArgRegs, Reg);
      }
    }
    if (STI.isTargetILP32())
      return HasReg(CC_AArch64_DarwinPCS_ILP32_VarArg_ArgRegs, Reg);
    return HasReg(CC_AArch64_DarwinPCS_VarArg_ArgRegs, Reg);
  case CallingConv::Win64:
    if (IsVarArg)
      HasReg(CC_AArch64_Win64_VarArg_ArgRegs, Reg);
    return HasReg(CC_AArch64_Win64PCS_ArgRegs, Reg);
  case CallingConv::CFGuard_Check:
    return HasReg(CC_AArch64_Win64_CFGuard_Check_ArgRegs, Reg);
  case CallingConv::AArch64_VectorCall:
  case CallingConv::AArch64_SVE_VectorCall:
  case CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X0:
  case CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X1:
  case CallingConv::AArch64_SME_ABI_Support_Routines_PreserveMost_From_X2:
    if (STI.isTargetWindows())
      return HasReg(CC_AArch64_Win64PCS_ArgRegs, Reg);
    return HasReg(CC_AArch64_AAPCS_ArgRegs, Reg);
  }
}

Register
AArch64RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const AArch64FrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? AArch64::FP : AArch64::SP;
}

bool AArch64RegisterInfo::requiresRegisterScavenging(
    const MachineFunction &MF) const {
  return true;
}

bool AArch64RegisterInfo::requiresVirtualBaseRegisters(
    const MachineFunction &MF) const {
  return true;
}

bool
AArch64RegisterInfo::useFPForScavengingIndex(const MachineFunction &MF) const {
  // This function indicates whether the emergency spillslot should be placed
  // close to the beginning of the stackframe (closer to FP) or the end
  // (closer to SP).
  //
  // The beginning works most reliably if we have a frame pointer.
  // In the presence of any non-constant space between FP and locals,
  // (e.g. in case of stack realignment or a scalable SVE area), it is
  // better to use SP or BP.
  const AArch64FrameLowering &TFI = *getFrameLowering(MF);
  const AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();
  assert((!MF.getSubtarget<AArch64Subtarget>().hasSVE() ||
          AFI->hasCalculatedStackSizeSVE()) &&
         "Expected SVE area to be calculated by this point");
  return TFI.hasFP(MF) && !hasStackRealignment(MF) && !AFI->getStackSizeSVE();
}

bool AArch64RegisterInfo::requiresFrameIndexScavenging(
    const MachineFunction &MF) const {
  return true;
}

bool
AArch64RegisterInfo::cannotEliminateFrame(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (MF.getTarget().Options.DisableFramePointerElim(MF) && MFI.adjustsStack())
    return true;
  return MFI.hasVarSizedObjects() || MFI.isFrameAddressTaken();
}

/// needsFrameBaseReg - Returns true if the instruction's frame index
/// reference would be better served by a base register other than FP
/// or SP. Used by LocalStackFrameAllocation to determine which frame index
/// references it should create new base registers for.
bool AArch64RegisterInfo::needsFrameBaseReg(MachineInstr *MI,
                                            int64_t Offset) const {
  for (unsigned i = 0; !MI->getOperand(i).isFI(); ++i)
    assert(i < MI->getNumOperands() &&
           "Instr doesn't have FrameIndex operand!");

  // It's the load/store FI references that cause issues, as it can be difficult
  // to materialize the offset if it won't fit in the literal field. Estimate
  // based on the size of the local frame and some conservative assumptions
  // about the rest of the stack frame (note, this is pre-regalloc, so
  // we don't know everything for certain yet) whether this offset is likely
  // to be out of range of the immediate. Return true if so.

  // We only generate virtual base registers for loads and stores, so
  // return false for everything else.
  if (!MI->mayLoad() && !MI->mayStore())
    return false;

  // Without a virtual base register, if the function has variable sized
  // objects, all fixed-size local references will be via the frame pointer,
  // Approximate the offset and see if it's legal for the instruction.
  // Note that the incoming offset is based on the SP value at function entry,
  // so it'll be negative.
  MachineFunction &MF = *MI->getParent()->getParent();
  const AArch64FrameLowering *TFI = getFrameLowering(MF);
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Estimate an offset from the frame pointer.
  // Conservatively assume all GPR callee-saved registers get pushed.
  // FP, LR, X19-X28, D8-D15. 64-bits each.
  int64_t FPOffset = Offset - 16 * 20;
  // Estimate an offset from the stack pointer.
  // The incoming offset is relating to the SP at the start of the function,
  // but when we access the local it'll be relative to the SP after local
  // allocation, so adjust our SP-relative offset by that allocation size.
  Offset += MFI.getLocalFrameSize();
  // Assume that we'll have at least some spill slots allocated.
  // FIXME: This is a total SWAG number. We should run some statistics
  //        and pick a real one.
  Offset += 128; // 128 bytes of spill slots

  // If there is a frame pointer, try using it.
  // The FP is only available if there is no dynamic realignment. We
  // don't know for sure yet whether we'll need that, so we guess based
  // on whether there are any local variables that would trigger it.
  if (TFI->hasFP(MF) && isFrameOffsetLegal(MI, AArch64::FP, FPOffset))
    return false;

  // If we can reference via the stack pointer or base pointer, try that.
  // FIXME: This (and the code that resolves the references) can be improved
  //        to only disallow SP relative references in the live range of
  //        the VLA(s). In practice, it's unclear how much difference that
  //        would make, but it may be worth doing.
  if (isFrameOffsetLegal(MI, AArch64::SP, Offset))
    return false;

  // If even offset 0 is illegal, we don't want a virtual base register.
  if (!isFrameOffsetLegal(MI, AArch64::SP, 0))
    return false;

  // The offset likely isn't legal; we want to allocate a virtual base register.
  return true;
}

bool AArch64RegisterInfo::isFrameOffsetLegal(const MachineInstr *MI,
                                             Register BaseReg,
                                             int64_t Offset) const {
  assert(MI && "Unable to get the legal offset for nil instruction.");
  StackOffset SaveOffset = StackOffset::getFixed(Offset);
  return isAArch64FrameOffsetLegal(*MI, SaveOffset) & AArch64FrameOffsetIsLegal;
}

/// Insert defining instruction(s) for BaseReg to be a pointer to FrameIdx
/// at the beginning of the basic block.
Register
AArch64RegisterInfo::materializeFrameBaseRegister(MachineBasicBlock *MBB,
                                                  int FrameIdx,
                                                  int64_t Offset) const {
  MachineBasicBlock::iterator Ins = MBB->begin();
  DebugLoc DL; // Defaults to "unknown"
  if (Ins != MBB->end())
    DL = Ins->getDebugLoc();
  const MachineFunction &MF = *MBB->getParent();
  const AArch64InstrInfo *TII =
      MF.getSubtarget<AArch64Subtarget>().getInstrInfo();
  const MCInstrDesc &MCID = TII->get(AArch64::ADDXri);
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  Register BaseReg = MRI.createVirtualRegister(&AArch64::GPR64spRegClass);
  MRI.constrainRegClass(BaseReg, TII->getRegClass(MCID, 0, this, MF));
  unsigned Shifter = AArch64_AM::getShifterImm(AArch64_AM::LSL, 0);

  BuildMI(*MBB, Ins, DL, MCID, BaseReg)
      .addFrameIndex(FrameIdx)
      .addImm(Offset)
      .addImm(Shifter);

  return BaseReg;
}

void AArch64RegisterInfo::resolveFrameIndex(MachineInstr &MI, Register BaseReg,
                                            int64_t Offset) const {
  // ARM doesn't need the general 64-bit offsets
  StackOffset Off = StackOffset::getFixed(Offset);

  unsigned i = 0;
  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }

  const MachineFunction *MF = MI.getParent()->getParent();
  const AArch64InstrInfo *TII =
      MF->getSubtarget<AArch64Subtarget>().getInstrInfo();
  bool Done = rewriteAArch64FrameIndex(MI, i, BaseReg, Off, TII);
  assert(Done && "Unable to resolve frame index!");
  (void)Done;
}

// Create a scratch register for the frame index elimination in an instruction.
// This function has special handling of stack tagging loop pseudos, in which
// case it can also change the instruction opcode.
static Register
createScratchRegisterForInstruction(MachineInstr &MI, unsigned FIOperandNum,
                                    const AArch64InstrInfo *TII) {
  // ST*Gloop have a reserved scratch register in operand 1. Use it, and also
  // replace the instruction with the writeback variant because it will now
  // satisfy the operand constraints for it.
  Register ScratchReg;
  if (MI.getOpcode() == AArch64::STGloop ||
      MI.getOpcode() == AArch64::STZGloop) {
    assert(FIOperandNum == 3 &&
           "Wrong frame index operand for STGloop/STZGloop");
    unsigned Op = MI.getOpcode() == AArch64::STGloop ? AArch64::STGloop_wback
                                                     : AArch64::STZGloop_wback;
    ScratchReg = MI.getOperand(1).getReg();
    MI.getOperand(3).ChangeToRegister(ScratchReg, false, false, true);
    MI.setDesc(TII->get(Op));
    MI.tieOperands(1, 3);
  } else {
    ScratchReg =
        MI.getMF()->getRegInfo().createVirtualRegister(&AArch64::GPR64RegClass);
    MI.getOperand(FIOperandNum)
        .ChangeToRegister(ScratchReg, false, false, true);
  }
  return ScratchReg;
}

void AArch64RegisterInfo::getOffsetOpcodes(
    const StackOffset &Offset, SmallVectorImpl<uint64_t> &Ops) const {
  // The smallest scalable element supported by scaled SVE addressing
  // modes are predicates, which are 2 scalable bytes in size. So the scalable
  // byte offset must always be a multiple of 2.
  assert(Offset.getScalable() % 2 == 0 && "Invalid frame offset");

  // Add fixed-sized offset using existing DIExpression interface.
  DIExpression::appendOffset(Ops, Offset.getFixed());

  unsigned VG = getDwarfRegNum(AArch64::VG, true);
  int64_t VGSized = Offset.getScalable() / 2;
  if (VGSized > 0) {
    Ops.push_back(dwarf::DW_OP_constu);
    Ops.push_back(VGSized);
    Ops.append({dwarf::DW_OP_bregx, VG, 0ULL});
    Ops.push_back(dwarf::DW_OP_mul);
    Ops.push_back(dwarf::DW_OP_plus);
  } else if (VGSized < 0) {
    Ops.push_back(dwarf::DW_OP_constu);
    Ops.push_back(-VGSized);
    Ops.append({dwarf::DW_OP_bregx, VG, 0ULL});
    Ops.push_back(dwarf::DW_OP_mul);
    Ops.push_back(dwarf::DW_OP_minus);
  }
}

bool AArch64RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                              int SPAdj, unsigned FIOperandNum,
                                              RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");

  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const AArch64InstrInfo *TII =
      MF.getSubtarget<AArch64Subtarget>().getInstrInfo();
  const AArch64FrameLowering *TFI = getFrameLowering(MF);
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  bool Tagged =
      MI.getOperand(FIOperandNum).getTargetFlags() & AArch64II::MO_TAGGED;
  Register FrameReg;

  // Special handling of dbg_value, stackmap patchpoint statepoint instructions.
  if (MI.getOpcode() == TargetOpcode::STACKMAP ||
      MI.getOpcode() == TargetOpcode::PATCHPOINT ||
      MI.getOpcode() == TargetOpcode::STATEPOINT) {
    StackOffset Offset =
        TFI->resolveFrameIndexReference(MF, FrameIndex, FrameReg,
                                        /*PreferFP=*/true,
                                        /*ForSimm=*/false);
    Offset += StackOffset::getFixed(MI.getOperand(FIOperandNum + 1).getImm());
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false /*isDef*/);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset.getFixed());
    return false;
  }

  if (MI.getOpcode() == TargetOpcode::LOCAL_ESCAPE) {
    MachineOperand &FI = MI.getOperand(FIOperandNum);
    StackOffset Offset = TFI->getNonLocalFrameIndexReference(MF, FrameIndex);
    assert(!Offset.getScalable() &&
           "Frame offsets with a scalable component are not supported");
    FI.ChangeToImmediate(Offset.getFixed());
    return false;
  }

  StackOffset Offset;
  if (MI.getOpcode() == AArch64::TAGPstack) {
    // TAGPstack must use the virtual frame register in its 3rd operand.
    const AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();
    FrameReg = MI.getOperand(3).getReg();
    Offset = StackOffset::getFixed(MFI.getObjectOffset(FrameIndex) +
                                      AFI->getTaggedBasePointerOffset());
  } else if (Tagged) {
    StackOffset SPOffset = StackOffset::getFixed(
        MFI.getObjectOffset(FrameIndex) + (int64_t)MFI.getStackSize());
    if (MFI.hasVarSizedObjects() ||
        isAArch64FrameOffsetLegal(MI, SPOffset, nullptr, nullptr, nullptr) !=
            (AArch64FrameOffsetCanUpdate | AArch64FrameOffsetIsLegal)) {
      // Can't update to SP + offset in place. Precalculate the tagged pointer
      // in a scratch register.
      Offset = TFI->resolveFrameIndexReference(
          MF, FrameIndex, FrameReg, /*PreferFP=*/false, /*ForSimm=*/true);
      Register ScratchReg =
          MF.getRegInfo().createVirtualRegister(&AArch64::GPR64RegClass);
      emitFrameOffset(MBB, II, MI.getDebugLoc(), ScratchReg, FrameReg, Offset,
                      TII);
      BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(AArch64::LDG), ScratchReg)
          .addReg(ScratchReg)
          .addReg(ScratchReg)
          .addImm(0);
      MI.getOperand(FIOperandNum)
          .ChangeToRegister(ScratchReg, false, false, true);
      return false;
    }
    FrameReg = AArch64::SP;
    Offset = StackOffset::getFixed(MFI.getObjectOffset(FrameIndex) +
                                   (int64_t)MFI.getStackSize());
  } else {
    Offset = TFI->resolveFrameIndexReference(
        MF, FrameIndex, FrameReg, /*PreferFP=*/false, /*ForSimm=*/true);
  }

  // Modify MI as necessary to handle as much of 'Offset' as possible
  if (rewriteAArch64FrameIndex(MI, FIOperandNum, FrameReg, Offset, TII))
    return true;

  assert((!RS || !RS->isScavengingFrameIndex(FrameIndex)) &&
         "Emergency spill slot is out of reach");

  // If we get here, the immediate doesn't fit into the instruction.  We folded
  // as much as possible above.  Handle the rest, providing a register that is
  // SP+LargeImm.
  Register ScratchReg =
      createScratchRegisterForInstruction(MI, FIOperandNum, TII);
  emitFrameOffset(MBB, II, MI.getDebugLoc(), ScratchReg, FrameReg, Offset, TII);
  return false;
}

unsigned AArch64RegisterInfo::getRegPressureLimit(const TargetRegisterClass *RC,
                                                  MachineFunction &MF) const {
  const AArch64FrameLowering *TFI = getFrameLowering(MF);

  switch (RC->getID()) {
  default:
    return 0;
  case AArch64::GPR32RegClassID:
  case AArch64::GPR32spRegClassID:
  case AArch64::GPR32allRegClassID:
  case AArch64::GPR64spRegClassID:
  case AArch64::GPR64allRegClassID:
  case AArch64::GPR64RegClassID:
  case AArch64::GPR32commonRegClassID:
  case AArch64::GPR64commonRegClassID:
    return 32 - 1                                   // XZR/SP
              - (TFI->hasFP(MF) || TT.isOSDarwin()) // FP
              - MF.getSubtarget<AArch64Subtarget>().getNumXRegisterReserved()
              - hasBasePointer(MF);  // X19
  case AArch64::FPR8RegClassID:
  case AArch64::FPR16RegClassID:
  case AArch64::FPR32RegClassID:
  case AArch64::FPR64RegClassID:
  case AArch64::FPR128RegClassID:
    return 32;

  case AArch64::MatrixIndexGPR32_8_11RegClassID:
  case AArch64::MatrixIndexGPR32_12_15RegClassID:
    return 4;

  case AArch64::DDRegClassID:
  case AArch64::DDDRegClassID:
  case AArch64::DDDDRegClassID:
  case AArch64::QQRegClassID:
  case AArch64::QQQRegClassID:
  case AArch64::QQQQRegClassID:
    return 32;

  case AArch64::FPR128_loRegClassID:
  case AArch64::FPR64_loRegClassID:
  case AArch64::FPR16_loRegClassID:
    return 16;
  case AArch64::FPR128_0to7RegClassID:
    return 8;
  }
}

unsigned AArch64RegisterInfo::getLocalAddressRegister(
  const MachineFunction &MF) const {
  const auto &MFI = MF.getFrameInfo();
  if (!MF.hasEHFunclets() && !MFI.hasVarSizedObjects())
    return AArch64::SP;
  else if (hasStackRealignment(MF))
    return getBaseRegister();
  return getFrameRegister(MF);
}

/// SrcRC and DstRC will be morphed into NewRC if this returns true
bool AArch64RegisterInfo::shouldCoalesce(
    MachineInstr *MI, const TargetRegisterClass *SrcRC, unsigned SubReg,
    const TargetRegisterClass *DstRC, unsigned DstSubReg,
    const TargetRegisterClass *NewRC, LiveIntervals &LIS) const {
  MachineRegisterInfo &MRI = MI->getMF()->getRegInfo();

  if (MI->isCopy() &&
      ((DstRC->getID() == AArch64::GPR64RegClassID) ||
       (DstRC->getID() == AArch64::GPR64commonRegClassID)) &&
      MI->getOperand(0).getSubReg() && MI->getOperand(1).getSubReg())
    // Do not coalesce in the case of a 32-bit subregister copy
    // which implements a 32 to 64 bit zero extension
    // which relies on the upper 32 bits being zeroed.
    return false;

  auto IsCoalescerBarrier = [](const MachineInstr &MI) {
    switch (MI.getOpcode()) {
    case AArch64::COALESCER_BARRIER_FPR16:
    case AArch64::COALESCER_BARRIER_FPR32:
    case AArch64::COALESCER_BARRIER_FPR64:
    case AArch64::COALESCER_BARRIER_FPR128:
      return true;
    default:
      return false;
    }
  };

  // For calls that temporarily have to toggle streaming mode as part of the
  // call-sequence, we need to be more careful when coalescing copy instructions
  // so that we don't end up coalescing the NEON/FP result or argument register
  // with a whole Z-register, such that after coalescing the register allocator
  // will try to spill/reload the entire Z register.
  //
  // We do this by checking if the node has any defs/uses that are
  // COALESCER_BARRIER pseudos. These are 'nops' in practice, but they exist to
  // instruct the coalescer to avoid coalescing the copy.
  if (MI->isCopy() && SubReg != DstSubReg &&
      (AArch64::ZPRRegClass.hasSubClassEq(DstRC) ||
       AArch64::ZPRRegClass.hasSubClassEq(SrcRC))) {
    unsigned SrcReg = MI->getOperand(1).getReg();
    if (any_of(MRI.def_instructions(SrcReg), IsCoalescerBarrier))
      return false;
    unsigned DstReg = MI->getOperand(0).getReg();
    if (any_of(MRI.use_nodbg_instructions(DstReg), IsCoalescerBarrier))
      return false;
  }

  return true;
}

bool AArch64RegisterInfo::shouldAnalyzePhysregInMachineLoopInfo(
    MCRegister R) const {
  return R == AArch64::VG;
}
