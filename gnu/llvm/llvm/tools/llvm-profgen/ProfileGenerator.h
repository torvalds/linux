//===-- ProfileGenerator.h - Profile Generator -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_PROGEN_PROFILEGENERATOR_H
#define LLVM_TOOLS_LLVM_PROGEN_PROFILEGENERATOR_H
#include "CSPreInliner.h"
#include "ErrorHandling.h"
#include "PerfReader.h"
#include "ProfiledBinary.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/ProfileData/SampleProfWriter.h"
#include <memory>
#include <unordered_set>

using namespace llvm;
using namespace sampleprof;

namespace llvm {
namespace sampleprof {

using ProbeCounterMap =
    std::unordered_map<const MCDecodedPseudoProbe *, uint64_t>;

// This base class for profile generation of sample-based PGO. We reuse all
// structures relating to function profiles and profile writers as seen in
// /ProfileData/SampleProf.h.
class ProfileGeneratorBase {

public:
  ProfileGeneratorBase(ProfiledBinary *Binary) : Binary(Binary){};
  ProfileGeneratorBase(ProfiledBinary *Binary,
                       const ContextSampleCounterMap *Counters)
      : Binary(Binary), SampleCounters(Counters){};
  ProfileGeneratorBase(ProfiledBinary *Binary,
                       const SampleProfileMap &&Profiles)
      : Binary(Binary), ProfileMap(std::move(Profiles)){};

  virtual ~ProfileGeneratorBase() = default;
  static std::unique_ptr<ProfileGeneratorBase>
  create(ProfiledBinary *Binary, const ContextSampleCounterMap *Counters,
         bool profileIsCS);
  static std::unique_ptr<ProfileGeneratorBase>
  create(ProfiledBinary *Binary, SampleProfileMap &ProfileMap,
         bool profileIsCS);
  virtual void generateProfile() = 0;
  void write();

  static uint32_t
  getDuplicationFactor(unsigned Discriminator,
                       bool UseFSD = ProfileGeneratorBase::UseFSDiscriminator) {
    return UseFSD ? 1
                  : llvm::DILocation::getDuplicationFactorFromDiscriminator(
                        Discriminator);
  }

  static uint32_t
  getBaseDiscriminator(unsigned Discriminator,
                       bool UseFSD = ProfileGeneratorBase::UseFSDiscriminator) {
    return UseFSD ? Discriminator
                  : DILocation::getBaseDiscriminatorFromDiscriminator(
                        Discriminator, /* IsFSDiscriminator */ false);
  }

  static bool UseFSDiscriminator;

protected:
  // Use SampleProfileWriter to serialize profile map
  void write(std::unique_ptr<SampleProfileWriter> Writer,
             SampleProfileMap &ProfileMap);
  /*
  For each region boundary point, mark if it is begin or end (or both) of
  the region. Boundary points are inclusive. Log the sample count as well
  so we can use it when we compute the sample count of each disjoint region
  later. Note that there might be multiple ranges with different sample
  count that share same begin/end point. We need to accumulate the sample
  count for the boundary point for such case, because for the example
  below,

  |<--100-->|
  |<------200------>|
  A         B       C

  sample count for disjoint region [A,B] would be 300.
  */
  void findDisjointRanges(RangeSample &DisjointRanges,
                          const RangeSample &Ranges);

  // Go through each address from range to extract the top frame probe by
  // looking up in the Address2ProbeMap
  void extractProbesFromRange(const RangeSample &RangeCounter,
                              ProbeCounterMap &ProbeCounter,
                              bool FindDisjointRanges = true);

  // Helper function for updating body sample for a leaf location in
  // FunctionProfile
  void updateBodySamplesforFunctionProfile(FunctionSamples &FunctionProfile,
                                           const SampleContextFrame &LeafLoc,
                                           uint64_t Count);

  void updateFunctionSamples();

  void updateTotalSamples();

