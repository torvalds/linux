//===- llvm/Analysis/ProfileSummaryInfo.h - profile summary ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that provides access to profile summary
// information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_PROFILESUMMARYINFO_H
#define LLVM_ANALYSIS_PROFILESUMMARYINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/Pass.h"
#include <memory>
#include <optional>

namespace llvm {
class BasicBlock;
class CallBase;
class MachineFunction;

/// Analysis providing profile information.
///
/// This is an immutable analysis pass that provides ability to query global
/// (program-level) profile information. The main APIs are isHotCount and
/// isColdCount that tells whether a given profile count is considered hot/cold
/// based on the profile summary. This also provides convenience methods to
/// check whether a function is hot or cold.

// FIXME: Provide convenience methods to determine hotness/coldness of other IR
// units. This would require making this depend on BFI.
class ProfileSummaryInfo {
private:
  const Module *M;
  std::unique_ptr<ProfileSummary> Summary;
  void computeThresholds();
  // Count thresholds to answer isHotCount and isColdCount queries.
  std::optional<uint64_t> HotCountThreshold, ColdCountThreshold;
  // True if the working set size of the code is considered huge,
  // because the number of profile counts required to reach the hot
  // percentile is above a huge threshold.
  std::optional<bool> HasHugeWorkingSetSize;
  // True if the working set size of the code is considered large,
  // because the number of profile counts required to reach the hot
  // percentile is above a large threshold.
  std::optional<bool> HasLargeWorkingSetSize;
  // Compute the threshold for a given cutoff.
  std::optional<uint64_t> computeThreshold(int PercentileCutoff) const;
  // The map that caches the threshold values. The keys are the percentile
  // cutoff values and the values are the corresponding threshold values.
  mutable DenseMap<int, uint64_t> ThresholdCache;

public:
  ProfileSummaryInfo(const Module &M) : M(&M) { refresh(); }
  ProfileSummaryInfo(ProfileSummaryInfo &&Arg) = default;

  /// If no summary is present, attempt to refresh.
  void refresh();

  /// Returns true if profile summary is available.
  bool hasProfileSummary() const { return Summary != nullptr; }

  /// Returns true if module \c M has sample profile.
  bool hasSampleProfile() const {
    return hasProfileSummary() &&
           Summary->getKind() == ProfileSummary::PSK_Sample;
  }

  /// Returns true if module \c M has instrumentation profile.
  bool hasInstrumentationProfile() const {
    return hasProfileSummary() &&
           Summary->getKind() == ProfileSummary::PSK_Instr;
  }

  /// Returns true if module \c M has context sensitive instrumentation profile.
  bool hasCSInstrumentationProfile() const {
    return hasProfileSummary() &&
           Summary->getKind() == ProfileSummary::PSK_CSInstr;
  }

  /// Handle the invalidation of this information.
  ///
  /// When used as a result of \c ProfileSummaryAnalysis this method will be
  /// called when the module this was computed for changes. Since profile
  /// summary is immutable after it is annotated on the module, we return false
  /// here.
  bool invalidate(Module &, const PreservedAnalyses &,
                  ModuleAnalysisManager::Invalidator &) {
    return false;
  }

  /// Returns the profile count for \p CallInst.
  std::optional<uint64_t> getProfileCount(const CallBase &CallInst,
                                          BlockFrequencyInfo *BFI,
                                          bool AllowSynthetic = false) const;
  /// Returns true if module \c M has partial-profile sample profile.
  bool hasPartialSampleProfile() const;
  /// Returns true if the working set size of the code is considered huge.
  bool hasHugeWorkingSetSize() const;
  /// Returns true if the working set size of the code is considered large.
  bool hasLargeWorkingSetSize() const;
  /// Returns true if \p F has hot function entry. If it returns false, it
  /// either means it is not hot or it is unknown whether it is hot or not (for
  /// example, no profile data is available).
  template <typename FuncT> bool isFunctionEntryHot(const FuncT *F) const {
    if (!F || !hasProfileSummary())
      return false;
    std::optional<Function::ProfileCount> FunctionCount = getEntryCount(F);
    // FIXME: The heuristic used below for determining hotness is based on
    // preliminary SPEC tuning for inliner. This will eventually be a
    // convenience method that calls isHotCount.
    return FunctionCount && isHotCount(FunctionCount->getCount());
  }

