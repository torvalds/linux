//=== X86CallingConv.cpp - X86 Custom Calling Convention Impl   -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of custom routines for the X86
// Calling Convention that aren't done by tablegen.
//
//===----------------------------------------------------------------------===//

#include "X86CallingConv.h"
#include "X86Subtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Module.h"

using namespace llvm;

/// When regcall calling convention compiled to 32 bit arch, special treatment
/// is required for 64 bit masks.
/// The value should be assigned to two GPRs.
/// \return true if registers were allocated and false otherwise.
static bool CC_X86_32_RegCall_Assign2Regs(unsigned &ValNo, MVT &ValVT,
                                          MVT &LocVT,
                                          CCValAssign::LocInfo &LocInfo,
                                          ISD::ArgFlagsTy &ArgFlags,
                                          CCState &State) {
  // List of GPR registers that are available to store values in regcall
  // calling convention.
  static const MCPhysReg RegList[] = {X86::EAX, X86::ECX, X86::EDX, X86::EDI,
                                      X86::ESI};

  // The vector will save all the available registers for allocation.
  SmallVector<unsigned, 5> AvailableRegs;

  // searching for the available registers.
  for (auto Reg : RegList) {
    if (!State.isAllocated(Reg))
      AvailableRegs.push_back(Reg);
  }

  const size_t RequiredGprsUponSplit = 2;
  if (AvailableRegs.size() < RequiredGprsUponSplit)
    return false; // Not enough free registers - continue the search.

  // Allocating the available registers.
  for (unsigned I = 0; I < RequiredGprsUponSplit; I++) {

    // Marking the register as located.
    unsigned Reg = State.AllocateReg(AvailableRegs[I]);

    // Since we previously made sure that 2 registers are available
    // we expect that a real register number will be returned.
    assert(Reg && "Expecting a register will be available");

    // Assign the value to the allocated register
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  }

  // Successful in allocating registers - stop scanning next rules.
  return true;
}

static ArrayRef<MCPhysReg> CC_X86_VectorCallGetSSEs(const MVT &ValVT) {
  if (ValVT.is512BitVector()) {
    static const MCPhysReg RegListZMM[] = {X86::ZMM0, X86::ZMM1, X86::ZMM2,
                                           X86::ZMM3, X86::ZMM4, X86::ZMM5};
    return ArrayRef(std::begin(RegListZMM), std::end(RegListZMM));
  }

  if (ValVT.is256BitVector()) {
    static const MCPhysReg RegListYMM[] = {X86::YMM0, X86::YMM1, X86::YMM2,
                                           X86::YMM3, X86::YMM4, X86::YMM5};
    return ArrayRef(std::begin(RegListYMM), std::end(RegListYMM));
  }

  static const MCPhysReg RegListXMM[] = {X86::XMM0, X86::XMM1, X86::XMM2,
                                         X86::XMM3, X86::XMM4, X86::XMM5};
  return ArrayRef(std::begin(RegListXMM), std::end(RegListXMM));
}

static ArrayRef<MCPhysReg> CC_X86_64_VectorCallGetGPRs() {
  static const MCPhysReg RegListGPR[] = {X86::RCX, X86::RDX, X86::R8, X86::R9};
  return ArrayRef(std::begin(RegListGPR), std::end(RegListGPR));
}

static bool CC_X86_VectorCallAssignRegister(unsigned &ValNo, MVT &ValVT,
                                            MVT &LocVT,
                                            CCValAssign::LocInfo &LocInfo,
                                            ISD::ArgFlagsTy &ArgFlags,
                                            CCState &State) {

  ArrayRef<MCPhysReg> RegList = CC_X86_VectorCallGetSSEs(ValVT);
  bool Is64bit = static_cast<const X86Subtarget &>(
                     State.getMachineFunction().getSubtarget())
                     .is64Bit();

  for (auto Reg : RegList) {
    // If the register is not marked as allocated - assign to it.
    if (!State.isAllocated(Reg)) {
      unsigned AssigedReg = State.AllocateReg(Reg);
      assert(AssigedReg == Reg && "Expecting a valid register allocation");
      State.addLoc(
          CCValAssign::getReg(ValNo, ValVT, AssigedReg, LocVT, LocInfo));
      return true;
    }
    // If the register is marked as shadow allocated - assign to it.
    if (Is64bit && State.IsShadowAllocatedReg(Reg)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return true;
    }
  }

  llvm_unreachable("Clang should ensure that hva marked vectors will have "
                   "an available register.");
  return false;
}

