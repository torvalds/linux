//===- SampleProfileMatcher.cpp - Sampling-based Stale Profile Matcher ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SampleProfileMatcher used for stale
// profile matching.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/SampleProfileMatcher.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace sampleprof;

#define DEBUG_TYPE "sample-profile-matcher"

static cl::opt<unsigned> FuncProfileSimilarityThreshold(
    "func-profile-similarity-threshold", cl::Hidden, cl::init(80),
    cl::desc("Consider a profile matches a function if the similarity of their "
             "callee sequences is above the specified percentile."));

static cl::opt<unsigned> MinFuncCountForCGMatching(
    "min-func-count-for-cg-matching", cl::Hidden, cl::init(5),
    cl::desc("The minimum number of basic blocks required for a function to "
             "run stale profile call graph matching."));

static cl::opt<unsigned> MinCallCountForCGMatching(
    "min-call-count-for-cg-matching", cl::Hidden, cl::init(3),
    cl::desc("The minimum number of call anchors required for a function to "
             "run stale profile call graph matching."));

extern cl::opt<bool> SalvageStaleProfile;
extern cl::opt<bool> SalvageUnusedProfile;
extern cl::opt<bool> PersistProfileStaleness;
extern cl::opt<bool> ReportProfileStaleness;

static cl::opt<unsigned> SalvageStaleProfileMaxCallsites(
    "salvage-stale-profile-max-callsites", cl::Hidden, cl::init(UINT_MAX),
    cl::desc("The maximum number of callsites in a function, above which stale "
             "profile matching will be skipped."));

void SampleProfileMatcher::findIRAnchors(const Function &F,
                                         AnchorMap &IRAnchors) const {
  // For inlined code, recover the original callsite and callee by finding the
  // top-level inline frame. e.g. For frame stack "main:1 @ foo:2 @ bar:3", the
  // top-level frame is "main:1", the callsite is "1" and the callee is "foo".
  auto FindTopLevelInlinedCallsite = [](const DILocation *DIL) {
    assert((DIL && DIL->getInlinedAt()) && "No inlined callsite");
    const DILocation *PrevDIL = nullptr;
    do {
      PrevDIL = DIL;
      DIL = DIL->getInlinedAt();
    } while (DIL->getInlinedAt());

    LineLocation Callsite = FunctionSamples::getCallSiteIdentifier(
        DIL, FunctionSamples::ProfileIsFS);
    StringRef CalleeName = PrevDIL->getSubprogramLinkageName();
    return std::make_pair(Callsite, FunctionId(CalleeName));
  };

  auto GetCanonicalCalleeName = [](const CallBase *CB) {
    StringRef CalleeName = UnknownIndirectCallee;
    if (Function *Callee = CB->getCalledFunction())
      CalleeName = FunctionSamples::getCanonicalFnName(Callee->getName());
    return CalleeName;
  };

  // Extract profile matching anchors in the IR.
  for (auto &BB : F) {
    for (auto &I : BB) {
      DILocation *DIL = I.getDebugLoc();
      if (!DIL)
        continue;

      if (FunctionSamples::ProfileIsProbeBased) {
        if (auto Probe = extractProbe(I)) {
          // Flatten inlined IR for the matching.
          if (DIL->getInlinedAt()) {
            IRAnchors.emplace(FindTopLevelInlinedCallsite(DIL));
          } else {
            // Use empty StringRef for basic block probe.
            StringRef CalleeName;
            if (const auto *CB = dyn_cast<CallBase>(&I)) {
              // Skip the probe inst whose callee name is "llvm.pseudoprobe".
              if (!isa<IntrinsicInst>(&I))
                CalleeName = GetCanonicalCalleeName(CB);
            }
            LineLocation Loc = LineLocation(Probe->Id, 0);
            IRAnchors.emplace(Loc, FunctionId(CalleeName));
          }
        }
      } else {
        // TODO: For line-number based profile(AutoFDO), currently only support
        // find callsite anchors. In future, we need to parse all the non-call
        // instructions to extract the line locations for profile matching.
        if (!isa<CallBase>(&I) || isa<IntrinsicInst>(&I))
          continue;

        if (DIL->getInlinedAt()) {
          IRAnchors.emplace(FindTopLevelInlinedCallsite(DIL));
        } else {
          LineLocation Callsite = FunctionSamples::getCallSiteIdentifier(
              DIL, FunctionSamples::ProfileIsFS);
          StringRef CalleeName = GetCanonicalCalleeName(dyn_cast<CallBase>(&I));
          IRAnchors.emplace(Callsite, FunctionId(CalleeName));
        }
      }
    }
  }
}

