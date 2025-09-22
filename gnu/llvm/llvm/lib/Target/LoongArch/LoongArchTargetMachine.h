//=- LoongArchTargetMachine.h - Define TargetMachine for LoongArch -*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the LoongArch specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LOONGARCH_LOONGARCHTARGETMACHINE_H
#define LLVM_LIB_TARGET_LOONGARCH_LOONGARCHTARGETMACHINE_H

#include "LoongArchSubtarget.h"
#include "llvm/Target/TargetMachine.h"
#include <optional>

namespace llvm {

class LoongArchTargetMachine : public LLVMTargetMachine {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  mutable StringMap<std::unique_ptr<LoongArchSubtarget>> SubtargetMap;

public:
  LoongArchTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                         StringRef FS, const TargetOptions &Options,
                         std::optional<Reloc::Model> RM,
                         std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                         bool JIT);
  ~LoongArchTargetMachine() override;

  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;
  const LoongArchSubtarget *getSubtargetImpl(const Function &F) const override;
  const LoongArchSubtarget *getSubtargetImpl() const = delete;

  // Pass Pipeline Configuration
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;

  // Addrspacecasts are always noops.
  bool isNoopAddrSpaceCast(unsigned SrcAS, unsigned DestAS) const override {
    return true;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_LOONGARCH_LOONGARCHTARGETMACHINE_H
