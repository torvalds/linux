//===- llvm/CodeGen/GlobalISel/CallLowering.h - Call lowering ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes how to lower LLVM calls to machine code calls.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_CALLLOWERING_H
#define LLVM_CODEGEN_GLOBALISEL_CALLLOWERING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>
#include <functional>

namespace llvm {

class AttributeList;
class CallBase;
class DataLayout;
class Function;
class FunctionLoweringInfo;
class MachineIRBuilder;
class MachineFunction;
struct MachinePointerInfo;
class MachineRegisterInfo;
class TargetLowering;

class CallLowering {
  const TargetLowering *TLI;

  virtual void anchor();
public:
  struct BaseArgInfo {
    Type *Ty;
    SmallVector<ISD::ArgFlagsTy, 4> Flags;
    bool IsFixed;

    BaseArgInfo(Type *Ty,
                ArrayRef<ISD::ArgFlagsTy> Flags = ArrayRef<ISD::ArgFlagsTy>(),
                bool IsFixed = true)
        : Ty(Ty), Flags(Flags.begin(), Flags.end()), IsFixed(IsFixed) {}

    BaseArgInfo() : Ty(nullptr), IsFixed(false) {}
  };

  struct ArgInfo : public BaseArgInfo {
    SmallVector<Register, 4> Regs;
    // If the argument had to be split into multiple parts according to the
    // target calling convention, then this contains the original vregs
    // if the argument was an incoming arg.
    SmallVector<Register, 2> OrigRegs;

    /// Optionally track the original IR value for the argument. This may not be
    /// meaningful in all contexts. This should only be used on for forwarding
    /// through to use for aliasing information in MachinePointerInfo for memory
    /// arguments.
    const Value *OrigValue = nullptr;

    /// Index original Function's argument.
    unsigned OrigArgIndex;

    /// Sentinel value for implicit machine-level input arguments.
    static const unsigned NoArgIndex = UINT_MAX;

    ArgInfo(ArrayRef<Register> Regs, Type *Ty, unsigned OrigIndex,
            ArrayRef<ISD::ArgFlagsTy> Flags = ArrayRef<ISD::ArgFlagsTy>(),
            bool IsFixed = true, const Value *OrigValue = nullptr)
        : BaseArgInfo(Ty, Flags, IsFixed), Regs(Regs.begin(), Regs.end()),
          OrigValue(OrigValue), OrigArgIndex(OrigIndex) {
      if (!Regs.empty() && Flags.empty())
        this->Flags.push_back(ISD::ArgFlagsTy());
      // FIXME: We should have just one way of saying "no register".
      assert(((Ty->isVoidTy() || Ty->isEmptyTy()) ==
              (Regs.empty() || Regs[0] == 0)) &&
             "only void types should have no register");
    }

    ArgInfo(ArrayRef<Register> Regs, const Value &OrigValue, unsigned OrigIndex,
            ArrayRef<ISD::ArgFlagsTy> Flags = ArrayRef<ISD::ArgFlagsTy>(),
            bool IsFixed = true)
      : ArgInfo(Regs, OrigValue.getType(), OrigIndex, Flags, IsFixed, &OrigValue) {}

    ArgInfo() = default;
  };

  struct PtrAuthInfo {
    uint64_t Key;
    Register Discriminator;
  };

  struct CallLoweringInfo {
    /// Calling convention to be used for the call.
    CallingConv::ID CallConv = CallingConv::C;

    /// Destination of the call. It should be either a register, globaladdress,
    /// or externalsymbol.
    MachineOperand Callee = MachineOperand::CreateImm(0);

    /// Descriptor for the return type of the function.
    ArgInfo OrigRet;

    /// List of descriptors of the arguments passed to the function.
    SmallVector<ArgInfo, 32> OrigArgs;

    /// Valid if the call has a swifterror inout parameter, and contains the
    /// vreg that the swifterror should be copied into after the call.
    Register SwiftErrorVReg;

    /// Valid if the call is a controlled convergent operation.
    Register ConvergenceCtrlToken;

    /// Original IR callsite corresponding to this call, if available.
    const CallBase *CB = nullptr;

    MDNode *KnownCallees = nullptr;

