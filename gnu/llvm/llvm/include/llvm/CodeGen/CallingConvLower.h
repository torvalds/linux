//===- llvm/CallingConvLower.h - Calling Conventions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the CCState and CCValAssign classes, used for lowering
// and implementing calling conventions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CALLINGCONVLOWER_H
#define LLVM_CODEGEN_CALLINGCONVLOWER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Support/Alignment.h"
#include <variant>

namespace llvm {

class CCState;
class MachineFunction;
class MVT;
class TargetRegisterInfo;

/// CCValAssign - Represent assignment of one arg/retval to a location.
class CCValAssign {
public:
  enum LocInfo {
    Full,      // The value fills the full location.
    SExt,      // The value is sign extended in the location.
    ZExt,      // The value is zero extended in the location.
    AExt,      // The value is extended with undefined upper bits.
    SExtUpper, // The value is in the upper bits of the location and should be
               // sign extended when retrieved.
    ZExtUpper, // The value is in the upper bits of the location and should be
               // zero extended when retrieved.
    AExtUpper, // The value is in the upper bits of the location and should be
               // extended with undefined upper bits when retrieved.
    BCvt,      // The value is bit-converted in the location.
    Trunc,     // The value is truncated in the location.
    VExt,      // The value is vector-widened in the location.
               // FIXME: Not implemented yet. Code that uses AExt to mean
               // vector-widen should be fixed to use VExt instead.
    FPExt,     // The floating-point value is fp-extended in the location.
    Indirect   // The location contains pointer to the value.
    // TODO: a subset of the value is in the location.
  };

private:
  // Holds one of:
  // - the register that the value is assigned to;
  // - the memory offset at which the value resides;
  // - additional information about pending location; the exact interpretation
  //   of the data is target-dependent.
  std::variant<Register, int64_t, unsigned> Data;

  /// ValNo - This is the value number being assigned (e.g. an argument number).
  unsigned ValNo;

  /// isCustom - True if this arg/retval requires special handling.
  unsigned isCustom : 1;

  /// Information about how the value is assigned.
  LocInfo HTP : 6;

  /// ValVT - The type of the value being assigned.
  MVT ValVT;

  /// LocVT - The type of the location being assigned to.
  MVT LocVT;

  CCValAssign(LocInfo HTP, unsigned ValNo, MVT ValVT, MVT LocVT, bool IsCustom)
      : ValNo(ValNo), isCustom(IsCustom), HTP(HTP), ValVT(ValVT), LocVT(LocVT) {
  }

public:
  static CCValAssign getReg(unsigned ValNo, MVT ValVT, unsigned RegNo,
                            MVT LocVT, LocInfo HTP, bool IsCustom = false) {
    CCValAssign Ret(HTP, ValNo, ValVT, LocVT, IsCustom);
    Ret.Data = Register(RegNo);
    return Ret;
  }

  static CCValAssign getCustomReg(unsigned ValNo, MVT ValVT, unsigned RegNo,
                                  MVT LocVT, LocInfo HTP) {
    return getReg(ValNo, ValVT, RegNo, LocVT, HTP, /*IsCustom=*/true);
  }

  static CCValAssign getMem(unsigned ValNo, MVT ValVT, int64_t Offset,
                            MVT LocVT, LocInfo HTP, bool IsCustom = false) {
    CCValAssign Ret(HTP, ValNo, ValVT, LocVT, IsCustom);
    Ret.Data = Offset;
    return Ret;
  }

  static CCValAssign getCustomMem(unsigned ValNo, MVT ValVT, int64_t Offset,
                                  MVT LocVT, LocInfo HTP) {
    return getMem(ValNo, ValVT, Offset, LocVT, HTP, /*IsCustom=*/true);
  }

  static CCValAssign getPending(unsigned ValNo, MVT ValVT, MVT LocVT,
                                LocInfo HTP, unsigned ExtraInfo = 0) {
    CCValAssign Ret(HTP, ValNo, ValVT, LocVT, false);
    Ret.Data = ExtraInfo;
    return Ret;
  }

  void convertToReg(unsigned RegNo) { Data = Register(RegNo); }

  void convertToMem(int64_t Offset) { Data = Offset; }

  unsigned getValNo() const { return ValNo; }
  MVT getValVT() const { return ValVT; }

  bool isRegLoc() const { return std::holds_alternative<Register>(Data); }
  bool isMemLoc() const { return std::holds_alternative<int64_t>(Data); }
  bool isPendingLoc() const { return std::holds_alternative<unsigned>(Data); }