  void updateCallsiteSamples();

  void filterAmbiguousProfile(SampleProfileMap &Profiles);

  bool filterAmbiguousProfile(FunctionSamples &FS);

  StringRef getCalleeNameForAddress(uint64_t TargetAddress);

  void computeSummaryAndThreshold(SampleProfileMap &ProfileMap);

  void calculateBodySamplesAndSize(const FunctionSamples &FSamples,
                                   uint64_t &TotalBodySamples,
                                   uint64_t &FuncBodySize);

  double calculateDensity(const SampleProfileMap &Profiles);

  void calculateAndShowDensity(const SampleProfileMap &Profiles);

  void showDensitySuggestion(double Density);

  void collectProfiledFunctions();

  bool collectFunctionsFromRawProfile(
      std::unordered_set<const BinaryFunction *> &ProfiledFunctions);

  // Collect profiled Functions for llvm sample profile input.
  virtual bool collectFunctionsFromLLVMProfile(
      std::unordered_set<const BinaryFunction *> &ProfiledFunctions) = 0;

  // List of function prefix to filter out.
  static constexpr const char *FuncPrefixsToFilter[] = {"__cxx_global_var_init",
                                                        "__tls_init"};

  // Thresholds from profile summary to answer isHotCount/isColdCount queries.
  uint64_t HotCountThreshold;

  uint64_t ColdCountThreshold;

  ProfiledBinary *Binary = nullptr;

  std::unique_ptr<ProfileSummary> Summary;

  // Used by SampleProfileWriter
  SampleProfileMap ProfileMap;

  const ContextSampleCounterMap *SampleCounters = nullptr;
};

class ProfileGenerator : public ProfileGeneratorBase {

public:
  ProfileGenerator(ProfiledBinary *Binary,
                   const ContextSampleCounterMap *Counters)
      : ProfileGeneratorBase(Binary, Counters){};
  ProfileGenerator(ProfiledBinary *Binary, const SampleProfileMap &&Profiles)
      : ProfileGeneratorBase(Binary, std::move(Profiles)){};
  void generateProfile() override;

private:
  void generateLineNumBasedProfile();
  void generateProbeBasedProfile();
  RangeSample preprocessRangeCounter(const RangeSample &RangeCounter);
  FunctionSamples &getTopLevelFunctionProfile(FunctionId FuncName);
  // Helper function to get the leaf frame's FunctionProfile by traversing the
  // inline stack and meanwhile it adds the total samples for each frame's
  // function profile.
  FunctionSamples &
  getLeafProfileAndAddTotalSamples(const SampleContextFrameVector &FrameVec,
                                   uint64_t Count);
  void populateBodySamplesForAllFunctions(const RangeSample &RangeCounter);
  void
  populateBoundarySamplesForAllFunctions(const BranchSample &BranchCounters);
  void
  populateBodySamplesWithProbesForAllFunctions(const RangeSample &RangeCounter);
  void populateBoundarySamplesWithProbesForAllFunctions(
      const BranchSample &BranchCounters);
  void postProcessProfiles();
  void trimColdProfiles(const SampleProfileMap &Profiles,
                        uint64_t ColdCntThreshold);
  bool collectFunctionsFromLLVMProfile(
      std::unordered_set<const BinaryFunction *> &ProfiledFunctions) override;
};

class CSProfileGenerator : public ProfileGeneratorBase {
public:
  CSProfileGenerator(ProfiledBinary *Binary,
                     const ContextSampleCounterMap *Counters)
      : ProfileGeneratorBase(Binary, Counters){};
  CSProfileGenerator(ProfiledBinary *Binary, SampleProfileMap &Profiles)
      : ProfileGeneratorBase(Binary), ContextTracker(Profiles, nullptr){};
  void generateProfile() override;