void SampleProfileMatcher::findProfileAnchors(const FunctionSamples &FS,
                                              AnchorMap &ProfileAnchors) const {
  auto isInvalidLineOffset = [](uint32_t LineOffset) {
    return LineOffset & 0x8000;
  };

  auto InsertAnchor = [](const LineLocation &Loc, const FunctionId &CalleeName,
                         AnchorMap &ProfileAnchors) {
    auto Ret = ProfileAnchors.try_emplace(Loc, CalleeName);
    if (!Ret.second) {
      // For multiple callees, which indicates it's an indirect call, we use a
      // dummy name(UnknownIndirectCallee) as the indrect callee name.
      Ret.first->second = FunctionId(UnknownIndirectCallee);
    }
  };

  for (const auto &I : FS.getBodySamples()) {
    const LineLocation &Loc = I.first;
    if (isInvalidLineOffset(Loc.LineOffset))
      continue;
    for (const auto &C : I.second.getCallTargets())
      InsertAnchor(Loc, C.first, ProfileAnchors);
  }

  for (const auto &I : FS.getCallsiteSamples()) {
    const LineLocation &Loc = I.first;
    if (isInvalidLineOffset(Loc.LineOffset))
      continue;
    for (const auto &C : I.second)
      InsertAnchor(Loc, C.first, ProfileAnchors);
  }
}

bool SampleProfileMatcher::functionHasProfile(const FunctionId &IRFuncName,
                                              Function *&FuncWithoutProfile) {
  FuncWithoutProfile = nullptr;
  auto R = FunctionsWithoutProfile.find(IRFuncName);
  if (R != FunctionsWithoutProfile.end())
    FuncWithoutProfile = R->second;
  return !FuncWithoutProfile;
}

bool SampleProfileMatcher::isProfileUnused(const FunctionId &ProfileFuncName) {
  return SymbolMap->find(ProfileFuncName) == SymbolMap->end();
}

bool SampleProfileMatcher::functionMatchesProfile(
    const FunctionId &IRFuncName, const FunctionId &ProfileFuncName,
    bool FindMatchedProfileOnly) {
  if (IRFuncName == ProfileFuncName)
    return true;
  if (!SalvageUnusedProfile)
    return false;

  // If IR function doesn't have profile and the profile is unused, try
  // matching them.
  Function *IRFunc = nullptr;
  if (functionHasProfile(IRFuncName, IRFunc) ||
      !isProfileUnused(ProfileFuncName))
    return false;

  assert(FunctionId(IRFunc->getName()) != ProfileFuncName &&
         "IR function should be different from profile function to match");
  return functionMatchesProfile(*IRFunc, ProfileFuncName,
                                FindMatchedProfileOnly);
}

LocToLocMap
SampleProfileMatcher::longestCommonSequence(const AnchorList &AnchorList1,
                                            const AnchorList &AnchorList2,
                                            bool MatchUnusedFunction) {
  int32_t Size1 = AnchorList1.size(), Size2 = AnchorList2.size(),
          MaxDepth = Size1 + Size2;
  auto Index = [&](int32_t I) { return I + MaxDepth; };

  LocToLocMap EqualLocations;
  if (MaxDepth == 0)
    return EqualLocations;

  // Backtrack the SES result.
  auto Backtrack = [&](const std::vector<std::vector<int32_t>> &Trace,
                       const AnchorList &AnchorList1,
                       const AnchorList &AnchorList2,
                       LocToLocMap &EqualLocations) {
    int32_t X = Size1, Y = Size2;
    for (int32_t Depth = Trace.size() - 1; X > 0 || Y > 0; Depth--) {
      const auto &P = Trace[Depth];
      int32_t K = X - Y;
      int32_t PrevK = K;
      if (K == -Depth || (K != Depth && P[Index(K - 1)] < P[Index(K + 1)]))
        PrevK = K + 1;
      else
        PrevK = K - 1;

      int32_t PrevX = P[Index(PrevK)];
      int32_t PrevY = PrevX - PrevK;
      while (X > PrevX && Y > PrevY) {
        X--;
        Y--;
        EqualLocations.insert({AnchorList1[X].first, AnchorList2[Y].first});
      }

      if (Depth == 0)
        break;

      if (Y == PrevY)
        X--;
      else if (X == PrevX)
        Y--;
      X = PrevX;
      Y = PrevY;
    }
  };

  // The greedy LCS/SES algorithm.

  // An array contains the endpoints of the furthest reaching D-paths.
  std::vector<int32_t> V(2 * MaxDepth + 1, -1);
  V[Index(1)] = 0;
  // Trace is used to backtrack the SES result.
  std::vector<std::vector<int32_t>> Trace;
  for (int32_t Depth = 0; Depth <= MaxDepth; Depth++) {
    Trace.push_back(V);
    for (int32_t K = -Depth; K <= Depth; K += 2) {
      int32_t X = 0, Y = 0;
      if (K == -Depth || (K != Depth && V[Index(K - 1)] < V[Index(K + 1)]))
        X = V[Index(K + 1)];
      else
        X = V[Index(K - 1)] + 1;
      Y = X - K;
      while (X < Size1 && Y < Size2 &&
             functionMatchesProfile(
                 AnchorList1[X].second, AnchorList2[Y].second,
                 !MatchUnusedFunction /* Find matched function only */))
        X++, Y++;

      V[Index(K)] = X;

      if (X >= Size1 && Y >= Size2) {
        // Length of an SES is D.
        Backtrack(Trace, AnchorList1, AnchorList2, EqualLocations);
        return EqualLocations;
      }
    }
  }
  // Length of an SES is greater than MaxDepth.
  return EqualLocations;
}