  bool needsCustom() const { return isCustom; }

  Register getLocReg() const { return std::get<Register>(Data); }
  int64_t getLocMemOffset() const { return std::get<int64_t>(Data); }
  unsigned getExtraInfo() const { return std::get<unsigned>(Data); }

  MVT getLocVT() const { return LocVT; }

  LocInfo getLocInfo() const { return HTP; }
  bool isExtInLoc() const {
    return (HTP == AExt || HTP == SExt || HTP == ZExt);
  }

  bool isUpperBitsInLoc() const {
    return HTP == AExtUpper || HTP == SExtUpper || HTP == ZExtUpper;
  }
};

/// Describes a register that needs to be forwarded from the prologue to a
/// musttail call.
struct ForwardedRegister {
  ForwardedRegister(Register VReg, MCPhysReg PReg, MVT VT)
      : VReg(VReg), PReg(PReg), VT(VT) {}
  Register VReg;
  MCPhysReg PReg;
  MVT VT;
};

/// CCAssignFn - This function assigns a location for Val, updating State to
/// reflect the change.  It returns 'true' if it failed to handle Val.
typedef bool CCAssignFn(unsigned ValNo, MVT ValVT,
                        MVT LocVT, CCValAssign::LocInfo LocInfo,
                        ISD::ArgFlagsTy ArgFlags, CCState &State);

/// CCCustomFn - This function assigns a location for Val, possibly updating
/// all args to reflect changes and indicates if it handled it. It must set
/// isCustom if it handles the arg and returns true.
typedef bool CCCustomFn(unsigned &ValNo, MVT &ValVT,
                        MVT &LocVT, CCValAssign::LocInfo &LocInfo,
                        ISD::ArgFlagsTy &ArgFlags, CCState &State);

/// CCState - This class holds information needed while lowering arguments and
/// return values.  It captures which registers are already assigned and which
/// stack slots are used.  It provides accessors to allocate these values.
class CCState {
private:
  CallingConv::ID CallingConv;
  bool IsVarArg;
  bool AnalyzingMustTailForwardedRegs = false;
  MachineFunction &MF;
  const TargetRegisterInfo &TRI;
  SmallVectorImpl<CCValAssign> &Locs;
  LLVMContext &Context;
  // True if arguments should be allocated at negative offsets.
  bool NegativeOffsets;

  uint64_t StackSize;
  Align MaxStackArgAlign;
  SmallVector<uint32_t, 16> UsedRegs;
  SmallVector<CCValAssign, 4> PendingLocs;
  SmallVector<ISD::ArgFlagsTy, 4> PendingArgFlags;

  // ByValInfo and SmallVector<ByValInfo, 4> ByValRegs:
  //
  // Vector of ByValInfo instances (ByValRegs) is introduced for byval registers
  // tracking.
  // Or, in another words it tracks byval parameters that are stored in
  // general purpose registers.
  //
  // For 4 byte stack alignment,
  // instance index means byval parameter number in formal
  // arguments set. Assume, we have some "struct_type" with size = 4 bytes,
  // then, for function "foo":
  //
  // i32 foo(i32 %p, %struct_type* %r, i32 %s, %struct_type* %t)
  //
  // ByValRegs[0] describes how "%r" is stored (Begin == r1, End == r2)
  // ByValRegs[1] describes how "%t" is stored (Begin == r3, End == r4).
  //
  // In case of 8 bytes stack alignment,
  // In function shown above, r3 would be wasted according to AAPCS rules.
  // ByValRegs vector size still would be 2,
  // while "%t" goes to the stack: it wouldn't be described in ByValRegs.
  //
  // Supposed use-case for this collection:
  // 1. Initially ByValRegs is empty, InRegsParamsProcessed is 0.
  // 2. HandleByVal fills up ByValRegs.
  // 3. Argument analysis (LowerFormatArguments, for example). After
  // some byval argument was analyzed, InRegsParamsProcessed is increased.
  struct ByValInfo {
    ByValInfo(unsigned B, unsigned E) : Begin(B), End(E) {}

    // First register allocated for current parameter.
    unsigned Begin;

    // First after last register allocated for current parameter.
    unsigned End;
  };
  SmallVector<ByValInfo, 4 > ByValRegs;

