//===-- SPIRVTargetMachine.h - Define TargetMachine for SPIR-V -*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SPIR-V specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRVTARGETMACHINE_H
#define LLVM_LIB_TARGET_SPIRV_SPIRVTARGETMACHINE_H

#include "SPIRVSubtarget.h"
#include "llvm/Target/TargetMachine.h"
#include <optional>

namespace llvm {
class SPIRVTargetMachine : public LLVMTargetMachine {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  SPIRVSubtarget Subtarget;

public:
  SPIRVTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                     StringRef FS, const TargetOptions &Options,
                     std::optional<Reloc::Model> RM,
                     std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                     bool JIT);

  const SPIRVSubtarget *getSubtargetImpl() const { return &Subtarget; }

  const SPIRVSubtarget *getSubtargetImpl(const Function &) const override {
    return &Subtarget;
  }

  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;

  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;
  bool usesPhysRegsForValues() const override { return false; }

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};
} // namespace llvm

#endif // LLVM_LIB_TARGET_SPIRV_SPIRVTARGETMACHINE_H
