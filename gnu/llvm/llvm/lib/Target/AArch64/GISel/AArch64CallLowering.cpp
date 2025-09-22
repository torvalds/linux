//===--- AArch64CallLowering.cpp - Call lowering --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the lowering of LLVM calls to machine code calls for
/// GlobalISel.
///
//===----------------------------------------------------------------------===//

#include "AArch64CallLowering.h"
#include "AArch64GlobalISelUtils.h"
#include "AArch64ISelLowering.h"
#include "AArch64MachineFunctionInfo.h"
#include "AArch64RegisterInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ObjCARCUtil.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/LowLevelTypeUtils.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>

#define DEBUG_TYPE "aarch64-call-lowering"

using namespace llvm;
using namespace AArch64GISelUtils;

extern cl::opt<bool> EnableSVEGISel;

AArch64CallLowering::AArch64CallLowering(const AArch64TargetLowering &TLI)
  : CallLowering(&TLI) {}

static void applyStackPassedSmallTypeDAGHack(EVT OrigVT, MVT &ValVT,
                                             MVT &LocVT) {
  // If ValVT is i1/i8/i16, we should set LocVT to i8/i8/i16. This is a legacy
  // hack because the DAG calls the assignment function with pre-legalized
  // register typed values, not the raw type.
  //
  // This hack is not applied to return values which are not passed on the
  // stack.
  if (OrigVT == MVT::i1 || OrigVT == MVT::i8)
    ValVT = LocVT = MVT::i8;
  else if (OrigVT == MVT::i16)
    ValVT = LocVT = MVT::i16;
}

// Account for i1/i8/i16 stack passed value hack
static LLT getStackValueStoreTypeHack(const CCValAssign &VA) {
  const MVT ValVT = VA.getValVT();
  return (ValVT == MVT::i8 || ValVT == MVT::i16) ? LLT(ValVT)
                                                 : LLT(VA.getLocVT());
}

namespace {

struct AArch64IncomingValueAssigner
    : public CallLowering::IncomingValueAssigner {
  AArch64IncomingValueAssigner(CCAssignFn *AssignFn_,
                               CCAssignFn *AssignFnVarArg_)
      : IncomingValueAssigner(AssignFn_, AssignFnVarArg_) {}

  bool assignArg(unsigned ValNo, EVT OrigVT, MVT ValVT, MVT LocVT,
                 CCValAssign::LocInfo LocInfo,
                 const CallLowering::ArgInfo &Info, ISD::ArgFlagsTy Flags,
                 CCState &State) override {
    applyStackPassedSmallTypeDAGHack(OrigVT, ValVT, LocVT);
    return IncomingValueAssigner::assignArg(ValNo, OrigVT, ValVT, LocVT,
                                            LocInfo, Info, Flags, State);
  }
};

struct AArch64OutgoingValueAssigner
    : public CallLowering::OutgoingValueAssigner {
  const AArch64Subtarget &Subtarget;

  /// Track if this is used for a return instead of function argument
  /// passing. We apply a hack to i1/i8/i16 stack passed values, but do not use
  /// stack passed returns for them and cannot apply the type adjustment.
  bool IsReturn;

  AArch64OutgoingValueAssigner(CCAssignFn *AssignFn_,
                               CCAssignFn *AssignFnVarArg_,
                               const AArch64Subtarget &Subtarget_,
                               bool IsReturn)
      : OutgoingValueAssigner(AssignFn_, AssignFnVarArg_),
        Subtarget(Subtarget_), IsReturn(IsReturn) {}

  bool assignArg(unsigned ValNo, EVT OrigVT, MVT ValVT, MVT LocVT,
                 CCValAssign::LocInfo LocInfo,
                 const CallLowering::ArgInfo &Info, ISD::ArgFlagsTy Flags,
                 CCState &State) override {
    const Function &F = State.getMachineFunction().getFunction();
    bool IsCalleeWin =
        Subtarget.isCallingConvWin64(State.getCallingConv(), F.isVarArg());
    bool UseVarArgsCCForFixed = IsCalleeWin && State.isVarArg();

    bool Res;
    if (Info.IsFixed && !UseVarArgsCCForFixed) {
      if (!IsReturn)
        applyStackPassedSmallTypeDAGHack(OrigVT, ValVT, LocVT);
      Res = AssignFn(ValNo, ValVT, LocVT, LocInfo, Flags, State);
    } else
      Res = AssignFnVarArg(ValNo, ValVT, LocVT, LocInfo, Flags, State);

    StackSize = State.getStackSize();
    return Res;
  }
};

struct IncomingArgHandler : public CallLowering::IncomingValueHandler {
  IncomingArgHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
      : IncomingValueHandler(MIRBuilder, MRI) {}

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    auto &MFI = MIRBuilder.getMF().getFrameInfo();

    // Byval is assumed to be writable memory, but other stack passed arguments
    // are not.
    const bool IsImmutable = !Flags.isByVal();

    int FI = MFI.CreateFixedObject(Size, Offset, IsImmutable);
    MPO = MachinePointerInfo::getFixedStack(MIRBuilder.getMF(), FI);
    auto AddrReg = MIRBuilder.buildFrameIndex(LLT::pointer(0, 64), FI);
    return AddrReg.getReg(0);
  }

  LLT getStackValueStoreType(const DataLayout &DL, const CCValAssign &VA,
                             ISD::ArgFlagsTy Flags) const override {
    // For pointers, we just need to fixup the integer types reported in the
    // CCValAssign.
    if (Flags.isPointer())
      return CallLowering::ValueHandler::getStackValueStoreType(DL, VA, Flags);
    return getStackValueStoreTypeHack(VA);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override {
    markPhysRegUsed(PhysReg);
    IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    MachineFunction &MF = MIRBuilder.getMF();

    LLT ValTy(VA.getValVT());
    LLT LocTy(VA.getLocVT());

    // Fixup the types for the DAG compatibility hack.
    if (VA.getValVT() == MVT::i8 || VA.getValVT() == MVT::i16)
      std::swap(ValTy, LocTy);
    else {
      // The calling code knows if this is a pointer or not, we're only touching
      // the LocTy for the i8/i16 hack.
      assert(LocTy.getSizeInBits() == MemTy.getSizeInBits());
      LocTy = MemTy;
    }

    auto MMO = MF.getMachineMemOperand(
        MPO, MachineMemOperand::MOLoad | MachineMemOperand::MOInvariant, LocTy,
        inferAlignFromPtrInfo(MF, MPO));

    switch (VA.getLocInfo()) {
    case CCValAssign::LocInfo::ZExt:
      MIRBuilder.buildLoadInstr(TargetOpcode::G_ZEXTLOAD, ValVReg, Addr, *MMO);
      return;
    case CCValAssign::LocInfo::SExt:
      MIRBuilder.buildLoadInstr(TargetOpcode::G_SEXTLOAD, ValVReg, Addr, *MMO);
      return;
    default:
      MIRBuilder.buildLoad(ValVReg, Addr, *MMO);
      return;
    }
  }

  /// How the physical register gets marked varies between formal
  /// parameters (it's a basic-block live-in), and a call instruction
  /// (it's an implicit-def of the BL).
  virtual void markPhysRegUsed(MCRegister PhysReg) = 0;
};

struct FormalArgHandler : public IncomingArgHandler {
  FormalArgHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
      : IncomingArgHandler(MIRBuilder, MRI) {}

  void markPhysRegUsed(MCRegister PhysReg) override {
    MIRBuilder.getMRI()->addLiveIn(PhysReg);
    MIRBuilder.getMBB().addLiveIn(PhysReg);
  }
};

struct CallReturnHandler : public IncomingArgHandler {
  CallReturnHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                    MachineInstrBuilder MIB)
      : IncomingArgHandler(MIRBuilder, MRI), MIB(MIB) {}

  void markPhysRegUsed(MCRegister PhysReg) override {
    MIB.addDef(PhysReg, RegState::Implicit);
  }

  MachineInstrBuilder MIB;
};

