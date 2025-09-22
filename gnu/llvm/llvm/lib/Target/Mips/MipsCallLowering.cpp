//===- MipsCallLowering.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements the lowering of LLVM calls to machine code calls for
/// GlobalISel.
//
//===----------------------------------------------------------------------===//

#include "MipsCallLowering.h"
#include "MipsCCState.h"
#include "MipsMachineFunction.h"
#include "MipsTargetMachine.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h"

using namespace llvm;

MipsCallLowering::MipsCallLowering(const MipsTargetLowering &TLI)
    : CallLowering(&TLI) {}

namespace {
struct MipsOutgoingValueAssigner : public CallLowering::OutgoingValueAssigner {
  /// This is the name of the function being called
  /// FIXME: Relying on this is unsound
  const char *Func = nullptr;

  /// Is this a return value, or an outgoing call operand.
  bool IsReturn;

  MipsOutgoingValueAssigner(CCAssignFn *AssignFn_, const char *Func,
                            bool IsReturn)
      : OutgoingValueAssigner(AssignFn_), Func(Func), IsReturn(IsReturn) {}

  bool assignArg(unsigned ValNo, EVT OrigVT, MVT ValVT, MVT LocVT,
                 CCValAssign::LocInfo LocInfo,
                 const CallLowering::ArgInfo &Info, ISD::ArgFlagsTy Flags,
                 CCState &State_) override {
    MipsCCState &State = static_cast<MipsCCState &>(State_);

    if (IsReturn)
      State.PreAnalyzeReturnValue(EVT::getEVT(Info.Ty));
    else
      State.PreAnalyzeCallOperand(Info.Ty, Info.IsFixed, Func);

    return CallLowering::OutgoingValueAssigner::assignArg(
        ValNo, OrigVT, ValVT, LocVT, LocInfo, Info, Flags, State);
  }
};

struct MipsIncomingValueAssigner : public CallLowering::IncomingValueAssigner {
  /// This is the name of the function being called
  /// FIXME: Relying on this is unsound
  const char *Func = nullptr;

  /// Is this a call return value, or an incoming function argument.
  bool IsReturn;

  MipsIncomingValueAssigner(CCAssignFn *AssignFn_, const char *Func,
                            bool IsReturn)
      : IncomingValueAssigner(AssignFn_), Func(Func), IsReturn(IsReturn) {}

  bool assignArg(unsigned ValNo, EVT OrigVT, MVT ValVT, MVT LocVT,
                 CCValAssign::LocInfo LocInfo,
                 const CallLowering::ArgInfo &Info, ISD::ArgFlagsTy Flags,
                 CCState &State_) override {
    MipsCCState &State = static_cast<MipsCCState &>(State_);

    if (IsReturn)
      State.PreAnalyzeCallResult(Info.Ty, Func);
    else
      State.PreAnalyzeFormalArgument(Info.Ty, Flags);

    return CallLowering::IncomingValueAssigner::assignArg(
        ValNo, OrigVT, ValVT, LocVT, LocInfo, Info, Flags, State);
  }
};

class MipsIncomingValueHandler : public CallLowering::IncomingValueHandler {
  const MipsSubtarget &STI;

public:
  MipsIncomingValueHandler(MachineIRBuilder &MIRBuilder,
                           MachineRegisterInfo &MRI)
      : IncomingValueHandler(MIRBuilder, MRI),
        STI(MIRBuilder.getMF().getSubtarget<MipsSubtarget>()) {}

private:
  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override;

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override;
  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override;

  unsigned assignCustomValue(CallLowering::ArgInfo &Arg,
                             ArrayRef<CCValAssign> VAs,
                             std::function<void()> *Thunk = nullptr) override;