  /// Returns true if \p F contains hot code.
  template <typename FuncT, typename BFIT>
  bool isFunctionHotInCallGraph(const FuncT *F, BFIT &BFI) const {
    if (!F || !hasProfileSummary())
      return false;
    if (auto FunctionCount = getEntryCount(F))
      if (isHotCount(FunctionCount->getCount()))
        return true;

    if (auto TotalCallCount = getTotalCallCount(F))
      if (isHotCount(*TotalCallCount))
        return true;

    for (const auto &BB : *F)
      if (isHotBlock(&BB, &BFI))
        return true;
    return false;
  }
  /// Returns true if \p F has cold function entry.
  bool isFunctionEntryCold(const Function *F) const;
  /// Returns true if \p F contains only cold code.
  template <typename FuncT, typename BFIT>
  bool isFunctionColdInCallGraph(const FuncT *F, BFIT &BFI) const {
    if (!F || !hasProfileSummary())
      return false;
    if (auto FunctionCount = getEntryCount(F))
      if (!isColdCount(FunctionCount->getCount()))
        return false;

    if (auto TotalCallCount = getTotalCallCount(F))
      if (!isColdCount(*TotalCallCount))
        return false;

    for (const auto &BB : *F)
      if (!isColdBlock(&BB, &BFI))
        return false;
    return true;
  }
  /// Returns true if the hotness of \p F is unknown.
  bool isFunctionHotnessUnknown(const Function &F) const;
  /// Returns true if \p F contains hot code with regard to a given hot
  /// percentile cutoff value.
  template <typename FuncT, typename BFIT>
  bool isFunctionHotInCallGraphNthPercentile(int PercentileCutoff,
                                             const FuncT *F, BFIT &BFI) const {
    return isFunctionHotOrColdInCallGraphNthPercentile<true, FuncT, BFIT>(
        PercentileCutoff, F, BFI);
  }
  /// Returns true if \p F contains cold code with regard to a given cold
  /// percentile cutoff value.
  template <typename FuncT, typename BFIT>
  bool isFunctionColdInCallGraphNthPercentile(int PercentileCutoff,
                                              const FuncT *F, BFIT &BFI) const {
    return isFunctionHotOrColdInCallGraphNthPercentile<false, FuncT, BFIT>(
        PercentileCutoff, F, BFI);
  }
  /// Returns true if count \p C is considered hot.
  bool isHotCount(uint64_t C) const;
  /// Returns true if count \p C is considered cold.
  bool isColdCount(uint64_t C) const;
  /// Returns true if count \p C is considered hot with regard to a given
  /// hot percentile cutoff value.
  /// PercentileCutoff is encoded as a 6 digit decimal fixed point number, where
  /// the first two digits are the whole part. E.g. 995000 for 99.5 percentile.
  bool isHotCountNthPercentile(int PercentileCutoff, uint64_t C) const;
  /// Returns true if count \p C is considered cold with regard to a given
  /// cold percentile cutoff value.
  /// PercentileCutoff is encoded as a 6 digit decimal fixed point number, where
  /// the first two digits are the whole part. E.g. 995000 for 99.5 percentile.
  bool isColdCountNthPercentile(int PercentileCutoff, uint64_t C) const;

  /// Returns true if BasicBlock \p BB is considered hot.
  template <typename BBType, typename BFIT>
  bool isHotBlock(const BBType *BB, BFIT *BFI) const {
    auto Count = BFI->getBlockProfileCount(BB);
    return Count && isHotCount(*Count);
  }

  /// Returns true if BasicBlock \p BB is considered cold.
  template <typename BBType, typename BFIT>
  bool isColdBlock(const BBType *BB, BFIT *BFI) const {
    auto Count = BFI->getBlockProfileCount(BB);
    return Count && isColdCount(*Count);
  }

  template <typename BFIT>
  bool isColdBlock(BlockFrequency BlockFreq, const BFIT *BFI) const {
    auto Count = BFI->getProfileCountFromFreq(BlockFreq);
    return Count && isColdCount(*Count);
  }

  template <typename BBType, typename BFIT>
  bool isHotBlockNthPercentile(int PercentileCutoff, const BBType *BB,
                               BFIT *BFI) const {
    return isHotOrColdBlockNthPercentile<true, BBType, BFIT>(PercentileCutoff,
                                                             BB, BFI);
  }

  template <typename BFIT>
  bool isHotBlockNthPercentile(int PercentileCutoff, BlockFrequency BlockFreq,
                               BFIT *BFI) const {
    return isHotOrColdBlockNthPercentile<true, BFIT>(PercentileCutoff,
                                                     BlockFreq, BFI);
  }

  /// Returns true if BasicBlock \p BB is considered cold with regard to a given
  /// cold percentile cutoff value.
  /// PercentileCutoff is encoded as a 6 digit decimal fixed point number, where
  /// the first two digits are the whole part. E.g. 995000 for 99.5 percentile.
  template <typename BBType, typename BFIT>
  bool isColdBlockNthPercentile(int PercentileCutoff, const BBType *BB,
                                BFIT *BFI) const {
    return isHotOrColdBlockNthPercentile<false, BBType, BFIT>(PercentileCutoff,
                                                              BB, BFI);
  }
  template <typename BFIT>
  bool isColdBlockNthPercentile(int PercentileCutoff, BlockFrequency BlockFreq,
                                BFIT *BFI) const {
    return isHotOrColdBlockNthPercentile<false, BFIT>(PercentileCutoff,
                                                      BlockFreq, BFI);
  }
  /// Returns true if the call site \p CB is considered hot.
  bool isHotCallSite(const CallBase &CB, BlockFrequencyInfo *BFI) const;
  /// Returns true if call site \p CB is considered cold.
  bool isColdCallSite(const CallBase &CB, BlockFrequencyInfo *BFI) const;
  /// Returns HotCountThreshold if set. Recompute HotCountThreshold
  /// if not set.
  uint64_t getOrCompHotCountThreshold() const;
  /// Returns ColdCountThreshold if set. Recompute HotCountThreshold
  /// if not set.
  uint64_t getOrCompColdCountThreshold() const;
  /// Returns HotCountThreshold if set.
  uint64_t getHotCountThreshold() const {
    return HotCountThreshold.value_or(0);
  }
  /// Returns ColdCountThreshold if set.
  uint64_t getColdCountThreshold() const {
    return ColdCountThreshold.value_or(0);
  }

private:
  template <typename FuncT>
  std::optional<uint64_t> getTotalCallCount(const FuncT *F) const {
    return std::nullopt;
  }

