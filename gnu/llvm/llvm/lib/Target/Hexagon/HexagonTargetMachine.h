//=-- HexagonTargetMachine.h - Define TargetMachine for Hexagon ---*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Hexagon specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONTARGETMACHINE_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONTARGETMACHINE_H

#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "HexagonTargetObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include <optional>

namespace llvm {

class HexagonTargetMachine : public LLVMTargetMachine {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  HexagonSubtarget Subtarget;
  mutable StringMap<std::unique_ptr<HexagonSubtarget>> SubtargetMap;

public:
  HexagonTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       std::optional<Reloc::Model> RM,
                       std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                       bool JIT);
  ~HexagonTargetMachine() override;
  const HexagonSubtarget *getSubtargetImpl(const Function &F) const override;

  void registerPassBuilderCallbacks(PassBuilder &PB) override;
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;
  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;

  HexagonTargetObjectFile *getObjFileLowering() const override {
    return static_cast<HexagonTargetObjectFile*>(TLOF.get());
  }

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;
};

} // end namespace llvm

#endif
