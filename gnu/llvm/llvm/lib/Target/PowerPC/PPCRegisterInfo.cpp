//===-- PPCRegisterInfo.cpp - PowerPC Register Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the PowerPC implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#include "PPCRegisterInfo.h"
#include "PPCFrameLowering.h"
#include "PPCInstrBuilder.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <cstdlib>

using namespace llvm;

#define DEBUG_TYPE "reginfo"

#define GET_REGINFO_TARGET_DESC
#include "PPCGenRegisterInfo.inc"

STATISTIC(InflateGPRC, "Number of gprc inputs for getLargestLegalClass");
STATISTIC(InflateGP8RC, "Number of g8rc inputs for getLargestLegalClass");

static cl::opt<bool>
EnableBasePointer("ppc-use-base-pointer", cl::Hidden, cl::init(true),
         cl::desc("Enable use of a base pointer for complex stack frames"));

static cl::opt<bool>
AlwaysBasePointer("ppc-always-use-base-pointer", cl::Hidden, cl::init(false),
         cl::desc("Force the use of a base pointer in every function"));

static cl::opt<bool>
EnableGPRToVecSpills("ppc-enable-gpr-to-vsr-spills", cl::Hidden, cl::init(false),
         cl::desc("Enable spills from gpr to vsr rather than stack"));

static cl::opt<bool>
StackPtrConst("ppc-stack-ptr-caller-preserved",
                cl::desc("Consider R1 caller preserved so stack saves of "
                         "caller preserved registers can be LICM candidates"),
                cl::init(true), cl::Hidden);

static cl::opt<unsigned>
MaxCRBitSpillDist("ppc-max-crbit-spill-dist",
                  cl::desc("Maximum search distance for definition of CR bit "
                           "spill on ppc"),
                  cl::Hidden, cl::init(100));

// Copies/moves of physical accumulators are expensive operations
// that should be avoided whenever possible. MMA instructions are
// meant to be used in performance-sensitive computational kernels.
// This option is provided, at least for the time being, to give the
// user a tool to detect this expensive operation and either rework
// their code or report a compiler bug if that turns out to be the
// cause.
#ifndef NDEBUG
static cl::opt<bool>
ReportAccMoves("ppc-report-acc-moves",
               cl::desc("Emit information about accumulator register spills "
                        "and copies"),
               cl::Hidden, cl::init(false));
#endif

extern cl::opt<bool> DisableAutoPairedVecSt;

static unsigned offsetMinAlignForOpcode(unsigned OpC);

PPCRegisterInfo::PPCRegisterInfo(const PPCTargetMachine &TM)
  : PPCGenRegisterInfo(TM.isPPC64() ? PPC::LR8 : PPC::LR,
                       TM.isPPC64() ? 0 : 1,
                       TM.isPPC64() ? 0 : 1),
    TM(TM) {
  ImmToIdxMap[PPC::LD]   = PPC::LDX;    ImmToIdxMap[PPC::STD]  = PPC::STDX;
  ImmToIdxMap[PPC::LBZ]  = PPC::LBZX;   ImmToIdxMap[PPC::STB]  = PPC::STBX;
  ImmToIdxMap[PPC::LHZ]  = PPC::LHZX;   ImmToIdxMap[PPC::LHA]  = PPC::LHAX;
  ImmToIdxMap[PPC::LWZ]  = PPC::LWZX;   ImmToIdxMap[PPC::LWA]  = PPC::LWAX;
  ImmToIdxMap[PPC::LFS]  = PPC::LFSX;   ImmToIdxMap[PPC::LFD]  = PPC::LFDX;
  ImmToIdxMap[PPC::STH]  = PPC::STHX;   ImmToIdxMap[PPC::STW]  = PPC::STWX;
  ImmToIdxMap[PPC::STFS] = PPC::STFSX;  ImmToIdxMap[PPC::STFD] = PPC::STFDX;
  ImmToIdxMap[PPC::ADDI] = PPC::ADD4;
  ImmToIdxMap[PPC::LWA_32] = PPC::LWAX_32;

  // 64-bit
  ImmToIdxMap[PPC::LHA8] = PPC::LHAX8; ImmToIdxMap[PPC::LBZ8] = PPC::LBZX8;
  ImmToIdxMap[PPC::LHZ8] = PPC::LHZX8; ImmToIdxMap[PPC::LWZ8] = PPC::LWZX8;
  ImmToIdxMap[PPC::STB8] = PPC::STBX8; ImmToIdxMap[PPC::STH8] = PPC::STHX8;
  ImmToIdxMap[PPC::STW8] = PPC::STWX8; ImmToIdxMap[PPC::STDU] = PPC::STDUX;
  ImmToIdxMap[PPC::ADDI8] = PPC::ADD8;
  ImmToIdxMap[PPC::LQ] = PPC::LQX_PSEUDO;
  ImmToIdxMap[PPC::STQ] = PPC::STQX_PSEUDO;

  // VSX
  ImmToIdxMap[PPC::DFLOADf32] = PPC::LXSSPX;
  ImmToIdxMap[PPC::DFLOADf64] = PPC::LXSDX;
  ImmToIdxMap[PPC::SPILLTOVSR_LD] = PPC::SPILLTOVSR_LDX;
  ImmToIdxMap[PPC::SPILLTOVSR_ST] = PPC::SPILLTOVSR_STX;
  ImmToIdxMap[PPC::DFSTOREf32] = PPC::STXSSPX;
  ImmToIdxMap[PPC::DFSTOREf64] = PPC::STXSDX;
  ImmToIdxMap[PPC::LXV] = PPC::LXVX;
  ImmToIdxMap[PPC::LXSD] = PPC::LXSDX;
  ImmToIdxMap[PPC::LXSSP] = PPC::LXSSPX;
  ImmToIdxMap[PPC::STXV] = PPC::STXVX;
  ImmToIdxMap[PPC::STXSD] = PPC::STXSDX;
  ImmToIdxMap[PPC::STXSSP] = PPC::STXSSPX;

  // SPE
  ImmToIdxMap[PPC::EVLDD] = PPC::EVLDDX;
  ImmToIdxMap[PPC::EVSTDD] = PPC::EVSTDDX;
  ImmToIdxMap[PPC::SPESTW] = PPC::SPESTWX;
  ImmToIdxMap[PPC::SPELWZ] = PPC::SPELWZX;

  // Power10
  ImmToIdxMap[PPC::PLBZ]   = PPC::LBZX; ImmToIdxMap[PPC::PLBZ8]   = PPC::LBZX8;
  ImmToIdxMap[PPC::PLHZ]   = PPC::LHZX; ImmToIdxMap[PPC::PLHZ8]   = PPC::LHZX8;
  ImmToIdxMap[PPC::PLHA]   = PPC::LHAX; ImmToIdxMap[PPC::PLHA8]   = PPC::LHAX8;
  ImmToIdxMap[PPC::PLWZ]   = PPC::LWZX; ImmToIdxMap[PPC::PLWZ8]   = PPC::LWZX8;
  ImmToIdxMap[PPC::PLWA]   = PPC::LWAX; ImmToIdxMap[PPC::PLWA8]   = PPC::LWAX;
  ImmToIdxMap[PPC::PLD]    = PPC::LDX;  ImmToIdxMap[PPC::PSTD]   = PPC::STDX;

  ImmToIdxMap[PPC::PSTB]   = PPC::STBX; ImmToIdxMap[PPC::PSTB8]   = PPC::STBX8;
  ImmToIdxMap[PPC::PSTH]   = PPC::STHX; ImmToIdxMap[PPC::PSTH8]   = PPC::STHX8;
  ImmToIdxMap[PPC::PSTW]   = PPC::STWX; ImmToIdxMap[PPC::PSTW8]   = PPC::STWX8;

  ImmToIdxMap[PPC::PLFS]   = PPC::LFSX; ImmToIdxMap[PPC::PSTFS]   = PPC::STFSX;
  ImmToIdxMap[PPC::PLFD]   = PPC::LFDX; ImmToIdxMap[PPC::PSTFD]   = PPC::STFDX;
  ImmToIdxMap[PPC::PLXSSP] = PPC::LXSSPX; ImmToIdxMap[PPC::PSTXSSP] = PPC::STXSSPX;
  ImmToIdxMap[PPC::PLXSD]  = PPC::LXSDX; ImmToIdxMap[PPC::PSTXSD]  = PPC::STXSDX;
  ImmToIdxMap[PPC::PLXV]   = PPC::LXVX; ImmToIdxMap[PPC::PSTXV]  = PPC::STXVX;

  ImmToIdxMap[PPC::LXVP]   = PPC::LXVPX;
  ImmToIdxMap[PPC::STXVP]  = PPC::STXVPX;
  ImmToIdxMap[PPC::PLXVP]  = PPC::LXVPX;
  ImmToIdxMap[PPC::PSTXVP] = PPC::STXVPX;
}

/// getPointerRegClass - Return the register class to use to hold pointers.
/// This is used for addressing modes.
const TargetRegisterClass *
PPCRegisterInfo::getPointerRegClass(const MachineFunction &MF, unsigned Kind)
                                                                       const {
  // Note that PPCInstrInfo::foldImmediate also directly uses this Kind value
  // when it checks for ZERO folding.
  if (Kind == 1) {
    if (TM.isPPC64())
      return &PPC::G8RC_NOX0RegClass;
    return &PPC::GPRC_NOR0RegClass;
  }

  if (TM.isPPC64())
    return &PPC::G8RCRegClass;
  return &PPC::GPRCRegClass;
}

