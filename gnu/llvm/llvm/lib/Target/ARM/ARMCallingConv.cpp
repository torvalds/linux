//=== ARMCallingConv.cpp - ARM Custom CC Routines ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the custom routines for the ARM Calling Convention that
// aren't done by tablegen, and includes the table generated implementations.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMCallingConv.h"
#include "ARMSubtarget.h"
#include "ARMRegisterInfo.h"
using namespace llvm;

// APCS f64 is in register pairs, possibly split to stack
static bool f64AssignAPCS(unsigned ValNo, MVT ValVT, MVT LocVT,
                          CCValAssign::LocInfo LocInfo,
                          CCState &State, bool CanFail) {
  static const MCPhysReg RegList[] = { ARM::R0, ARM::R1, ARM::R2, ARM::R3 };

  // Try to get the first register.
  if (unsigned Reg = State.AllocateReg(RegList))
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  else {
    // For the 2nd half of a v2f64, do not fail.
    if (CanFail)
      return false;

    // Put the whole thing on the stack.
    State.addLoc(CCValAssign::getCustomMem(
        ValNo, ValVT, State.AllocateStack(8, Align(4)), LocVT, LocInfo));
    return true;
  }

  // Try to get the second register.
  if (unsigned Reg = State.AllocateReg(RegList))
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  else
    State.addLoc(CCValAssign::getCustomMem(
        ValNo, ValVT, State.AllocateStack(4, Align(4)), LocVT, LocInfo));
  return true;
}

static bool CC_ARM_APCS_Custom_f64(unsigned ValNo, MVT ValVT, MVT LocVT,
                                   CCValAssign::LocInfo LocInfo,
                                   ISD::ArgFlagsTy ArgFlags,
                                   CCState &State) {
  if (!f64AssignAPCS(ValNo, ValVT, LocVT, LocInfo, State, true))
    return false;
  if (LocVT == MVT::v2f64 &&
      !f64AssignAPCS(ValNo, ValVT, LocVT, LocInfo, State, false))
    return false;
  return true;  // we handled it
}

// AAPCS f64 is in aligned register pairs
static bool f64AssignAAPCS(unsigned ValNo, MVT ValVT, MVT LocVT,
                           CCValAssign::LocInfo LocInfo,
                           CCState &State, bool CanFail) {
  static const MCPhysReg HiRegList[] = { ARM::R0, ARM::R2 };
  static const MCPhysReg LoRegList[] = { ARM::R1, ARM::R3 };
  static const MCPhysReg ShadowRegList[] = { ARM::R0, ARM::R1 };
  static const MCPhysReg GPRArgRegs[] = { ARM::R0, ARM::R1, ARM::R2, ARM::R3 };

  unsigned Reg = State.AllocateReg(HiRegList, ShadowRegList);
  if (Reg == 0) {

    // If we had R3 unallocated only, now we still must to waste it.
    Reg = State.AllocateReg(GPRArgRegs);
    assert((!Reg || Reg == ARM::R3) && "Wrong GPRs usage for f64");

    // For the 2nd half of a v2f64, do not just fail.
    if (CanFail)
      return false;

    // Put the whole thing on the stack.
    State.addLoc(CCValAssign::getCustomMem(
        ValNo, ValVT, State.AllocateStack(8, Align(8)), LocVT, LocInfo));
    return true;
  }

  unsigned i;
  for (i = 0; i < 2; ++i)
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

static bool CC_ARM_AAPCS_Custom_f64(unsigned ValNo, MVT ValVT, MVT LocVT,
                                    CCValAssign::LocInfo LocInfo,
                                    ISD::ArgFlagsTy ArgFlags,
                                    CCState &State) {
  if (!f64AssignAAPCS(ValNo, ValVT, LocVT, LocInfo, State, true))
    return false;
  if (LocVT == MVT::v2f64 &&
      !f64AssignAAPCS(ValNo, ValVT, LocVT, LocInfo, State, false))
    return false;
  return true;  // we handled it
}

static bool f64RetAssign(unsigned ValNo, MVT ValVT, MVT LocVT,
                         CCValAssign::LocInfo LocInfo, CCState &State) {
  static const MCPhysReg HiRegList[] = { ARM::R0, ARM::R2 };
  static const MCPhysReg LoRegList[] = { ARM::R1, ARM::R3 };

  unsigned Reg = State.AllocateReg(HiRegList, LoRegList);
  if (Reg == 0)
    return false; // we didn't handle it

  unsigned i;
  for (i = 0; i < 2; ++i)
    if (HiRegList[i] == Reg)
      break;

  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
  State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, LoRegList[i],
                                         LocVT, LocInfo));
  return true;
}

