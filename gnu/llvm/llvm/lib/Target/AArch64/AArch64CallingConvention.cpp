//=== AArch64CallingConvention.cpp - AArch64 CC impl ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the table-generated and custom routines for the AArch64
// Calling Convention.
//
//===----------------------------------------------------------------------===//

#include "AArch64CallingConvention.h"
#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/CallingConv.h"
using namespace llvm;

static const MCPhysReg XRegList[] = {AArch64::X0, AArch64::X1, AArch64::X2,
                                     AArch64::X3, AArch64::X4, AArch64::X5,
                                     AArch64::X6, AArch64::X7};
static const MCPhysReg HRegList[] = {AArch64::H0, AArch64::H1, AArch64::H2,
                                     AArch64::H3, AArch64::H4, AArch64::H5,
                                     AArch64::H6, AArch64::H7};
static const MCPhysReg SRegList[] = {AArch64::S0, AArch64::S1, AArch64::S2,
                                     AArch64::S3, AArch64::S4, AArch64::S5,
                                     AArch64::S6, AArch64::S7};
static const MCPhysReg DRegList[] = {AArch64::D0, AArch64::D1, AArch64::D2,
                                     AArch64::D3, AArch64::D4, AArch64::D5,
                                     AArch64::D6, AArch64::D7};
static const MCPhysReg QRegList[] = {AArch64::Q0, AArch64::Q1, AArch64::Q2,
                                     AArch64::Q3, AArch64::Q4, AArch64::Q5,
                                     AArch64::Q6, AArch64::Q7};
static const MCPhysReg ZRegList[] = {AArch64::Z0, AArch64::Z1, AArch64::Z2,
                                     AArch64::Z3, AArch64::Z4, AArch64::Z5,
                                     AArch64::Z6, AArch64::Z7};
static const MCPhysReg PRegList[] = {AArch64::P0, AArch64::P1, AArch64::P2,
                                     AArch64::P3};

static bool finishStackBlock(SmallVectorImpl<CCValAssign> &PendingMembers,
                             MVT LocVT, ISD::ArgFlagsTy &ArgFlags,
                             CCState &State, Align SlotAlign) {
  if (LocVT.isScalableVector()) {
    const AArch64Subtarget &Subtarget = static_cast<const AArch64Subtarget &>(
        State.getMachineFunction().getSubtarget());
    const AArch64TargetLowering *TLI = Subtarget.getTargetLowering();

    // We are about to reinvoke the CCAssignFn auto-generated handler. If we
    // don't unset these flags we will get stuck in an infinite loop forever
    // invoking the custom handler.
    ArgFlags.setInConsecutiveRegs(false);
    ArgFlags.setInConsecutiveRegsLast(false);

    // The calling convention for passing SVE tuples states that in the event
    // we cannot allocate enough registers for the tuple we should still leave
    // any remaining registers unallocated. However, when we call the
    // CCAssignFn again we want it to behave as if all remaining registers are
    // allocated. This will force the code to pass the tuple indirectly in
    // accordance with the PCS.
    bool ZRegsAllocated[8];
    for (int I = 0; I < 8; I++) {
      ZRegsAllocated[I] = State.isAllocated(ZRegList[I]);
      State.AllocateReg(ZRegList[I]);
    }
    // The same applies to P registers.
    bool PRegsAllocated[4];
    for (int I = 0; I < 4; I++) {
      PRegsAllocated[I] = State.isAllocated(PRegList[I]);
      State.AllocateReg(PRegList[I]);
    }

    auto &It = PendingMembers[0];
    CCAssignFn *AssignFn =
        TLI->CCAssignFnForCall(State.getCallingConv(), /*IsVarArg=*/false);
    if (AssignFn(It.getValNo(), It.getValVT(), It.getValVT(), CCValAssign::Full,
                 ArgFlags, State))
      llvm_unreachable("Call operand has unhandled type");

    // Return the flags to how they were before.
    ArgFlags.setInConsecutiveRegs(true);
    ArgFlags.setInConsecutiveRegsLast(true);

    // Return the register state back to how it was before, leaving any
    // unallocated registers available for other smaller types.
    for (int I = 0; I < 8; I++)
      if (!ZRegsAllocated[I])
        State.DeallocateReg(ZRegList[I]);
    for (int I = 0; I < 4; I++)
      if (!PRegsAllocated[I])
        State.DeallocateReg(PRegList[I]);

    // All pending members have now been allocated
    PendingMembers.clear();
    return true;
  }

  unsigned Size = LocVT.getSizeInBits() / 8;
  for (auto &It : PendingMembers) {
    It.convertToMem(State.AllocateStack(Size, SlotAlign));
    State.addLoc(It);
    SlotAlign = Align(1);
  }

  // All pending members have now been allocated
  PendingMembers.clear();
  return true;
}

