//===-- CallingConvLower.cpp - Calling Conventions ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CCState class, used for lowering and implementing
// calling conventions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

CCState::CCState(CallingConv::ID CC, bool IsVarArg, MachineFunction &MF,
                 SmallVectorImpl<CCValAssign> &Locs, LLVMContext &Context,
                 bool NegativeOffsets)
    : CallingConv(CC), IsVarArg(IsVarArg), MF(MF),
      TRI(*MF.getSubtarget().getRegisterInfo()), Locs(Locs), Context(Context),
      NegativeOffsets(NegativeOffsets) {

  // No stack is used.
  StackSize = 0;

  clearByValRegsInfo();
  UsedRegs.resize((TRI.getNumRegs()+31)/32);
}

/// Allocate space on the stack large enough to pass an argument by value.
/// The size and alignment information of the argument is encoded in
/// its parameter attribute.
void CCState::HandleByVal(unsigned ValNo, MVT ValVT, MVT LocVT,
                          CCValAssign::LocInfo LocInfo, int MinSize,
                          Align MinAlign, ISD::ArgFlagsTy ArgFlags) {
  Align Alignment = ArgFlags.getNonZeroByValAlign();
  unsigned Size  = ArgFlags.getByValSize();
  if (MinSize > (int)Size)
    Size = MinSize;
  if (MinAlign > Alignment)
    Alignment = MinAlign;
  ensureMaxAlignment(Alignment);
  MF.getSubtarget().getTargetLowering()->HandleByVal(this, Size, Alignment);
  Size = unsigned(alignTo(Size, MinAlign));
  uint64_t Offset = AllocateStack(Size, Alignment);
  addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
}

/// Mark a register and all of its aliases as allocated.
void CCState::MarkAllocated(MCPhysReg Reg) {
  for (MCRegAliasIterator AI(Reg, &TRI, true); AI.isValid(); ++AI)
    UsedRegs[*AI / 32] |= 1 << (*AI & 31);
}

void CCState::MarkUnallocated(MCPhysReg Reg) {
  for (MCRegAliasIterator AI(Reg, &TRI, true); AI.isValid(); ++AI)
    UsedRegs[*AI / 32] &= ~(1 << (*AI & 31));
}

bool CCState::IsShadowAllocatedReg(MCRegister Reg) const {
  if (!isAllocated(Reg))
    return false;

  for (auto const &ValAssign : Locs)
    if (ValAssign.isRegLoc() && TRI.regsOverlap(ValAssign.getLocReg(), Reg))
      return false;
  return true;
}

/// Analyze an array of argument values,
/// incorporating info about the formals into this state.
void
CCState::AnalyzeFormalArguments(const SmallVectorImpl<ISD::InputArg> &Ins,
                                CCAssignFn Fn) {
  unsigned NumArgs = Ins.size();

  for (unsigned i = 0; i != NumArgs; ++i) {
    MVT ArgVT = Ins[i].VT;
    ISD::ArgFlagsTy ArgFlags = Ins[i].Flags;
    if (Fn(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags, *this))
      report_fatal_error("unable to allocate function argument #" + Twine(i));
  }
}

/// Analyze the return values of a function, returning true if the return can
/// be performed without sret-demotion and false otherwise.
bool CCState::CheckReturn(const SmallVectorImpl<ISD::OutputArg> &Outs,
                          CCAssignFn Fn) {
  // Determine which register each value should be copied into.
  for (unsigned i = 0, e = Outs.size(); i != e; ++i) {
    MVT VT = Outs[i].VT;
    ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
    if (Fn(i, VT, VT, CCValAssign::Full, ArgFlags, *this))
      return false;
  }
  return true;
}

