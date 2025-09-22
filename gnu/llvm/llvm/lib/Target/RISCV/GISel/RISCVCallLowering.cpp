//===-- RISCVCallLowering.cpp - Call lowering -------------------*- C++ -*-===//
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

#include "RISCVCallLowering.h"
#include "RISCVISelLowering.h"
#include "RISCVMachineFunctionInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h"

using namespace llvm;

namespace {

struct RISCVOutgoingValueAssigner : public CallLowering::OutgoingValueAssigner {
private:
  // The function used internally to assign args - we ignore the AssignFn stored
  // by OutgoingValueAssigner since RISC-V implements its CC using a custom
  // function with a different signature.
  RISCVTargetLowering::RISCVCCAssignFn *RISCVAssignFn;

  // Whether this is assigning args for a return.
  bool IsRet;

  RVVArgDispatcher &RVVDispatcher;

public:
  RISCVOutgoingValueAssigner(
      RISCVTargetLowering::RISCVCCAssignFn *RISCVAssignFn_, bool IsRet,
      RVVArgDispatcher &RVVDispatcher)
      : CallLowering::OutgoingValueAssigner(nullptr),
        RISCVAssignFn(RISCVAssignFn_), IsRet(IsRet),
        RVVDispatcher(RVVDispatcher) {}