/// Vectorcall calling convention has special handling for vector types or
/// HVA for 64 bit arch.
/// For HVAs shadow registers might be allocated on the first pass
/// and actual XMM registers are allocated on the second pass.
/// For vector types, actual XMM registers are allocated on the first pass.
/// \return true if registers were allocated and false otherwise.
static bool CC_X86_64_VectorCall(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                 CCValAssign::LocInfo &LocInfo,
                                 ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  // On the second pass, go through the HVAs only.
  if (ArgFlags.isSecArgPass()) {
    if (ArgFlags.isHva())
      return CC_X86_VectorCallAssignRegister(ValNo, ValVT, LocVT, LocInfo,
                                             ArgFlags, State);
    return true;
  }

  // Process only vector types as defined by vectorcall spec:
  // "A vector type is either a floating-point type, for example,
  //  a float or double, or an SIMD vector type, for example, __m128 or __m256".
  if (!(ValVT.isFloatingPoint() ||
        (ValVT.isVector() && ValVT.getSizeInBits() >= 128))) {
    // If R9 was already assigned it means that we are after the fourth element
    // and because this is not an HVA / Vector type, we need to allocate
    // shadow XMM register.
    if (State.isAllocated(X86::R9)) {
      // Assign shadow XMM register.
      (void)State.AllocateReg(CC_X86_VectorCallGetSSEs(ValVT));
    }

    return false;
  }

  if (!ArgFlags.isHva() || ArgFlags.isHvaStart()) {
    // Assign shadow GPR register.
    (void)State.AllocateReg(CC_X86_64_VectorCallGetGPRs());

    // Assign XMM register - (shadow for HVA and non-shadow for non HVA).
    if (unsigned Reg = State.AllocateReg(CC_X86_VectorCallGetSSEs(ValVT))) {
      // In Vectorcall Calling convention, additional shadow stack can be
      // created on top of the basic 32 bytes of win64.
      // It can happen if the fifth or sixth argument is vector type or HVA.
      // At that case for each argument a shadow stack of 8 bytes is allocated.
      const TargetRegisterInfo *TRI =
          State.getMachineFunction().getSubtarget().getRegisterInfo();
      if (TRI->regsOverlap(Reg, X86::XMM4) ||
          TRI->regsOverlap(Reg, X86::XMM5))
        State.AllocateStack(8, Align(8));

      if (!ArgFlags.isHva()) {
        State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
        return true; // Allocated a register - Stop the search.
      }
    }
  }

  // If this is an HVA - Stop the search,
  // otherwise continue the search.
  return ArgFlags.isHva();
}

/// Vectorcall calling convention has special handling for vector types or
/// HVA for 32 bit arch.
/// For HVAs actual XMM registers are allocated on the second pass.
/// For vector types, actual XMM registers are allocated on the first pass.
/// \return true if registers were allocated and false otherwise.
static bool CC_X86_32_VectorCall(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                 CCValAssign::LocInfo &LocInfo,
                                 ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  // On the second pass, go through the HVAs only.
  if (ArgFlags.isSecArgPass()) {
    if (ArgFlags.isHva())
      return CC_X86_VectorCallAssignRegister(ValNo, ValVT, LocVT, LocInfo,
                                             ArgFlags, State);
    return true;
  }

  // Process only vector types as defined by vectorcall spec:
  // "A vector type is either a floating point type, for example,
  //  a float or double, or an SIMD vector type, for example, __m128 or __m256".
  if (!(ValVT.isFloatingPoint() ||
        (ValVT.isVector() && ValVT.getSizeInBits() >= 128))) {
    return false;
  }

  if (ArgFlags.isHva())
    return true; // If this is an HVA - Stop the search.

  // Assign XMM register.
  if (unsigned Reg = State.AllocateReg(CC_X86_VectorCallGetSSEs(ValVT))) {
    State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
    return true;
  }

  // In case we did not find an available XMM register for a vector -
  // pass it indirectly.
  // It is similar to CCPassIndirect, with the addition of inreg.
  if (!ValVT.isFloatingPoint()) {
    LocVT = MVT::i32;
    LocInfo = CCValAssign::Indirect;
    ArgFlags.setInReg();
  }

  return false; // No register was assigned - Continue the search.
}