  // Trim the context stack at a given depth.
  template <typename T>
  static void trimContext(SmallVectorImpl<T> &S, int Depth = MaxContextDepth) {
    if (Depth < 0 || static_cast<size_t>(Depth) >= S.size())
      return;
    std::copy(S.begin() + S.size() - static_cast<size_t>(Depth), S.end(),
              S.begin());
    S.resize(Depth);
  }

  // Remove adjacent repeated context sequences up to a given sequence length,
  // -1 means no size limit. Note that repeated sequences are identified based
  // on the exact call site, this is finer granularity than function recursion.
  template <typename T>
  static void compressRecursionContext(SmallVectorImpl<T> &Context,
                                       int32_t CSize = MaxCompressionSize) {
    uint32_t I = 1;
    uint32_t HS = static_cast<uint32_t>(Context.size() / 2);
    uint32_t MaxDedupSize =
        CSize == -1 ? HS : std::min(static_cast<uint32_t>(CSize), HS);
    auto BeginIter = Context.begin();
    // Use an in-place algorithm to save memory copy
    // End indicates the end location of current iteration's data
    uint32_t End = 0;
    // Deduplicate from length 1 to the max possible size of a repeated
    // sequence.
    while (I <= MaxDedupSize) {
      // This is a linear algorithm that deduplicates adjacent repeated
      // sequences of size I. The deduplication detection runs on a sliding
      // window whose size is 2*I and it keeps sliding the window to deduplicate
      // the data inside. Once duplication is detected, deduplicate it by
      // skipping the right half part of the window, otherwise just copy back
      // the new one by appending them at the back of End pointer(for the next
      // iteration).
      //
      // For example:
      // Input: [a1, a2, b1, b2]
      // (Added index to distinguish the same char, the origin is [a, a, b,
      // b], the size of the dedup window is 2(I = 1) at the beginning)
      //
      // 1) The initial status is a dummy window[null, a1], then just copy the
      // right half of the window(End = 0), then slide the window.
      // Result: [a1], a2, b1, b2 (End points to the element right before ],
      // after ] is the data of the previous iteration)
      //
      // 2) Next window is [a1, a2]. Since a1 == a2, then skip the right half of
      // the window i.e the duplication happen. Only slide the window.
      // Result: [a1], a2, b1, b2
      //
      // 3) Next window is [a2, b1], copy the right half of the window(b1 is
      // new) to the End and slide the window.
      // Result: [a1, b1], b1, b2
      //
      // 4) Next window is [b1, b2], same to 2), skip b2.
      // Result: [a1, b1], b1, b2
      // After resize, it will be [a, b]

      // Use pointers like below to do comparison inside the window
      //    [a         b         c        a       b        c]
      //     |         |         |                |        |
      // LeftBoundary Left     Right           Left+I    Right+I
      // A duplication found if Left < LeftBoundry.

      int32_t Right = I - 1;
      End = I;
      int32_t LeftBoundary = 0;
      while (Right + I < Context.size()) {
        // To avoids scanning a part of a sequence repeatedly, it finds out
        // the common suffix of two hald in the window. The common suffix will
        // serve as the common prefix of next possible pair of duplicate
        // sequences. The non-common part will be ignored and never scanned
        // again.

        // For example.
        // Input: [a, b1], c1, b2, c2
        // I = 2
        //
        // 1) For the window [a, b1, c1, b2], non-common-suffix for the right
        // part is 'c1', copy it and only slide the window 1 step.
        // Result: [a, b1, c1], b2, c2
        //
        // 2) Next window is [b1, c1, b2, c2], so duplication happen.
        // Result after resize: [a, b, c]

        int32_t Left = Right;
        while (Left >= LeftBoundary && Context[Left] == Context[Left + I]) {
          // Find the longest suffix inside the window. When stops, Left points
          // at the diverging point in the current sequence.
          Left--;
        }

        bool DuplicationFound = (Left < LeftBoundary);
        // Don't need to recheck the data before Right
        LeftBoundary = Right + 1;
        if (DuplicationFound) {
          // Duplication found, skip right half of the window.
          Right += I;
        } else {
          // Copy the non-common-suffix part of the adjacent sequence.
          std::copy(BeginIter + Right + 1, BeginIter + Left + I + 1,
                    BeginIter + End);
          End += Left + I - Right;
          // Only slide the window by the size of non-common-suffix
          Right = Left + I;
        }
      }
      // Don't forget the remaining part that's not scanned.
      std::copy(BeginIter + Right + 1, Context.end(), BeginIter + End);
      End += Context.size() - Right - 1;
      I++;
      Context.resize(End);
      MaxDedupSize = std::min(static_cast<uint32_t>(End / 2), MaxDedupSize);
    }
  }

private:
  void generateLineNumBasedProfile();

