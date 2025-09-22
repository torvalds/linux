//===-- M68kTargetMachine.h - Define TargetMachine for M68k -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the M68k specific subclass of TargetMachine.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_M68K_M68KTARGETMACHINE_H
#define LLVM_LIB_TARGET_M68K_M68KTARGETMACHINE_H

#include "M68kSubtarget.h"
#include "MCTargetDesc/M68kMCTargetDesc.h"

#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Target/TargetMachine.h"

#include <optional>

namespace llvm {
class formatted_raw_ostream;
class M68kRegisterInfo;

class M68kTargetMachine : public LLVMTargetMachine {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  M68kSubtarget Subtarget;

  mutable StringMap<std::unique_ptr<M68kSubtarget>> SubtargetMap;

public:
  M68kTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                    StringRef FS, const TargetOptions &Options,
                    std::optional<Reloc::Model> RM,
                    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                    bool JIT);

  ~M68kTargetMachine() override;

  const M68kSubtarget *getSubtargetImpl() const { return &Subtarget; }

  const M68kSubtarget *getSubtargetImpl(const Function &F) const override;

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;

  // Pass Pipeline Configuration
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};
} // namespace llvm

#endif // LLVM_LIB_TARGET_M68K_M68KTARGETMACHINE_H
