//===-- CSPreInliner.h - Profile guided preinliner ---------------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_PROFGEN_PGOINLINEADVISOR_H
#define LLVM_TOOLS_LLVM_PROFGEN_PGOINLINEADVISOR_H

#include "ProfiledBinary.h"
#include "llvm/ADT/PriorityQueue.h"
#include "llvm/ProfileData/ProfileCommon.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Transforms/IPO/ProfiledCallGraph.h"
#include "llvm/Transforms/IPO/SampleContextTracker.h"

using namespace llvm;
using namespace sampleprof;

namespace llvm {
namespace sampleprof {

// Inline candidate seen from profile
struct ProfiledInlineCandidate {
  ProfiledInlineCandidate(const FunctionSamples *Samples, uint64_t Count,
                          uint32_t Size)
      : CalleeSamples(Samples), CallsiteCount(Count), SizeCost(Size) {}
  // Context-sensitive function profile for inline candidate
  const FunctionSamples *CalleeSamples;
  // Call site count for an inline candidate
  // TODO: make sure entry count for context profile and call site
  // target count for corresponding call are consistent.
  uint64_t CallsiteCount;
  // Size proxy for function under particular call context.
  uint64_t SizeCost;
};

// Inline candidate comparer using call site weight
struct ProfiledCandidateComparer {
  bool operator()(const ProfiledInlineCandidate &LHS,
                  const ProfiledInlineCandidate &RHS) {
    // Always prioritize inlining zero-sized functions as they do not affect the
    // size budget. This could happen when all of the callee's code is gone and
    // only pseudo probes are left.
    if ((LHS.SizeCost == 0 || RHS.SizeCost == 0) &&
        (LHS.SizeCost != RHS.SizeCost))
      return RHS.SizeCost == 0;

    if (LHS.CallsiteCount != RHS.CallsiteCount)
      return LHS.CallsiteCount < RHS.CallsiteCount;

    if (LHS.SizeCost != RHS.SizeCost)
      return LHS.SizeCost > RHS.SizeCost;

    // Tie breaker using GUID so we have stable/deterministic inlining order
    assert(LHS.CalleeSamples && RHS.CalleeSamples &&
           "Expect non-null FunctionSamples");
    return LHS.CalleeSamples->getGUID() < RHS.CalleeSamples->getGUID();
  }
};

using ProfiledCandidateQueue =
    PriorityQueue<ProfiledInlineCandidate, std::vector<ProfiledInlineCandidate>,
                  ProfiledCandidateComparer>;

// Pre-compilation inliner based on context-sensitive profile.
// The PreInliner estimates inline decision using hotness from profile
// and cost estimation from machine code size. It helps merges context
// profile globally and achieves better post-inine profile quality, which
// otherwise won't be possible for ThinLTO. It also reduce context profile
// size by only keep context that is estimated to be inlined.
class CSPreInliner {
public:
  CSPreInliner(SampleContextTracker &Tracker, ProfiledBinary &Binary,
               ProfileSummary *Summary);
  void run();

private:
  bool getInlineCandidates(ProfiledCandidateQueue &CQueue,
                           const FunctionSamples *FCallerContextSamples);
  std::vector<FunctionId> buildTopDownOrder();
  void processFunction(FunctionId Name);
  bool shouldInline(ProfiledInlineCandidate &Candidate);
  uint32_t getFuncSize(const ContextTrieNode *ContextNode);
  bool UseContextCost;
  SampleContextTracker &ContextTracker;
  ProfiledBinary &Binary;
  ProfileSummary *Summary;
};

} // end namespace sampleprof
} // end namespace llvm

#endif