    /// The auth-call information in the "ptrauth" bundle, if present.
    std::optional<PtrAuthInfo> PAI;

    /// True if the call must be tail call optimized.
    bool IsMustTailCall = false;

    /// True if the call passes all target-independent checks for tail call
    /// optimization.
    bool IsTailCall = false;

    /// True if the call was lowered as a tail call. This is consumed by the
    /// legalizer. This allows the legalizer to lower libcalls as tail calls.
    bool LoweredTailCall = false;

    /// True if the call is to a vararg function.
    bool IsVarArg = false;

    /// True if the function's return value can be lowered to registers.
    bool CanLowerReturn = true;

    /// VReg to hold the hidden sret parameter.
    Register DemoteRegister;

    /// The stack index for sret demotion.
    int DemoteStackIndex;

    /// Expected type identifier for indirect calls with a CFI check.
    const ConstantInt *CFIType = nullptr;

    /// True if this call results in convergent operations.
    bool IsConvergent = true;
  };

  /// Argument handling is mostly uniform between the four places that
  /// make these decisions: function formal arguments, call
  /// instruction args, call instruction returns and function
  /// returns. However, once a decision has been made on where an
  /// argument should go, exactly what happens can vary slightly. This
  /// class abstracts the differences.
  ///
  /// ValueAssigner should not depend on any specific function state, and
  /// only determine the types and locations for arguments.
  struct ValueAssigner {
    ValueAssigner(bool IsIncoming, CCAssignFn *AssignFn_,
                  CCAssignFn *AssignFnVarArg_ = nullptr)
        : AssignFn(AssignFn_), AssignFnVarArg(AssignFnVarArg_),
          IsIncomingArgumentHandler(IsIncoming) {

      // Some targets change the handler depending on whether the call is
      // varargs or not. If
      if (!AssignFnVarArg)
        AssignFnVarArg = AssignFn;
    }

    virtual ~ValueAssigner() = default;

    /// Returns true if the handler is dealing with incoming arguments,
    /// i.e. those that move values from some physical location to vregs.
    bool isIncomingArgumentHandler() const {
      return IsIncomingArgumentHandler;
    }

    /// Wrap call to (typically tablegenerated CCAssignFn). This may be
    /// overridden to track additional state information as arguments are
    /// assigned or apply target specific hacks around the legacy
    /// infrastructure.
    virtual bool assignArg(unsigned ValNo, EVT OrigVT, MVT ValVT, MVT LocVT,
                           CCValAssign::LocInfo LocInfo, const ArgInfo &Info,
                           ISD::ArgFlagsTy Flags, CCState &State) {
      if (getAssignFn(State.isVarArg())(ValNo, ValVT, LocVT, LocInfo, Flags,
                                        State))
        return true;
      StackSize = State.getStackSize();
      return false;
    }

    /// Assignment function to use for a general call.
    CCAssignFn *AssignFn;

    /// Assignment function to use for a variadic call. This is usually the same
    /// as AssignFn on most targets.
    CCAssignFn *AssignFnVarArg;

    /// The size of the currently allocated portion of the stack.
    uint64_t StackSize = 0;

    /// Select the appropriate assignment function depending on whether this is
    /// a variadic call.
    CCAssignFn *getAssignFn(bool IsVarArg) const {
      return IsVarArg ? AssignFnVarArg : AssignFn;
    }

  private:
    const bool IsIncomingArgumentHandler;
    virtual void anchor();
  };

  struct IncomingValueAssigner : public ValueAssigner {
    IncomingValueAssigner(CCAssignFn *AssignFn_,
                          CCAssignFn *AssignFnVarArg_ = nullptr)
        : ValueAssigner(true, AssignFn_, AssignFnVarArg_) {}
  };

  struct OutgoingValueAssigner : public ValueAssigner {
    OutgoingValueAssigner(CCAssignFn *AssignFn_,
                          CCAssignFn *AssignFnVarArg_ = nullptr)
        : ValueAssigner(false, AssignFn_, AssignFnVarArg_) {}
  };

  struct ValueHandler {
    MachineIRBuilder &MIRBuilder;
    MachineRegisterInfo &MRI;
    const bool IsIncomingArgumentHandler;

