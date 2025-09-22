//===- LowerAllowCheckPass.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/LowerAllowCheckPass.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include <memory>
#include <random>

using namespace llvm;

#define DEBUG_TYPE "lower-allow-check"

static cl::opt<int>
    HotPercentileCutoff("lower-allow-check-percentile-cutoff-hot",
                        cl::desc("Hot percentile cuttoff."));

static cl::opt<float>
    RandomRate("lower-allow-check-random-rate",
               cl::desc("Probability value in the range [0.0, 1.0] of "
                        "unconditional pseudo-random checks."));

STATISTIC(NumChecksTotal, "Number of checks");
STATISTIC(NumChecksRemoved, "Number of removed checks");

struct RemarkInfo {
  ore::NV Kind;
  ore::NV F;
  ore::NV BB;
  explicit RemarkInfo(IntrinsicInst *II)
      : Kind("Kind", II->getArgOperand(0)),
        F("Function", II->getParent()->getParent()),
        BB("Block", II->getParent()->getName()) {}
};

static void emitRemark(IntrinsicInst *II, OptimizationRemarkEmitter &ORE,
                       bool Removed) {
  if (Removed) {
    ORE.emit([&]() {
      RemarkInfo Info(II);
      return OptimizationRemark(DEBUG_TYPE, "Removed", II)
             << "Removed check: Kind=" << Info.Kind << " F=" << Info.F
             << " BB=" << Info.BB;
    });
  } else {
    ORE.emit([&]() {
      RemarkInfo Info(II);
      return OptimizationRemarkMissed(DEBUG_TYPE, "Allowed", II)
             << "Allowed check: Kind=" << Info.Kind << " F=" << Info.F
             << " BB=" << Info.BB;
    });
  }
}

static bool removeUbsanTraps(Function &F, const BlockFrequencyInfo &BFI,
                             const ProfileSummaryInfo *PSI,
                             OptimizationRemarkEmitter &ORE) {
  SmallVector<std::pair<IntrinsicInst *, bool>, 16> ReplaceWithValue;
  std::unique_ptr<RandomNumberGenerator> Rng;

  auto ShouldRemove = [&](bool IsHot) {
    if (!RandomRate.getNumOccurrences())
      return IsHot;
    if (!Rng)
      Rng = F.getParent()->createRNG(F.getName());
    std::bernoulli_distribution D(RandomRate);
    return !D(*Rng);
  };

  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      IntrinsicInst *II = dyn_cast<IntrinsicInst>(&I);
      if (!II)
        continue;
      auto ID = II->getIntrinsicID();
      switch (ID) {
      case Intrinsic::allow_ubsan_check:
      case Intrinsic::allow_runtime_check: {
        ++NumChecksTotal;

        bool IsHot = false;
        if (PSI) {
          uint64_t Count = BFI.getBlockProfileCount(&BB).value_or(0);
          IsHot = PSI->isHotCountNthPercentile(HotPercentileCutoff, Count);
        }

        bool ToRemove = ShouldRemove(IsHot);
        ReplaceWithValue.push_back({
            II,
            ToRemove,
        });
        if (ToRemove)
          ++NumChecksRemoved;
        emitRemark(II, ORE, ToRemove);
        break;
      }
      default:
        break;
      }
    }
  }

  for (auto [I, V] : ReplaceWithValue) {
    I->replaceAllUsesWith(ConstantInt::getBool(I->getType(), !V));
    I->eraseFromParent();
  }

  return !ReplaceWithValue.empty();
}

PreservedAnalyses LowerAllowCheckPass::run(Function &F,
                                           FunctionAnalysisManager &AM) {
  if (F.isDeclaration())
    return PreservedAnalyses::all();
  auto &MAMProxy = AM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
  ProfileSummaryInfo *PSI =
      MAMProxy.getCachedResult<ProfileSummaryAnalysis>(*F.getParent());
  BlockFrequencyInfo &BFI = AM.getResult<BlockFrequencyAnalysis>(F);
  OptimizationRemarkEmitter &ORE =
      AM.getResult<OptimizationRemarkEmitterAnalysis>(F);

  return removeUbsanTraps(F, BFI, PSI, ORE) ? PreservedAnalyses::none()
                                            : PreservedAnalyses::all();
}

bool LowerAllowCheckPass::IsRequested() {
  return RandomRate.getNumOccurrences() ||
         HotPercentileCutoff.getNumOccurrences();
}
