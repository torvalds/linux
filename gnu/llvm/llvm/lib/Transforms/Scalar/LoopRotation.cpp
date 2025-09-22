//===- LoopRotation.cpp - Loop Rotation Pass ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Loop Rotation Pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopRotation.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LazyBlockFrequencyInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/LoopRotationUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include <optional>
using namespace llvm;

#define DEBUG_TYPE "loop-rotate"

static cl::opt<unsigned> DefaultRotationThreshold(
    "rotation-max-header-size", cl::init(16), cl::Hidden,
    cl::desc("The default maximum header size for automatic loop rotation"));

static cl::opt<bool> PrepareForLTOOption(
    "rotation-prepare-for-lto", cl::init(false), cl::Hidden,
    cl::desc("Run loop-rotation in the prepare-for-lto stage. This option "
             "should be used for testing only."));

LoopRotatePass::LoopRotatePass(bool EnableHeaderDuplication, bool PrepareForLTO)
    : EnableHeaderDuplication(EnableHeaderDuplication),
      PrepareForLTO(PrepareForLTO) {}

void LoopRotatePass::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  static_cast<PassInfoMixin<LoopRotatePass> *>(this)->printPipeline(
      OS, MapClassName2PassName);
  OS << "<";
  if (!EnableHeaderDuplication)
    OS << "no-";
  OS << "header-duplication;";

  if (!PrepareForLTO)
    OS << "no-";
  OS << "prepare-for-lto";
  OS << ">";
}

PreservedAnalyses LoopRotatePass::run(Loop &L, LoopAnalysisManager &AM,
                                      LoopStandardAnalysisResults &AR,
                                      LPMUpdater &) {
  // Vectorization requires loop-rotation. Use default threshold for loops the
  // user explicitly marked for vectorization, even when header duplication is
  // disabled.
  int Threshold =
      (EnableHeaderDuplication && !L.getHeader()->getParent()->hasMinSize()) ||
              hasVectorizeTransformation(&L) == TM_ForcedByUser
          ? DefaultRotationThreshold
          : 0;
  const DataLayout &DL = L.getHeader()->getDataLayout();
  const SimplifyQuery SQ = getBestSimplifyQuery(AR, DL);

  std::optional<MemorySSAUpdater> MSSAU;
  if (AR.MSSA)
    MSSAU = MemorySSAUpdater(AR.MSSA);
  bool Changed = LoopRotation(&L, &AR.LI, &AR.TTI, &AR.AC, &AR.DT, &AR.SE,
                              MSSAU ? &*MSSAU : nullptr, SQ, false, Threshold,
                              false, PrepareForLTO || PrepareForLTOOption);

  if (!Changed)
    return PreservedAnalyses::all();

  if (AR.MSSA && VerifyMemorySSA)
    AR.MSSA->verifyMemorySSA();

  auto PA = getLoopPassPreservedAnalyses();
  if (AR.MSSA)
    PA.preserve<MemorySSAAnalysis>();
  return PA;
}