  // InRegsParamsProcessed - shows how many instances of ByValRegs was proceed
  // during argument analysis.
  unsigned InRegsParamsProcessed;

public:
  CCState(CallingConv::ID CC, bool IsVarArg, MachineFunction &MF,
          SmallVectorImpl<CCValAssign> &Locs, LLVMContext &Context,
          bool NegativeOffsets = false);

  void addLoc(const CCValAssign &V) {
    Locs.push_back(V);
  }

  LLVMContext &getContext() const { return Context; }
  MachineFunction &getMachineFunction() const { return MF; }
  CallingConv::ID getCallingConv() const { return CallingConv; }
  bool isVarArg() const { return IsVarArg; }

  /// Returns the size of the currently allocated portion of the stack.
  uint64_t getStackSize() const { return StackSize; }

  /// getAlignedCallFrameSize - Return the size of the call frame needed to
  /// be able to store all arguments and such that the alignment requirement
  /// of each of the arguments is satisfied.
  uint64_t getAlignedCallFrameSize() const {
    return alignTo(StackSize, MaxStackArgAlign);
  }

  /// isAllocated - Return true if the specified register (or an alias) is
  /// allocated.
  bool isAllocated(MCRegister Reg) const {
    return UsedRegs[Reg / 32] & (1 << (Reg & 31));
  }

  /// AnalyzeFormalArguments - Analyze an array of argument values,
  /// incorporating info about the formals into this state.
  void AnalyzeFormalArguments(const SmallVectorImpl<ISD::InputArg> &Ins,
                              CCAssignFn Fn);

  /// The function will invoke AnalyzeFormalArguments.
  void AnalyzeArguments(const SmallVectorImpl<ISD::InputArg> &Ins,
                        CCAssignFn Fn) {
    AnalyzeFormalArguments(Ins, Fn);
  }

  /// AnalyzeReturn - Analyze the returned values of a return,
  /// incorporating info about the result values into this state.
  void AnalyzeReturn(const SmallVectorImpl<ISD::OutputArg> &Outs,
                     CCAssignFn Fn);

  /// CheckReturn - Analyze the return values of a function, returning
  /// true if the return can be performed without sret-demotion, and
  /// false otherwise.
  bool CheckReturn(const SmallVectorImpl<ISD::OutputArg> &Outs,
                   CCAssignFn Fn);

  /// AnalyzeCallOperands - Analyze the outgoing arguments to a call,
  /// incorporating info about the passed values into this state.
  void AnalyzeCallOperands(const SmallVectorImpl<ISD::OutputArg> &Outs,
                           CCAssignFn Fn);

  /// AnalyzeCallOperands - Same as above except it takes vectors of types
  /// and argument flags.
  void AnalyzeCallOperands(SmallVectorImpl<MVT> &ArgVTs,
                           SmallVectorImpl<ISD::ArgFlagsTy> &Flags,
                           CCAssignFn Fn);

  /// The function will invoke AnalyzeCallOperands.
  void AnalyzeArguments(const SmallVectorImpl<ISD::OutputArg> &Outs,
                        CCAssignFn Fn) {
    AnalyzeCallOperands(Outs, Fn);
  }

  /// AnalyzeCallResult - Analyze the return values of a call,
  /// incorporating info about the passed values into this state.
  void AnalyzeCallResult(const SmallVectorImpl<ISD::InputArg> &Ins,
                         CCAssignFn Fn);

  /// A shadow allocated register is a register that was allocated
  /// but wasn't added to the location list (Locs).
  /// \returns true if the register was allocated as shadow or false otherwise.
  bool IsShadowAllocatedReg(MCRegister Reg) const;

  /// AnalyzeCallResult - Same as above except it's specialized for calls which
  /// produce a single value.
  void AnalyzeCallResult(MVT VT, CCAssignFn Fn);

  /// getFirstUnallocated - Return the index of the first unallocated register
  /// in the set, or Regs.size() if they are all allocated.
  unsigned getFirstUnallocated(ArrayRef<MCPhysReg> Regs) const {
    for (unsigned i = 0; i < Regs.size(); ++i)
      if (!isAllocated(Regs[i]))
        return i;
    return Regs.size();
  }

  void DeallocateReg(MCPhysReg Reg) {
    assert(isAllocated(Reg) && "Trying to deallocate an unallocated register");
    MarkUnallocated(Reg);
  }

  /// AllocateReg - Attempt to allocate one register.  If it is not available,
  /// return zero.  Otherwise, return the register, marking it and any aliases
  /// as allocated.
  MCRegister AllocateReg(MCPhysReg Reg) {
    if (isAllocated(Reg))
      return MCRegister();
    MarkAllocated(Reg);
    return Reg;
  }