const MCPhysReg*
PPCRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  const PPCSubtarget &Subtarget = MF->getSubtarget<PPCSubtarget>();
  if (MF->getFunction().getCallingConv() == CallingConv::AnyReg) {
    if (!TM.isPPC64() && Subtarget.isAIXABI())
      report_fatal_error("AnyReg unimplemented on 32-bit AIX.");
    if (Subtarget.hasVSX()) {
      if (Subtarget.pairedVectorMemops())
        return CSR_64_AllRegs_VSRP_SaveList;
      if (Subtarget.isAIXABI() && !TM.getAIXExtendedAltivecABI())
        return CSR_64_AllRegs_AIX_Dflt_VSX_SaveList;
      return CSR_64_AllRegs_VSX_SaveList;
    }
    if (Subtarget.hasAltivec()) {
      if (Subtarget.isAIXABI() && !TM.getAIXExtendedAltivecABI())
        return CSR_64_AllRegs_AIX_Dflt_Altivec_SaveList;
      return CSR_64_AllRegs_Altivec_SaveList;
    }
    return CSR_64_AllRegs_SaveList;
  }

  // On PPC64, we might need to save r2 (but only if it is not reserved).
  // We do not need to treat R2 as callee-saved when using PC-Relative calls
  // because any direct uses of R2 will cause it to be reserved. If the function
  // is a leaf or the only uses of R2 are implicit uses for calls, the calls
  // will use the @notoc relocation which will cause this function to set the
  // st_other bit to 1, thereby communicating to its caller that it arbitrarily
  // clobbers the TOC.
  bool SaveR2 = MF->getRegInfo().isAllocatable(PPC::X2) &&
                !Subtarget.isUsingPCRelativeCalls();

  // Cold calling convention CSRs.
  if (MF->getFunction().getCallingConv() == CallingConv::Cold) {
    if (Subtarget.isAIXABI())
      report_fatal_error("Cold calling unimplemented on AIX.");
    if (TM.isPPC64()) {
      if (Subtarget.pairedVectorMemops())
        return SaveR2 ? CSR_SVR64_ColdCC_R2_VSRP_SaveList
                      : CSR_SVR64_ColdCC_VSRP_SaveList;
      if (Subtarget.hasAltivec())
        return SaveR2 ? CSR_SVR64_ColdCC_R2_Altivec_SaveList
                      : CSR_SVR64_ColdCC_Altivec_SaveList;
      return SaveR2 ? CSR_SVR64_ColdCC_R2_SaveList
                    : CSR_SVR64_ColdCC_SaveList;
    }
    // 32-bit targets.
    if (Subtarget.pairedVectorMemops())
      return CSR_SVR32_ColdCC_VSRP_SaveList;
    else if (Subtarget.hasAltivec())
      return CSR_SVR32_ColdCC_Altivec_SaveList;
    else if (Subtarget.hasSPE())
      return CSR_SVR32_ColdCC_SPE_SaveList;
    return CSR_SVR32_ColdCC_SaveList;
  }
  // Standard calling convention CSRs.
  if (TM.isPPC64()) {
    if (Subtarget.pairedVectorMemops()) {
      if (Subtarget.isAIXABI()) {
        if (!TM.getAIXExtendedAltivecABI())
          return SaveR2 ? CSR_PPC64_R2_SaveList : CSR_PPC64_SaveList;
        return SaveR2 ? CSR_AIX64_R2_VSRP_SaveList : CSR_AIX64_VSRP_SaveList;
      }
      return SaveR2 ? CSR_SVR464_R2_VSRP_SaveList : CSR_SVR464_VSRP_SaveList;
    }
    if (Subtarget.hasAltivec() &&
        (!Subtarget.isAIXABI() || TM.getAIXExtendedAltivecABI())) {
      return SaveR2 ? CSR_PPC64_R2_Altivec_SaveList
                    : CSR_PPC64_Altivec_SaveList;
    }
    return SaveR2 ? CSR_PPC64_R2_SaveList : CSR_PPC64_SaveList;
  }
  // 32-bit targets.
  if (Subtarget.isAIXABI()) {
    if (Subtarget.pairedVectorMemops())
      return TM.getAIXExtendedAltivecABI() ? CSR_AIX32_VSRP_SaveList
                                           : CSR_AIX32_SaveList;
    if (Subtarget.hasAltivec())
      return TM.getAIXExtendedAltivecABI() ? CSR_AIX32_Altivec_SaveList
                                           : CSR_AIX32_SaveList;
    return CSR_AIX32_SaveList;
  }
  if (Subtarget.pairedVectorMemops())
    return CSR_SVR432_VSRP_SaveList;
  if (Subtarget.hasAltivec())
    return CSR_SVR432_Altivec_SaveList;
  else if (Subtarget.hasSPE()) {
    if (TM.isPositionIndependent() && !TM.isPPC64())
      return CSR_SVR432_SPE_NO_S30_31_SaveList;
    return CSR_SVR432_SPE_SaveList;
   }
  return CSR_SVR432_SaveList;
}

const uint32_t *
PPCRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                      CallingConv::ID CC) const {
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  if (CC == CallingConv::AnyReg) {
    if (Subtarget.hasVSX()) {
      if (Subtarget.pairedVectorMemops())
        return CSR_64_AllRegs_VSRP_RegMask;
      if (Subtarget.isAIXABI() && !TM.getAIXExtendedAltivecABI())
        return CSR_64_AllRegs_AIX_Dflt_VSX_RegMask;
      return CSR_64_AllRegs_VSX_RegMask;
    }
    if (Subtarget.hasAltivec()) {
      if (Subtarget.isAIXABI() && !TM.getAIXExtendedAltivecABI())
        return CSR_64_AllRegs_AIX_Dflt_Altivec_RegMask;
      return CSR_64_AllRegs_Altivec_RegMask;
    }
    return CSR_64_AllRegs_RegMask;
  }

  if (Subtarget.isAIXABI()) {
    if (Subtarget.pairedVectorMemops()) {
      if (!TM.getAIXExtendedAltivecABI())
        return TM.isPPC64() ? CSR_PPC64_RegMask : CSR_AIX32_RegMask;
      return TM.isPPC64() ? CSR_AIX64_VSRP_RegMask : CSR_AIX32_VSRP_RegMask;
    }
    return TM.isPPC64()
               ? ((Subtarget.hasAltivec() && TM.getAIXExtendedAltivecABI())
                      ? CSR_PPC64_Altivec_RegMask
                      : CSR_PPC64_RegMask)
               : ((Subtarget.hasAltivec() && TM.getAIXExtendedAltivecABI())
                      ? CSR_AIX32_Altivec_RegMask
                      : CSR_AIX32_RegMask);
  }

  if (CC == CallingConv::Cold) {
    if (TM.isPPC64())
      return Subtarget.pairedVectorMemops()
                 ? CSR_SVR64_ColdCC_VSRP_RegMask
                 : (Subtarget.hasAltivec() ? CSR_SVR64_ColdCC_Altivec_RegMask
                                           : CSR_SVR64_ColdCC_RegMask);
    else
      return Subtarget.pairedVectorMemops()
                 ? CSR_SVR32_ColdCC_VSRP_RegMask
                 : (Subtarget.hasAltivec()
                        ? CSR_SVR32_ColdCC_Altivec_RegMask
                        : (Subtarget.hasSPE() ? CSR_SVR32_ColdCC_SPE_RegMask
                                              : CSR_SVR32_ColdCC_RegMask));
  }

  if (TM.isPPC64())
    return Subtarget.pairedVectorMemops()
               ? CSR_SVR464_VSRP_RegMask
               : (Subtarget.hasAltivec() ? CSR_PPC64_Altivec_RegMask
                                         : CSR_PPC64_RegMask);
  else
    return Subtarget.pairedVectorMemops()
               ? CSR_SVR432_VSRP_RegMask
               : (Subtarget.hasAltivec()
                      ? CSR_SVR432_Altivec_RegMask
                      : (Subtarget.hasSPE()
                             ? (TM.isPositionIndependent()
                                     ? CSR_SVR432_SPE_NO_S30_31_RegMask
                                     : CSR_SVR432_SPE_RegMask)
                             : CSR_SVR432_RegMask));
}

const uint32_t*
PPCRegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

void PPCRegisterInfo::adjustStackMapLiveOutMask(uint32_t *Mask) const {
  for (unsigned PseudoReg : {PPC::ZERO, PPC::ZERO8, PPC::RM})
    Mask[PseudoReg / 32] &= ~(1u << (PseudoReg % 32));
}

BitVector PPCRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const PPCFrameLowering *TFI = getFrameLowering(MF);

  // The ZERO register is not really a register, but the representation of r0
  // when used in instructions that treat r0 as the constant 0.
  markSuperRegs(Reserved, PPC::ZERO);

  // The FP register is also not really a register, but is the representation
  // of the frame pointer register used by ISD::FRAMEADDR.
  markSuperRegs(Reserved, PPC::FP);

  // The BP register is also not really a register, but is the representation
  // of the base pointer register used by setjmp.
  markSuperRegs(Reserved, PPC::BP);

  // The counter registers must be reserved so that counter-based loops can
  // be correctly formed (and the mtctr instructions are not DCE'd).
  markSuperRegs(Reserved, PPC::CTR);
  markSuperRegs(Reserved, PPC::CTR8);

  markSuperRegs(Reserved, PPC::R1);
  markSuperRegs(Reserved, PPC::LR);
  markSuperRegs(Reserved, PPC::LR8);
  markSuperRegs(Reserved, PPC::RM);

  markSuperRegs(Reserved, PPC::VRSAVE);

  // The SVR4 ABI reserves r2 and r13
  if (Subtarget.isSVR4ABI()) {
    // We only reserve r2 if we need to use the TOC pointer. If we have no
    // explicit uses of the TOC pointer (meaning we're a leaf function with
    // no constant-pool loads, etc.) and we have no potential uses inside an
    // inline asm block, then we can treat r2 has an ordinary callee-saved
    // register.
    const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
    if (!TM.isPPC64() || FuncInfo->usesTOCBasePtr() || MF.hasInlineAsm())
      markSuperRegs(Reserved, PPC::R2);  // System-reserved register
    markSuperRegs(Reserved, PPC::R13); // Small Data Area pointer register
  }

  // Always reserve r2 on AIX for now.
  // TODO: Make r2 allocatable on AIX/XCOFF for some leaf functions.
  if (Subtarget.isAIXABI())
    markSuperRegs(Reserved, PPC::R2);  // System-reserved register

  // On PPC64, r13 is the thread pointer. Never allocate this register.
  if (TM.isPPC64())
    markSuperRegs(Reserved, PPC::R13);

  if (TFI->needsFP(MF))
    markSuperRegs(Reserved, PPC::R31);

  bool IsPositionIndependent = TM.isPositionIndependent();
  if (hasBasePointer(MF)) {
    if (Subtarget.is32BitELFABI() && IsPositionIndependent)
      markSuperRegs(Reserved, PPC::R29);
    else
      markSuperRegs(Reserved, PPC::R30);
  }

  if (Subtarget.is32BitELFABI() && IsPositionIndependent)
    markSuperRegs(Reserved, PPC::R30);

  // Reserve Altivec registers when Altivec is unavailable.
  if (!Subtarget.hasAltivec())
    for (MCRegister Reg : PPC::VRRCRegClass)
      markSuperRegs(Reserved, Reg);

  if (Subtarget.isAIXABI() && Subtarget.hasAltivec() &&
      !TM.getAIXExtendedAltivecABI()) {
    //  In the AIX default Altivec ABI, vector registers VR20-VR31 are reserved
    //  and cannot be used.
    for (auto Reg : CSR_Altivec_SaveList) {
      if (Reg == 0)
        break;
      markSuperRegs(Reserved, Reg);
      for (MCRegAliasIterator AS(Reg, this, true); AS.isValid(); ++AS) {
        Reserved.set(*AS);
      }
    }
  }

  assert(checkAllSuperRegsMarked(Reserved));
  return Reserved;
}

