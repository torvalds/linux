//===-- ModuleSummaryIndex.cpp - Module Summary Index ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the module index and summary classes for the
// IR library.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "module-summary-index"

STATISTIC(ReadOnlyLiveGVars,
          "Number of live global variables marked read only");
STATISTIC(WriteOnlyLiveGVars,
          "Number of live global variables marked write only");

static cl::opt<bool> PropagateAttrs("propagate-attrs", cl::init(true),
                                    cl::Hidden,
                                    cl::desc("Propagate attributes in index"));

static cl::opt<bool> ImportConstantsWithRefs(
    "import-constants-with-refs", cl::init(true), cl::Hidden,
    cl::desc("Import constant global variables with references"));

constexpr uint32_t FunctionSummary::ParamAccess::RangeWidth;

FunctionSummary FunctionSummary::ExternalNode =
    FunctionSummary::makeDummyFunctionSummary({});

GlobalValue::VisibilityTypes ValueInfo::getELFVisibility() const {
  bool HasProtected = false;
  for (const auto &S : make_pointee_range(getSummaryList())) {
    if (S.getVisibility() == GlobalValue::HiddenVisibility)
      return GlobalValue::HiddenVisibility;
    if (S.getVisibility() == GlobalValue::ProtectedVisibility)
      HasProtected = true;
  }
  return HasProtected ? GlobalValue::ProtectedVisibility
                      : GlobalValue::DefaultVisibility;
}

bool ValueInfo::isDSOLocal(bool WithDSOLocalPropagation) const {
  // With DSOLocal propagation done, the flag in evey summary is the same.
  // Check the first one is enough.
  return WithDSOLocalPropagation
             ? getSummaryList().size() && getSummaryList()[0]->isDSOLocal()
             : getSummaryList().size() &&
                   llvm::all_of(
                       getSummaryList(),
                       [](const std::unique_ptr<GlobalValueSummary> &Summary) {
                         return Summary->isDSOLocal();
                       });
}

bool ValueInfo::canAutoHide() const {
  // Can only auto hide if all copies are eligible to auto hide.
  return getSummaryList().size() &&
         llvm::all_of(getSummaryList(),
                      [](const std::unique_ptr<GlobalValueSummary> &Summary) {
                        return Summary->canAutoHide();
                      });
}

// Gets the number of readonly and writeonly refs in RefEdgeList
std::pair<unsigned, unsigned> FunctionSummary::specialRefCounts() const {
  // Here we take advantage of having all readonly and writeonly references
  // located in the end of the RefEdgeList.
  auto Refs = refs();
  unsigned RORefCnt = 0, WORefCnt = 0;
  int I;
  for (I = Refs.size() - 1; I >= 0 && Refs[I].isWriteOnly(); --I)
    WORefCnt++;
  for (; I >= 0 && Refs[I].isReadOnly(); --I)
    RORefCnt++;
  return {RORefCnt, WORefCnt};
}

constexpr uint64_t ModuleSummaryIndex::BitcodeSummaryVersion;

uint64_t ModuleSummaryIndex::getFlags() const {
  uint64_t Flags = 0;
  if (withGlobalValueDeadStripping())
    Flags |= 0x1;
  if (skipModuleByDistributedBackend())
    Flags |= 0x2;
  if (hasSyntheticEntryCounts())
    Flags |= 0x4;
  if (enableSplitLTOUnit())
    Flags |= 0x8;
  if (partiallySplitLTOUnits())
    Flags |= 0x10;
  if (withAttributePropagation())
    Flags |= 0x20;
  if (withDSOLocalPropagation())
    Flags |= 0x40;
  if (withWholeProgramVisibility())
    Flags |= 0x80;
  if (withSupportsHotColdNew())
    Flags |= 0x100;
  if (hasUnifiedLTO())
    Flags |= 0x200;
  return Flags;
}