  /// Version of AllocateReg with extra register to be shadowed.
  MCRegister AllocateReg(MCPhysReg Reg, MCPhysReg ShadowReg) {
    if (isAllocated(Reg))
      return MCRegister();
    MarkAllocated(Reg);
    MarkAllocated(ShadowReg);
    return Reg;
  }

  /// AllocateReg - Attempt to allocate one of the specified registers.  If none
  /// are available, return zero.  Otherwise, return the first one available,
  /// marking it and any aliases as allocated.
  MCPhysReg AllocateReg(ArrayRef<MCPhysReg> Regs) {
    unsigned FirstUnalloc = getFirstUnallocated(Regs);
    if (FirstUnalloc == Regs.size())
      return MCRegister();    // Didn't find the reg.

    // Mark the register and any aliases as allocated.
    MCPhysReg Reg = Regs[FirstUnalloc];
    MarkAllocated(Reg);
    return Reg;
  }

  /// AllocateRegBlock - Attempt to allocate a block of RegsRequired consecutive
  /// registers. If this is not possible, return zero. Otherwise, return the first
  /// register of the block that were allocated, marking the entire block as allocated.
  MCPhysReg AllocateRegBlock(ArrayRef<MCPhysReg> Regs, unsigned RegsRequired) {
    if (RegsRequired > Regs.size())
      return 0;

    for (unsigned StartIdx = 0; StartIdx <= Regs.size() - RegsRequired;
         ++StartIdx) {
      bool BlockAvailable = true;
      // Check for already-allocated regs in this block
      for (unsigned BlockIdx = 0; BlockIdx < RegsRequired; ++BlockIdx) {
        if (isAllocated(Regs[StartIdx + BlockIdx])) {
          BlockAvailable = false;
          break;
        }
      }
      if (BlockAvailable) {
        // Mark the entire block as allocated
        for (unsigned BlockIdx = 0; BlockIdx < RegsRequired; ++BlockIdx) {
          MarkAllocated(Regs[StartIdx + BlockIdx]);
        }
        return Regs[StartIdx];
      }
    }
    // No block was available
    return 0;
  }

  /// Version of AllocateReg with list of registers to be shadowed.
  MCRegister AllocateReg(ArrayRef<MCPhysReg> Regs, const MCPhysReg *ShadowRegs) {
    unsigned FirstUnalloc = getFirstUnallocated(Regs);
    if (FirstUnalloc == Regs.size())
      return MCRegister();    // Didn't find the reg.

    // Mark the register and any aliases as allocated.
    MCRegister Reg = Regs[FirstUnalloc], ShadowReg = ShadowRegs[FirstUnalloc];
    MarkAllocated(Reg);
    MarkAllocated(ShadowReg);
    return Reg;
  }

  /// AllocateStack - Allocate a chunk of stack space with the specified size
  /// and alignment.
  int64_t AllocateStack(unsigned Size, Align Alignment) {
    int64_t Offset;
    if (NegativeOffsets) {
      StackSize = alignTo(StackSize + Size, Alignment);
      Offset = -StackSize;
    } else {
      Offset = alignTo(StackSize, Alignment);
      StackSize = Offset + Size;
    }
    MaxStackArgAlign = std::max(Alignment, MaxStackArgAlign);
    ensureMaxAlignment(Alignment);
    return Offset;
  }

  void ensureMaxAlignment(Align Alignment);

  /// Version of AllocateStack with list of extra registers to be shadowed.
  /// Note that, unlike AllocateReg, this shadows ALL of the shadow registers.
  int64_t AllocateStack(unsigned Size, Align Alignment,
                        ArrayRef<MCPhysReg> ShadowRegs) {
    for (MCPhysReg Reg : ShadowRegs)
      MarkAllocated(Reg);
    return AllocateStack(Size, Alignment);
  }

  // HandleByVal - Allocate a stack slot large enough to pass an argument by
  // value. The size and alignment information of the argument is encoded in its
  // parameter attribute.
  void HandleByVal(unsigned ValNo, MVT ValVT, MVT LocVT,
                   CCValAssign::LocInfo LocInfo, int MinSize, Align MinAlign,
                   ISD::ArgFlagsTy ArgFlags);

  // Returns count of byval arguments that are to be stored (even partly)
  // in registers.
  unsigned getInRegsParamsCount() const { return ByValRegs.size(); }

