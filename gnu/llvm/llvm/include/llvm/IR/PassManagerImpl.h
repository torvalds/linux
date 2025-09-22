//===- PassManagerImpl.h - Pass management infrastructure -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Provides implementations for PassManager and AnalysisManager template
/// methods. These classes should be explicitly instantiated for any IR unit,
/// and files doing the explicit instantiation should include this header.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PASSMANAGERIMPL_H
#define LLVM_IR_PASSMANAGERIMPL_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/PrettyStackTrace.h"

extern llvm::cl::opt<bool> UseNewDbgInfoFormat;

namespace llvm {

template <typename IRUnitT, typename AnalysisManagerT, typename... ExtraArgTs>
PreservedAnalyses PassManager<IRUnitT, AnalysisManagerT, ExtraArgTs...>::run(
    IRUnitT &IR, AnalysisManagerT &AM, ExtraArgTs... ExtraArgs) {
  class StackTraceEntry : public PrettyStackTraceEntry {
    const PassInstrumentation &PI;
    IRUnitT &IR;
    PassConceptT *Pass = nullptr;

  public:
    explicit StackTraceEntry(const PassInstrumentation &PI, IRUnitT &IR)
        : PI(PI), IR(IR) {}

    void setPass(PassConceptT *P) { Pass = P; }

    void print(raw_ostream &OS) const override {
      OS << "Running pass \"";
      if (Pass)
        Pass->printPipeline(OS, [this](StringRef ClassName) {
          auto PassName = PI.getPassNameForClassName(ClassName);
          return PassName.empty() ? ClassName : PassName;
        });
      else
        OS << "unknown";
      OS << "\" on ";
      printIRUnitNameForStackTrace(OS, IR);
      OS << "\n";
    }
  };

  PreservedAnalyses PA = PreservedAnalyses::all();

  // Request PassInstrumentation from analysis manager, will use it to run
  // instrumenting callbacks for the passes later.
  // Here we use std::tuple wrapper over getResult which helps to extract
  // AnalysisManager's arguments out of the whole ExtraArgs set.
  PassInstrumentation PI =
      detail::getAnalysisResult<PassInstrumentationAnalysis>(
          AM, IR, std::tuple<ExtraArgTs...>(ExtraArgs...));

  // RemoveDIs: if requested, convert debug-info to DbgRecord representation
  // for duration of these passes.
  ScopedDbgInfoFormatSetter FormatSetter(IR, UseNewDbgInfoFormat);

  StackTraceEntry Entry(PI, IR);
  for (auto &Pass : Passes) {
    Entry.setPass(&*Pass);

    // Check the PassInstrumentation's BeforePass callbacks before running the
    // pass, skip its execution completely if asked to (callback returns
    // false).
    if (!PI.runBeforePass<IRUnitT>(*Pass, IR))
      continue;

    PreservedAnalyses PassPA = Pass->run(IR, AM, ExtraArgs...);

    // Update the analysis manager as each pass runs and potentially
    // invalidates analyses.
    AM.invalidate(IR, PassPA);

    // Call onto PassInstrumentation's AfterPass callbacks immediately after
    // running the pass.
    PI.runAfterPass<IRUnitT>(*Pass, IR, PassPA);

    // Finally, intersect the preserved analyses to compute the aggregate
    // preserved set for this pass manager.
    PA.intersect(std::move(PassPA));
  }

  // Invalidation was handled after each pass in the above loop for the
  // current unit of IR. Therefore, the remaining analysis results in the
  // AnalysisManager are preserved. We mark this with a set so that we don't
  // need to inspect each one individually.
  PA.preserveSet<AllAnalysesOn<IRUnitT>>();

  return PA;
}

template <typename IRUnitT, typename... ExtraArgTs>
inline AnalysisManager<IRUnitT, ExtraArgTs...>::AnalysisManager() = default;

template <typename IRUnitT, typename... ExtraArgTs>
inline AnalysisManager<IRUnitT, ExtraArgTs...>::AnalysisManager(
    AnalysisManager &&) = default;

template <typename IRUnitT, typename... ExtraArgTs>
inline AnalysisManager<IRUnitT, ExtraArgTs...> &
AnalysisManager<IRUnitT, ExtraArgTs...>::operator=(AnalysisManager &&) =
    default;

template <typename IRUnitT, typename... ExtraArgTs>
inline void
AnalysisManager<IRUnitT, ExtraArgTs...>::clear(IRUnitT &IR,
                                               llvm::StringRef Name) {
  if (auto *PI = getCachedResult<PassInstrumentationAnalysis>(IR))
    PI->runAnalysesCleared(Name);

  auto ResultsListI = AnalysisResultLists.find(&IR);
  if (ResultsListI == AnalysisResultLists.end())
    return;
  // Delete the map entries that point into the results list.
  for (auto &IDAndResult : ResultsListI->second)
    AnalysisResults.erase({IDAndResult.first, &IR});

  // And actually destroy and erase the results associated with this IR.
  AnalysisResultLists.erase(ResultsListI);
}

template <typename IRUnitT, typename... ExtraArgTs>
inline typename AnalysisManager<IRUnitT, ExtraArgTs...>::ResultConceptT &
AnalysisManager<IRUnitT, ExtraArgTs...>::getResultImpl(
    AnalysisKey *ID, IRUnitT &IR, ExtraArgTs... ExtraArgs) {
  typename AnalysisResultMapT::iterator RI;
  bool Inserted;
  std::tie(RI, Inserted) = AnalysisResults.insert(std::make_pair(
      std::make_pair(ID, &IR), typename AnalysisResultListT::iterator()));

  // If we don't have a cached result for this function, look up the pass and
  // run it to produce a result, which we then add to the cache.
  if (Inserted) {
    auto &P = this->lookUpPass(ID);

    PassInstrumentation PI;
    if (ID != PassInstrumentationAnalysis::ID()) {
      PI = getResult<PassInstrumentationAnalysis>(IR, ExtraArgs...);
      PI.runBeforeAnalysis(P, IR);
    }

    AnalysisResultListT &ResultList = AnalysisResultLists[&IR];
    ResultList.emplace_back(ID, P.run(IR, *this, ExtraArgs...));

    PI.runAfterAnalysis(P, IR);

    // P.run may have inserted elements into AnalysisResults and invalidated
    // RI.
    RI = AnalysisResults.find({ID, &IR});
    assert(RI != AnalysisResults.end() && "we just inserted it!");

    RI->second = std::prev(ResultList.end());
  }

  return *RI->second->second;
}

template <typename IRUnitT, typename... ExtraArgTs>
inline void AnalysisManager<IRUnitT, ExtraArgTs...>::invalidate(
    IRUnitT &IR, const PreservedAnalyses &PA) {
  // We're done if all analyses on this IR unit are preserved.
  if (PA.allAnalysesInSetPreserved<AllAnalysesOn<IRUnitT>>())
    return;

  // Track whether each analysis's result is invalidated in
  // IsResultInvalidated.
  SmallDenseMap<AnalysisKey *, bool, 8> IsResultInvalidated;
  Invalidator Inv(IsResultInvalidated, AnalysisResults);
  AnalysisResultListT &ResultsList = AnalysisResultLists[&IR];
  for (auto &AnalysisResultPair : ResultsList) {
    // This is basically the same thing as Invalidator::invalidate, but we
    // can't call it here because we're operating on the type-erased result.
    // Moreover if we instead called invalidate() directly, it would do an
    // unnecessary look up in ResultsList.
    AnalysisKey *ID = AnalysisResultPair.first;
    auto &Result = *AnalysisResultPair.second;

    auto IMapI = IsResultInvalidated.find(ID);
    if (IMapI != IsResultInvalidated.end())
      // This result was already handled via the Invalidator.
      continue;

    // Try to invalidate the result, giving it the Invalidator so it can
    // recursively query for any dependencies it has and record the result.
    // Note that we cannot reuse 'IMapI' here or pre-insert the ID, as
    // Result.invalidate may insert things into the map, invalidating our
    // iterator.
    bool Inserted =
        IsResultInvalidated.insert({ID, Result.invalidate(IR, PA, Inv)}).second;
    (void)Inserted;
    assert(Inserted && "Should never have already inserted this ID, likely "
                       "indicates a cycle!");
  }

  // Now erase the results that were marked above as invalidated.
  if (!IsResultInvalidated.empty()) {
    for (auto I = ResultsList.begin(), E = ResultsList.end(); I != E;) {
      AnalysisKey *ID = I->first;
      if (!IsResultInvalidated.lookup(ID)) {
        ++I;
        continue;
      }

      if (auto *PI = getCachedResult<PassInstrumentationAnalysis>(IR))
        PI->runAnalysisInvalidated(this->lookUpPass(ID), IR);

      I = ResultsList.erase(I);
      AnalysisResults.erase({ID, &IR});
    }
  }

  if (ResultsList.empty())
    AnalysisResultLists.erase(&IR);
}
} // end namespace llvm

#endif // LLVM_IR_PASSMANAGERIMPL_H
