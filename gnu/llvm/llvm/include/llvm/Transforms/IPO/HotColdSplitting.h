//===- HotColdSplitting.h ---- Outline Cold Regions -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// This pass outlines cold regions to a separate function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_HOTCOLDSPLITTING_H
#define LLVM_TRANSFORMS_IPO_HOTCOLDSPLITTING_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/BranchProbability.h"

namespace llvm {

class Module;
class ProfileSummaryInfo;
class BasicBlock;
class BlockFrequencyInfo;
class TargetTransformInfo;
class OptimizationRemarkEmitter;
class AssumptionCache;
class DominatorTree;
class CodeExtractor;
class CodeExtractorAnalysisCache;

/// A sequence of basic blocks.
///
/// A 0-sized SmallVector is slightly cheaper to move than a std::vector.
using BlockSequence = SmallVector<BasicBlock *, 0>;

class HotColdSplitting {
public:
  HotColdSplitting(ProfileSummaryInfo *ProfSI,
                   function_ref<BlockFrequencyInfo *(Function &)> GBFI,
                   function_ref<TargetTransformInfo &(Function &)> GTTI,
                   std::function<OptimizationRemarkEmitter &(Function &)> *GORE,
                   function_ref<AssumptionCache *(Function &)> LAC)
      : PSI(ProfSI), GetBFI(GBFI), GetTTI(GTTI), GetORE(GORE), LookupAC(LAC) {}
  bool run(Module &M);

private:
  bool isFunctionCold(const Function &F) const;
  bool isBasicBlockCold(BasicBlock *BB, BranchProbability ColdProbThresh,
                        SmallPtrSetImpl<BasicBlock *> &AnnotatedColdBlocks,
                        BlockFrequencyInfo *BFI) const;
  bool shouldOutlineFrom(const Function &F) const;
  bool outlineColdRegions(Function &F, bool HasProfileSummary);
  bool isSplittingBeneficial(CodeExtractor &CE, const BlockSequence &Region,
                             TargetTransformInfo &TTI);
  Function *extractColdRegion(BasicBlock &EntryPoint, CodeExtractor &CE,
                              const CodeExtractorAnalysisCache &CEAC,
                              BlockFrequencyInfo *BFI, TargetTransformInfo &TTI,
                              OptimizationRemarkEmitter &ORE);
  ProfileSummaryInfo *PSI;
  function_ref<BlockFrequencyInfo *(Function &)> GetBFI;
  function_ref<TargetTransformInfo &(Function &)> GetTTI;
  std::function<OptimizationRemarkEmitter &(Function &)> *GetORE;
  function_ref<AssumptionCache *(Function &)> LookupAC;
};

/// Pass to outline cold regions.
class HotColdSplittingPass : public PassInfoMixin<HotColdSplittingPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_HOTCOLDSPLITTING_H

