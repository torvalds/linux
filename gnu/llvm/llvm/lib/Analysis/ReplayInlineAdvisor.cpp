//===- ReplayInlineAdvisor.cpp - Replay InlineAdvisor ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements ReplayInlineAdvisor that replays inline decisions based
// on previous inline remarks from optimization remark log. This is a best
// effort approach useful for testing compiler/source changes while holding
// inlining steady.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ReplayInlineAdvisor.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "replay-inline"

ReplayInlineAdvisor::ReplayInlineAdvisor(
    Module &M, FunctionAnalysisManager &FAM, LLVMContext &Context,
    std::unique_ptr<InlineAdvisor> OriginalAdvisor,
    const ReplayInlinerSettings &ReplaySettings, bool EmitRemarks,
    InlineContext IC)
    : InlineAdvisor(M, FAM, IC), OriginalAdvisor(std::move(OriginalAdvisor)),
      ReplaySettings(ReplaySettings), EmitRemarks(EmitRemarks) {

  auto BufferOrErr = MemoryBuffer::getFileOrSTDIN(ReplaySettings.ReplayFile);
  std::error_code EC = BufferOrErr.getError();
  if (EC) {
    Context.emitError("Could not open remarks file: " + EC.message());
    return;
  }

  // Example for inline remarks to parse:
  //   main:3:1.1: '_Z3subii' inlined into 'main' at callsite sum:1 @
  //   main:3:1.1;
  // We use the callsite string after `at callsite` to replay inlining.
  line_iterator LineIt(*BufferOrErr.get(), /*SkipBlanks=*/true);
  const std::string PositiveRemark = "' inlined into '";
  const std::string NegativeRemark = "' will not be inlined into '";

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef Line = *LineIt;
    auto Pair = Line.split(" at callsite ");

    bool IsPositiveRemark = true;
    if (Pair.first.contains(NegativeRemark))
      IsPositiveRemark = false;

    auto CalleeCaller =
        Pair.first.split(IsPositiveRemark ? PositiveRemark : NegativeRemark);

    StringRef Callee = CalleeCaller.first.rsplit(": '").second;
    StringRef Caller = CalleeCaller.second.rsplit("'").first;

    auto CallSite = Pair.second.split(";").first;

    if (Callee.empty() || Caller.empty() || CallSite.empty()) {
      Context.emitError("Invalid remark format: " + Line);
      return;
    }

    std::string Combined = (Callee + CallSite).str();
    InlineSitesFromRemarks[Combined] = IsPositiveRemark;
    if (ReplaySettings.ReplayScope == ReplayInlinerSettings::Scope::Function)
      CallersToReplay.insert(Caller);
  }

  HasReplayRemarks = true;
}

std::unique_ptr<InlineAdvisor>
llvm::getReplayInlineAdvisor(Module &M, FunctionAnalysisManager &FAM,
                             LLVMContext &Context,
                             std::unique_ptr<InlineAdvisor> OriginalAdvisor,
                             const ReplayInlinerSettings &ReplaySettings,
                             bool EmitRemarks, InlineContext IC) {
  auto Advisor = std::make_unique<ReplayInlineAdvisor>(
      M, FAM, Context, std::move(OriginalAdvisor), ReplaySettings, EmitRemarks,
      IC);
  if (!Advisor->areReplayRemarksLoaded())
    Advisor.reset();
  return Advisor;
}

std::unique_ptr<InlineAdvice> ReplayInlineAdvisor::getAdviceImpl(CallBase &CB) {
  assert(HasReplayRemarks);

  Function &Caller = *CB.getCaller();
  auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(Caller);

  // Decision not made by replay system
  if (!hasInlineAdvice(*CB.getFunction())) {
    // If there's a registered original advisor, return its decision
    if (OriginalAdvisor)
      return OriginalAdvisor->getAdvice(CB);

    // If no decision is made above, return non-decision
    return {};
  }

  std::string CallSiteLoc =
      formatCallSiteLocation(CB.getDebugLoc(), ReplaySettings.ReplayFormat);
  StringRef Callee = CB.getCalledFunction()->getName();
  std::string Combined = (Callee + CallSiteLoc).str();

  // Replay decision, if it has one
  auto Iter = InlineSitesFromRemarks.find(Combined);
  if (Iter != InlineSitesFromRemarks.end()) {
    if (InlineSitesFromRemarks[Combined]) {
      LLVM_DEBUG(dbgs() << "Replay Inliner: Inlined " << Callee << " @ "
                        << CallSiteLoc << "\n");
      return std::make_unique<DefaultInlineAdvice>(
          this, CB, llvm::InlineCost::getAlways("previously inlined"), ORE,
          EmitRemarks);
    } else {
      LLVM_DEBUG(dbgs() << "Replay Inliner: Not Inlined " << Callee << " @ "
                        << CallSiteLoc << "\n");
      // A negative inline is conveyed by "None" std::optional<InlineCost>
      return std::make_unique<DefaultInlineAdvice>(this, CB, std::nullopt, ORE,
                                                   EmitRemarks);
    }
  }

  // Fallback decisions
  if (ReplaySettings.ReplayFallback ==
      ReplayInlinerSettings::Fallback::AlwaysInline)
    return std::make_unique<DefaultInlineAdvice>(
        this, CB, llvm::InlineCost::getAlways("AlwaysInline Fallback"), ORE,
        EmitRemarks);
  else if (ReplaySettings.ReplayFallback ==
           ReplayInlinerSettings::Fallback::NeverInline)
    // A negative inline is conveyed by "None" std::optional<InlineCost>
    return std::make_unique<DefaultInlineAdvice>(this, CB, std::nullopt, ORE,
                                                 EmitRemarks);
  else {
    assert(ReplaySettings.ReplayFallback ==
           ReplayInlinerSettings::Fallback::Original);
    // If there's a registered original advisor, return its decision
    if (OriginalAdvisor)
      return OriginalAdvisor->getAdvice(CB);
  }

  // If no decision is made above, return non-decision
  return {};
}
