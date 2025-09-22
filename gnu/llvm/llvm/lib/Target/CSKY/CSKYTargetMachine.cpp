//===--- CSKYTargetMachine.cpp - Define TargetMachine for CSKY ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the info about CSKY target spec.
//
//===----------------------------------------------------------------------===//

#include "CSKYTargetMachine.h"
#include "CSKY.h"
#include "CSKYMachineFunctionInfo.h"
#include "CSKYSubtarget.h"
#include "CSKYTargetObjectFile.h"
#include "TargetInfo/CSKYTargetInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeCSKYTarget() {
  RegisterTargetMachine<CSKYTargetMachine> X(getTheCSKYTarget());

  PassRegistry *Registry = PassRegistry::getPassRegistry();
  initializeCSKYConstantIslandsPass(*Registry);
  initializeCSKYDAGToDAGISelLegacyPass(*Registry);
}

static std::string computeDataLayout(const Triple &TT) {
  std::string Ret;

  // Only support little endian for now.
  // TODO: Add support for big endian.
  Ret += "e";

  // CSKY is always 32-bit target with the CSKYv2 ABI as prefer now.
  // It's a 4-byte aligned stack with ELF mangling only.
  Ret += "-m:e-S32-p:32:32-i32:32:32-i64:32:32-f32:32:32-f64:32:32-v64:32:32"
         "-v128:32:32-a:0:32-Fi32-n32";

  return Ret;
}

CSKYTargetMachine::CSKYTargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(TT), TT, CPU, FS, Options,
                        RM.value_or(Reloc::Static),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<CSKYELFTargetObjectFile>()) {
  initAsmInfo();
}

const CSKYSubtarget *
CSKYTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute TuneAttr = F.getFnAttribute("tune-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string TuneCPU =
      TuneAttr.isValid() ? TuneAttr.getValueAsString().str() : CPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  std::string Key = CPU + TuneCPU + FS;
  auto &I = SubtargetMap[Key];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    I = std::make_unique<CSKYSubtarget>(TargetTriple, CPU, TuneCPU, FS, *this);
    if (I->useHardFloat() && !I->hasAnyFloatExt())
      errs() << "Hard-float can't be used with current CPU,"
                " set to Soft-float\n";
  }
  return I.get();
}

MachineFunctionInfo *CSKYTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return CSKYMachineFunctionInfo::create<CSKYMachineFunctionInfo>(Allocator, F,
                                                                  STI);
}

namespace {
class CSKYPassConfig : public TargetPassConfig {
public:
  CSKYPassConfig(CSKYTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  CSKYTargetMachine &getCSKYTargetMachine() const {
    return getTM<CSKYTargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
};

} // namespace

TargetPassConfig *CSKYTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new CSKYPassConfig(*this, PM);
}

void CSKYPassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool CSKYPassConfig::addInstSelector() {
  addPass(createCSKYISelDag(getCSKYTargetMachine(), getOptLevel()));

  return false;
}

void CSKYPassConfig::addPreEmitPass() {
  addPass(createCSKYConstantIslandPass());
}
