//===- InlineOrder.cpp - Inlining order abstraction -*- C++ ---*-----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/InlineOrder.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

#define DEBUG_TYPE "inline-order"

enum class InlinePriorityMode : int { Size, Cost, CostBenefit, ML };

static cl::opt<InlinePriorityMode> UseInlinePriority(
    "inline-priority-mode", cl::init(InlinePriorityMode::Size), cl::Hidden,
    cl::desc("Choose the priority mode to use in module inline"),
    cl::values(clEnumValN(InlinePriorityMode::Size, "size",
                          "Use callee size priority."),
               clEnumValN(InlinePriorityMode::Cost, "cost",
                          "Use inline cost priority."),
               clEnumValN(InlinePriorityMode::CostBenefit, "cost-benefit",
                          "Use cost-benefit ratio."),
               clEnumValN(InlinePriorityMode::ML, "ml", "Use ML.")));

static cl::opt<int> ModuleInlinerTopPriorityThreshold(
    "module-inliner-top-priority-threshold", cl::Hidden, cl::init(0),
    cl::desc("The cost threshold for call sites that get inlined without the "
             "cost-benefit analysis"));

namespace {

llvm::InlineCost getInlineCostWrapper(CallBase &CB,
                                      FunctionAnalysisManager &FAM,
                                      const InlineParams &Params) {
  Function &Caller = *CB.getCaller();
  ProfileSummaryInfo *PSI =
      FAM.getResult<ModuleAnalysisManagerFunctionProxy>(Caller)
          .getCachedResult<ProfileSummaryAnalysis>(
              *CB.getParent()->getParent()->getParent());

  auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(Caller);
  auto GetAssumptionCache = [&](Function &F) -> AssumptionCache & {
    return FAM.getResult<AssumptionAnalysis>(F);
  };
  auto GetBFI = [&](Function &F) -> BlockFrequencyInfo & {
    return FAM.getResult<BlockFrequencyAnalysis>(F);
  };
  auto GetTLI = [&](Function &F) -> const TargetLibraryInfo & {
    return FAM.getResult<TargetLibraryAnalysis>(F);
  };

  Function &Callee = *CB.getCalledFunction();
  auto &CalleeTTI = FAM.getResult<TargetIRAnalysis>(Callee);
  bool RemarksEnabled =
      Callee.getContext().getDiagHandlerPtr()->isMissedOptRemarkEnabled(
          DEBUG_TYPE);
  return getInlineCost(CB, Params, CalleeTTI, GetAssumptionCache, GetTLI,
                       GetBFI, PSI, RemarksEnabled ? &ORE : nullptr);
}

class SizePriority {
public:
  SizePriority() = default;
  SizePriority(const CallBase *CB, FunctionAnalysisManager &,
               const InlineParams &) {
    Function *Callee = CB->getCalledFunction();
    Size = Callee->getInstructionCount();
  }

  static bool isMoreDesirable(const SizePriority &P1, const SizePriority &P2) {
    return P1.Size < P2.Size;
  }

private:
  unsigned Size = UINT_MAX;
};

class CostPriority {
public:
  CostPriority() = default;
  CostPriority(const CallBase *CB, FunctionAnalysisManager &FAM,
               const InlineParams &Params) {
    auto IC = getInlineCostWrapper(const_cast<CallBase &>(*CB), FAM, Params);
    if (IC.isVariable())
      Cost = IC.getCost();
    else
      Cost = IC.isNever() ? INT_MAX : INT_MIN;
  }

  static bool isMoreDesirable(const CostPriority &P1, const CostPriority &P2) {
    return P1.Cost < P2.Cost;
  }

private:
  int Cost = INT_MAX;
};

class CostBenefitPriority {
public:
  CostBenefitPriority() = default;
  CostBenefitPriority(const CallBase *CB, FunctionAnalysisManager &FAM,
                      const InlineParams &Params) {
    auto IC = getInlineCostWrapper(const_cast<CallBase &>(*CB), FAM, Params);
    if (IC.isVariable())
      Cost = IC.getCost();
    else
      Cost = IC.isNever() ? INT_MAX : INT_MIN;
    StaticBonusApplied = IC.getStaticBonusApplied();
    CostBenefit = IC.getCostBenefit();
  }

