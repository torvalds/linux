//===-- MSP430TargetMachine.cpp - Define TargetMachine for MSP430 ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Top-level implementation for the MSP430 target.
//
//===----------------------------------------------------------------------===//

#include "MSP430TargetMachine.h"
#include "MSP430.h"
#include "MSP430MachineFunctionInfo.h"
#include "TargetInfo/MSP430TargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include <optional>
using namespace llvm;

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMSP430Target() {
  // Register the target.
  RegisterTargetMachine<MSP430TargetMachine> X(getTheMSP430Target());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeMSP430DAGToDAGISelLegacyPass(PR);
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

static std::string computeDataLayout(const Triple &TT, StringRef CPU,
                                     const TargetOptions &Options) {
  return "e-m:e-p:16:16-i32:16-i64:16-f32:16-f64:16-a:8-n8:16-S16";
}

MSP430TargetMachine::MSP430TargetMachine(const Target &T, const Triple &TT,
                                         StringRef CPU, StringRef FS,
                                         const TargetOptions &Options,
                                         std::optional<Reloc::Model> RM,
                                         std::optional<CodeModel::Model> CM,
                                         CodeGenOptLevel OL, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(TT, CPU, Options), TT, CPU, FS,
                        Options, getEffectiveRelocModel(RM),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  initAsmInfo();
}

MSP430TargetMachine::~MSP430TargetMachine() = default;

namespace {
/// MSP430 Code Generator Pass Configuration Options.
class MSP430PassConfig : public TargetPassConfig {
public:
  MSP430PassConfig(MSP430TargetMachine &TM, PassManagerBase &PM)
    : TargetPassConfig(TM, PM) {}

  MSP430TargetMachine &getMSP430TargetMachine() const {
    return getTM<MSP430TargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *MSP430TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new MSP430PassConfig(*this, PM);
}

MachineFunctionInfo *MSP430TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return MSP430MachineFunctionInfo::create<MSP430MachineFunctionInfo>(Allocator,
                                                                      F, STI);
}

void MSP430PassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());

  TargetPassConfig::addIRPasses();
}

bool MSP430PassConfig::addInstSelector() {
  // Install an instruction selector.
  addPass(createMSP430ISelDag(getMSP430TargetMachine(), getOptLevel()));
  return false;
}

void MSP430PassConfig::addPreEmitPass() {
  // Must run branch selection immediately preceding the asm printer.
  addPass(createMSP430BranchSelectionPass());
}
