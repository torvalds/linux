//=-- ProfilesummaryBuilder.cpp - Profile summary computation ---------------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for computing profile summary data.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/ProfileCommon.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

namespace llvm {
cl::opt<bool> UseContextLessSummary(
    "profile-summary-contextless", cl::Hidden,
    cl::desc("Merge context profiles before calculating thresholds."));

// The following two parameters determine the threshold for a count to be
// considered hot/cold. These two parameters are percentile values (multiplied
// by 10000). If the counts are sorted in descending order, the minimum count to
// reach ProfileSummaryCutoffHot gives the threshold to determine a hot count.
// Similarly, the minimum count to reach ProfileSummaryCutoffCold gives the
// threshold for determining cold count (everything <= this threshold is
// considered cold).
cl::opt<int> ProfileSummaryCutoffHot(
    "profile-summary-cutoff-hot", cl::Hidden, cl::init(990000),
    cl::desc("A count is hot if it exceeds the minimum count to"
             " reach this percentile of total counts."));

cl::opt<int> ProfileSummaryCutoffCold(
    "profile-summary-cutoff-cold", cl::Hidden, cl::init(999999),
    cl::desc("A count is cold if it is below the minimum count"
             " to reach this percentile of total counts."));

cl::opt<unsigned> ProfileSummaryHugeWorkingSetSizeThreshold(
    "profile-summary-huge-working-set-size-threshold", cl::Hidden,
    cl::init(15000),
    cl::desc("The code working set size is considered huge if the number of"
             " blocks required to reach the -profile-summary-cutoff-hot"
             " percentile exceeds this count."));

cl::opt<unsigned> ProfileSummaryLargeWorkingSetSizeThreshold(
    "profile-summary-large-working-set-size-threshold", cl::Hidden,
    cl::init(12500),
    cl::desc("The code working set size is considered large if the number of"
             " blocks required to reach the -profile-summary-cutoff-hot"
             " percentile exceeds this count."));

// The next two options override the counts derived from summary computation and
// are useful for debugging purposes.
cl::opt<uint64_t> ProfileSummaryHotCount(
    "profile-summary-hot-count", cl::ReallyHidden,
    cl::desc("A fixed hot count that overrides the count derived from"
             " profile-summary-cutoff-hot"));

cl::opt<uint64_t> ProfileSummaryColdCount(
    "profile-summary-cold-count", cl::ReallyHidden,
    cl::desc("A fixed cold count that overrides the count derived from"
             " profile-summary-cutoff-cold"));
} // namespace llvm

// A set of cutoff values. Each value, when divided by ProfileSummary::Scale
// (which is 1000000) is a desired percentile of total counts.
static const uint32_t DefaultCutoffsData[] = {
    10000,  /*  1% */
    100000, /* 10% */
    200000, 300000, 400000, 500000, 600000, 700000, 800000,
    900000, 950000, 990000, 999000, 999900, 999990, 999999};
const ArrayRef<uint32_t> ProfileSummaryBuilder::DefaultCutoffs =
    DefaultCutoffsData;

const ProfileSummaryEntry &
ProfileSummaryBuilder::getEntryForPercentile(const SummaryEntryVector &DS,
                                             uint64_t Percentile) {
  auto It = partition_point(DS, [=](const ProfileSummaryEntry &Entry) {
    return Entry.Cutoff < Percentile;
  });
  // The required percentile has to be <= one of the percentiles in the
  // detailed summary.
  if (It == DS.end())
    report_fatal_error("Desired percentile exceeds the maximum cutoff");
  return *It;
}

void InstrProfSummaryBuilder::addRecord(const InstrProfRecord &R) {
  // The first counter is not necessarily an entry count for IR
  // instrumentation profiles.
  // Eventually MaxFunctionCount will become obsolete and this can be
  // removed.

  if (R.getCountPseudoKind() != InstrProfRecord::NotPseudo)
    return;

  addEntryCount(R.Counts[0]);
  for (size_t I = 1, E = R.Counts.size(); I < E; ++I)
    addInternalCount(R.Counts[I]);
}

// To compute the detailed summary, we consider each line containing samples as
// equivalent to a block with a count in the instrumented profile.
void SampleProfileSummaryBuilder::addRecord(
    const sampleprof::FunctionSamples &FS, bool isCallsiteSample) {
  if (!isCallsiteSample) {
    NumFunctions++;
    if (FS.getHeadSamples() > MaxFunctionCount)
      MaxFunctionCount = FS.getHeadSamples();
  } else if (FS.getContext().hasAttribute(
                 sampleprof::ContextDuplicatedIntoBase)) {
    // Do not recount callee samples if they are already merged into their base
    // profiles. This can happen to CS nested profile.
    return;
  }

  for (const auto &I : FS.getBodySamples()) {
    uint64_t Count = I.second.getSamples();
      addCount(Count);
  }
  for (const auto &I : FS.getCallsiteSamples())
    for (const auto &CS : I.second)
      addRecord(CS.second, true);
}

