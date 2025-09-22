//===- AssumeBundleQueries.cpp - tool to query assume bundles ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/AssumeBundleQueries.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/DebugCounter.h"

#define DEBUG_TYPE "assume-queries"

using namespace llvm;
using namespace llvm::PatternMatch;

STATISTIC(NumAssumeQueries, "Number of Queries into an assume assume bundles");
STATISTIC(
    NumUsefullAssumeQueries,
    "Number of Queries into an assume assume bundles that were satisfied");

DEBUG_COUNTER(AssumeQueryCounter, "assume-queries-counter",
              "Controls which assumes gets created");

static bool bundleHasArgument(const CallBase::BundleOpInfo &BOI, unsigned Idx) {
  return BOI.End - BOI.Begin > Idx;
}

static Value *getValueFromBundleOpInfo(AssumeInst &Assume,
                                       const CallBase::BundleOpInfo &BOI,
                                       unsigned Idx) {
  assert(bundleHasArgument(BOI, Idx) && "index out of range");
  return (Assume.op_begin() + BOI.Begin + Idx)->get();
}

bool llvm::hasAttributeInAssume(AssumeInst &Assume, Value *IsOn,
                                StringRef AttrName, uint64_t *ArgVal) {
  assert(Attribute::isExistingAttribute(AttrName) &&
         "this attribute doesn't exist");
  assert((ArgVal == nullptr || Attribute::isIntAttrKind(
                                   Attribute::getAttrKindFromName(AttrName))) &&
         "requested value for an attribute that has no argument");
  if (Assume.bundle_op_infos().empty())
    return false;

  for (auto &BOI : Assume.bundle_op_infos()) {
    if (BOI.Tag->getKey() != AttrName)
      continue;
    if (IsOn && (BOI.End - BOI.Begin <= ABA_WasOn ||
                 IsOn != getValueFromBundleOpInfo(Assume, BOI, ABA_WasOn)))
      continue;
    if (ArgVal) {
      assert(BOI.End - BOI.Begin > ABA_Argument);
      *ArgVal =
          cast<ConstantInt>(getValueFromBundleOpInfo(Assume, BOI, ABA_Argument))
              ->getZExtValue();
    }
    return true;
  }
  return false;
}

void llvm::fillMapFromAssume(AssumeInst &Assume, RetainedKnowledgeMap &Result) {
  for (auto &Bundles : Assume.bundle_op_infos()) {
    std::pair<Value *, Attribute::AttrKind> Key{
        nullptr, Attribute::getAttrKindFromName(Bundles.Tag->getKey())};
    if (bundleHasArgument(Bundles, ABA_WasOn))
      Key.first = getValueFromBundleOpInfo(Assume, Bundles, ABA_WasOn);

    if (Key.first == nullptr && Key.second == Attribute::None)
      continue;
    if (!bundleHasArgument(Bundles, ABA_Argument)) {
      Result[Key][&Assume] = {0, 0};
      continue;
    }
    auto *CI = dyn_cast<ConstantInt>(
        getValueFromBundleOpInfo(Assume, Bundles, ABA_Argument));
    if (!CI)
      continue;
    uint64_t Val = CI->getZExtValue();
    auto Lookup = Result.find(Key);
    if (Lookup == Result.end() || !Lookup->second.count(&Assume)) {
      Result[Key][&Assume] = {Val, Val};
      continue;
    }
    Lookup->second[&Assume].Min = std::min(Val, Lookup->second[&Assume].Min);
    Lookup->second[&Assume].Max = std::max(Val, Lookup->second[&Assume].Max);
  }
}

RetainedKnowledge
llvm::getKnowledgeFromBundle(AssumeInst &Assume,
                             const CallBase::BundleOpInfo &BOI) {
  RetainedKnowledge Result;
  if (!DebugCounter::shouldExecute(AssumeQueryCounter))
    return Result;

  Result.AttrKind = Attribute::getAttrKindFromName(BOI.Tag->getKey());
  if (bundleHasArgument(BOI, ABA_WasOn))
    Result.WasOn = getValueFromBundleOpInfo(Assume, BOI, ABA_WasOn);
  auto GetArgOr1 = [&](unsigned Idx) -> uint64_t {
    if (auto *ConstInt = dyn_cast<ConstantInt>(
            getValueFromBundleOpInfo(Assume, BOI, ABA_Argument + Idx)))
      return ConstInt->getZExtValue();
    return 1;
  };
  if (BOI.End - BOI.Begin > ABA_Argument)
    Result.ArgValue = GetArgOr1(0);
  if (Result.AttrKind == Attribute::Alignment)
    if (BOI.End - BOI.Begin > ABA_Argument + 1)
      Result.ArgValue = MinAlign(Result.ArgValue, GetArgOr1(1));
  return Result;
}

