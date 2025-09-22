//===- llvm/LinkAllPasses.h ------------ Reference All Passes ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file pulls in all transformation and analysis passes for tools
// like opt and bugpoint that need this functionality.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LINKALLPASSES_H
#define LLVM_LINKALLPASSES_H

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysisEvaluator.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CallPrinter.h"
#include "llvm/Analysis/DomPrinter.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/RegionPass.h"
#include "llvm/Analysis/RegionPrinter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/Support/Valgrind.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/ObjCARC.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Scalarizer.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/SymbolRewriter.h"
#include "llvm/Transforms/Vectorize/LoadStoreVectorizer.h"
#include <cstdlib>

namespace {
  struct ForcePassLinking {
    ForcePassLinking() {
      // We must reference the passes in such a way that compilers will not
      // delete it all as dead code, even with whole program optimization,
      // yet is effectively a NO-OP. As the compiler isn't smart enough
      // to know that getenv() never returns -1, this will do the job.
      // This is so that globals in the translation units where these functions
      // are defined are forced to be initialized, populating various
      // registries.
      if (std::getenv("bar") != (char*) -1)
        return;

      (void)llvm::createAtomicExpandLegacyPass();
      (void) llvm::createBasicAAWrapperPass();
      (void) llvm::createSCEVAAWrapperPass();
      (void) llvm::createTypeBasedAAWrapperPass();
      (void) llvm::createScopedNoAliasAAWrapperPass();
      (void) llvm::createBreakCriticalEdgesPass();
      (void) llvm::createCallGraphDOTPrinterPass();
      (void) llvm::createCallGraphViewerPass();
      (void) llvm::createCFGSimplificationPass();
      (void) llvm::createStructurizeCFGPass();
      (void) llvm::createDeadArgEliminationPass();
      (void) llvm::createDeadCodeEliminationPass();
      (void) llvm::createDependenceAnalysisWrapperPass();
      (void) llvm::createDomOnlyPrinterWrapperPassPass();
      (void) llvm::createDomPrinterWrapperPassPass();
      (void) llvm::createDomOnlyViewerWrapperPassPass();
      (void) llvm::createDomViewerWrapperPassPass();
      (void) llvm::createAlwaysInlinerLegacyPass();
      (void) llvm::createGlobalsAAWrapperPass();
      (void) llvm::createInstSimplifyLegacyPass();
      (void) llvm::createInstructionCombiningPass();
      (void) llvm::createJMCInstrumenterPass();
      (void) llvm::createKCFIPass();
      (void) llvm::createLCSSAPass();
      (void) llvm::createLICMPass();
      (void) llvm::createLazyValueInfoPass();
      (void) llvm::createLoopExtractorPass();
      (void) llvm::createLoopSimplifyPass();
      (void) llvm::createLoopStrengthReducePass();
      (void) llvm::createLoopUnrollPass();
      (void) llvm::createLowerConstantIntrinsicsPass();
      (void) llvm::createLowerGlobalDtorsLegacyPass();
      (void) llvm::createLowerInvokePass();
      (void) llvm::createLowerSwitchPass();
      (void) llvm::createNaryReassociatePass();
      (void) llvm::createObjCARCContractPass();
      (void) llvm::createPromoteMemoryToRegisterPass();
      (void)llvm::createPostDomOnlyPrinterWrapperPassPass();
      (void)llvm::createPostDomPrinterWrapperPassPass();
      (void)llvm::createPostDomOnlyViewerWrapperPassPass();
      (void)llvm::createPostDomViewerWrapperPassPass();
      (void) llvm::createReassociatePass();
      (void) llvm::createRegionInfoPass();
      (void) llvm::createRegionOnlyPrinterPass();
      (void) llvm::createRegionOnlyViewerPass();
      (void) llvm::createRegionPrinterPass();
      (void) llvm::createRegionViewerPass();
      (void) llvm::createSafeStackPass();
      (void) llvm::createSROAPass();
      (void) llvm::createSingleLoopExtractorPass();
      (void) llvm::createTailCallEliminationPass();
      (void)llvm::createTLSVariableHoistPass();
      (void) llvm::createConstantHoistingPass();
      (void)llvm::createCodeGenPrepareLegacyPass();
      (void) llvm::createPostInlineEntryExitInstrumenterPass();
      (void) llvm::createEarlyCSEPass();
      (void) llvm::createGVNPass();
      (void) llvm::createPostDomTree();
      (void) llvm::createMergeICmpsLegacyPass();
      (void) llvm::createExpandLargeDivRemPass();
      (void)llvm::createExpandMemCmpLegacyPass();
      (void) llvm::createExpandVectorPredicationPass();
      std::string buf;
      llvm::raw_string_ostream os(buf);
      (void) llvm::createPrintModulePass(os);
      (void) llvm::createPrintFunctionPass(os);
      (void) llvm::createSinkingPass();
      (void) llvm::createLowerAtomicPass();
      (void) llvm::createLoadStoreVectorizerPass();
      (void) llvm::createPartiallyInlineLibCallsPass();
      (void) llvm::createSeparateConstOffsetFromGEPPass();
      (void) llvm::createSpeculativeExecutionPass();
      (void) llvm::createSpeculativeExecutionIfHasBranchDivergencePass();
      (void) llvm::createStraightLineStrengthReducePass();
      (void)llvm::createScalarizeMaskedMemIntrinLegacyPass();
      (void) llvm::createHardwareLoopsLegacyPass();
      (void) llvm::createUnifyLoopExitsPass();
      (void) llvm::createFixIrreduciblePass();
      (void)llvm::createSelectOptimizePass();

      (void)new llvm::ScalarEvolutionWrapperPass();
      llvm::Function::Create(nullptr, llvm::GlobalValue::ExternalLinkage)->viewCFGOnly();
      llvm::RGPassManager RGM;
      llvm::TargetLibraryInfoImpl TLII;
      llvm::TargetLibraryInfo TLI(TLII);
      llvm::AliasAnalysis AA(TLI);
      llvm::BatchAAResults BAA(AA);
      llvm::AliasSetTracker X(BAA);
      X.add(llvm::MemoryLocation()); // for -print-alias-sets
      (void) llvm::AreStatisticsEnabled();
      (void) llvm::sys::RunningOnValgrind();
    }
  } ForcePassLinking; // Force link by creating a global definition.
}

#endif
