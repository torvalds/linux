////===- SampleProfileLoadBaseUtil.h - Profile loader util func --*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the utility functions for the sampled PGO loader base
/// implementation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SAMPLEPROFILELOADERBASEUTIL_H
#define LLVM_TRANSFORMS_UTILS_SAMPLEPROFILELOADERBASEUTIL_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {
using namespace sampleprof;

class ProfileSummaryInfo;
class Module;

extern cl::opt<unsigned> SampleProfileMaxPropagateIterations;
extern cl::opt<unsigned> SampleProfileRecordCoverage;
extern cl::opt<unsigned> SampleProfileSampleCoverage;
extern cl::opt<bool> NoWarnSampleUnused;

namespace sampleprofutil {

class SampleCoverageTracker {
public:
  bool markSamplesUsed(const FunctionSamples *FS, uint32_t LineOffset,
                       uint32_t Discriminator, uint64_t Samples);
  unsigned computeCoverage(unsigned Used, unsigned Total) const;
  unsigned countUsedRecords(const FunctionSamples *FS,
                            ProfileSummaryInfo *PSI) const;
  unsigned countBodyRecords(const FunctionSamples *FS,
                            ProfileSummaryInfo *PSI) const;
  uint64_t getTotalUsedSamples() const { return TotalUsedSamples; }
  uint64_t countBodySamples(const FunctionSamples *FS,
                            ProfileSummaryInfo *PSI) const;

  void clear() {
    SampleCoverage.clear();
    TotalUsedSamples = 0;
  }
  void setProfAccForSymsInList(bool V) { ProfAccForSymsInList = V; }

private:
  using BodySampleCoverageMap = std::map<LineLocation, unsigned>;
  using FunctionSamplesCoverageMap =
      DenseMap<const FunctionSamples *, BodySampleCoverageMap>;

  /// Coverage map for sampling records.
  ///
  /// This map keeps a record of sampling records that have been matched to
  /// an IR instruction. This is used to detect some form of staleness in
  /// profiles (see flag -sample-profile-check-coverage).
  ///
  /// Each entry in the map corresponds to a FunctionSamples instance.  This is
  /// another map that counts how many times the sample record at the
  /// given location has been used.
  FunctionSamplesCoverageMap SampleCoverage;

  /// Number of samples used from the profile.
  ///
  /// When a sampling record is used for the first time, the samples from
  /// that record are added to this accumulator.  Coverage is later computed
  /// based on the total number of samples available in this function and
  /// its callsites.
  ///
  /// Note that this accumulator tracks samples used from a single function
  /// and all the inlined callsites. Strictly, we should have a map of counters
  /// keyed by FunctionSamples pointers, but these stats are cleared after
  /// every function, so we just need to keep a single counter.
  uint64_t TotalUsedSamples = 0;

  // For symbol in profile symbol list, whether to regard their profiles
  // to be accurate. This is passed from the SampleLoader instance.
  bool ProfAccForSymsInList = false;
};

/// Return true if the given callsite is hot wrt to hot cutoff threshold.
bool callsiteIsHot(const FunctionSamples *CallsiteFS, ProfileSummaryInfo *PSI,
                   bool ProfAccForSymsInList);

/// Create a global variable to flag FSDiscriminators are used.
void createFSDiscriminatorVariable(Module *M);

} // end of namespace sampleprofutil
} // end of namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SAMPLEPROFILELOADERBASEUTIL_H
