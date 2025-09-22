//===-- LoongArchTargetMachine.cpp - Define TargetMachine for LoongArch ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the info about LoongArch target spec.
//
//===----------------------------------------------------------------------===//

#include "LoongArchTargetMachine.h"
#include "LoongArch.h"
#include "LoongArchMachineFunctionInfo.h"
#include "LoongArchTargetTransformInfo.h"
#include "MCTargetDesc/LoongArchBaseInfo.h"
#include "TargetInfo/LoongArchTargetInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Transforms/Scalar.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "loongarch"

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeLoongArchTarget() {
  // Register the target.
  RegisterTargetMachine<LoongArchTargetMachine> X(getTheLoongArch32Target());
  RegisterTargetMachine<LoongArchTargetMachine> Y(getTheLoongArch64Target());
  auto *PR = PassRegistry::getPassRegistry();
  initializeLoongArchDeadRegisterDefinitionsPass(*PR);
  initializeLoongArchOptWInstrsPass(*PR);
  initializeLoongArchPreRAExpandPseudoPass(*PR);
  initializeLoongArchDAGToDAGISelLegacyPass(*PR);
}

static cl::opt<bool> EnableLoongArchDeadRegisterElimination(
    "loongarch-enable-dead-defs", cl::Hidden,
    cl::desc("Enable the pass that removes dead"
             " definitons and replaces stores to"
             " them with stores to r0"),
    cl::init(true));

static cl::opt<bool>
    EnableLoopDataPrefetch("loongarch-enable-loop-data-prefetch", cl::Hidden,
                           cl::desc("Enable the loop data prefetch pass"),
                           cl::init(false));

static std::string computeDataLayout(const Triple &TT) {
  if (TT.isArch64Bit())
    return "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128";
  assert(TT.isArch32Bit() && "only LA32 and LA64 are currently supported");
  return "e-m:e-p:32:32-i64:64-n32-S128";
}

static Reloc::Model getEffectiveRelocModel(const Triple &TT,
                                           std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

static CodeModel::Model
getEffectiveLoongArchCodeModel(const Triple &TT,
                               std::optional<CodeModel::Model> CM) {
  if (!CM)
    return CodeModel::Small;

  switch (*CM) {
  case CodeModel::Small:
    return *CM;
  case CodeModel::Medium:
  case CodeModel::Large:
    if (!TT.isArch64Bit())
      report_fatal_error("Medium/Large code model requires LA64");
    return *CM;
  default:
    report_fatal_error(
        "Only small, medium and large code models are allowed on LoongArch");
  }
}

LoongArchTargetMachine::LoongArchTargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(TT), TT, CPU, FS, Options,
                        getEffectiveRelocModel(TT, RM),
                        getEffectiveLoongArchCodeModel(TT, CM), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()) {
  initAsmInfo();
}

LoongArchTargetMachine::~LoongArchTargetMachine() = default;

const LoongArchSubtarget *
LoongArchTargetMachine::getSubtargetImpl(const Function &F) const {
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
    auto ABIName = Options.MCOptions.getABIName();
    if (const MDString *ModuleTargetABI = dyn_cast_or_null<MDString>(
            F.getParent()->getModuleFlag("target-abi"))) {
      auto TargetABI = LoongArchABI::getTargetABI(ABIName);
      if (TargetABI != LoongArchABI::ABI_Unknown &&
          ModuleTargetABI->getString() != ABIName) {
        report_fatal_error("-target-abi option != target-abi module flag");
      }
      ABIName = ModuleTargetABI->getString();
    }
    I = std::make_unique<LoongArchSubtarget>(TargetTriple, CPU, TuneCPU, FS,
                                             ABIName, *this);
  }
  return I.get();
}

MachineFunctionInfo *LoongArchTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return LoongArchMachineFunctionInfo::create<LoongArchMachineFunctionInfo>(
      Allocator, F, STI);
}

namespace {
class LoongArchPassConfig : public TargetPassConfig {
public:
  LoongArchPassConfig(LoongArchTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  LoongArchTargetMachine &getLoongArchTargetMachine() const {
    return getTM<LoongArchTargetMachine>();
  }

  void addIRPasses() override;
  void addCodeGenPrepare() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
  void addPreEmitPass2() override;
  void addMachineSSAOptimization() override;
  void addPreRegAlloc() override;
  bool addRegAssignAndRewriteFast() override;
  bool addRegAssignAndRewriteOptimized() override;
};
} // end namespace

TargetPassConfig *
LoongArchTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new LoongArchPassConfig(*this, PM);
}

void LoongArchPassConfig::addIRPasses() {
  // Run LoopDataPrefetch
  //
  // Run this before LSR to remove the multiplies involved in computing the
  // pointer values N iterations ahead.
  if (TM->getOptLevel() != CodeGenOptLevel::None && EnableLoopDataPrefetch)
    addPass(createLoopDataPrefetchPass());
  addPass(createAtomicExpandLegacyPass());

  TargetPassConfig::addIRPasses();
}

void LoongArchPassConfig::addCodeGenPrepare() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createTypePromotionLegacyPass());
  TargetPassConfig::addCodeGenPrepare();
}

bool LoongArchPassConfig::addInstSelector() {
  addPass(createLoongArchISelDag(getLoongArchTargetMachine()));

  return false;
}

TargetTransformInfo
LoongArchTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(LoongArchTTIImpl(this, F));
}

void LoongArchPassConfig::addPreEmitPass() { addPass(&BranchRelaxationPassID); }

void LoongArchPassConfig::addPreEmitPass2() {
  addPass(createLoongArchExpandPseudoPass());
  // Schedule the expansion of AtomicPseudos at the last possible moment,
  // avoiding the possibility for other passes to break the requirements for
  // forward progress in the LL/SC block.
  addPass(createLoongArchExpandAtomicPseudoPass());
}

void LoongArchPassConfig::addMachineSSAOptimization() {
  TargetPassConfig::addMachineSSAOptimization();

  if (TM->getTargetTriple().isLoongArch64()) {
    addPass(createLoongArchOptWInstrsPass());
  }
}

void LoongArchPassConfig::addPreRegAlloc() {
  addPass(createLoongArchPreRAExpandPseudoPass());
}

bool LoongArchPassConfig::addRegAssignAndRewriteFast() {
  if (TM->getOptLevel() != CodeGenOptLevel::None &&
      EnableLoongArchDeadRegisterElimination)
    addPass(createLoongArchDeadRegisterDefinitionsPass());
  return TargetPassConfig::addRegAssignAndRewriteFast();
}

bool LoongArchPassConfig::addRegAssignAndRewriteOptimized() {
  if (TM->getOptLevel() != CodeGenOptLevel::None &&
      EnableLoongArchDeadRegisterElimination)
    addPass(createLoongArchDeadRegisterDefinitionsPass());
  return TargetPassConfig::addRegAssignAndRewriteOptimized();
}
