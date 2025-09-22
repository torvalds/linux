//===-- PPCTargetMachine.h - Define TargetMachine for PowerPC ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the PowerPC specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPCTARGETMACHINE_H
#define LLVM_LIB_TARGET_POWERPC_PPCTARGETMACHINE_H

#include "PPCInstrInfo.h"
#include "PPCSubtarget.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Target/TargetMachine.h"
#include <optional>

namespace llvm {

/// Common code between 32-bit and 64-bit PowerPC targets.
///
class PPCTargetMachine final : public LLVMTargetMachine {
public:
  enum PPCABI { PPC_ABI_UNKNOWN, PPC_ABI_ELFv1, PPC_ABI_ELFv2 };
  enum Endian { NOT_DETECTED, LITTLE, BIG };

private:
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  PPCABI TargetABI;
  Endian Endianness = Endian::NOT_DETECTED;
  mutable bool HasGlibcHWCAPAccess = false;

  mutable StringMap<std::unique_ptr<PPCSubtarget>> SubtargetMap;

public:
  PPCTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                   StringRef FS, const TargetOptions &Options,
                   std::optional<Reloc::Model> RM,
                   std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                   bool JIT);

  ~PPCTargetMachine() override;

  const PPCSubtarget *getSubtargetImpl(const Function &F) const override;
  // DO NOT IMPLEMENT: There is no such thing as a valid default subtarget,
  // subtargets are per-function entities based on the target-specific
  // attributes of each function.
  const PPCSubtarget *getSubtargetImpl() const = delete;

  // Pass Pipeline Configuration
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;

  bool isELFv2ABI() const { return TargetABI == PPC_ABI_ELFv2; }
  bool hasGlibcHWCAPAccess() const { return HasGlibcHWCAPAccess; }
  void setGlibcHWCAPAccess(bool Val = true) const { HasGlibcHWCAPAccess = Val; }
  bool isPPC64() const {
    const Triple &TT = getTargetTriple();
    return (TT.getArch() == Triple::ppc64 || TT.getArch() == Triple::ppc64le);
  };

  bool isNoopAddrSpaceCast(unsigned SrcAS, unsigned DestAS) const override {
    // Addrspacecasts are always noops.
    return true;
  }

  bool isLittleEndian() const;

  int unqualifiedInlineAsmVariant() const override { return 1; }
};
} // end namespace llvm

#endif