/// A special return arg handler for "returned" attribute arg calls.
struct ReturnedArgCallReturnHandler : public CallReturnHandler {
  ReturnedArgCallReturnHandler(MachineIRBuilder &MIRBuilder,
                               MachineRegisterInfo &MRI,
                               MachineInstrBuilder MIB)
      : CallReturnHandler(MIRBuilder, MRI, MIB) {}

  void markPhysRegUsed(MCRegister PhysReg) override {}
};

struct OutgoingArgHandler : public CallLowering::OutgoingValueHandler {
  OutgoingArgHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                     MachineInstrBuilder MIB, bool IsTailCall = false,
                     int FPDiff = 0)
      : OutgoingValueHandler(MIRBuilder, MRI), MIB(MIB), IsTailCall(IsTailCall),
        FPDiff(FPDiff),
        Subtarget(MIRBuilder.getMF().getSubtarget<AArch64Subtarget>()) {}

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    MachineFunction &MF = MIRBuilder.getMF();
    LLT p0 = LLT::pointer(0, 64);
    LLT s64 = LLT::scalar(64);

    if (IsTailCall) {
      assert(!Flags.isByVal() && "byval unhandled with tail calls");

      Offset += FPDiff;
      int FI = MF.getFrameInfo().CreateFixedObject(Size, Offset, true);
      auto FIReg = MIRBuilder.buildFrameIndex(p0, FI);
      MPO = MachinePointerInfo::getFixedStack(MF, FI);
      return FIReg.getReg(0);
    }

    if (!SPReg)
      SPReg = MIRBuilder.buildCopy(p0, Register(AArch64::SP)).getReg(0);

    auto OffsetReg = MIRBuilder.buildConstant(s64, Offset);

    auto AddrReg = MIRBuilder.buildPtrAdd(p0, SPReg, OffsetReg);

    MPO = MachinePointerInfo::getStack(MF, Offset);
    return AddrReg.getReg(0);
  }

  /// We need to fixup the reported store size for certain value types because
  /// we invert the interpretation of ValVT and LocVT in certain cases. This is
  /// for compatability with the DAG call lowering implementation, which we're
  /// currently building on top of.
  LLT getStackValueStoreType(const DataLayout &DL, const CCValAssign &VA,
                             ISD::ArgFlagsTy Flags) const override {
    if (Flags.isPointer())
      return CallLowering::ValueHandler::getStackValueStoreType(DL, VA, Flags);
    return getStackValueStoreTypeHack(VA);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override {
    MIB.addUse(PhysReg, RegState::Implicit);
    Register ExtReg = extendRegister(ValVReg, VA);
    MIRBuilder.buildCopy(PhysReg, ExtReg);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    MachineFunction &MF = MIRBuilder.getMF();
    auto MMO = MF.getMachineMemOperand(MPO, MachineMemOperand::MOStore, MemTy,
                                       inferAlignFromPtrInfo(MF, MPO));
    MIRBuilder.buildStore(ValVReg, Addr, *MMO);
  }

  void assignValueToAddress(const CallLowering::ArgInfo &Arg, unsigned RegIndex,
                            Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    unsigned MaxSize = MemTy.getSizeInBytes() * 8;
    // For varargs, we always want to extend them to 8 bytes, in which case
    // we disable setting a max.
    if (!Arg.IsFixed)
      MaxSize = 0;

    Register ValVReg = Arg.Regs[RegIndex];
    if (VA.getLocInfo() != CCValAssign::LocInfo::FPExt) {
      MVT LocVT = VA.getLocVT();
      MVT ValVT = VA.getValVT();

      if (VA.getValVT() == MVT::i8 || VA.getValVT() == MVT::i16) {
        std::swap(ValVT, LocVT);
        MemTy = LLT(VA.getValVT());
      }

      ValVReg = extendRegister(ValVReg, VA, MaxSize);
    } else {
      // The store does not cover the full allocated stack slot.
      MemTy = LLT(VA.getValVT());
    }

    assignValueToAddress(ValVReg, Addr, MemTy, MPO, VA);
  }

  MachineInstrBuilder MIB;

  bool IsTailCall;

  /// For tail calls, the byte offset of the call's argument area from the
  /// callee's. Unused elsewhere.
  int FPDiff;

  // Cache the SP register vreg if we need it more than once in this call site.
  Register SPReg;

  const AArch64Subtarget &Subtarget;
};
} // namespace

static bool doesCalleeRestoreStack(CallingConv::ID CallConv, bool TailCallOpt) {
  return (CallConv == CallingConv::Fast && TailCallOpt) ||
         CallConv == CallingConv::Tail || CallConv == CallingConv::SwiftTail;
}

bool AArch64CallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                      const Value *Val,
                                      ArrayRef<Register> VRegs,
                                      FunctionLoweringInfo &FLI,
                                      Register SwiftErrorVReg) const {
  auto MIB = MIRBuilder.buildInstrNoInsert(AArch64::RET_ReallyLR);
  assert(((Val && !VRegs.empty()) || (!Val && VRegs.empty())) &&
         "Return value without a vreg");

  bool Success = true;
  if (!FLI.CanLowerReturn) {
    insertSRetStores(MIRBuilder, Val->getType(), VRegs, FLI.DemoteRegister);
  } else if (!VRegs.empty()) {
    MachineFunction &MF = MIRBuilder.getMF();
    const Function &F = MF.getFunction();
    const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();

    MachineRegisterInfo &MRI = MF.getRegInfo();
    const AArch64TargetLowering &TLI = *getTLI<AArch64TargetLowering>();
    CCAssignFn *AssignFn = TLI.CCAssignFnForReturn(F.getCallingConv());
    auto &DL = F.getDataLayout();
    LLVMContext &Ctx = Val->getType()->getContext();

    SmallVector<EVT, 4> SplitEVTs;
    ComputeValueVTs(TLI, DL, Val->getType(), SplitEVTs);
    assert(VRegs.size() == SplitEVTs.size() &&
           "For each split Type there should be exactly one VReg.");

    SmallVector<ArgInfo, 8> SplitArgs;
    CallingConv::ID CC = F.getCallingConv();

    for (unsigned i = 0; i < SplitEVTs.size(); ++i) {
      Register CurVReg = VRegs[i];
      ArgInfo CurArgInfo = ArgInfo{CurVReg, SplitEVTs[i].getTypeForEVT(Ctx), 0};
      setArgFlags(CurArgInfo, AttributeList::ReturnIndex, DL, F);

      // i1 is a special case because SDAG i1 true is naturally zero extended
      // when widened using ANYEXT. We need to do it explicitly here.
      auto &Flags = CurArgInfo.Flags[0];
      if (MRI.getType(CurVReg).getSizeInBits() == 1 && !Flags.isSExt() &&
          !Flags.isZExt()) {
        CurVReg = MIRBuilder.buildZExt(LLT::scalar(8), CurVReg).getReg(0);
      } else if (TLI.getNumRegistersForCallingConv(Ctx, CC, SplitEVTs[i]) ==
                 1) {
        // Some types will need extending as specified by the CC.
        MVT NewVT = TLI.getRegisterTypeForCallingConv(Ctx, CC, SplitEVTs[i]);
        if (EVT(NewVT) != SplitEVTs[i]) {
          unsigned ExtendOp = TargetOpcode::G_ANYEXT;
          if (F.getAttributes().hasRetAttr(Attribute::SExt))
            ExtendOp = TargetOpcode::G_SEXT;
          else if (F.getAttributes().hasRetAttr(Attribute::ZExt))
            ExtendOp = TargetOpcode::G_ZEXT;

          LLT NewLLT(NewVT);
          LLT OldLLT = getLLTForType(*CurArgInfo.Ty, DL);
          CurArgInfo.Ty = EVT(NewVT).getTypeForEVT(Ctx);
          // Instead of an extend, we might have a vector type which needs
          // padding with more elements, e.g. <2 x half> -> <4 x half>.
          if (NewVT.isVector()) {
            if (OldLLT.isVector()) {
              if (NewLLT.getNumElements() > OldLLT.getNumElements()) {
                CurVReg =
                    MIRBuilder.buildPadVectorWithUndefElements(NewLLT, CurVReg)
                        .getReg(0);
              } else {
                // Just do a vector extend.
                CurVReg = MIRBuilder.buildInstr(ExtendOp, {NewLLT}, {CurVReg})
                              .getReg(0);
              }
            } else if (NewLLT.getNumElements() >= 2 &&
                       NewLLT.getNumElements() <= 8) {
              // We need to pad a <1 x S> type to <2/4/8 x S>. Since we don't
              // have <1 x S> vector types in GISel we use a build_vector
              // instead of a vector merge/concat.
              CurVReg =
                  MIRBuilder.buildPadVectorWithUndefElements(NewLLT, CurVReg)
                      .getReg(0);
            } else {
              LLVM_DEBUG(dbgs() << "Could not handle ret ty\n");
              return false;
            }
          } else {
            // If the split EVT was a <1 x T> vector, and NewVT is T, then we
            // don't have to do anything since we don't distinguish between the
            // two.
            if (NewLLT != MRI.getType(CurVReg)) {
              // A scalar extend.
              CurVReg = MIRBuilder.buildInstr(ExtendOp, {NewLLT}, {CurVReg})
                            .getReg(0);
            }
          }
        }
      }
      if (CurVReg != CurArgInfo.Regs[0]) {
        CurArgInfo.Regs[0] = CurVReg;
        // Reset the arg flags after modifying CurVReg.
        setArgFlags(CurArgInfo, AttributeList::ReturnIndex, DL, F);
      }
      splitToValueTypes(CurArgInfo, SplitArgs, DL, CC);
    }

    AArch64OutgoingValueAssigner Assigner(AssignFn, AssignFn, Subtarget,
                                          /*IsReturn*/ true);
    OutgoingArgHandler Handler(MIRBuilder, MRI, MIB);
    Success = determineAndHandleAssignments(Handler, Assigner, SplitArgs,
                                            MIRBuilder, CC, F.isVarArg());
  }

  if (SwiftErrorVReg) {
    MIB.addUse(AArch64::X21, RegState::Implicit);
    MIRBuilder.buildCopy(AArch64::X21, SwiftErrorVReg);
  }

  MIRBuilder.insertInstr(MIB);
  return Success;
}

