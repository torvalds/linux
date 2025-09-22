//===- llvm/lib/Target/ARM/ARMCallLowering.h - Call lowering ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file describes how to lower LLVM calls to machine code calls.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMCALLLOWERING_H
#define LLVM_LIB_TARGET_ARM_ARMCALLLOWERING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/IR/CallingConv.h"
#include <cstdint>
#include <functional>

namespace llvm {

class ARMTargetLowering;
class MachineInstrBuilder;
class MachineIRBuilder;
class Value;

class ARMCallLowering : public CallLowering {
public:
  ARMCallLowering(const ARMTargetLowering &TLI);

  bool lowerReturn(MachineIRBuilder &MIRBuilder, const Value *Val,
                   ArrayRef<Register> VRegs,
                   FunctionLoweringInfo &FLI) const override;

  bool lowerFormalArguments(MachineIRBuilder &MIRBuilder, const Function &F,
                            ArrayRef<ArrayRef<Register>> VRegs,
                            FunctionLoweringInfo &FLI) const override;

  bool lowerCall(MachineIRBuilder &MIRBuilder,
                 CallLoweringInfo &Info) const override;

  bool enableBigEndian() const override;

private:
  bool lowerReturnVal(MachineIRBuilder &MIRBuilder, const Value *Val,
                      ArrayRef<Register> VRegs,
                      MachineInstrBuilder &Ret) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARMCALLLOWERING_H