bool PPCRegisterInfo::isAsmClobberable(const MachineFunction &MF,
                                       MCRegister PhysReg) const {
  // We cannot use getReservedRegs() to find the registers that are not asm
  // clobberable because there are some reserved registers which can be
  // clobbered by inline asm. For example, when LR is clobbered, the register is
  // saved and restored. We will hardcode the registers that are not asm
  // cloberable in this function.

  // The stack pointer (R1/X1) is not clobberable by inline asm
  return PhysReg != PPC::R1 && PhysReg != PPC::X1;
}

bool PPCRegisterInfo::requiresFrameIndexScavenging(const MachineFunction &MF) const {
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const PPCInstrInfo *InstrInfo =  Subtarget.getInstrInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const std::vector<CalleeSavedInfo> &Info = MFI.getCalleeSavedInfo();

  LLVM_DEBUG(dbgs() << "requiresFrameIndexScavenging for " << MF.getName()
                    << ".\n");
  // If the callee saved info is invalid we have to default to true for safety.
  if (!MFI.isCalleeSavedInfoValid()) {
    LLVM_DEBUG(dbgs() << "TRUE - Invalid callee saved info.\n");
    return true;
  }

  // We will require the use of X-Forms because the frame is larger than what
  // can be represented in signed 16 bits that fit in the immediate of a D-Form.
  // If we need an X-Form then we need a register to store the address offset.
  unsigned FrameSize = MFI.getStackSize();
  // Signed 16 bits means that the FrameSize cannot be more than 15 bits.
  if (FrameSize & ~0x7FFF) {
    LLVM_DEBUG(dbgs() << "TRUE - Frame size is too large for D-Form.\n");
    return true;
  }

  // The callee saved info is valid so it can be traversed.
  // Checking for registers that need saving that do not have load or store
  // forms where the address offset is an immediate.
  for (const CalleeSavedInfo &CSI : Info) {
    // If the spill is to a register no scavenging is required.
    if (CSI.isSpilledToReg())
      continue;

    int FrIdx = CSI.getFrameIdx();
    Register Reg = CSI.getReg();

    const TargetRegisterClass *RC = getMinimalPhysRegClass(Reg);
    unsigned Opcode = InstrInfo->getStoreOpcodeForSpill(RC);
    if (!MFI.isFixedObjectIndex(FrIdx)) {
      // This is not a fixed object. If it requires alignment then we may still
      // need to use the XForm.
      if (offsetMinAlignForOpcode(Opcode) > 1) {
        LLVM_DEBUG(dbgs() << "Memory Operand: " << InstrInfo->getName(Opcode)
                          << " for register " << printReg(Reg, this) << ".\n");
        LLVM_DEBUG(dbgs() << "TRUE - Not fixed frame object that requires "
                          << "alignment.\n");
        return true;
      }
    }

    // This is eiher:
    // 1) A fixed frame index object which we know are aligned so
    // as long as we have a valid DForm/DSForm/DQForm (non XForm) we don't
    // need to consider the alignment here.
    // 2) A not fixed object but in that case we now know that the min required
    // alignment is no more than 1 based on the previous check.
    if (InstrInfo->isXFormMemOp(Opcode)) {
      LLVM_DEBUG(dbgs() << "Memory Operand: " << InstrInfo->getName(Opcode)
                        << " for register " << printReg(Reg, this) << ".\n");
      LLVM_DEBUG(dbgs() << "TRUE - Memory operand is X-Form.\n");
      return true;
    }

    // This is a spill/restore of a quadword.
    if ((Opcode == PPC::RESTORE_QUADWORD) || (Opcode == PPC::SPILL_QUADWORD)) {
      LLVM_DEBUG(dbgs() << "Memory Operand: " << InstrInfo->getName(Opcode)
                        << " for register " << printReg(Reg, this) << ".\n");
      LLVM_DEBUG(dbgs() << "TRUE - Memory operand is a quadword.\n");
      return true;
    }
  }
  LLVM_DEBUG(dbgs() << "FALSE - Scavenging is not required.\n");
  return false;
}

bool PPCRegisterInfo::requiresVirtualBaseRegisters(
    const MachineFunction &MF) const {
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  // Do not use virtual base registers when ROP protection is turned on.
  // Virtual base registers break the layout of the local variable space and may
  // push the ROP Hash location past the 512 byte range of the ROP store
  // instruction.
  return !Subtarget.hasROPProtect();
}

bool PPCRegisterInfo::isCallerPreservedPhysReg(MCRegister PhysReg,
                                               const MachineFunction &MF) const {
  assert(Register::isPhysicalRegister(PhysReg));
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  if (!Subtarget.is64BitELFABI() && !Subtarget.isAIXABI())
    return false;
  if (PhysReg == Subtarget.getTOCPointerRegister())
    // X2/R2 is guaranteed to be preserved within a function if it is reserved.
    // The reason it's reserved is that it's the TOC pointer (and the function
    // uses the TOC). In functions where it isn't reserved (i.e. leaf functions
    // with no TOC access), we can't claim that it is preserved.
    return (getReservedRegs(MF).test(PhysReg));
  if (StackPtrConst && PhysReg == Subtarget.getStackPointerRegister() &&
      !MFI.hasVarSizedObjects() && !MFI.hasOpaqueSPAdjustment())
    // The value of the stack pointer does not change within a function after
    // the prologue and before the epilogue if there are no dynamic allocations
    // and no inline asm which clobbers X1/R1.
    return true;
  return false;
}

bool PPCRegisterInfo::getRegAllocationHints(Register VirtReg,
                                            ArrayRef<MCPhysReg> Order,
                                            SmallVectorImpl<MCPhysReg> &Hints,
                                            const MachineFunction &MF,
                                            const VirtRegMap *VRM,
                                            const LiveRegMatrix *Matrix) const {
  const MachineRegisterInfo *MRI = &MF.getRegInfo();

  // Call the base implementation first to set any hints based on the usual
  // heuristics and decide what the return value should be. We want to return
  // the same value returned by the base implementation. If the base
  // implementation decides to return true and force the allocation then we
  // will leave it as such. On the other hand if the base implementation
  // decides to return false the following code will not force the allocation
  // as we are just looking to provide a hint.
  bool BaseImplRetVal = TargetRegisterInfo::getRegAllocationHints(
      VirtReg, Order, Hints, MF, VRM, Matrix);

  // Don't use the allocation hints for ISAFuture.
  // The WACC registers used in ISAFuture are unlike the ACC registers on
  // Power 10 and so this logic to register allocation hints does not apply.
  if (MF.getSubtarget<PPCSubtarget>().isISAFuture())
    return BaseImplRetVal;

  // We are interested in instructions that copy values to ACC/UACC.
  // The copy into UACC will be simply a COPY to a subreg so we
  // want to allocate the corresponding physical subreg for the source.
  // The copy into ACC will be a BUILD_UACC so we want to allocate
  // the same number UACC for the source.
  const TargetRegisterClass *RegClass = MRI->getRegClass(VirtReg);
  for (MachineInstr &Use : MRI->reg_nodbg_instructions(VirtReg)) {
    const MachineOperand *ResultOp = nullptr;
    Register ResultReg;
    switch (Use.getOpcode()) {
    case TargetOpcode::COPY: {
      ResultOp = &Use.getOperand(0);
      ResultReg = ResultOp->getReg();
      if (ResultReg.isVirtual() &&
          MRI->getRegClass(ResultReg)->contains(PPC::UACC0) &&
          VRM->hasPhys(ResultReg)) {
        Register UACCPhys = VRM->getPhys(ResultReg);
        Register HintReg;
        if (RegClass->contains(PPC::VSRp0)) {
          HintReg = getSubReg(UACCPhys, ResultOp->getSubReg());
          // Ensure that the hint is a VSRp register.
          if (HintReg >= PPC::VSRp0 && HintReg <= PPC::VSRp31)
            Hints.push_back(HintReg);
        } else if (RegClass->contains(PPC::ACC0)) {
          HintReg = PPC::ACC0 + (UACCPhys - PPC::UACC0);
          if (HintReg >= PPC::ACC0 && HintReg <= PPC::ACC7)
            Hints.push_back(HintReg);
        }
      }
      break;
    }
    case PPC::BUILD_UACC: {
      ResultOp = &Use.getOperand(0);
      ResultReg = ResultOp->getReg();
      if (MRI->getRegClass(ResultReg)->contains(PPC::ACC0) &&
          VRM->hasPhys(ResultReg)) {
        Register ACCPhys = VRM->getPhys(ResultReg);
        assert((ACCPhys >= PPC::ACC0 && ACCPhys <= PPC::ACC7) &&
               "Expecting an ACC register for BUILD_UACC.");
        Register HintReg = PPC::UACC0 + (ACCPhys - PPC::ACC0);
        Hints.push_back(HintReg);
      }
      break;
    }
    }
  }
  return BaseImplRetVal;
}