  bool assignArg(unsigned ValNo, EVT OrigVT, MVT ValVT, MVT LocVT,
                 CCValAssign::LocInfo LocInfo,
                 const CallLowering::ArgInfo &Info, ISD::ArgFlagsTy Flags,
                 CCState &State) override {
    MachineFunction &MF = State.getMachineFunction();
    const DataLayout &DL = MF.getDataLayout();
    const RISCVSubtarget &Subtarget = MF.getSubtarget<RISCVSubtarget>();

    if (RISCVAssignFn(DL, Subtarget.getTargetABI(), ValNo, ValVT, LocVT,
                      LocInfo, Flags, State, Info.IsFixed, IsRet, Info.Ty,
                      *Subtarget.getTargetLowering(), RVVDispatcher))
      return true;

    StackSize = State.getStackSize();
    return false;
  }
};

struct RISCVOutgoingValueHandler : public CallLowering::OutgoingValueHandler {
  RISCVOutgoingValueHandler(MachineIRBuilder &B, MachineRegisterInfo &MRI,
                            MachineInstrBuilder MIB)
      : OutgoingValueHandler(B, MRI), MIB(MIB),
        Subtarget(MIRBuilder.getMF().getSubtarget<RISCVSubtarget>()) {}
  Register getStackAddress(uint64_t MemSize, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    MachineFunction &MF = MIRBuilder.getMF();
    LLT p0 = LLT::pointer(0, Subtarget.getXLen());
    LLT sXLen = LLT::scalar(Subtarget.getXLen());

    if (!SPReg)
      SPReg = MIRBuilder.buildCopy(p0, Register(RISCV::X2)).getReg(0);

    auto OffsetReg = MIRBuilder.buildConstant(sXLen, Offset);

    auto AddrReg = MIRBuilder.buildPtrAdd(p0, SPReg, OffsetReg);

    MPO = MachinePointerInfo::getStack(MF, Offset);
    return AddrReg.getReg(0);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    MachineFunction &MF = MIRBuilder.getMF();
    uint64_t LocMemOffset = VA.getLocMemOffset();

    // TODO: Move StackAlignment to subtarget and share with FrameLowering.
    auto MMO =
        MF.getMachineMemOperand(MPO, MachineMemOperand::MOStore, MemTy,
                                commonAlignment(Align(16), LocMemOffset));

    Register ExtReg = extendRegister(ValVReg, VA);
    MIRBuilder.buildStore(ExtReg, Addr, *MMO);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override {
    // If we're passing a smaller fp value into a larger integer register,
    // anyextend before copying.
    if ((VA.getLocVT() == MVT::i64 && VA.getValVT() == MVT::f32) ||
        ((VA.getLocVT() == MVT::i32 || VA.getLocVT() == MVT::i64) &&
         VA.getValVT() == MVT::f16)) {
      LLT DstTy = LLT::scalar(VA.getLocVT().getSizeInBits());
      ValVReg = MIRBuilder.buildAnyExt(DstTy, ValVReg).getReg(0);
    }

    Register ExtReg = extendRegister(ValVReg, VA);
    MIRBuilder.buildCopy(PhysReg, ExtReg);
    MIB.addUse(PhysReg, RegState::Implicit);
  }

  unsigned assignCustomValue(CallLowering::ArgInfo &Arg,
                             ArrayRef<CCValAssign> VAs,
                             std::function<void()> *Thunk) override {
    assert(VAs.size() >= 2 && "Expected at least 2 VAs.");
    const CCValAssign &VALo = VAs[0];
    const CCValAssign &VAHi = VAs[1];

    assert(VAHi.needsCustom() && "Value doesn't need custom handling");
    assert(VALo.getValNo() == VAHi.getValNo() &&
           "Values belong to different arguments");

    assert(VALo.getLocVT() == MVT::i32 && VAHi.getLocVT() == MVT::i32 &&
           VALo.getValVT() == MVT::f64 && VAHi.getValVT() == MVT::f64 &&
           "unexpected custom value");

    Register NewRegs[] = {MRI.createGenericVirtualRegister(LLT::scalar(32)),
                          MRI.createGenericVirtualRegister(LLT::scalar(32))};
    MIRBuilder.buildUnmerge(NewRegs, Arg.Regs[0]);

    if (VAHi.isMemLoc()) {
      LLT MemTy(VAHi.getLocVT());

      MachinePointerInfo MPO;
      Register StackAddr = getStackAddress(
          MemTy.getSizeInBytes(), VAHi.getLocMemOffset(), MPO, Arg.Flags[0]);

      assignValueToAddress(NewRegs[1], StackAddr, MemTy, MPO,
                           const_cast<CCValAssign &>(VAHi));
    }

    auto assignFunc = [=]() {
      assignValueToReg(NewRegs[0], VALo.getLocReg(), VALo);
      if (VAHi.isRegLoc())
        assignValueToReg(NewRegs[1], VAHi.getLocReg(), VAHi);
    };

    if (Thunk) {
      *Thunk = assignFunc;
      return 2;
    }

    assignFunc();
    return 2;
  }

private:
  MachineInstrBuilder MIB;

  // Cache the SP register vreg if we need it more than once in this call site.
  Register SPReg;

  const RISCVSubtarget &Subtarget;
};

struct RISCVIncomingValueAssigner : public CallLowering::IncomingValueAssigner {
private:
  // The function used internally to assign args - we ignore the AssignFn stored
  // by IncomingValueAssigner since RISC-V implements its CC using a custom
  // function with a different signature.
  RISCVTargetLowering::RISCVCCAssignFn *RISCVAssignFn;

  // Whether this is assigning args from a return.
  bool IsRet;

  RVVArgDispatcher &RVVDispatcher;

public:
  RISCVIncomingValueAssigner(
      RISCVTargetLowering::RISCVCCAssignFn *RISCVAssignFn_, bool IsRet,
      RVVArgDispatcher &RVVDispatcher)
      : CallLowering::IncomingValueAssigner(nullptr),
        RISCVAssignFn(RISCVAssignFn_), IsRet(IsRet),
        RVVDispatcher(RVVDispatcher) {}