bool AArch64CallLowering::canLowerReturn(MachineFunction &MF,
                                         CallingConv::ID CallConv,
                                         SmallVectorImpl<BaseArgInfo> &Outs,
                                         bool IsVarArg) const {
  SmallVector<CCValAssign, 16> ArgLocs;
  const auto &TLI = *getTLI<AArch64TargetLowering>();
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs,
                 MF.getFunction().getContext());

  return checkReturn(CCInfo, Outs, TLI.CCAssignFnForReturn(CallConv));
}

/// Helper function to compute forwarded registers for musttail calls. Computes
/// the forwarded registers, sets MBB liveness, and emits COPY instructions that
/// can be used to save + restore registers later.
static void handleMustTailForwardedRegisters(MachineIRBuilder &MIRBuilder,
                                             CCAssignFn *AssignFn) {
  MachineBasicBlock &MBB = MIRBuilder.getMBB();
  MachineFunction &MF = MIRBuilder.getMF();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  if (!MFI.hasMustTailInVarArgFunc())
    return;

  AArch64FunctionInfo *FuncInfo = MF.getInfo<AArch64FunctionInfo>();
  const Function &F = MF.getFunction();
  assert(F.isVarArg() && "Expected F to be vararg?");

  // Compute the set of forwarded registers. The rest are scratch.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(F.getCallingConv(), /*IsVarArg=*/true, MF, ArgLocs,
                 F.getContext());
  SmallVector<MVT, 2> RegParmTypes;
  RegParmTypes.push_back(MVT::i64);
  RegParmTypes.push_back(MVT::f128);

  // Later on, we can use this vector to restore the registers if necessary.
  SmallVectorImpl<ForwardedRegister> &Forwards =
      FuncInfo->getForwardedMustTailRegParms();
  CCInfo.analyzeMustTailForwardedRegisters(Forwards, RegParmTypes, AssignFn);

  // Conservatively forward X8, since it might be used for an aggregate
  // return.
  if (!CCInfo.isAllocated(AArch64::X8)) {
    Register X8VReg = MF.addLiveIn(AArch64::X8, &AArch64::GPR64RegClass);
    Forwards.push_back(ForwardedRegister(X8VReg, AArch64::X8, MVT::i64));
  }

  // Add the forwards to the MachineBasicBlock and MachineFunction.
  for (const auto &F : Forwards) {
    MBB.addLiveIn(F.PReg);
    MIRBuilder.buildCopy(Register(F.VReg), Register(F.PReg));
  }
}

bool AArch64CallLowering::fallBackToDAGISel(const MachineFunction &MF) const {
  auto &F = MF.getFunction();
  if (!EnableSVEGISel && (F.getReturnType()->isScalableTy() ||
                          llvm::any_of(F.args(), [](const Argument &A) {
                            return A.getType()->isScalableTy();
                          })))
    return true;
  const auto &ST = MF.getSubtarget<AArch64Subtarget>();
  if (!ST.hasNEON() || !ST.hasFPARMv8()) {
    LLVM_DEBUG(dbgs() << "Falling back to SDAG because we don't support no-NEON\n");
    return true;
  }

  SMEAttrs Attrs(F);
  if (Attrs.hasZAState() || Attrs.hasZT0State() ||
      Attrs.hasStreamingInterfaceOrBody() ||
      Attrs.hasStreamingCompatibleInterface())
    return true;

  return false;
}

void AArch64CallLowering::saveVarArgRegisters(
    MachineIRBuilder &MIRBuilder, CallLowering::IncomingValueHandler &Handler,
    CCState &CCInfo) const {
  auto GPRArgRegs = AArch64::getGPRArgRegs();
  auto FPRArgRegs = AArch64::getFPRArgRegs();

  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  AArch64FunctionInfo *FuncInfo = MF.getInfo<AArch64FunctionInfo>();
  auto &Subtarget = MF.getSubtarget<AArch64Subtarget>();
  bool IsWin64CC = Subtarget.isCallingConvWin64(CCInfo.getCallingConv(),
                                                MF.getFunction().isVarArg());
  const LLT p0 = LLT::pointer(0, 64);
  const LLT s64 = LLT::scalar(64);

  unsigned FirstVariadicGPR = CCInfo.getFirstUnallocated(GPRArgRegs);
  unsigned NumVariadicGPRArgRegs = GPRArgRegs.size() - FirstVariadicGPR + 1;

  unsigned GPRSaveSize = 8 * (GPRArgRegs.size() - FirstVariadicGPR);
  int GPRIdx = 0;
  if (GPRSaveSize != 0) {
    if (IsWin64CC) {
      GPRIdx = MFI.CreateFixedObject(GPRSaveSize,
                                     -static_cast<int>(GPRSaveSize), false);
      if (GPRSaveSize & 15)
        // The extra size here, if triggered, will always be 8.
        MFI.CreateFixedObject(16 - (GPRSaveSize & 15),
                              -static_cast<int>(alignTo(GPRSaveSize, 16)),
                              false);
    } else
      GPRIdx = MFI.CreateStackObject(GPRSaveSize, Align(8), false);

    auto FIN = MIRBuilder.buildFrameIndex(p0, GPRIdx);
    auto Offset =
        MIRBuilder.buildConstant(MRI.createGenericVirtualRegister(s64), 8);

    for (unsigned i = FirstVariadicGPR; i < GPRArgRegs.size(); ++i) {
      Register Val = MRI.createGenericVirtualRegister(s64);
      Handler.assignValueToReg(
          Val, GPRArgRegs[i],
          CCValAssign::getReg(i + MF.getFunction().getNumOperands(), MVT::i64,
                              GPRArgRegs[i], MVT::i64, CCValAssign::Full));
      auto MPO = IsWin64CC ? MachinePointerInfo::getFixedStack(
                               MF, GPRIdx, (i - FirstVariadicGPR) * 8)
                         : MachinePointerInfo::getStack(MF, i * 8);
      MIRBuilder.buildStore(Val, FIN, MPO, inferAlignFromPtrInfo(MF, MPO));

      FIN = MIRBuilder.buildPtrAdd(MRI.createGenericVirtualRegister(p0),
                                   FIN.getReg(0), Offset);
    }
  }
  FuncInfo->setVarArgsGPRIndex(GPRIdx);
  FuncInfo->setVarArgsGPRSize(GPRSaveSize);

  if (Subtarget.hasFPARMv8() && !IsWin64CC) {
    unsigned FirstVariadicFPR = CCInfo.getFirstUnallocated(FPRArgRegs);

    unsigned FPRSaveSize = 16 * (FPRArgRegs.size() - FirstVariadicFPR);
    int FPRIdx = 0;
    if (FPRSaveSize != 0) {
      FPRIdx = MFI.CreateStackObject(FPRSaveSize, Align(16), false);

      auto FIN = MIRBuilder.buildFrameIndex(p0, FPRIdx);
      auto Offset =
          MIRBuilder.buildConstant(MRI.createGenericVirtualRegister(s64), 16);

      for (unsigned i = FirstVariadicFPR; i < FPRArgRegs.size(); ++i) {
        Register Val = MRI.createGenericVirtualRegister(LLT::scalar(128));
        Handler.assignValueToReg(
            Val, FPRArgRegs[i],
            CCValAssign::getReg(
                i + MF.getFunction().getNumOperands() + NumVariadicGPRArgRegs,
                MVT::f128, FPRArgRegs[i], MVT::f128, CCValAssign::Full));

        auto MPO = MachinePointerInfo::getStack(MF, i * 16);
        MIRBuilder.buildStore(Val, FIN, MPO, inferAlignFromPtrInfo(MF, MPO));

        FIN = MIRBuilder.buildPtrAdd(MRI.createGenericVirtualRegister(p0),
                                     FIN.getReg(0), Offset);
      }
    }
    FuncInfo->setVarArgsFPRIndex(FPRIdx);
    FuncInfo->setVarArgsFPRSize(FPRSaveSize);
  }
}