  virtual void markPhysRegUsed(unsigned PhysReg) {
    MIRBuilder.getMRI()->addLiveIn(PhysReg);
    MIRBuilder.getMBB().addLiveIn(PhysReg);
  }
};

class CallReturnHandler : public MipsIncomingValueHandler {
public:
  CallReturnHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                    MachineInstrBuilder &MIB)
      : MipsIncomingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

private:
  void markPhysRegUsed(unsigned PhysReg) override {
    MIB.addDef(PhysReg, RegState::Implicit);
  }

  MachineInstrBuilder &MIB;
};

} // end anonymous namespace

void MipsIncomingValueHandler::assignValueToReg(Register ValVReg,
                                                Register PhysReg,
                                                const CCValAssign &VA) {
  markPhysRegUsed(PhysReg);
  IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA);
}

Register MipsIncomingValueHandler::getStackAddress(uint64_t Size,
                                                   int64_t Offset,
                                                   MachinePointerInfo &MPO,
                                                   ISD::ArgFlagsTy Flags) {

  MachineFunction &MF = MIRBuilder.getMF();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // FIXME: This should only be immutable for non-byval memory arguments.
  int FI = MFI.CreateFixedObject(Size, Offset, true);
  MPO = MachinePointerInfo::getFixedStack(MIRBuilder.getMF(), FI);

  return MIRBuilder.buildFrameIndex(LLT::pointer(0, 32), FI).getReg(0);
}

void MipsIncomingValueHandler::assignValueToAddress(
    Register ValVReg, Register Addr, LLT MemTy, const MachinePointerInfo &MPO,
    const CCValAssign &VA) {
  MachineFunction &MF = MIRBuilder.getMF();
  auto MMO = MF.getMachineMemOperand(MPO, MachineMemOperand::MOLoad, MemTy,
                                     inferAlignFromPtrInfo(MF, MPO));
  MIRBuilder.buildLoad(ValVReg, Addr, *MMO);
}

/// Handle cases when f64 is split into 2 32-bit GPRs. This is a custom
/// assignment because generic code assumes getNumRegistersForCallingConv is
/// accurate. In this case it is not because the type/number are context
/// dependent on other arguments.
unsigned
MipsIncomingValueHandler::assignCustomValue(CallLowering::ArgInfo &Arg,
                                            ArrayRef<CCValAssign> VAs,
                                            std::function<void()> *Thunk) {
  const CCValAssign &VALo = VAs[0];
  const CCValAssign &VAHi = VAs[1];

  assert(VALo.getLocVT() == MVT::i32 && VAHi.getLocVT() == MVT::i32 &&
         VALo.getValVT() == MVT::f64 && VAHi.getValVT() == MVT::f64 &&
         "unexpected custom value");

  auto CopyLo = MIRBuilder.buildCopy(LLT::scalar(32), VALo.getLocReg());
  auto CopyHi = MIRBuilder.buildCopy(LLT::scalar(32), VAHi.getLocReg());
  if (!STI.isLittle())
    std::swap(CopyLo, CopyHi);

  Arg.OrigRegs.assign(Arg.Regs.begin(), Arg.Regs.end());
  Arg.Regs = { CopyLo.getReg(0), CopyHi.getReg(0) };
  MIRBuilder.buildMergeLikeInstr(Arg.OrigRegs[0], {CopyLo, CopyHi});

  markPhysRegUsed(VALo.getLocReg());
  markPhysRegUsed(VAHi.getLocReg());
  return 2;
}

namespace {
class MipsOutgoingValueHandler : public CallLowering::OutgoingValueHandler {
  const MipsSubtarget &STI;

public:
  MipsOutgoingValueHandler(MachineIRBuilder &MIRBuilder,
                           MachineRegisterInfo &MRI, MachineInstrBuilder &MIB)
      : OutgoingValueHandler(MIRBuilder, MRI),
        STI(MIRBuilder.getMF().getSubtarget<MipsSubtarget>()), MIB(MIB) {}

private:
  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override;

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override;

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override;
  unsigned assignCustomValue(CallLowering::ArgInfo &Arg,
                             ArrayRef<CCValAssign> VAs,
                             std::function<void()> *Thunk) override;

