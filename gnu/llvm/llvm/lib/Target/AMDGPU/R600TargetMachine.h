//===-- R600TargetMachine.h - AMDGPU TargetMachine Interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// The AMDGPU TargetMachine interface definition for hw codegen targets.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600TARGETMACHINE_H
#define LLVM_LIB_TARGET_AMDGPU_R600TARGETMACHINE_H

#include "AMDGPUTargetMachine.h"
#include "R600Subtarget.h"
#include "llvm/Target/TargetMachine.h"
#include <optional>

namespace llvm {

//===----------------------------------------------------------------------===//
// R600 Target Machine (R600 -> Cayman)
//===----------------------------------------------------------------------===//

class R600TargetMachine final : public AMDGPUTargetMachine {
private:
  mutable StringMap<std::unique_ptr<R600Subtarget>> SubtargetMap;

public:
  R600TargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                    StringRef FS, const TargetOptions &Options,
                    std::optional<Reloc::Model> RM,
                    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                    bool JIT);

  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  Error buildCodeGenPipeline(ModulePassManager &MPM, raw_pwrite_stream &Out,
                             raw_pwrite_stream *DwoOut,
                             CodeGenFileType FileType,
                             const CGPassBuilderOption &Opt,
                             PassInstrumentationCallbacks *PIC) override;

  const TargetSubtargetInfo *getSubtargetImpl(const Function &) const override;

  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;

  bool isMachineVerifierClean() const override { return false; }

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_R600TARGETMACHINE_H
