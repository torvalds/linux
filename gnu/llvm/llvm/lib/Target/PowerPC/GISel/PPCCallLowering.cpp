//===-- PPCCallLowering.h - Call lowering for GlobalISel -------*- C++ -*-===//
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

#include "PPCCallLowering.h"
#include "PPCCallingConv.h"
#include "PPCISelLowering.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "ppc-call-lowering"

using namespace llvm;

namespace {

struct OutgoingArgHandler : public CallLowering::OutgoingValueHandler {
  OutgoingArgHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                     MachineInstrBuilder MIB)
      : OutgoingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override;
  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override;
  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override;

  MachineInstrBuilder MIB;
};
} // namespace

void OutgoingArgHandler::assignValueToReg(Register ValVReg, Register PhysReg,
                                          const CCValAssign &VA) {
  MIB.addUse(PhysReg, RegState::Implicit);
  Register ExtReg = extendRegister(ValVReg, VA);
  MIRBuilder.buildCopy(PhysReg, ExtReg);
}

void OutgoingArgHandler::assignValueToAddress(Register ValVReg, Register Addr,
                                              LLT MemTy,
                                              const MachinePointerInfo &MPO,
                                              const CCValAssign &VA) {
  llvm_unreachable("unimplemented");
}

Register OutgoingArgHandler::getStackAddress(uint64_t Size, int64_t Offset,
                                             MachinePointerInfo &MPO,
                                             ISD::ArgFlagsTy Flags) {
  llvm_unreachable("unimplemented");
}

PPCCallLowering::PPCCallLowering(const PPCTargetLowering &TLI)
    : CallLowering(&TLI) {}

bool PPCCallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                  const Value *Val, ArrayRef<Register> VRegs,
                                  FunctionLoweringInfo &FLI,
                                  Register SwiftErrorVReg) const {
  auto MIB = MIRBuilder.buildInstrNoInsert(PPC::BLR8);
  bool Success = true;
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  auto &DL = F.getDataLayout();
  if (!VRegs.empty()) {
    // Setup the information about the return value.
    ArgInfo OrigArg{VRegs, Val->getType(), 0};
    setArgFlags(OrigArg, AttributeList::ReturnIndex, DL, F);

    // Split the return value into consecutive registers if needed.
    SmallVector<ArgInfo, 8> SplitArgs;
    splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv());

    // Use the calling convention callback to determine type and location of
    // return value.
    OutgoingValueAssigner ArgAssigner(RetCC_PPC);

    // Handler to move the return value into the correct location.
    OutgoingArgHandler ArgHandler(MIRBuilder, MRI, MIB);

    // Iterate over all return values, and move them to the assigned location.
    Success = determineAndHandleAssignments(ArgHandler, ArgAssigner, SplitArgs,
                                            MIRBuilder, F.getCallingConv(),
                                            F.isVarArg());
  }
  MIRBuilder.insertInstr(MIB);
  return Success;
}

bool PPCCallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                CallLoweringInfo &Info) const {
  return false;
}

bool PPCCallLowering::lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                           const Function &F,
                                           ArrayRef<ArrayRef<Register>> VRegs,
                                           FunctionLoweringInfo &FLI) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const auto &DL = F.getDataLayout();
  auto &TLI = *getTLI<PPCTargetLowering>();

  // Loop over each arg, set flags and split to single value types
  SmallVector<ArgInfo, 8> SplitArgs;
  unsigned I = 0;
  for (const auto &Arg : F.args()) {
    if (DL.getTypeStoreSize(Arg.getType()).isZero())
      continue;

    ArgInfo OrigArg{VRegs[I], Arg, I};
    setArgFlags(OrigArg, I + AttributeList::FirstArgIndex, DL, F);
    splitToValueTypes(OrigArg, SplitArgs, DL, F.getCallingConv());
    ++I;
  }

  CCAssignFn *AssignFn =
      TLI.ccAssignFnForCall(F.getCallingConv(), false, F.isVarArg());
  IncomingValueAssigner ArgAssigner(AssignFn);
  FormalArgHandler ArgHandler(MIRBuilder, MRI);
  return determineAndHandleAssignments(ArgHandler, ArgAssigner, SplitArgs,
                                       MIRBuilder, F.getCallingConv(),
                                       F.isVarArg());
}

void PPCIncomingValueHandler::assignValueToReg(Register ValVReg,
                                               Register PhysReg,
                                               const CCValAssign &VA) {
  markPhysRegUsed(PhysReg);
  IncomingValueHandler::assignValueToReg(ValVReg, PhysReg, VA);
}

void PPCIncomingValueHandler::assignValueToAddress(
    Register ValVReg, Register Addr, LLT MemTy, const MachinePointerInfo &MPO,
    const CCValAssign &VA) {
  // define a lambda expression to load value
  auto BuildLoad = [](MachineIRBuilder &MIRBuilder,
                      const MachinePointerInfo &MPO, LLT MemTy,
                      const DstOp &Res, Register Addr) {
    MachineFunction &MF = MIRBuilder.getMF();
    auto *MMO = MF.getMachineMemOperand(MPO, MachineMemOperand::MOLoad, MemTy,
                                        inferAlignFromPtrInfo(MF, MPO));
    return MIRBuilder.buildLoad(Res, Addr, *MMO);
  };

  BuildLoad(MIRBuilder, MPO, MemTy, ValVReg, Addr);
}

Register PPCIncomingValueHandler::getStackAddress(uint64_t Size, int64_t Offset,
                                                  MachinePointerInfo &MPO,
                                                  ISD::ArgFlagsTy Flags) {
  auto &MFI = MIRBuilder.getMF().getFrameInfo();
  const bool IsImmutable = !Flags.isByVal();
  int FI = MFI.CreateFixedObject(Size, Offset, IsImmutable);
  MPO = MachinePointerInfo::getFixedStack(MIRBuilder.getMF(), FI);

  // Build Frame Index based on whether the machine is 32-bit or 64-bit
  llvm::LLT FramePtr = LLT::pointer(
      0, MIRBuilder.getMF().getDataLayout().getPointerSizeInBits());
  MachineInstrBuilder AddrReg = MIRBuilder.buildFrameIndex(FramePtr, FI);
  StackUsed = std::max(StackUsed, Size + Offset);
  return AddrReg.getReg(0);
}

void FormalArgHandler::markPhysRegUsed(unsigned PhysReg) {
  MIRBuilder.getMRI()->addLiveIn(PhysReg);
  MIRBuilder.getMBB().addLiveIn(PhysReg);
}