bool AArch64CallLowering::lowerFormalArguments(
    MachineIRBuilder &MIRBuilder, const Function &F,
    ArrayRef<ArrayRef<Register>> VRegs, FunctionLoweringInfo &FLI) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineBasicBlock &MBB = MIRBuilder.getMBB();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  auto &DL = F.getDataLayout();
  auto &Subtarget = MF.getSubtarget<AArch64Subtarget>();

  // Arm64EC has extra requirements for varargs calls which are only implemented
  // in SelectionDAG; bail out for now.
  if (F.isVarArg() && Subtarget.isWindowsArm64EC())
    return false;

  // Arm64EC thunks have a special calling convention which is only implemented
  // in SelectionDAG; bail out for now.
  if (F.getCallingConv() == CallingConv::ARM64EC_Thunk_Native ||
      F.getCallingConv() == CallingConv::ARM64EC_Thunk_X64)
    return false;

  bool IsWin64 =
      Subtarget.isCallingConvWin64(F.getCallingConv(), F.isVarArg()) &&
      !Subtarget.isWindowsArm64EC();

  SmallVector<ArgInfo, 8> SplitArgs;
  SmallVector<std::pair<Register, Register>> BoolArgs;

  // Insert the hidden sret parameter if the return value won't fit in the
  // return registers.
  if (!FLI.CanLowerReturn)
    insertSRetIncomingArgument(F, SplitArgs, FLI.DemoteRegister, MRI, DL);

  unsigned i = 0;
  for (auto &Arg : F.args()) {
    if (DL.getTypeStoreSize(Arg.getType()).isZero())
      continue;

    ArgInfo OrigArg{VRegs[i], Arg, i};
    setArgFlags(OrigArg, i + AttributeList::FirstArgIndex, DL, F);

    // i1 arguments are zero-extended to i8 by the caller. Emit a
    // hint to reflect this.
    if (OrigArg.Ty->isIntegerTy(1)) {
      assert(OrigArg.Regs.size() == 1 &&
             MRI.getType(OrigArg.Regs[0]).getSizeInBits() == 1 &&
             "Unexpected registers used for i1 arg");

      auto &Flags = OrigArg.Flags[0];
      if (!Flags.isZExt() && !Flags.isSExt()) {
        // Lower i1 argument as i8, and insert AssertZExt + Trunc later.
        Register OrigReg = OrigArg.Regs[0];
        Register WideReg = MRI.createGenericVirtualRegister(LLT::scalar(8));
        OrigArg.Regs[0] = WideReg;
        BoolArgs.push_back({OrigReg, WideReg});
      }
    }

    if (Arg.hasAttribute(Attribute::SwiftAsync))
      MF.getInfo<AArch64FunctionInfo>()->setHasSwiftAsyncContext(true);

    splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv());
    ++i;
  }

  if (!MBB.empty())
    MIRBuilder.setInstr(*MBB.begin());

  const AArch64TargetLowering &TLI = *getTLI<AArch64TargetLowering>();
  CCAssignFn *AssignFn = TLI.CCAssignFnForCall(F.getCallingConv(), IsWin64 && F.isVarArg());

  AArch64IncomingValueAssigner Assigner(AssignFn, AssignFn);
  FormalArgHandler Handler(MIRBuilder, MRI);
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs, F.getContext());
  if (!determineAssignments(Assigner, SplitArgs, CCInfo) ||
      !handleAssignments(Handler, SplitArgs, CCInfo, ArgLocs, MIRBuilder))
    return false;

  if (!BoolArgs.empty()) {
    for (auto &KV : BoolArgs) {
      Register OrigReg = KV.first;
      Register WideReg = KV.second;
      LLT WideTy = MRI.getType(WideReg);
      assert(MRI.getType(OrigReg).getScalarSizeInBits() == 1 &&
             "Unexpected bit size of a bool arg");
      MIRBuilder.buildTrunc(
          OrigReg, MIRBuilder.buildAssertZExt(WideTy, WideReg, 1).getReg(0));
    }
  }

  AArch64FunctionInfo *FuncInfo = MF.getInfo<AArch64FunctionInfo>();
  uint64_t StackSize = Assigner.StackSize;
  if (F.isVarArg()) {
    if ((!Subtarget.isTargetDarwin() && !Subtarget.isWindowsArm64EC()) || IsWin64) {
      // The AAPCS variadic function ABI is identical to the non-variadic
      // one. As a result there may be more arguments in registers and we should
      // save them for future reference.
      // Win64 variadic functions also pass arguments in registers, but all
      // float arguments are passed in integer registers.
      saveVarArgRegisters(MIRBuilder, Handler, CCInfo);
    } else if (Subtarget.isWindowsArm64EC()) {
      return false;
    }

    // We currently pass all varargs at 8-byte alignment, or 4 in ILP32.
    StackSize = alignTo(Assigner.StackSize, Subtarget.isTargetILP32() ? 4 : 8);

    auto &MFI = MIRBuilder.getMF().getFrameInfo();
    FuncInfo->setVarArgsStackIndex(MFI.CreateFixedObject(4, StackSize, true));
  }

  if (doesCalleeRestoreStack(F.getCallingConv(),
                             MF.getTarget().Options.GuaranteedTailCallOpt)) {
    // We have a non-standard ABI, so why not make full use of the stack that
    // we're going to pop? It must be aligned to 16 B in any case.
    StackSize = alignTo(StackSize, 16);

    // If we're expected to restore the stack (e.g. fastcc), then we'll be
    // adding a multiple of 16.
    FuncInfo->setArgumentStackToRestore(StackSize);

    // Our own callers will guarantee that the space is free by giving an
    // aligned value to CALLSEQ_START.
  }

  // When we tail call, we need to check if the callee's arguments
  // will fit on the caller's stack. So, whenever we lower formal arguments,
  // we should keep track of this information, since we might lower a tail call
  // in this function later.
  FuncInfo->setBytesInStackArgArea(StackSize);

  if (Subtarget.hasCustomCallingConv())
    Subtarget.getRegisterInfo()->UpdateCustomCalleeSavedRegs(MF);

  handleMustTailForwardedRegisters(MIRBuilder, AssignFn);

  // Move back to the end of the basic block.
  MIRBuilder.setMBB(MBB);

  return true;
}

