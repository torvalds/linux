//===- SampleProfileLoaderBaseUtil.cpp - Profile loader Util func ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SampleProfileLoader base utility functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/SampleProfileLoaderBaseUtil.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

namespace llvm {

cl::opt<unsigned> SampleProfileMaxPropagateIterations(
    "sample-profile-max-propagate-iterations", cl::init(100),
    cl::desc("Maximum number of iterations to go through when propagating "
             "sample block/edge weights through the CFG."));

cl::opt<unsigned> SampleProfileRecordCoverage(
    "sample-profile-check-record-coverage", cl::init(0), cl::value_desc("N"),
    cl::desc("Emit a warning if less than N% of records in the input profile "
             "are matched to the IR."));

cl::opt<unsigned> SampleProfileSampleCoverage(
    "sample-profile-check-sample-coverage", cl::init(0), cl::value_desc("N"),
    cl::desc("Emit a warning if less than N% of samples in the input profile "
             "are matched to the IR."));

cl::opt<bool> NoWarnSampleUnused(
    "no-warn-sample-unused", cl::init(false), cl::Hidden,
    cl::desc("Use this option to turn off/on warnings about function with "
             "samples but without debug information to use those samples. "));

cl::opt<bool> SampleProfileUseProfi(
    "sample-profile-use-profi", cl::Hidden,
    cl::desc("Use profi to infer block and edge counts."));

namespace sampleprofutil {

/// Return true if the given callsite is hot wrt to hot cutoff threshold.
///
/// Functions that were inlined in the original binary will be represented
/// in the inline stack in the sample profile. If the profile shows that
/// the original inline decision was "good" (i.e., the callsite is executed
/// frequently), then we will recreate the inline decision and apply the
/// profile from the inlined callsite.
///
/// To decide whether an inlined callsite is hot, we compare the callsite
/// sample count with the hot cutoff computed by ProfileSummaryInfo, it is
/// regarded as hot if the count is above the cutoff value.
///
/// When ProfileAccurateForSymsInList is enabled and profile symbol list
/// is present, functions in the profile symbol list but without profile will
/// be regarded as cold and much less inlining will happen in CGSCC inlining
/// pass, so we tend to lower the hot criteria here to allow more early
/// inlining to happen for warm callsites and it is helpful for performance.
bool callsiteIsHot(const FunctionSamples *CallsiteFS, ProfileSummaryInfo *PSI,
                   bool ProfAccForSymsInList) {
  if (!CallsiteFS)
    return false; // The callsite was not inlined in the original binary.

  assert(PSI && "PSI is expected to be non null");
  uint64_t CallsiteTotalSamples = CallsiteFS->getTotalSamples();
  if (ProfAccForSymsInList)
    return !PSI->isColdCount(CallsiteTotalSamples);
  else
    return PSI->isHotCount(CallsiteTotalSamples);
}

/// Mark as used the sample record for the given function samples at
/// (LineOffset, Discriminator).
///
/// \returns true if this is the first time we mark the given record.
bool SampleCoverageTracker::markSamplesUsed(const FunctionSamples *FS,
                                            uint32_t LineOffset,
                                            uint32_t Discriminator,
                                            uint64_t Samples) {
  LineLocation Loc(LineOffset, Discriminator);
  unsigned &Count = SampleCoverage[FS][Loc];
  bool FirstTime = (++Count == 1);
  if (FirstTime)
    TotalUsedSamples += Samples;
  return FirstTime;
}

/// Return the number of sample records that were applied from this profile.
///
/// This count does not include records from cold inlined callsites.
unsigned
SampleCoverageTracker::countUsedRecords(const FunctionSamples *FS,
                                        ProfileSummaryInfo *PSI) const {
  auto I = SampleCoverage.find(FS);

  // The size of the coverage map for FS represents the number of records
  // that were marked used at least once.
  unsigned Count = (I != SampleCoverage.end()) ? I->second.size() : 0;

  // If there are inlined callsites in this function, count the samples found
  // in the respective bodies. However, do not bother counting callees with 0
  // total samples, these are callees that were never invoked at runtime.
  for (const auto &I : FS->getCallsiteSamples())
    for (const auto &J : I.second) {
      const FunctionSamples *CalleeSamples = &J.second;
      if (callsiteIsHot(CalleeSamples, PSI, ProfAccForSymsInList))
        Count += countUsedRecords(CalleeSamples, PSI);
    }

  return Count;
}

/// Return the number of sample records in the body of this profile.
///
/// This count does not include records from cold inlined callsites.
unsigned
SampleCoverageTracker::countBodyRecords(const FunctionSamples *FS,
                                        ProfileSummaryInfo *PSI) const {
  unsigned Count = FS->getBodySamples().size();

  // Only count records in hot callsites.
  for (const auto &I : FS->getCallsiteSamples())
    for (const auto &J : I.second) {
      const FunctionSamples *CalleeSamples = &J.second;
      if (callsiteIsHot(CalleeSamples, PSI, ProfAccForSymsInList))
        Count += countBodyRecords(CalleeSamples, PSI);
    }

  return Count;
}

/// Return the number of samples collected in the body of this profile.
///
/// This count does not include samples from cold inlined callsites.
uint64_t
SampleCoverageTracker::countBodySamples(const FunctionSamples *FS,
                                        ProfileSummaryInfo *PSI) const {
  uint64_t Total = 0;
  for (const auto &I : FS->getBodySamples())
    Total += I.second.getSamples();

  // Only count samples in hot callsites.
  for (const auto &I : FS->getCallsiteSamples())
    for (const auto &J : I.second) {
      const FunctionSamples *CalleeSamples = &J.second;
      if (callsiteIsHot(CalleeSamples, PSI, ProfAccForSymsInList))
        Total += countBodySamples(CalleeSamples, PSI);
    }

  return Total;
}

/// Return the fraction of sample records used in this profile.
///
/// The returned value is an unsigned integer in the range 0-100 indicating
/// the percentage of sample records that were used while applying this
/// profile to the associated function.
unsigned SampleCoverageTracker::computeCoverage(unsigned Used,
                                                unsigned Total) const {
  assert(Used <= Total &&
         "number of used records cannot exceed the total number of records");
  return Total > 0 ? Used * 100 / Total : 100;
}

/// Create a global variable to flag FSDiscriminators are used.
void createFSDiscriminatorVariable(Module *M) {
  const char *FSDiscriminatorVar = "__llvm_fs_discriminator__";
  if (M->getGlobalVariable(FSDiscriminatorVar))
    return;

  auto &Context = M->getContext();
  // Place this variable to llvm.used so it won't be GC'ed.
  appendToUsed(*M, {new GlobalVariable(*M, Type::getInt1Ty(Context), true,
                                       GlobalValue::WeakODRLinkage,
                                       ConstantInt::getTrue(Context),
                                       FSDiscriminatorVar)});
}

} // end of namespace sampleprofutil
} // end of namespace llvm
