//=-- SampleProf.cpp - Sample profiling format support --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains common definitions used in the reading and writing of
// sample profile data.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/PseudoProbe.h"
#include "llvm/ProfileData/SampleProfReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <system_error>

using namespace llvm;
using namespace sampleprof;

static cl::opt<uint64_t> ProfileSymbolListCutOff(
    "profile-symbol-list-cutoff", cl::Hidden, cl::init(-1),
    cl::desc("Cutoff value about how many symbols in profile symbol list "
             "will be used. This is very useful for performance debugging"));

static cl::opt<bool> GenerateMergedBaseProfiles(
    "generate-merged-base-profiles",
    cl::desc("When generating nested context-sensitive profiles, always "
             "generate extra base profile for function with all its context "
             "profiles merged into it."));

namespace llvm {
namespace sampleprof {
bool FunctionSamples::ProfileIsProbeBased = false;
bool FunctionSamples::ProfileIsCS = false;
bool FunctionSamples::ProfileIsPreInlined = false;
bool FunctionSamples::UseMD5 = false;
bool FunctionSamples::HasUniqSuffix = true;
bool FunctionSamples::ProfileIsFS = false;
} // namespace sampleprof
} // namespace llvm

namespace {

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class SampleProfErrorCategoryType : public std::error_category {
  const char *name() const noexcept override { return "llvm.sampleprof"; }

  std::string message(int IE) const override {
    sampleprof_error E = static_cast<sampleprof_error>(IE);
    switch (E) {
    case sampleprof_error::success:
      return "Success";
    case sampleprof_error::bad_magic:
      return "Invalid sample profile data (bad magic)";
    case sampleprof_error::unsupported_version:
      return "Unsupported sample profile format version";
    case sampleprof_error::too_large:
      return "Too much profile data";
    case sampleprof_error::truncated:
      return "Truncated profile data";
    case sampleprof_error::malformed:
      return "Malformed sample profile data";
    case sampleprof_error::unrecognized_format:
      return "Unrecognized sample profile encoding format";
    case sampleprof_error::unsupported_writing_format:
      return "Profile encoding format unsupported for writing operations";
    case sampleprof_error::truncated_name_table:
      return "Truncated function name table";
    case sampleprof_error::not_implemented:
      return "Unimplemented feature";
    case sampleprof_error::counter_overflow:
      return "Counter overflow";
    case sampleprof_error::ostream_seek_unsupported:
      return "Ostream does not support seek";
    case sampleprof_error::uncompress_failed:
      return "Uncompress failure";
    case sampleprof_error::zlib_unavailable:
      return "Zlib is unavailable";
    case sampleprof_error::hash_mismatch:
      return "Function hash mismatch";
    }
    llvm_unreachable("A value of sampleprof_error has no message.");
  }
};

} // end anonymous namespace

const std::error_category &llvm::sampleprof_category() {
  static SampleProfErrorCategoryType ErrorCategory;
  return ErrorCategory;
}

void LineLocation::print(raw_ostream &OS) const {
  OS << LineOffset;
  if (Discriminator > 0)
    OS << "." << Discriminator;
}

raw_ostream &llvm::sampleprof::operator<<(raw_ostream &OS,
                                          const LineLocation &Loc) {
  Loc.print(OS);
  return OS;
}