void SampleProfileMatcher::matchNonCallsiteLocs(
    const LocToLocMap &MatchedAnchors, const AnchorMap &IRAnchors,
    LocToLocMap &IRToProfileLocationMap) {
  auto InsertMatching = [&](const LineLocation &From, const LineLocation &To) {
    // Skip the unchanged location mapping to save memory.
    if (From != To)
      IRToProfileLocationMap.insert({From, To});
  };

  // Use function's beginning location as the initial anchor.
  int32_t LocationDelta = 0;
  SmallVector<LineLocation> LastMatchedNonAnchors;
  for (const auto &IR : IRAnchors) {
    const auto &Loc = IR.first;
    bool IsMatchedAnchor = false;
    // Match the anchor location in lexical order.
    auto R = MatchedAnchors.find(Loc);
    if (R != MatchedAnchors.end()) {
      const auto &Candidate = R->second;
      InsertMatching(Loc, Candidate);
      LLVM_DEBUG(dbgs() << "Callsite with callee:" << IR.second.stringRef()
                        << " is matched from " << Loc << " to " << Candidate
                        << "\n");
      LocationDelta = Candidate.LineOffset - Loc.LineOffset;

      // Match backwards for non-anchor locations.
      // The locations in LastMatchedNonAnchors have been matched forwards
      // based on the previous anchor, spilt it evenly and overwrite the
      // second half based on the current anchor.
      for (size_t I = (LastMatchedNonAnchors.size() + 1) / 2;
           I < LastMatchedNonAnchors.size(); I++) {
        const auto &L = LastMatchedNonAnchors[I];
        uint32_t CandidateLineOffset = L.LineOffset + LocationDelta;
        LineLocation Candidate(CandidateLineOffset, L.Discriminator);
        InsertMatching(L, Candidate);
        LLVM_DEBUG(dbgs() << "Location is rematched backwards from " << L
                          << " to " << Candidate << "\n");
      }

      IsMatchedAnchor = true;
      LastMatchedNonAnchors.clear();
    }

    // Match forwards for non-anchor locations.
    if (!IsMatchedAnchor) {
      uint32_t CandidateLineOffset = Loc.LineOffset + LocationDelta;
      LineLocation Candidate(CandidateLineOffset, Loc.Discriminator);
      InsertMatching(Loc, Candidate);
      LLVM_DEBUG(dbgs() << "Location is matched from " << Loc << " to "
                        << Candidate << "\n");
      LastMatchedNonAnchors.emplace_back(Loc);
    }
  }
}

// Filter the non-call locations from IRAnchors and ProfileAnchors and write
// them into a list for random access later.
void SampleProfileMatcher::getFilteredAnchorList(
    const AnchorMap &IRAnchors, const AnchorMap &ProfileAnchors,
    AnchorList &FilteredIRAnchorsList, AnchorList &FilteredProfileAnchorList) {
  for (const auto &I : IRAnchors) {
    if (I.second.stringRef().empty())
      continue;
    FilteredIRAnchorsList.emplace_back(I);
  }

  for (const auto &I : ProfileAnchors)
    FilteredProfileAnchorList.emplace_back(I);
}