/// Analyze the returned values of a return,
/// incorporating info about the result values into this state.
void CCState::AnalyzeReturn(const SmallVectorImpl<ISD::OutputArg> &Outs,
                            CCAssignFn Fn) {
  // Determine which register each value should be copied into.
  for (unsigned i = 0, e = Outs.size(); i != e; ++i) {
    MVT VT = Outs[i].VT;
    ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
    if (Fn(i, VT, VT, CCValAssign::Full, ArgFlags, *this))
      report_fatal_error("unable to allocate function return #" + Twine(i));
  }
}

/// Analyze the outgoing arguments to a call,
/// incorporating info about the passed values into this state.
void CCState::AnalyzeCallOperands(const SmallVectorImpl<ISD::OutputArg> &Outs,
                                  CCAssignFn Fn) {
  unsigned NumOps = Outs.size();
  for (unsigned i = 0; i != NumOps; ++i) {
    MVT ArgVT = Outs[i].VT;
    ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
    if (Fn(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags, *this)) {
#ifndef NDEBUG
      dbgs() << "Call operand #" << i << " has unhandled type "
             << ArgVT << '\n';
#endif
      llvm_unreachable(nullptr);
    }
  }
}

/// Same as above except it takes vectors of types and argument flags.
void CCState::AnalyzeCallOperands(SmallVectorImpl<MVT> &ArgVTs,
                                  SmallVectorImpl<ISD::ArgFlagsTy> &Flags,
                                  CCAssignFn Fn) {
  unsigned NumOps = ArgVTs.size();
  for (unsigned i = 0; i != NumOps; ++i) {
    MVT ArgVT = ArgVTs[i];
    ISD::ArgFlagsTy ArgFlags = Flags[i];
    if (Fn(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags, *this)) {
#ifndef NDEBUG
      dbgs() << "Call operand #" << i << " has unhandled type "
             << ArgVT << '\n';
#endif
      llvm_unreachable(nullptr);
    }
  }
}

/// Analyze the return values of a call, incorporating info about the passed
/// values into this state.
void CCState::AnalyzeCallResult(const SmallVectorImpl<ISD::InputArg> &Ins,
                                CCAssignFn Fn) {
  for (unsigned i = 0, e = Ins.size(); i != e; ++i) {
    MVT VT = Ins[i].VT;
    ISD::ArgFlagsTy Flags = Ins[i].Flags;
    if (Fn(i, VT, VT, CCValAssign::Full, Flags, *this)) {
#ifndef NDEBUG
      dbgs() << "Call result #" << i << " has unhandled type "
             << VT << '\n';
#endif
      llvm_unreachable(nullptr);
    }
  }
}

/// Same as above except it's specialized for calls that produce a single value.
void CCState::AnalyzeCallResult(MVT VT, CCAssignFn Fn) {
  if (Fn(0, VT, VT, CCValAssign::Full, ISD::ArgFlagsTy(), *this)) {
#ifndef NDEBUG
    dbgs() << "Call result has unhandled type "
           << VT << '\n';
#endif
    llvm_unreachable(nullptr);
  }
}

void CCState::ensureMaxAlignment(Align Alignment) {
  if (!AnalyzingMustTailForwardedRegs)
    MF.getFrameInfo().ensureMaxAlignment(Alignment);
}

static bool isValueTypeInRegForCC(CallingConv::ID CC, MVT VT) {
  if (VT.isVector())
    return true; // Assume -msse-regparm might be in effect.
  if (!VT.isInteger())
    return false;
  return (CC == CallingConv::X86_VectorCall || CC == CallingConv::X86_FastCall);
}