    ValueHandler(bool IsIncoming, MachineIRBuilder &MIRBuilder,
                 MachineRegisterInfo &MRI)
        : MIRBuilder(MIRBuilder), MRI(MRI),
          IsIncomingArgumentHandler(IsIncoming) {}

    virtual ~ValueHandler() = default;

    /// Returns true if the handler is dealing with incoming arguments,
    /// i.e. those that move values from some physical location to vregs.
    bool isIncomingArgumentHandler() const {
      return IsIncomingArgumentHandler;
    }

    /// Materialize a VReg containing the address of the specified
    /// stack-based object. This is either based on a FrameIndex or
    /// direct SP manipulation, depending on the context. \p MPO
    /// should be initialized to an appropriate description of the
    /// address created.
    virtual Register getStackAddress(uint64_t MemSize, int64_t Offset,
                                     MachinePointerInfo &MPO,
                                     ISD::ArgFlagsTy Flags) = 0;

    /// Return the in-memory size to write for the argument at \p VA. This may
    /// be smaller than the allocated stack slot size.
    ///
    /// This is overridable primarily for targets to maintain compatibility with
    /// hacks around the existing DAG call lowering infrastructure.
    virtual LLT getStackValueStoreType(const DataLayout &DL,
                                       const CCValAssign &VA,
                                       ISD::ArgFlagsTy Flags) const;

    /// The specified value has been assigned to a physical register,
    /// handle the appropriate COPY (either to or from) and mark any
    /// relevant uses/defines as needed.
    virtual void assignValueToReg(Register ValVReg, Register PhysReg,
                                  const CCValAssign &VA) = 0;

    /// The specified value has been assigned to a stack
    /// location. Load or store it there, with appropriate extension
    /// if necessary.
    virtual void assignValueToAddress(Register ValVReg, Register Addr,
                                      LLT MemTy, const MachinePointerInfo &MPO,
                                      const CCValAssign &VA) = 0;

    /// An overload which takes an ArgInfo if additional information about the
    /// arg is needed. \p ValRegIndex is the index in \p Arg.Regs for the value
    /// to store.
    virtual void assignValueToAddress(const ArgInfo &Arg, unsigned ValRegIndex,
                                      Register Addr, LLT MemTy,
                                      const MachinePointerInfo &MPO,
                                      const CCValAssign &VA) {
      assignValueToAddress(Arg.Regs[ValRegIndex], Addr, MemTy, MPO, VA);
    }

    /// Handle custom values, which may be passed into one or more of \p VAs.
    /// \p If the handler wants the assignments to be delayed until after
    /// mem loc assignments, then it sets \p Thunk to the thunk to do the
    /// assignment.
    /// \return The number of \p VAs that have been assigned including the
    ///         first one, and which should therefore be skipped from further
    ///         processing.
    virtual unsigned assignCustomValue(ArgInfo &Arg, ArrayRef<CCValAssign> VAs,
                                       std::function<void()> *Thunk = nullptr) {
      // This is not a pure virtual method because not all targets need to worry
      // about custom values.
      llvm_unreachable("Custom values not supported");
    }

    /// Do a memory copy of \p MemSize bytes from \p SrcPtr to \p DstPtr. This
    /// is necessary for outgoing stack-passed byval arguments.
    void
    copyArgumentMemory(const ArgInfo &Arg, Register DstPtr, Register SrcPtr,
                       const MachinePointerInfo &DstPtrInfo, Align DstAlign,
                       const MachinePointerInfo &SrcPtrInfo, Align SrcAlign,
                       uint64_t MemSize, CCValAssign &VA) const;

    /// Extend a register to the location type given in VA, capped at extending
    /// to at most MaxSize bits. If MaxSizeBits is 0 then no maximum is set.
    Register extendRegister(Register ValReg, const CCValAssign &VA,
                            unsigned MaxSizeBits = 0);
  };

  /// Base class for ValueHandlers used for arguments coming into the current
  /// function, or for return values received from a call.
  struct IncomingValueHandler : public ValueHandler {
    IncomingValueHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
        : ValueHandler(/*IsIncoming*/ true, MIRBuilder, MRI) {}