static bool CC_X86_AnyReg_Error(unsigned &, MVT &, MVT &,
                                CCValAssign::LocInfo &, ISD::ArgFlagsTy &,
                                CCState &) {
  llvm_unreachable("The AnyReg calling convention is only supported by the "
                   "stackmap and patchpoint intrinsics.");
  // gracefully fallback to X86 C calling convention on Release builds.
  return false;
}

static bool CC_X86_32_MCUInReg(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                               CCValAssign::LocInfo &LocInfo,
                               ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  // This is similar to CCAssignToReg<[EAX, EDX, ECX]>, but makes sure
  // not to split i64 and double between a register and stack
  static const MCPhysReg RegList[] = {X86::EAX, X86::EDX, X86::ECX};
  static const unsigned NumRegs = std::size(RegList);

  SmallVectorImpl<CCValAssign> &PendingMembers = State.getPendingLocs();

  // If this is the first part of an double/i64/i128, or if we're already
  // in the middle of a split, add to the pending list. If this is not
  // the end of the split, return, otherwise go on to process the pending
  // list
  if (ArgFlags.isSplit() || !PendingMembers.empty()) {
    PendingMembers.push_back(
        CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo));
    if (!ArgFlags.isSplitEnd())
      return true;
  }

  // If there are no pending members, we are not in the middle of a split,
  // so do the usual inreg stuff.
  if (PendingMembers.empty()) {
    if (unsigned Reg = State.AllocateReg(RegList)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return true;
    }
    return false;
  }

  assert(ArgFlags.isSplitEnd());

  // We now have the entire original argument in PendingMembers, so decide
  // whether to use registers or the stack.
  // Per the MCU ABI:
  // a) To use registers, we need to have enough of them free to contain
  // the entire argument.
  // b) We never want to use more than 2 registers for a single argument.

  unsigned FirstFree = State.getFirstUnallocated(RegList);
  bool UseRegs = PendingMembers.size() <= std::min(2U, NumRegs - FirstFree);

  for (auto &It : PendingMembers) {
    if (UseRegs)
      It.convertToReg(State.AllocateReg(RegList[FirstFree++]));
    else
      It.convertToMem(State.AllocateStack(4, Align(4)));
    State.addLoc(It);
  }

  PendingMembers.clear();

  return true;
}

/// X86 interrupt handlers can only take one or two stack arguments, but if
/// there are two arguments, they are in the opposite order from the standard
/// convention. Therefore, we have to look at the argument count up front before
/// allocating stack for each argument.
static bool CC_X86_Intr(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                        CCValAssign::LocInfo &LocInfo,
                        ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  const MachineFunction &MF = State.getMachineFunction();
  size_t ArgCount = State.getMachineFunction().getFunction().arg_size();
  bool Is64Bit = MF.getSubtarget<X86Subtarget>().is64Bit();
  unsigned SlotSize = Is64Bit ? 8 : 4;
  unsigned Offset;
  if (ArgCount == 1 && ValNo == 0) {
    // If we have one argument, the argument is five stack slots big, at fixed
    // offset zero.
    Offset = State.AllocateStack(5 * SlotSize, Align(4));
  } else if (ArgCount == 2 && ValNo == 0) {
    // If we have two arguments, the stack slot is *after* the error code
    // argument. Pretend it doesn't consume stack space, and account for it when
    // we assign the second argument.
    Offset = SlotSize;
  } else if (ArgCount == 2 && ValNo == 1) {
    // If this is the second of two arguments, it must be the error code. It
    // appears first on the stack, and is then followed by the five slot
    // interrupt struct.
    Offset = 0;
    (void)State.AllocateStack(6 * SlotSize, Align(4));
  } else {
    report_fatal_error("unsupported x86 interrupt prototype");
  }

  // FIXME: This should be accounted for in
  // X86FrameLowering::getFrameIndexReference, not here.
  if (Is64Bit && ArgCount == 2)
    Offset += SlotSize;

  State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
  return true;
}

static bool CC_X86_64_Pointer(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                              CCValAssign::LocInfo &LocInfo,
                              ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  if (LocVT != MVT::i64) {
    LocVT = MVT::i64;
    LocInfo = CCValAssign::ZExt;
  }
  return false;
}

// Provides entry points of CC_X86 and RetCC_X86.
#include "X86GenCallingConv.inc"