  MachineInstrBuilder &MIB;
};
} // end anonymous namespace

void MipsOutgoingValueHandler::assignValueToReg(Register ValVReg,
                                                Register PhysReg,
                                                const CCValAssign &VA) {
  Register ExtReg = extendRegister(ValVReg, VA);
  MIRBuilder.buildCopy(PhysReg, ExtReg);
  MIB.addUse(PhysReg, RegState::Implicit);
}

Register MipsOutgoingValueHandler::getStackAddress(uint64_t Size,
                                                   int64_t Offset,
                                                   MachinePointerInfo &MPO,
                                                   ISD::ArgFlagsTy Flags) {
  MachineFunction &MF = MIRBuilder.getMF();
  MPO = MachinePointerInfo::getStack(MF, Offset);

  LLT p0 = LLT::pointer(0, 32);
  LLT s32 = LLT::scalar(32);
  auto SPReg = MIRBuilder.buildCopy(p0, Register(Mips::SP));

  auto OffsetReg = MIRBuilder.buildConstant(s32, Offset);
  auto AddrReg = MIRBuilder.buildPtrAdd(p0, SPReg, OffsetReg);
  return AddrReg.getReg(0);
}

void MipsOutgoingValueHandler::assignValueToAddress(
    Register ValVReg, Register Addr, LLT MemTy, const MachinePointerInfo &MPO,
    const CCValAssign &VA) {
  MachineFunction &MF = MIRBuilder.getMF();
  uint64_t LocMemOffset = VA.getLocMemOffset();

  auto MMO = MF.getMachineMemOperand(
      MPO, MachineMemOperand::MOStore, MemTy,
      commonAlignment(STI.getStackAlignment(), LocMemOffset));

  Register ExtReg = extendRegister(ValVReg, VA);
  MIRBuilder.buildStore(ExtReg, Addr, *MMO);
}

unsigned
MipsOutgoingValueHandler::assignCustomValue(CallLowering::ArgInfo &Arg,
                                            ArrayRef<CCValAssign> VAs,
                                            std::function<void()> *Thunk) {
  const CCValAssign &VALo = VAs[0];
  const CCValAssign &VAHi = VAs[1];

  assert(VALo.getLocVT() == MVT::i32 && VAHi.getLocVT() == MVT::i32 &&
         VALo.getValVT() == MVT::f64 && VAHi.getValVT() == MVT::f64 &&
         "unexpected custom value");

  auto Unmerge =
      MIRBuilder.buildUnmerge({LLT::scalar(32), LLT::scalar(32)}, Arg.Regs[0]);
  Register Lo = Unmerge.getReg(0);
  Register Hi = Unmerge.getReg(1);

  Arg.OrigRegs.assign(Arg.Regs.begin(), Arg.Regs.end());
  Arg.Regs = { Lo, Hi };
  if (!STI.isLittle())
    std::swap(Lo, Hi);

  // If we can return a thunk, just include the register copies. The unmerge can
  // be emitted earlier.
  if (Thunk) {
    *Thunk = [=]() {
      MIRBuilder.buildCopy(VALo.getLocReg(), Lo);
      MIRBuilder.buildCopy(VAHi.getLocReg(), Hi);
    };
    return 2;
  }
  MIRBuilder.buildCopy(VALo.getLocReg(), Lo);
  MIRBuilder.buildCopy(VAHi.getLocReg(), Hi);
  return 2;
}

static bool isSupportedArgumentType(Type *T) {
  if (T->isIntegerTy())
    return true;
  if (T->isPointerTy())
    return true;
  if (T->isFloatingPointTy())
    return true;
  return false;
}

static bool isSupportedReturnType(Type *T) {
  if (T->isIntegerTy())
    return true;
  if (T->isPointerTy())
    return true;
  if (T->isFloatingPointTy())
    return true;
  if (T->isAggregateType())
    return true;
  return false;
}

