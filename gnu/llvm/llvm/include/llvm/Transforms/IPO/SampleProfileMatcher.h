//===- Transforms/IPO/SampleProfileMatcher.h ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for SampleProfileMatcher.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_SAMPLEPROFILEMATCHER_H
#define LLVM_TRANSFORMS_IPO_SAMPLEPROFILEMATCHER_H

#include "llvm/ADT/StringSet.h"
#include "llvm/Transforms/Utils/SampleProfileLoaderBaseImpl.h"

namespace llvm {

using AnchorList = std::vector<std::pair<LineLocation, FunctionId>>;
using AnchorMap = std::map<LineLocation, FunctionId>;

// Sample profile matching - fuzzy match.
class SampleProfileMatcher {
  Module &M;
  SampleProfileReader &Reader;
  LazyCallGraph &CG;
  const PseudoProbeManager *ProbeManager;
  const ThinOrFullLTOPhase LTOPhase;
  SampleProfileMap FlattenedProfiles;
  // For each function, the matcher generates a map, of which each entry is a
  // mapping from the source location of current build to the source location
  // in the profile.
  StringMap<LocToLocMap> FuncMappings;

  // Match state for an anchor/callsite.
  enum class MatchState {
    Unknown = 0,
    // Initial match between input profile and current IR.
    InitialMatch = 1,
    // Initial mismatch between input profile and current IR.
    InitialMismatch = 2,
    // InitialMatch stays matched after fuzzy profile matching.
    UnchangedMatch = 3,
    // InitialMismatch stays mismatched after fuzzy profile matching.
    UnchangedMismatch = 4,
    // InitialMismatch is recovered after fuzzy profile matching.
    RecoveredMismatch = 5,
    // InitialMatch is removed and becomes mismatched after fuzzy profile
    // matching.
    RemovedMatch = 6,
  };

  // For each function, store every callsite and its matching state into this
  // map, of which each entry is a pair of callsite location and MatchState.
  // This is used for profile staleness computation and report.
  StringMap<std::unordered_map<LineLocation, MatchState, LineLocationHash>>
      FuncCallsiteMatchStates;

  struct FuncToProfileNameMapHash {
    uint64_t
    operator()(const std::pair<const Function *, FunctionId> &P) const {
      return hash_combine(P.first, P.second);
    }
  };
  // A map from a pair of function and profile name to a boolean value
  // indicating whether they are matched. This is used as a cache for the
  // matching result.
  std::unordered_map<std::pair<const Function *, FunctionId>, bool,
                     FuncToProfileNameMapHash>
      FuncProfileMatchCache;
  // The new functions found by the call graph matching. The map's key is the
  // the new(renamed) function pointer and the value is old(unused) profile
  // name.
  std::unordered_map<Function *, FunctionId> FuncToProfileNameMap;

  // A map pointer to the FuncNameToProfNameMap in SampleProfileLoader,
  // which maps the function name to the matched profile name. This is used
  // for sample loader to look up profile using the new name.
  HashKeyMap<std::unordered_map, FunctionId, FunctionId> *FuncNameToProfNameMap;

  // A map pointer to the SymbolMap in SampleProfileLoader, which stores all
  // the original matched symbols before the matching. this is to determine if
  // the profile is unused(to be matched) or not.
  HashKeyMap<std::unordered_map, FunctionId, Function *> *SymbolMap;

  // The new functions from IR.
  HashKeyMap<std::unordered_map, FunctionId, Function *>
      FunctionsWithoutProfile;

  // Pointer to the Profile Symbol List in the reader.
  std::shared_ptr<ProfileSymbolList> PSL;

  // Profile mismatch statstics:
  uint64_t TotalProfiledFunc = 0;
  // Num of checksum-mismatched function.
  uint64_t NumStaleProfileFunc = 0;
  uint64_t TotalProfiledCallsites = 0;
  uint64_t NumMismatchedCallsites = 0;
  uint64_t NumRecoveredCallsites = 0;
  // Total samples for all profiled functions.
  uint64_t TotalFunctionSamples = 0;
  // Total samples for all checksum-mismatched functions.
  uint64_t MismatchedFunctionSamples = 0;
  uint64_t MismatchedCallsiteSamples = 0;
  uint64_t RecoveredCallsiteSamples = 0;

  // Profile call-graph matching statstics:
  uint64_t NumCallGraphRecoveredProfiledFunc = 0;
  uint64_t NumCallGraphRecoveredFuncSamples = 0;

