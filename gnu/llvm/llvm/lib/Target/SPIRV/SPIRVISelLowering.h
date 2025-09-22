//===-- SPIRVISelLowering.h - SPIR-V DAG Lowering Interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that SPIR-V uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRVISELLOWERING_H
#define LLVM_LIB_TARGET_SPIRV_SPIRVISELLOWERING_H

#include "SPIRVGlobalRegistry.h"
#include "llvm/CodeGen/TargetLowering.h"
#include <set>

namespace llvm {
class SPIRVSubtarget;

class SPIRVTargetLowering : public TargetLowering {
  const SPIRVSubtarget &STI;

  // Record of already processed machine functions
  mutable std::set<const MachineFunction *> ProcessedMF;

public:
  explicit SPIRVTargetLowering(const TargetMachine &TM,
                               const SPIRVSubtarget &ST)
      : TargetLowering(TM), STI(ST) {}

  // Stop IRTranslator breaking up FMA instrs to preserve types information.
  bool isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                  EVT) const override {
    return true;
  }

  // prevent creation of jump tables
  bool areJTsAllowed(const Function *) const override { return false; }

  // This is to prevent sexts of non-i64 vector indices which are generated
  // within general IRTranslator hence type generation for it is omitted.
  MVT getVectorIdxTy(const DataLayout &DL) const override {
    return MVT::getIntegerVT(32);
  }
  unsigned getNumRegistersForCallingConv(LLVMContext &Context,
                                         CallingConv::ID CC,
                                         EVT VT) const override;
  MVT getRegisterTypeForCallingConv(LLVMContext &Context, CallingConv::ID CC,
                                    EVT VT) const override;
  bool getTgtMemIntrinsic(IntrinsicInfo &Info, const CallInst &I,
                          MachineFunction &MF,
                          unsigned Intrinsic) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;
  unsigned
  getNumRegisters(LLVMContext &Context, EVT VT,
                  std::optional<MVT> RegisterVT = std::nullopt) const override {
    return 1;
  }

  // Call the default implementation and finalize target lowering by inserting
  // extra instructions required to preserve validity of SPIR-V code imposed by
  // the standard.
  void finalizeLowering(MachineFunction &MF) const override;

  MVT getPreferredSwitchConditionType(LLVMContext &Context,
                                      EVT ConditionVT) const override {
    return ConditionVT.getSimpleVT();
  }
};
} // namespace llvm

#endif // LLVM_LIB_TARGET_SPIRV_SPIRVISELLOWERING_H
