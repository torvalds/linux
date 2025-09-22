//===-- ARMTargetMachine.h - Define TargetMachine for ARM -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the ARM specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMTARGETMACHINE_H
#define LLVM_LIB_TARGET_ARM_ARMTARGETMACHINE_H

#include "ARMSubtarget.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Target/TargetMachine.h"
#include <memory>
#include <optional>

namespace llvm {

class ARMBaseTargetMachine : public LLVMTargetMachine {
public:
  enum ARMABI {
    ARM_ABI_UNKNOWN,
    ARM_ABI_APCS,
    ARM_ABI_AAPCS, // ARM EABI
    ARM_ABI_AAPCS16
  } TargetABI;

protected:
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  bool isLittle;
  mutable StringMap<std::unique_ptr<ARMSubtarget>> SubtargetMap;

public:
  ARMBaseTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       std::optional<Reloc::Model> RM,
                       std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                       bool isLittle);
  ~ARMBaseTargetMachine() override;

  const ARMSubtarget *getSubtargetImpl(const Function &F) const override;
  // DO NOT IMPLEMENT: There is no such thing as a valid default subtarget,
  // subtargets are per-function entities based on the target-specific
  // attributes of each function.
  const ARMSubtarget *getSubtargetImpl() const = delete;
  bool isLittleEndian() const { return isLittle; }

  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;

  // Pass Pipeline Configuration
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }

  bool isTargetHardFloat() const {
    return TargetTriple.getEnvironment() == Triple::GNUEABIHF ||
           TargetTriple.getEnvironment() == Triple::GNUEABIHFT64 ||
           TargetTriple.getEnvironment() == Triple::MuslEABIHF ||
           TargetTriple.getEnvironment() == Triple::EABIHF ||
           (TargetTriple.isOSBinFormatMachO() &&
            TargetTriple.getSubArch() == Triple::ARMSubArch_v7em) ||
           TargetTriple.isOSWindows() ||
           TargetABI == ARMBaseTargetMachine::ARM_ABI_AAPCS16;
  }

  bool targetSchedulesPostRAScheduling() const override { return true; };

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;

  /// Returns true if a cast between SrcAS and DestAS is a noop.
  bool isNoopAddrSpaceCast(unsigned SrcAS, unsigned DestAS) const override {
    // Addrspacecasts are always noops.
    return true;
  }

  yaml::MachineFunctionInfo *createDefaultFuncInfoYAML() const override;
  yaml::MachineFunctionInfo *
  convertFuncInfoToYAML(const MachineFunction &MF) const override;
  bool parseMachineFunctionInfo(const yaml::MachineFunctionInfo &,
                                PerFunctionMIParsingState &PFS,
                                SMDiagnostic &Error,
                                SMRange &SourceRange) const override;
};

/// ARM/Thumb little endian target machine.
///
class ARMLETargetMachine : public ARMBaseTargetMachine {
public:
  ARMLETargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                     StringRef FS, const TargetOptions &Options,
                     std::optional<Reloc::Model> RM,
                     std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                     bool JIT);
};

/// ARM/Thumb big endian target machine.
///
class ARMBETargetMachine : public ARMBaseTargetMachine {
public:
  ARMBETargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                     StringRef FS, const TargetOptions &Options,
                     std::optional<Reloc::Model> RM,
                     std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                     bool JIT);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARMTARGETMACHINE_H