/// The Darwin variadic PCS places anonymous arguments in 8-byte stack slots. An
/// [N x Ty] type must still be contiguous in memory though.
static bool CC_AArch64_Custom_Stack_Block(
      unsigned &ValNo, MVT &ValVT, MVT &LocVT, CCValAssign::LocInfo &LocInfo,
      ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  SmallVectorImpl<CCValAssign> &PendingMembers = State.getPendingLocs();

  // Add the argument to the list to be allocated once we know the size of the
  // block.
  PendingMembers.push_back(
      CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo));

  if (!ArgFlags.isInConsecutiveRegsLast())
    return true;

  return finishStackBlock(PendingMembers, LocVT, ArgFlags, State, Align(8));
}

/// Given an [N x Ty] block, it should be passed in a consecutive sequence of
/// registers. If no such sequence is available, mark the rest of the registers
/// of that type as used and place the argument on the stack.
static bool CC_AArch64_Custom_Block(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                    CCValAssign::LocInfo &LocInfo,
                                    ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  const AArch64Subtarget &Subtarget = static_cast<const AArch64Subtarget &>(
      State.getMachineFunction().getSubtarget());
  bool IsDarwinILP32 = Subtarget.isTargetILP32() && Subtarget.isTargetMachO();

  // Try to allocate a contiguous block of registers, each of the correct
  // size to hold one member.
  ArrayRef<MCPhysReg> RegList;
  if (LocVT.SimpleTy == MVT::i64 || (IsDarwinILP32 && LocVT.SimpleTy == MVT::i32))
    RegList = XRegList;
  else if (LocVT.SimpleTy == MVT::f16)
    RegList = HRegList;
  else if (LocVT.SimpleTy == MVT::f32 || LocVT.is32BitVector())
    RegList = SRegList;
  else if (LocVT.SimpleTy == MVT::f64 || LocVT.is64BitVector())
    RegList = DRegList;
  else if (LocVT.SimpleTy == MVT::f128 || LocVT.is128BitVector())
    RegList = QRegList;
  else if (LocVT.isScalableVector()) {
    // Scalable masks should be pass by Predicate registers.
    if (LocVT == MVT::nxv1i1 || LocVT == MVT::nxv2i1 || LocVT == MVT::nxv4i1 ||
        LocVT == MVT::nxv8i1 || LocVT == MVT::nxv16i1 ||
        LocVT == MVT::aarch64svcount)
      RegList = PRegList;
    else
      RegList = ZRegList;
  } else {
    // Not an array we want to split up after all.
    return false;
  }

  SmallVectorImpl<CCValAssign> &PendingMembers = State.getPendingLocs();

  // Add the argument to the list to be allocated once we know the size of the
  // block.
  PendingMembers.push_back(
      CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo));

  if (!ArgFlags.isInConsecutiveRegsLast())
    return true;

  // [N x i32] arguments get packed into x-registers on Darwin's arm64_32
  // because that's how the armv7k Clang front-end emits small structs.
  unsigned EltsPerReg = (IsDarwinILP32 && LocVT.SimpleTy == MVT::i32) ? 2 : 1;
  unsigned RegResult = State.AllocateRegBlock(
      RegList, alignTo(PendingMembers.size(), EltsPerReg) / EltsPerReg);
  if (RegResult && EltsPerReg == 1) {
    for (auto &It : PendingMembers) {
      It.convertToReg(RegResult);
      State.addLoc(It);
      ++RegResult;
    }
    PendingMembers.clear();
    return true;
  } else if (RegResult) {
    assert(EltsPerReg == 2 && "unexpected ABI");
    bool UseHigh = false;
    CCValAssign::LocInfo Info;
    for (auto &It : PendingMembers) {
      Info = UseHigh ? CCValAssign::AExtUpper : CCValAssign::ZExt;
      State.addLoc(CCValAssign::getReg(It.getValNo(), MVT::i32, RegResult,
                                       MVT::i64, Info));
      UseHigh = !UseHigh;
      if (!UseHigh)
        ++RegResult;
    }
    PendingMembers.clear();
    return true;
  }

  if (!LocVT.isScalableVector()) {
    // Mark all regs in the class as unavailable
    for (auto Reg : RegList)
      State.AllocateReg(Reg);
  }

  const Align StackAlign =
      State.getMachineFunction().getDataLayout().getStackAlignment();
  const Align MemAlign = ArgFlags.getNonZeroMemAlign();
  Align SlotAlign = std::min(MemAlign, StackAlign);
  if (!Subtarget.isTargetDarwin())
    SlotAlign = std::max(SlotAlign, Align(8));

  return finishStackBlock(PendingMembers, LocVT, ArgFlags, State, SlotAlign);
}

// TableGen provides definitions of the calling convention analysis entry
// points.
#include "AArch64GenCallingConv.inc"
