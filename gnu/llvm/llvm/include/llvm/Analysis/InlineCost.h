//===- InlineCost.h - Cost analysis for inliner -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements heuristics for inlining decisions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_INLINECOST_H
#define LLVM_ANALYSIS_INLINECOST_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Analysis/InlineModelFeatureMaps.h"
#include "llvm/IR/PassManager.h"
#include <cassert>
#include <climits>
#include <optional>

namespace llvm {
class AssumptionCache;
class OptimizationRemarkEmitter;
class BlockFrequencyInfo;
class CallBase;
class DataLayout;
class Function;
class ProfileSummaryInfo;
class TargetTransformInfo;
class TargetLibraryInfo;

namespace InlineConstants {
// Various thresholds used by inline cost analysis.
/// Use when optsize (-Os) is specified.
const int OptSizeThreshold = 50;

/// Use when minsize (-Oz) is specified.
const int OptMinSizeThreshold = 5;

/// Use when -O3 is specified.
const int OptAggressiveThreshold = 250;

// Various magic constants used to adjust heuristics.
int getInstrCost();
const int IndirectCallThreshold = 100;
const int LoopPenalty = 25;
const int LastCallToStaticBonus = 15000;
const int ColdccPenalty = 2000;
/// Do not inline functions which allocate this many bytes on the stack
/// when the caller is recursive.
const unsigned TotalAllocaSizeRecursiveCaller = 1024;
/// Do not inline dynamic allocas that have been constant propagated to be
/// static allocas above this amount in bytes.
const uint64_t MaxSimplifiedDynamicAllocaToInline = 65536;

const char FunctionInlineCostMultiplierAttributeName[] =
    "function-inline-cost-multiplier";

const char MaxInlineStackSizeAttributeName[] = "inline-max-stacksize";
} // namespace InlineConstants

// The cost-benefit pair computed by cost-benefit analysis.
class CostBenefitPair {
public:
  CostBenefitPair(APInt Cost, APInt Benefit)
      : Cost(std::move(Cost)), Benefit(std::move(Benefit)) {}

  const APInt &getCost() const { return Cost; }

  const APInt &getBenefit() const { return Benefit; }

private:
  APInt Cost;
  APInt Benefit;
};

/// Represents the cost of inlining a function.
///
/// This supports special values for functions which should "always" or
/// "never" be inlined. Otherwise, the cost represents a unitless amount;
/// smaller values increase the likelihood of the function being inlined.
///
/// Objects of this type also provide the adjusted threshold for inlining
/// based on the information available for a particular callsite. They can be
/// directly tested to determine if inlining should occur given the cost and
/// threshold for this cost metric.
class InlineCost {
  enum SentinelValues { AlwaysInlineCost = INT_MIN, NeverInlineCost = INT_MAX };

  /// The estimated cost of inlining this callsite.
  int Cost = 0;

  /// The adjusted threshold against which this cost was computed.
  int Threshold = 0;

  /// The amount of StaticBonus that has been applied.
  int StaticBonusApplied = 0;

  /// Must be set for Always and Never instances.
  const char *Reason = nullptr;

  /// The cost-benefit pair computed by cost-benefit analysis.
  std::optional<CostBenefitPair> CostBenefit;

  // Trivial constructor, interesting logic in the factory functions below.
  InlineCost(int Cost, int Threshold, int StaticBonusApplied,
             const char *Reason = nullptr,
             std::optional<CostBenefitPair> CostBenefit = std::nullopt)
      : Cost(Cost), Threshold(Threshold),
        StaticBonusApplied(StaticBonusApplied), Reason(Reason),
        CostBenefit(CostBenefit) {
    assert((isVariable() || Reason) &&
           "Reason must be provided for Never or Always");
  }

public:
  static InlineCost get(int Cost, int Threshold, int StaticBonus = 0) {
    assert(Cost > AlwaysInlineCost && "Cost crosses sentinel value");
    assert(Cost < NeverInlineCost && "Cost crosses sentinel value");
    return InlineCost(Cost, Threshold, StaticBonus);
  }
  static InlineCost
  getAlways(const char *Reason,
            std::optional<CostBenefitPair> CostBenefit = std::nullopt) {
    return InlineCost(AlwaysInlineCost, 0, 0, Reason, CostBenefit);
  }
  static InlineCost
  getNever(const char *Reason,
           std::optional<CostBenefitPair> CostBenefit = std::nullopt) {
    return InlineCost(NeverInlineCost, 0, 0, Reason, CostBenefit);
  }