/// Return true if the calling convention is one that we can guarantee TCO for.
static bool canGuaranteeTCO(CallingConv::ID CC, bool GuaranteeTailCalls) {
  return (CC == CallingConv::Fast && GuaranteeTailCalls) ||
         CC == CallingConv::Tail || CC == CallingConv::SwiftTail;
}

/// Return true if we might ever do TCO for calls with this calling convention.
static bool mayTailCallThisCC(CallingConv::ID CC) {
  switch (CC) {
  case CallingConv::C:
  case CallingConv::PreserveMost:
  case CallingConv::PreserveAll:
  case CallingConv::PreserveNone:
  case CallingConv::Swift:
  case CallingConv::SwiftTail:
  case CallingConv::Tail:
  case CallingConv::Fast:
    return true;
  default:
    return false;
  }
}

/// Returns a pair containing the fixed CCAssignFn and the vararg CCAssignFn for
/// CC.
static std::pair<CCAssignFn *, CCAssignFn *>
getAssignFnsForCC(CallingConv::ID CC, const AArch64TargetLowering &TLI) {
  return {TLI.CCAssignFnForCall(CC, false), TLI.CCAssignFnForCall(CC, true)};
}

bool AArch64CallLowering::doCallerAndCalleePassArgsTheSameWay(
    CallLoweringInfo &Info, MachineFunction &MF,
    SmallVectorImpl<ArgInfo> &InArgs) const {
  const Function &CallerF = MF.getFunction();
  CallingConv::ID CalleeCC = Info.CallConv;
  CallingConv::ID CallerCC = CallerF.getCallingConv();

  // If the calling conventions match, then everything must be the same.
  if (CalleeCC == CallerCC)
    return true;

  // Check if the caller and callee will handle arguments in the same way.
  const AArch64TargetLowering &TLI = *getTLI<AArch64TargetLowering>();
  CCAssignFn *CalleeAssignFnFixed;
  CCAssignFn *CalleeAssignFnVarArg;
  std::tie(CalleeAssignFnFixed, CalleeAssignFnVarArg) =
      getAssignFnsForCC(CalleeCC, TLI);

  CCAssignFn *CallerAssignFnFixed;
  CCAssignFn *CallerAssignFnVarArg;
  std::tie(CallerAssignFnFixed, CallerAssignFnVarArg) =
      getAssignFnsForCC(CallerCC, TLI);

  AArch64IncomingValueAssigner CalleeAssigner(CalleeAssignFnFixed,
                                              CalleeAssignFnVarArg);
  AArch64IncomingValueAssigner CallerAssigner(CallerAssignFnFixed,
                                              CallerAssignFnVarArg);

  if (!resultsCompatible(Info, MF, InArgs, CalleeAssigner, CallerAssigner))
    return false;

  // Make sure that the caller and callee preserve all of the same registers.
  auto TRI = MF.getSubtarget<AArch64Subtarget>().getRegisterInfo();
  const uint32_t *CallerPreserved = TRI->getCallPreservedMask(MF, CallerCC);
  const uint32_t *CalleePreserved = TRI->getCallPreservedMask(MF, CalleeCC);
  if (MF.getSubtarget<AArch64Subtarget>().hasCustomCallingConv()) {
    TRI->UpdateCustomCallPreservedMask(MF, &CallerPreserved);
    TRI->UpdateCustomCallPreservedMask(MF, &CalleePreserved);
  }

  return TRI->regmaskSubsetEqual(CallerPreserved, CalleePreserved);
}

bool AArch64CallLowering::areCalleeOutgoingArgsTailCallable(
    CallLoweringInfo &Info, MachineFunction &MF,
    SmallVectorImpl<ArgInfo> &OrigOutArgs) const {
  // If there are no outgoing arguments, then we are done.
  if (OrigOutArgs.empty())
    return true;

  const Function &CallerF = MF.getFunction();
  LLVMContext &Ctx = CallerF.getContext();
  CallingConv::ID CalleeCC = Info.CallConv;
  CallingConv::ID CallerCC = CallerF.getCallingConv();
  const AArch64TargetLowering &TLI = *getTLI<AArch64TargetLowering>();
  const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();

  CCAssignFn *AssignFnFixed;
  CCAssignFn *AssignFnVarArg;
  std::tie(AssignFnFixed, AssignFnVarArg) = getAssignFnsForCC(CalleeCC, TLI);

  // We have outgoing arguments. Make sure that we can tail call with them.
  SmallVector<CCValAssign, 16> OutLocs;
  CCState OutInfo(CalleeCC, false, MF, OutLocs, Ctx);

  AArch64OutgoingValueAssigner CalleeAssigner(AssignFnFixed, AssignFnVarArg,
                                              Subtarget, /*IsReturn*/ false);
  // determineAssignments() may modify argument flags, so make a copy.
  SmallVector<ArgInfo, 8> OutArgs;
  append_range(OutArgs, OrigOutArgs);
  if (!determineAssignments(CalleeAssigner, OutArgs, OutInfo)) {
    LLVM_DEBUG(dbgs() << "... Could not analyze call operands.\n");
    return false;
  }

  // Make sure that they can fit on the caller's stack.
  const AArch64FunctionInfo *FuncInfo = MF.getInfo<AArch64FunctionInfo>();
  if (OutInfo.getStackSize() > FuncInfo->getBytesInStackArgArea()) {
    LLVM_DEBUG(dbgs() << "... Cannot fit call operands on caller's stack.\n");
    return false;
  }

  // Verify that the parameters in callee-saved registers match.
  // TODO: Port this over to CallLowering as general code once swiftself is
  // supported.
  auto TRI = MF.getSubtarget<AArch64Subtarget>().getRegisterInfo();
  const uint32_t *CallerPreservedMask = TRI->getCallPreservedMask(MF, CallerCC);
  MachineRegisterInfo &MRI = MF.getRegInfo();

  if (Info.IsVarArg) {
    // Be conservative and disallow variadic memory operands to match SDAG's
    // behaviour.
    // FIXME: If the caller's calling convention is C, then we can
    // potentially use its argument area. However, for cases like fastcc,
    // we can't do anything.
    for (unsigned i = 0; i < OutLocs.size(); ++i) {
      auto &ArgLoc = OutLocs[i];
      if (ArgLoc.isRegLoc())
        continue;

      LLVM_DEBUG(
          dbgs()
          << "... Cannot tail call vararg function with stack arguments\n");
      return false;
    }
  }

  return parametersInCSRMatch(MRI, CallerPreservedMask, OutLocs, OutArgs);
}