// Call target name anchor based profile fuzzy matching.
// Input:
// For IR locations, the anchor is the callee name of direct callsite; For
// profile locations, it's the call target name for BodySamples or inlinee's
// profile name for CallsiteSamples.
// Matching heuristic:
// First match all the anchors using the diff algorithm, then split the
// non-anchor locations between the two anchors evenly, first half are matched
// based on the start anchor, second half are matched based on the end anchor.
// For example, given:
// IR locations:      [1, 2(foo), 3, 5, 6(bar), 7]
// Profile locations: [1, 2, 3(foo), 4, 7, 8(bar), 9]
// The matching gives:
//   [1,    2(foo), 3,  5,  6(bar), 7]
//    |     |       |   |     |     |
//   [1, 2, 3(foo), 4,  7,  8(bar), 9]
// The output mapping: [2->3, 3->4, 5->7, 6->8, 7->9].
void SampleProfileMatcher::runStaleProfileMatching(
    const Function &F, const AnchorMap &IRAnchors,
    const AnchorMap &ProfileAnchors, LocToLocMap &IRToProfileLocationMap,
    bool RunCFGMatching, bool RunCGMatching) {
  if (!RunCFGMatching && !RunCGMatching)
    return;
  LLVM_DEBUG(dbgs() << "Run stale profile matching for " << F.getName()
                    << "\n");
  assert(IRToProfileLocationMap.empty() &&
         "Run stale profile matching only once per function");

  AnchorList FilteredProfileAnchorList;
  AnchorList FilteredIRAnchorsList;
  getFilteredAnchorList(IRAnchors, ProfileAnchors, FilteredIRAnchorsList,
                        FilteredProfileAnchorList);

  if (FilteredIRAnchorsList.empty() || FilteredProfileAnchorList.empty())
    return;

  if (FilteredIRAnchorsList.size() > SalvageStaleProfileMaxCallsites ||
      FilteredProfileAnchorList.size() > SalvageStaleProfileMaxCallsites) {
    LLVM_DEBUG(dbgs() << "Skip stale profile matching for " << F.getName()
                      << " because the number of callsites in the IR is "
                      << FilteredIRAnchorsList.size()
                      << " and in the profile is "
                      << FilteredProfileAnchorList.size() << "\n");
    return;
  }

  // Match the callsite anchors by finding the longest common subsequence
  // between IR and profile.
  // Define a match between two anchors as follows:
  // 1) The function names of anchors are the same.
  // 2) The similarity between the anchor functions is above a threshold if
  // RunCGMatching is set.
  // For 2), we only consider the anchor functions from IR and profile don't
  // appear on either side to reduce the matching scope. Note that we need to
  // use IR anchor as base(A side) to align with the order of
  // IRToProfileLocationMap.
  LocToLocMap MatchedAnchors =
      longestCommonSequence(FilteredIRAnchorsList, FilteredProfileAnchorList,
                            RunCGMatching /* Match unused functions */);

  // CFG level matching:
  // Apply the callsite matchings to infer matching for the basic
  // block(non-callsite) locations and write the result to
  // IRToProfileLocationMap.
  if (RunCFGMatching)
    matchNonCallsiteLocs(MatchedAnchors, IRAnchors, IRToProfileLocationMap);
}

void SampleProfileMatcher::runOnFunction(Function &F) {
  // We need to use flattened function samples for matching.
  // Unlike IR, which includes all callsites from the source code, the callsites
  // in profile only show up when they are hit by samples, i,e. the profile
  // callsites in one context may differ from those in another context. To get
  // the maximum number of callsites, we merge the function profiles from all
  // contexts, aka, the flattened profile to find profile anchors.
  const auto *FSFlattened = getFlattenedSamplesFor(F);
  if (SalvageUnusedProfile && !FSFlattened) {
    // Apply the matching in place to find the new function's matched profile.
    // TODO: For extended profile format, if a function profile is unused and
    // it's top-level, even if the profile is matched, it's not found in the
    // profile. This is because sample reader only read the used profile at the
    // beginning, we need to support loading the profile on-demand in future.
    auto R = FuncToProfileNameMap.find(&F);
    if (R != FuncToProfileNameMap.end())
      FSFlattened = getFlattenedSamplesFor(R->second);
  }
  if (!FSFlattened)
    return;

  // Anchors for IR. It's a map from IR location to callee name, callee name is
  // empty for non-call instruction and use a dummy name(UnknownIndirectCallee)
  // for unknown indrect callee name.
  AnchorMap IRAnchors;
  findIRAnchors(F, IRAnchors);
  // Anchors for profile. It's a map from callsite location to a set of callee
  // name.
  AnchorMap ProfileAnchors;
  findProfileAnchors(*FSFlattened, ProfileAnchors);

  // Compute the callsite match states for profile staleness report.
  if (ReportProfileStaleness || PersistProfileStaleness)
    recordCallsiteMatchStates(F, IRAnchors, ProfileAnchors, nullptr);

  if (!SalvageStaleProfile)
    return;
  // For probe-based profiles, run matching only when profile checksum is
  // mismatched.
  bool ChecksumMismatch = FunctionSamples::ProfileIsProbeBased &&
                          !ProbeManager->profileIsValid(F, *FSFlattened);
  bool RunCFGMatching =
      !FunctionSamples::ProfileIsProbeBased || ChecksumMismatch;
  bool RunCGMatching = SalvageUnusedProfile;
  // For imported functions, the checksum metadata(pseudo_probe_desc) are
  // dropped, so we leverage function attribute(profile-checksum-mismatch) to
  // transfer the info: add the attribute during pre-link phase and check it
  // during post-link phase(see "profileIsValid").
  if (ChecksumMismatch && LTOPhase == ThinOrFullLTOPhase::ThinLTOPreLink)
    F.addFnAttr("profile-checksum-mismatch");

  // The matching result will be saved to IRToProfileLocationMap, create a
  // new map for each function.
  auto &IRToProfileLocationMap = getIRToProfileLocationMap(F);
  runStaleProfileMatching(F, IRAnchors, ProfileAnchors, IRToProfileLocationMap,
                          RunCFGMatching, RunCGMatching);
  // Find and update callsite match states after matching.
  if (RunCFGMatching && (ReportProfileStaleness || PersistProfileStaleness))
    recordCallsiteMatchStates(F, IRAnchors, ProfileAnchors,
                              &IRToProfileLocationMap);
}

