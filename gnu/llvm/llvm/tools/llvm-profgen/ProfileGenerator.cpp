//===-- ProfileGenerator.cpp - Profile Generator  ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "ProfileGenerator.h"
#include "ErrorHandling.h"
#include "MissingFrameInferrer.h"
#include "PerfReader.h"
#include "ProfiledBinary.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/ProfileData/ProfileCommon.h"
#include <algorithm>
#include <float.h>
#include <unordered_set>
#include <utility>

cl::opt<std::string> OutputFilename("output", cl::value_desc("output"),
                                    cl::Required,
                                    cl::desc("Output profile file"));
static cl::alias OutputA("o", cl::desc("Alias for --output"),
                         cl::aliasopt(OutputFilename));

static cl::opt<SampleProfileFormat> OutputFormat(
    "format", cl::desc("Format of output profile"), cl::init(SPF_Ext_Binary),
    cl::values(
        clEnumValN(SPF_Binary, "binary", "Binary encoding (default)"),
        clEnumValN(SPF_Ext_Binary, "extbinary", "Extensible binary encoding"),
        clEnumValN(SPF_Text, "text", "Text encoding"),
        clEnumValN(SPF_GCC, "gcc",
                   "GCC encoding (only meaningful for -sample)")));

static cl::opt<bool> UseMD5(
    "use-md5", cl::Hidden,
    cl::desc("Use md5 to represent function names in the output profile (only "
             "meaningful for -extbinary)"));

static cl::opt<bool> PopulateProfileSymbolList(
    "populate-profile-symbol-list", cl::init(false), cl::Hidden,
    cl::desc("Populate profile symbol list (only meaningful for -extbinary)"));

static cl::opt<bool> FillZeroForAllFuncs(
    "fill-zero-for-all-funcs", cl::init(false), cl::Hidden,
    cl::desc("Attribute all functions' range with zero count "
             "even it's not hit by any samples."));

static cl::opt<int32_t, true> RecursionCompression(
    "compress-recursion",
    cl::desc("Compressing recursion by deduplicating adjacent frame "
             "sequences up to the specified size. -1 means no size limit."),
    cl::Hidden,
    cl::location(llvm::sampleprof::CSProfileGenerator::MaxCompressionSize));

static cl::opt<bool>
    TrimColdProfile("trim-cold-profile",
                    cl::desc("If the total count of the profile is smaller "
                             "than threshold, it will be trimmed."));

static cl::opt<bool> CSProfMergeColdContext(
    "csprof-merge-cold-context", cl::init(true),
    cl::desc("If the total count of context profile is smaller than "
             "the threshold, it will be merged into context-less base "
             "profile."));

static cl::opt<uint32_t> CSProfMaxColdContextDepth(
    "csprof-max-cold-context-depth", cl::init(1),
    cl::desc("Keep the last K contexts while merging cold profile. 1 means the "
             "context-less base profile"));

static cl::opt<int, true> CSProfMaxContextDepth(
    "csprof-max-context-depth",
    cl::desc("Keep the last K contexts while merging profile. -1 means no "
             "depth limit."),
    cl::location(llvm::sampleprof::CSProfileGenerator::MaxContextDepth));

static cl::opt<double> ProfileDensityThreshold(
    "profile-density-threshold", llvm::cl::init(50),
    llvm::cl::desc("If the profile density is below the given threshold, it "
                   "will be suggested to increase the sampling rate."),
    llvm::cl::Optional);
static cl::opt<bool> ShowDensity("show-density", llvm::cl::init(false),
                                 llvm::cl::desc("show profile density details"),
                                 llvm::cl::Optional);
static cl::opt<int> ProfileDensityCutOffHot(
    "profile-density-cutoff-hot", llvm::cl::init(990000),
    llvm::cl::desc("Total samples cutoff for functions used to calculate "
                   "profile density."));

static cl::opt<bool> UpdateTotalSamples(
    "update-total-samples", llvm::cl::init(false),
    llvm::cl::desc(
        "Update total samples by accumulating all its body samples."),
    llvm::cl::Optional);

static cl::opt<bool> GenCSNestedProfile(
    "gen-cs-nested-profile", cl::Hidden, cl::init(true),
    cl::desc("Generate nested function profiles for CSSPGO"));

cl::opt<bool> InferMissingFrames(
    "infer-missing-frames", llvm::cl::init(true),
    llvm::cl::desc(
        "Infer missing call frames due to compiler tail call elimination."),
    llvm::cl::Optional);

extern cl::opt<bool> LeadingIPOnly;

using namespace llvm;
using namespace sampleprof;