    /// Insert G_ASSERT_ZEXT/G_ASSERT_SEXT or other hint instruction based on \p
    /// VA, returning the new register if a hint was inserted.
    Register buildExtensionHint(const CCValAssign &VA, Register SrcReg,
                                LLT NarrowTy);

    /// Provides a default implementation for argument handling.
    void assignValueToReg(Register ValVReg, Register PhysReg,
                          const CCValAssign &VA) override;
  };

  /// Base class for ValueHandlers used for arguments passed to a function call,
  /// or for return values.
  struct OutgoingValueHandler : public ValueHandler {
    OutgoingValueHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
        : ValueHandler(/*IsIncoming*/ false, MIRBuilder, MRI) {}
  };

protected:
  /// Getter for generic TargetLowering class.
  const TargetLowering *getTLI() const {
    return TLI;
  }

  /// Getter for target specific TargetLowering class.
  template <class XXXTargetLowering>
    const XXXTargetLowering *getTLI() const {
    return static_cast<const XXXTargetLowering *>(TLI);
  }

  /// \returns Flags corresponding to the attributes on the \p ArgIdx-th
  /// parameter of \p Call.
  ISD::ArgFlagsTy getAttributesForArgIdx(const CallBase &Call,
                                         unsigned ArgIdx) const;

  /// \returns Flags corresponding to the attributes on the return from \p Call.
  ISD::ArgFlagsTy getAttributesForReturn(const CallBase &Call) const;

  /// Adds flags to \p Flags based off of the attributes in \p Attrs.
  /// \p OpIdx is the index in \p Attrs to add flags from.
  void addArgFlagsFromAttributes(ISD::ArgFlagsTy &Flags,
                                 const AttributeList &Attrs,
                                 unsigned OpIdx) const;

  template <typename FuncInfoTy>
  void setArgFlags(ArgInfo &Arg, unsigned OpIdx, const DataLayout &DL,
                   const FuncInfoTy &FuncInfo) const;

  /// Break \p OrigArgInfo into one or more pieces the calling convention can
  /// process, returned in \p SplitArgs. For example, this should break structs
  /// down into individual fields.
  ///
  /// If \p Offsets is non-null, it points to a vector to be filled in
  /// with the in-memory offsets of each of the individual values.
  void splitToValueTypes(const ArgInfo &OrigArgInfo,
                         SmallVectorImpl<ArgInfo> &SplitArgs,
                         const DataLayout &DL, CallingConv::ID CallConv,
                         SmallVectorImpl<uint64_t> *Offsets = nullptr) const;

  /// Analyze the argument list in \p Args, using \p Assigner to populate \p
  /// CCInfo. This will determine the types and locations to use for passed or
  /// returned values. This may resize fields in \p Args if the value is split
  /// across multiple registers or stack slots.
  ///
  /// This is independent of the function state and can be used
  /// to determine how a call would pass arguments without needing to change the
  /// function. This can be used to check if arguments are suitable for tail
  /// call lowering.
  ///
  /// \return True if everything has succeeded, false otherwise.
  bool determineAssignments(ValueAssigner &Assigner,
                            SmallVectorImpl<ArgInfo> &Args,
                            CCState &CCInfo) const;

  /// Invoke ValueAssigner::assignArg on each of the given \p Args and then use
  /// \p Handler to move them to the assigned locations.
  ///
  /// \return True if everything has succeeded, false otherwise.
  bool determineAndHandleAssignments(
      ValueHandler &Handler, ValueAssigner &Assigner,
      SmallVectorImpl<ArgInfo> &Args, MachineIRBuilder &MIRBuilder,
      CallingConv::ID CallConv, bool IsVarArg,
      ArrayRef<Register> ThisReturnRegs = std::nullopt) const;

  /// Use \p Handler to insert code to handle the argument/return values
  /// represented by \p Args. It's expected determineAssignments previously
  /// processed these arguments to populate \p CCState and \p ArgLocs.
  bool
  handleAssignments(ValueHandler &Handler, SmallVectorImpl<ArgInfo> &Args,
                    CCState &CCState, SmallVectorImpl<CCValAssign> &ArgLocs,
                    MachineIRBuilder &MIRBuilder,
                    ArrayRef<Register> ThisReturnRegs = std::nullopt) const;

