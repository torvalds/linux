//===- ARCTargetMachine.cpp - Define TargetMachine for ARC ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "ARCTargetMachine.h"
#include "ARC.h"
#include "ARCMachineFunctionInfo.h"
#include "ARCTargetTransformInfo.h"
#include "TargetInfo/ARCTargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include <optional>

using namespace llvm;

static Reloc::Model getRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

/// ARCTargetMachine ctor - Create an ILP32 architecture model
ARCTargetMachine::ARCTargetMachine(const Target &T, const Triple &TT,
                                   StringRef CPU, StringRef FS,
                                   const TargetOptions &Options,
                                   std::optional<Reloc::Model> RM,
                                   std::optional<CodeModel::Model> CM,
                                   CodeGenOptLevel OL, bool JIT)
    : LLVMTargetMachine(T,
                        "e-m:e-p:32:32-i1:8:32-i8:8:32-i16:16:32-i32:32:32-"
                        "f32:32:32-i64:32-f64:32-a:0:32-n32",
                        TT, CPU, FS, Options, getRelocModel(RM),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  initAsmInfo();
}

ARCTargetMachine::~ARCTargetMachine() = default;

namespace {

/// ARC Code Generator Pass Configuration Options.
class ARCPassConfig : public TargetPassConfig {
public:
  ARCPassConfig(ARCTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  ARCTargetMachine &getARCTargetMachine() const {
    return getTM<ARCTargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
  void addPreRegAlloc() override;
};

} // end anonymous namespace

TargetPassConfig *ARCTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new ARCPassConfig(*this, PM);
}

void ARCPassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());

  TargetPassConfig::addIRPasses();
}

bool ARCPassConfig::addInstSelector() {
  addPass(createARCISelDag(getARCTargetMachine(), getOptLevel()));
  return false;
}

void ARCPassConfig::addPreEmitPass() { addPass(createARCBranchFinalizePass()); }

void ARCPassConfig::addPreRegAlloc() {
    addPass(createARCExpandPseudosPass());
    addPass(createARCOptAddrMode());
}

MachineFunctionInfo *ARCTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
    return ARCFunctionInfo::create<ARCFunctionInfo>(Allocator, F, STI);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeARCTarget() {
  RegisterTargetMachine<ARCTargetMachine> X(getTheARCTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeARCDAGToDAGISelLegacyPass(PR);
}

TargetTransformInfo
ARCTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(ARCTTIImpl(this, F));
}