void ModuleSummaryIndex::setFlags(uint64_t Flags) {
  assert(Flags <= 0x2ff && "Unexpected bits in flag");
  // 1 bit: WithGlobalValueDeadStripping flag.
  // Set on combined index only.
  if (Flags & 0x1)
    setWithGlobalValueDeadStripping();
  // 1 bit: SkipModuleByDistributedBackend flag.
  // Set on combined index only.
  if (Flags & 0x2)
    setSkipModuleByDistributedBackend();
  // 1 bit: HasSyntheticEntryCounts flag.
  // Set on combined index only.
  if (Flags & 0x4)
    setHasSyntheticEntryCounts();
  // 1 bit: DisableSplitLTOUnit flag.
  // Set on per module indexes. It is up to the client to validate
  // the consistency of this flag across modules being linked.
  if (Flags & 0x8)
    setEnableSplitLTOUnit();
  // 1 bit: PartiallySplitLTOUnits flag.
  // Set on combined index only.
  if (Flags & 0x10)
    setPartiallySplitLTOUnits();
  // 1 bit: WithAttributePropagation flag.
  // Set on combined index only.
  if (Flags & 0x20)
    setWithAttributePropagation();
  // 1 bit: WithDSOLocalPropagation flag.
  // Set on combined index only.
  if (Flags & 0x40)
    setWithDSOLocalPropagation();
  // 1 bit: WithWholeProgramVisibility flag.
  // Set on combined index only.
  if (Flags & 0x80)
    setWithWholeProgramVisibility();
  // 1 bit: WithSupportsHotColdNew flag.
  // Set on combined index only.
  if (Flags & 0x100)
    setWithSupportsHotColdNew();
  // 1 bit: WithUnifiedLTO flag.
  // Set on combined index only.
  if (Flags & 0x200)
    setUnifiedLTO();
}

// Collect for the given module the list of function it defines
// (GUID -> Summary).
void ModuleSummaryIndex::collectDefinedFunctionsForModule(
    StringRef ModulePath, GVSummaryMapTy &GVSummaryMap) const {
  for (auto &GlobalList : *this) {
    auto GUID = GlobalList.first;
    for (auto &GlobSummary : GlobalList.second.SummaryList) {
      auto *Summary = dyn_cast_or_null<FunctionSummary>(GlobSummary.get());
      if (!Summary)
        // Ignore global variable, focus on functions
        continue;
      // Ignore summaries from other modules.
      if (Summary->modulePath() != ModulePath)
        continue;
      GVSummaryMap[GUID] = Summary;
    }
  }
}

GlobalValueSummary *
ModuleSummaryIndex::getGlobalValueSummary(uint64_t ValueGUID,
                                          bool PerModuleIndex) const {
  auto VI = getValueInfo(ValueGUID);
  assert(VI && "GlobalValue not found in index");
  assert((!PerModuleIndex || VI.getSummaryList().size() == 1) &&
         "Expected a single entry per global value in per-module index");
  auto &Summary = VI.getSummaryList()[0];
  return Summary.get();
}

bool ModuleSummaryIndex::isGUIDLive(GlobalValue::GUID GUID) const {
  auto VI = getValueInfo(GUID);
  if (!VI)
    return true;
  const auto &SummaryList = VI.getSummaryList();
  if (SummaryList.empty())
    return true;
  for (auto &I : SummaryList)
    if (isGlobalValueLive(I.get()))
      return true;
  return false;
}

static void
propagateAttributesToRefs(GlobalValueSummary *S,
                          DenseSet<ValueInfo> &MarkedNonReadWriteOnly) {
  // If reference is not readonly or writeonly then referenced summary is not
  // read/writeonly either. Note that:
  // - All references from GlobalVarSummary are conservatively considered as
  //   not readonly or writeonly. Tracking them properly requires more complex
  //   analysis then we have now.
  //
  // - AliasSummary objects have no refs at all so this function is a no-op
  //   for them.
  for (auto &VI : S->refs()) {
    assert(VI.getAccessSpecifier() == 0 || isa<FunctionSummary>(S));
    if (!VI.getAccessSpecifier()) {
      if (!MarkedNonReadWriteOnly.insert(VI).second)
        continue;
    } else if (MarkedNonReadWriteOnly.contains(VI))
      continue;
    for (auto &Ref : VI.getSummaryList())
      // If references to alias is not read/writeonly then aliasee
      // is not read/writeonly
      if (auto *GVS = dyn_cast<GlobalVarSummary>(Ref->getBaseObject())) {
        if (!VI.isReadOnly())
          GVS->setReadOnly(false);
        if (!VI.isWriteOnly())
          GVS->setWriteOnly(false);
      }
  }
}

