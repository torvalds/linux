//===- OptimizationRemarkEmitter.cpp - Optimization Diagnostic --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Optimization diagnostic interfaces.  It's packaged as an analysis pass so
// that by using this service passes become dependent on BFI as well.  BFI is
// used to compute the "hotness" of the diagnostic message.
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/LazyBlockFrequencyInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/InitializePasses.h"
#include <optional>

using namespace llvm;

OptimizationRemarkEmitter::OptimizationRemarkEmitter(const Function *F)
    : F(F), BFI(nullptr) {
  if (!F->getContext().getDiagnosticsHotnessRequested())
    return;

  // First create a dominator tree.
  DominatorTree DT;
  DT.recalculate(*const_cast<Function *>(F));

  // Generate LoopInfo from it.
  LoopInfo LI;
  LI.analyze(DT);

  // Then compute BranchProbabilityInfo.
  BranchProbabilityInfo BPI(*F, LI, nullptr, &DT, nullptr);

  // Finally compute BFI.
  OwnedBFI = std::make_unique<BlockFrequencyInfo>(*F, BPI, LI);
  BFI = OwnedBFI.get();
}

bool OptimizationRemarkEmitter::invalidate(
    Function &F, const PreservedAnalyses &PA,
    FunctionAnalysisManager::Invalidator &Inv) {
  if (OwnedBFI) {
    OwnedBFI.reset();
    BFI = nullptr;
  }
  // This analysis has no state and so can be trivially preserved but it needs
  // a fresh view of BFI if it was constructed with one.
  if (BFI && Inv.invalidate<BlockFrequencyAnalysis>(F, PA))
    return true;

  // Otherwise this analysis result remains valid.
  return false;
}

std::optional<uint64_t>
OptimizationRemarkEmitter::computeHotness(const Value *V) {
  if (!BFI)
    return std::nullopt;

  return BFI->getBlockProfileCount(cast<BasicBlock>(V));
}

void OptimizationRemarkEmitter::computeHotness(
    DiagnosticInfoIROptimization &OptDiag) {
  const Value *V = OptDiag.getCodeRegion();
  if (V)
    OptDiag.setHotness(computeHotness(V));
}

void OptimizationRemarkEmitter::emit(
    DiagnosticInfoOptimizationBase &OptDiagBase) {
  auto &OptDiag = cast<DiagnosticInfoIROptimization>(OptDiagBase);
  computeHotness(OptDiag);

  // Only emit it if its hotness meets the threshold.
  if (OptDiag.getHotness().value_or(0) <
      F->getContext().getDiagnosticsHotnessThreshold()) {
    return;
  }

  F->getContext().diagnose(OptDiag);
}

OptimizationRemarkEmitterWrapperPass::OptimizationRemarkEmitterWrapperPass()
    : FunctionPass(ID) {
  initializeOptimizationRemarkEmitterWrapperPassPass(
      *PassRegistry::getPassRegistry());
}

bool OptimizationRemarkEmitterWrapperPass::runOnFunction(Function &Fn) {
  BlockFrequencyInfo *BFI;

  auto &Context = Fn.getContext();
  if (Context.getDiagnosticsHotnessRequested()) {
    BFI = &getAnalysis<LazyBlockFrequencyInfoPass>().getBFI();
    // Get hotness threshold from PSI. This should only happen once.
    if (Context.isDiagnosticsHotnessThresholdSetFromPSI()) {
      if (ProfileSummaryInfo *PSI =
              &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI())
        Context.setDiagnosticsHotnessThreshold(
            PSI->getOrCompHotCountThreshold());
    }
  } else
    BFI = nullptr;

  ORE = std::make_unique<OptimizationRemarkEmitter>(&Fn, BFI);
  return false;
}

void OptimizationRemarkEmitterWrapperPass::getAnalysisUsage(
    AnalysisUsage &AU) const {
  LazyBlockFrequencyInfoPass::getLazyBFIAnalysisUsage(AU);
  AU.addRequired<ProfileSummaryInfoWrapperPass>();
  AU.setPreservesAll();
}

AnalysisKey OptimizationRemarkEmitterAnalysis::Key;

OptimizationRemarkEmitter
OptimizationRemarkEmitterAnalysis::run(Function &F,
                                       FunctionAnalysisManager &AM) {
  BlockFrequencyInfo *BFI;
  auto &Context = F.getContext();

  if (Context.getDiagnosticsHotnessRequested()) {
    BFI = &AM.getResult<BlockFrequencyAnalysis>(F);
    // Get hotness threshold from PSI. This should only happen once.
    if (Context.isDiagnosticsHotnessThresholdSetFromPSI()) {
      auto &MAMProxy = AM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
      if (ProfileSummaryInfo *PSI =
              MAMProxy.getCachedResult<ProfileSummaryAnalysis>(*F.getParent()))
        Context.setDiagnosticsHotnessThreshold(
            PSI->getOrCompHotCountThreshold());
    }
  } else
    BFI = nullptr;

  return OptimizationRemarkEmitter(&F, BFI);
}

char OptimizationRemarkEmitterWrapperPass::ID = 0;
static const char ore_name[] = "Optimization Remark Emitter";
#define ORE_NAME "opt-remark-emitter"

INITIALIZE_PASS_BEGIN(OptimizationRemarkEmitterWrapperPass, ORE_NAME, ore_name,
                      false, true)
INITIALIZE_PASS_DEPENDENCY(LazyBFIPass)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_END(OptimizationRemarkEmitterWrapperPass, ORE_NAME, ore_name,
                    false, true)