static bool RetCC_ARM_APCS_Custom_f64(unsigned ValNo, MVT ValVT, MVT LocVT,
                                      CCValAssign::LocInfo LocInfo,
                                      ISD::ArgFlagsTy ArgFlags,
                                      CCState &State) {
  if (!f64RetAssign(ValNo, ValVT, LocVT, LocInfo, State))
    return false;
  if (LocVT == MVT::v2f64 && !f64RetAssign(ValNo, ValVT, LocVT, LocInfo, State))
    return false;
  return true;  // we handled it
}

static bool RetCC_ARM_AAPCS_Custom_f64(unsigned ValNo, MVT ValVT, MVT LocVT,
                                       CCValAssign::LocInfo LocInfo,
                                       ISD::ArgFlagsTy ArgFlags,
                                       CCState &State) {
  return RetCC_ARM_APCS_Custom_f64(ValNo, ValVT, LocVT, LocInfo, ArgFlags,
                                   State);
}

static const MCPhysReg RRegList[] = { ARM::R0,  ARM::R1,  ARM::R2,  ARM::R3 };

static const MCPhysReg SRegList[] = { ARM::S0,  ARM::S1,  ARM::S2,  ARM::S3,
                                      ARM::S4,  ARM::S5,  ARM::S6,  ARM::S7,
                                      ARM::S8,  ARM::S9,  ARM::S10, ARM::S11,
                                      ARM::S12, ARM::S13, ARM::S14,  ARM::S15 };
static const MCPhysReg DRegList[] = { ARM::D0, ARM::D1, ARM::D2, ARM::D3,
                                      ARM::D4, ARM::D5, ARM::D6, ARM::D7 };
static const MCPhysReg QRegList[] = { ARM::Q0, ARM::Q1, ARM::Q2, ARM::Q3 };