  /// Check whether parameters to a call that are passed in callee saved
  /// registers are the same as from the calling function.  This needs to be
  /// checked for tail call eligibility.
  bool parametersInCSRMatch(const MachineRegisterInfo &MRI,
                            const uint32_t *CallerPreservedMask,
                            const SmallVectorImpl<CCValAssign> &ArgLocs,
                            const SmallVectorImpl<ArgInfo> &OutVals) const;

  /// \returns True if the calling convention for a callee and its caller pass
  /// results in the same way. Typically used for tail call eligibility checks.
  ///
  /// \p Info is the CallLoweringInfo for the call.
  /// \p MF is the MachineFunction for the caller.
  /// \p InArgs contains the results of the call.
  /// \p CalleeAssigner specifies the target's handling of the argument types
  /// for the callee.
  /// \p CallerAssigner specifies the target's handling of the
  /// argument types for the caller.
  bool resultsCompatible(CallLoweringInfo &Info, MachineFunction &MF,
                         SmallVectorImpl<ArgInfo> &InArgs,
                         ValueAssigner &CalleeAssigner,
                         ValueAssigner &CallerAssigner) const;

public:
  CallLowering(const TargetLowering *TLI) : TLI(TLI) {}
  virtual ~CallLowering() = default;

  /// \return true if the target is capable of handling swifterror values that
  /// have been promoted to a specified register. The extended versions of
  /// lowerReturn and lowerCall should be implemented.
  virtual bool supportSwiftError() const {
    return false;
  }

  /// Load the returned value from the stack into virtual registers in \p VRegs.
  /// It uses the frame index \p FI and the start offset from \p DemoteReg.
  /// The loaded data size will be determined from \p RetTy.
  void insertSRetLoads(MachineIRBuilder &MIRBuilder, Type *RetTy,
                       ArrayRef<Register> VRegs, Register DemoteReg,
                       int FI) const;

  /// Store the return value given by \p VRegs into stack starting at the offset
  /// specified in \p DemoteReg.
  void insertSRetStores(MachineIRBuilder &MIRBuilder, Type *RetTy,
                        ArrayRef<Register> VRegs, Register DemoteReg) const;

  /// Insert the hidden sret ArgInfo to the beginning of \p SplitArgs.
  /// This function should be called from the target specific
  /// lowerFormalArguments when \p F requires the sret demotion.
  void insertSRetIncomingArgument(const Function &F,
                                  SmallVectorImpl<ArgInfo> &SplitArgs,
                                  Register &DemoteReg, MachineRegisterInfo &MRI,
                                  const DataLayout &DL) const;

  /// For the call-base described by \p CB, insert the hidden sret ArgInfo to
  /// the OrigArgs field of \p Info.
  void insertSRetOutgoingArgument(MachineIRBuilder &MIRBuilder,
                                  const CallBase &CB,
                                  CallLoweringInfo &Info) const;

  /// \return True if the return type described by \p Outs can be returned
  /// without performing sret demotion.
  bool checkReturn(CCState &CCInfo, SmallVectorImpl<BaseArgInfo> &Outs,
                   CCAssignFn *Fn) const;

  /// Get the type and the ArgFlags for the split components of \p RetTy as
  /// returned by \c ComputeValueVTs.
  void getReturnInfo(CallingConv::ID CallConv, Type *RetTy, AttributeList Attrs,
                     SmallVectorImpl<BaseArgInfo> &Outs,
                     const DataLayout &DL) const;

  /// Toplevel function to check the return type based on the target calling
  /// convention. \return True if the return value of \p MF can be returned
  /// without performing sret demotion.
  bool checkReturnTypeForCallConv(MachineFunction &MF) const;

  /// This hook must be implemented to check whether the return values
  /// described by \p Outs can fit into the return registers. If false
  /// is returned, an sret-demotion is performed.
  virtual bool canLowerReturn(MachineFunction &MF, CallingConv::ID CallConv,
                              SmallVectorImpl<BaseArgInfo> &Outs,
                              bool IsVarArg) const {
    return true;
  }