  bool assignArg(unsigned ValNo, EVT OrigVT, MVT ValVT, MVT LocVT,
                 CCValAssign::LocInfo LocInfo,
                 const CallLowering::ArgInfo &Info, ISD::ArgFlagsTy Flags,
                 CCState &State) override {
    MachineFunction &MF = State.getMachineFunction();
    const DataLayout &DL = MF.getDataLayout();
    const RISCVSubtarget &Subtarget = MF.getSubtarget<RISCVSubtarget>();

    if (LocVT.isScalableVector())
      MF.getInfo<RISCVMachineFunctionInfo>()->setIsVectorCall();

    if (RISCVAssignFn(DL, Subtarget.getTargetABI(), ValNo, ValVT, LocVT,
                      LocInfo, Flags, State, /*IsFixed=*/true, IsRet, Info.Ty,
                      *Subtarget.getTargetLowering(), RVVDispatcher))
      return true;

    StackSize = State.getStackSize();
    return false;
  }
};

struct RISCVIncomingValueHandler : public CallLowering::IncomingValueHandler {
  RISCVIncomingValueHandler(MachineIRBuilder &B, MachineRegisterInfo &MRI)
      : IncomingValueHandler(B, MRI),
        Subtarget(MIRBuilder.getMF().getSubtarget<RISCVSubtarget>()) {}

  Register getStackAddress(uint64_t MemSize, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    MachineFrameInfo &MFI = MIRBuilder.getMF().getFrameInfo();

    int FI = MFI.CreateFixedObject(MemSize, Offset, /*Immutable=*/true);
    MPO = MachinePointerInfo::getFixedStack(MIRBuilder.getMF(), FI);
    return MIRBuilder.buildFrameIndex(LLT::pointer(0, Subtarget.getXLen()), FI)
        .getReg(0);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    MachineFunction &MF = MIRBuilder.getMF();
    auto MMO = MF.getMachineMemOperand(MPO, MachineMemOperand::MOLoad, MemTy,
                                       inferAlignFromPtrInfo(MF, MPO));
    MIRBuilder.buildLoad(ValVReg, Addr, *MMO);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override {
    markPhysRegUsed(PhysReg);
    IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA);
  }

  unsigned assignCustomValue(CallLowering::ArgInfo &Arg,
                             ArrayRef<CCValAssign> VAs,
                             std::function<void()> *Thunk) override {
    assert(VAs.size() >= 2 && "Expected at least 2 VAs.");
    const CCValAssign &VALo = VAs[0];
    const CCValAssign &VAHi = VAs[1];

    assert(VAHi.needsCustom() && "Value doesn't need custom handling");
    assert(VALo.getValNo() == VAHi.getValNo() &&
           "Values belong to different arguments");

    assert(VALo.getLocVT() == MVT::i32 && VAHi.getLocVT() == MVT::i32 &&
           VALo.getValVT() == MVT::f64 && VAHi.getValVT() == MVT::f64 &&
           "unexpected custom value");

    Register NewRegs[] = {MRI.createGenericVirtualRegister(LLT::scalar(32)),
                          MRI.createGenericVirtualRegister(LLT::scalar(32))};

    if (VAHi.isMemLoc()) {
      LLT MemTy(VAHi.getLocVT());

      MachinePointerInfo MPO;
      Register StackAddr = getStackAddress(
          MemTy.getSizeInBytes(), VAHi.getLocMemOffset(), MPO, Arg.Flags[0]);

      assignValueToAddress(NewRegs[1], StackAddr, MemTy, MPO,
                           const_cast<CCValAssign &>(VAHi));
    }

    assignValueToReg(NewRegs[0], VALo.getLocReg(), VALo);
    if (VAHi.isRegLoc())
      assignValueToReg(NewRegs[1], VAHi.getLocReg(), VAHi);

    MIRBuilder.buildMergeLikeInstr(Arg.Regs[0], NewRegs);

    return 2;
  }

  /// How the physical register gets marked varies between formal
  /// parameters (it's a basic-block live-in), and a call instruction
  /// (it's an implicit-def of the BL).
  virtual void markPhysRegUsed(MCRegister PhysReg) = 0;

private:
  const RISCVSubtarget &Subtarget;
};

struct RISCVFormalArgHandler : public RISCVIncomingValueHandler {
  RISCVFormalArgHandler(MachineIRBuilder &B, MachineRegisterInfo &MRI)
      : RISCVIncomingValueHandler(B, MRI) {}