  static bool isMoreDesirable(const CostBenefitPriority &P1,
                              const CostBenefitPriority &P2) {
    // We prioritize call sites in the dictionary order of the following
    // priorities:
    //
    // 1. Those call sites that are expected to reduce the caller size when
    //    inlined.  Within them, we prioritize those call sites with bigger
    //    reduction.
    //
    // 2. Those call sites that have gone through the cost-benefit analysis.
    //    Currently, they are limited to hot call sites.  Within them, we
    //    prioritize those call sites with higher benefit-to-cost ratios.
    //
    // 3. Remaining call sites are prioritized according to their costs.

    // We add back StaticBonusApplied to determine whether we expect the caller
    // to shrink (even if we don't delete the callee).
    bool P1ReducesCallerSize =
        P1.Cost + P1.StaticBonusApplied < ModuleInlinerTopPriorityThreshold;
    bool P2ReducesCallerSize =
        P2.Cost + P2.StaticBonusApplied < ModuleInlinerTopPriorityThreshold;
    if (P1ReducesCallerSize || P2ReducesCallerSize) {
      // If one reduces the caller size while the other doesn't, then return
      // true iff P1 reduces the caller size.
      if (P1ReducesCallerSize != P2ReducesCallerSize)
        return P1ReducesCallerSize;

      // If they both reduce the caller size, pick the one with the smaller
      // cost.
      return P1.Cost < P2.Cost;
    }

    bool P1HasCB = P1.CostBenefit.has_value();
    bool P2HasCB = P2.CostBenefit.has_value();
    if (P1HasCB || P2HasCB) {
      // If one has undergone the cost-benefit analysis while the other hasn't,
      // then return true iff P1 has.
      if (P1HasCB != P2HasCB)
        return P1HasCB;

      // If they have undergone the cost-benefit analysis, then pick the one
      // with a higher benefit-to-cost ratio.
      APInt LHS = P1.CostBenefit->getBenefit() * P2.CostBenefit->getCost();
      APInt RHS = P2.CostBenefit->getBenefit() * P1.CostBenefit->getCost();
      return LHS.ugt(RHS);
    }

    // Remaining call sites are ordered according to their costs.
    return P1.Cost < P2.Cost;
  }

private:
  int Cost = INT_MAX;
  int StaticBonusApplied = 0;
  std::optional<CostBenefitPair> CostBenefit;
};

class MLPriority {
public:
  MLPriority() = default;
  MLPriority(const CallBase *CB, FunctionAnalysisManager &FAM,
             const InlineParams &Params) {
    auto IC = getInlineCostWrapper(const_cast<CallBase &>(*CB), FAM, Params);
    if (IC.isVariable())
      Cost = IC.getCost();
    else
      Cost = IC.isNever() ? INT_MAX : INT_MIN;
  }

  static bool isMoreDesirable(const MLPriority &P1, const MLPriority &P2) {
    return P1.Cost < P2.Cost;
  }

private:
  int Cost = INT_MAX;
};

template <typename PriorityT>
class PriorityInlineOrder : public InlineOrder<std::pair<CallBase *, int>> {
  using T = std::pair<CallBase *, int>;

  bool hasLowerPriority(const CallBase *L, const CallBase *R) const {
    const auto I1 = Priorities.find(L);
    const auto I2 = Priorities.find(R);
    assert(I1 != Priorities.end() && I2 != Priorities.end());
    return PriorityT::isMoreDesirable(I2->second, I1->second);
  }

  bool updateAndCheckDecreased(const CallBase *CB) {
    auto It = Priorities.find(CB);
    const auto OldPriority = It->second;
    It->second = PriorityT(CB, FAM, Params);
    const auto NewPriority = It->second;
    return PriorityT::isMoreDesirable(OldPriority, NewPriority);
  }