  /// This hook must be implemented to lower outgoing return values, described
  /// by \p Val, into the specified virtual registers \p VRegs.
  /// This hook is used by GlobalISel.
  ///
  /// \p FLI is required for sret demotion.
  ///
  /// \p SwiftErrorVReg is non-zero if the function has a swifterror parameter
  /// that needs to be implicitly returned.
  ///
  /// \return True if the lowering succeeds, false otherwise.
  virtual bool lowerReturn(MachineIRBuilder &MIRBuilder, const Value *Val,
                           ArrayRef<Register> VRegs, FunctionLoweringInfo &FLI,
                           Register SwiftErrorVReg) const {
    if (!supportSwiftError()) {
      assert(SwiftErrorVReg == 0 && "attempt to use unsupported swifterror");
      return lowerReturn(MIRBuilder, Val, VRegs, FLI);
    }
    return false;
  }

  /// This hook behaves as the extended lowerReturn function, but for targets
  /// that do not support swifterror value promotion.
  virtual bool lowerReturn(MachineIRBuilder &MIRBuilder, const Value *Val,
                           ArrayRef<Register> VRegs,
                           FunctionLoweringInfo &FLI) const {
    return false;
  }

  virtual bool fallBackToDAGISel(const MachineFunction &MF) const {
    return false;
  }

  /// This hook must be implemented to lower the incoming (formal)
  /// arguments, described by \p VRegs, for GlobalISel. Each argument
  /// must end up in the related virtual registers described by \p VRegs.
  /// In other words, the first argument should end up in \c VRegs[0],
  /// the second in \c VRegs[1], and so on. For each argument, there will be one
  /// register for each non-aggregate type, as returned by \c computeValueLLTs.
  /// \p MIRBuilder is set to the proper insertion for the argument
  /// lowering. \p FLI is required for sret demotion.
  ///
  /// \return True if the lowering succeeded, false otherwise.
  virtual bool lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                    const Function &F,
                                    ArrayRef<ArrayRef<Register>> VRegs,
                                    FunctionLoweringInfo &FLI) const {
    return false;
  }

  /// This hook must be implemented to lower the given call instruction,
  /// including argument and return value marshalling.
  ///
  ///
  /// \return true if the lowering succeeded, false otherwise.
  virtual bool lowerCall(MachineIRBuilder &MIRBuilder,
                         CallLoweringInfo &Info) const {
    return false;
  }

  /// Lower the given call instruction, including argument and return value
  /// marshalling.
  ///
  /// \p CI is the call/invoke instruction.
  ///
  /// \p ResRegs are the registers where the call's return value should be
  /// stored (or 0 if there is no return value). There will be one register for
  /// each non-aggregate type, as returned by \c computeValueLLTs.
  ///
  /// \p ArgRegs is a list of lists of virtual registers containing each
  /// argument that needs to be passed (argument \c i should be placed in \c
  /// ArgRegs[i]). For each argument, there will be one register for each
  /// non-aggregate type, as returned by \c computeValueLLTs.
  ///
  /// \p SwiftErrorVReg is non-zero if the call has a swifterror inout
  /// parameter, and contains the vreg that the swifterror should be copied into
  /// after the call.
  ///
  /// \p GetCalleeReg is a callback to materialize a register for the callee if
  /// the target determines it cannot jump to the destination based purely on \p
  /// CI. This might be because \p CI is indirect, or because of the limited
  /// range of an immediate jump.
  ///
  /// \return true if the lowering succeeded, false otherwise.
  bool lowerCall(MachineIRBuilder &MIRBuilder, const CallBase &Call,
                 ArrayRef<Register> ResRegs,
                 ArrayRef<ArrayRef<Register>> ArgRegs, Register SwiftErrorVReg,
                 std::optional<PtrAuthInfo> PAI, Register ConvergenceCtrlToken,
                 std::function<unsigned()> GetCalleeReg) const;

  /// For targets which want to use big-endian can enable it with
  /// enableBigEndian() hook
  virtual bool enableBigEndian() const { return false; }

  /// For targets which support the "returned" parameter attribute, returns
  /// true if the given type is a valid one to use with "returned".
  virtual bool isTypeIsValidForThisReturn(EVT Ty) const { return false; }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_CALLLOWERING_H
