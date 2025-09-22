//===- lib/Target/AMDGPU/AMDGPUCallLowering.h - Call lowering -*- C++ -*---===//
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

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUCALLLOWERING_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUCALLLOWERING_H

#include "llvm/CodeGen/GlobalISel/CallLowering.h"

namespace llvm {

class AMDGPUTargetLowering;
class GCNSubtarget;
class MachineInstrBuilder;
class SIMachineFunctionInfo;

class AMDGPUCallLowering final : public CallLowering {
  void lowerParameterPtr(Register DstReg, MachineIRBuilder &B,
                         uint64_t Offset) const;

  void lowerParameter(MachineIRBuilder &B, ArgInfo &AI, uint64_t Offset,
                      Align Alignment) const;

  bool canLowerReturn(MachineFunction &MF, CallingConv::ID CallConv,
                      SmallVectorImpl<BaseArgInfo> &Outs,
                      bool IsVarArg) const override;

  bool lowerReturnVal(MachineIRBuilder &B, const Value *Val,
                      ArrayRef<Register> VRegs, MachineInstrBuilder &Ret) const;

public:
  AMDGPUCallLowering(const AMDGPUTargetLowering &TLI);

  bool lowerReturn(MachineIRBuilder &B, const Value *Val,
                   ArrayRef<Register> VRegs,
                   FunctionLoweringInfo &FLI) const override;

  bool lowerFormalArgumentsKernel(MachineIRBuilder &B, const Function &F,
                                  ArrayRef<ArrayRef<Register>> VRegs) const;

  bool lowerFormalArguments(MachineIRBuilder &B, const Function &F,
                            ArrayRef<ArrayRef<Register>> VRegs,
                            FunctionLoweringInfo &FLI) const override;

  bool passSpecialInputs(MachineIRBuilder &MIRBuilder,
                         CCState &CCInfo,
                         SmallVectorImpl<std::pair<MCRegister, Register>> &ArgRegs,
                         CallLoweringInfo &Info) const;

  bool
  doCallerAndCalleePassArgsTheSameWay(CallLoweringInfo &Info,
                                      MachineFunction &MF,
                                      SmallVectorImpl<ArgInfo> &InArgs) const;

  bool
  areCalleeOutgoingArgsTailCallable(CallLoweringInfo &Info, MachineFunction &MF,
                                    SmallVectorImpl<ArgInfo> &OutArgs) const;

  /// Returns true if the call can be lowered as a tail call.
  bool
  isEligibleForTailCallOptimization(MachineIRBuilder &MIRBuilder,
                                    CallLoweringInfo &Info,
                                    SmallVectorImpl<ArgInfo> &InArgs,
                                    SmallVectorImpl<ArgInfo> &OutArgs) const;

  void handleImplicitCallArguments(
      MachineIRBuilder &MIRBuilder, MachineInstrBuilder &CallInst,
      const GCNSubtarget &ST, const SIMachineFunctionInfo &MFI,
      CallingConv::ID CalleeCC,
      ArrayRef<std::pair<MCRegister, Register>> ImplicitArgRegs) const;

  bool lowerTailCall(MachineIRBuilder &MIRBuilder, CallLoweringInfo &Info,
                     SmallVectorImpl<ArgInfo> &OutArgs) const;
  bool lowerChainCall(MachineIRBuilder &MIRBuilder,
                      CallLoweringInfo &Info) const;
  bool lowerCall(MachineIRBuilder &MIRBuilder,
                 CallLoweringInfo &Info) const override;

  static CCAssignFn *CCAssignFnForCall(CallingConv::ID CC, bool IsVarArg);
  static CCAssignFn *CCAssignFnForReturn(CallingConv::ID CC, bool IsVarArg);
};
} // End of namespace llvm;
#endif