void CCState::getRemainingRegParmsForType(SmallVectorImpl<MCPhysReg> &Regs,
                                          MVT VT, CCAssignFn Fn) {
  uint64_t SavedStackSize = StackSize;
  Align SavedMaxStackArgAlign = MaxStackArgAlign;
  unsigned NumLocs = Locs.size();

  // Set the 'inreg' flag if it is used for this calling convention.
  ISD::ArgFlagsTy Flags;
  if (isValueTypeInRegForCC(CallingConv, VT))
    Flags.setInReg();

  // Allocate something of this value type repeatedly until we get assigned a
  // location in memory.
  bool HaveRegParm;
  do {
    if (Fn(0, VT, VT, CCValAssign::Full, Flags, *this)) {
#ifndef NDEBUG
      dbgs() << "Call has unhandled type " << VT
             << " while computing remaining regparms\n";
#endif
      llvm_unreachable(nullptr);
    }
    HaveRegParm = Locs.back().isRegLoc();
  } while (HaveRegParm);

  // Copy all the registers from the value locations we added.
  assert(NumLocs < Locs.size() && "CC assignment failed to add location");
  for (unsigned I = NumLocs, E = Locs.size(); I != E; ++I)
    if (Locs[I].isRegLoc())
      Regs.push_back(MCPhysReg(Locs[I].getLocReg()));

  // Clear the assigned values and stack memory. We leave the registers marked
  // as allocated so that future queries don't return the same registers, i.e.
  // when i64 and f64 are both passed in GPRs.
  StackSize = SavedStackSize;
  MaxStackArgAlign = SavedMaxStackArgAlign;
  Locs.truncate(NumLocs);
}

void CCState::analyzeMustTailForwardedRegisters(
    SmallVectorImpl<ForwardedRegister> &Forwards, ArrayRef<MVT> RegParmTypes,
    CCAssignFn Fn) {
  // Oftentimes calling conventions will not user register parameters for
  // variadic functions, so we need to assume we're not variadic so that we get
  // all the registers that might be used in a non-variadic call.
  SaveAndRestore SavedVarArg(IsVarArg, false);
  SaveAndRestore SavedMustTail(AnalyzingMustTailForwardedRegs, true);

  for (MVT RegVT : RegParmTypes) {
    SmallVector<MCPhysReg, 8> RemainingRegs;
    getRemainingRegParmsForType(RemainingRegs, RegVT, Fn);
    const TargetLowering *TL = MF.getSubtarget().getTargetLowering();
    const TargetRegisterClass *RC = TL->getRegClassFor(RegVT);
    for (MCPhysReg PReg : RemainingRegs) {
      Register VReg = MF.addLiveIn(PReg, RC);
      Forwards.push_back(ForwardedRegister(VReg, PReg, RegVT));
    }
  }
}

bool CCState::resultsCompatible(CallingConv::ID CalleeCC,
                                CallingConv::ID CallerCC, MachineFunction &MF,
                                LLVMContext &C,
                                const SmallVectorImpl<ISD::InputArg> &Ins,
                                CCAssignFn CalleeFn, CCAssignFn CallerFn) {
  if (CalleeCC == CallerCC)
    return true;
  SmallVector<CCValAssign, 4> RVLocs1;
  CCState CCInfo1(CalleeCC, false, MF, RVLocs1, C);
  CCInfo1.AnalyzeCallResult(Ins, CalleeFn);

  SmallVector<CCValAssign, 4> RVLocs2;
  CCState CCInfo2(CallerCC, false, MF, RVLocs2, C);
  CCInfo2.AnalyzeCallResult(Ins, CallerFn);

  auto AreCompatible = [](const CCValAssign &Loc1, const CCValAssign &Loc2) {
    assert(!Loc1.isPendingLoc() && !Loc2.isPendingLoc() &&
           "The location must have been decided by now");
    // Must fill the same part of their locations.
    if (Loc1.getLocInfo() != Loc2.getLocInfo())
      return false;
    // Must both be in the same registers, or both in memory at the same offset.
    if (Loc1.isRegLoc() && Loc2.isRegLoc())
      return Loc1.getLocReg() == Loc2.getLocReg();
    if (Loc1.isMemLoc() && Loc2.isMemLoc())
      return Loc1.getLocMemOffset() == Loc2.getLocMemOffset();
    llvm_unreachable("Unknown location kind");
  };

  return std::equal(RVLocs1.begin(), RVLocs1.end(), RVLocs2.begin(),
                    RVLocs2.end(), AreCompatible);
}