// Do the access attribute and DSOLocal propagation in combined index.
// The goal of attribute propagation is internalization of readonly (RO)
// or writeonly (WO) variables. To determine which variables are RO or WO
// and which are not we take following steps:
// - During analysis we speculatively assign readonly and writeonly
//   attribute to all variables which can be internalized. When computing
//   function summary we also assign readonly or writeonly attribute to a
//   reference if function doesn't modify referenced variable (readonly)
//   or doesn't read it (writeonly).
//
// - After computing dead symbols in combined index we do the attribute
//   and DSOLocal propagation. During this step we:
//   a. clear RO and WO attributes from variables which are preserved or
//      can't be imported
//   b. clear RO and WO attributes from variables referenced by any global
//      variable initializer
//   c. clear RO attribute from variable referenced by a function when
//      reference is not readonly
//   d. clear WO attribute from variable referenced by a function when
//      reference is not writeonly
//   e. clear IsDSOLocal flag in every summary if any of them is false.
//
//   Because of (c, d) we don't internalize variables read by function A
//   and modified by function B.
//
// Internalization itself happens in the backend after import is finished
// See internalizeGVsAfterImport.
void ModuleSummaryIndex::propagateAttributes(
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols) {
  if (!PropagateAttrs)
    return;
  DenseSet<ValueInfo> MarkedNonReadWriteOnly;
  for (auto &P : *this) {
    bool IsDSOLocal = true;
    for (auto &S : P.second.SummaryList) {
      if (!isGlobalValueLive(S.get())) {
        // computeDeadSymbolsAndUpdateIndirectCalls should have marked all
        // copies live. Note that it is possible that there is a GUID collision
        // between internal symbols with the same name in different files of the
        // same name but not enough distinguishing path. Because
        // computeDeadSymbolsAndUpdateIndirectCalls should conservatively mark
        // all copies live we can assert here that all are dead if any copy is
        // dead.
        assert(llvm::none_of(
            P.second.SummaryList,
            [&](const std::unique_ptr<GlobalValueSummary> &Summary) {
              return isGlobalValueLive(Summary.get());
            }));
        // We don't examine references from dead objects
        break;
      }

      // Global variable can't be marked read/writeonly if it is not eligible
      // to import since we need to ensure that all external references get
      // a local (imported) copy. It also can't be marked read/writeonly if
      // it or any alias (since alias points to the same memory) are preserved
      // or notEligibleToImport, since either of those means there could be
      // writes (or reads in case of writeonly) that are not visible (because
      // preserved means it could have external to DSO writes or reads, and
      // notEligibleToImport means it could have writes or reads via inline
      // assembly leading it to be in the @llvm.*used).
      if (auto *GVS = dyn_cast<GlobalVarSummary>(S->getBaseObject()))
        // Here we intentionally pass S.get() not GVS, because S could be
        // an alias. We don't analyze references here, because we have to
        // know exactly if GV is readonly to do so.
        if (!canImportGlobalVar(S.get(), /* AnalyzeRefs */ false) ||
            GUIDPreservedSymbols.count(P.first)) {
          GVS->setReadOnly(false);
          GVS->setWriteOnly(false);
        }
      propagateAttributesToRefs(S.get(), MarkedNonReadWriteOnly);

      // If the flag from any summary is false, the GV is not DSOLocal.
      IsDSOLocal &= S->isDSOLocal();
    }
    if (!IsDSOLocal)
      // Mark the flag in all summaries false so that we can do quick check
      // without going through the whole list.
      for (const std::unique_ptr<GlobalValueSummary> &Summary :
           P.second.SummaryList)
        Summary->setDSOLocal(false);
  }
  setWithAttributePropagation();
  setWithDSOLocalPropagation();
  if (llvm::AreStatisticsEnabled())
    for (auto &P : *this)
      if (P.second.SummaryList.size())
        if (auto *GVS = dyn_cast<GlobalVarSummary>(
                P.second.SummaryList[0]->getBaseObject()))
          if (isGlobalValueLive(GVS)) {
            if (GVS->maybeReadOnly())
              ReadOnlyLiveGVars++;
            if (GVS->maybeWriteOnly())
              WriteOnlyLiveGVars++;
          }
}