  // A dummy name for unknown indirect callee, used to differentiate from a
  // non-call instruction that also has an empty callee name.
  static constexpr const char *UnknownIndirectCallee =
      "unknown.indirect.callee";

public:
  SampleProfileMatcher(
      Module &M, SampleProfileReader &Reader, LazyCallGraph &CG,
      const PseudoProbeManager *ProbeManager, ThinOrFullLTOPhase LTOPhase,
      HashKeyMap<std::unordered_map, FunctionId, Function *> &SymMap,
      std::shared_ptr<ProfileSymbolList> PSL,
      HashKeyMap<std::unordered_map, FunctionId, FunctionId>
          &FuncNameToProfNameMap)
      : M(M), Reader(Reader), CG(CG), ProbeManager(ProbeManager),
        LTOPhase(LTOPhase), FuncNameToProfNameMap(&FuncNameToProfNameMap),
        SymbolMap(&SymMap), PSL(PSL) {};
  void runOnModule();
  void clearMatchingData() {
    // Do not clear FuncMappings, it stores IRLoc to ProfLoc remappings which
    // will be used for sample loader.
    // Do not clear FlattenedProfiles as it contains function names referenced
    // by FuncNameToProfNameMap. Clearing this memory could lead to a
    // use-after-free error.
    freeContainer(FuncCallsiteMatchStates);
    freeContainer(FunctionsWithoutProfile);
    freeContainer(FuncToProfileNameMap);
  }

private:
  FunctionSamples *getFlattenedSamplesFor(const FunctionId &Fname) {
    auto It = FlattenedProfiles.find(Fname);
    if (It != FlattenedProfiles.end())
      return &It->second;
    return nullptr;
  }
  FunctionSamples *getFlattenedSamplesFor(const Function &F) {
    StringRef CanonFName = FunctionSamples::getCanonicalFnName(F);
    return getFlattenedSamplesFor(FunctionId(CanonFName));
  }
  template <typename T> inline void freeContainer(T &C) {
    T Empty;
    std::swap(C, Empty);
  }
  void getFilteredAnchorList(const AnchorMap &IRAnchors,
                             const AnchorMap &ProfileAnchors,
                             AnchorList &FilteredIRAnchorsList,
                             AnchorList &FilteredProfileAnchorList);
  void runOnFunction(Function &F);
  void findIRAnchors(const Function &F, AnchorMap &IRAnchors) const;
  void findProfileAnchors(const FunctionSamples &FS,
                          AnchorMap &ProfileAnchors) const;
  // Record the callsite match states for profile staleness report, the result
  // is saved in FuncCallsiteMatchStates.
  void recordCallsiteMatchStates(const Function &F, const AnchorMap &IRAnchors,
                                 const AnchorMap &ProfileAnchors,
                                 const LocToLocMap *IRToProfileLocationMap);

  bool isMismatchState(const enum MatchState &State) {
    return State == MatchState::InitialMismatch ||
           State == MatchState::UnchangedMismatch ||
           State == MatchState::RemovedMatch;
  };

  bool isInitialState(const enum MatchState &State) {
    return State == MatchState::InitialMatch ||
           State == MatchState::InitialMismatch;
  };

  bool isFinalState(const enum MatchState &State) {
    return State == MatchState::UnchangedMatch ||
           State == MatchState::UnchangedMismatch ||
           State == MatchState::RecoveredMismatch ||
           State == MatchState::RemovedMatch;
  };

  void countCallGraphRecoveredSamples(
      const FunctionSamples &FS,
      std::unordered_set<FunctionId> &MatchedUnusedProfile);
  // Count the samples of checksum mismatched function for the top-level
  // function and all inlinees.
  void countMismatchedFuncSamples(const FunctionSamples &FS, bool IsTopLevel);
  // Count the number of mismatched or recovered callsites.
  void countMismatchCallsites(const FunctionSamples &FS);
  // Count the samples of mismatched or recovered callsites for top-level
  // function and all inlinees.
  void countMismatchedCallsiteSamples(const FunctionSamples &FS);
  void computeAndReportProfileStaleness();

  LocToLocMap &getIRToProfileLocationMap(const Function &F) {
    auto Ret = FuncMappings.try_emplace(
        FunctionSamples::getCanonicalFnName(F.getName()), LocToLocMap());
    return Ret.first->second;
  }
  void distributeIRToProfileLocationMap();
  void distributeIRToProfileLocationMap(FunctionSamples &FS);
  // This function implements the Myers diff algorithm used for stale profile
  // matching. The algorithm provides a simple and efficient way to find the
  // Longest Common Subsequence(LCS) or the Shortest Edit Script(SES) of two
  // sequences. For more details, refer to the paper 'An O(ND) Difference
  // Algorithm and Its Variations' by Eugene W. Myers.
  // In the scenario of profile fuzzy matching, the two sequences are the IR
  // callsite anchors and profile callsite anchors. The subsequence equivalent
  // parts from the resulting SES are used to remap the IR locations to the
  // profile locations. As the number of function callsite is usually not big,
  // we currently just implements the basic greedy version(page 6 of the paper).
  LocToLocMap longestCommonSequence(const AnchorList &IRCallsiteAnchors,
                                    const AnchorList &ProfileCallsiteAnchors,
                                    bool MatchUnusedFunction);
  void matchNonCallsiteLocs(const LocToLocMap &AnchorMatchings,
                            const AnchorMap &IRAnchors,
                            LocToLocMap &IRToProfileLocationMap);
  void runStaleProfileMatching(const Function &F, const AnchorMap &IRAnchors,
                               const AnchorMap &ProfileAnchors,
                               LocToLocMap &IRToProfileLocationMap,
                               bool RunCFGMatching, bool RunCGMatching);
  // If the function doesn't have profile, return the pointer to the function.
  bool functionHasProfile(const FunctionId &IRFuncName,
                          Function *&FuncWithoutProfile);
  bool isProfileUnused(const FunctionId &ProfileFuncName);
  bool functionMatchesProfileHelper(const Function &IRFunc,
                                    const FunctionId &ProfFunc);
  // Determine if the function matches profile. If FindMatchedProfileOnly is
  // set, only search the existing matched function. Otherwise, try matching the
  // two functions.
  bool functionMatchesProfile(const FunctionId &IRFuncName,
                              const FunctionId &ProfileFuncName,
                              bool FindMatchedProfileOnly);
  // Determine if the function matches profile by computing a similarity ratio
  // between two sequences of callsite anchors extracted from function and
  // profile. If it's above the threshold, the function matches the profile.
  bool functionMatchesProfile(Function &IRFunc, const FunctionId &ProfFunc,
                              bool FindMatchedProfileOnly);
  // Find functions that don't show in the profile or profile symbol list,
  // which are supposed to be new functions. We use them as the targets for
  // call graph matching.
  void findFunctionsWithoutProfile();
  void reportOrPersistProfileStats();
};
} // end namespace llvm
#endif // LLVM_TRANSFORMS_IPO_SAMPLEPROFILEMATCHER_H