  template <bool isHot, typename FuncT, typename BFIT>
  bool isFunctionHotOrColdInCallGraphNthPercentile(int PercentileCutoff,
                                                   const FuncT *F,
                                                   BFIT &FI) const {
    if (!F || !hasProfileSummary())
      return false;
    if (auto FunctionCount = getEntryCount(F)) {
      if (isHot &&
          isHotCountNthPercentile(PercentileCutoff, FunctionCount->getCount()))
        return true;
      if (!isHot && !isColdCountNthPercentile(PercentileCutoff,
                                              FunctionCount->getCount()))
        return false;
    }
    if (auto TotalCallCount = getTotalCallCount(F)) {
      if (isHot && isHotCountNthPercentile(PercentileCutoff, *TotalCallCount))
        return true;
      if (!isHot &&
          !isColdCountNthPercentile(PercentileCutoff, *TotalCallCount))
        return false;
    }
    for (const auto &BB : *F) {
      if (isHot && isHotBlockNthPercentile(PercentileCutoff, &BB, &FI))
        return true;
      if (!isHot && !isColdBlockNthPercentile(PercentileCutoff, &BB, &FI))
        return false;
    }
    return !isHot;
  }

  template <bool isHot>
  bool isHotOrColdCountNthPercentile(int PercentileCutoff, uint64_t C) const;

  template <bool isHot, typename BBType, typename BFIT>
  bool isHotOrColdBlockNthPercentile(int PercentileCutoff, const BBType *BB,
                                     BFIT *BFI) const {
    auto Count = BFI->getBlockProfileCount(BB);
    if (isHot)
      return Count && isHotCountNthPercentile(PercentileCutoff, *Count);
    else
      return Count && isColdCountNthPercentile(PercentileCutoff, *Count);
  }

  template <bool isHot, typename BFIT>
  bool isHotOrColdBlockNthPercentile(int PercentileCutoff,
                                     BlockFrequency BlockFreq,
                                     BFIT *BFI) const {
    auto Count = BFI->getProfileCountFromFreq(BlockFreq);
    if (isHot)
      return Count && isHotCountNthPercentile(PercentileCutoff, *Count);
    else
      return Count && isColdCountNthPercentile(PercentileCutoff, *Count);
  }

  template <typename FuncT>
  std::optional<Function::ProfileCount> getEntryCount(const FuncT *F) const {
    return F->getEntryCount();
  }
};

template <>
inline std::optional<uint64_t>
ProfileSummaryInfo::getTotalCallCount<Function>(const Function *F) const {
  if (!hasSampleProfile())
    return std::nullopt;
  uint64_t TotalCallCount = 0;
  for (const auto &BB : *F)
    for (const auto &I : BB)
      if (isa<CallInst>(I) || isa<InvokeInst>(I))
        if (auto CallCount = getProfileCount(cast<CallBase>(I), nullptr))
          TotalCallCount += *CallCount;
  return TotalCallCount;
}

// Declare template specialization for llvm::MachineFunction. Do not implement
// here, because we cannot include MachineFunction header here, that would break
// dependency rules.
template <>
std::optional<Function::ProfileCount>
ProfileSummaryInfo::getEntryCount<MachineFunction>(
    const MachineFunction *F) const;

/// An analysis pass based on legacy pass manager to deliver ProfileSummaryInfo.
class ProfileSummaryInfoWrapperPass : public ImmutablePass {
  std::unique_ptr<ProfileSummaryInfo> PSI;

public:
  static char ID;
  ProfileSummaryInfoWrapperPass();

  ProfileSummaryInfo &getPSI() { return *PSI; }
  const ProfileSummaryInfo &getPSI() const { return *PSI; }

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

/// An analysis pass based on the new PM to deliver ProfileSummaryInfo.
class ProfileSummaryAnalysis
    : public AnalysisInfoMixin<ProfileSummaryAnalysis> {
public:
  typedef ProfileSummaryInfo Result;

  Result run(Module &M, ModuleAnalysisManager &);

private:
  friend AnalysisInfoMixin<ProfileSummaryAnalysis>;
  static AnalysisKey Key;
};

/// Printer pass that uses \c ProfileSummaryAnalysis.
class ProfileSummaryPrinterPass
    : public PassInfoMixin<ProfileSummaryPrinterPass> {
  raw_ostream &OS;

public:
  explicit ProfileSummaryPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // end namespace llvm

#endif
