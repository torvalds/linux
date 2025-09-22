//===-- M68kCallLowering.cpp - Call lowering --------------------*- C++ -*-===//
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

#include "M68kCallLowering.h"
#include "M68kISelLowering.h"
#include "M68kInstrInfo.h"
#include "M68kSubtarget.h"
#include "M68kTargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/TargetCallingConv.h"

using namespace llvm;

namespace {

struct M68kFormalArgHandler : public M68kIncomingValueHandler {
  M68kFormalArgHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
      : M68kIncomingValueHandler(MIRBuilder, MRI) {}
};

struct CallReturnHandler : public M68kIncomingValueHandler {
  CallReturnHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                    MachineInstrBuilder &MIB)
      : M68kIncomingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

private:
  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override;

  MachineInstrBuilder &MIB;
};

} // end anonymous namespace

M68kCallLowering::M68kCallLowering(const M68kTargetLowering &TLI)
    : CallLowering(&TLI) {}

struct M68kOutgoingArgHandler : public CallLowering::OutgoingValueHandler {
  M68kOutgoingArgHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                         MachineInstrBuilder MIB)
      : OutgoingValueHandler(MIRBuilder, MRI), MIB(MIB),
        DL(MIRBuilder.getMF().getDataLayout()),
        STI(MIRBuilder.getMF().getSubtarget<M68kSubtarget>()) {}

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

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override {
    LLT p0 = LLT::pointer(0, DL.getPointerSizeInBits(0));
    LLT SType = LLT::scalar(DL.getPointerSizeInBits(0));
    Register StackReg = STI.getRegisterInfo()->getStackRegister();
    auto SPReg = MIRBuilder.buildCopy(p0, StackReg).getReg(0);
    auto OffsetReg = MIRBuilder.buildConstant(SType, Offset);
    auto AddrReg = MIRBuilder.buildPtrAdd(p0, SPReg, OffsetReg);
    MPO = MachinePointerInfo::getStack(MIRBuilder.getMF(), Offset);
    return AddrReg.getReg(0);
  }
  MachineInstrBuilder MIB;
  const DataLayout &DL;
  const M68kSubtarget &STI;
};
bool M68kCallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                   const Value *Val, ArrayRef<Register> VRegs,
                                   FunctionLoweringInfo &FLI,
                                   Register SwiftErrorVReg) const {

  auto MIB = MIRBuilder.buildInstrNoInsert(M68k::RTS);
  bool Success = true;
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const M68kTargetLowering &TLI = *getTLI<M68kTargetLowering>();
  CCAssignFn *AssignFn =
      TLI.getCCAssignFn(F.getCallingConv(), true, F.isVarArg());
  auto &DL = F.getDataLayout();
  if (!VRegs.empty()) {
    SmallVector<ArgInfo, 8> SplitArgs;
    ArgInfo OrigArg{VRegs, Val->getType(), 0};
    setArgFlags(OrigArg, AttributeList::ReturnIndex, DL, F);
    splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv());
    OutgoingValueAssigner ArgAssigner(AssignFn);
    M68kOutgoingArgHandler ArgHandler(MIRBuilder, MRI, MIB);
    Success = determineAndHandleAssignments(ArgHandler, ArgAssigner, SplitArgs,
                                            MIRBuilder, F.getCallingConv(),
                                            F.isVarArg());
  }
  MIRBuilder.insertInstr(MIB);
  return Success;
}

bool M68kCallLowering::lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                            const Function &F,
                                            ArrayRef<ArrayRef<Register>> VRegs,
                                            FunctionLoweringInfo &FLI) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const auto &DL = F.getDataLayout();
  auto &TLI = *getTLI<M68kTargetLowering>();

  SmallVector<ArgInfo, 8> SplitArgs;
  unsigned I = 0;
  for (const auto &Arg : F.args()) {
    ArgInfo OrigArg{VRegs[I], Arg.getType(), I};
    setArgFlags(OrigArg, I + AttributeList::FirstArgIndex, DL, F);
    splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv());
    ++I;
  }

  CCAssignFn *AssignFn =
      TLI.getCCAssignFn(F.getCallingConv(), false, F.isVarArg());
  IncomingValueAssigner ArgAssigner(AssignFn);
  M68kFormalArgHandler ArgHandler(MIRBuilder, MRI);
  return determineAndHandleAssignments(ArgHandler, ArgAssigner, SplitArgs,
                                       MIRBuilder, F.getCallingConv(),
                                       F.isVarArg());
}

void M68kIncomingValueHandler::assignValueToReg(Register ValVReg,
                                                Register PhysReg,
                                                const CCValAssign &VA) {
  MIRBuilder.getMRI()->addLiveIn(PhysReg);
  MIRBuilder.getMBB().addLiveIn(PhysReg);
  IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA);
}