bool ModuleSummaryIndex::canImportGlobalVar(const GlobalValueSummary *S,
                                            bool AnalyzeRefs) const {
  auto HasRefsPreventingImport = [this](const GlobalVarSummary *GVS) {
    // We don't analyze GV references during attribute propagation, so
    // GV with non-trivial initializer can be marked either read or
    // write-only.
    // Importing definiton of readonly GV with non-trivial initializer
    // allows us doing some extra optimizations (like converting indirect
    // calls to direct).
    // Definition of writeonly GV with non-trivial initializer should also
    // be imported. Not doing so will result in:
    // a) GV internalization in source module (because it's writeonly)
    // b) Importing of GV declaration to destination module as a result
    //    of promotion.
    // c) Link error (external declaration with internal definition).
    // However we do not promote objects referenced by writeonly GV
    // initializer by means of converting it to 'zeroinitializer'
    return !(ImportConstantsWithRefs && GVS->isConstant()) &&
           !isReadOnly(GVS) && !isWriteOnly(GVS) && GVS->refs().size();
  };
  auto *GVS = cast<GlobalVarSummary>(S->getBaseObject());

  // Global variable with non-trivial initializer can be imported
  // if it's readonly. This gives us extra opportunities for constant
  // folding and converting indirect calls to direct calls. We don't
  // analyze GV references during attribute propagation, because we
  // don't know yet if it is readonly or not.
  return !GlobalValue::isInterposableLinkage(S->linkage()) &&
         !S->notEligibleToImport() &&
         (!AnalyzeRefs || !HasRefsPreventingImport(GVS));
}

// TODO: write a graphviz dumper for SCCs (see ModuleSummaryIndex::exportToDot)
// then delete this function and update its tests
LLVM_DUMP_METHOD
void ModuleSummaryIndex::dumpSCCs(raw_ostream &O) {
  for (scc_iterator<ModuleSummaryIndex *> I =
           scc_begin<ModuleSummaryIndex *>(this);
       !I.isAtEnd(); ++I) {
    O << "SCC (" << utostr(I->size()) << " node" << (I->size() == 1 ? "" : "s")
      << ") {\n";
    for (const ValueInfo &V : *I) {
      FunctionSummary *F = nullptr;
      if (V.getSummaryList().size())
        F = cast<FunctionSummary>(V.getSummaryList().front().get());
      O << " " << (F == nullptr ? "External" : "") << " " << utostr(V.getGUID())
        << (I.hasCycle() ? " (has cycle)" : "") << "\n";
    }
    O << "}\n";
  }
}

namespace {
struct Attributes {
  void add(const Twine &Name, const Twine &Value,
           const Twine &Comment = Twine());
  void addComment(const Twine &Comment);
  std::string getAsString() const;

  std::vector<std::string> Attrs;
  std::string Comments;
};

struct Edge {
  uint64_t SrcMod;
  int Hotness;
  GlobalValue::GUID Src;
  GlobalValue::GUID Dst;
};
}

void Attributes::add(const Twine &Name, const Twine &Value,
                     const Twine &Comment) {
  std::string A = Name.str();
  A += "=\"";
  A += Value.str();
  A += "\"";
  Attrs.push_back(A);
  addComment(Comment);
}