unsigned PPCRegisterInfo::getRegPressureLimit(const TargetRegisterClass *RC,
                                              MachineFunction &MF) const {
  const PPCFrameLowering *TFI = getFrameLowering(MF);
  const unsigned DefaultSafety = 1;

  switch (RC->getID()) {
  default:
    return 0;
  case PPC::G8RC_NOX0RegClassID:
  case PPC::GPRC_NOR0RegClassID:
  case PPC::SPERCRegClassID:
  case PPC::G8RCRegClassID:
  case PPC::GPRCRegClassID: {
    unsigned FP = TFI->hasFP(MF) ? 1 : 0;
    return 32 - FP - DefaultSafety;
  }
  case PPC::F4RCRegClassID:
  case PPC::F8RCRegClassID:
  case PPC::VSLRCRegClassID:
    return 32 - DefaultSafety;
  case PPC::VFRCRegClassID:
  case PPC::VRRCRegClassID: {
    const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
    // Vector registers VR20-VR31 are reserved and cannot be used in the default
    // Altivec ABI on AIX.
    if (!TM.getAIXExtendedAltivecABI() && Subtarget.isAIXABI())
      return 20 - DefaultSafety;
  }
    return 32 - DefaultSafety;
  case PPC::VSFRCRegClassID:
  case PPC::VSSRCRegClassID:
  case PPC::VSRCRegClassID: {
    const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
    if (!TM.getAIXExtendedAltivecABI() && Subtarget.isAIXABI())
      // Vector registers VR20-VR31 are reserved and cannot be used in the
      // default Altivec ABI on AIX.
      return 52 - DefaultSafety;
  }
    return 64 - DefaultSafety;
  case PPC::CRRCRegClassID:
    return 8 - DefaultSafety;
  }
}

const TargetRegisterClass *
PPCRegisterInfo::getLargestLegalSuperClass(const TargetRegisterClass *RC,
                                           const MachineFunction &MF) const {
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const auto *DefaultSuperclass =
      TargetRegisterInfo::getLargestLegalSuperClass(RC, MF);
  if (Subtarget.hasVSX()) {
    // With VSX, we can inflate various sub-register classes to the full VSX
    // register set.

    // For Power9 we allow the user to enable GPR to vector spills.
    // FIXME: Currently limited to spilling GP8RC. A follow on patch will add
    // support to spill GPRC.
    if (TM.isELFv2ABI() || Subtarget.isAIXABI()) {
      if (Subtarget.hasP9Vector() && EnableGPRToVecSpills &&
          RC == &PPC::G8RCRegClass) {
        InflateGP8RC++;
        return &PPC::SPILLTOVSRRCRegClass;
      }
      if (RC == &PPC::GPRCRegClass && EnableGPRToVecSpills)
        InflateGPRC++;
    }

    for (const auto *I = RC->getSuperClasses(); *I; ++I) {
      if (getRegSizeInBits(**I) != getRegSizeInBits(*RC))
        continue;

      switch ((*I)->getID()) {
      case PPC::VSSRCRegClassID:
        return Subtarget.hasP8Vector() ? *I : DefaultSuperclass;
      case PPC::VSFRCRegClassID:
      case PPC::VSRCRegClassID:
        return *I;
      case PPC::VSRpRCRegClassID:
        return Subtarget.pairedVectorMemops() ? *I : DefaultSuperclass;
      case PPC::ACCRCRegClassID:
      case PPC::UACCRCRegClassID:
        return Subtarget.hasMMA() ? *I : DefaultSuperclass;
      }
    }
  }

  return DefaultSuperclass;
}

//===----------------------------------------------------------------------===//
// Stack Frame Processing methods
//===----------------------------------------------------------------------===//

/// lowerDynamicAlloc - Generate the code for allocating an object in the
/// current frame.  The sequence of code will be in the general form
///
///   addi   R0, SP, \#frameSize ; get the address of the previous frame
///   stwxu  R0, SP, Rnegsize   ; add and update the SP with the negated size
///   addi   Rnew, SP, \#maxCalFrameSize ; get the top of the allocation
///
void PPCRegisterInfo::lowerDynamicAlloc(MachineBasicBlock::iterator II) const {
  // Get the instruction.
  MachineInstr &MI = *II;
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  // Get the basic block's function.
  MachineFunction &MF = *MBB.getParent();
  // Get the frame info.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  // Get the instruction info.
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  // Determine whether 64-bit pointers are used.
  bool LP64 = TM.isPPC64();
  DebugLoc dl = MI.getDebugLoc();

  // Get the maximum call stack size.
  unsigned maxCallFrameSize = MFI.getMaxCallFrameSize();
  Align MaxAlign = MFI.getMaxAlign();
  assert(isAligned(MaxAlign, maxCallFrameSize) &&
         "Maximum call-frame size not sufficiently aligned");
  (void)MaxAlign;

  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;
  Register Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  bool KillNegSizeReg = MI.getOperand(1).isKill();
  Register NegSizeReg = MI.getOperand(1).getReg();

  prepareDynamicAlloca(II, NegSizeReg, KillNegSizeReg, Reg);
  // Grow the stack and update the stack pointer link, then determine the
  // address of new allocated space.
  if (LP64) {
    BuildMI(MBB, II, dl, TII.get(PPC::STDUX), PPC::X1)
        .addReg(Reg, RegState::Kill)
        .addReg(PPC::X1)
        .addReg(NegSizeReg, getKillRegState(KillNegSizeReg));
    BuildMI(MBB, II, dl, TII.get(PPC::ADDI8), MI.getOperand(0).getReg())
        .addReg(PPC::X1)
        .addImm(maxCallFrameSize);
  } else {
    BuildMI(MBB, II, dl, TII.get(PPC::STWUX), PPC::R1)
        .addReg(Reg, RegState::Kill)
        .addReg(PPC::R1)
        .addReg(NegSizeReg, getKillRegState(KillNegSizeReg));
    BuildMI(MBB, II, dl, TII.get(PPC::ADDI), MI.getOperand(0).getReg())
        .addReg(PPC::R1)
        .addImm(maxCallFrameSize);
  }

  // Discard the DYNALLOC instruction.
  MBB.erase(II);
}

/// To accomplish dynamic stack allocation, we have to calculate exact size
/// subtracted from the stack pointer according alignment information and get
/// previous frame pointer.
void PPCRegisterInfo::prepareDynamicAlloca(MachineBasicBlock::iterator II,
                                           Register &NegSizeReg,
                                           bool &KillNegSizeReg,
                                           Register &FramePointer) const {
  // Get the instruction.
  MachineInstr &MI = *II;
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  // Get the basic block's function.
  MachineFunction &MF = *MBB.getParent();
  // Get the frame info.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  // Get the instruction info.
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  // Determine whether 64-bit pointers are used.
  bool LP64 = TM.isPPC64();
  DebugLoc dl = MI.getDebugLoc();
  // Get the total frame size.
  unsigned FrameSize = MFI.getStackSize();

  // Get stack alignments.
  const PPCFrameLowering *TFI = getFrameLowering(MF);
  Align TargetAlign = TFI->getStackAlign();
  Align MaxAlign = MFI.getMaxAlign();

  // Determine the previous frame's address.  If FrameSize can't be
  // represented as 16 bits or we need special alignment, then we load the
  // previous frame's address from 0(SP).  Why not do an addis of the hi?
  // Because R0 is our only safe tmp register and addi/addis treat R0 as zero.
  // Constructing the constant and adding would take 3 instructions.
  // Fortunately, a frame greater than 32K is rare.
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  if (MaxAlign < TargetAlign && isInt<16>(FrameSize)) {
    if (LP64)
      BuildMI(MBB, II, dl, TII.get(PPC::ADDI8), FramePointer)
          .addReg(PPC::X31)
          .addImm(FrameSize);
    else
      BuildMI(MBB, II, dl, TII.get(PPC::ADDI), FramePointer)
          .addReg(PPC::R31)
          .addImm(FrameSize);
  } else if (LP64) {
    BuildMI(MBB, II, dl, TII.get(PPC::LD), FramePointer)
        .addImm(0)
        .addReg(PPC::X1);
  } else {
    BuildMI(MBB, II, dl, TII.get(PPC::LWZ), FramePointer)
        .addImm(0)
        .addReg(PPC::R1);
  }
  // Determine the actual NegSizeReg according to alignment info.
  if (LP64) {
    if (MaxAlign > TargetAlign) {
      unsigned UnalNegSizeReg = NegSizeReg;
      NegSizeReg = MF.getRegInfo().createVirtualRegister(G8RC);

      // Unfortunately, there is no andi, only andi., and we can't insert that
      // here because we might clobber cr0 while it is live.
      BuildMI(MBB, II, dl, TII.get(PPC::LI8), NegSizeReg)
          .addImm(~(MaxAlign.value() - 1));

      unsigned NegSizeReg1 = NegSizeReg;
      NegSizeReg = MF.getRegInfo().createVirtualRegister(G8RC);
      BuildMI(MBB, II, dl, TII.get(PPC::AND8), NegSizeReg)
          .addReg(UnalNegSizeReg, getKillRegState(KillNegSizeReg))
          .addReg(NegSizeReg1, RegState::Kill);
      KillNegSizeReg = true;
    }
  } else {
    if (MaxAlign > TargetAlign) {
      unsigned UnalNegSizeReg = NegSizeReg;
      NegSizeReg = MF.getRegInfo().createVirtualRegister(GPRC);

      // Unfortunately, there is no andi, only andi., and we can't insert that
      // here because we might clobber cr0 while it is live.
      BuildMI(MBB, II, dl, TII.get(PPC::LI), NegSizeReg)
          .addImm(~(MaxAlign.value() - 1));

      unsigned NegSizeReg1 = NegSizeReg;
      NegSizeReg = MF.getRegInfo().createVirtualRegister(GPRC);
      BuildMI(MBB, II, dl, TII.get(PPC::AND), NegSizeReg)
          .addReg(UnalNegSizeReg, getKillRegState(KillNegSizeReg))
          .addReg(NegSizeReg1, RegState::Kill);
      KillNegSizeReg = true;
    }
  }
}

void PPCRegisterInfo::lowerPrepareProbedAlloca(
    MachineBasicBlock::iterator II) const {
  MachineInstr &MI = *II;
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  // Get the basic block's function.
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  // Get the instruction info.
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  // Determine whether 64-bit pointers are used.
  bool LP64 = TM.isPPC64();
  DebugLoc dl = MI.getDebugLoc();
  Register FramePointer = MI.getOperand(0).getReg();
  const Register ActualNegSizeReg = MI.getOperand(1).getReg();
  bool KillNegSizeReg = MI.getOperand(2).isKill();
  Register NegSizeReg = MI.getOperand(2).getReg();
  const MCInstrDesc &CopyInst = TII.get(LP64 ? PPC::OR8 : PPC::OR);
  // RegAllocator might allocate FramePointer and NegSizeReg in the same phyreg.
  if (FramePointer == NegSizeReg) {
    assert(KillNegSizeReg && "FramePointer is a def and NegSizeReg is an use, "
                             "NegSizeReg should be killed");
    // FramePointer is clobbered earlier than the use of NegSizeReg in
    // prepareDynamicAlloca, save NegSizeReg in ActualNegSizeReg to avoid
    // misuse.
    BuildMI(MBB, II, dl, CopyInst, ActualNegSizeReg)
        .addReg(NegSizeReg)
        .addReg(NegSizeReg);
    NegSizeReg = ActualNegSizeReg;
    KillNegSizeReg = false;
  }
  prepareDynamicAlloca(II, NegSizeReg, KillNegSizeReg, FramePointer);
  // NegSizeReg might be updated in prepareDynamicAlloca if MaxAlign >
  // TargetAlign.
  if (NegSizeReg != ActualNegSizeReg)
    BuildMI(MBB, II, dl, CopyInst, ActualNegSizeReg)
        .addReg(NegSizeReg)
        .addReg(NegSizeReg);
  MBB.erase(II);
}