// Allocate part of an AAPCS HFA or HVA. We assume that each member of the HA
// has InConsecutiveRegs set, and that the last member also has
// InConsecutiveRegsLast set. We must process all members of the HA before
// we can allocate it, as we need to know the total number of registers that
// will be needed in order to (attempt to) allocate a contiguous block.
static bool CC_ARM_AAPCS_Custom_Aggregate(unsigned ValNo, MVT ValVT,
                                          MVT LocVT,
                                          CCValAssign::LocInfo LocInfo,
                                          ISD::ArgFlagsTy ArgFlags,
                                          CCState &State) {
  SmallVectorImpl<CCValAssign> &PendingMembers = State.getPendingLocs();

  // AAPCS HFAs must have 1-4 elements, all of the same type
  if (PendingMembers.size() > 0)
    assert(PendingMembers[0].getLocVT() == LocVT);

  // Add the argument to the list to be allocated once we know the size of the
  // aggregate. Store the type's required alignment as extra info for later: in
  // the [N x i64] case all trace has been removed by the time we actually get
  // to do allocation.
  PendingMembers.push_back(CCValAssign::getPending(
      ValNo, ValVT, LocVT, LocInfo, ArgFlags.getNonZeroOrigAlign().value()));

  if (!ArgFlags.isInConsecutiveRegsLast())
    return true;

  // Try to allocate a contiguous block of registers, each of the correct
  // size to hold one member.
  auto &DL = State.getMachineFunction().getDataLayout();
  const Align StackAlign = DL.getStackAlignment();
  const Align FirstMemberAlign(PendingMembers[0].getExtraInfo());
  Align Alignment = std::min(FirstMemberAlign, StackAlign);

  ArrayRef<MCPhysReg> RegList;
  switch (LocVT.SimpleTy) {
  case MVT::i32: {
    RegList = RRegList;
    unsigned RegIdx = State.getFirstUnallocated(RegList);

    // First consume all registers that would give an unaligned object. Whether
    // we go on stack or in regs, no-one will be using them in future.
    unsigned RegAlign = alignTo(Alignment.value(), 4) / 4;
    while (RegIdx % RegAlign != 0 && RegIdx < RegList.size())
      State.AllocateReg(RegList[RegIdx++]);

    break;
  }
  case MVT::f16:
  case MVT::bf16:
  case MVT::f32:
    RegList = SRegList;
    break;
  case MVT::v4f16:
  case MVT::v4bf16:
  case MVT::f64:
    RegList = DRegList;
    break;
  case MVT::v8f16:
  case MVT::v8bf16:
  case MVT::v2f64:
    RegList = QRegList;
    break;
  default:
    llvm_unreachable("Unexpected member type for block aggregate");
    break;
  }

  unsigned RegResult = State.AllocateRegBlock(RegList, PendingMembers.size());
  if (RegResult) {
    for (CCValAssign &PendingMember : PendingMembers) {
      PendingMember.convertToReg(RegResult);
      State.addLoc(PendingMember);
      ++RegResult;
    }
    PendingMembers.clear();
    return true;
  }

  // Register allocation failed, we'll be needing the stack
  unsigned Size = LocVT.getSizeInBits() / 8;
  if (LocVT == MVT::i32 && State.getStackSize() == 0) {
    // If nothing else has used the stack until this point, a non-HFA aggregate
    // can be split between regs and stack.
    unsigned RegIdx = State.getFirstUnallocated(RegList);
    for (auto &It : PendingMembers) {
      if (RegIdx >= RegList.size())
        It.convertToMem(State.AllocateStack(Size, Align(Size)));
      else
        It.convertToReg(State.AllocateReg(RegList[RegIdx++]));

      State.addLoc(It);
    }
    PendingMembers.clear();
    return true;
  }

  if (LocVT != MVT::i32)
    RegList = SRegList;

  // Mark all regs as unavailable (AAPCS rule C.2.vfp for VFP, C.6 for core)
  for (auto Reg : RegList)
    State.AllocateReg(Reg);

  // Clamp the alignment between 4 and 8.
  if (State.getMachineFunction().getSubtarget<ARMSubtarget>().isTargetAEABI())
    Alignment = ArgFlags.getNonZeroMemAlign() <= 4 ? Align(4) : Align(8);

  // After the first item has been allocated, the rest are packed as tightly as
  // possible. (E.g. an incoming i64 would have starting Align of 8, but we'll
  // be allocating a bunch of i32 slots).
  for (auto &It : PendingMembers) {
    It.convertToMem(State.AllocateStack(Size, Alignment));
    State.addLoc(It);
    Alignment = Align(1);
  }

  // All pending members have now been allocated
  PendingMembers.clear();

  // This will be allocated by the last member of the aggregate
  return true;
}

static bool CustomAssignInRegList(unsigned ValNo, MVT ValVT, MVT LocVT,
                                  CCValAssign::LocInfo LocInfo, CCState &State,
                                  ArrayRef<MCPhysReg> RegList) {
  unsigned Reg = State.AllocateReg(RegList);
  if (Reg) {
    State.addLoc(CCValAssign::getCustomReg(ValNo, ValVT, Reg, LocVT, LocInfo));
    return true;
  }
  return false;
}

static bool CC_ARM_AAPCS_Custom_f16(unsigned ValNo, MVT ValVT, MVT LocVT,
                                    CCValAssign::LocInfo LocInfo,
                                    ISD::ArgFlagsTy ArgFlags, CCState &State) {
  // f16 arguments are extended to i32 and assigned to a register in [r0, r3]
  return CustomAssignInRegList(ValNo, ValVT, MVT::i32, LocInfo, State,
                               RRegList);
}

static bool CC_ARM_AAPCS_VFP_Custom_f16(unsigned ValNo, MVT ValVT, MVT LocVT,
                                        CCValAssign::LocInfo LocInfo,
                                        ISD::ArgFlagsTy ArgFlags,
                                        CCState &State) {
  // f16 arguments are extended to f32 and assigned to a register in [s0, s15]
  return CustomAssignInRegList(ValNo, ValVT, MVT::f32, LocInfo, State,
                               SRegList);
}

// Include the table generated calling convention implementations.
#include "ARMGenCallingConv.inc"
