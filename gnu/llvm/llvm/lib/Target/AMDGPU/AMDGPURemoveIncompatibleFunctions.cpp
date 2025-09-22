//===-- AMDGPURemoveIncompatibleFunctions.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass replaces all uses of functions that use GPU features
/// incompatible with the current GPU with null then deletes the function.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "amdgpu-remove-incompatible-functions"

using namespace llvm;

namespace llvm {
extern const SubtargetFeatureKV
    AMDGPUFeatureKV[AMDGPU::NumSubtargetFeatures - 1];
} // namespace llvm

namespace {

using Generation = AMDGPUSubtarget::Generation;

class AMDGPURemoveIncompatibleFunctions : public ModulePass {
public:
  static char ID;

  AMDGPURemoveIncompatibleFunctions(const TargetMachine *TM = nullptr)
      : ModulePass(ID), TM(TM) {
    assert(TM && "No TargetMachine!");
  }

  StringRef getPassName() const override {
    return "AMDGPU Remove Incompatible Functions";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {}

  /// Checks a single function, returns true if the function must be deleted.
  bool checkFunction(Function &F);

  bool runOnModule(Module &M) override {
    assert(TM->getTargetTriple().isAMDGCN());

    SmallVector<Function *, 4> FnsToDelete;
    for (Function &F : M) {
      if (checkFunction(F))
        FnsToDelete.push_back(&F);
    }

    for (Function *F : FnsToDelete) {
      F->replaceAllUsesWith(ConstantPointerNull::get(F->getType()));
      F->eraseFromParent();
    }
    return !FnsToDelete.empty();
  }

private:
  const TargetMachine *TM = nullptr;
};

StringRef getFeatureName(unsigned Feature) {
  for (const SubtargetFeatureKV &KV : AMDGPUFeatureKV)
    if (Feature == KV.Value)
      return KV.Key;

  llvm_unreachable("Unknown Target feature");
}

const SubtargetSubTypeKV *getGPUInfo(const GCNSubtarget &ST,
                                     StringRef GPUName) {
  for (const SubtargetSubTypeKV &KV : ST.getAllProcessorDescriptions())
    if (StringRef(KV.Key) == GPUName)
      return &KV;

  return nullptr;
}

constexpr unsigned FeaturesToCheck[] = {AMDGPU::FeatureGFX11Insts,
                                        AMDGPU::FeatureGFX10Insts,
                                        AMDGPU::FeatureGFX9Insts,
                                        AMDGPU::FeatureGFX8Insts,
                                        AMDGPU::FeatureDPP,
                                        AMDGPU::Feature16BitInsts,
                                        AMDGPU::FeatureDot1Insts,
                                        AMDGPU::FeatureDot2Insts,
                                        AMDGPU::FeatureDot3Insts,
                                        AMDGPU::FeatureDot4Insts,
                                        AMDGPU::FeatureDot5Insts,
                                        AMDGPU::FeatureDot6Insts,
                                        AMDGPU::FeatureDot7Insts,
                                        AMDGPU::FeatureDot8Insts,
                                        AMDGPU::FeatureExtendedImageInsts,
                                        AMDGPU::FeatureSMemRealTime,
                                        AMDGPU::FeatureSMemTimeInst,
                                        AMDGPU::FeatureGWS};

FeatureBitset expandImpliedFeatures(const FeatureBitset &Features) {
  FeatureBitset Result = Features;
  for (const SubtargetFeatureKV &FE : AMDGPUFeatureKV) {
    if (Features.test(FE.Value) && FE.Implies.any())
      Result |= expandImpliedFeatures(FE.Implies.getAsBitset());
  }
  return Result;
}

void reportFunctionRemoved(Function &F, unsigned Feature) {
  OptimizationRemarkEmitter ORE(&F);
  ORE.emit([&]() {
    // Note: we print the function name as part of the diagnostic because if
    // debug info is not present, users get "<unknown>:0:0" as the debug
    // loc. If we didn't print the function name there would be no way to
    // tell which function got removed.
    return OptimizationRemark(DEBUG_TYPE, "AMDGPUIncompatibleFnRemoved", &F)
           << "removing function '" << F.getName() << "': +"
           << getFeatureName(Feature)
           << " is not supported on the current target";
  });
}
} // end anonymous namespace

bool AMDGPURemoveIncompatibleFunctions::checkFunction(Function &F) {
  if (F.isDeclaration())
    return false;

  const GCNSubtarget *ST =
      static_cast<const GCNSubtarget *>(TM->getSubtargetImpl(F));

  // Check the GPU isn't generic or generic-hsa. Generic is used for testing
  // only and we don't want this pass to interfere with it.
  StringRef GPUName = ST->getCPU();
  if (GPUName.empty() || GPUName.starts_with("generic"))
    return false;

  // Try to fetch the GPU's info. If we can't, it's likely an unknown processor
  // so just bail out.
  const SubtargetSubTypeKV *GPUInfo = getGPUInfo(*ST, GPUName);
  if (!GPUInfo)
    return false;

  // Get all the features implied by the current GPU, and recursively expand
  // the features that imply other features.
  //
  // e.g. GFX90A implies FeatureGFX9, and FeatureGFX9 implies a whole set of
  // other features.
  const FeatureBitset GPUFeatureBits =
      expandImpliedFeatures(GPUInfo->Implies.getAsBitset());

  // Now that the have a FeatureBitset containing all possible features for
  // the chosen GPU, check our list of "suspicious" features.

  // Check that the user didn't enable any features that aren't part of that
  // GPU's feature set. We only check a predetermined set of features.
  for (unsigned Feature : FeaturesToCheck) {
    if (ST->hasFeature(Feature) && !GPUFeatureBits.test(Feature)) {
      reportFunctionRemoved(F, Feature);
      return true;
    }
  }

  // Delete FeatureWavefrontSize32 functions for
  // gfx9 and below targets that don't support the mode.
  // gfx10+ is implied to support both wave32 and 64 features.
  // They are not in the feature set. So, we need a separate check
  if (ST->getGeneration() < AMDGPUSubtarget::GFX10 &&
      ST->hasFeature(AMDGPU::FeatureWavefrontSize32)) {
    reportFunctionRemoved(F, AMDGPU::FeatureWavefrontSize32);
    return true;
  }
  return false;
}

INITIALIZE_PASS(AMDGPURemoveIncompatibleFunctions, DEBUG_TYPE,
                "AMDGPU Remove Incompatible Functions", false, false)

char AMDGPURemoveIncompatibleFunctions::ID = 0;

ModulePass *
llvm::createAMDGPURemoveIncompatibleFunctionsPass(const TargetMachine *TM) {
  return new AMDGPURemoveIncompatibleFunctions(TM);
}