void PPCRegisterInfo::lowerDynamicAreaOffset(
    MachineBasicBlock::iterator II) const {
  // Get the instruction.
  MachineInstr &MI = *II;
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  // Get the basic block's function.
  MachineFunction &MF = *MBB.getParent();
  // Get the frame info.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  // Get the instruction info.
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();

  unsigned maxCallFrameSize = MFI.getMaxCallFrameSize();
  bool is64Bit = TM.isPPC64();
  DebugLoc dl = MI.getDebugLoc();
  BuildMI(MBB, II, dl, TII.get(is64Bit ? PPC::LI8 : PPC::LI),
          MI.getOperand(0).getReg())
      .addImm(maxCallFrameSize);
  MBB.erase(II);
}

/// lowerCRSpilling - Generate the code for spilling a CR register. Instead of
/// reserving a whole register (R0), we scrounge for one here. This generates
/// code like this:
///
///   mfcr rA                  ; Move the conditional register into GPR rA.
///   rlwinm rA, rA, SB, 0, 31 ; Shift the bits left so they are in CR0's slot.
///   stw rA, FI               ; Store rA to the frame.
///
void PPCRegisterInfo::lowerCRSpilling(MachineBasicBlock::iterator II,
                                      unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; SPILL_CR <SrcReg>, <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  bool LP64 = TM.isPPC64();
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  Register Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  Register SrcReg = MI.getOperand(0).getReg();

  // We need to store the CR in the low 4-bits of the saved value. First, issue
  // an MFOCRF to save all of the CRBits and, if needed, kill the SrcReg.
  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::MFOCRF8 : PPC::MFOCRF), Reg)
      .addReg(SrcReg, getKillRegState(MI.getOperand(0).isKill()));

  // If the saved register wasn't CR0, shift the bits left so that they are in
  // CR0's slot.
  if (SrcReg != PPC::CR0) {
    Register Reg1 = Reg;
    Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);

    // rlwinm rA, rA, ShiftBits, 0, 31.
    BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::RLWINM8 : PPC::RLWINM), Reg)
      .addReg(Reg1, RegState::Kill)
      .addImm(getEncodingValue(SrcReg) * 4)
      .addImm(0)
      .addImm(31);
  }

  addFrameReference(BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::STW8 : PPC::STW))
                    .addReg(Reg, RegState::Kill),
                    FrameIndex);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

void PPCRegisterInfo::lowerCRRestore(MachineBasicBlock::iterator II,
                                      unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; <DestReg> = RESTORE_CR <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  bool LP64 = TM.isPPC64();
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  Register Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  Register DestReg = MI.getOperand(0).getReg();
  assert(MI.definesRegister(DestReg, /*TRI=*/nullptr) &&
         "RESTORE_CR does not define its destination");

  addFrameReference(BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::LWZ8 : PPC::LWZ),
                              Reg), FrameIndex);

  // If the reloaded register isn't CR0, shift the bits right so that they are
  // in the right CR's slot.
  if (DestReg != PPC::CR0) {
    Register Reg1 = Reg;
    Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);

    unsigned ShiftBits = getEncodingValue(DestReg)*4;
    // rlwinm r11, r11, 32-ShiftBits, 0, 31.
    BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::RLWINM8 : PPC::RLWINM), Reg)
             .addReg(Reg1, RegState::Kill).addImm(32-ShiftBits).addImm(0)
             .addImm(31);
  }

  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::MTOCRF8 : PPC::MTOCRF), DestReg)
             .addReg(Reg, RegState::Kill);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

void PPCRegisterInfo::lowerCRBitSpilling(MachineBasicBlock::iterator II,
                                         unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; SPILL_CRBIT <SrcReg>, <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  const TargetRegisterInfo* TRI = Subtarget.getRegisterInfo();
  DebugLoc dl = MI.getDebugLoc();

  bool LP64 = TM.isPPC64();
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  Register Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  Register SrcReg = MI.getOperand(0).getReg();

  // Search up the BB to find the definition of the CR bit.
  MachineBasicBlock::reverse_iterator Ins = MI;
  MachineBasicBlock::reverse_iterator Rend = MBB.rend();
  ++Ins;
  unsigned CRBitSpillDistance = 0;
  bool SeenUse = false;
  for (; Ins != Rend; ++Ins) {
    // Definition found.
    if (Ins->modifiesRegister(SrcReg, TRI))
      break;
    // Use found.
    if (Ins->readsRegister(SrcReg, TRI))
      SeenUse = true;
    // Unable to find CR bit definition within maximum search distance.
    if (CRBitSpillDistance == MaxCRBitSpillDist) {
      Ins = MI;
      break;
    }
    // Skip debug instructions when counting CR bit spill distance.
    if (!Ins->isDebugInstr())
      CRBitSpillDistance++;
  }

  // Unable to find the definition of the CR bit in the MBB.
  if (Ins == MBB.rend())
    Ins = MI;

  bool SpillsKnownBit = false;
  // There is no need to extract the CR bit if its value is already known.
  switch (Ins->getOpcode()) {
  case PPC::CRUNSET:
    BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::LI8 : PPC::LI), Reg)
      .addImm(0);
    SpillsKnownBit = true;
    break;
  case PPC::CRSET:
    BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::LIS8 : PPC::LIS), Reg)
      .addImm(-32768);
    SpillsKnownBit = true;
    break;
  default:
    // On Power10, we can use SETNBC to spill all CR bits. SETNBC will set all
    // bits (specifically, it produces a -1 if the CR bit is set). Ultimately,
    // the bit that is of importance to us is bit 32 (bit 0 of a 32-bit
    // register), and SETNBC will set this.
    if (Subtarget.isISA3_1()) {
      BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::SETNBC8 : PPC::SETNBC), Reg)
          .addReg(SrcReg, RegState::Undef);
      break;
    }

    // On Power9, we can use SETB to extract the LT bit. This only works for
    // the LT bit since SETB produces -1/1/0 for LT/GT/<neither>. So the value
    // of the bit we care about (32-bit sign bit) will be set to the value of
    // the LT bit (regardless of the other bits in the CR field).
    if (Subtarget.isISA3_0()) {
      if (SrcReg == PPC::CR0LT || SrcReg == PPC::CR1LT ||
          SrcReg == PPC::CR2LT || SrcReg == PPC::CR3LT ||
          SrcReg == PPC::CR4LT || SrcReg == PPC::CR5LT ||
          SrcReg == PPC::CR6LT || SrcReg == PPC::CR7LT) {
        BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::SETB8 : PPC::SETB), Reg)
          .addReg(getCRFromCRBit(SrcReg), RegState::Undef);
        break;
      }
    }

    // We need to move the CR field that contains the CR bit we are spilling.
    // The super register may not be explicitly defined (i.e. it can be defined
    // by a CR-logical that only defines the subreg) so we state that the CR
    // field is undef. Also, in order to preserve the kill flag on the CR bit,
    // we add it as an implicit use.
    BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::MFOCRF8 : PPC::MFOCRF), Reg)
      .addReg(getCRFromCRBit(SrcReg), RegState::Undef)
      .addReg(SrcReg,
              RegState::Implicit | getKillRegState(MI.getOperand(0).isKill()));

    // If the saved register wasn't CR0LT, shift the bits left so that the bit
    // to store is the first one. Mask all but that bit.
    Register Reg1 = Reg;
    Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);

    // rlwinm rA, rA, ShiftBits, 0, 0.
    BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::RLWINM8 : PPC::RLWINM), Reg)
      .addReg(Reg1, RegState::Kill)
      .addImm(getEncodingValue(SrcReg))
      .addImm(0).addImm(0);
  }
  addFrameReference(BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::STW8 : PPC::STW))
                    .addReg(Reg, RegState::Kill),
                    FrameIndex);

  bool KillsCRBit = MI.killsRegister(SrcReg, TRI);
  // Discard the pseudo instruction.
  MBB.erase(II);
  if (SpillsKnownBit && KillsCRBit && !SeenUse) {
    Ins->setDesc(TII.get(PPC::UNENCODED_NOP));
    Ins->removeOperand(0);
  }
}

void PPCRegisterInfo::lowerCRBitRestore(MachineBasicBlock::iterator II,
                                      unsigned FrameIndex) const {
  // Get the instruction.
  MachineInstr &MI = *II;       // ; <DestReg> = RESTORE_CRBIT <offset>
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();

  bool LP64 = TM.isPPC64();
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  Register Reg = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  Register DestReg = MI.getOperand(0).getReg();
  assert(MI.definesRegister(DestReg, /*TRI=*/nullptr) &&
         "RESTORE_CRBIT does not define its destination");

  addFrameReference(BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::LWZ8 : PPC::LWZ),
                              Reg), FrameIndex);

  BuildMI(MBB, II, dl, TII.get(TargetOpcode::IMPLICIT_DEF), DestReg);

  Register RegO = MF.getRegInfo().createVirtualRegister(LP64 ? G8RC : GPRC);
  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::MFOCRF8 : PPC::MFOCRF), RegO)
          .addReg(getCRFromCRBit(DestReg));

  unsigned ShiftBits = getEncodingValue(DestReg);
  // rlwimi r11, r10, 32-ShiftBits, ..., ...
  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::RLWIMI8 : PPC::RLWIMI), RegO)
      .addReg(RegO, RegState::Kill)
      .addReg(Reg, RegState::Kill)
      .addImm(ShiftBits ? 32 - ShiftBits : 0)
      .addImm(ShiftBits)
      .addImm(ShiftBits);

  BuildMI(MBB, II, dl, TII.get(LP64 ? PPC::MTOCRF8 : PPC::MTOCRF),
          getCRFromCRBit(DestReg))
      .addReg(RegO, RegState::Kill)
      // Make sure we have a use dependency all the way through this
      // sequence of instructions. We can't have the other bits in the CR
      // modified in between the mfocrf and the mtocrf.
      .addReg(getCRFromCRBit(DestReg), RegState::Implicit);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