  /// Test whether the inline cost is low enough for inlining.
  explicit operator bool() const { return Cost < Threshold; }

  bool isAlways() const { return Cost == AlwaysInlineCost; }
  bool isNever() const { return Cost == NeverInlineCost; }
  bool isVariable() const { return !isAlways() && !isNever(); }

  /// Get the inline cost estimate.
  /// It is an error to call this on an "always" or "never" InlineCost.
  int getCost() const {
    assert(isVariable() && "Invalid access of InlineCost");
    return Cost;
  }

  /// Get the threshold against which the cost was computed
  int getThreshold() const {
    assert(isVariable() && "Invalid access of InlineCost");
    return Threshold;
  }

  /// Get the amount of StaticBonus applied.
  int getStaticBonusApplied() const {
    assert(isVariable() && "Invalid access of InlineCost");
    return StaticBonusApplied;
  }

  /// Get the cost-benefit pair which was computed by cost-benefit analysis
  std::optional<CostBenefitPair> getCostBenefit() const { return CostBenefit; }

  /// Get the reason of Always or Never.
  const char *getReason() const {
    assert((Reason || isVariable()) &&
           "InlineCost reason must be set for Always or Never");
    return Reason;
  }

  /// Get the cost delta from the threshold for inlining.
  /// Only valid if the cost is of the variable kind. Returns a negative
  /// value if the cost is too high to inline.
  int getCostDelta() const { return Threshold - getCost(); }
};

/// InlineResult is basically true or false. For false results the message
/// describes a reason.
class InlineResult {
  const char *Message = nullptr;
  InlineResult(const char *Message = nullptr) : Message(Message) {}

public:
  static InlineResult success() { return {}; }
  static InlineResult failure(const char *Reason) {
    return InlineResult(Reason);
  }
  bool isSuccess() const { return Message == nullptr; }
  const char *getFailureReason() const {
    assert(!isSuccess() &&
           "getFailureReason should only be called in failure cases");
    return Message;
  }
};

/// Thresholds to tune inline cost analysis. The inline cost analysis decides
/// the condition to apply a threshold and applies it. Otherwise,
/// DefaultThreshold is used. If a threshold is Optional, it is applied only
/// when it has a valid value. Typically, users of inline cost analysis
/// obtain an InlineParams object through one of the \c getInlineParams methods
/// and pass it to \c getInlineCost. Some specialized versions of inliner
/// (such as the pre-inliner) might have custom logic to compute \c InlineParams
/// object.

struct InlineParams {
  /// The default threshold to start with for a callee.
  int DefaultThreshold = -1;

  /// Threshold to use for callees with inline hint.
  std::optional<int> HintThreshold;

  /// Threshold to use for cold callees.
  std::optional<int> ColdThreshold;

  /// Threshold to use when the caller is optimized for size.
  std::optional<int> OptSizeThreshold;

  /// Threshold to use when the caller is optimized for minsize.
  std::optional<int> OptMinSizeThreshold;

  /// Threshold to use when the callsite is considered hot.
  std::optional<int> HotCallSiteThreshold;

  /// Threshold to use when the callsite is considered hot relative to function
  /// entry.
  std::optional<int> LocallyHotCallSiteThreshold;

  /// Threshold to use when the callsite is considered cold.
  std::optional<int> ColdCallSiteThreshold;

  /// Compute inline cost even when the cost has exceeded the threshold.
  std::optional<bool> ComputeFullInlineCost;

  /// Indicate whether we should allow inline deferral.
  std::optional<bool> EnableDeferral;