// The argument to this method is a vector of cutoff percentages and the return
// value is a vector of (Cutoff, MinCount, NumCounts) triplets.
void ProfileSummaryBuilder::computeDetailedSummary() {
  if (DetailedSummaryCutoffs.empty())
    return;
  llvm::sort(DetailedSummaryCutoffs);
  auto Iter = CountFrequencies.begin();
  const auto End = CountFrequencies.end();

  uint32_t CountsSeen = 0;
  uint64_t CurrSum = 0, Count = 0;

  for (const uint32_t Cutoff : DetailedSummaryCutoffs) {
    assert(Cutoff <= 999999);
    APInt Temp(128, TotalCount);
    APInt N(128, Cutoff);
    APInt D(128, ProfileSummary::Scale);
    Temp *= N;
    Temp = Temp.sdiv(D);
    uint64_t DesiredCount = Temp.getZExtValue();
    assert(DesiredCount <= TotalCount);
    while (CurrSum < DesiredCount && Iter != End) {
      Count = Iter->first;
      uint32_t Freq = Iter->second;
      CurrSum += (Count * Freq);
      CountsSeen += Freq;
      Iter++;
    }
    assert(CurrSum >= DesiredCount);
    ProfileSummaryEntry PSE = {Cutoff, Count, CountsSeen};
    DetailedSummary.push_back(PSE);
  }
}

uint64_t
ProfileSummaryBuilder::getHotCountThreshold(const SummaryEntryVector &DS) {
  auto &HotEntry =
      ProfileSummaryBuilder::getEntryForPercentile(DS, ProfileSummaryCutoffHot);
  uint64_t HotCountThreshold = HotEntry.MinCount;
  if (ProfileSummaryHotCount.getNumOccurrences() > 0)
    HotCountThreshold = ProfileSummaryHotCount;
  return HotCountThreshold;
}

uint64_t
ProfileSummaryBuilder::getColdCountThreshold(const SummaryEntryVector &DS) {
  auto &ColdEntry = ProfileSummaryBuilder::getEntryForPercentile(
      DS, ProfileSummaryCutoffCold);
  uint64_t ColdCountThreshold = ColdEntry.MinCount;
  if (ProfileSummaryColdCount.getNumOccurrences() > 0)
    ColdCountThreshold = ProfileSummaryColdCount;
  return ColdCountThreshold;
}

std::unique_ptr<ProfileSummary> SampleProfileSummaryBuilder::getSummary() {
  computeDetailedSummary();
  return std::make_unique<ProfileSummary>(
      ProfileSummary::PSK_Sample, DetailedSummary, TotalCount, MaxCount, 0,
      MaxFunctionCount, NumCounts, NumFunctions);
}

std::unique_ptr<ProfileSummary>
SampleProfileSummaryBuilder::computeSummaryForProfiles(
    const SampleProfileMap &Profiles) {
  assert(NumFunctions == 0 &&
         "This can only be called on an empty summary builder");
  sampleprof::SampleProfileMap ContextLessProfiles;
  const sampleprof::SampleProfileMap *ProfilesToUse = &Profiles;
  // For CSSPGO, context-sensitive profile effectively split a function profile
  // into many copies each representing the CFG profile of a particular calling
  // context. That makes the count distribution looks more flat as we now have
  // more function profiles each with lower counts, which in turn leads to lower
  // hot thresholds. To compensate for that, by default we merge context
  // profiles before computing profile summary.
  if (UseContextLessSummary || (sampleprof::FunctionSamples::ProfileIsCS &&
                                !UseContextLessSummary.getNumOccurrences())) {
    ProfileConverter::flattenProfile(Profiles, ContextLessProfiles, true);
    ProfilesToUse = &ContextLessProfiles;
  }

  for (const auto &I : *ProfilesToUse) {
    const sampleprof::FunctionSamples &Profile = I.second;
    addRecord(Profile);
  }

  return getSummary();
}

std::unique_ptr<ProfileSummary> InstrProfSummaryBuilder::getSummary() {
  computeDetailedSummary();
  return std::make_unique<ProfileSummary>(
      ProfileSummary::PSK_Instr, DetailedSummary, TotalCount, MaxCount,
      MaxInternalBlockCount, MaxFunctionCount, NumCounts, NumFunctions);
}

void InstrProfSummaryBuilder::addEntryCount(uint64_t Count) {
  assert(Count <= getInstrMaxCountValue() &&
         "Count value should be less than the max count value.");
  NumFunctions++;
  addCount(Count);
  if (Count > MaxFunctionCount)
    MaxFunctionCount = Count;
}

void InstrProfSummaryBuilder::addInternalCount(uint64_t Count) {
  assert(Count <= getInstrMaxCountValue() &&
         "Count value should be less than the max count value.");
  addCount(Count);
  if (Count > MaxInternalBlockCount)
    MaxInternalBlockCount = Count;
}