bool MipsCallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                   const Value *Val, ArrayRef<Register> VRegs,
                                   FunctionLoweringInfo &FLI) const {

  MachineInstrBuilder Ret = MIRBuilder.buildInstrNoInsert(Mips::RetRA);

  if (Val != nullptr && !isSupportedReturnType(Val->getType()))
    return false;

  if (!VRegs.empty()) {
    MachineFunction &MF = MIRBuilder.getMF();
    const Function &F = MF.getFunction();
    const DataLayout &DL = MF.getDataLayout();
    const MipsTargetLowering &TLI = *getTLI<MipsTargetLowering>();

    SmallVector<ArgInfo, 8> RetInfos;

    ArgInfo ArgRetInfo(VRegs, *Val, 0);
    setArgFlags(ArgRetInfo, AttributeList::ReturnIndex, DL, F);
    splitToValueTypes(ArgRetInfo, RetInfos, DL, F.getCallingConv());

    SmallVector<CCValAssign, 16> ArgLocs;
    SmallVector<ISD::OutputArg, 8> Outs;

    MipsCCState CCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs,
                       F.getContext());

    MipsOutgoingValueHandler RetHandler(MIRBuilder, MF.getRegInfo(), Ret);
    std::string FuncName = F.getName().str();
    MipsOutgoingValueAssigner Assigner(TLI.CCAssignFnForReturn(),
                                       FuncName.c_str(), /*IsReturn*/ true);

    if (!determineAssignments(Assigner, RetInfos, CCInfo))
      return false;

    if (!handleAssignments(RetHandler, RetInfos, CCInfo, ArgLocs, MIRBuilder))
      return false;
  }

  MIRBuilder.insertInstr(Ret);
  return true;
}

bool MipsCallLowering::lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                            const Function &F,
                                            ArrayRef<ArrayRef<Register>> VRegs,
                                            FunctionLoweringInfo &FLI) const {

  // Quick exit if there aren't any args.
  if (F.arg_empty())
    return true;

  for (auto &Arg : F.args()) {
    if (!isSupportedArgumentType(Arg.getType()))
      return false;
  }

  MachineFunction &MF = MIRBuilder.getMF();
  const DataLayout &DL = MF.getDataLayout();
  const MipsTargetLowering &TLI = *getTLI<MipsTargetLowering>();

  SmallVector<ArgInfo, 8> ArgInfos;
  unsigned i = 0;
  for (auto &Arg : F.args()) {
    ArgInfo AInfo(VRegs[i], Arg, i);
    setArgFlags(AInfo, i + AttributeList::FirstArgIndex, DL, F);

    splitToValueTypes(AInfo, ArgInfos, DL, F.getCallingConv());
    ++i;
  }

  SmallVector<ISD::InputArg, 8> Ins;

  SmallVector<CCValAssign, 16> ArgLocs;
  MipsCCState CCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs,
                     F.getContext());

  const MipsTargetMachine &TM =
      static_cast<const MipsTargetMachine &>(MF.getTarget());
  const MipsABIInfo &ABI = TM.getABI();
  CCInfo.AllocateStack(ABI.GetCalleeAllocdArgSizeInBytes(F.getCallingConv()),
                       Align(1));

  const std::string FuncName = F.getName().str();
  MipsIncomingValueAssigner Assigner(TLI.CCAssignFnForCall(), FuncName.c_str(),
                                     /*IsReturn*/ false);
  if (!determineAssignments(Assigner, ArgInfos, CCInfo))
    return false;

  MipsIncomingValueHandler Handler(MIRBuilder, MF.getRegInfo());
  if (!handleAssignments(Handler, ArgInfos, CCInfo, ArgLocs, MIRBuilder))
    return false;

  if (F.isVarArg()) {
    ArrayRef<MCPhysReg> ArgRegs = ABI.GetVarArgRegs();
    unsigned Idx = CCInfo.getFirstUnallocated(ArgRegs);

    int VaArgOffset;
    unsigned RegSize = 4;
    if (ArgRegs.size() == Idx)
      VaArgOffset = alignTo(CCInfo.getStackSize(), RegSize);
    else {
      VaArgOffset =
          (int)ABI.GetCalleeAllocdArgSizeInBytes(CCInfo.getCallingConv()) -
          (int)(RegSize * (ArgRegs.size() - Idx));
    }

    MachineFrameInfo &MFI = MF.getFrameInfo();
    int FI = MFI.CreateFixedObject(RegSize, VaArgOffset, true);
    MF.getInfo<MipsFunctionInfo>()->setVarArgsFrameIndex(FI);

    for (unsigned I = Idx; I < ArgRegs.size(); ++I, VaArgOffset += RegSize) {
      MIRBuilder.getMBB().addLiveIn(ArgRegs[I]);
      LLT RegTy = LLT::scalar(RegSize * 8);
      MachineInstrBuilder Copy =
          MIRBuilder.buildCopy(RegTy, Register(ArgRegs[I]));
      FI = MFI.CreateFixedObject(RegSize, VaArgOffset, true);
      MachinePointerInfo MPO = MachinePointerInfo::getFixedStack(MF, FI);

      const LLT PtrTy = LLT::pointer(MPO.getAddrSpace(), 32);
      auto FrameIndex = MIRBuilder.buildFrameIndex(PtrTy, FI);
      MachineMemOperand *MMO = MF.getMachineMemOperand(
          MPO, MachineMemOperand::MOStore, RegTy, Align(RegSize));
      MIRBuilder.buildStore(Copy, FrameIndex, *MMO);
    }
  }

  return true;
}