  void markPhysRegUsed(MCRegister PhysReg) override {
    MIRBuilder.getMRI()->addLiveIn(PhysReg);
    MIRBuilder.getMBB().addLiveIn(PhysReg);
  }
};

struct RISCVCallReturnHandler : public RISCVIncomingValueHandler {
  RISCVCallReturnHandler(MachineIRBuilder &B, MachineRegisterInfo &MRI,
                         MachineInstrBuilder &MIB)
      : RISCVIncomingValueHandler(B, MRI), MIB(MIB) {}

  void markPhysRegUsed(MCRegister PhysReg) override {
    MIB.addDef(PhysReg, RegState::Implicit);
  }

  MachineInstrBuilder MIB;
};

} // namespace

RISCVCallLowering::RISCVCallLowering(const RISCVTargetLowering &TLI)
    : CallLowering(&TLI) {}

/// Return true if scalable vector with ScalarTy is legal for lowering.
static bool isLegalElementTypeForRVV(Type *EltTy,
                                     const RISCVSubtarget &Subtarget) {
  if (EltTy->isPointerTy())
    return Subtarget.is64Bit() ? Subtarget.hasVInstructionsI64() : true;
  if (EltTy->isIntegerTy(1) || EltTy->isIntegerTy(8) ||
      EltTy->isIntegerTy(16) || EltTy->isIntegerTy(32))
    return true;
  if (EltTy->isIntegerTy(64))
    return Subtarget.hasVInstructionsI64();
  if (EltTy->isHalfTy())
    return Subtarget.hasVInstructionsF16();
  if (EltTy->isBFloatTy())
    return Subtarget.hasVInstructionsBF16();
  if (EltTy->isFloatTy())
    return Subtarget.hasVInstructionsF32();
  if (EltTy->isDoubleTy())
    return Subtarget.hasVInstructionsF64();
  return false;
}

// TODO: Support all argument types.
// TODO: Remove IsLowerArgs argument by adding support for vectors in lowerCall.
static bool isSupportedArgumentType(Type *T, const RISCVSubtarget &Subtarget,
                                    bool IsLowerArgs = false) {
  if (T->isIntegerTy())
    return true;
  if (T->isHalfTy() || T->isFloatTy() || T->isDoubleTy())
    return true;
  if (T->isPointerTy())
    return true;
  // TODO: Support fixed vector types.
  if (IsLowerArgs && T->isVectorTy() && Subtarget.hasVInstructions() &&
      T->isScalableTy() &&
      isLegalElementTypeForRVV(T->getScalarType(), Subtarget))
    return true;
  return false;
}

// TODO: Only integer, pointer and aggregate types are supported now.
// TODO: Remove IsLowerRetVal argument by adding support for vectors in
// lowerCall.
static bool isSupportedReturnType(Type *T, const RISCVSubtarget &Subtarget,
                                  bool IsLowerRetVal = false) {
  // TODO: Integers larger than 2*XLen are passed indirectly which is not
  // supported yet.
  if (T->isIntegerTy())
    return T->getIntegerBitWidth() <= Subtarget.getXLen() * 2;
  if (T->isHalfTy() || T->isFloatTy() || T->isDoubleTy())
    return true;
  if (T->isPointerTy())
    return true;

  if (T->isArrayTy())
    return isSupportedReturnType(T->getArrayElementType(), Subtarget);

  if (T->isStructTy()) {
    auto StructT = cast<StructType>(T);
    for (unsigned i = 0, e = StructT->getNumElements(); i != e; ++i)
      if (!isSupportedReturnType(StructT->getElementType(i), Subtarget))
        return false;
    return true;
  }

  if (IsLowerRetVal && T->isVectorTy() && Subtarget.hasVInstructions() &&
      T->isScalableTy() &&
      isLegalElementTypeForRVV(T->getScalarType(), Subtarget))
    return true;

  return false;
}

bool RISCVCallLowering::lowerReturnVal(MachineIRBuilder &MIRBuilder,
                                       const Value *Val,
                                       ArrayRef<Register> VRegs,
                                       MachineInstrBuilder &Ret) const {
  if (!Val)
    return true;

  const RISCVSubtarget &Subtarget =
      MIRBuilder.getMF().getSubtarget<RISCVSubtarget>();
  if (!isSupportedReturnType(Val->getType(), Subtarget, /*IsLowerRetVal=*/true))
    return false;

  MachineFunction &MF = MIRBuilder.getMF();
  const DataLayout &DL = MF.getDataLayout();
  const Function &F = MF.getFunction();
  CallingConv::ID CC = F.getCallingConv();

  ArgInfo OrigRetInfo(VRegs, Val->getType(), 0);
  setArgFlags(OrigRetInfo, AttributeList::ReturnIndex, DL, F);

  SmallVector<ArgInfo, 4> SplitRetInfos;
  splitToValueTypes(OrigRetInfo, SplitRetInfos, DL, CC);

  RVVArgDispatcher Dispatcher{&MF, getTLI<RISCVTargetLowering>(),
                              ArrayRef(F.getReturnType())};
  RISCVOutgoingValueAssigner Assigner(
      CC == CallingConv::Fast ? RISCV::CC_RISCV_FastCC : RISCV::CC_RISCV,
      /*IsRet=*/true, Dispatcher);
  RISCVOutgoingValueHandler Handler(MIRBuilder, MF.getRegInfo(), Ret);
  return determineAndHandleAssignments(Handler, Assigner, SplitRetInfos,
                                       MIRBuilder, CC, F.isVarArg());
}

bool RISCVCallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                    const Value *Val, ArrayRef<Register> VRegs,
                                    FunctionLoweringInfo &FLI) const {
  assert(!Val == VRegs.empty() && "Return value without a vreg");
  MachineInstrBuilder Ret = MIRBuilder.buildInstrNoInsert(RISCV::PseudoRET);

  if (!lowerReturnVal(MIRBuilder, Val, VRegs, Ret))
    return false;

  MIRBuilder.insertInstr(Ret);
  return true;
}