bool AArch64CallLowering::isEligibleForTailCallOptimization(
    MachineIRBuilder &MIRBuilder, CallLoweringInfo &Info,
    SmallVectorImpl<ArgInfo> &InArgs,
    SmallVectorImpl<ArgInfo> &OutArgs) const {

  // Must pass all target-independent checks in order to tail call optimize.
  if (!Info.IsTailCall)
    return false;

  CallingConv::ID CalleeCC = Info.CallConv;
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &CallerF = MF.getFunction();

  LLVM_DEBUG(dbgs() << "Attempting to lower call as tail call\n");

  if (Info.SwiftErrorVReg) {
    // TODO: We should handle this.
    // Note that this is also handled by the check for no outgoing arguments.
    // Proactively disabling this though, because the swifterror handling in
    // lowerCall inserts a COPY *after* the location of the call.
    LLVM_DEBUG(dbgs() << "... Cannot handle tail calls with swifterror yet.\n");
    return false;
  }

  if (!mayTailCallThisCC(CalleeCC)) {
    LLVM_DEBUG(dbgs() << "... Calling convention cannot be tail called.\n");
    return false;
  }

  // Byval parameters hand the function a pointer directly into the stack area
  // we want to reuse during a tail call. Working around this *is* possible (see
  // X86).
  //
  // FIXME: In AArch64ISelLowering, this isn't worked around. Can/should we try
  // it?
  //
  // On Windows, "inreg" attributes signify non-aggregate indirect returns.
  // In this case, it is necessary to save/restore X0 in the callee. Tail
  // call opt interferes with this. So we disable tail call opt when the
  // caller has an argument with "inreg" attribute.
  //
  // FIXME: Check whether the callee also has an "inreg" argument.
  //
  // When the caller has a swifterror argument, we don't want to tail call
  // because would have to move into the swifterror register before the
  // tail call.
  if (any_of(CallerF.args(), [](const Argument &A) {
        return A.hasByValAttr() || A.hasInRegAttr() || A.hasSwiftErrorAttr();
      })) {
    LLVM_DEBUG(dbgs() << "... Cannot tail call from callers with byval, "
                         "inreg, or swifterror arguments\n");
    return false;
  }

  // Externally-defined functions with weak linkage should not be
  // tail-called on AArch64 when the OS does not support dynamic
  // pre-emption of symbols, as the AAELF spec requires normal calls
  // to undefined weak functions to be replaced with a NOP or jump to the
  // next instruction. The behaviour of branch instructions in this
  // situation (as used for tail calls) is implementation-defined, so we
  // cannot rely on the linker replacing the tail call with a return.
  if (Info.Callee.isGlobal()) {
    const GlobalValue *GV = Info.Callee.getGlobal();
    const Triple &TT = MF.getTarget().getTargetTriple();
    if (GV->hasExternalWeakLinkage() &&
        (!TT.isOSWindows() || TT.isOSBinFormatELF() ||
         TT.isOSBinFormatMachO())) {
      LLVM_DEBUG(dbgs() << "... Cannot tail call externally-defined function "
                           "with weak linkage for this OS.\n");
      return false;
    }
  }

  // If we have -tailcallopt, then we're done.
  if (canGuaranteeTCO(CalleeCC, MF.getTarget().Options.GuaranteedTailCallOpt))
    return CalleeCC == CallerF.getCallingConv();

  // We don't have -tailcallopt, so we're allowed to change the ABI (sibcall).
  // Try to find cases where we can do that.

  // I want anyone implementing a new calling convention to think long and hard
  // about this assert.
  assert((!Info.IsVarArg || CalleeCC == CallingConv::C) &&
         "Unexpected variadic calling convention");

  // Verify that the incoming and outgoing arguments from the callee are
  // safe to tail call.
  if (!doCallerAndCalleePassArgsTheSameWay(Info, MF, InArgs)) {
    LLVM_DEBUG(
        dbgs()
        << "... Caller and callee have incompatible calling conventions.\n");
    return false;
  }

  if (!areCalleeOutgoingArgsTailCallable(Info, MF, OutArgs))
    return false;

  LLVM_DEBUG(
      dbgs() << "... Call is eligible for tail call optimization.\n");
  return true;
}

static unsigned getCallOpcode(const MachineFunction &CallerF, bool IsIndirect,
                              bool IsTailCall,
                              std::optional<CallLowering::PtrAuthInfo> &PAI,
                              MachineRegisterInfo &MRI) {
  const AArch64FunctionInfo *FuncInfo = CallerF.getInfo<AArch64FunctionInfo>();

  if (!IsTailCall) {
    if (!PAI)
      return IsIndirect ? getBLRCallOpcode(CallerF) : (unsigned)AArch64::BL;

    assert(IsIndirect && "Direct call should not be authenticated");
    assert((PAI->Key == AArch64PACKey::IA || PAI->Key == AArch64PACKey::IB) &&
           "Invalid auth call key");
    return AArch64::BLRA;
  }

  if (!IsIndirect)
    return AArch64::TCRETURNdi;

  // When BTI or PAuthLR are enabled, there are restrictions on using x16 and
  // x17 to hold the function pointer.
  if (FuncInfo->branchTargetEnforcement()) {
    if (FuncInfo->branchProtectionPAuthLR()) {
      assert(!PAI && "ptrauth tail-calls not yet supported with PAuthLR");
      return AArch64::TCRETURNrix17;
    }
    if (PAI)
      return AArch64::AUTH_TCRETURN_BTI;
    return AArch64::TCRETURNrix16x17;
  }

  if (FuncInfo->branchProtectionPAuthLR()) {
    assert(!PAI && "ptrauth tail-calls not yet supported with PAuthLR");
    return AArch64::TCRETURNrinotx16;
  }

  if (PAI)
    return AArch64::AUTH_TCRETURN;
  return AArch64::TCRETURNri;
}

static const uint32_t *
getMaskForArgs(SmallVectorImpl<AArch64CallLowering::ArgInfo> &OutArgs,
               AArch64CallLowering::CallLoweringInfo &Info,
               const AArch64RegisterInfo &TRI, MachineFunction &MF) {
  const uint32_t *Mask;
  if (!OutArgs.empty() && OutArgs[0].Flags[0].isReturned()) {
    // For 'this' returns, use the X0-preserving mask if applicable
    Mask = TRI.getThisReturnPreservedMask(MF, Info.CallConv);
    if (!Mask) {
      OutArgs[0].Flags[0].setReturned(false);
      Mask = TRI.getCallPreservedMask(MF, Info.CallConv);
    }
  } else {
    Mask = TRI.getCallPreservedMask(MF, Info.CallConv);
  }
  return Mask;
}