void Attributes::addComment(const Twine &Comment) {
  if (!Comment.isTriviallyEmpty()) {
    if (Comments.empty())
      Comments = " // ";
    else
      Comments += ", ";
    Comments += Comment.str();
  }
}

std::string Attributes::getAsString() const {
  if (Attrs.empty())
    return "";

  std::string Ret = "[";
  for (auto &A : Attrs)
    Ret += A + ",";
  Ret.pop_back();
  Ret += "];";
  Ret += Comments;
  return Ret;
}

static std::string linkageToString(GlobalValue::LinkageTypes LT) {
  switch (LT) {
  case GlobalValue::ExternalLinkage:
    return "extern";
  case GlobalValue::AvailableExternallyLinkage:
    return "av_ext";
  case GlobalValue::LinkOnceAnyLinkage:
    return "linkonce";
  case GlobalValue::LinkOnceODRLinkage:
    return "linkonce_odr";
  case GlobalValue::WeakAnyLinkage:
    return "weak";
  case GlobalValue::WeakODRLinkage:
    return "weak_odr";
  case GlobalValue::AppendingLinkage:
    return "appending";
  case GlobalValue::InternalLinkage:
    return "internal";
  case GlobalValue::PrivateLinkage:
    return "private";
  case GlobalValue::ExternalWeakLinkage:
    return "extern_weak";
  case GlobalValue::CommonLinkage:
    return "common";
  }

  return "<unknown>";
}

static std::string fflagsToString(FunctionSummary::FFlags F) {
  auto FlagValue = [](unsigned V) { return V ? '1' : '0'; };
  char FlagRep[] = {FlagValue(F.ReadNone),
                    FlagValue(F.ReadOnly),
                    FlagValue(F.NoRecurse),
                    FlagValue(F.ReturnDoesNotAlias),
                    FlagValue(F.NoInline),
                    FlagValue(F.AlwaysInline),
                    FlagValue(F.NoUnwind),
                    FlagValue(F.MayThrow),
                    FlagValue(F.HasUnknownCall),
                    FlagValue(F.MustBeUnreachable),
                    0};

  return FlagRep;
}

// Get string representation of function instruction count and flags.
static std::string getSummaryAttributes(GlobalValueSummary* GVS) {
  auto *FS = dyn_cast_or_null<FunctionSummary>(GVS);
  if (!FS)
    return "";

  return std::string("inst: ") + std::to_string(FS->instCount()) +
         ", ffl: " + fflagsToString(FS->fflags());
}

static std::string getNodeVisualName(GlobalValue::GUID Id) {
  return std::string("@") + std::to_string(Id);
}

static std::string getNodeVisualName(const ValueInfo &VI) {
  return VI.name().empty() ? getNodeVisualName(VI.getGUID()) : VI.name().str();
}

static std::string getNodeLabel(const ValueInfo &VI, GlobalValueSummary *GVS) {
  if (isa<AliasSummary>(GVS))
    return getNodeVisualName(VI);

  std::string Attrs = getSummaryAttributes(GVS);
  std::string Label =
      getNodeVisualName(VI) + "|" + linkageToString(GVS->linkage());
  if (!Attrs.empty())
    Label += std::string(" (") + Attrs + ")";
  Label += "}";

  return Label;
}

// Write definition of external node, which doesn't have any
// specific module associated with it. Typically this is function
// or variable defined in native object or library.
static void defineExternalNode(raw_ostream &OS, const char *Pfx,
                               const ValueInfo &VI, GlobalValue::GUID Id) {
  auto StrId = std::to_string(Id);
  OS << "  " << StrId << " [label=\"";

  if (VI) {
    OS << getNodeVisualName(VI);
  } else {
    OS << getNodeVisualName(Id);
  }
  OS << "\"]; // defined externally\n";
}