/// If there are varargs that were passed in a0-a7, the data in those registers
/// must be copied to the varargs save area on the stack.
void RISCVCallLowering::saveVarArgRegisters(
    MachineIRBuilder &MIRBuilder, CallLowering::IncomingValueHandler &Handler,
    IncomingValueAssigner &Assigner, CCState &CCInfo) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const RISCVSubtarget &Subtarget = MF.getSubtarget<RISCVSubtarget>();
  unsigned XLenInBytes = Subtarget.getXLen() / 8;
  ArrayRef<MCPhysReg> ArgRegs = RISCV::getArgGPRs(Subtarget.getTargetABI());
  MachineRegisterInfo &MRI = MF.getRegInfo();
  unsigned Idx = CCInfo.getFirstUnallocated(ArgRegs);
  MachineFrameInfo &MFI = MF.getFrameInfo();
  RISCVMachineFunctionInfo *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();

  // Size of the vararg save area. For now, the varargs save area is either
  // zero or large enough to hold a0-a7.
  int VarArgsSaveSize = XLenInBytes * (ArgRegs.size() - Idx);
  int FI;

  // If all registers are allocated, then all varargs must be passed on the
  // stack and we don't need to save any argregs.
  if (VarArgsSaveSize == 0) {
    int VaArgOffset = Assigner.StackSize;
    FI = MFI.CreateFixedObject(XLenInBytes, VaArgOffset, true);
  } else {
    int VaArgOffset = -VarArgsSaveSize;
    FI = MFI.CreateFixedObject(VarArgsSaveSize, VaArgOffset, true);

    // If saving an odd number of registers then create an extra stack slot to
    // ensure that the frame pointer is 2*XLEN-aligned, which in turn ensures
    // offsets to even-numbered registered remain 2*XLEN-aligned.
    if (Idx % 2) {
      MFI.CreateFixedObject(XLenInBytes,
                            VaArgOffset - static_cast<int>(XLenInBytes), true);
      VarArgsSaveSize += XLenInBytes;
    }

    const LLT p0 = LLT::pointer(MF.getDataLayout().getAllocaAddrSpace(),
                                Subtarget.getXLen());
    const LLT sXLen = LLT::scalar(Subtarget.getXLen());

    auto FIN = MIRBuilder.buildFrameIndex(p0, FI);
    auto Offset = MIRBuilder.buildConstant(
        MRI.createGenericVirtualRegister(sXLen), XLenInBytes);

    // Copy the integer registers that may have been used for passing varargs
    // to the vararg save area.
    const MVT XLenVT = Subtarget.getXLenVT();
    for (unsigned I = Idx; I < ArgRegs.size(); ++I) {
      const Register VReg = MRI.createGenericVirtualRegister(sXLen);
      Handler.assignValueToReg(
          VReg, ArgRegs[I],
          CCValAssign::getReg(I + MF.getFunction().getNumOperands(), XLenVT,
                              ArgRegs[I], XLenVT, CCValAssign::Full));
      auto MPO =
          MachinePointerInfo::getFixedStack(MF, FI, (I - Idx) * XLenInBytes);
      MIRBuilder.buildStore(VReg, FIN, MPO, inferAlignFromPtrInfo(MF, MPO));
      FIN = MIRBuilder.buildPtrAdd(MRI.createGenericVirtualRegister(p0),
                                   FIN.getReg(0), Offset);
    }
  }

  // Record the frame index of the first variable argument which is a value
  // necessary to G_VASTART.
  RVFI->setVarArgsFrameIndex(FI);
  RVFI->setVarArgsSaveSize(VarArgsSaveSize);
}

