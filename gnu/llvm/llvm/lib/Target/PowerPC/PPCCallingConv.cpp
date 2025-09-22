//===-- PPCCallingConv.cpp - ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PPCRegisterInfo.h"
#include "PPCCallingConv.h"
#include "PPCSubtarget.h"
#include "PPCCCState.h"
using namespace llvm;

inline bool CC_PPC_AnyReg_Error(unsigned &, MVT &, MVT &,
                                CCValAssign::LocInfo &, ISD::ArgFlagsTy &,
                                CCState &) {
  llvm_unreachable("The AnyReg calling convention is only supported by the " \
                   "stackmap and patchpoint intrinsics.");
  // gracefully fallback to PPC C calling convention on Release builds.
  return false;
}

// This function handles the shadowing of GPRs for fp and vector types,
// and is a depiction of the algorithm described in the ELFv2 ABI,
// Section 2.2.4.1: Parameter Passing Register Selection Algorithm.
inline bool CC_PPC64_ELF_Shadow_GPR_Regs(unsigned &ValNo, MVT &ValVT,
                                         MVT &LocVT,
                                         CCValAssign::LocInfo &LocInfo,
                                         ISD::ArgFlagsTy &ArgFlags,
                                         CCState &State) {

  // The 64-bit ELFv2 ABI-defined parameter passing general purpose registers.
  static const MCPhysReg ELF64ArgGPRs[] = {PPC::X3, PPC::X4, PPC::X5, PPC::X6,
                                           PPC::X7, PPC::X8, PPC::X9, PPC::X10};
  const unsigned ELF64NumArgGPRs = std::size(ELF64ArgGPRs);

  unsigned FirstUnallocGPR = State.getFirstUnallocated(ELF64ArgGPRs);
  if (FirstUnallocGPR == ELF64NumArgGPRs)
    return false;

  // As described in 2.2.4.1 under the "float" section, shadow a single GPR
  // for single/double precision. ppcf128 gets broken up into two doubles
  // and will also shadow GPRs within this section.
  if (LocVT == MVT::f32 || LocVT == MVT::f64)
    State.AllocateReg(ELF64ArgGPRs);
  else if (LocVT.is128BitVector() || (LocVT == MVT::f128)) {
    // For vector and __float128 (which is represents the "vector" section
    // in 2.2.4.1), shadow two even GPRs (skipping the odd one if it is next
    // in the allocation order). To check if the GPR is even, the specific
    // condition checks if the register allocated is odd, because the even
    // physical registers are odd values.
    if ((State.AllocateReg(ELF64ArgGPRs) - PPC::X3) % 2 == 1)
      State.AllocateReg(ELF64ArgGPRs);
    State.AllocateReg(ELF64ArgGPRs);
  }
  return false;
}

static bool CC_PPC32_SVR4_Custom_Dummy(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                       CCValAssign::LocInfo &LocInfo,
                                       ISD::ArgFlagsTy &ArgFlags,
                                       CCState &State) {
  return true;
}

static bool CC_PPC32_SVR4_Custom_AlignArgRegs(unsigned &ValNo, MVT &ValVT,
                                              MVT &LocVT,
                                              CCValAssign::LocInfo &LocInfo,
                                              ISD::ArgFlagsTy &ArgFlags,
                                              CCState &State) {
  static const MCPhysReg ArgRegs[] = {
    PPC::R3, PPC::R4, PPC::R5, PPC::R6,
    PPC::R7, PPC::R8, PPC::R9, PPC::R10,
  };
  const unsigned NumArgRegs = std::size(ArgRegs);

  unsigned RegNum = State.getFirstUnallocated(ArgRegs);

  // Skip one register if the first unallocated register has an even register
  // number and there are still argument registers available which have not been
  // allocated yet. RegNum is actually an index into ArgRegs, which means we
  // need to skip a register if RegNum is odd.
  if (RegNum != NumArgRegs && RegNum % 2 == 1) {
    State.AllocateReg(ArgRegs[RegNum]);
  }

  // Always return false here, as this function only makes sure that the first
  // unallocated register has an odd register number and does not actually
  // allocate a register for the current argument.
  return false;
}