static bool hasReadOnlyFlag(const GlobalValueSummary *S) {
  if (auto *GVS = dyn_cast<GlobalVarSummary>(S))
    return GVS->maybeReadOnly();
  return false;
}

static bool hasWriteOnlyFlag(const GlobalValueSummary *S) {
  if (auto *GVS = dyn_cast<GlobalVarSummary>(S))
    return GVS->maybeWriteOnly();
  return false;
}

static bool hasConstantFlag(const GlobalValueSummary *S) {
  if (auto *GVS = dyn_cast<GlobalVarSummary>(S))
    return GVS->isConstant();
  return false;
}

void ModuleSummaryIndex::exportToDot(
    raw_ostream &OS,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols) const {
  std::vector<Edge> CrossModuleEdges;
  DenseMap<GlobalValue::GUID, std::vector<uint64_t>> NodeMap;
  using GVSOrderedMapTy = std::map<GlobalValue::GUID, GlobalValueSummary *>;
  std::map<StringRef, GVSOrderedMapTy> ModuleToDefinedGVS;
  collectDefinedGVSummariesPerModule(ModuleToDefinedGVS);

  // Assign an id to each module path for use in graph labels. Since the
  // StringMap iteration order isn't guaranteed, order by path string before
  // assigning ids.
  std::vector<StringRef> ModulePaths;
  for (auto &[ModPath, _] : modulePaths())
    ModulePaths.push_back(ModPath);
  llvm::sort(ModulePaths);
  DenseMap<StringRef, uint64_t> ModuleIdMap;
  for (auto &ModPath : ModulePaths)
    ModuleIdMap.try_emplace(ModPath, ModuleIdMap.size());

  // Get node identifier in form MXXX_<GUID>. The MXXX prefix is required,
  // because we may have multiple linkonce functions summaries.
  auto NodeId = [](uint64_t ModId, GlobalValue::GUID Id) {
    return ModId == (uint64_t)-1 ? std::to_string(Id)
                                 : std::string("M") + std::to_string(ModId) +
                                       "_" + std::to_string(Id);
  };

  auto DrawEdge = [&](const char *Pfx, uint64_t SrcMod, GlobalValue::GUID SrcId,
                      uint64_t DstMod, GlobalValue::GUID DstId,
                      int TypeOrHotness) {
    // 0 - alias
    // 1 - reference
    // 2 - constant reference
    // 3 - writeonly reference
    // Other value: (hotness - 4).
    TypeOrHotness += 4;
    static const char *EdgeAttrs[] = {
        " [style=dotted]; // alias",
        " [style=dashed]; // ref",
        " [style=dashed,color=forestgreen]; // const-ref",
        " [style=dashed,color=violetred]; // writeOnly-ref",
        " // call (hotness : Unknown)",
        " [color=blue]; // call (hotness : Cold)",
        " // call (hotness : None)",
        " [color=brown]; // call (hotness : Hot)",
        " [style=bold,color=red]; // call (hotness : Critical)"};

    assert(static_cast<size_t>(TypeOrHotness) < std::size(EdgeAttrs));
    OS << Pfx << NodeId(SrcMod, SrcId) << " -> " << NodeId(DstMod, DstId)
       << EdgeAttrs[TypeOrHotness] << "\n";
  };

  OS << "digraph Summary {\n";
  for (auto &ModIt : ModuleToDefinedGVS) {
    // Will be empty for a just built per-module index, which doesn't setup a
    // module paths table. In that case use 0 as the module id.
    assert(ModuleIdMap.count(ModIt.first) || ModuleIdMap.empty());
    auto ModId = ModuleIdMap.empty() ? 0 : ModuleIdMap[ModIt.first];
    OS << "  // Module: " << ModIt.first << "\n";
    OS << "  subgraph cluster_" << std::to_string(ModId) << " {\n";
    OS << "    style = filled;\n";
    OS << "    color = lightgrey;\n";
    OS << "    label = \"" << sys::path::filename(ModIt.first) << "\";\n";
    OS << "    node [style=filled,fillcolor=lightblue];\n";

    auto &GVSMap = ModIt.second;
    auto Draw = [&](GlobalValue::GUID IdFrom, GlobalValue::GUID IdTo, int Hotness) {
      if (!GVSMap.count(IdTo)) {
        CrossModuleEdges.push_back({ModId, Hotness, IdFrom, IdTo});
        return;
      }
      DrawEdge("    ", ModId, IdFrom, ModId, IdTo, Hotness);
    };

    for (auto &SummaryIt : GVSMap) {
      NodeMap[SummaryIt.first].push_back(ModId);
      auto Flags = SummaryIt.second->flags();
      Attributes A;
      if (isa<FunctionSummary>(SummaryIt.second)) {
        A.add("shape", "record", "function");
      } else if (isa<AliasSummary>(SummaryIt.second)) {
        A.add("style", "dotted,filled", "alias");
        A.add("shape", "box");
      } else {
        A.add("shape", "Mrecord", "variable");
        if (Flags.Live && hasReadOnlyFlag(SummaryIt.second))
          A.addComment("immutable");
        if (Flags.Live && hasWriteOnlyFlag(SummaryIt.second))
          A.addComment("writeOnly");
        if (Flags.Live && hasConstantFlag(SummaryIt.second))
          A.addComment("constant");
      }
      if (Flags.Visibility)
        A.addComment("visibility");
      if (Flags.DSOLocal)
        A.addComment("dsoLocal");
      if (Flags.CanAutoHide)
        A.addComment("canAutoHide");
      if (Flags.ImportType == GlobalValueSummary::ImportKind::Definition)
        A.addComment("definition");
      else if (Flags.ImportType == GlobalValueSummary::ImportKind::Declaration)
        A.addComment("declaration");
      if (GUIDPreservedSymbols.count(SummaryIt.first))
        A.addComment("preserved");

      auto VI = getValueInfo(SummaryIt.first);
      A.add("label", getNodeLabel(VI, SummaryIt.second));
      if (!Flags.Live)
        A.add("fillcolor", "red", "dead");
      else if (Flags.NotEligibleToImport)
        A.add("fillcolor", "yellow", "not eligible to import");

      OS << "    " << NodeId(ModId, SummaryIt.first) << " " << A.getAsString()
         << "\n";
    }
    OS << "    // Edges:\n";

    for (auto &SummaryIt : GVSMap) {
      auto *GVS = SummaryIt.second;
      for (auto &R : GVS->refs())
        Draw(SummaryIt.first, R.getGUID(),
             R.isWriteOnly() ? -1 : (R.isReadOnly() ? -2 : -3));

      if (auto *AS = dyn_cast_or_null<AliasSummary>(SummaryIt.second)) {
        Draw(SummaryIt.first, AS->getAliaseeGUID(), -4);
        continue;
      }

      if (auto *FS = dyn_cast_or_null<FunctionSummary>(SummaryIt.second))
        for (auto &CGEdge : FS->calls())
          Draw(SummaryIt.first, CGEdge.first.getGUID(),
               static_cast<int>(CGEdge.second.Hotness));
    }
    OS << "  }\n";
  }

  OS << "  // Cross-module edges:\n";
  for (auto &E : CrossModuleEdges) {
    auto &ModList = NodeMap[E.Dst];
    if (ModList.empty()) {
      defineExternalNode(OS, "  ", getValueInfo(E.Dst), E.Dst);
      // Add fake module to the list to draw an edge to an external node
      // in the loop below.
      ModList.push_back(-1);
    }
    for (auto DstMod : ModList)
      // The edge representing call or ref is drawn to every module where target
      // symbol is defined. When target is a linkonce symbol there can be
      // multiple edges representing a single call or ref, both intra-module and
      // cross-module. As we've already drawn all intra-module edges before we
      // skip it here.
      if (DstMod != E.SrcMod)
        DrawEdge("  ", E.SrcMod, E.Src, DstMod, E.Dst, E.Hotness);
  }

  OS << "}";
}