  /// Indicate whether we allow inlining for recursive call.
  std::optional<bool> AllowRecursiveCall = false;
};

std::optional<int> getStringFnAttrAsInt(CallBase &CB, StringRef AttrKind);

/// Generate the parameters to tune the inline cost analysis based only on the
/// commandline options.
InlineParams getInlineParams();

/// Generate the parameters to tune the inline cost analysis based on command
/// line options. If -inline-threshold option is not explicitly passed,
/// \p Threshold is used as the default threshold.
InlineParams getInlineParams(int Threshold);

/// Generate the parameters to tune the inline cost analysis based on command
/// line options. If -inline-threshold option is not explicitly passed,
/// the default threshold is computed from \p OptLevel and \p SizeOptLevel.
/// An \p OptLevel value above 3 is considered an aggressive optimization mode.
/// \p SizeOptLevel of 1 corresponds to the -Os flag and 2 corresponds to
/// the -Oz flag.
InlineParams getInlineParams(unsigned OptLevel, unsigned SizeOptLevel);

/// Return the cost associated with a callsite, including parameter passing
/// and the call/return instruction.
int getCallsiteCost(const TargetTransformInfo &TTI, const CallBase &Call,
                    const DataLayout &DL);

/// Get an InlineCost object representing the cost of inlining this
/// callsite.
///
/// Note that a default threshold is passed into this function. This threshold
/// could be modified based on callsite's properties and only costs below this
/// new threshold are computed with any accuracy. The new threshold can be
/// used to bound the computation necessary to determine whether the cost is
/// sufficiently low to warrant inlining.
///
/// Also note that calling this function *dynamically* computes the cost of
/// inlining the callsite. It is an expensive, heavyweight call.
InlineCost
getInlineCost(CallBase &Call, const InlineParams &Params,
              TargetTransformInfo &CalleeTTI,
              function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
              function_ref<const TargetLibraryInfo &(Function &)> GetTLI,
              function_ref<BlockFrequencyInfo &(Function &)> GetBFI = nullptr,
              ProfileSummaryInfo *PSI = nullptr,
              OptimizationRemarkEmitter *ORE = nullptr);

/// Get an InlineCost with the callee explicitly specified.
/// This allows you to calculate the cost of inlining a function via a
/// pointer. This behaves exactly as the version with no explicit callee
/// parameter in all other respects.
//
InlineCost
getInlineCost(CallBase &Call, Function *Callee, const InlineParams &Params,
              TargetTransformInfo &CalleeTTI,
              function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
              function_ref<const TargetLibraryInfo &(Function &)> GetTLI,
              function_ref<BlockFrequencyInfo &(Function &)> GetBFI = nullptr,
              ProfileSummaryInfo *PSI = nullptr,
              OptimizationRemarkEmitter *ORE = nullptr);

/// Returns InlineResult::success() if the call site should be always inlined
/// because of user directives, and the inlining is viable. Returns
/// InlineResult::failure() if the inlining may never happen because of user
/// directives or incompatibilities detectable without needing callee traversal.
/// Otherwise returns std::nullopt, meaning that inlining should be decided
/// based on other criteria (e.g. cost modeling).
std::optional<InlineResult> getAttributeBasedInliningDecision(
    CallBase &Call, Function *Callee, TargetTransformInfo &CalleeTTI,
    function_ref<const TargetLibraryInfo &(Function &)> GetTLI);

/// Get the cost estimate ignoring thresholds. This is similar to getInlineCost
/// when passed InlineParams::ComputeFullInlineCost, or a non-null ORE. It
/// uses default InlineParams otherwise.
/// Contrary to getInlineCost, which makes a threshold-based final evaluation of
/// should/shouldn't inline, captured in InlineResult, getInliningCostEstimate
/// returns:
/// - std::nullopt, if the inlining cannot happen (is illegal)
/// - an integer, representing the cost.
std::optional<int> getInliningCostEstimate(
    CallBase &Call, TargetTransformInfo &CalleeTTI,
    function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
    function_ref<BlockFrequencyInfo &(Function &)> GetBFI = nullptr,
    ProfileSummaryInfo *PSI = nullptr,
    OptimizationRemarkEmitter *ORE = nullptr);

/// Get the expanded cost features. The features are returned unconditionally,
/// even if inlining is impossible.
std::optional<InlineCostFeatures> getInliningCostFeatures(
    CallBase &Call, TargetTransformInfo &CalleeTTI,
    function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
    function_ref<BlockFrequencyInfo &(Function &)> GetBFI = nullptr,
    ProfileSummaryInfo *PSI = nullptr,
    OptimizationRemarkEmitter *ORE = nullptr);

/// Minimal filter to detect invalid constructs for inlining.
InlineResult isInlineViable(Function &Callee);

// This pass is used to annotate instructions during the inline process for
// debugging and analysis. The main purpose of the pass is to see and test
// inliner's decisions when creating new optimizations to InlineCost.
struct InlineCostAnnotationPrinterPass
    : PassInfoMixin<InlineCostAnnotationPrinterPass> {
  raw_ostream &OS;

public:
  explicit InlineCostAnnotationPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
  static bool isRequired() { return true; }
};
} // namespace llvm

#endif