void SampleProfileMatcher::recordCallsiteMatchStates(
    const Function &F, const AnchorMap &IRAnchors,
    const AnchorMap &ProfileAnchors,
    const LocToLocMap *IRToProfileLocationMap) {
  bool IsPostMatch = IRToProfileLocationMap != nullptr;
  auto &CallsiteMatchStates =
      FuncCallsiteMatchStates[FunctionSamples::getCanonicalFnName(F.getName())];

  auto MapIRLocToProfileLoc = [&](const LineLocation &IRLoc) {
    // IRToProfileLocationMap is null in pre-match phrase.
    if (!IRToProfileLocationMap)
      return IRLoc;
    const auto &ProfileLoc = IRToProfileLocationMap->find(IRLoc);
    if (ProfileLoc != IRToProfileLocationMap->end())
      return ProfileLoc->second;
    else
      return IRLoc;
  };

  for (const auto &I : IRAnchors) {
    // After fuzzy profile matching, use the matching result to remap the
    // current IR callsite.
    const auto &ProfileLoc = MapIRLocToProfileLoc(I.first);
    const auto &IRCalleeId = I.second;
    const auto &It = ProfileAnchors.find(ProfileLoc);
    if (It == ProfileAnchors.end())
      continue;
    const auto &ProfCalleeId = It->second;
    if (IRCalleeId == ProfCalleeId) {
      auto It = CallsiteMatchStates.find(ProfileLoc);
      if (It == CallsiteMatchStates.end())
        CallsiteMatchStates.emplace(ProfileLoc, MatchState::InitialMatch);
      else if (IsPostMatch) {
        if (It->second == MatchState::InitialMatch)
          It->second = MatchState::UnchangedMatch;
        else if (It->second == MatchState::InitialMismatch)
          It->second = MatchState::RecoveredMismatch;
      }
    }
  }

  // Check if there are any callsites in the profile that does not match to any
  // IR callsites.
  for (const auto &I : ProfileAnchors) {
    const auto &Loc = I.first;
    assert(!I.second.stringRef().empty() && "Callees should not be empty");
    auto It = CallsiteMatchStates.find(Loc);
    if (It == CallsiteMatchStates.end())
      CallsiteMatchStates.emplace(Loc, MatchState::InitialMismatch);
    else if (IsPostMatch) {
      // Update the state if it's not matched(UnchangedMatch or
      // RecoveredMismatch).
      if (It->second == MatchState::InitialMismatch)
        It->second = MatchState::UnchangedMismatch;
      else if (It->second == MatchState::InitialMatch)
        It->second = MatchState::RemovedMatch;
    }
  }
}

void SampleProfileMatcher::countMismatchedFuncSamples(const FunctionSamples &FS,
                                                      bool IsTopLevel) {
  const auto *FuncDesc = ProbeManager->getDesc(FS.getGUID());
  // Skip the function that is external or renamed.
  if (!FuncDesc)
    return;

  if (ProbeManager->profileIsHashMismatched(*FuncDesc, FS)) {
    if (IsTopLevel)
      NumStaleProfileFunc++;
    // Given currently all probe ids are after block probe ids, once the
    // checksum is mismatched, it's likely all the callites are mismatched and
    // dropped. We conservatively count all the samples as mismatched and stop
    // counting the inlinees' profiles.
    MismatchedFunctionSamples += FS.getTotalSamples();
    return;
  }

  // Even the current-level function checksum is matched, it's possible that the
  // nested inlinees' checksums are mismatched that affect the inlinee's sample
  // loading, we need to go deeper to check the inlinees' function samples.
  // Similarly, count all the samples as mismatched if the inlinee's checksum is
  // mismatched using this recursive function.
  for (const auto &I : FS.getCallsiteSamples())
    for (const auto &CS : I.second)
      countMismatchedFuncSamples(CS.second, false);
}