bool AArch64CallLowering::lowerTailCall(
    MachineIRBuilder &MIRBuilder, CallLoweringInfo &Info,
    SmallVectorImpl<ArgInfo> &OutArgs) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const AArch64TargetLowering &TLI = *getTLI<AArch64TargetLowering>();
  AArch64FunctionInfo *FuncInfo = MF.getInfo<AArch64FunctionInfo>();

  // True when we're tail calling, but without -tailcallopt.
  bool IsSibCall = !MF.getTarget().Options.GuaranteedTailCallOpt &&
                   Info.CallConv != CallingConv::Tail &&
                   Info.CallConv != CallingConv::SwiftTail;

  // Find out which ABI gets to decide where things go.
  CallingConv::ID CalleeCC = Info.CallConv;
  CCAssignFn *AssignFnFixed;
  CCAssignFn *AssignFnVarArg;
  std::tie(AssignFnFixed, AssignFnVarArg) = getAssignFnsForCC(CalleeCC, TLI);

  MachineInstrBuilder CallSeqStart;
  if (!IsSibCall)
    CallSeqStart = MIRBuilder.buildInstr(AArch64::ADJCALLSTACKDOWN);

  unsigned Opc = getCallOpcode(MF, Info.Callee.isReg(), true, Info.PAI, MRI);
  auto MIB = MIRBuilder.buildInstrNoInsert(Opc);
  MIB.add(Info.Callee);

  // Tell the call which registers are clobbered.
  const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();
  auto TRI = Subtarget.getRegisterInfo();

  // Byte offset for the tail call. When we are sibcalling, this will always
  // be 0.
  MIB.addImm(0);

  // Authenticated tail calls always take key/discriminator arguments.
  if (Opc == AArch64::AUTH_TCRETURN || Opc == AArch64::AUTH_TCRETURN_BTI) {
    assert((Info.PAI->Key == AArch64PACKey::IA ||
            Info.PAI->Key == AArch64PACKey::IB) &&
           "Invalid auth call key");
    MIB.addImm(Info.PAI->Key);

    Register AddrDisc = 0;
    uint16_t IntDisc = 0;
    std::tie(IntDisc, AddrDisc) =
        extractPtrauthBlendDiscriminators(Info.PAI->Discriminator, MRI);

    MIB.addImm(IntDisc);
    MIB.addUse(AddrDisc);
    if (AddrDisc != AArch64::NoRegister) {
      MIB->getOperand(4).setReg(constrainOperandRegClass(
          MF, *TRI, MRI, *MF.getSubtarget().getInstrInfo(),
          *MF.getSubtarget().getRegBankInfo(), *MIB, MIB->getDesc(),
          MIB->getOperand(4), 4));
    }
  }

  // Tell the call which registers are clobbered.
  const uint32_t *Mask = TRI->getCallPreservedMask(MF, CalleeCC);
  if (Subtarget.hasCustomCallingConv())
    TRI->UpdateCustomCallPreservedMask(MF, &Mask);
  MIB.addRegMask(Mask);

  if (Info.CFIType)
    MIB->setCFIType(MF, Info.CFIType->getZExtValue());

  if (TRI->isAnyArgRegReserved(MF))
    TRI->emitReservedArgRegCallError(MF);

  // FPDiff is the byte offset of the call's argument area from the callee's.
  // Stores to callee stack arguments will be placed in FixedStackSlots offset
  // by this amount for a tail call. In a sibling call it must be 0 because the
  // caller will deallocate the entire stack and the callee still expects its
  // arguments to begin at SP+0.
  int FPDiff = 0;

  // This will be 0 for sibcalls, potentially nonzero for tail calls produced
  // by -tailcallopt. For sibcalls, the memory operands for the call are
  // already available in the caller's incoming argument space.
  unsigned NumBytes = 0;
  if (!IsSibCall) {
    // We aren't sibcalling, so we need to compute FPDiff. We need to do this
    // before handling assignments, because FPDiff must be known for memory
    // arguments.
    unsigned NumReusableBytes = FuncInfo->getBytesInStackArgArea();
    SmallVector<CCValAssign, 16> OutLocs;
    CCState OutInfo(CalleeCC, false, MF, OutLocs, F.getContext());

    AArch64OutgoingValueAssigner CalleeAssigner(AssignFnFixed, AssignFnVarArg,
                                                Subtarget, /*IsReturn*/ false);
    if (!determineAssignments(CalleeAssigner, OutArgs, OutInfo))
      return false;

    // The callee will pop the argument stack as a tail call. Thus, we must
    // keep it 16-byte aligned.
    NumBytes = alignTo(OutInfo.getStackSize(), 16);

    // FPDiff will be negative if this tail call requires more space than we
    // would automatically have in our incoming argument space. Positive if we
    // actually shrink the stack.
    FPDiff = NumReusableBytes - NumBytes;

    // Update the required reserved area if this is the tail call requiring the
    // most argument stack space.
    if (FPDiff < 0 && FuncInfo->getTailCallReservedStack() < (unsigned)-FPDiff)
      FuncInfo->setTailCallReservedStack(-FPDiff);

    // The stack pointer must be 16-byte aligned at all times it's used for a
    // memory operation, which in practice means at *all* times and in
    // particular across call boundaries. Therefore our own arguments started at
    // a 16-byte aligned SP and the delta applied for the tail call should
    // satisfy the same constraint.
    assert(FPDiff % 16 == 0 && "unaligned stack on tail call");
  }

  const auto &Forwards = FuncInfo->getForwardedMustTailRegParms();

  AArch64OutgoingValueAssigner Assigner(AssignFnFixed, AssignFnVarArg,
                                        Subtarget, /*IsReturn*/ false);

  // Do the actual argument marshalling.
  OutgoingArgHandler Handler(MIRBuilder, MRI, MIB,
                             /*IsTailCall*/ true, FPDiff);
  if (!determineAndHandleAssignments(Handler, Assigner, OutArgs, MIRBuilder,
                                     CalleeCC, Info.IsVarArg))
    return false;

  Mask = getMaskForArgs(OutArgs, Info, *TRI, MF);

  if (Info.IsVarArg && Info.IsMustTailCall) {
    // Now we know what's being passed to the function. Add uses to the call for
    // the forwarded registers that we *aren't* passing as parameters. This will
    // preserve the copies we build earlier.
    for (const auto &F : Forwards) {
      Register ForwardedReg = F.PReg;
      // If the register is already passed, or aliases a register which is
      // already being passed, then skip it.
      if (any_of(MIB->uses(), [&ForwardedReg, &TRI](const MachineOperand &Use) {
            if (!Use.isReg())
              return false;
            return TRI->regsOverlap(Use.getReg(), ForwardedReg);
          }))
        continue;

      // We aren't passing it already, so we should add it to the call.
      MIRBuilder.buildCopy(ForwardedReg, Register(F.VReg));
      MIB.addReg(ForwardedReg, RegState::Implicit);
    }
  }

  // If we have -tailcallopt, we need to adjust the stack. We'll do the call
  // sequence start and end here.
  if (!IsSibCall) {
    MIB->getOperand(1).setImm(FPDiff);
    CallSeqStart.addImm(0).addImm(0);
    // End the call sequence *before* emitting the call. Normally, we would
    // tidy the frame up after the call. However, here, we've laid out the
    // parameters so that when SP is reset, they will be in the correct
    // location.
    MIRBuilder.buildInstr(AArch64::ADJCALLSTACKUP).addImm(0).addImm(0);
  }

  // Now we can add the actual call instruction to the correct basic block.
  MIRBuilder.insertInstr(MIB);

  // If Callee is a reg, since it is used by a target specific instruction,
  // it must have a register class matching the constraint of that instruction.
  if (MIB->getOperand(0).isReg())
    constrainOperandRegClass(MF, *TRI, MRI, *MF.getSubtarget().getInstrInfo(),
                             *MF.getSubtarget().getRegBankInfo(), *MIB,
                             MIB->getDesc(), MIB->getOperand(0), 0);

  MF.getFrameInfo().setHasTailCall();
  Info.LoweredTailCall = true;
  return true;
}

