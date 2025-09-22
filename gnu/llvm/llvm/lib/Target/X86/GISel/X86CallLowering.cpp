//===- llvm/lib/Target/X86/X86CallLowering.cpp - Call lowering ------------===//
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

#include "X86CallLowering.h"
#include "X86CallingConv.h"
#include "X86ISelLowering.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "X86RegisterInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
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
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCRegisterInfo.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

X86CallLowering::X86CallLowering(const X86TargetLowering &TLI)
    : CallLowering(&TLI) {}

namespace {

struct X86OutgoingValueAssigner : public CallLowering::OutgoingValueAssigner {
private:
  uint64_t StackSize = 0;
  unsigned NumXMMRegs = 0;

public:
  uint64_t getStackSize() { return StackSize; }
  unsigned getNumXmmRegs() { return NumXMMRegs; }

  X86OutgoingValueAssigner(CCAssignFn *AssignFn_)
      : CallLowering::OutgoingValueAssigner(AssignFn_) {}

  bool assignArg(unsigned ValNo, EVT OrigVT, MVT ValVT, MVT LocVT,
                 CCValAssign::LocInfo LocInfo,
                 const CallLowering::ArgInfo &Info, ISD::ArgFlagsTy Flags,
                 CCState &State) override {
    bool Res = AssignFn(ValNo, ValVT, LocVT, LocInfo, Flags, State);
    StackSize = State.getStackSize();

    static const MCPhysReg XMMArgRegs[] = {X86::XMM0, X86::XMM1, X86::XMM2,
                                           X86::XMM3, X86::XMM4, X86::XMM5,
                                           X86::XMM6, X86::XMM7};
    if (!Info.IsFixed)
      NumXMMRegs = State.getFirstUnallocated(XMMArgRegs);

    return Res;
  }
};

struct X86OutgoingValueHandler : public CallLowering::OutgoingValueHandler {
  X86OutgoingValueHandler(MachineIRBuilder &MIRBuilder,
                          MachineRegisterInfo &MRI, MachineInstrBuilder &MIB)
      : OutgoingValueHandler(MIRBuilder, MRI), MIB(MIB),
        DL(MIRBuilder.getMF().getDataLayout()),
        STI(MIRBuilder.getMF().getSubtarget<X86Subtarget>()) {}

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    LLT p0 = LLT::pointer(0, DL.getPointerSizeInBits(0));
    LLT SType = LLT::scalar(DL.getPointerSizeInBits(0));
    auto SPReg =
        MIRBuilder.buildCopy(p0, STI.getRegisterInfo()->getStackRegister());

    auto OffsetReg = MIRBuilder.buildConstant(SType, Offset);

    auto AddrReg = MIRBuilder.buildPtrAdd(p0, SPReg, OffsetReg);

    MPO = MachinePointerInfo::getStack(MIRBuilder.getMF(), Offset);
    return AddrReg.getReg(0);
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
    Register ExtReg = extendRegister(ValVReg, VA);

    auto *MMO = MF.getMachineMemOperand(MPO, MachineMemOperand::MOStore, MemTy,
                                        inferAlignFromPtrInfo(MF, MPO));
    MIRBuilder.buildStore(ExtReg, Addr, *MMO);
  }

protected:
  MachineInstrBuilder &MIB;
  const DataLayout &DL;
  const X86Subtarget &STI;
};

} // end anonymous namespace

bool X86CallLowering::canLowerReturn(
    MachineFunction &MF, CallingConv::ID CallConv,
    SmallVectorImpl<CallLowering::BaseArgInfo> &Outs, bool IsVarArg) const {
  LLVMContext &Context = MF.getFunction().getContext();
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return checkReturn(CCInfo, Outs, RetCC_X86);
}

bool X86CallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                  const Value *Val, ArrayRef<Register> VRegs,
                                  FunctionLoweringInfo &FLI) const {
  assert(((Val && !VRegs.empty()) || (!Val && VRegs.empty())) &&
         "Return value without a vreg");
  MachineFunction &MF = MIRBuilder.getMF();
  auto MIB = MIRBuilder.buildInstrNoInsert(X86::RET).addImm(0);
  auto FuncInfo = MF.getInfo<X86MachineFunctionInfo>();
  const auto &STI = MF.getSubtarget<X86Subtarget>();
  Register RetReg = STI.is64Bit() ? X86::RAX : X86::EAX;

  if (!FLI.CanLowerReturn) {
    insertSRetStores(MIRBuilder, Val->getType(), VRegs, FLI.DemoteRegister);
    MIRBuilder.buildCopy(RetReg, FLI.DemoteRegister);
    MIB.addReg(RetReg);
  } else if (Register Reg = FuncInfo->getSRetReturnReg()) {
    MIRBuilder.buildCopy(RetReg, Reg);
    MIB.addReg(RetReg);
  } else if (!VRegs.empty()) {
    const Function &F = MF.getFunction();
    MachineRegisterInfo &MRI = MF.getRegInfo();
    const DataLayout &DL = MF.getDataLayout();

    ArgInfo OrigRetInfo(VRegs, Val->getType(), 0);
    setArgFlags(OrigRetInfo, AttributeList::ReturnIndex, DL, F);

    SmallVector<ArgInfo, 4> SplitRetInfos;
    splitToValueTypes(OrigRetInfo, SplitRetInfos, DL, F.getCallingConv());

    X86OutgoingValueAssigner Assigner(RetCC_X86);
    X86OutgoingValueHandler Handler(MIRBuilder, MRI, MIB);
    if (!determineAndHandleAssignments(Handler, Assigner, SplitRetInfos,
                                       MIRBuilder, F.getCallingConv(),
                                       F.isVarArg()))
      return false;
  }

  MIRBuilder.insertInstr(MIB);
  return true;
}

