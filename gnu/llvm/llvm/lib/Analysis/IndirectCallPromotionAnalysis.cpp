//===-- IndirectCallPromotionAnalysis.cpp - Find promotion candidates ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helper methods for identifying profitable indirect call promotion
// candidates for an instruction when the indirect-call value profile metadata
// is available.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/IndirectCallPromotionAnalysis.h"
#include "llvm/IR/Instruction.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "pgo-icall-prom-analysis"

// The percent threshold for the direct-call target (this call site vs the
// remaining call count) for it to be considered as the promotion target.
static cl::opt<unsigned> ICPRemainingPercentThreshold(
    "icp-remaining-percent-threshold", cl::init(30), cl::Hidden,
    cl::desc("The percentage threshold against remaining unpromoted indirect "
             "call count for the promotion"));

// The percent threshold for the direct-call target (this call site vs the
// total call count) for it to be considered as the promotion target.
static cl::opt<unsigned>
    ICPTotalPercentThreshold("icp-total-percent-threshold", cl::init(5),
                             cl::Hidden,
                             cl::desc("The percentage threshold against total "
                                      "count for the promotion"));

// Set the maximum number of targets to promote for a single indirect-call
// callsite.
static cl::opt<unsigned>
    MaxNumPromotions("icp-max-prom", cl::init(3), cl::Hidden,
                     cl::desc("Max number of promotions for a single indirect "
                              "call callsite"));

cl::opt<unsigned> MaxNumVTableAnnotations(
    "icp-max-num-vtables", cl::init(6), cl::Hidden,
    cl::desc("Max number of vtables annotated for a vtable load instruction."));

bool ICallPromotionAnalysis::isPromotionProfitable(uint64_t Count,
                                                   uint64_t TotalCount,
                                                   uint64_t RemainingCount) {
  return Count * 100 >= ICPRemainingPercentThreshold * RemainingCount &&
         Count * 100 >= ICPTotalPercentThreshold * TotalCount;
}

// Indirect-call promotion heuristic. The direct targets are sorted based on
// the count. Stop at the first target that is not promoted. Returns the
// number of candidates deemed profitable.
uint32_t ICallPromotionAnalysis::getProfitablePromotionCandidates(
    const Instruction *Inst, uint64_t TotalCount) {
  LLVM_DEBUG(dbgs() << " \nWork on callsite " << *Inst
                    << " Num_targets: " << ValueDataArray.size() << "\n");

  uint32_t I = 0;
  uint64_t RemainingCount = TotalCount;
  for (; I < MaxNumPromotions && I < ValueDataArray.size(); I++) {
    uint64_t Count = ValueDataArray[I].Count;
    assert(Count <= RemainingCount);
    LLVM_DEBUG(dbgs() << " Candidate " << I << " Count=" << Count
                      << "  Target_func: " << ValueDataArray[I].Value << "\n");

    if (!isPromotionProfitable(Count, TotalCount, RemainingCount)) {
      LLVM_DEBUG(dbgs() << " Not promote: Cold target.\n");
      return I;
    }
    RemainingCount -= Count;
  }
  return I;
}

MutableArrayRef<InstrProfValueData>
ICallPromotionAnalysis::getPromotionCandidatesForInstruction(
    const Instruction *I, uint64_t &TotalCount, uint32_t &NumCandidates) {
  ValueDataArray = getValueProfDataFromInst(*I, IPVK_IndirectCallTarget,
                                            MaxNumPromotions, TotalCount);
  if (ValueDataArray.empty()) {
    NumCandidates = 0;
    return MutableArrayRef<InstrProfValueData>();
  }
  NumCandidates = getProfitablePromotionCandidates(I, TotalCount);
  return ValueDataArray;
}