/// Merge the samples in \p Other into this record.
/// Optionally scale sample counts by \p Weight.
sampleprof_error SampleRecord::merge(const SampleRecord &Other,
                                     uint64_t Weight) {
  sampleprof_error Result;
  Result = addSamples(Other.getSamples(), Weight);
  for (const auto &I : Other.getCallTargets()) {
    mergeSampleProfErrors(Result, addCalledTarget(I.first, I.second, Weight));
  }
  return Result;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void LineLocation::dump() const { print(dbgs()); }
#endif

/// Print the sample record to the stream \p OS indented by \p Indent.
void SampleRecord::print(raw_ostream &OS, unsigned Indent) const {
  OS << NumSamples;
  if (hasCalls()) {
    OS << ", calls:";
    for (const auto &I : getSortedCallTargets())
      OS << " " << I.first << ":" << I.second;
  }
  OS << "\n";
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SampleRecord::dump() const { print(dbgs(), 0); }
#endif

raw_ostream &llvm::sampleprof::operator<<(raw_ostream &OS,
                                          const SampleRecord &Sample) {
  Sample.print(OS, 0);
  return OS;
}

/// Print the samples collected for a function on stream \p OS.
void FunctionSamples::print(raw_ostream &OS, unsigned Indent) const {
  if (getFunctionHash())
    OS << "CFG checksum " << getFunctionHash() << "\n";

  OS << TotalSamples << ", " << TotalHeadSamples << ", " << BodySamples.size()
     << " sampled lines\n";

  OS.indent(Indent);
  if (!BodySamples.empty()) {
    OS << "Samples collected in the function's body {\n";
    SampleSorter<LineLocation, SampleRecord> SortedBodySamples(BodySamples);
    for (const auto &SI : SortedBodySamples.get()) {
      OS.indent(Indent + 2);
      OS << SI->first << ": " << SI->second;
    }
    OS.indent(Indent);
    OS << "}\n";
  } else {
    OS << "No samples collected in the function's body\n";
  }

  OS.indent(Indent);
  if (!CallsiteSamples.empty()) {
    OS << "Samples collected in inlined callsites {\n";
    SampleSorter<LineLocation, FunctionSamplesMap> SortedCallsiteSamples(
        CallsiteSamples);
    for (const auto &CS : SortedCallsiteSamples.get()) {
      for (const auto &FS : CS->second) {
        OS.indent(Indent + 2);
        OS << CS->first << ": inlined callee: " << FS.second.getFunction()
           << ": ";
        FS.second.print(OS, Indent + 4);
      }
    }
    OS.indent(Indent);
    OS << "}\n";
  } else {
    OS << "No inlined callsites in this function\n";
  }
}

raw_ostream &llvm::sampleprof::operator<<(raw_ostream &OS,
                                          const FunctionSamples &FS) {
  FS.print(OS);
  return OS;
}

void sampleprof::sortFuncProfiles(
    const SampleProfileMap &ProfileMap,
    std::vector<NameFunctionSamples> &SortedProfiles) {
  for (const auto &I : ProfileMap) {
    SortedProfiles.push_back(std::make_pair(I.first, &I.second));
  }
  llvm::stable_sort(SortedProfiles, [](const NameFunctionSamples &A,
                                       const NameFunctionSamples &B) {
    if (A.second->getTotalSamples() == B.second->getTotalSamples())
      return A.second->getContext() < B.second->getContext();
    return A.second->getTotalSamples() > B.second->getTotalSamples();
  });
}

unsigned FunctionSamples::getOffset(const DILocation *DIL) {
  return (DIL->getLine() - DIL->getScope()->getSubprogram()->getLine()) &
      0xffff;
}

LineLocation FunctionSamples::getCallSiteIdentifier(const DILocation *DIL,
                                                    bool ProfileIsFS) {
  if (FunctionSamples::ProfileIsProbeBased) {
    // In a pseudo-probe based profile, a callsite is simply represented by the
    // ID of the probe associated with the call instruction. The probe ID is
    // encoded in the Discriminator field of the call instruction's debug
    // metadata.
    return LineLocation(PseudoProbeDwarfDiscriminator::extractProbeIndex(
                            DIL->getDiscriminator()),
                        0);
  } else {
    unsigned Discriminator =
        ProfileIsFS ? DIL->getDiscriminator() : DIL->getBaseDiscriminator();
    return LineLocation(FunctionSamples::getOffset(DIL), Discriminator);
  }
}

const FunctionSamples *FunctionSamples::findFunctionSamples(
    const DILocation *DIL, SampleProfileReaderItaniumRemapper *Remapper,
    const HashKeyMap<std::unordered_map, FunctionId, FunctionId>
        *FuncNameToProfNameMap) const {
  assert(DIL);
  SmallVector<std::pair<LineLocation, StringRef>, 10> S;

  const DILocation *PrevDIL = DIL;
  for (DIL = DIL->getInlinedAt(); DIL; DIL = DIL->getInlinedAt()) {
    // Use C++ linkage name if possible.
    StringRef Name = PrevDIL->getScope()->getSubprogram()->getLinkageName();
    if (Name.empty())
      Name = PrevDIL->getScope()->getSubprogram()->getName();
    S.emplace_back(FunctionSamples::getCallSiteIdentifier(
                       DIL, FunctionSamples::ProfileIsFS),
                   Name);
    PrevDIL = DIL;
  }

  if (S.size() == 0)
    return this;
  const FunctionSamples *FS = this;
  for (int i = S.size() - 1; i >= 0 && FS != nullptr; i--) {
    FS = FS->findFunctionSamplesAt(S[i].first, S[i].second, Remapper,
                                   FuncNameToProfNameMap);
  }
  return FS;
}

void FunctionSamples::findAllNames(DenseSet<FunctionId> &NameSet) const {
  NameSet.insert(getFunction());
  for (const auto &BS : BodySamples)
    for (const auto &TS : BS.second.getCallTargets())
      NameSet.insert(TS.first);

  for (const auto &CS : CallsiteSamples) {
    for (const auto &NameFS : CS.second) {
      NameSet.insert(NameFS.first);
      NameFS.second.findAllNames(NameSet);
    }
  }
}

const FunctionSamples *FunctionSamples::findFunctionSamplesAt(
    const LineLocation &Loc, StringRef CalleeName,
    SampleProfileReaderItaniumRemapper *Remapper,
    const HashKeyMap<std::unordered_map, FunctionId, FunctionId>
        *FuncNameToProfNameMap) const {
  CalleeName = getCanonicalFnName(CalleeName);

  auto I = CallsiteSamples.find(mapIRLocToProfileLoc(Loc));
  if (I == CallsiteSamples.end())
    return nullptr;
  auto FS = I->second.find(getRepInFormat(CalleeName));
  if (FS != I->second.end())
    return &FS->second;

  if (FuncNameToProfNameMap && !FuncNameToProfNameMap->empty()) {
    auto R = FuncNameToProfNameMap->find(FunctionId(CalleeName));
    if (R != FuncNameToProfNameMap->end()) {
      CalleeName = R->second.stringRef();
      auto FS = I->second.find(getRepInFormat(CalleeName));
      if (FS != I->second.end())
        return &FS->second;
    }
  }

  if (Remapper) {
    if (auto NameInProfile = Remapper->lookUpNameInProfile(CalleeName)) {
      auto FS = I->second.find(getRepInFormat(*NameInProfile));
      if (FS != I->second.end())
        return &FS->second;
    }
  }
  // If we cannot find exact match of the callee name, return the FS with
  // the max total count. Only do this when CalleeName is not provided,
  // i.e., only for indirect calls.
  if (!CalleeName.empty())
    return nullptr;
  uint64_t MaxTotalSamples = 0;
  const FunctionSamples *R = nullptr;
  for (const auto &NameFS : I->second)
    if (NameFS.second.getTotalSamples() >= MaxTotalSamples) {
      MaxTotalSamples = NameFS.second.getTotalSamples();
      R = &NameFS.second;
    }
  return R;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void FunctionSamples::dump() const { print(dbgs(), 0); }
#endif

std::error_code ProfileSymbolList::read(const uint8_t *Data,
                                        uint64_t ListSize) {
  const char *ListStart = reinterpret_cast<const char *>(Data);
  uint64_t Size = 0;
  uint64_t StrNum = 0;
  while (Size < ListSize && StrNum < ProfileSymbolListCutOff) {
    StringRef Str(ListStart + Size);
    add(Str);
    Size += Str.size() + 1;
    StrNum++;
  }
  if (Size != ListSize && StrNum != ProfileSymbolListCutOff)
    return sampleprof_error::malformed;
  return sampleprof_error::success;
}

void SampleContextTrimmer::trimAndMergeColdContextProfiles(
    uint64_t ColdCountThreshold, bool TrimColdContext, bool MergeColdContext,
    uint32_t ColdContextFrameLength, bool TrimBaseProfileOnly) {
  if (!TrimColdContext && !MergeColdContext)
    return;

  // Nothing to merge if sample threshold is zero
  if (ColdCountThreshold == 0)
    return;

  // Trimming base profiles only is mainly to honor the preinliner decsion. When
  // MergeColdContext is true preinliner decsion is not honored anyway so turn
  // off TrimBaseProfileOnly.
  if (MergeColdContext)
    TrimBaseProfileOnly = false;

  // Filter the cold profiles from ProfileMap and move them into a tmp
  // container
  std::vector<std::pair<hash_code, const FunctionSamples *>> ColdProfiles;
  for (const auto &I : ProfileMap) {
    const SampleContext &Context = I.second.getContext();
    const FunctionSamples &FunctionProfile = I.second;
    if (FunctionProfile.getTotalSamples() < ColdCountThreshold &&
        (!TrimBaseProfileOnly || Context.isBaseContext()))
      ColdProfiles.emplace_back(I.first, &I.second);
  }

  // Remove the cold profile from ProfileMap and merge them into
  // MergedProfileMap by the last K frames of context
  SampleProfileMap MergedProfileMap;
  for (const auto &I : ColdProfiles) {
    if (MergeColdContext) {
      auto MergedContext = I.second->getContext().getContextFrames();
      if (ColdContextFrameLength < MergedContext.size())
        MergedContext = MergedContext.take_back(ColdContextFrameLength);
      // Need to set MergedProfile's context here otherwise it will be lost.
      FunctionSamples &MergedProfile = MergedProfileMap.create(MergedContext);
      MergedProfile.merge(*I.second);
    }
    ProfileMap.erase(I.first);
  }

  // Move the merged profiles into ProfileMap;
  for (const auto &I : MergedProfileMap) {
    // Filter the cold merged profile
    if (TrimColdContext && I.second.getTotalSamples() < ColdCountThreshold &&
        ProfileMap.find(I.second.getContext()) == ProfileMap.end())
      continue;
    // Merge the profile if the original profile exists, otherwise just insert
    // as a new profile. If inserted as a new profile from MergedProfileMap, it
    // already has the right context.
    auto Ret = ProfileMap.emplace(I.second.getContext(), FunctionSamples());
    FunctionSamples &OrigProfile = Ret.first->second;
    OrigProfile.merge(I.second);
  }
}

std::error_code ProfileSymbolList::write(raw_ostream &OS) {
  // Sort the symbols before output. If doing compression.
  // It will make the compression much more effective.
  std::vector<StringRef> SortedList(Syms.begin(), Syms.end());
  llvm::sort(SortedList);

  std::string OutputString;
  for (auto &Sym : SortedList) {
    OutputString.append(Sym.str());
    OutputString.append(1, '\0');
  }

  OS << OutputString;
  return sampleprof_error::success;
}

void ProfileSymbolList::dump(raw_ostream &OS) const {
  OS << "======== Dump profile symbol list ========\n";
  std::vector<StringRef> SortedList(Syms.begin(), Syms.end());
  llvm::sort(SortedList);

  for (auto &Sym : SortedList)
    OS << Sym << "\n";
}

ProfileConverter::FrameNode *
ProfileConverter::FrameNode::getOrCreateChildFrame(const LineLocation &CallSite,
                                                   FunctionId CalleeName) {
  uint64_t Hash = FunctionSamples::getCallSiteHash(CalleeName, CallSite);
  auto It = AllChildFrames.find(Hash);
  if (It != AllChildFrames.end()) {
    assert(It->second.FuncName == CalleeName &&
           "Hash collision for child context node");
    return &It->second;
  }

  AllChildFrames[Hash] = FrameNode(CalleeName, nullptr, CallSite);
  return &AllChildFrames[Hash];
}

ProfileConverter::ProfileConverter(SampleProfileMap &Profiles)
    : ProfileMap(Profiles) {
  for (auto &FuncSample : Profiles) {
    FunctionSamples *FSamples = &FuncSample.second;
    auto *NewNode = getOrCreateContextPath(FSamples->getContext());
    assert(!NewNode->FuncSamples && "New node cannot have sample profile");
    NewNode->FuncSamples = FSamples;
  }
}

ProfileConverter::FrameNode *
ProfileConverter::getOrCreateContextPath(const SampleContext &Context) {
  auto Node = &RootFrame;
  LineLocation CallSiteLoc(0, 0);
  for (auto &Callsite : Context.getContextFrames()) {
    Node = Node->getOrCreateChildFrame(CallSiteLoc, Callsite.Func);
    CallSiteLoc = Callsite.Location;
  }
  return Node;
}

void ProfileConverter::convertCSProfiles(ProfileConverter::FrameNode &Node) {
  // Process each child profile. Add each child profile to callsite profile map
  // of the current node `Node` if `Node` comes with a profile. Otherwise
  // promote the child profile to a standalone profile.
  auto *NodeProfile = Node.FuncSamples;
  for (auto &It : Node.AllChildFrames) {
    auto &ChildNode = It.second;
    convertCSProfiles(ChildNode);
    auto *ChildProfile = ChildNode.FuncSamples;
    if (!ChildProfile)
      continue;
    SampleContext OrigChildContext = ChildProfile->getContext();
    uint64_t OrigChildContextHash = OrigChildContext.getHashCode();
    // Reset the child context to be contextless.
    ChildProfile->getContext().setFunction(OrigChildContext.getFunction());
    if (NodeProfile) {
      // Add child profile to the callsite profile map.
      auto &SamplesMap = NodeProfile->functionSamplesAt(ChildNode.CallSiteLoc);
      SamplesMap.emplace(OrigChildContext.getFunction(), *ChildProfile);
      NodeProfile->addTotalSamples(ChildProfile->getTotalSamples());
      // Remove the corresponding body sample for the callsite and update the
      // total weight.
      auto Count = NodeProfile->removeCalledTargetAndBodySample(
          ChildNode.CallSiteLoc.LineOffset, ChildNode.CallSiteLoc.Discriminator,
          OrigChildContext.getFunction());
      NodeProfile->removeTotalSamples(Count);
    }

    uint64_t NewChildProfileHash = 0;
    // Separate child profile to be a standalone profile, if the current parent
    // profile doesn't exist. This is a duplicating operation when the child
    // profile is already incorporated into the parent which is still useful and
    // thus done optionally. It is seen that duplicating context profiles into
    // base profiles improves the code quality for thinlto build by allowing a
    // profile in the prelink phase for to-be-fully-inlined functions.
    if (!NodeProfile) {
      ProfileMap[ChildProfile->getContext()].merge(*ChildProfile);
      NewChildProfileHash = ChildProfile->getContext().getHashCode();
    } else if (GenerateMergedBaseProfiles) {
      ProfileMap[ChildProfile->getContext()].merge(*ChildProfile);
      NewChildProfileHash = ChildProfile->getContext().getHashCode();
      auto &SamplesMap = NodeProfile->functionSamplesAt(ChildNode.CallSiteLoc);
      SamplesMap[ChildProfile->getFunction()].getContext().setAttribute(
          ContextDuplicatedIntoBase);
    }

    // Remove the original child profile. Check if MD5 of new child profile
    // collides with old profile, in this case the [] operator already
    // overwritten it without the need of erase.
    if (NewChildProfileHash != OrigChildContextHash)
      ProfileMap.erase(OrigChildContextHash);
  }
}

void ProfileConverter::convertCSProfiles() { convertCSProfiles(RootFrame); }