  // Returns count of byval in-regs arguments processed.
  unsigned getInRegsParamsProcessed() const { return InRegsParamsProcessed; }

  // Get information about N-th byval parameter that is stored in registers.
  // Here "ByValParamIndex" is N.
  void getInRegsParamInfo(unsigned InRegsParamRecordIndex,
                          unsigned& BeginReg, unsigned& EndReg) const {
    assert(InRegsParamRecordIndex < ByValRegs.size() &&
           "Wrong ByVal parameter index");

    const ByValInfo& info = ByValRegs[InRegsParamRecordIndex];
    BeginReg = info.Begin;
    EndReg = info.End;
  }

  // Add information about parameter that is kept in registers.
  void addInRegsParamInfo(unsigned RegBegin, unsigned RegEnd) {
    ByValRegs.push_back(ByValInfo(RegBegin, RegEnd));
  }

  // Goes either to next byval parameter (excluding "waste" record), or
  // to the end of collection.
  // Returns false, if end is reached.
  bool nextInRegsParam() {
    unsigned e = ByValRegs.size();
    if (InRegsParamsProcessed < e)
      ++InRegsParamsProcessed;
    return InRegsParamsProcessed < e;
  }

  // Clear byval registers tracking info.
  void clearByValRegsInfo() {
    InRegsParamsProcessed = 0;
    ByValRegs.clear();
  }

  // Rewind byval registers tracking info.
  void rewindByValRegsInfo() {
    InRegsParamsProcessed = 0;
  }

  // Get list of pending assignments
  SmallVectorImpl<CCValAssign> &getPendingLocs() {
    return PendingLocs;
  }

  // Get a list of argflags for pending assignments.
  SmallVectorImpl<ISD::ArgFlagsTy> &getPendingArgFlags() {
    return PendingArgFlags;
  }

  /// Compute the remaining unused register parameters that would be used for
  /// the given value type. This is useful when varargs are passed in the
  /// registers that normal prototyped parameters would be passed in, or for
  /// implementing perfect forwarding.
  void getRemainingRegParmsForType(SmallVectorImpl<MCPhysReg> &Regs, MVT VT,
                                   CCAssignFn Fn);

  /// Compute the set of registers that need to be preserved and forwarded to
  /// any musttail calls.
  void analyzeMustTailForwardedRegisters(
      SmallVectorImpl<ForwardedRegister> &Forwards, ArrayRef<MVT> RegParmTypes,
      CCAssignFn Fn);

  /// Returns true if the results of the two calling conventions are compatible.
  /// This is usually part of the check for tailcall eligibility.
  static bool resultsCompatible(CallingConv::ID CalleeCC,
                                CallingConv::ID CallerCC, MachineFunction &MF,
                                LLVMContext &C,
                                const SmallVectorImpl<ISD::InputArg> &Ins,
                                CCAssignFn CalleeFn, CCAssignFn CallerFn);

  /// The function runs an additional analysis pass over function arguments.
  /// It will mark each argument with the attribute flag SecArgPass.
  /// After running, it will sort the locs list.
  template <class T>
  void AnalyzeArgumentsSecondPass(const SmallVectorImpl<T> &Args,
                                  CCAssignFn Fn) {
    unsigned NumFirstPassLocs = Locs.size();

    /// Creates similar argument list to \p Args in which each argument is
    /// marked using SecArgPass flag.
    SmallVector<T, 16> SecPassArg;
    // SmallVector<ISD::InputArg, 16> SecPassArg;
    for (auto Arg : Args) {
      Arg.Flags.setSecArgPass();
      SecPassArg.push_back(Arg);
    }

    // Run the second argument pass
    AnalyzeArguments(SecPassArg, Fn);

    // Sort the locations of the arguments according to their original position.
    SmallVector<CCValAssign, 16> TmpArgLocs;
    TmpArgLocs.swap(Locs);
    auto B = TmpArgLocs.begin(), E = TmpArgLocs.end();
    std::merge(B, B + NumFirstPassLocs, B + NumFirstPassLocs, E,
               std::back_inserter(Locs),
               [](const CCValAssign &A, const CCValAssign &B) -> bool {
                 return A.getValNo() < B.getValNo();
               });
  }

private:
  /// MarkAllocated - Mark a register and all of its aliases as allocated.
  void MarkAllocated(MCPhysReg Reg);

  void MarkUnallocated(MCPhysReg Reg);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_CALLINGCONVLOWER_H