void SampleProfileMatcher::countMismatchedCallsiteSamples(
    const FunctionSamples &FS) {
  auto It = FuncCallsiteMatchStates.find(FS.getFuncName());
  // Skip it if no mismatched callsite or this is an external function.
  if (It == FuncCallsiteMatchStates.end() || It->second.empty())
    return;
  const auto &CallsiteMatchStates = It->second;

  auto findMatchState = [&](const LineLocation &Loc) {
    auto It = CallsiteMatchStates.find(Loc);
    if (It == CallsiteMatchStates.end())
      return MatchState::Unknown;
    return It->second;
  };

  auto AttributeMismatchedSamples = [&](const enum MatchState &State,
                                        uint64_t Samples) {
    if (isMismatchState(State))
      MismatchedCallsiteSamples += Samples;
    else if (State == MatchState::RecoveredMismatch)
      RecoveredCallsiteSamples += Samples;
  };

  // The non-inlined callsites are saved in the body samples of function
  // profile, go through it to count the non-inlined callsite samples.
  for (const auto &I : FS.getBodySamples())
    AttributeMismatchedSamples(findMatchState(I.first), I.second.getSamples());

  // Count the inlined callsite samples.
  for (const auto &I : FS.getCallsiteSamples()) {
    auto State = findMatchState(I.first);
    uint64_t CallsiteSamples = 0;
    for (const auto &CS : I.second)
      CallsiteSamples += CS.second.getTotalSamples();
    AttributeMismatchedSamples(State, CallsiteSamples);

    if (isMismatchState(State))
      continue;

    // When the current level of inlined call site matches the profiled call
    // site, we need to go deeper along the inline tree to count mismatches from
    // lower level inlinees.
    for (const auto &CS : I.second)
      countMismatchedCallsiteSamples(CS.second);
  }
}

void SampleProfileMatcher::countMismatchCallsites(const FunctionSamples &FS) {
  auto It = FuncCallsiteMatchStates.find(FS.getFuncName());
  // Skip it if no mismatched callsite or this is an external function.
  if (It == FuncCallsiteMatchStates.end() || It->second.empty())
    return;
  const auto &MatchStates = It->second;
  [[maybe_unused]] bool OnInitialState =
      isInitialState(MatchStates.begin()->second);
  for (const auto &I : MatchStates) {
    TotalProfiledCallsites++;
    assert(
        (OnInitialState ? isInitialState(I.second) : isFinalState(I.second)) &&
        "Profile matching state is inconsistent");

    if (isMismatchState(I.second))
      NumMismatchedCallsites++;
    else if (I.second == MatchState::RecoveredMismatch)
      NumRecoveredCallsites++;
  }
}

void SampleProfileMatcher::countCallGraphRecoveredSamples(
    const FunctionSamples &FS,
    std::unordered_set<FunctionId> &CallGraphRecoveredProfiles) {
  if (CallGraphRecoveredProfiles.count(FS.getFunction())) {
    NumCallGraphRecoveredFuncSamples += FS.getTotalSamples();
    return;
  }

  for (const auto &CM : FS.getCallsiteSamples()) {
    for (const auto &CS : CM.second) {
      countCallGraphRecoveredSamples(CS.second, CallGraphRecoveredProfiles);
    }
  }
}

