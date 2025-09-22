//===- llvm/Transforms/Utils/SizeOpts.h - size optimization -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains some shared code size optimization related code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SIZEOPTS_H
#define LLVM_TRANSFORMS_UTILS_SIZEOPTS_H

#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {
extern cl::opt<bool> EnablePGSO;
extern cl::opt<bool> PGSOLargeWorkingSetSizeOnly;
extern cl::opt<bool> PGSOColdCodeOnly;
extern cl::opt<bool> PGSOColdCodeOnlyForInstrPGO;
extern cl::opt<bool> PGSOColdCodeOnlyForSamplePGO;
extern cl::opt<bool> PGSOColdCodeOnlyForPartialSamplePGO;
extern cl::opt<bool> ForcePGSO;
extern cl::opt<int> PgsoCutoffInstrProf;
extern cl::opt<int> PgsoCutoffSampleProf;

class BasicBlock;
class BlockFrequencyInfo;
class Function;

enum class PGSOQueryType {
  IRPass, // A query call from an IR-level transform pass.
  Test,   // A query call from a unit test.
  Other,  // Others.
};

static inline bool isPGSOColdCodeOnly(ProfileSummaryInfo *PSI) {
  return PGSOColdCodeOnly ||
         (PSI->hasInstrumentationProfile() && PGSOColdCodeOnlyForInstrPGO) ||
         (PSI->hasSampleProfile() &&
          ((!PSI->hasPartialSampleProfile() && PGSOColdCodeOnlyForSamplePGO) ||
           (PSI->hasPartialSampleProfile() &&
            PGSOColdCodeOnlyForPartialSamplePGO))) ||
         (PGSOLargeWorkingSetSizeOnly && !PSI->hasLargeWorkingSetSize());
}

template <typename FuncT, typename BFIT>
bool shouldFuncOptimizeForSizeImpl(const FuncT *F, ProfileSummaryInfo *PSI,
                                   BFIT *BFI, PGSOQueryType QueryType) {
  assert(F);
  if (!PSI || !BFI || !PSI->hasProfileSummary())
    return false;
  if (ForcePGSO)
    return true;
  if (!EnablePGSO)
    return false;
  if (isPGSOColdCodeOnly(PSI))
    return PSI->isFunctionColdInCallGraph(F, *BFI);
  if (PSI->hasSampleProfile())
    // The "isCold" check seems to work better for Sample PGO as it could have
    // many profile-unannotated functions.
    return PSI->isFunctionColdInCallGraphNthPercentile(PgsoCutoffSampleProf, F,
                                                       *BFI);
  return !PSI->isFunctionHotInCallGraphNthPercentile(PgsoCutoffInstrProf, F,
                                                     *BFI);
}

template <typename BlockTOrBlockFreq, typename BFIT>
bool shouldOptimizeForSizeImpl(BlockTOrBlockFreq BBOrBlockFreq,
                               ProfileSummaryInfo *PSI, BFIT *BFI,
                               PGSOQueryType QueryType) {
  if (!PSI || !BFI || !PSI->hasProfileSummary())
    return false;
  if (ForcePGSO)
    return true;
  if (!EnablePGSO)
    return false;
  if (isPGSOColdCodeOnly(PSI))
    return PSI->isColdBlock(BBOrBlockFreq, BFI);
  if (PSI->hasSampleProfile())
    // The "isCold" check seems to work better for Sample PGO as it could have
    // many profile-unannotated functions.
    return PSI->isColdBlockNthPercentile(PgsoCutoffSampleProf, BBOrBlockFreq,
                                         BFI);
  return !PSI->isHotBlockNthPercentile(PgsoCutoffInstrProf, BBOrBlockFreq, BFI);
}

/// Returns true if function \p F is suggested to be size-optimized based on the
/// profile.
bool shouldOptimizeForSize(const Function *F, ProfileSummaryInfo *PSI,
                           BlockFrequencyInfo *BFI,
                           PGSOQueryType QueryType = PGSOQueryType::Other);

/// Returns true if basic block \p BB is suggested to be size-optimized based on
/// the profile.
bool shouldOptimizeForSize(const BasicBlock *BB, ProfileSummaryInfo *PSI,
                           BlockFrequencyInfo *BFI,
                           PGSOQueryType QueryType = PGSOQueryType::Other);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SIZEOPTS_H