bool AArch64CallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                    CallLoweringInfo &Info) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  auto &DL = F.getDataLayout();
  const AArch64TargetLowering &TLI = *getTLI<AArch64TargetLowering>();
  const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();

  // Arm64EC has extra requirements for varargs calls; bail out for now.
  //
  // Arm64EC has special mangling rules for calls; bail out on all calls for
  // now.
  if (Subtarget.isWindowsArm64EC())
    return false;

  // Arm64EC thunks have a special calling convention which is only implemented
  // in SelectionDAG; bail out for now.
  if (Info.CallConv == CallingConv::ARM64EC_Thunk_Native ||
      Info.CallConv == CallingConv::ARM64EC_Thunk_X64)
    return false;

  SmallVector<ArgInfo, 8> OutArgs;
  for (auto &OrigArg : Info.OrigArgs) {
    splitToValueTypes(OrigArg, OutArgs, DL, Info.CallConv);
    // AAPCS requires that we zero-extend i1 to 8 bits by the caller.
    auto &Flags = OrigArg.Flags[0];
    if (OrigArg.Ty->isIntegerTy(1) && !Flags.isSExt() && !Flags.isZExt()) {
      ArgInfo &OutArg = OutArgs.back();
      assert(OutArg.Regs.size() == 1 &&
             MRI.getType(OutArg.Regs[0]).getSizeInBits() == 1 &&
             "Unexpected registers used for i1 arg");

      // We cannot use a ZExt ArgInfo flag here, because it will
      // zero-extend the argument to i32 instead of just i8.
      OutArg.Regs[0] =
          MIRBuilder.buildZExt(LLT::scalar(8), OutArg.Regs[0]).getReg(0);
      LLVMContext &Ctx = MF.getFunction().getContext();
      OutArg.Ty = Type::getInt8Ty(Ctx);
    }
  }

  SmallVector<ArgInfo, 8> InArgs;
  if (!Info.OrigRet.Ty->isVoidTy())
    splitToValueTypes(Info.OrigRet, InArgs, DL, Info.CallConv);

  // If we can lower as a tail call, do that instead.
  bool CanTailCallOpt =
      isEligibleForTailCallOptimization(MIRBuilder, Info, InArgs, OutArgs);

  // We must emit a tail call if we have musttail.
  if (Info.IsMustTailCall && !CanTailCallOpt) {
    // There are types of incoming/outgoing arguments we can't handle yet, so
    // it doesn't make sense to actually die here like in ISelLowering. Instead,
    // fall back to SelectionDAG and let it try to handle this.
    LLVM_DEBUG(dbgs() << "Failed to lower musttail call as tail call\n");
    return false;
  }

  Info.IsTailCall = CanTailCallOpt;
  if (CanTailCallOpt)
    return lowerTailCall(MIRBuilder, Info, OutArgs);

  // Find out which ABI gets to decide where things go.
  CCAssignFn *AssignFnFixed;
  CCAssignFn *AssignFnVarArg;
  std::tie(AssignFnFixed, AssignFnVarArg) =
      getAssignFnsForCC(Info.CallConv, TLI);

  MachineInstrBuilder CallSeqStart;
  CallSeqStart = MIRBuilder.buildInstr(AArch64::ADJCALLSTACKDOWN);

  // Create a temporarily-floating call instruction so we can add the implicit
  // uses of arg registers.

  unsigned Opc = 0;
  // Calls with operand bundle "clang.arc.attachedcall" are special. They should
  // be expanded to the call, directly followed by a special marker sequence and
  // a call to an ObjC library function.
  if (Info.CB && objcarc::hasAttachedCallOpBundle(Info.CB))
    Opc = Info.PAI ? AArch64::BLRA_RVMARKER : AArch64::BLR_RVMARKER;
  // A call to a returns twice function like setjmp must be followed by a bti
  // instruction.
  else if (Info.CB && Info.CB->hasFnAttr(Attribute::ReturnsTwice) &&
           !Subtarget.noBTIAtReturnTwice() &&
           MF.getInfo<AArch64FunctionInfo>()->branchTargetEnforcement())
    Opc = AArch64::BLR_BTI;
  else {
    // For an intrinsic call (e.g. memset), use GOT if "RtLibUseGOT" (-fno-plt)
    // is set.
    if (Info.Callee.isSymbol() && F.getParent()->getRtLibUseGOT()) {
      auto MIB = MIRBuilder.buildInstr(TargetOpcode::G_GLOBAL_VALUE);
      DstOp(getLLTForType(*F.getType(), DL)).addDefToMIB(MRI, MIB);
      MIB.addExternalSymbol(Info.Callee.getSymbolName(), AArch64II::MO_GOT);
      Info.Callee = MachineOperand::CreateReg(MIB.getReg(0), false);
    }
    Opc = getCallOpcode(MF, Info.Callee.isReg(), false, Info.PAI, MRI);
  }

  auto MIB = MIRBuilder.buildInstrNoInsert(Opc);
  unsigned CalleeOpNo = 0;

  if (Opc == AArch64::BLR_RVMARKER || Opc == AArch64::BLRA_RVMARKER) {
    // Add a target global address for the retainRV/claimRV runtime function
    // just before the call target.
    Function *ARCFn = *objcarc::getAttachedARCFunction(Info.CB);
    MIB.addGlobalAddress(ARCFn);
    ++CalleeOpNo;
  } else if (Info.CFIType) {
    MIB->setCFIType(MF, Info.CFIType->getZExtValue());
  }

  MIB.add(Info.Callee);

  // Tell the call which registers are clobbered.
  const uint32_t *Mask;
  const auto *TRI = Subtarget.getRegisterInfo();

  AArch64OutgoingValueAssigner Assigner(AssignFnFixed, AssignFnVarArg,
                                        Subtarget, /*IsReturn*/ false);
  // Do the actual argument marshalling.
  OutgoingArgHandler Handler(MIRBuilder, MRI, MIB, /*IsReturn*/ false);
  if (!determineAndHandleAssignments(Handler, Assigner, OutArgs, MIRBuilder,
                                     Info.CallConv, Info.IsVarArg))
    return false;

  Mask = getMaskForArgs(OutArgs, Info, *TRI, MF);

  if (Opc == AArch64::BLRA || Opc == AArch64::BLRA_RVMARKER) {
    assert((Info.PAI->Key == AArch64PACKey::IA ||
            Info.PAI->Key == AArch64PACKey::IB) &&
           "Invalid auth call key");
    MIB.addImm(Info.PAI->Key);

    Register AddrDisc = 0;
    uint16_t IntDisc = 0;
    std::tie(IntDisc, AddrDisc) =
        extractPtrauthBlendDiscriminators(Info.PAI->Discriminator, MRI);

    MIB.addImm(IntDisc);
    MIB.addUse(AddrDisc);
    if (AddrDisc != AArch64::NoRegister) {
      constrainOperandRegClass(MF, *TRI, MRI, *MF.getSubtarget().getInstrInfo(),
                               *MF.getSubtarget().getRegBankInfo(), *MIB,
                               MIB->getDesc(), MIB->getOperand(CalleeOpNo + 3),
                               CalleeOpNo + 3);
    }
  }

  // Tell the call which registers are clobbered.
  if (MF.getSubtarget<AArch64Subtarget>().hasCustomCallingConv())
    TRI->UpdateCustomCallPreservedMask(MF, &Mask);
  MIB.addRegMask(Mask);

  if (TRI->isAnyArgRegReserved(MF))
    TRI->emitReservedArgRegCallError(MF);

  // Now we can add the actual call instruction to the correct basic block.
  MIRBuilder.insertInstr(MIB);

  uint64_t CalleePopBytes =
      doesCalleeRestoreStack(Info.CallConv,
                             MF.getTarget().Options.GuaranteedTailCallOpt)
          ? alignTo(Assigner.StackSize, 16)
          : 0;

  CallSeqStart.addImm(Assigner.StackSize).addImm(0);
  MIRBuilder.buildInstr(AArch64::ADJCALLSTACKUP)
      .addImm(Assigner.StackSize)
      .addImm(CalleePopBytes);

  // If Callee is a reg, since it is used by a target specific
  // instruction, it must have a register class matching the
  // constraint of that instruction.
  if (MIB->getOperand(CalleeOpNo).isReg())
    constrainOperandRegClass(MF, *TRI, MRI, *Subtarget.getInstrInfo(),
                             *Subtarget.getRegBankInfo(), *MIB, MIB->getDesc(),
                             MIB->getOperand(CalleeOpNo), CalleeOpNo);

  // Finally we can copy the returned value back into its virtual-register. In
  // symmetry with the arguments, the physical register must be an
  // implicit-define of the call instruction.
  if (Info.CanLowerReturn  && !Info.OrigRet.Ty->isVoidTy()) {
    CCAssignFn *RetAssignFn = TLI.CCAssignFnForReturn(Info.CallConv);
    CallReturnHandler Handler(MIRBuilder, MRI, MIB);
    bool UsingReturnedArg =
        !OutArgs.empty() && OutArgs[0].Flags[0].isReturned();

    AArch64OutgoingValueAssigner Assigner(RetAssignFn, RetAssignFn, Subtarget,
                                          /*IsReturn*/ false);
    ReturnedArgCallReturnHandler ReturnedArgHandler(MIRBuilder, MRI, MIB);
    if (!determineAndHandleAssignments(
            UsingReturnedArg ? ReturnedArgHandler : Handler, Assigner, InArgs,
            MIRBuilder, Info.CallConv, Info.IsVarArg,
            UsingReturnedArg ? ArrayRef(OutArgs[0].Regs) : std::nullopt))
      return false;
  }

  if (Info.SwiftErrorVReg) {
    MIB.addDef(AArch64::X21, RegState::Implicit);
    MIRBuilder.buildCopy(Info.SwiftErrorVReg, Register(AArch64::X21));
  }

  if (!Info.CanLowerReturn) {
    insertSRetLoads(MIRBuilder, Info.OrigRet.Ty, Info.OrigRet.Regs,
                    Info.DemoteRegister, Info.DemoteStackIndex);
  }
  return true;
}

bool AArch64CallLowering::isTypeIsValidForThisReturn(EVT Ty) const {
  return Ty.getSizeInBits() == 64;
}
