//===- Transforms/IPO/SampleProfileProbe.h ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for the pseudo probe implementation for
/// AutoFDO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_SAMPLEPROFILEPROBE_H
#define LLVM_TRANSFORMS_IPO_SAMPLEPROFILEPROBE_H

#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/ProfileData/SampleProf.h"
#include <unordered_map>

namespace llvm {
class BasicBlock;
class Function;
class Instruction;
class Loop;
class PassInstrumentationCallbacks;
class TargetMachine;

class Module;

using namespace sampleprof;
using BlockIdMap = std::unordered_map<BasicBlock *, uint32_t>;
using InstructionIdMap = std::unordered_map<Instruction *, uint32_t>;
// Map from tuples of Probe id and inline stack hash code to distribution
// factors.
using ProbeFactorMap = std::unordered_map<std::pair<uint64_t, uint64_t>, float,
                                          pair_hash<uint64_t, uint64_t>>;
using FuncProbeFactorMap = StringMap<ProbeFactorMap>;


// A pseudo probe verifier that can be run after each IR passes to detect the
// violation of updating probe factors. In principle, the sum of distribution
// factor for a probe should be identical before and after a pass. For a
// function pass, the factor sum for a probe would be typically 100%.
class PseudoProbeVerifier {
public:
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

  // Implementation of pass instrumentation callbacks for new pass manager.
  void runAfterPass(StringRef PassID, Any IR);

private:
  // Allow a little bias due the rounding to integral factors.
  constexpr static float DistributionFactorVariance = 0.02f;
  // Distribution factors from last pass.
  FuncProbeFactorMap FunctionProbeFactors;

  void collectProbeFactors(const BasicBlock *BB, ProbeFactorMap &ProbeFactors);
  void runAfterPass(const Module *M);
  void runAfterPass(const LazyCallGraph::SCC *C);
  void runAfterPass(const Function *F);
  void runAfterPass(const Loop *L);
  bool shouldVerifyFunction(const Function *F);
  void verifyProbeFactors(const Function *F,
                          const ProbeFactorMap &ProbeFactors);
};

/// Sample profile pseudo prober.
///
/// Insert pseudo probes for block sampling and value sampling.
class SampleProfileProber {
public:
  // Give an empty module id when the prober is not used for instrumentation.
  SampleProfileProber(Function &F, const std::string &CurModuleUniqueId);
  void instrumentOneFunc(Function &F, TargetMachine *TM);

private:
  Function *getFunction() const { return F; }
  uint64_t getFunctionHash() const { return FunctionHash; }
  uint32_t getBlockId(const BasicBlock *BB) const;
  uint32_t getCallsiteId(const Instruction *Call) const;
  void findUnreachableBlocks(DenseSet<BasicBlock *> &BlocksToIgnore);
  void findInvokeNormalDests(DenseSet<BasicBlock *> &InvokeNormalDests);
  void computeBlocksToIgnore(DenseSet<BasicBlock *> &BlocksToIgnore,
                             DenseSet<BasicBlock *> &BlocksAndCallsToIgnore);
  const Instruction *
  getOriginalTerminator(const BasicBlock *Head,
                        const DenseSet<BasicBlock *> &BlocksToIgnore);
  void computeCFGHash(const DenseSet<BasicBlock *> &BlocksToIgnore);
  void computeProbeId(const DenseSet<BasicBlock *> &BlocksToIgnore,
                      const DenseSet<BasicBlock *> &BlocksAndCallsToIgnore);

  Function *F;

  /// The current module ID that is used to name a static object as a comdat
  /// group.
  std::string CurModuleUniqueId;

  /// A CFG hash code used to identify a function code changes.
  uint64_t FunctionHash;

  /// Map basic blocks to the their pseudo probe ids.
  BlockIdMap BlockProbeIds;

  /// Map indirect calls to the their pseudo probe ids.
  InstructionIdMap CallProbeIds;

  /// The ID of the last probe, Can be used to number a new probe.
  uint32_t LastProbeId;
};

class SampleProfileProbePass : public PassInfoMixin<SampleProfileProbePass> {
  TargetMachine *TM;

public:
  SampleProfileProbePass(TargetMachine *TM) : TM(TM) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

// Pseudo probe distribution factor updater.
// Sample profile annotation can happen in both LTO prelink and postlink. The
// postlink-time re-annotation can degrade profile quality because of prelink
// code duplication transformation, such as loop unrolling, jump threading,
// indirect call promotion etc. As such, samples corresponding to a source
// location may be aggregated multiple times in postlink. With a concept of
// distribution factor for pseudo probes, samples can be distributed among
// duplicated probes reasonable based on the assumption that optimizations
// duplicating code well-maintain the branch frequency information (BFI). This
// pass updates distribution factors for each pseudo probe at the end of the
// prelink pipeline, to reflect an estimated portion of the real execution
// count.
class PseudoProbeUpdatePass : public PassInfoMixin<PseudoProbeUpdatePass> {
  void runOnFunction(Function &F, FunctionAnalysisManager &FAM);

public:
  PseudoProbeUpdatePass() = default;
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm
#endif // LLVM_TRANSFORMS_IPO_SAMPLEPROFILEPROBE_H