void PPCRegisterInfo::emitAccCopyInfo(MachineBasicBlock &MBB,
                                      MCRegister DestReg, MCRegister SrcReg) {
#ifdef NDEBUG
  return;
#else
  if (ReportAccMoves) {
    std::string Dest = PPC::ACCRCRegClass.contains(DestReg) ? "acc" : "uacc";
    std::string Src = PPC::ACCRCRegClass.contains(SrcReg) ? "acc" : "uacc";
    dbgs() << "Emitting copy from " << Src << " to " << Dest << ":\n";
    MBB.dump();
  }
#endif
}

static void emitAccSpillRestoreInfo(MachineBasicBlock &MBB, bool IsPrimed,
                                    bool IsRestore) {
#ifdef NDEBUG
  return;
#else
  if (ReportAccMoves) {
    dbgs() << "Emitting " << (IsPrimed ? "acc" : "uacc") << " register "
           << (IsRestore ? "restore" : "spill") << ":\n";
    MBB.dump();
  }
#endif
}

static void spillRegPairs(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator II, DebugLoc DL,
                          const TargetInstrInfo &TII, Register SrcReg,
                          unsigned FrameIndex, bool IsLittleEndian,
                          bool IsKilled, bool TwoPairs) {
  unsigned Offset = 0;
  // The register arithmetic in this function does not support virtual
  // registers.
  assert(!SrcReg.isVirtual() &&
         "Spilling register pairs does not support virtual registers.");

  if (TwoPairs)
    Offset = IsLittleEndian ? 48 : 0;
  else
    Offset = IsLittleEndian ? 16 : 0;
  Register Reg = (SrcReg > PPC::VSRp15) ? PPC::V0 + (SrcReg - PPC::VSRp16) * 2
                                        : PPC::VSL0 + (SrcReg - PPC::VSRp0) * 2;
  addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::STXV))
                        .addReg(Reg, getKillRegState(IsKilled)),
                    FrameIndex, Offset);
  Offset += IsLittleEndian ? -16 : 16;
  addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::STXV))
                        .addReg(Reg + 1, getKillRegState(IsKilled)),
                    FrameIndex, Offset);
  if (TwoPairs) {
    Offset += IsLittleEndian ? -16 : 16;
    addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::STXV))
                          .addReg(Reg + 2, getKillRegState(IsKilled)),
                      FrameIndex, Offset);
    Offset += IsLittleEndian ? -16 : 16;
    addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::STXV))
                          .addReg(Reg + 3, getKillRegState(IsKilled)),
                      FrameIndex, Offset);
  }
}

/// Remove any STXVP[X] instructions and split them out into a pair of
/// STXV[X] instructions if --disable-auto-paired-vec-st is specified on
/// the command line.
void PPCRegisterInfo::lowerOctWordSpilling(MachineBasicBlock::iterator II,
                                           unsigned FrameIndex) const {
  assert(DisableAutoPairedVecSt &&
         "Expecting to do this only if paired vector stores are disabled.");
  MachineInstr &MI = *II; // STXVP <SrcReg>, <offset>
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  Register SrcReg = MI.getOperand(0).getReg();
  bool IsLittleEndian = Subtarget.isLittleEndian();
  bool IsKilled = MI.getOperand(0).isKill();
  spillRegPairs(MBB, II, DL, TII, SrcReg, FrameIndex, IsLittleEndian, IsKilled,
                /* TwoPairs */ false);
  // Discard the original instruction.
  MBB.erase(II);
}

static void emitWAccSpillRestoreInfo(MachineBasicBlock &MBB, bool IsRestore) {
#ifdef NDEBUG
  return;
#else
  if (ReportAccMoves) {
    dbgs() << "Emitting wacc register " << (IsRestore ? "restore" : "spill")
           << ":\n";
    MBB.dump();
  }
#endif
}

/// lowerACCSpilling - Generate the code for spilling the accumulator register.
/// Similarly to other spills/reloads that use pseudo-ops, we do not actually
/// eliminate the FrameIndex here nor compute the stack offset. We simply
/// create a real instruction with an FI and rely on eliminateFrameIndex to
/// handle the FI elimination.
void PPCRegisterInfo::lowerACCSpilling(MachineBasicBlock::iterator II,
                                       unsigned FrameIndex) const {
  MachineInstr &MI = *II; // SPILL_ACC <SrcReg>, <offset>
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  Register SrcReg = MI.getOperand(0).getReg();
  bool IsKilled = MI.getOperand(0).isKill();

  bool IsPrimed = PPC::ACCRCRegClass.contains(SrcReg);
  Register Reg =
      PPC::VSRp0 + (SrcReg - (IsPrimed ? PPC::ACC0 : PPC::UACC0)) * 2;
  bool IsLittleEndian = Subtarget.isLittleEndian();

  emitAccSpillRestoreInfo(MBB, IsPrimed, false);

  // De-prime the register being spilled, create two stores for the pair
  // subregisters accounting for endianness and then re-prime the register if
  // it isn't killed.  This uses the Offset parameter to addFrameReference() to
  // adjust the offset of the store that is within the 64-byte stack slot.
  if (IsPrimed)
    BuildMI(MBB, II, DL, TII.get(PPC::XXMFACC), SrcReg).addReg(SrcReg);
  if (DisableAutoPairedVecSt)
    spillRegPairs(MBB, II, DL, TII, Reg, FrameIndex, IsLittleEndian, IsKilled,
                  /* TwoPairs */ true);
  else {
    addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::STXVP))
                          .addReg(Reg, getKillRegState(IsKilled)),
                      FrameIndex, IsLittleEndian ? 32 : 0);
    addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::STXVP))
                          .addReg(Reg + 1, getKillRegState(IsKilled)),
                      FrameIndex, IsLittleEndian ? 0 : 32);
  }
  if (IsPrimed && !IsKilled)
    BuildMI(MBB, II, DL, TII.get(PPC::XXMTACC), SrcReg).addReg(SrcReg);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

/// lowerACCRestore - Generate the code to restore the accumulator register.
void PPCRegisterInfo::lowerACCRestore(MachineBasicBlock::iterator II,
                                      unsigned FrameIndex) const {
  MachineInstr &MI = *II; // <DestReg> = RESTORE_ACC <offset>
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  Register DestReg = MI.getOperand(0).getReg();
  assert(MI.definesRegister(DestReg, /*TRI=*/nullptr) &&
         "RESTORE_ACC does not define its destination");

  bool IsPrimed = PPC::ACCRCRegClass.contains(DestReg);
  Register Reg =
      PPC::VSRp0 + (DestReg - (IsPrimed ? PPC::ACC0 : PPC::UACC0)) * 2;
  bool IsLittleEndian = Subtarget.isLittleEndian();

  emitAccSpillRestoreInfo(MBB, IsPrimed, true);

  // Create two loads for the pair subregisters accounting for endianness and
  // then prime the accumulator register being restored.
  addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::LXVP), Reg),
                    FrameIndex, IsLittleEndian ? 32 : 0);
  addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::LXVP), Reg + 1),
                    FrameIndex, IsLittleEndian ? 0 : 32);
  if (IsPrimed)
    BuildMI(MBB, II, DL, TII.get(PPC::XXMTACC), DestReg).addReg(DestReg);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

/// lowerWACCSpilling - Generate the code for spilling the wide accumulator
/// register.
void PPCRegisterInfo::lowerWACCSpilling(MachineBasicBlock::iterator II,
                                        unsigned FrameIndex) const {
  MachineInstr &MI = *II; // SPILL_WACC <SrcReg>, <offset>
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  bool IsLittleEndian = Subtarget.isLittleEndian();

  emitWAccSpillRestoreInfo(MBB, false);

  const TargetRegisterClass *RC = &PPC::VSRpRCRegClass;
  Register VSRpReg0 = MF.getRegInfo().createVirtualRegister(RC);
  Register VSRpReg1 = MF.getRegInfo().createVirtualRegister(RC);
  Register SrcReg = MI.getOperand(0).getReg();

  BuildMI(MBB, II, DL, TII.get(PPC::DMXXEXTFDMR512), VSRpReg0)
      .addDef(VSRpReg1)
      .addReg(SrcReg);

  addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::STXVP))
                        .addReg(VSRpReg0, RegState::Kill),
                    FrameIndex, IsLittleEndian ? 32 : 0);
  addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::STXVP))
                        .addReg(VSRpReg1, RegState::Kill),
                    FrameIndex, IsLittleEndian ? 0 : 32);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

/// lowerWACCRestore - Generate the code to restore the wide accumulator
/// register.
void PPCRegisterInfo::lowerWACCRestore(MachineBasicBlock::iterator II,
                                       unsigned FrameIndex) const {
  MachineInstr &MI = *II; // <DestReg> = RESTORE_WACC <offset>
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  bool IsLittleEndian = Subtarget.isLittleEndian();

  emitWAccSpillRestoreInfo(MBB, true);

  const TargetRegisterClass *RC = &PPC::VSRpRCRegClass;
  Register VSRpReg0 = MF.getRegInfo().createVirtualRegister(RC);
  Register VSRpReg1 = MF.getRegInfo().createVirtualRegister(RC);
  Register DestReg = MI.getOperand(0).getReg();

  addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::LXVP), VSRpReg0),
                    FrameIndex, IsLittleEndian ? 32 : 0);
  addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::LXVP), VSRpReg1),
                    FrameIndex, IsLittleEndian ? 0 : 32);

  // Kill VSRpReg0, VSRpReg1   (killedRegState::Killed)
  BuildMI(MBB, II, DL, TII.get(PPC::DMXXINSTFDMR512), DestReg)
      .addReg(VSRpReg0, RegState::Kill)
      .addReg(VSRpReg1, RegState::Kill);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

/// lowerQuadwordSpilling - Generate code to spill paired general register.
void PPCRegisterInfo::lowerQuadwordSpilling(MachineBasicBlock::iterator II,
                                            unsigned FrameIndex) const {
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  Register SrcReg = MI.getOperand(0).getReg();
  bool IsKilled = MI.getOperand(0).isKill();

  Register Reg = PPC::X0 + (SrcReg - PPC::G8p0) * 2;
  bool IsLittleEndian = Subtarget.isLittleEndian();

  addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::STD))
                        .addReg(Reg, getKillRegState(IsKilled)),
                    FrameIndex, IsLittleEndian ? 8 : 0);
  addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::STD))
                        .addReg(Reg + 1, getKillRegState(IsKilled)),
                    FrameIndex, IsLittleEndian ? 0 : 8);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

