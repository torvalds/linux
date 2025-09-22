//===- IndirectCallPromotionAnalysis.h - Indirect call analysis -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Interface to identify indirect call promotion candidates.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_INDIRECTCALLPROMOTIONANALYSIS_H
#define LLVM_ANALYSIS_INDIRECTCALLPROMOTIONANALYSIS_H

#include "llvm/ProfileData/InstrProf.h"

namespace llvm {

class Instruction;

// Class for identifying profitable indirect call promotion candidates when
// the indirect-call value profile metadata is available.
class ICallPromotionAnalysis {
private:
  // Allocate space to read the profile annotation.
  SmallVector<InstrProfValueData, 4> ValueDataArray;

  // Count is the call count for the direct-call target.
  // TotalCount is the total call count for the indirect-call callsite.
  // RemainingCount is the TotalCount minus promoted-direct-call count.
  // Return true we should promote this indirect-call target.
  bool isPromotionProfitable(uint64_t Count, uint64_t TotalCount,
                             uint64_t RemainingCount);

  // Returns the number of profitable candidates to promote for the
  // current ValueDataArray and the given \p Inst.
  uint32_t getProfitablePromotionCandidates(const Instruction *Inst,
                                            uint64_t TotalCount);

  // Noncopyable
  ICallPromotionAnalysis(const ICallPromotionAnalysis &other) = delete;
  ICallPromotionAnalysis &
  operator=(const ICallPromotionAnalysis &other) = delete;

public:
  ICallPromotionAnalysis() = default;

  /// Returns reference to array of InstrProfValueData for the given
  /// instruction \p I.
  ///
  /// The \p NumVals, \p TotalCount and \p NumCandidates
  /// are set to the number of values in the array, the total profile count
  /// of the indirect call \p I, and the number of profitable candidates
  /// in the given array (which is sorted in reverse order of profitability).
  ///
  /// The returned array space is owned by this class, and overwritten on
  /// subsequent calls.
  MutableArrayRef<InstrProfValueData> getPromotionCandidatesForInstruction(
      const Instruction *I, uint64_t &TotalCount, uint32_t &NumCandidates);
};

} // end namespace llvm

#endif
