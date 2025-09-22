//===-- PPCCallLowering.h - Call lowering for GlobalISel -------*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_POWERPC_GISEL_PPCCALLLOWERING_H
#define LLVM_LIB_TARGET_POWERPC_GISEL_PPCCALLLOWERING_H

#include "PPCISelLowering.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/IR/CallingConv.h"

namespace llvm {

class PPCTargetLowering;

class PPCCallLowering : public CallLowering {
public:
  PPCCallLowering(const PPCTargetLowering &TLI);

  bool lowerReturn(MachineIRBuilder &MIRBuilder, const Value *Val,
                   ArrayRef<Register> VRegs, FunctionLoweringInfo &FLI,
                   Register SwiftErrorVReg) const override;
  bool lowerFormalArguments(MachineIRBuilder &MIRBuilder, const Function &F,
                            ArrayRef<ArrayRef<Register>> VRegs,
                            FunctionLoweringInfo &FLI) const override;
  bool lowerCall(MachineIRBuilder &MIRBuilder,
                 CallLoweringInfo &Info) const override;
};

class PPCIncomingValueHandler : public CallLowering::IncomingValueHandler {
public:
  PPCIncomingValueHandler(MachineIRBuilder &MIRBuilder,
                          MachineRegisterInfo &MRI)
      : CallLowering::IncomingValueHandler(MIRBuilder, MRI) {}

  uint64_t StackUsed;

private:
  void assignValueToReg(Register ValVReg, Register PhysReg,
                        const CCValAssign &VA) override;

  void assignValueToAddress(Register ValVReg, Register Addr, LLT MemTy,
                            const MachinePointerInfo &MPO,
                            const CCValAssign &VA) override;

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO,
                           ISD::ArgFlagsTy Flags) override;

  virtual void markPhysRegUsed(unsigned PhysReg) = 0;
};

class FormalArgHandler : public PPCIncomingValueHandler {

  void markPhysRegUsed(unsigned PhysReg) override;

public:
  FormalArgHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
      : PPCIncomingValueHandler(MIRBuilder, MRI) {}
};

} // end namespace llvm

#endif