void SampleProfileMatcher::computeAndReportProfileStaleness() {
  if (!ReportProfileStaleness && !PersistProfileStaleness)
    return;

  std::unordered_set<FunctionId> CallGraphRecoveredProfiles;
  if (SalvageUnusedProfile) {
    for (const auto &I : FuncToProfileNameMap) {
      CallGraphRecoveredProfiles.insert(I.second);
      if (GlobalValue::isAvailableExternallyLinkage(I.first->getLinkage()))
        continue;
      NumCallGraphRecoveredProfiledFunc++;
    }
  }

  // Count profile mismatches for profile staleness report.
  for (const auto &F : M) {
    if (skipProfileForFunction(F))
      continue;
    // As the stats will be merged by linker, skip reporting the metrics for
    // imported functions to avoid repeated counting.
    if (GlobalValue::isAvailableExternallyLinkage(F.getLinkage()))
      continue;
    const auto *FS = Reader.getSamplesFor(F);
    if (!FS)
      continue;
    TotalProfiledFunc++;
    TotalFunctionSamples += FS->getTotalSamples();

    if (SalvageUnusedProfile && !CallGraphRecoveredProfiles.empty())
      countCallGraphRecoveredSamples(*FS, CallGraphRecoveredProfiles);

    // Checksum mismatch is only used in pseudo-probe mode.
    if (FunctionSamples::ProfileIsProbeBased)
      countMismatchedFuncSamples(*FS, true);

    // Count mismatches and samples for calliste.
    countMismatchCallsites(*FS);
    countMismatchedCallsiteSamples(*FS);
  }

  if (ReportProfileStaleness) {
    if (FunctionSamples::ProfileIsProbeBased) {
      errs() << "(" << NumStaleProfileFunc << "/" << TotalProfiledFunc
             << ") of functions' profile are invalid and ("
             << MismatchedFunctionSamples << "/" << TotalFunctionSamples
             << ") of samples are discarded due to function hash mismatch.\n";
    }
    if (SalvageUnusedProfile) {
      errs() << "(" << NumCallGraphRecoveredProfiledFunc << "/"
             << TotalProfiledFunc << ") of functions' profile are matched and ("
             << NumCallGraphRecoveredFuncSamples << "/" << TotalFunctionSamples
             << ") of samples are reused by call graph matching.\n";
    }

    errs() << "(" << (NumMismatchedCallsites + NumRecoveredCallsites) << "/"
           << TotalProfiledCallsites
           << ") of callsites' profile are invalid and ("
           << (MismatchedCallsiteSamples + RecoveredCallsiteSamples) << "/"
           << TotalFunctionSamples
           << ") of samples are discarded due to callsite location mismatch.\n";
    errs() << "(" << NumRecoveredCallsites << "/"
           << (NumRecoveredCallsites + NumMismatchedCallsites)
           << ") of callsites and (" << RecoveredCallsiteSamples << "/"
           << (RecoveredCallsiteSamples + MismatchedCallsiteSamples)
           << ") of samples are recovered by stale profile matching.\n";
  }

  if (PersistProfileStaleness) {
    LLVMContext &Ctx = M.getContext();
    MDBuilder MDB(Ctx);

    SmallVector<std::pair<StringRef, uint64_t>> ProfStatsVec;
    if (FunctionSamples::ProfileIsProbeBased) {
      ProfStatsVec.emplace_back("NumStaleProfileFunc", NumStaleProfileFunc);
      ProfStatsVec.emplace_back("TotalProfiledFunc", TotalProfiledFunc);
      ProfStatsVec.emplace_back("MismatchedFunctionSamples",
                                MismatchedFunctionSamples);
      ProfStatsVec.emplace_back("TotalFunctionSamples", TotalFunctionSamples);
    }

    if (SalvageUnusedProfile) {
      ProfStatsVec.emplace_back("NumCallGraphRecoveredProfiledFunc",
                                NumCallGraphRecoveredProfiledFunc);
      ProfStatsVec.emplace_back("NumCallGraphRecoveredFuncSamples",
                                NumCallGraphRecoveredFuncSamples);
    }

    ProfStatsVec.emplace_back("NumMismatchedCallsites", NumMismatchedCallsites);
    ProfStatsVec.emplace_back("NumRecoveredCallsites", NumRecoveredCallsites);
    ProfStatsVec.emplace_back("TotalProfiledCallsites", TotalProfiledCallsites);
    ProfStatsVec.emplace_back("MismatchedCallsiteSamples",
                              MismatchedCallsiteSamples);
    ProfStatsVec.emplace_back("RecoveredCallsiteSamples",
                              RecoveredCallsiteSamples);

    auto *MD = MDB.createLLVMStats(ProfStatsVec);
    auto *NMD = M.getOrInsertNamedMetadata("llvm.stats");
    NMD->addOperand(MD);
  }
}

void SampleProfileMatcher::findFunctionsWithoutProfile() {
  // TODO: Support MD5 profile.
  if (FunctionSamples::UseMD5)
    return;
  StringSet<> NamesInProfile;
  if (auto NameTable = Reader.getNameTable()) {
    for (auto Name : *NameTable)
      NamesInProfile.insert(Name.stringRef());
  }

  for (auto &F : M) {
    // Skip declarations, as even if the function can be matched, we have
    // nothing to do with it.
    if (F.isDeclaration())
      continue;

    StringRef CanonFName = FunctionSamples::getCanonicalFnName(F.getName());
    const auto *FS = getFlattenedSamplesFor(F);
    if (FS)
      continue;

    // For extended binary, functions fully inlined may not be loaded in the
    // top-level profile, so check the NameTable which has the all symbol names
    // in profile.
    if (NamesInProfile.count(CanonFName))
      continue;

    // For extended binary, non-profiled function symbols are in the profile
    // symbol list table.
    if (PSL && PSL->contains(CanonFName))
      continue;

    LLVM_DEBUG(dbgs() << "Function " << CanonFName
                      << " is not in profile or profile symbol list.\n");
    FunctionsWithoutProfile[FunctionId(CanonFName)] = &F;
  }
}