namespace {

struct X86IncomingValueHandler : public CallLowering::IncomingValueHandler {
  X86IncomingValueHandler(MachineIRBuilder &MIRBuilder,
                          MachineRegisterInfo &MRI)
      : IncomingValueHandler(MIRBuilder, MRI),
        DL(MIRBuilder.getMF().getDataLayout()) {}

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    auto &MFI = MIRBuilder.getMF().getFrameInfo();

    // Byval is assumed to be writable memory, but other stack passed arguments
    // are not.
    const bool IsImmutable = !Flags.isByVal();

    int FI = MFI.CreateFixedObject(Size, Offset, IsImmutable);
    MPO = MachinePointerInfo::getFixedStack(MIRBuilder.getMF(), FI);

    return MIRBuilder
        .buildFrameIndex(LLT::pointer(0, DL.getPointerSizeInBits(0)), FI)
        .getReg(0);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override {
    MachineFunction &MF = MIRBuilder.getMF();
    auto *MMO = MF.getMachineMemOperand(
        MPO, MachineMemOperand::MOLoad | MachineMemOperand::MOInvariant, MemTy,
        inferAlignFromPtrInfo(MF, MPO));
    MIRBuilder.buildLoad(ValVReg, Addr, *MMO);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override {
    markPhysRegUsed(PhysReg);
    IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA);
  }

  /// How the physical register gets marked varies between formal
  /// parameters (it's a basic-block live-in), and a call instruction
  /// (it's an implicit-def of the BL).
  virtual void markPhysRegUsed(unsigned PhysReg) = 0;

protected:
  const DataLayout &DL;
};

struct FormalArgHandler : public X86IncomingValueHandler {
  FormalArgHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
      : X86IncomingValueHandler(MIRBuilder, MRI) {}

  void markPhysRegUsed(unsigned PhysReg) override {
    MIRBuilder.getMRI()->addLiveIn(PhysReg);
    MIRBuilder.getMBB().addLiveIn(PhysReg);
  }
};

struct CallReturnHandler : public X86IncomingValueHandler {
  CallReturnHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                    MachineInstrBuilder &MIB)
      : X86IncomingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

  void markPhysRegUsed(unsigned PhysReg) override {
    MIB.addDef(PhysReg, RegState::Implicit);
  }

protected:
  MachineInstrBuilder &MIB;
};

} // end anonymous namespace

bool X86CallLowering::lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                           const Function &F,
                                           ArrayRef<ArrayRef<Register>> VRegs,
                                           FunctionLoweringInfo &FLI) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  auto DL = MF.getDataLayout();
  auto FuncInfo = MF.getInfo<X86MachineFunctionInfo>();

  SmallVector<ArgInfo, 8> SplitArgs;

  if (!FLI.CanLowerReturn)
    insertSRetIncomingArgument(F, SplitArgs, FLI.DemoteRegister, MRI, DL);

  // TODO: handle variadic function
  if (F.isVarArg())
    return false;

  unsigned Idx = 0;
  for (const auto &Arg : F.args()) {
    // TODO: handle not simple cases.
    if (Arg.hasAttribute(Attribute::ByVal) ||
        Arg.hasAttribute(Attribute::InReg) ||
        Arg.hasAttribute(Attribute::SwiftSelf) ||
        Arg.hasAttribute(Attribute::SwiftError) ||
        Arg.hasAttribute(Attribute::Nest) || VRegs[Idx].size() > 1)
      return false;

    if (Arg.hasAttribute(Attribute::StructRet)) {
      assert(VRegs[Idx].size() == 1 &&
             "Unexpected amount of registers for sret argument.");
      FuncInfo->setSRetReturnReg(VRegs[Idx][0]);
    }

    ArgInfo OrigArg(VRegs[Idx], Arg.getType(), Idx);
    setArgFlags(OrigArg, Idx + AttributeList::FirstArgIndex, DL, F);
    splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv());
    Idx++;
  }

  if (SplitArgs.empty())
    return true;

  MachineBasicBlock &MBB = MIRBuilder.getMBB();
  if (!MBB.empty())
    MIRBuilder.setInstr(*MBB.begin());

  X86OutgoingValueAssigner Assigner(CC_X86);
  FormalArgHandler Handler(MIRBuilder, MRI);
  if (!determineAndHandleAssignments(Handler, Assigner, SplitArgs, MIRBuilder,
                                     F.getCallingConv(), F.isVarArg()))
    return false;

  // Move back to the end of the basic block.
  MIRBuilder.setMBB(MBB);

  return true;
}