  FunctionSamples *getOrCreateFunctionSamples(ContextTrieNode *ContextNode,
                                              bool WasLeafInlined = false);

  // Lookup or create ContextTrieNode for the context, FunctionSamples is
  // created inside this function.
  ContextTrieNode *getOrCreateContextNode(const SampleContextFrames Context,
                                          bool WasLeafInlined = false);

  // For profiled only functions, on-demand compute their inline context
  // function byte size which is used by the pre-inliner.
  void computeSizeForProfiledFunctions();
  // Post processing for profiles before writing out, such as mermining
  // and trimming cold profiles, running preinliner on profiles.
  void postProcessProfiles();

  void populateBodySamplesForFunction(FunctionSamples &FunctionProfile,
                                      const RangeSample &RangeCounters);

  void populateBoundarySamplesForFunction(ContextTrieNode *CallerNode,
                                          const BranchSample &BranchCounters);

  void populateInferredFunctionSamples(ContextTrieNode &Node);

  void updateFunctionSamples();

  void generateProbeBasedProfile();

  // Fill in function body samples from probes
  void populateBodySamplesWithProbes(const RangeSample &RangeCounter,
                                     const AddrBasedCtxKey *CtxKey);
  // Fill in boundary samples for a call probe
  void populateBoundarySamplesWithProbes(const BranchSample &BranchCounter,
                                         const AddrBasedCtxKey *CtxKey);

  ContextTrieNode *
  getContextNodeForLeafProbe(const AddrBasedCtxKey *CtxKey,
                             const MCDecodedPseudoProbe *LeafProbe);

  // Helper function to get FunctionSamples for the leaf probe
  FunctionSamples &
  getFunctionProfileForLeafProbe(const AddrBasedCtxKey *CtxKey,
                                 const MCDecodedPseudoProbe *LeafProbe);

  void convertToProfileMap(ContextTrieNode &Node,
                           SampleContextFrameVector &Context);

  void convertToProfileMap();

  void computeSummaryAndThreshold();

  bool collectFunctionsFromLLVMProfile(
      std::unordered_set<const BinaryFunction *> &ProfiledFunctions) override;

  void initializeMissingFrameInferrer();

  // Given an input `Context`, output `NewContext` with inferred missing tail
  // call frames.
  void inferMissingFrames(const SmallVectorImpl<uint64_t> &Context,
                          SmallVectorImpl<uint64_t> &NewContext);

  ContextTrieNode &getRootContext() { return ContextTracker.getRootContext(); };

  // The container for holding the FunctionSamples used by context trie.
  std::list<FunctionSamples> FSamplesList;

  // Underlying context table serves for sample profile writer.
  std::unordered_set<SampleContextFrameVector, SampleContextFrameHash> Contexts;

  SampleContextTracker ContextTracker;

  bool IsProfileValidOnTrie = true;

public:
  // Deduplicate adjacent repeated context sequences up to a given sequence
  // length. -1 means no size limit.
  static int32_t MaxCompressionSize;
  static int MaxContextDepth;
};

} // end namespace sampleprof
} // end namespace llvm

#endif
