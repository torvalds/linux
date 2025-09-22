//===-- NVPTXTargetMachine.h - Define TargetMachine for NVPTX ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the NVPTX specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXTARGETMACHINE_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXTARGETMACHINE_H

#include "NVPTXSubtarget.h"
#include "llvm/Target/TargetMachine.h"
#include <optional>
#include <utility>

namespace llvm {

/// NVPTXTargetMachine
///
class NVPTXTargetMachine : public LLVMTargetMachine {
  bool is64bit;
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  NVPTX::DrvInterface drvInterface;
  NVPTXSubtarget Subtarget;

  // Hold Strings that can be free'd all together with NVPTXTargetMachine
  BumpPtrAllocator StrAlloc;
  UniqueStringSaver StrPool;

public:
  NVPTXTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                     StringRef FS, const TargetOptions &Options,
                     std::optional<Reloc::Model> RM,
                     std::optional<CodeModel::Model> CM, CodeGenOptLevel OP,
                     bool is64bit);
  ~NVPTXTargetMachine() override;
  const NVPTXSubtarget *getSubtargetImpl(const Function &) const override {
    return &Subtarget;
  }
  const NVPTXSubtarget *getSubtargetImpl() const { return &Subtarget; }
  bool is64Bit() const { return is64bit; }
  NVPTX::DrvInterface getDrvInterface() const { return drvInterface; }
  UniqueStringSaver &getStrPool() const {
    return const_cast<UniqueStringSaver &>(StrPool);
  }

  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  // Emission of machine code through MCJIT is not supported.
  bool addPassesToEmitMC(PassManagerBase &, MCContext *&, raw_pwrite_stream &,
                         bool = true) override {
    return true;
  }
  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;

  void registerDefaultAliasAnalyses(AAManager &AAM) override;

  void registerPassBuilderCallbacks(PassBuilder &PB) override;

  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;

  bool isMachineVerifierClean() const override {
    return false;
  }

  std::pair<const Value *, unsigned>
  getPredicatedAddrSpace(const Value *V) const override;
}; // NVPTXTargetMachine.

class NVPTXTargetMachine32 : public NVPTXTargetMachine {
  virtual void anchor();

public:
  NVPTXTargetMachine32(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       std::optional<Reloc::Model> RM,
                       std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                       bool JIT);
};

class NVPTXTargetMachine64 : public NVPTXTargetMachine {
  virtual void anchor();

public:
  NVPTXTargetMachine64(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       std::optional<Reloc::Model> RM,
                       std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                       bool JIT);
};

} // end namespace llvm

#endif