bool X86CallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                CallLoweringInfo &Info) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const DataLayout &DL = F.getDataLayout();
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  const X86RegisterInfo *TRI = STI.getRegisterInfo();

  // Handle only Linux C, X86_64_SysV calling conventions for now.
  if (!STI.isTargetLinux() || !(Info.CallConv == CallingConv::C ||
                                Info.CallConv == CallingConv::X86_64_SysV))
    return false;

  unsigned AdjStackDown = TII.getCallFrameSetupOpcode();
  auto CallSeqStart = MIRBuilder.buildInstr(AdjStackDown);

  // Create a temporarily-floating call instruction so we can add the implicit
  // uses of arg registers.
  bool Is64Bit = STI.is64Bit();
  unsigned CallOpc = Info.Callee.isReg()
                         ? (Is64Bit ? X86::CALL64r : X86::CALL32r)
                         : (Is64Bit ? X86::CALL64pcrel32 : X86::CALLpcrel32);

  auto MIB = MIRBuilder.buildInstrNoInsert(CallOpc)
                 .add(Info.Callee)
                 .addRegMask(TRI->getCallPreservedMask(MF, Info.CallConv));

  SmallVector<ArgInfo, 8> SplitArgs;
  for (const auto &OrigArg : Info.OrigArgs) {

    // TODO: handle not simple cases.
    if (OrigArg.Flags[0].isByVal())
      return false;

    if (OrigArg.Regs.size() > 1)
      return false;

    splitToValueTypes(OrigArg, SplitArgs, DL, Info.CallConv);
  }
  // Do the actual argument marshalling.
  X86OutgoingValueAssigner Assigner(CC_X86);
  X86OutgoingValueHandler Handler(MIRBuilder, MRI, MIB);
  if (!determineAndHandleAssignments(Handler, Assigner, SplitArgs, MIRBuilder,
                                     Info.CallConv, Info.IsVarArg))
    return false;

  bool IsFixed = Info.OrigArgs.empty() ? true : Info.OrigArgs.back().IsFixed;
  if (STI.is64Bit() && !IsFixed && !STI.isCallingConvWin64(Info.CallConv)) {
    // From AMD64 ABI document:
    // For calls that may call functions that use varargs or stdargs
    // (prototype-less calls or calls to functions containing ellipsis (...) in
    // the declaration) %al is used as hidden argument to specify the number
    // of SSE registers used. The contents of %al do not need to match exactly
    // the number of registers, but must be an ubound on the number of SSE
    // registers used and is in the range 0 - 8 inclusive.

    MIRBuilder.buildInstr(X86::MOV8ri)
        .addDef(X86::AL)
        .addImm(Assigner.getNumXmmRegs());
    MIB.addUse(X86::AL, RegState::Implicit);
  }

  // Now we can add the actual call instruction to the correct basic block.
  MIRBuilder.insertInstr(MIB);

  // If Callee is a reg, since it is used by a target specific
  // instruction, it must have a register class matching the
  // constraint of that instruction.
  if (Info.Callee.isReg())
    MIB->getOperand(0).setReg(constrainOperandRegClass(
        MF, *TRI, MRI, *MF.getSubtarget().getInstrInfo(),
        *MF.getSubtarget().getRegBankInfo(), *MIB, MIB->getDesc(), Info.Callee,
        0));

  // Finally we can copy the returned value back into its virtual-register. In
  // symmetry with the arguments, the physical register must be an
  // implicit-define of the call instruction.

  if (Info.CanLowerReturn && !Info.OrigRet.Ty->isVoidTy()) {
    if (Info.OrigRet.Regs.size() > 1)
      return false;

    SplitArgs.clear();
    SmallVector<Register, 8> NewRegs;

    splitToValueTypes(Info.OrigRet, SplitArgs, DL, Info.CallConv);

    X86OutgoingValueAssigner Assigner(RetCC_X86);
    CallReturnHandler Handler(MIRBuilder, MRI, MIB);
    if (!determineAndHandleAssignments(Handler, Assigner, SplitArgs, MIRBuilder,
                                       Info.CallConv, Info.IsVarArg))
      return false;

    if (!NewRegs.empty())
      MIRBuilder.buildMergeLikeInstr(Info.OrigRet.Regs[0], NewRegs);
  }

  CallSeqStart.addImm(Assigner.getStackSize())
      .addImm(0 /* see getFrameTotalSize */)
      .addImm(0 /* see getFrameAdjustment */);

  unsigned AdjStackUp = TII.getCallFrameDestroyOpcode();
  MIRBuilder.buildInstr(AdjStackUp)
      .addImm(Assigner.getStackSize())
      .addImm(0 /* NumBytesForCalleeToPop */);

  if (!Info.CanLowerReturn)
    insertSRetLoads(MIRBuilder, Info.OrigRet.Ty, Info.OrigRet.Regs,
                    Info.DemoteRegister, Info.DemoteStackIndex);

  return true;
}