/// lowerQuadwordRestore - Generate code to restore paired general register.
void PPCRegisterInfo::lowerQuadwordRestore(MachineBasicBlock::iterator II,
                                           unsigned FrameIndex) const {
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  Register DestReg = MI.getOperand(0).getReg();
  assert(MI.definesRegister(DestReg, /*TRI=*/nullptr) &&
         "RESTORE_QUADWORD does not define its destination");

  Register Reg = PPC::X0 + (DestReg - PPC::G8p0) * 2;
  bool IsLittleEndian = Subtarget.isLittleEndian();

  addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::LD), Reg), FrameIndex,
                    IsLittleEndian ? 8 : 0);
  addFrameReference(BuildMI(MBB, II, DL, TII.get(PPC::LD), Reg + 1), FrameIndex,
                    IsLittleEndian ? 0 : 8);

  // Discard the pseudo instruction.
  MBB.erase(II);
}

bool PPCRegisterInfo::hasReservedSpillSlot(const MachineFunction &MF,
                                           Register Reg, int &FrameIdx) const {
  // For the nonvolatile condition registers (CR2, CR3, CR4) return true to
  // prevent allocating an additional frame slot.
  // For 64-bit ELF and AIX, the CR save area is in the linkage area at SP+8,
  // for 32-bit AIX the CR save area is in the linkage area at SP+4.
  // We have created a FrameIndex to that spill slot to keep the CalleSaveInfos
  // valid.
  // For 32-bit ELF, we have previously created the stack slot if needed, so
  // return its FrameIdx.
  if (PPC::CR2 <= Reg && Reg <= PPC::CR4) {
    FrameIdx = MF.getInfo<PPCFunctionInfo>()->getCRSpillFrameIndex();
    return true;
  }
  return false;
}

// If the offset must be a multiple of some value, return what that value is.
static unsigned offsetMinAlignForOpcode(unsigned OpC) {
  switch (OpC) {
  default:
    return 1;
  case PPC::LWA:
  case PPC::LWA_32:
  case PPC::LD:
  case PPC::LDU:
  case PPC::STD:
  case PPC::STDU:
  case PPC::DFLOADf32:
  case PPC::DFLOADf64:
  case PPC::DFSTOREf32:
  case PPC::DFSTOREf64:
  case PPC::LXSD:
  case PPC::LXSSP:
  case PPC::STXSD:
  case PPC::STXSSP:
  case PPC::STQ:
    return 4;
  case PPC::EVLDD:
  case PPC::EVSTDD:
    return 8;
  case PPC::LXV:
  case PPC::STXV:
  case PPC::LQ:
  case PPC::LXVP:
  case PPC::STXVP:
    return 16;
  }
}

// If the offset must be a multiple of some value, return what that value is.
static unsigned offsetMinAlign(const MachineInstr &MI) {
  unsigned OpC = MI.getOpcode();
  return offsetMinAlignForOpcode(OpC);
}

// Return the OffsetOperandNo given the FIOperandNum (and the instruction).
static unsigned getOffsetONFromFION(const MachineInstr &MI,
                                    unsigned FIOperandNum) {
  // Take into account whether it's an add or mem instruction
  unsigned OffsetOperandNo = (FIOperandNum == 2) ? 1 : 2;
  if (MI.isInlineAsm())
    OffsetOperandNo = FIOperandNum - 1;
  else if (MI.getOpcode() == TargetOpcode::STACKMAP ||
           MI.getOpcode() == TargetOpcode::PATCHPOINT)
    OffsetOperandNo = FIOperandNum + 1;

  return OffsetOperandNo;
}

bool
PPCRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                     int SPAdj, unsigned FIOperandNum,
                                     RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");

  // Get the instruction.
  MachineInstr &MI = *II;
  // Get the instruction's basic block.
  MachineBasicBlock &MBB = *MI.getParent();
  // Get the basic block's function.
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  // Get the instruction info.
  const PPCInstrInfo &TII = *Subtarget.getInstrInfo();
  // Get the frame info.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  DebugLoc dl = MI.getDebugLoc();

  unsigned OffsetOperandNo = getOffsetONFromFION(MI, FIOperandNum);

  // Get the frame index.
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  // Get the frame pointer save index.  Users of this index are primarily
  // DYNALLOC instructions.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  int FPSI = FI->getFramePointerSaveIndex();
  // Get the instruction opcode.
  unsigned OpC = MI.getOpcode();

  if ((OpC == PPC::DYNAREAOFFSET || OpC == PPC::DYNAREAOFFSET8)) {
    lowerDynamicAreaOffset(II);
    // lowerDynamicAreaOffset erases II
    return true;
  }

  // Special case for dynamic alloca.
  if (FPSI && FrameIndex == FPSI &&
      (OpC == PPC::DYNALLOC || OpC == PPC::DYNALLOC8)) {
    lowerDynamicAlloc(II);
    // lowerDynamicAlloc erases II
    return true;
  }

  if (FPSI && FrameIndex == FPSI &&
      (OpC == PPC::PREPARE_PROBED_ALLOCA_64 ||
       OpC == PPC::PREPARE_PROBED_ALLOCA_32 ||
       OpC == PPC::PREPARE_PROBED_ALLOCA_NEGSIZE_SAME_REG_64 ||
       OpC == PPC::PREPARE_PROBED_ALLOCA_NEGSIZE_SAME_REG_32)) {
    lowerPrepareProbedAlloca(II);
    // lowerPrepareProbedAlloca erases II
    return true;
  }

  // Special case for pseudo-ops SPILL_CR and RESTORE_CR, etc.
  if (OpC == PPC::SPILL_CR) {
    lowerCRSpilling(II, FrameIndex);
    return true;
  } else if (OpC == PPC::RESTORE_CR) {
    lowerCRRestore(II, FrameIndex);
    return true;
  } else if (OpC == PPC::SPILL_CRBIT) {
    lowerCRBitSpilling(II, FrameIndex);
    return true;
  } else if (OpC == PPC::RESTORE_CRBIT) {
    lowerCRBitRestore(II, FrameIndex);
    return true;
  } else if (OpC == PPC::SPILL_ACC || OpC == PPC::SPILL_UACC) {
    lowerACCSpilling(II, FrameIndex);
    return true;
  } else if (OpC == PPC::RESTORE_ACC || OpC == PPC::RESTORE_UACC) {
    lowerACCRestore(II, FrameIndex);
    return true;
  } else if (OpC == PPC::STXVP && DisableAutoPairedVecSt) {
    lowerOctWordSpilling(II, FrameIndex);
    return true;
  } else if (OpC == PPC::SPILL_WACC) {
    lowerWACCSpilling(II, FrameIndex);
    return true;
  } else if (OpC == PPC::RESTORE_WACC) {
    lowerWACCRestore(II, FrameIndex);
    return true;
  } else if (OpC == PPC::SPILL_QUADWORD) {
    lowerQuadwordSpilling(II, FrameIndex);
    return true;
  } else if (OpC == PPC::RESTORE_QUADWORD) {
    lowerQuadwordRestore(II, FrameIndex);
    return true;
  }

  // Replace the FrameIndex with base register with GPR1 (SP) or GPR31 (FP).
  MI.getOperand(FIOperandNum).ChangeToRegister(
    FrameIndex < 0 ? getBaseRegister(MF) : getFrameRegister(MF), false);

  // If the instruction is not present in ImmToIdxMap, then it has no immediate
  // form (and must be r+r).
  bool noImmForm = !MI.isInlineAsm() && OpC != TargetOpcode::STACKMAP &&
                   OpC != TargetOpcode::PATCHPOINT && !ImmToIdxMap.count(OpC);

  // Now add the frame object offset to the offset from r1.
  int64_t Offset = MFI.getObjectOffset(FrameIndex);
  Offset += MI.getOperand(OffsetOperandNo).getImm();

  // If we're not using a Frame Pointer that has been set to the value of the
  // SP before having the stack size subtracted from it, then add the stack size
  // to Offset to get the correct offset.
  // Naked functions have stack size 0, although getStackSize may not reflect
  // that because we didn't call all the pieces that compute it for naked
  // functions.
  if (!MF.getFunction().hasFnAttribute(Attribute::Naked)) {
    if (!(hasBasePointer(MF) && FrameIndex < 0))
      Offset += MFI.getStackSize();
  }

  // If we encounter an LXVP/STXVP with an offset that doesn't fit, we can
  // transform it to the prefixed version so we don't have to use the XForm.
  if ((OpC == PPC::LXVP || OpC == PPC::STXVP) &&
      (!isInt<16>(Offset) || (Offset % offsetMinAlign(MI)) != 0) &&
      Subtarget.hasPrefixInstrs() && Subtarget.hasP10Vector()) {
    unsigned NewOpc = OpC == PPC::LXVP ? PPC::PLXVP : PPC::PSTXVP;
    MI.setDesc(TII.get(NewOpc));
    OpC = NewOpc;
  }

  // If we can, encode the offset directly into the instruction.  If this is a
  // normal PPC "ri" instruction, any 16-bit value can be safely encoded.  If
  // this is a PPC64 "ix" instruction, only a 16-bit value with the low two bits
  // clear can be encoded.  This is extremely uncommon, because normally you
  // only "std" to a stack slot that is at least 4-byte aligned, but it can
  // happen in invalid code.
  assert(OpC != PPC::DBG_VALUE &&
         "This should be handled in a target-independent way");
  // FIXME: This should be factored out to a separate function as prefixed
  // instructions add a number of opcodes for which we can use 34-bit imm.
  bool OffsetFitsMnemonic = (OpC == PPC::EVSTDD || OpC == PPC::EVLDD) ?
                            isUInt<8>(Offset) :
                            isInt<16>(Offset);
  if (TII.isPrefixed(MI.getOpcode()))
    OffsetFitsMnemonic = isInt<34>(Offset);
  if (!noImmForm && ((OffsetFitsMnemonic &&
                      ((Offset % offsetMinAlign(MI)) == 0)) ||
                     OpC == TargetOpcode::STACKMAP ||
                     OpC == TargetOpcode::PATCHPOINT)) {
    MI.getOperand(OffsetOperandNo).ChangeToImmediate(Offset);
    return false;
  }

  // The offset doesn't fit into a single register, scavenge one to build the
  // offset in.

  bool is64Bit = TM.isPPC64();
  const TargetRegisterClass *G8RC = &PPC::G8RCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;
  const TargetRegisterClass *RC = is64Bit ? G8RC : GPRC;
  unsigned NewOpcode = 0u;
  bool ScavengingFailed = RS && RS->getRegsAvailable(RC).none() &&
                          RS->getRegsAvailable(&PPC::VSFRCRegClass).any();
  Register SRegHi, SReg, VSReg;

  // The register scavenger is unable to get a GPR but can get a VSR. We
  // need to stash a GPR into a VSR so that we can free one up.
  if (ScavengingFailed && Subtarget.hasDirectMove()) {
    // Pick a volatile register and if we are spilling/restoring that
    // particular one, pick the next one.
    SRegHi = SReg = is64Bit ? PPC::X4 : PPC::R4;
    if (MI.getOperand(0).getReg() == SReg)
      SRegHi = SReg = SReg + 1;
    VSReg = MF.getRegInfo().createVirtualRegister(&PPC::VSFRCRegClass);
    BuildMI(MBB, II, dl, TII.get(is64Bit ? PPC::MTVSRD : PPC::MTVSRWZ), VSReg)
        .addReg(SReg);
  } else {
    SRegHi = MF.getRegInfo().createVirtualRegister(RC);
    SReg = MF.getRegInfo().createVirtualRegister(RC);
  }

  // Insert a set of rA with the full offset value before the ld, st, or add
  if (isInt<16>(Offset))
    BuildMI(MBB, II, dl, TII.get(is64Bit ? PPC::LI8 : PPC::LI), SReg)
        .addImm(Offset);
  else if (isInt<32>(Offset)) {
    BuildMI(MBB, II, dl, TII.get(is64Bit ? PPC::LIS8 : PPC::LIS), SRegHi)
        .addImm(Offset >> 16);
    BuildMI(MBB, II, dl, TII.get(is64Bit ? PPC::ORI8 : PPC::ORI), SReg)
        .addReg(SRegHi, RegState::Kill)
        .addImm(Offset);
  } else {
    assert(is64Bit && "Huge stack is only supported on PPC64");
    TII.materializeImmPostRA(MBB, II, dl, SReg, Offset);
  }

  // Convert into indexed form of the instruction:
  //
  //   sth 0:rA, 1:imm 2:(rB) ==> sthx 0:rA, 2:rB, 1:r0
  //   addi 0:rA 1:rB, 2, imm ==> add 0:rA, 1:rB, 2:r0
  unsigned OperandBase;

  if (noImmForm)
    OperandBase = 1;
  else if (OpC != TargetOpcode::INLINEASM &&
           OpC != TargetOpcode::INLINEASM_BR) {
    assert(ImmToIdxMap.count(OpC) &&
           "No indexed form of load or store available!");
    NewOpcode = ImmToIdxMap.find(OpC)->second;
    MI.setDesc(TII.get(NewOpcode));
    OperandBase = 1;
  } else {
    OperandBase = OffsetOperandNo;
  }

  Register StackReg = MI.getOperand(FIOperandNum).getReg();
  MI.getOperand(OperandBase).ChangeToRegister(StackReg, false);
  MI.getOperand(OperandBase + 1).ChangeToRegister(SReg, false, false, true);

  // If we stashed a value from a GPR into a VSR, we need to get it back after
  // spilling the register.
  if (ScavengingFailed && Subtarget.hasDirectMove())
    BuildMI(MBB, ++II, dl, TII.get(is64Bit ? PPC::MFVSRD : PPC::MFVSRWZ), SReg)
        .addReg(VSReg);

  // Since these are not real X-Form instructions, we must
  // add the registers and access 0(NewReg) rather than
  // emitting the X-Form pseudo.
  if (NewOpcode == PPC::LQX_PSEUDO || NewOpcode == PPC::STQX_PSEUDO) {
    assert(is64Bit && "Quadword loads/stores only supported in 64-bit mode");
    Register NewReg = MF.getRegInfo().createVirtualRegister(&PPC::G8RCRegClass);
    BuildMI(MBB, II, dl, TII.get(PPC::ADD8), NewReg)
        .addReg(SReg, RegState::Kill)
        .addReg(StackReg);
    MI.setDesc(TII.get(NewOpcode == PPC::LQX_PSEUDO ? PPC::LQ : PPC::STQ));
    MI.getOperand(OperandBase + 1).ChangeToRegister(NewReg, false);
    MI.getOperand(OperandBase).ChangeToImmediate(0);
  }
  return false;
}