bool RISCVCallLowering::lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                             const Function &F,
                                             ArrayRef<ArrayRef<Register>> VRegs,
                                             FunctionLoweringInfo &FLI) const {
  // Early exit if there are no arguments. varargs are not part of F.args() but
  // must be lowered.
  if (F.arg_empty() && !F.isVarArg())
    return true;

  const RISCVSubtarget &Subtarget =
      MIRBuilder.getMF().getSubtarget<RISCVSubtarget>();
  for (auto &Arg : F.args()) {
    if (!isSupportedArgumentType(Arg.getType(), Subtarget,
                                 /*IsLowerArgs=*/true))
      return false;
  }

  MachineFunction &MF = MIRBuilder.getMF();
  const DataLayout &DL = MF.getDataLayout();
  CallingConv::ID CC = F.getCallingConv();

  SmallVector<ArgInfo, 32> SplitArgInfos;
  SmallVector<Type *, 4> TypeList;
  unsigned Index = 0;
  for (auto &Arg : F.args()) {
    // Construct the ArgInfo object from destination register and argument type.
    ArgInfo AInfo(VRegs[Index], Arg.getType(), Index);
    setArgFlags(AInfo, Index + AttributeList::FirstArgIndex, DL, F);

    // Handle any required merging from split value types from physical
    // registers into the desired VReg. ArgInfo objects are constructed
    // correspondingly and appended to SplitArgInfos.
    splitToValueTypes(AInfo, SplitArgInfos, DL, CC);

    TypeList.push_back(Arg.getType());

    ++Index;
  }

  RVVArgDispatcher Dispatcher{&MF, getTLI<RISCVTargetLowering>(),
                              ArrayRef(TypeList)};
  RISCVIncomingValueAssigner Assigner(
      CC == CallingConv::Fast ? RISCV::CC_RISCV_FastCC : RISCV::CC_RISCV,
      /*IsRet=*/false, Dispatcher);
  RISCVFormalArgHandler Handler(MIRBuilder, MF.getRegInfo());

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CC, F.isVarArg(), MIRBuilder.getMF(), ArgLocs, F.getContext());
  if (!determineAssignments(Assigner, SplitArgInfos, CCInfo) ||
      !handleAssignments(Handler, SplitArgInfos, CCInfo, ArgLocs, MIRBuilder))
    return false;

  if (F.isVarArg())
    saveVarArgRegisters(MIRBuilder, Handler, Assigner, CCInfo);

  return true;
}