void M68kIncomingValueHandler::assignValueToAddress(
    Register ValVReg, Register Addr, LLT MemTy, const MachinePointerInfo &MPO,
    const CCValAssign &VA) {
  MachineFunction &MF = MIRBuilder.getMF();
  auto *MMO = MF.getMachineMemOperand(MPO, MachineMemOperand::MOLoad, MemTy,
                                      inferAlignFromPtrInfo(MF, MPO));
  MIRBuilder.buildLoad(ValVReg, Addr, *MMO);
}

Register M68kIncomingValueHandler::getStackAddress(uint64_t Size,
                                                   int64_t Offset,
                                                   MachinePointerInfo &MPO,
                                                   ISD::ArgFlagsTy Flags) {
  auto &MFI = MIRBuilder.getMF().getFrameInfo();
  const bool IsImmutable = !Flags.isByVal();
  int FI = MFI.CreateFixedObject(Size, Offset, IsImmutable);
  MPO = MachinePointerInfo::getFixedStack(MIRBuilder.getMF(), FI);

  // Build Frame Index
  llvm::LLT FramePtr = LLT::pointer(
      0, MIRBuilder.getMF().getDataLayout().getPointerSizeInBits());
  MachineInstrBuilder AddrReg = MIRBuilder.buildFrameIndex(FramePtr, FI);
  StackUsed = std::max(StackUsed, Size + Offset);
  return AddrReg.getReg(0);
}

void CallReturnHandler::assignValueToReg(Register ValVReg, Register PhysReg,
                                         const CCValAssign &VA) {
  MIB.addDef(PhysReg, RegState::Implicit);
  MIRBuilder.buildCopy(ValVReg, PhysReg);
}

bool M68kCallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                 CallLoweringInfo &Info) const {
  MachineFunction &MF = MIRBuilder.getMF();
  Function &F = MF.getFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  auto &DL = F.getDataLayout();
  const M68kTargetLowering &TLI = *getTLI<M68kTargetLowering>();
  const M68kSubtarget &STI = MF.getSubtarget<M68kSubtarget>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  const M68kRegisterInfo *TRI = STI.getRegisterInfo();

  SmallVector<ArgInfo, 8> OutArgs;
  for (auto &OrigArg : Info.OrigArgs)
    splitToValueTypes(OrigArg, OutArgs, DL, Info.CallConv);

  SmallVector<ArgInfo, 8> InArgs;
  if (!Info.OrigRet.Ty->isVoidTy())
    splitToValueTypes(Info.OrigRet, InArgs, DL, Info.CallConv);

  unsigned AdjStackDown = TII.getCallFrameSetupOpcode();
  auto CallSeqStart = MIRBuilder.buildInstr(AdjStackDown);

  unsigned Opc = TLI.getTargetMachine().isPositionIndependent() ? M68k::CALLq
                 : Info.Callee.isReg()                          ? M68k::CALLj
                                                                : M68k::CALLb;

  auto MIB = MIRBuilder.buildInstrNoInsert(Opc)
                 .add(Info.Callee)
                 .addRegMask(TRI->getCallPreservedMask(MF, Info.CallConv));

  CCAssignFn *AssignFn = TLI.getCCAssignFn(Info.CallConv, false, Info.IsVarArg);
  OutgoingValueAssigner Assigner(AssignFn);
  M68kOutgoingArgHandler Handler(MIRBuilder, MRI, MIB);
  if (!determineAndHandleAssignments(Handler, Assigner, OutArgs, MIRBuilder,
                                     Info.CallConv, Info.IsVarArg))
    return false;

  if (Info.Callee.isReg())
    constrainOperandRegClass(MF, *TRI, MRI, *STI.getInstrInfo(),
                             *STI.getRegBankInfo(), *MIB, MIB->getDesc(),
                             Info.Callee, 0);

  MIRBuilder.insertInstr(MIB);

  if (!Info.OrigRet.Ty->isVoidTy()) {
    CCAssignFn *RetAssignFn =
        TLI.getCCAssignFn(Info.CallConv, true, Info.IsVarArg);

    OutgoingValueAssigner Assigner(RetAssignFn, RetAssignFn);
    CallReturnHandler Handler(MIRBuilder, MRI, MIB);
    if (!determineAndHandleAssignments(Handler, Assigner, InArgs, MIRBuilder,
                                       Info.CallConv, Info.IsVarArg))
      return false;
  }

  CallSeqStart.addImm(Assigner.StackSize).addImm(0);

  unsigned AdjStackUp = TII.getCallFrameDestroyOpcode();
  MIRBuilder.buildInstr(AdjStackUp).addImm(Assigner.StackSize).addImm(0);

  return true;
}

bool M68kCallLowering::enableBigEndian() const { return true; }