Register PPCRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const PPCFrameLowering *TFI = getFrameLowering(MF);

  if (!TM.isPPC64())
    return TFI->hasFP(MF) ? PPC::R31 : PPC::R1;
  else
    return TFI->hasFP(MF) ? PPC::X31 : PPC::X1;
}

Register PPCRegisterInfo::getBaseRegister(const MachineFunction &MF) const {
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  if (!hasBasePointer(MF))
    return getFrameRegister(MF);

  if (TM.isPPC64())
    return PPC::X30;

  if (Subtarget.isSVR4ABI() && TM.isPositionIndependent())
    return PPC::R29;

  return PPC::R30;
}

bool PPCRegisterInfo::hasBasePointer(const MachineFunction &MF) const {
  if (!EnableBasePointer)
    return false;
  if (AlwaysBasePointer)
    return true;

  // If we need to realign the stack, then the stack pointer can no longer
  // serve as an offset into the caller's stack space. As a result, we need a
  // base pointer.
  return hasStackRealignment(MF);
}

/// Returns true if the instruction's frame index
/// reference would be better served by a base register other than FP
/// or SP. Used by LocalStackFrameAllocation to determine which frame index
/// references it should create new base registers for.
bool PPCRegisterInfo::
needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const {
  assert(Offset < 0 && "Local offset must be negative");

  // It's the load/store FI references that cause issues, as it can be difficult
  // to materialize the offset if it won't fit in the literal field. Estimate
  // based on the size of the local frame and some conservative assumptions
  // about the rest of the stack frame (note, this is pre-regalloc, so
  // we don't know everything for certain yet) whether this offset is likely
  // to be out of range of the immediate. Return true if so.

  // We only generate virtual base registers for loads and stores that have
  // an r+i form. Return false for everything else.
  unsigned OpC = MI->getOpcode();
  if (!ImmToIdxMap.count(OpC))
    return false;

  // Don't generate a new virtual base register just to add zero to it.
  if ((OpC == PPC::ADDI || OpC == PPC::ADDI8) &&
      MI->getOperand(2).getImm() == 0)
    return false;

  MachineBasicBlock &MBB = *MI->getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCFrameLowering *TFI = getFrameLowering(MF);
  unsigned StackEst = TFI->determineFrameLayout(MF, true);

  // If we likely don't need a stack frame, then we probably don't need a
  // virtual base register either.
  if (!StackEst)
    return false;

  // Estimate an offset from the stack pointer.
  // The incoming offset is relating to the SP at the start of the function,
  // but when we access the local it'll be relative to the SP after local
  // allocation, so adjust our SP-relative offset by that allocation size.
  Offset += StackEst;

  // The frame pointer will point to the end of the stack, so estimate the
  // offset as the difference between the object offset and the FP location.
  return !isFrameOffsetLegal(MI, getBaseRegister(MF), Offset);
}

/// Insert defining instruction(s) for BaseReg to
/// be a pointer to FrameIdx at the beginning of the basic block.
Register PPCRegisterInfo::materializeFrameBaseRegister(MachineBasicBlock *MBB,
                                                       int FrameIdx,
                                                       int64_t Offset) const {
  unsigned ADDriOpc = TM.isPPC64() ? PPC::ADDI8 : PPC::ADDI;

  MachineBasicBlock::iterator Ins = MBB->begin();
  DebugLoc DL;                  // Defaults to "unknown"
  if (Ins != MBB->end())
    DL = Ins->getDebugLoc();

  const MachineFunction &MF = *MBB->getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  const MCInstrDesc &MCID = TII.get(ADDriOpc);
  MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
  const TargetRegisterClass *RC = getPointerRegClass(MF);
  Register BaseReg = MRI.createVirtualRegister(RC);
  MRI.constrainRegClass(BaseReg, TII.getRegClass(MCID, 0, this, MF));

  BuildMI(*MBB, Ins, DL, MCID, BaseReg)
    .addFrameIndex(FrameIdx).addImm(Offset);

  return BaseReg;
}

void PPCRegisterInfo::resolveFrameIndex(MachineInstr &MI, Register BaseReg,
                                        int64_t Offset) const {
  unsigned FIOperandNum = 0;
  while (!MI.getOperand(FIOperandNum).isFI()) {
    ++FIOperandNum;
    assert(FIOperandNum < MI.getNumOperands() &&
           "Instr doesn't have FrameIndex operand!");
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(BaseReg, false);
  unsigned OffsetOperandNo = getOffsetONFromFION(MI, FIOperandNum);
  Offset += MI.getOperand(OffsetOperandNo).getImm();
  MI.getOperand(OffsetOperandNo).ChangeToImmediate(Offset);

  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  const MCInstrDesc &MCID = MI.getDesc();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MRI.constrainRegClass(BaseReg,
                        TII.getRegClass(MCID, FIOperandNum, this, MF));
}

bool PPCRegisterInfo::isFrameOffsetLegal(const MachineInstr *MI,
                                         Register BaseReg,
                                         int64_t Offset) const {
  unsigned FIOperandNum = 0;
  while (!MI->getOperand(FIOperandNum).isFI()) {
    ++FIOperandNum;
    assert(FIOperandNum < MI->getNumOperands() &&
           "Instr doesn't have FrameIndex operand!");
  }

  unsigned OffsetOperandNo = getOffsetONFromFION(*MI, FIOperandNum);
  Offset += MI->getOperand(OffsetOperandNo).getImm();

  return MI->getOpcode() == PPC::DBG_VALUE || // DBG_VALUE is always Reg+Imm
         MI->getOpcode() == TargetOpcode::STACKMAP ||
         MI->getOpcode() == TargetOpcode::PATCHPOINT ||
         (isInt<16>(Offset) && (Offset % offsetMinAlign(*MI)) == 0);
}