RetainedKnowledge llvm::getKnowledgeFromOperandInAssume(AssumeInst &Assume,
                                                        unsigned Idx) {
  CallBase::BundleOpInfo BOI = Assume.getBundleOpInfoForOperand(Idx);
  return getKnowledgeFromBundle(Assume, BOI);
}

bool llvm::isAssumeWithEmptyBundle(const AssumeInst &Assume) {
  return none_of(Assume.bundle_op_infos(),
                 [](const CallBase::BundleOpInfo &BOI) {
                   return BOI.Tag->getKey() != IgnoreBundleTag;
                 });
}

static CallInst::BundleOpInfo *getBundleFromUse(const Use *U) {
  if (!match(U->getUser(),
             m_Intrinsic<Intrinsic::assume>(m_Unless(m_Specific(U->get())))))
    return nullptr;
  auto *Intr = cast<IntrinsicInst>(U->getUser());
  return &Intr->getBundleOpInfoForOperand(U->getOperandNo());
}

RetainedKnowledge
llvm::getKnowledgeFromUse(const Use *U,
                          ArrayRef<Attribute::AttrKind> AttrKinds) {
  CallInst::BundleOpInfo* Bundle = getBundleFromUse(U);
  if (!Bundle)
    return RetainedKnowledge::none();
  RetainedKnowledge RK =
      getKnowledgeFromBundle(*cast<AssumeInst>(U->getUser()), *Bundle);
  if (llvm::is_contained(AttrKinds, RK.AttrKind))
    return RK;
  return RetainedKnowledge::none();
}

RetainedKnowledge
llvm::getKnowledgeForValue(const Value *V,
                           ArrayRef<Attribute::AttrKind> AttrKinds,
                           AssumptionCache *AC,
                           function_ref<bool(RetainedKnowledge, Instruction *,
                                             const CallBase::BundleOpInfo *)>
                               Filter) {
  NumAssumeQueries++;
  if (AC) {
    for (AssumptionCache::ResultElem &Elem : AC->assumptionsFor(V)) {
      auto *II = cast_or_null<AssumeInst>(Elem.Assume);
      if (!II || Elem.Index == AssumptionCache::ExprResultIdx)
        continue;
      if (RetainedKnowledge RK = getKnowledgeFromBundle(
              *II, II->bundle_op_info_begin()[Elem.Index])) {
        if (V != RK.WasOn)
          continue;
        if (is_contained(AttrKinds, RK.AttrKind) &&
            Filter(RK, II, &II->bundle_op_info_begin()[Elem.Index])) {
          NumUsefullAssumeQueries++;
          return RK;
        }
      }
    }
    return RetainedKnowledge::none();
  }
  for (const auto &U : V->uses()) {
    CallInst::BundleOpInfo* Bundle = getBundleFromUse(&U);
    if (!Bundle)
      continue;
    if (RetainedKnowledge RK =
            getKnowledgeFromBundle(*cast<AssumeInst>(U.getUser()), *Bundle))
      if (is_contained(AttrKinds, RK.AttrKind) &&
          Filter(RK, cast<Instruction>(U.getUser()), Bundle)) {
        NumUsefullAssumeQueries++;
        return RK;
      }
  }
  return RetainedKnowledge::none();
}

RetainedKnowledge llvm::getKnowledgeValidInContext(
    const Value *V, ArrayRef<Attribute::AttrKind> AttrKinds,
    const Instruction *CtxI, const DominatorTree *DT, AssumptionCache *AC) {
  return getKnowledgeForValue(V, AttrKinds, AC,
                              [&](auto, Instruction *I, auto) {
                                return isValidAssumeForContext(I, CtxI, DT);
                              });
}
