//===-- LanaiTargetMachine.cpp - Define TargetMachine for Lanai ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the info about Lanai target spec.
//
//===----------------------------------------------------------------------===//

#include "LanaiTargetMachine.h"

#include "Lanai.h"
#include "LanaiMachineFunctionInfo.h"
#include "LanaiTargetObjectFile.h"
#include "LanaiTargetTransformInfo.h"
#include "TargetInfo/LanaiTargetInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Target/TargetOptions.h"
#include <optional>

using namespace llvm;

namespace llvm {
void initializeLanaiMemAluCombinerPass(PassRegistry &);
} // namespace llvm

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLanaiTarget() {
  // Register the target.
  RegisterTargetMachine<LanaiTargetMachine> registered_target(
      getTheLanaiTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeLanaiDAGToDAGISelLegacyPass(PR);
}

static std::string computeDataLayout() {
  // Data layout (keep in sync with clang/lib/Basic/Targets.cpp)
  return "E"        // Big endian
         "-m:e"     // ELF name manging
         "-p:32:32" // 32-bit pointers, 32 bit aligned
         "-i64:64"  // 64 bit integers, 64 bit aligned
         "-a:0:32"  // 32 bit alignment of objects of aggregate type
         "-n32"     // 32 bit native integer width
         "-S64";    // 64 bit natural stack alignment
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::PIC_);
}

LanaiTargetMachine::LanaiTargetMachine(
    const Target &T, const Triple &TT, StringRef Cpu, StringRef FeatureString,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CodeModel, CodeGenOptLevel OptLevel,
    bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(), TT, Cpu, FeatureString, Options,
                        getEffectiveRelocModel(RM),
                        getEffectiveCodeModel(CodeModel, CodeModel::Medium),
                        OptLevel),
      Subtarget(TT, Cpu, FeatureString, *this, Options, getCodeModel(),
                OptLevel),
      TLOF(new LanaiTargetObjectFile()) {
  initAsmInfo();
}

TargetTransformInfo
LanaiTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(LanaiTTIImpl(this, F));
}

MachineFunctionInfo *LanaiTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return LanaiMachineFunctionInfo::create<LanaiMachineFunctionInfo>(Allocator,
                                                                    F, STI);
}

namespace {
// Lanai Code Generator Pass Configuration Options.
class LanaiPassConfig : public TargetPassConfig {
public:
  LanaiPassConfig(LanaiTargetMachine &TM, PassManagerBase *PassManager)
      : TargetPassConfig(TM, *PassManager) {}

  LanaiTargetMachine &getLanaiTargetMachine() const {
    return getTM<LanaiTargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *
LanaiTargetMachine::createPassConfig(PassManagerBase &PassManager) {
  return new LanaiPassConfig(*this, &PassManager);
}

void LanaiPassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());

  TargetPassConfig::addIRPasses();
}

// Install an instruction selector pass.
bool LanaiPassConfig::addInstSelector() {
  addPass(createLanaiISelDag(getLanaiTargetMachine()));
  return false;
}

// Implemented by targets that want to run passes immediately before
// machine code is emitted.
void LanaiPassConfig::addPreEmitPass() {
  addPass(createLanaiDelaySlotFillerPass(getLanaiTargetMachine()));
}

// Run passes after prolog-epilog insertion and before the second instruction
// scheduling pass.
void LanaiPassConfig::addPreSched2() {
  addPass(createLanaiMemAluCombinerPass());
}