  // A call site could become less desirable for inlining because of the size
  // growth from prior inlining into the callee. This method is used to lazily
  // update the desirability of a call site if it's decreasing. It is only
  // called on pop(), not every time the desirability changes. When the
  // desirability of the front call site decreases, an updated one would be
  // pushed right back into the heap. For simplicity, those cases where the
  // desirability of a call site increases are ignored here.
  void pop_heap_adjust() {
    std::pop_heap(Heap.begin(), Heap.end(), isLess);
    while (updateAndCheckDecreased(Heap.back())) {
      std::push_heap(Heap.begin(), Heap.end(), isLess);
      std::pop_heap(Heap.begin(), Heap.end(), isLess);
    }
  }

public:
  PriorityInlineOrder(FunctionAnalysisManager &FAM, const InlineParams &Params)
      : FAM(FAM), Params(Params) {
    isLess = [&](const CallBase *L, const CallBase *R) {
      return hasLowerPriority(L, R);
    };
  }

  size_t size() override { return Heap.size(); }

  void push(const T &Elt) override {
    CallBase *CB = Elt.first;
    const int InlineHistoryID = Elt.second;

    Heap.push_back(CB);
    Priorities[CB] = PriorityT(CB, FAM, Params);
    std::push_heap(Heap.begin(), Heap.end(), isLess);
    InlineHistoryMap[CB] = InlineHistoryID;
  }

  T pop() override {
    assert(size() > 0);
    pop_heap_adjust();

    CallBase *CB = Heap.pop_back_val();
    T Result = std::make_pair(CB, InlineHistoryMap[CB]);
    InlineHistoryMap.erase(CB);
    return Result;
  }

  void erase_if(function_ref<bool(T)> Pred) override {
    auto PredWrapper = [=](CallBase *CB) -> bool {
      return Pred(std::make_pair(CB, InlineHistoryMap[CB]));
    };
    llvm::erase_if(Heap, PredWrapper);
    std::make_heap(Heap.begin(), Heap.end(), isLess);
  }

private:
  SmallVector<CallBase *, 16> Heap;
  std::function<bool(const CallBase *L, const CallBase *R)> isLess;
  DenseMap<CallBase *, int> InlineHistoryMap;
  DenseMap<const CallBase *, PriorityT> Priorities;
  FunctionAnalysisManager &FAM;
  const InlineParams &Params;
};

} // namespace

AnalysisKey llvm::PluginInlineOrderAnalysis::Key;
bool llvm::PluginInlineOrderAnalysis::HasBeenRegistered;

std::unique_ptr<InlineOrder<std::pair<CallBase *, int>>>
llvm::getDefaultInlineOrder(FunctionAnalysisManager &FAM,
                            const InlineParams &Params,
                            ModuleAnalysisManager &MAM, Module &M) {
  switch (UseInlinePriority) {
  case InlinePriorityMode::Size:
    LLVM_DEBUG(dbgs() << "    Current used priority: Size priority ---- \n");
    return std::make_unique<PriorityInlineOrder<SizePriority>>(FAM, Params);

  case InlinePriorityMode::Cost:
    LLVM_DEBUG(dbgs() << "    Current used priority: Cost priority ---- \n");
    return std::make_unique<PriorityInlineOrder<CostPriority>>(FAM, Params);

  case InlinePriorityMode::CostBenefit:
    LLVM_DEBUG(
        dbgs() << "    Current used priority: cost-benefit priority ---- \n");
    return std::make_unique<PriorityInlineOrder<CostBenefitPriority>>(FAM,
                                                                      Params);
  case InlinePriorityMode::ML:
    LLVM_DEBUG(dbgs() << "    Current used priority: ML priority ---- \n");
    return std::make_unique<PriorityInlineOrder<MLPriority>>(FAM, Params);
  }
  return nullptr;
}

std::unique_ptr<InlineOrder<std::pair<CallBase *, int>>>
llvm::getInlineOrder(FunctionAnalysisManager &FAM, const InlineParams &Params,
                     ModuleAnalysisManager &MAM, Module &M) {
  if (llvm::PluginInlineOrderAnalysis::isRegistered()) {
    LLVM_DEBUG(dbgs() << "    Current used priority: plugin ---- \n");
    return MAM.getResult<PluginInlineOrderAnalysis>(M).Factory(FAM, Params, MAM,
                                                               M);
  }
  return getDefaultInlineOrder(FAM, Params, MAM, M);
}