static bool CC_PPC32_SVR4_Custom_SkipLastArgRegsPPCF128(
    unsigned &ValNo, MVT &ValVT, MVT &LocVT, CCValAssign::LocInfo &LocInfo,
    ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  static const MCPhysReg ArgRegs[] = {
    PPC::R3, PPC::R4, PPC::R5, PPC::R6,
    PPC::R7, PPC::R8, PPC::R9, PPC::R10,
  };
  const unsigned NumArgRegs = std::size(ArgRegs);

  unsigned RegNum = State.getFirstUnallocated(ArgRegs);
  int RegsLeft = NumArgRegs - RegNum;

  // Skip if there is not enough registers left for long double type (4 gpr regs
  // in soft float mode) and put long double argument on the stack.
  if (RegNum != NumArgRegs && RegsLeft < 4) {
    for (int i = 0; i < RegsLeft; i++) {
      State.AllocateReg(ArgRegs[RegNum + i]);
    }
  }

  return false;
}

static bool CC_PPC32_SVR4_Custom_AlignFPArgRegs(unsigned &ValNo, MVT &ValVT,
                                                MVT &LocVT,
                                                CCValAssign::LocInfo &LocInfo,
                                                ISD::ArgFlagsTy &ArgFlags,
                                                CCState &State) {
  static const MCPhysReg ArgRegs[] = {
    PPC::F1, PPC::F2, PPC::F3, PPC::F4, PPC::F5, PPC::F6, PPC::F7,
    PPC::F8
  };

  const unsigned NumArgRegs = std::size(ArgRegs);

  unsigned RegNum = State.getFirstUnallocated(ArgRegs);

  // If there is only one Floating-point register left we need to put both f64
  // values of a split ppc_fp128 value on the stack.
  if (RegNum != NumArgRegs && ArgRegs[RegNum] == PPC::F8) {
    State.AllocateReg(ArgRegs[RegNum]);
  }

  // Always return false here, as this function only makes sure that the two f64
  // values a ppc_fp128 value is split into are both passed in registers or both
  // passed on the stack and does not actually allocate a register for the
  // current argument.
  return false;
}

// Split F64 arguments into two 32-bit consecutive registers.
static bool CC_PPC32_SPE_CustomSplitFP64(unsigned &ValNo, MVT &ValVT,
                                        MVT &LocVT,
                                        CCValAssign::LocInfo &LocInfo,
                                        ISD::ArgFlagsTy &ArgFlags,
                                        CCState &State) {
  static const MCPhysReg HiRegList[] = { PPC::R3, PPC::R5, PPC::R7, PPC::R9 };
  static const MCPhysReg LoRegList[] = { PPC::R4, PPC::R6, PPC::R8, PPC::R10 };

  // Try to get the first register.
  unsigned Reg = State.AllocateReg(HiRegList);
  if (!Reg)
    return false;

  unsigned i;
  for (i = 0; i < std::size(HiRegList); ++i)
    if (HiRegList[i] == Reg)
      break;

  unsigned T = State.AllocateReg(LoRegList[i]);
  (void)T;
  assert(T == LoRegList[i] && "Could not allocate register");

  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, LoRegList[i],
                                         LocVT, LocInfo));
  return true;
}

// Same as above, but for return values, so only allocate for R3 and R4
static bool CC_PPC32_SPE_RetF64(unsigned &ValNo, MVT &ValVT,
                               MVT &LocVT,
                               CCValAssign::LocInfo &LocInfo,
                               ISD::ArgFlagsTy &ArgFlags,
                               CCState &State) {
  static const MCPhysReg HiRegList[] = { PPC::R3 };
  static const MCPhysReg LoRegList[] = { PPC::R4 };

  // Try to get the first register.
  unsigned Reg = State.AllocateReg(HiRegList, LoRegList);
  if (!Reg)
    return false;

  unsigned i;
  for (i = 0; i < std::size(HiRegList); ++i)
    if (HiRegList[i] == Reg)
      break;

  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, LoRegList[i],
                                         LocVT, LocInfo));
  return true;
}

#include "PPCGenCallingConv.inc"