bool MipsCallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                 CallLoweringInfo &Info) const {

  if (Info.CallConv != CallingConv::C)
    return false;

  for (auto &Arg : Info.OrigArgs) {
    if (!isSupportedArgumentType(Arg.Ty))
      return false;
    if (Arg.Flags[0].isByVal())
      return false;
    if (Arg.Flags[0].isSRet() && !Arg.Ty->isPointerTy())
      return false;
  }

  if (!Info.OrigRet.Ty->isVoidTy() && !isSupportedReturnType(Info.OrigRet.Ty))
    return false;

  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  const DataLayout &DL = MF.getDataLayout();
  const MipsTargetLowering &TLI = *getTLI<MipsTargetLowering>();
  const MipsTargetMachine &TM =
      static_cast<const MipsTargetMachine &>(MF.getTarget());
  const MipsABIInfo &ABI = TM.getABI();

  MachineInstrBuilder CallSeqStart =
      MIRBuilder.buildInstr(Mips::ADJCALLSTACKDOWN);

  const bool IsCalleeGlobalPIC =
      Info.Callee.isGlobal() && TM.isPositionIndependent();

  MachineInstrBuilder MIB = MIRBuilder.buildInstrNoInsert(
      Info.Callee.isReg() || IsCalleeGlobalPIC ? Mips::JALRPseudo : Mips::JAL);
  MIB.addDef(Mips::SP, RegState::Implicit);
  if (IsCalleeGlobalPIC) {
    Register CalleeReg =
        MF.getRegInfo().createGenericVirtualRegister(LLT::pointer(0, 32));
    MachineInstr *CalleeGlobalValue =
        MIRBuilder.buildGlobalValue(CalleeReg, Info.Callee.getGlobal());
    if (!Info.Callee.getGlobal()->hasLocalLinkage())
      CalleeGlobalValue->getOperand(1).setTargetFlags(MipsII::MO_GOT_CALL);
    MIB.addUse(CalleeReg);
  } else
    MIB.add(Info.Callee);
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  MIB.addRegMask(TRI->getCallPreservedMask(MF, Info.CallConv));

  TargetLowering::ArgListTy FuncOrigArgs;
  FuncOrigArgs.reserve(Info.OrigArgs.size());

  SmallVector<ArgInfo, 8> ArgInfos;
  for (auto &Arg : Info.OrigArgs)
    splitToValueTypes(Arg, ArgInfos, DL, Info.CallConv);

  SmallVector<CCValAssign, 8> ArgLocs;
  bool IsCalleeVarArg = false;
  if (Info.Callee.isGlobal()) {
    const Function *CF = static_cast<const Function *>(Info.Callee.getGlobal());
    IsCalleeVarArg = CF->isVarArg();
  }

  // FIXME: Should use MipsCCState::getSpecialCallingConvForCallee, but it
  // depends on looking directly at the call target.
  MipsCCState CCInfo(Info.CallConv, IsCalleeVarArg, MF, ArgLocs,
                     F.getContext());

  CCInfo.AllocateStack(ABI.GetCalleeAllocdArgSizeInBytes(Info.CallConv),
                       Align(1));

  const char *Call =
      Info.Callee.isSymbol() ? Info.Callee.getSymbolName() : nullptr;

  MipsOutgoingValueAssigner Assigner(TLI.CCAssignFnForCall(), Call,
                                     /*IsReturn*/ false);
  if (!determineAssignments(Assigner, ArgInfos, CCInfo))
    return false;

  MipsOutgoingValueHandler ArgHandler(MIRBuilder, MF.getRegInfo(), MIB);
  if (!handleAssignments(ArgHandler, ArgInfos, CCInfo, ArgLocs, MIRBuilder))
    return false;

  unsigned StackSize = CCInfo.getStackSize();
  unsigned StackAlignment = F.getParent()->getOverrideStackAlignment();
  if (!StackAlignment) {
    const TargetFrameLowering *TFL = MF.getSubtarget().getFrameLowering();
    StackAlignment = TFL->getStackAlignment();
  }
  StackSize = alignTo(StackSize, StackAlignment);
  CallSeqStart.addImm(StackSize).addImm(0);

  if (IsCalleeGlobalPIC) {
    MIRBuilder.buildCopy(
      Register(Mips::GP),
      MF.getInfo<MipsFunctionInfo>()->getGlobalBaseRegForGlobalISel(MF));
    MIB.addDef(Mips::GP, RegState::Implicit);
  }
  MIRBuilder.insertInstr(MIB);
  if (MIB->getOpcode() == Mips::JALRPseudo) {
    const MipsSubtarget &STI = MIRBuilder.getMF().getSubtarget<MipsSubtarget>();
    MIB.constrainAllUses(MIRBuilder.getTII(), *STI.getRegisterInfo(),
                         *STI.getRegBankInfo());
  }

  if (!Info.OrigRet.Ty->isVoidTy()) {
    ArgInfos.clear();

    CallLowering::splitToValueTypes(Info.OrigRet, ArgInfos, DL,
                                    F.getCallingConv());

    const std::string FuncName = F.getName().str();
    SmallVector<ISD::InputArg, 8> Ins;
    SmallVector<CCValAssign, 8> ArgLocs;
    MipsIncomingValueAssigner Assigner(TLI.CCAssignFnForReturn(),
                                       FuncName.c_str(),
                                       /*IsReturn*/ true);
    CallReturnHandler RetHandler(MIRBuilder, MF.getRegInfo(), MIB);

    MipsCCState CCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs,
                       F.getContext());

    if (!determineAssignments(Assigner, ArgInfos, CCInfo))
      return false;

    if (!handleAssignments(RetHandler, ArgInfos, CCInfo, ArgLocs, MIRBuilder))
      return false;
  }

  MIRBuilder.buildInstr(Mips::ADJCALLSTACKUP).addImm(StackSize).addImm(0);

  return true;
}