bool SampleProfileMatcher::functionMatchesProfileHelper(
    const Function &IRFunc, const FunctionId &ProfFunc) {
  // The value is in the range [0, 1]. The bigger the value is, the more similar
  // two sequences are.
  float Similarity = 0.0;

  const auto *FSFlattened = getFlattenedSamplesFor(ProfFunc);
  if (!FSFlattened)
    return false;
  // The check for similarity or checksum may not be reliable if the function is
  // tiny, we use the number of basic block as a proxy for the function
  // complexity and skip the matching if it's too small.
  if (IRFunc.size() < MinFuncCountForCGMatching ||
      FSFlattened->getBodySamples().size() < MinFuncCountForCGMatching)
    return false;

  // For probe-based function, we first trust the checksum info. If the checksum
  // doesn't match, we continue checking for similarity.
  if (FunctionSamples::ProfileIsProbeBased) {
    const auto *FuncDesc = ProbeManager->getDesc(IRFunc);
    if (FuncDesc &&
        !ProbeManager->profileIsHashMismatched(*FuncDesc, *FSFlattened)) {
      LLVM_DEBUG(dbgs() << "The checksums for " << IRFunc.getName()
                        << "(IR) and " << ProfFunc << "(Profile) match.\n");

      return true;
    }
  }

  AnchorMap IRAnchors;
  findIRAnchors(IRFunc, IRAnchors);
  AnchorMap ProfileAnchors;
  findProfileAnchors(*FSFlattened, ProfileAnchors);

  AnchorList FilteredIRAnchorsList;
  AnchorList FilteredProfileAnchorList;
  getFilteredAnchorList(IRAnchors, ProfileAnchors, FilteredIRAnchorsList,
                        FilteredProfileAnchorList);

  // Similarly skip the matching if the num of anchors is not enough.
  if (FilteredIRAnchorsList.size() < MinCallCountForCGMatching ||
      FilteredProfileAnchorList.size() < MinCallCountForCGMatching)
    return false;

  // Use the diff algorithm to find the LCS between IR and profile.

  // Don't recursively match the callee function to avoid infinite matching,
  // callee functions will be handled later since it's processed in top-down
  // order .
  LocToLocMap MatchedAnchors =
      longestCommonSequence(FilteredIRAnchorsList, FilteredProfileAnchorList,
                            false /* Match unused functions */);

  Similarity =
      static_cast<float>(MatchedAnchors.size()) * 2 /
      (FilteredIRAnchorsList.size() + FilteredProfileAnchorList.size());

  LLVM_DEBUG(dbgs() << "The similarity between " << IRFunc.getName()
                    << "(IR) and " << ProfFunc << "(profile) is "
                    << format("%.2f", Similarity) << "\n");
  assert((Similarity >= 0 && Similarity <= 1.0) &&
         "Similarity value should be in [0, 1]");
  return Similarity * 100 > FuncProfileSimilarityThreshold;
}

// If FindMatchedProfileOnly is set to true, only use the processed function
// results. This is used for skipping the repeated recursive matching.
bool SampleProfileMatcher::functionMatchesProfile(Function &IRFunc,
                                                  const FunctionId &ProfFunc,
                                                  bool FindMatchedProfileOnly) {
  auto R = FuncProfileMatchCache.find({&IRFunc, ProfFunc});
  if (R != FuncProfileMatchCache.end())
    return R->second;

  if (FindMatchedProfileOnly)
    return false;

  bool Matched = functionMatchesProfileHelper(IRFunc, ProfFunc);
  FuncProfileMatchCache[{&IRFunc, ProfFunc}] = Matched;
  if (Matched) {
    FuncToProfileNameMap[&IRFunc] = ProfFunc;
    LLVM_DEBUG(dbgs() << "Function:" << IRFunc.getName()
                      << " matches profile:" << ProfFunc << "\n");
  }

  return Matched;
}

void SampleProfileMatcher::runOnModule() {
  ProfileConverter::flattenProfile(Reader.getProfiles(), FlattenedProfiles,
                                   FunctionSamples::ProfileIsCS);
  if (SalvageUnusedProfile)
    findFunctionsWithoutProfile();

  // Process the matching in top-down order so that the caller matching result
  // can be used to the callee matching.
  std::vector<Function *> TopDownFunctionList;
  TopDownFunctionList.reserve(M.size());
  buildTopDownFuncOrder(CG, TopDownFunctionList);
  for (auto *F : TopDownFunctionList) {
    if (skipProfileForFunction(*F))
      continue;
    runOnFunction(*F);
  }

  // Update the data in SampleLoader.
  if (SalvageUnusedProfile)
    for (auto &I : FuncToProfileNameMap) {
      assert(I.first && "New function is null");
      FunctionId FuncName(I.first->getName());
      FuncNameToProfNameMap->emplace(FuncName, I.second);
      // We need to remove the old entry to avoid duplicating the function
      // processing.
      SymbolMap->erase(FuncName);
      SymbolMap->emplace(I.second, I.first);
    }

  if (SalvageStaleProfile)
    distributeIRToProfileLocationMap();

  computeAndReportProfileStaleness();
}

void SampleProfileMatcher::distributeIRToProfileLocationMap(
    FunctionSamples &FS) {
  const auto ProfileMappings = FuncMappings.find(FS.getFuncName());
  if (ProfileMappings != FuncMappings.end()) {
    FS.setIRToProfileLocationMap(&(ProfileMappings->second));
  }

  for (auto &Callees :
       const_cast<CallsiteSampleMap &>(FS.getCallsiteSamples())) {
    for (auto &FS : Callees.second) {
      distributeIRToProfileLocationMap(FS.second);
    }
  }
}

// Use a central place to distribute the matching results. Outlined and inlined
// profile with the function name will be set to the same pointer.
void SampleProfileMatcher::distributeIRToProfileLocationMap() {
  for (auto &I : Reader.getProfiles()) {
    distributeIRToProfileLocationMap(I.second);
  }
}