bool RISCVCallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                  CallLoweringInfo &Info) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const DataLayout &DL = MF.getDataLayout();
  const Function &F = MF.getFunction();
  CallingConv::ID CC = F.getCallingConv();

  const RISCVSubtarget &Subtarget =
      MIRBuilder.getMF().getSubtarget<RISCVSubtarget>();
  for (auto &AInfo : Info.OrigArgs) {
    if (!isSupportedArgumentType(AInfo.Ty, Subtarget))
      return false;
  }

  if (!Info.OrigRet.Ty->isVoidTy() &&
      !isSupportedReturnType(Info.OrigRet.Ty, Subtarget))
    return false;

  MachineInstrBuilder CallSeqStart =
      MIRBuilder.buildInstr(RISCV::ADJCALLSTACKDOWN);

  SmallVector<ArgInfo, 32> SplitArgInfos;
  SmallVector<ISD::OutputArg, 8> Outs;
  SmallVector<Type *, 4> TypeList;
  for (auto &AInfo : Info.OrigArgs) {
    // Handle any required unmerging of split value types from a given VReg into
    // physical registers. ArgInfo objects are constructed correspondingly and
    // appended to SplitArgInfos.
    splitToValueTypes(AInfo, SplitArgInfos, DL, CC);
    TypeList.push_back(AInfo.Ty);
  }

  // TODO: Support tail calls.
  Info.IsTailCall = false;

  // Select the recommended relocation type R_RISCV_CALL_PLT.
  if (!Info.Callee.isReg())
    Info.Callee.setTargetFlags(RISCVII::MO_CALL);

  MachineInstrBuilder Call =
      MIRBuilder
          .buildInstrNoInsert(Info.Callee.isReg() ? RISCV::PseudoCALLIndirect
                                                  : RISCV::PseudoCALL)
          .add(Info.Callee);
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  Call.addRegMask(TRI->getCallPreservedMask(MF, Info.CallConv));

  RVVArgDispatcher ArgDispatcher{&MF, getTLI<RISCVTargetLowering>(),
                                 ArrayRef(TypeList)};
  RISCVOutgoingValueAssigner ArgAssigner(
      CC == CallingConv::Fast ? RISCV::CC_RISCV_FastCC : RISCV::CC_RISCV,
      /*IsRet=*/false, ArgDispatcher);
  RISCVOutgoingValueHandler ArgHandler(MIRBuilder, MF.getRegInfo(), Call);
  if (!determineAndHandleAssignments(ArgHandler, ArgAssigner, SplitArgInfos,
                                     MIRBuilder, CC, Info.IsVarArg))
    return false;

  MIRBuilder.insertInstr(Call);

  CallSeqStart.addImm(ArgAssigner.StackSize).addImm(0);
  MIRBuilder.buildInstr(RISCV::ADJCALLSTACKUP)
      .addImm(ArgAssigner.StackSize)
      .addImm(0);

  // If Callee is a reg, since it is used by a target specific
  // instruction, it must have a register class matching the
  // constraint of that instruction.
  if (Call->getOperand(0).isReg())
    constrainOperandRegClass(MF, *TRI, MF.getRegInfo(),
                             *Subtarget.getInstrInfo(),
                             *Subtarget.getRegBankInfo(), *Call,
                             Call->getDesc(), Call->getOperand(0), 0);

  if (Info.OrigRet.Ty->isVoidTy())
    return true;

  SmallVector<ArgInfo, 4> SplitRetInfos;
  splitToValueTypes(Info.OrigRet, SplitRetInfos, DL, CC);

  RVVArgDispatcher RetDispatcher{&MF, getTLI<RISCVTargetLowering>(),
                                 ArrayRef(F.getReturnType())};
  RISCVIncomingValueAssigner RetAssigner(
      CC == CallingConv::Fast ? RISCV::CC_RISCV_FastCC : RISCV::CC_RISCV,
      /*IsRet=*/true, RetDispatcher);
  RISCVCallReturnHandler RetHandler(MIRBuilder, MF.getRegInfo(), Call);
  if (!determineAndHandleAssignments(RetHandler, RetAssigner, SplitRetInfos,
                                     MIRBuilder, CC, Info.IsVarArg))
    return false;

  return true;
}