namespace llvm {
extern cl::opt<int> ProfileSummaryCutoffHot;
extern cl::opt<bool> UseContextLessSummary;

namespace sampleprof {

// Initialize the MaxCompressionSize to -1 which means no size limit
int32_t CSProfileGenerator::MaxCompressionSize = -1;

int CSProfileGenerator::MaxContextDepth = -1;

bool ProfileGeneratorBase::UseFSDiscriminator = false;

std::unique_ptr<ProfileGeneratorBase>
ProfileGeneratorBase::create(ProfiledBinary *Binary,
                             const ContextSampleCounterMap *SampleCounters,
                             bool ProfileIsCS) {
  std::unique_ptr<ProfileGeneratorBase> Generator;
  if (ProfileIsCS) {
    Generator.reset(new CSProfileGenerator(Binary, SampleCounters));
  } else {
    Generator.reset(new ProfileGenerator(Binary, SampleCounters));
  }
  ProfileGeneratorBase::UseFSDiscriminator = Binary->useFSDiscriminator();
  FunctionSamples::ProfileIsFS = Binary->useFSDiscriminator();

  return Generator;
}

std::unique_ptr<ProfileGeneratorBase>
ProfileGeneratorBase::create(ProfiledBinary *Binary, SampleProfileMap &Profiles,
                             bool ProfileIsCS) {
  std::unique_ptr<ProfileGeneratorBase> Generator;
  if (ProfileIsCS) {
    Generator.reset(new CSProfileGenerator(Binary, Profiles));
  } else {
    Generator.reset(new ProfileGenerator(Binary, std::move(Profiles)));
  }
  ProfileGeneratorBase::UseFSDiscriminator = Binary->useFSDiscriminator();
  FunctionSamples::ProfileIsFS = Binary->useFSDiscriminator();

  return Generator;
}

void ProfileGeneratorBase::write(std::unique_ptr<SampleProfileWriter> Writer,
                                 SampleProfileMap &ProfileMap) {
  // Populate profile symbol list if extended binary format is used.
  ProfileSymbolList SymbolList;

  if (PopulateProfileSymbolList && OutputFormat == SPF_Ext_Binary) {
    Binary->populateSymbolListFromDWARF(SymbolList);
    Writer->setProfileSymbolList(&SymbolList);
  }

  if (std::error_code EC = Writer->write(ProfileMap))
    exitWithError(std::move(EC));
}

void ProfileGeneratorBase::write() {
  auto WriterOrErr = SampleProfileWriter::create(OutputFilename, OutputFormat);
  if (std::error_code EC = WriterOrErr.getError())
    exitWithError(EC, OutputFilename);

  if (UseMD5) {
    if (OutputFormat != SPF_Ext_Binary)
      WithColor::warning() << "-use-md5 is ignored. Specify "
                              "--format=extbinary to enable it\n";
    else
      WriterOrErr.get()->setUseMD5();
  }

  write(std::move(WriterOrErr.get()), ProfileMap);
}

void ProfileGeneratorBase::showDensitySuggestion(double Density) {
  if (Density == 0.0)
    WithColor::warning() << "The output profile is empty or the "
                            "--profile-density-cutoff-hot option is "
                            "set too low. Please check your command.\n";
  else if (Density < ProfileDensityThreshold)
    WithColor::warning()
        << "Sample PGO is estimated to optimize better with "
        << format("%.1f", ProfileDensityThreshold / Density)
        << "x more samples. Please consider increasing sampling rate or "
           "profiling for longer duration to get more samples.\n";

  if (ShowDensity)
    outs() << "Functions with density >= " << format("%.1f", Density)
           << " account for "
           << format("%.2f",
                     static_cast<double>(ProfileDensityCutOffHot) / 10000)
           << "% total sample counts.\n";
}

bool ProfileGeneratorBase::filterAmbiguousProfile(FunctionSamples &FS) {
  for (const auto &Prefix : FuncPrefixsToFilter) {
    if (FS.getFuncName().starts_with(Prefix))
      return true;
  }

  // Filter the function profiles for the inlinees. It's useful for fuzzy
  // profile matching which flattens the profile and inlinees' samples are
  // merged into top-level function.
  for (auto &Callees :
       const_cast<CallsiteSampleMap &>(FS.getCallsiteSamples())) {
    auto &CalleesMap = Callees.second;
    for (auto I = CalleesMap.begin(); I != CalleesMap.end();) {
      auto FS = I++;
      if (filterAmbiguousProfile(FS->second))
        CalleesMap.erase(FS);
    }
  }
  return false;
}

// For built-in local initialization function such as __cxx_global_var_init,
// __tls_init prefix function, there could be multiple versions of the functions
// in the final binary. However, in the profile generation, we call
// getCanonicalFnName to canonicalize the names which strips the suffixes.
// Therefore, samples from different functions queries the same profile and the
// samples are merged. As the functions are essentially different, entries of
// the merged profile are ambiguous. In sample loader, the IR from one version
// would be attributed towards a merged entries, which is inaccurate. Especially
// for fuzzy profile matching, it gets multiple callsites(from different
// function) but used to match one callsite, which misleads the matching and
// causes a lot of false positives report. Hence, we want to filter them out
// from the profile map during the profile generation time. The profiles are all
// cold functions, it won't have perf impact.
void ProfileGeneratorBase::filterAmbiguousProfile(SampleProfileMap &Profiles) {
  for (auto I = ProfileMap.begin(); I != ProfileMap.end();) {
    auto FS = I++;
    if (filterAmbiguousProfile(FS->second))
      ProfileMap.erase(FS);
  }
}

void ProfileGeneratorBase::findDisjointRanges(RangeSample &DisjointRanges,
                                              const RangeSample &Ranges) {

  /*
  Regions may overlap with each other. Using the boundary info, find all
  disjoint ranges and their sample count. BoundaryPoint contains the count
  multiple samples begin/end at this points.

  |<--100-->|           Sample1
  |<------200------>|   Sample2
  A         B       C

  In the example above,
  Sample1 begins at A, ends at B, its value is 100.
  Sample2 beings at A, ends at C, its value is 200.
  For A, BeginCount is the sum of sample begins at A, which is 300 and no
  samples ends at A, so EndCount is 0.
  Then boundary points A, B, and C with begin/end counts are:
  A: (300, 0)
  B: (0, 100)
  C: (0, 200)
  */
  struct BoundaryPoint {
    // Sum of sample counts beginning at this point
    uint64_t BeginCount = UINT64_MAX;
    // Sum of sample counts ending at this point
    uint64_t EndCount = UINT64_MAX;
    // Is the begin point of a zero range.
    bool IsZeroRangeBegin = false;
    // Is the end point of a zero range.
    bool IsZeroRangeEnd = false;

    void addBeginCount(uint64_t Count) {
      if (BeginCount == UINT64_MAX)
        BeginCount = 0;
      BeginCount += Count;
    }

    void addEndCount(uint64_t Count) {
      if (EndCount == UINT64_MAX)
        EndCount = 0;
      EndCount += Count;
    }
  };

  /*
  For the above example. With boundary points, follwing logic finds two
  disjoint region of

  [A,B]:   300
  [B+1,C]: 200

  If there is a boundary point that both begin and end, the point itself
  becomes a separate disjoint region. For example, if we have original
  ranges of

  |<--- 100 --->|
                |<--- 200 --->|
  A             B             C

  there are three boundary points with their begin/end counts of

  A: (100, 0)
  B: (200, 100)
  C: (0, 200)

  the disjoint ranges would be

  [A, B-1]: 100
  [B, B]:   300
  [B+1, C]: 200.

  Example for zero value range:

    |<--- 100 --->|
                       |<--- 200 --->|
  |<---------------  0 ----------------->|
  A  B            C    D             E   F

  [A, B-1]  : 0
  [B, C]    : 100
  [C+1, D-1]: 0
  [D, E]    : 200
  [E+1, F]  : 0
  */
  std::map<uint64_t, BoundaryPoint> Boundaries;

  for (const auto &Item : Ranges) {
    assert(Item.first.first <= Item.first.second &&
           "Invalid instruction range");
    auto &BeginPoint = Boundaries[Item.first.first];
    auto &EndPoint = Boundaries[Item.first.second];
    uint64_t Count = Item.second;

    BeginPoint.addBeginCount(Count);
    EndPoint.addEndCount(Count);
    if (Count == 0) {
      BeginPoint.IsZeroRangeBegin = true;
      EndPoint.IsZeroRangeEnd = true;
    }
  }

  // Use UINT64_MAX to indicate there is no existing range between BeginAddress
  // and the next valid address
  uint64_t BeginAddress = UINT64_MAX;
  int ZeroRangeDepth = 0;
  uint64_t Count = 0;
  for (const auto &Item : Boundaries) {
    uint64_t Address = Item.first;
    const BoundaryPoint &Point = Item.second;
    if (Point.BeginCount != UINT64_MAX) {
      if (BeginAddress != UINT64_MAX)
        DisjointRanges[{BeginAddress, Address - 1}] = Count;
      Count += Point.BeginCount;
      BeginAddress = Address;
      ZeroRangeDepth += Point.IsZeroRangeBegin;
    }
    if (Point.EndCount != UINT64_MAX) {
      assert((BeginAddress != UINT64_MAX) &&
             "First boundary point cannot be 'end' point");
      DisjointRanges[{BeginAddress, Address}] = Count;
      assert(Count >= Point.EndCount && "Mismatched live ranges");
      Count -= Point.EndCount;
      BeginAddress = Address + 1;
      ZeroRangeDepth -= Point.IsZeroRangeEnd;
      // If the remaining count is zero and it's no longer in a zero range, this
      // means we consume all the ranges before, thus mark BeginAddress as
      // UINT64_MAX. e.g. supposing we have two non-overlapping ranges:
      //  [<---- 10 ---->]
      //                       [<---- 20 ---->]
      //   A             B     C              D
      // The BeginAddress(B+1) will reset to invalid(UINT64_MAX), so we won't
      // have the [B+1, C-1] zero range.
      if (Count == 0 && ZeroRangeDepth == 0)
        BeginAddress = UINT64_MAX;
    }
  }
}

void ProfileGeneratorBase::updateBodySamplesforFunctionProfile(
    FunctionSamples &FunctionProfile, const SampleContextFrame &LeafLoc,
    uint64_t Count) {
  // Use the maximum count of samples with same line location
  uint32_t Discriminator = getBaseDiscriminator(LeafLoc.Location.Discriminator);

  if (LeadingIPOnly) {
    // When computing an IP-based profile we take the SUM of counts at the
    // location instead of applying duplication factors and taking the MAX.
    FunctionProfile.addBodySamples(LeafLoc.Location.LineOffset, Discriminator,
                                   Count);
  } else {
    // Otherwise, use duplication factor to compensate for loop
    // unroll/vectorization. Note that this is only needed when we're taking
    // MAX of the counts at the location instead of SUM.
    Count *= getDuplicationFactor(LeafLoc.Location.Discriminator);

    ErrorOr<uint64_t> R = FunctionProfile.findSamplesAt(
        LeafLoc.Location.LineOffset, Discriminator);

    uint64_t PreviousCount = R ? R.get() : 0;
    if (PreviousCount <= Count) {
      FunctionProfile.addBodySamples(LeafLoc.Location.LineOffset, Discriminator,
                                     Count - PreviousCount);
    }
  }
}

void ProfileGeneratorBase::updateTotalSamples() {
  for (auto &Item : ProfileMap) {
    FunctionSamples &FunctionProfile = Item.second;
    FunctionProfile.updateTotalSamples();
  }
}

void ProfileGeneratorBase::updateCallsiteSamples() {
  for (auto &Item : ProfileMap) {
    FunctionSamples &FunctionProfile = Item.second;
    FunctionProfile.updateCallsiteSamples();
  }
}

void ProfileGeneratorBase::updateFunctionSamples() {
  updateCallsiteSamples();

  if (UpdateTotalSamples)
    updateTotalSamples();
}

void ProfileGeneratorBase::collectProfiledFunctions() {
  std::unordered_set<const BinaryFunction *> ProfiledFunctions;
  if (collectFunctionsFromRawProfile(ProfiledFunctions))
    Binary->setProfiledFunctions(ProfiledFunctions);
  else if (collectFunctionsFromLLVMProfile(ProfiledFunctions))
    Binary->setProfiledFunctions(ProfiledFunctions);
  else
    llvm_unreachable("Unsupported input profile");
}

bool ProfileGeneratorBase::collectFunctionsFromRawProfile(
    std::unordered_set<const BinaryFunction *> &ProfiledFunctions) {
  if (!SampleCounters)
    return false;
  // Go through all the stacks, ranges and branches in sample counters, use
  // the start of the range to look up the function it belongs and record the
  // function.
  for (const auto &CI : *SampleCounters) {
    if (const auto *CtxKey = dyn_cast<AddrBasedCtxKey>(CI.first.getPtr())) {
      for (auto StackAddr : CtxKey->Context) {
        if (FuncRange *FRange = Binary->findFuncRange(StackAddr))
          ProfiledFunctions.insert(FRange->Func);
      }
    }

    for (auto Item : CI.second.RangeCounter) {
      uint64_t StartAddress = Item.first.first;
      if (FuncRange *FRange = Binary->findFuncRange(StartAddress))
        ProfiledFunctions.insert(FRange->Func);
    }

    for (auto Item : CI.second.BranchCounter) {
      uint64_t SourceAddress = Item.first.first;
      uint64_t TargetAddress = Item.first.second;
      if (FuncRange *FRange = Binary->findFuncRange(SourceAddress))
        ProfiledFunctions.insert(FRange->Func);
      if (FuncRange *FRange = Binary->findFuncRange(TargetAddress))
        ProfiledFunctions.insert(FRange->Func);
    }
  }
  return true;
}

bool ProfileGenerator::collectFunctionsFromLLVMProfile(
    std::unordered_set<const BinaryFunction *> &ProfiledFunctions) {
  for (const auto &FS : ProfileMap) {
    if (auto *Func = Binary->getBinaryFunction(FS.second.getFunction()))
      ProfiledFunctions.insert(Func);
  }
  return true;
}

bool CSProfileGenerator::collectFunctionsFromLLVMProfile(
    std::unordered_set<const BinaryFunction *> &ProfiledFunctions) {
  for (auto *Node : ContextTracker) {
    if (!Node->getFuncName().empty())
      if (auto *Func = Binary->getBinaryFunction(Node->getFuncName()))
        ProfiledFunctions.insert(Func);
  }
  return true;
}

FunctionSamples &
ProfileGenerator::getTopLevelFunctionProfile(FunctionId FuncName) {
  SampleContext Context(FuncName);
  return ProfileMap.create(Context);
}

void ProfileGenerator::generateProfile() {
  collectProfiledFunctions();

  if (Binary->usePseudoProbes())
    Binary->decodePseudoProbe();

  if (SampleCounters) {
    if (Binary->usePseudoProbes()) {
      generateProbeBasedProfile();
    } else {
      generateLineNumBasedProfile();
    }
  }

  postProcessProfiles();
}

void ProfileGenerator::postProcessProfiles() {
  computeSummaryAndThreshold(ProfileMap);
  trimColdProfiles(ProfileMap, ColdCountThreshold);
  filterAmbiguousProfile(ProfileMap);
  calculateAndShowDensity(ProfileMap);
}

void ProfileGenerator::trimColdProfiles(const SampleProfileMap &Profiles,
                                        uint64_t ColdCntThreshold) {
  if (!TrimColdProfile)
    return;

  // Move cold profiles into a tmp container.
  std::vector<hash_code> ColdProfileHashes;
  for (const auto &I : ProfileMap) {
    if (I.second.getTotalSamples() < ColdCntThreshold)
      ColdProfileHashes.emplace_back(I.first);
  }

  // Remove the cold profile from ProfileMap.
  for (const auto &I : ColdProfileHashes)
    ProfileMap.erase(I);
}

void ProfileGenerator::generateLineNumBasedProfile() {
  assert(SampleCounters->size() == 1 &&
         "Must have one entry for profile generation.");
  const SampleCounter &SC = SampleCounters->begin()->second;
  // Fill in function body samples
  populateBodySamplesForAllFunctions(SC.RangeCounter);
  // Fill in boundary sample counts as well as call site samples for calls
  populateBoundarySamplesForAllFunctions(SC.BranchCounter);

  updateFunctionSamples();
}

void ProfileGenerator::generateProbeBasedProfile() {
  assert(SampleCounters->size() == 1 &&
         "Must have one entry for profile generation.");
  // Enable pseudo probe functionalities in SampleProf
  FunctionSamples::ProfileIsProbeBased = true;
  const SampleCounter &SC = SampleCounters->begin()->second;
  // Fill in function body samples
  populateBodySamplesWithProbesForAllFunctions(SC.RangeCounter);
  // Fill in boundary sample counts as well as call site samples for calls
  populateBoundarySamplesWithProbesForAllFunctions(SC.BranchCounter);

  updateFunctionSamples();
}

void ProfileGenerator::populateBodySamplesWithProbesForAllFunctions(
    const RangeSample &RangeCounter) {
  ProbeCounterMap ProbeCounter;
  // preprocessRangeCounter returns disjoint ranges, so no longer to redo it
  // inside extractProbesFromRange.
  extractProbesFromRange(preprocessRangeCounter(RangeCounter), ProbeCounter,
                         false);

  for (const auto &PI : ProbeCounter) {
    const MCDecodedPseudoProbe *Probe = PI.first;
    uint64_t Count = PI.second;
    SampleContextFrameVector FrameVec;
    Binary->getInlineContextForProbe(Probe, FrameVec, true);
    FunctionSamples &FunctionProfile =
        getLeafProfileAndAddTotalSamples(FrameVec, Count);
    FunctionProfile.addBodySamples(Probe->getIndex(), Probe->getDiscriminator(),
                                   Count);
    if (Probe->isEntry())
      FunctionProfile.addHeadSamples(Count);
  }
}

void ProfileGenerator::populateBoundarySamplesWithProbesForAllFunctions(
    const BranchSample &BranchCounters) {
  for (const auto &Entry : BranchCounters) {
    uint64_t SourceAddress = Entry.first.first;
    uint64_t TargetAddress = Entry.first.second;
    uint64_t Count = Entry.second;
    assert(Count != 0 && "Unexpected zero weight branch");

    StringRef CalleeName = getCalleeNameForAddress(TargetAddress);
    if (CalleeName.size() == 0)
      continue;

    const MCDecodedPseudoProbe *CallProbe =
        Binary->getCallProbeForAddr(SourceAddress);
    if (CallProbe == nullptr)
      continue;

    // Record called target sample and its count.
    SampleContextFrameVector FrameVec;
    Binary->getInlineContextForProbe(CallProbe, FrameVec, true);

    if (!FrameVec.empty()) {
      FunctionSamples &FunctionProfile =
          getLeafProfileAndAddTotalSamples(FrameVec, 0);
      FunctionProfile.addCalledTargetSamples(
          FrameVec.back().Location.LineOffset,
          FrameVec.back().Location.Discriminator,
          FunctionId(CalleeName), Count);
    }
  }
}

FunctionSamples &ProfileGenerator::getLeafProfileAndAddTotalSamples(
    const SampleContextFrameVector &FrameVec, uint64_t Count) {
  // Get top level profile
  FunctionSamples *FunctionProfile =
      &getTopLevelFunctionProfile(FrameVec[0].Func);
  FunctionProfile->addTotalSamples(Count);
  if (Binary->usePseudoProbes()) {
    const auto *FuncDesc = Binary->getFuncDescForGUID(
        FunctionProfile->getFunction().getHashCode());
    FunctionProfile->setFunctionHash(FuncDesc->FuncHash);
  }

  for (size_t I = 1; I < FrameVec.size(); I++) {
    LineLocation Callsite(
        FrameVec[I - 1].Location.LineOffset,
        getBaseDiscriminator(FrameVec[I - 1].Location.Discriminator));
    FunctionSamplesMap &SamplesMap =
        FunctionProfile->functionSamplesAt(Callsite);
    auto Ret =
        SamplesMap.emplace(FrameVec[I].Func, FunctionSamples());
    if (Ret.second) {
      SampleContext Context(FrameVec[I].Func);
      Ret.first->second.setContext(Context);
    }
    FunctionProfile = &Ret.first->second;
    FunctionProfile->addTotalSamples(Count);
    if (Binary->usePseudoProbes()) {
      const auto *FuncDesc = Binary->getFuncDescForGUID(
          FunctionProfile->getFunction().getHashCode());
      FunctionProfile->setFunctionHash(FuncDesc->FuncHash);
    }
  }

  return *FunctionProfile;
}

RangeSample
ProfileGenerator::preprocessRangeCounter(const RangeSample &RangeCounter) {
  RangeSample Ranges(RangeCounter.begin(), RangeCounter.end());
  if (FillZeroForAllFuncs) {
    for (auto &FuncI : Binary->getAllBinaryFunctions()) {
      for (auto &R : FuncI.second.Ranges) {
        Ranges[{R.first, R.second - 1}] += 0;
      }
    }
  } else {
    // For each range, we search for all ranges of the function it belongs to
    // and initialize it with zero count, so it remains zero if doesn't hit any
    // samples. This is to be consistent with compiler that interpret zero count
    // as unexecuted(cold).
    for (const auto &I : RangeCounter) {
      uint64_t StartAddress = I.first.first;
      for (const auto &Range : Binary->getRanges(StartAddress))
        Ranges[{Range.first, Range.second - 1}] += 0;
    }
  }
  RangeSample DisjointRanges;
  findDisjointRanges(DisjointRanges, Ranges);
  return DisjointRanges;
}

void ProfileGenerator::populateBodySamplesForAllFunctions(
    const RangeSample &RangeCounter) {
  for (const auto &Range : preprocessRangeCounter(RangeCounter)) {
    uint64_t RangeBegin = Range.first.first;
    uint64_t RangeEnd = Range.first.second;
    uint64_t Count = Range.second;

    InstructionPointer IP(Binary, RangeBegin, true);
    // Disjoint ranges may have range in the middle of two instr,
    // e.g. If Instr1 at Addr1, and Instr2 at Addr2, disjoint range
    // can be Addr1+1 to Addr2-1. We should ignore such range.
    if (IP.Address > RangeEnd)
      continue;

    do {
      const SampleContextFrameVector FrameVec =
          Binary->getFrameLocationStack(IP.Address);
      if (!FrameVec.empty()) {
        // FIXME: As accumulating total count per instruction caused some
        // regression, we changed to accumulate total count per byte as a
        // workaround. Tuning hotness threshold on the compiler side might be
        // necessary in the future.
        FunctionSamples &FunctionProfile = getLeafProfileAndAddTotalSamples(
            FrameVec, Count * Binary->getInstSize(IP.Address));
        updateBodySamplesforFunctionProfile(FunctionProfile, FrameVec.back(),
                                            Count);
      }
    } while (IP.advance() && IP.Address <= RangeEnd);
  }
}

StringRef
ProfileGeneratorBase::getCalleeNameForAddress(uint64_t TargetAddress) {
  // Get the function range by branch target if it's a call branch.
  auto *FRange = Binary->findFuncRangeForStartAddr(TargetAddress);

  // We won't accumulate sample count for a range whose start is not the real
  // function entry such as outlined function or inner labels.
  if (!FRange || !FRange->IsFuncEntry)
    return StringRef();

  return FunctionSamples::getCanonicalFnName(FRange->getFuncName());
}

void ProfileGenerator::populateBoundarySamplesForAllFunctions(
    const BranchSample &BranchCounters) {
  for (const auto &Entry : BranchCounters) {
    uint64_t SourceAddress = Entry.first.first;
    uint64_t TargetAddress = Entry.first.second;
    uint64_t Count = Entry.second;
    assert(Count != 0 && "Unexpected zero weight branch");

    StringRef CalleeName = getCalleeNameForAddress(TargetAddress);
    if (CalleeName.size() == 0)
      continue;
    // Record called target sample and its count.
    const SampleContextFrameVector &FrameVec =
        Binary->getCachedFrameLocationStack(SourceAddress);
    if (!FrameVec.empty()) {
      FunctionSamples &FunctionProfile =
          getLeafProfileAndAddTotalSamples(FrameVec, 0);
      FunctionProfile.addCalledTargetSamples(
          FrameVec.back().Location.LineOffset,
          getBaseDiscriminator(FrameVec.back().Location.Discriminator),
          FunctionId(CalleeName), Count);
    }
    // Add head samples for callee.
    FunctionSamples &CalleeProfile =
        getTopLevelFunctionProfile(FunctionId(CalleeName));
    CalleeProfile.addHeadSamples(Count);
  }
}

void ProfileGeneratorBase::calculateBodySamplesAndSize(
    const FunctionSamples &FSamples, uint64_t &TotalBodySamples,
    uint64_t &FuncBodySize) {
  // Note that ideally the size should be the number of function instruction.
  // However, for probe-based profile, we don't have the accurate instruction
  // count for each probe, instead, the probe sample is the samples count for
  // the block, which is equivelant to
  // total_instruction_samples/num_of_instruction in one block. Hence, we use
  // the number of probe as a proxy for the function's size.
  FuncBodySize += FSamples.getBodySamples().size();

  // The accumulated body samples re-calculated here could be different from the
  // TotalSamples(getTotalSamples) field of FunctionSamples for line-number
  // based profile. The reason is that TotalSamples is the sum of all the
  // samples of the machine instruction in one source-code line, however, the
  // entry of Bodysamples is the only max number of them, so the TotalSamples is
  // usually much bigger than the accumulated body samples as one souce-code
  // line can emit many machine instructions. We observed a regression when we
  // switched to use the accumulated body samples(by using
  // -update-total-samples). Hence, it's safer to re-calculate here to avoid
  // such discrepancy. There is no problem for probe-based profile, as the
  // TotalSamples is exactly the same as the accumulated body samples.
  for (const auto &I : FSamples.getBodySamples())
    TotalBodySamples += I.second.getSamples();

  for (const auto &CallsiteSamples : FSamples.getCallsiteSamples())
    for (const auto &Callee : CallsiteSamples.second) {
      // For binary-level density, the inlinees' samples and size should be
      // included in the calculation.
      calculateBodySamplesAndSize(Callee.second, TotalBodySamples,
                                  FuncBodySize);
    }
}

// Calculate Profile-density:
// Calculate the density for each function and sort them in descending order,
// keep accumulating their total samples unitl it exceeds the
// percentage_threshold(cut-off) of total profile samples, the profile-density
// is the last(minimum) function-density of the processed functions, which means
// all the functions hot to perf are on good density if the profile-density is
// good. The percentage_threshold(--profile-density-cutoff-hot) is configurable
// depending on how much regression the system want to tolerate.
double
ProfileGeneratorBase::calculateDensity(const SampleProfileMap &Profiles) {
  double ProfileDensity = 0.0;

  uint64_t TotalProfileSamples = 0;
  // A list of the function profile density and its total samples.
  std::vector<std::pair<double, uint64_t>> FuncDensityList;
  for (const auto &I : Profiles) {
    uint64_t TotalBodySamples = 0;
    uint64_t FuncBodySize = 0;
    calculateBodySamplesAndSize(I.second, TotalBodySamples, FuncBodySize);

    if (FuncBodySize == 0)
      continue;

    double FuncDensity = static_cast<double>(TotalBodySamples) / FuncBodySize;
    TotalProfileSamples += TotalBodySamples;
    FuncDensityList.emplace_back(FuncDensity, TotalBodySamples);
  }

  // Sorted by the density in descending order.
  llvm::stable_sort(FuncDensityList, [&](const std::pair<double, uint64_t> &A,
                                         const std::pair<double, uint64_t> &B) {
    if (A.first != B.first)
      return A.first > B.first;
    return A.second < B.second;
  });

  uint64_t AccumulatedSamples = 0;
  uint32_t I = 0;
  assert(ProfileDensityCutOffHot <= 1000000 &&
         "The cutoff value is greater than 1000000(100%)");
  while (AccumulatedSamples < TotalProfileSamples *
                                  static_cast<float>(ProfileDensityCutOffHot) /
                                  1000000 &&
         I < FuncDensityList.size()) {
    AccumulatedSamples += FuncDensityList[I].second;
    ProfileDensity = FuncDensityList[I].first;
    I++;
  }

  return ProfileDensity;
}

void ProfileGeneratorBase::calculateAndShowDensity(
    const SampleProfileMap &Profiles) {
  double Density = calculateDensity(Profiles);
  showDensitySuggestion(Density);
}

FunctionSamples *
CSProfileGenerator::getOrCreateFunctionSamples(ContextTrieNode *ContextNode,
                                               bool WasLeafInlined) {
  FunctionSamples *FProfile = ContextNode->getFunctionSamples();
  if (!FProfile) {
    FSamplesList.emplace_back();
    FProfile = &FSamplesList.back();
    FProfile->setFunction(ContextNode->getFuncName());
    ContextNode->setFunctionSamples(FProfile);
  }
  // Update ContextWasInlined attribute for existing contexts.
  // The current function can be called in two ways:
  //  - when processing a probe of the current frame
  //  - when processing the entry probe of an inlinee's frame, which
  //    is then used to update the callsite count of the current frame.
  // The two can happen in any order, hence here we are making sure
  // `ContextWasInlined` is always set as expected.
  // TODO: Note that the former does not always happen if no probes of the
  // current frame has samples, and if the latter happens, we could lose the
  // attribute. This should be fixed.
  if (WasLeafInlined)
    FProfile->getContext().setAttribute(ContextWasInlined);
  return FProfile;
}

ContextTrieNode *
CSProfileGenerator::getOrCreateContextNode(const SampleContextFrames Context,
                                           bool WasLeafInlined) {
  ContextTrieNode *ContextNode =
      ContextTracker.getOrCreateContextPath(Context, true);
  getOrCreateFunctionSamples(ContextNode, WasLeafInlined);
  return ContextNode;
}

void CSProfileGenerator::generateProfile() {
  FunctionSamples::ProfileIsCS = true;

  collectProfiledFunctions();

  if (Binary->usePseudoProbes()) {
    Binary->decodePseudoProbe();
    if (InferMissingFrames)
      initializeMissingFrameInferrer();
  }

  if (SampleCounters) {
    if (Binary->usePseudoProbes()) {
      generateProbeBasedProfile();
    } else {
      generateLineNumBasedProfile();
    }
  }

  if (Binary->getTrackFuncContextSize())
    computeSizeForProfiledFunctions();

  postProcessProfiles();
}

void CSProfileGenerator::initializeMissingFrameInferrer() {
  Binary->getMissingContextInferrer()->initialize(SampleCounters);
}

void CSProfileGenerator::inferMissingFrames(
    const SmallVectorImpl<uint64_t> &Context,
    SmallVectorImpl<uint64_t> &NewContext) {
  Binary->inferMissingFrames(Context, NewContext);
}

void CSProfileGenerator::computeSizeForProfiledFunctions() {
  for (auto *Func : Binary->getProfiledFunctions())
    Binary->computeInlinedContextSizeForFunc(Func);

  // Flush the symbolizer to save memory.
  Binary->flushSymbolizer();
}

void CSProfileGenerator::updateFunctionSamples() {
  for (auto *Node : ContextTracker) {
    FunctionSamples *FSamples = Node->getFunctionSamples();
    if (FSamples) {
      if (UpdateTotalSamples)
        FSamples->updateTotalSamples();
      FSamples->updateCallsiteSamples();
    }
  }
}

void CSProfileGenerator::generateLineNumBasedProfile() {
  for (const auto &CI : *SampleCounters) {
    const auto *CtxKey = cast<StringBasedCtxKey>(CI.first.getPtr());

    ContextTrieNode *ContextNode = &getRootContext();
    // Sample context will be empty if the jump is an external-to-internal call
    // pattern, the head samples should be added for the internal function.
    if (!CtxKey->Context.empty()) {
      // Get or create function profile for the range
      ContextNode =
          getOrCreateContextNode(CtxKey->Context, CtxKey->WasLeafInlined);
      // Fill in function body samples
      populateBodySamplesForFunction(*ContextNode->getFunctionSamples(),
                                     CI.second.RangeCounter);
    }
    // Fill in boundary sample counts as well as call site samples for calls
    populateBoundarySamplesForFunction(ContextNode, CI.second.BranchCounter);
  }
  // Fill in call site value sample for inlined calls and also use context to
  // infer missing samples. Since we don't have call count for inlined
  // functions, we estimate it from inlinee's profile using the entry of the
  // body sample.
  populateInferredFunctionSamples(getRootContext());

  updateFunctionSamples();
}

void CSProfileGenerator::populateBodySamplesForFunction(
    FunctionSamples &FunctionProfile, const RangeSample &RangeCounter) {
  // Compute disjoint ranges first, so we can use MAX
  // for calculating count for each location.
  RangeSample Ranges;
  findDisjointRanges(Ranges, RangeCounter);
  for (const auto &Range : Ranges) {
    uint64_t RangeBegin = Range.first.first;
    uint64_t RangeEnd = Range.first.second;
    uint64_t Count = Range.second;
    // Disjoint ranges have introduce zero-filled gap that
    // doesn't belong to current context, filter them out.
    if (Count == 0)
      continue;

    InstructionPointer IP(Binary, RangeBegin, true);
    // Disjoint ranges may have range in the middle of two instr,
    // e.g. If Instr1 at Addr1, and Instr2 at Addr2, disjoint range
    // can be Addr1+1 to Addr2-1. We should ignore such range.
    if (IP.Address > RangeEnd)
      continue;

    do {
      auto LeafLoc = Binary->getInlineLeafFrameLoc(IP.Address);
      if (LeafLoc) {
        // Recording body sample for this specific context
        updateBodySamplesforFunctionProfile(FunctionProfile, *LeafLoc, Count);
        FunctionProfile.addTotalSamples(Count);
      }
    } while (IP.advance() && IP.Address <= RangeEnd);
  }
}

void CSProfileGenerator::populateBoundarySamplesForFunction(
    ContextTrieNode *Node, const BranchSample &BranchCounters) {

  for (const auto &Entry : BranchCounters) {
    uint64_t SourceAddress = Entry.first.first;
    uint64_t TargetAddress = Entry.first.second;
    uint64_t Count = Entry.second;
    assert(Count != 0 && "Unexpected zero weight branch");

    StringRef CalleeName = getCalleeNameForAddress(TargetAddress);
    if (CalleeName.size() == 0)
      continue;

    ContextTrieNode *CallerNode = Node;
    LineLocation CalleeCallSite(0, 0);
    if (CallerNode != &getRootContext()) {
      // Record called target sample and its count
      auto LeafLoc = Binary->getInlineLeafFrameLoc(SourceAddress);
      if (LeafLoc) {
        CallerNode->getFunctionSamples()->addCalledTargetSamples(
            LeafLoc->Location.LineOffset,
            getBaseDiscriminator(LeafLoc->Location.Discriminator),
            FunctionId(CalleeName),
            Count);
        // Record head sample for called target(callee)
        CalleeCallSite = LeafLoc->Location;
      }
    }

    ContextTrieNode *CalleeNode =
        CallerNode->getOrCreateChildContext(CalleeCallSite,
                                            FunctionId(CalleeName));
    FunctionSamples *CalleeProfile = getOrCreateFunctionSamples(CalleeNode);
    CalleeProfile->addHeadSamples(Count);
  }
}

void CSProfileGenerator::populateInferredFunctionSamples(
    ContextTrieNode &Node) {
  // There is no call jmp sample between the inliner and inlinee, we need to use
  // the inlinee's context to infer inliner's context, i.e. parent(inliner)'s
  // sample depends on child(inlinee)'s sample, so traverse the tree in
  // post-order.
  for (auto &It : Node.getAllChildContext())
    populateInferredFunctionSamples(It.second);

  FunctionSamples *CalleeProfile = Node.getFunctionSamples();
  if (!CalleeProfile)
    return;
  // If we already have head sample counts, we must have value profile
  // for call sites added already. Skip to avoid double counting.
  if (CalleeProfile->getHeadSamples())
    return;
  ContextTrieNode *CallerNode = Node.getParentContext();
  // If we don't have context, nothing to do for caller's call site.
  // This could happen for entry point function.
  if (CallerNode == &getRootContext())
    return;

  LineLocation CallerLeafFrameLoc = Node.getCallSiteLoc();
  FunctionSamples &CallerProfile = *getOrCreateFunctionSamples(CallerNode);
  // Since we don't have call count for inlined functions, we
  // estimate it from inlinee's profile using entry body sample.
  uint64_t EstimatedCallCount = CalleeProfile->getHeadSamplesEstimate();
  // If we don't have samples with location, use 1 to indicate live.
  if (!EstimatedCallCount && !CalleeProfile->getBodySamples().size())
    EstimatedCallCount = 1;
  CallerProfile.addCalledTargetSamples(CallerLeafFrameLoc.LineOffset,
                                       CallerLeafFrameLoc.Discriminator,
                                       Node.getFuncName(), EstimatedCallCount);
  CallerProfile.addBodySamples(CallerLeafFrameLoc.LineOffset,
                               CallerLeafFrameLoc.Discriminator,
                               EstimatedCallCount);
  CallerProfile.addTotalSamples(EstimatedCallCount);
}

void CSProfileGenerator::convertToProfileMap(
    ContextTrieNode &Node, SampleContextFrameVector &Context) {
  FunctionSamples *FProfile = Node.getFunctionSamples();
  if (FProfile) {
    Context.emplace_back(Node.getFuncName(), LineLocation(0, 0));
    // Save the new context for future references.
    SampleContextFrames NewContext = *Contexts.insert(Context).first;
    auto Ret = ProfileMap.emplace(NewContext, std::move(*FProfile));
    FunctionSamples &NewProfile = Ret.first->second;
    NewProfile.getContext().setContext(NewContext);
    Context.pop_back();
  }

  for (auto &It : Node.getAllChildContext()) {
    ContextTrieNode &ChildNode = It.second;
    Context.emplace_back(Node.getFuncName(), ChildNode.getCallSiteLoc());
    convertToProfileMap(ChildNode, Context);
    Context.pop_back();
  }
}

void CSProfileGenerator::convertToProfileMap() {
  assert(ProfileMap.empty() &&
         "ProfileMap should be empty before converting from the trie");
  assert(IsProfileValidOnTrie &&
         "Do not convert the trie twice, it's already destroyed");

  SampleContextFrameVector Context;
  for (auto &It : getRootContext().getAllChildContext())
    convertToProfileMap(It.second, Context);

  IsProfileValidOnTrie = false;
}

void CSProfileGenerator::postProcessProfiles() {
  // Compute hot/cold threshold based on profile. This will be used for cold
  // context profile merging/trimming.
  computeSummaryAndThreshold();

  // Run global pre-inliner to adjust/merge context profile based on estimated
  // inline decisions.
  if (EnableCSPreInliner) {
    ContextTracker.populateFuncToCtxtMap();
    CSPreInliner(ContextTracker, *Binary, Summary.get()).run();
    // Turn off the profile merger by default unless it is explicitly enabled.
    if (!CSProfMergeColdContext.getNumOccurrences())
      CSProfMergeColdContext = false;
  }

  convertToProfileMap();

  // Trim and merge cold context profile using cold threshold above.
  if (TrimColdProfile || CSProfMergeColdContext) {
    SampleContextTrimmer(ProfileMap)
        .trimAndMergeColdContextProfiles(
            HotCountThreshold, TrimColdProfile, CSProfMergeColdContext,
            CSProfMaxColdContextDepth, EnableCSPreInliner);
  }

  if (GenCSNestedProfile) {
    ProfileConverter CSConverter(ProfileMap);
    CSConverter.convertCSProfiles();
    FunctionSamples::ProfileIsCS = false;
  }
  filterAmbiguousProfile(ProfileMap);
  ProfileGeneratorBase::calculateAndShowDensity(ProfileMap);
}

void ProfileGeneratorBase::computeSummaryAndThreshold(
    SampleProfileMap &Profiles) {
  SampleProfileSummaryBuilder Builder(ProfileSummaryBuilder::DefaultCutoffs);
  Summary = Builder.computeSummaryForProfiles(Profiles);
  HotCountThreshold = ProfileSummaryBuilder::getHotCountThreshold(
      (Summary->getDetailedSummary()));
  ColdCountThreshold = ProfileSummaryBuilder::getColdCountThreshold(
      (Summary->getDetailedSummary()));
}

void CSProfileGenerator::computeSummaryAndThreshold() {
  // Always merge and use context-less profile map to compute summary.
  SampleProfileMap ContextLessProfiles;
  ContextTracker.createContextLessProfileMap(ContextLessProfiles);

  // Set the flag below to avoid merging the profile again in
  // computeSummaryAndThreshold
  FunctionSamples::ProfileIsCS = false;
  assert(
      (!UseContextLessSummary.getNumOccurrences() || UseContextLessSummary) &&
      "Don't set --profile-summary-contextless to false for profile "
      "generation");
  ProfileGeneratorBase::computeSummaryAndThreshold(ContextLessProfiles);
  // Recover the old value.
  FunctionSamples::ProfileIsCS = true;
}

void ProfileGeneratorBase::extractProbesFromRange(
    const RangeSample &RangeCounter, ProbeCounterMap &ProbeCounter,
    bool FindDisjointRanges) {
  const RangeSample *PRanges = &RangeCounter;
  RangeSample Ranges;
  if (FindDisjointRanges) {
    findDisjointRanges(Ranges, RangeCounter);
    PRanges = &Ranges;
  }

  for (const auto &Range : *PRanges) {
    uint64_t RangeBegin = Range.first.first;
    uint64_t RangeEnd = Range.first.second;
    uint64_t Count = Range.second;

    InstructionPointer IP(Binary, RangeBegin, true);
    // Disjoint ranges may have range in the middle of two instr,
    // e.g. If Instr1 at Addr1, and Instr2 at Addr2, disjoint range
    // can be Addr1+1 to Addr2-1. We should ignore such range.
    if (IP.Address > RangeEnd)
      continue;

    do {
      const AddressProbesMap &Address2ProbesMap =
          Binary->getAddress2ProbesMap();
      auto It = Address2ProbesMap.find(IP.Address);
      if (It != Address2ProbesMap.end()) {
        for (const auto &Probe : It->second) {
          ProbeCounter[&Probe] += Count;
        }
      }
    } while (IP.advance() && IP.Address <= RangeEnd);
  }
}

static void extractPrefixContextStack(SampleContextFrameVector &ContextStack,
                                      const SmallVectorImpl<uint64_t> &AddrVec,
                                      ProfiledBinary *Binary) {
  SmallVector<const MCDecodedPseudoProbe *, 16> Probes;
  for (auto Address : reverse(AddrVec)) {
    const MCDecodedPseudoProbe *CallProbe =
        Binary->getCallProbeForAddr(Address);
    // These could be the cases when a probe is not found at a calliste. Cutting
    // off the context from here since the inliner will not know how to consume
    // a context with unknown callsites.
    // 1. for functions that are not sampled when
    // --decode-probe-for-profiled-functions-only is on.
    // 2. for a merged callsite. Callsite merging may cause the loss of original
    // probe IDs.
    // 3. for an external callsite.
    if (!CallProbe)
      break;
    Probes.push_back(CallProbe);
  }

  std::reverse(Probes.begin(), Probes.end());

  // Extract context stack for reusing, leaf context stack will be added
  // compressed while looking up function profile.
  for (const auto *P : Probes) {
    Binary->getInlineContextForProbe(P, ContextStack, true);
  }
}

void CSProfileGenerator::generateProbeBasedProfile() {
  // Enable pseudo probe functionalities in SampleProf
  FunctionSamples::ProfileIsProbeBased = true;
  for (const auto &CI : *SampleCounters) {
    const AddrBasedCtxKey *CtxKey =
        dyn_cast<AddrBasedCtxKey>(CI.first.getPtr());
    // Fill in function body samples from probes, also infer caller's samples
    // from callee's probe
    populateBodySamplesWithProbes(CI.second.RangeCounter, CtxKey);
    // Fill in boundary samples for a call probe
    populateBoundarySamplesWithProbes(CI.second.BranchCounter, CtxKey);
  }
}

void CSProfileGenerator::populateBodySamplesWithProbes(
    const RangeSample &RangeCounter, const AddrBasedCtxKey *CtxKey) {
  ProbeCounterMap ProbeCounter;
  // Extract the top frame probes by looking up each address among the range in
  // the Address2ProbeMap
  extractProbesFromRange(RangeCounter, ProbeCounter);
  std::unordered_map<MCDecodedPseudoProbeInlineTree *,
                     std::unordered_set<FunctionSamples *>>
      FrameSamples;
  for (const auto &PI : ProbeCounter) {
    const MCDecodedPseudoProbe *Probe = PI.first;
    uint64_t Count = PI.second;
    // Disjoint ranges have introduce zero-filled gap that
    // doesn't belong to current context, filter them out.
    if (!Probe->isBlock() || Count == 0)
      continue;

    ContextTrieNode *ContextNode = getContextNodeForLeafProbe(CtxKey, Probe);
    FunctionSamples &FunctionProfile = *ContextNode->getFunctionSamples();
    // Record the current frame and FunctionProfile whenever samples are
    // collected for non-danglie probes. This is for reporting all of the
    // zero count probes of the frame later.
    FrameSamples[Probe->getInlineTreeNode()].insert(&FunctionProfile);
    FunctionProfile.addBodySamples(Probe->getIndex(), Probe->getDiscriminator(),
                                   Count);
    FunctionProfile.addTotalSamples(Count);
    if (Probe->isEntry()) {
      FunctionProfile.addHeadSamples(Count);
      // Look up for the caller's function profile
      const auto *InlinerDesc = Binary->getInlinerDescForProbe(Probe);
      ContextTrieNode *CallerNode = ContextNode->getParentContext();
      if (InlinerDesc != nullptr && CallerNode != &getRootContext()) {
        // Since the context id will be compressed, we have to use callee's
        // context id to infer caller's context id to ensure they share the
        // same context prefix.
        uint64_t CallerIndex = ContextNode->getCallSiteLoc().LineOffset;
        uint64_t CallerDiscriminator = ContextNode->getCallSiteLoc().Discriminator;
        assert(CallerIndex &&
               "Inferred caller's location index shouldn't be zero!");
        assert(!CallerDiscriminator &&
               "Callsite probe should not have a discriminator!");
        FunctionSamples &CallerProfile =
            *getOrCreateFunctionSamples(CallerNode);
        CallerProfile.setFunctionHash(InlinerDesc->FuncHash);
        CallerProfile.addBodySamples(CallerIndex, CallerDiscriminator, Count);
        CallerProfile.addTotalSamples(Count);
        CallerProfile.addCalledTargetSamples(CallerIndex, CallerDiscriminator,
                                             ContextNode->getFuncName(), Count);
      }
    }
  }

  // Assign zero count for remaining probes without sample hits to
  // differentiate from probes optimized away, of which the counts are unknown
  // and will be inferred by the compiler.
  for (auto &I : FrameSamples) {
    for (auto *FunctionProfile : I.second) {
      for (auto *Probe : I.first->getProbes()) {
        FunctionProfile->addBodySamples(Probe->getIndex(),
                                        Probe->getDiscriminator(), 0);
      }
    }
  }
}

void CSProfileGenerator::populateBoundarySamplesWithProbes(
    const BranchSample &BranchCounter, const AddrBasedCtxKey *CtxKey) {
  for (const auto &BI : BranchCounter) {
    uint64_t SourceAddress = BI.first.first;
    uint64_t TargetAddress = BI.first.second;
    uint64_t Count = BI.second;
    const MCDecodedPseudoProbe *CallProbe =
        Binary->getCallProbeForAddr(SourceAddress);
    if (CallProbe == nullptr)
      continue;
    FunctionSamples &FunctionProfile =
        getFunctionProfileForLeafProbe(CtxKey, CallProbe);
    FunctionProfile.addBodySamples(CallProbe->getIndex(), 0, Count);
    FunctionProfile.addTotalSamples(Count);
    StringRef CalleeName = getCalleeNameForAddress(TargetAddress);
    if (CalleeName.size() == 0)
      continue;
    FunctionProfile.addCalledTargetSamples(CallProbe->getIndex(),
                                           CallProbe->getDiscriminator(),
                                           FunctionId(CalleeName), Count);
  }
}

ContextTrieNode *CSProfileGenerator::getContextNodeForLeafProbe(
    const AddrBasedCtxKey *CtxKey, const MCDecodedPseudoProbe *LeafProbe) {

  const SmallVectorImpl<uint64_t> *PContext = &CtxKey->Context;
  SmallVector<uint64_t, 16> NewContext;

  if (InferMissingFrames) {
    SmallVector<uint64_t, 16> Context = CtxKey->Context;
    // Append leaf frame for a complete inference.
    Context.push_back(LeafProbe->getAddress());
    inferMissingFrames(Context, NewContext);
    // Pop out the leaf probe that was pushed in above.
    NewContext.pop_back();
    PContext = &NewContext;
  }

  SampleContextFrameVector ContextStack;
  extractPrefixContextStack(ContextStack, *PContext, Binary);

  // Explicitly copy the context for appending the leaf context
  SampleContextFrameVector NewContextStack(ContextStack.begin(),
                                           ContextStack.end());
  Binary->getInlineContextForProbe(LeafProbe, NewContextStack, true);
  // For leaf inlined context with the top frame, we should strip off the top
  // frame's probe id, like:
  // Inlined stack: [foo:1, bar:2], the ContextId will be "foo:1 @ bar"
  auto LeafFrame = NewContextStack.back();
  LeafFrame.Location = LineLocation(0, 0);
  NewContextStack.pop_back();
  // Compress the context string except for the leaf frame
  CSProfileGenerator::compressRecursionContext(NewContextStack);
  CSProfileGenerator::trimContext(NewContextStack);
  NewContextStack.push_back(LeafFrame);

  const auto *FuncDesc = Binary->getFuncDescForGUID(LeafProbe->getGuid());
  bool WasLeafInlined = LeafProbe->getInlineTreeNode()->hasInlineSite();
  ContextTrieNode *ContextNode =
      getOrCreateContextNode(NewContextStack, WasLeafInlined);
  ContextNode->getFunctionSamples()->setFunctionHash(FuncDesc->FuncHash);
  return ContextNode;
}

FunctionSamples &CSProfileGenerator::getFunctionProfileForLeafProbe(
    const AddrBasedCtxKey *CtxKey, const MCDecodedPseudoProbe *LeafProbe) {
  return *getContextNodeForLeafProbe(CtxKey, LeafProbe)->getFunctionSamples();
}

} // end namespace sampleprof
} // end namespace llvm
