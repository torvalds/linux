//===--- Core.cpp - Core ORC APIs (MaterializationUnit, JITDylib, etc.) ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Core.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/ExecutionEngine/Orc/DebugUtils.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcError.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MSVCErrorWorkarounds.h"

#include <condition_variable>
#include <future>
#include <optional>

#define DEBUG_TYPE "orc"

namespace llvm {
namespace orc {

char ResourceTrackerDefunct::ID = 0;
char FailedToMaterialize::ID = 0;
char SymbolsNotFound::ID = 0;
char SymbolsCouldNotBeRemoved::ID = 0;
char MissingSymbolDefinitions::ID = 0;
char UnexpectedSymbolDefinitions::ID = 0;
char UnsatisfiedSymbolDependencies::ID = 0;
char MaterializationTask::ID = 0;
char LookupTask::ID = 0;

RegisterDependenciesFunction NoDependenciesToRegister =
    RegisterDependenciesFunction();

void MaterializationUnit::anchor() {}

ResourceTracker::ResourceTracker(JITDylibSP JD) {
  assert((reinterpret_cast<uintptr_t>(JD.get()) & 0x1) == 0 &&
         "JITDylib must be two byte aligned");
  JD->Retain();
  JDAndFlag.store(reinterpret_cast<uintptr_t>(JD.get()));
}

ResourceTracker::~ResourceTracker() {
  getJITDylib().getExecutionSession().destroyResourceTracker(*this);
  getJITDylib().Release();
}

Error ResourceTracker::remove() {
  return getJITDylib().getExecutionSession().removeResourceTracker(*this);
}

void ResourceTracker::transferTo(ResourceTracker &DstRT) {
  getJITDylib().getExecutionSession().transferResourceTracker(DstRT, *this);
}

void ResourceTracker::makeDefunct() {
  uintptr_t Val = JDAndFlag.load();
  Val |= 0x1U;
  JDAndFlag.store(Val);
}

ResourceManager::~ResourceManager() = default;

ResourceTrackerDefunct::ResourceTrackerDefunct(ResourceTrackerSP RT)
    : RT(std::move(RT)) {}

std::error_code ResourceTrackerDefunct::convertToErrorCode() const {
  return orcError(OrcErrorCode::UnknownORCError);
}

void ResourceTrackerDefunct::log(raw_ostream &OS) const {
  OS << "Resource tracker " << (void *)RT.get() << " became defunct";
}

FailedToMaterialize::FailedToMaterialize(
    std::shared_ptr<SymbolStringPool> SSP,
    std::shared_ptr<SymbolDependenceMap> Symbols)
    : SSP(std::move(SSP)), Symbols(std::move(Symbols)) {
  assert(this->SSP && "String pool cannot be null");
  assert(!this->Symbols->empty() && "Can not fail to resolve an empty set");

  // FIXME: Use a new dep-map type for FailedToMaterialize errors so that we
  // don't have to manually retain/release.
  for (auto &[JD, Syms] : *this->Symbols)
    JD->Retain();
}

FailedToMaterialize::~FailedToMaterialize() {
  for (auto &[JD, Syms] : *Symbols)
    JD->Release();
}

std::error_code FailedToMaterialize::convertToErrorCode() const {
  return orcError(OrcErrorCode::UnknownORCError);
}

void FailedToMaterialize::log(raw_ostream &OS) const {
  OS << "Failed to materialize symbols: " << *Symbols;
}

UnsatisfiedSymbolDependencies::UnsatisfiedSymbolDependencies(
    std::shared_ptr<SymbolStringPool> SSP, JITDylibSP JD,
    SymbolNameSet FailedSymbols, SymbolDependenceMap BadDeps,
    std::string Explanation)
    : SSP(std::move(SSP)), JD(std::move(JD)),
      FailedSymbols(std::move(FailedSymbols)), BadDeps(std::move(BadDeps)),
      Explanation(std::move(Explanation)) {}

std::error_code UnsatisfiedSymbolDependencies::convertToErrorCode() const {
  return orcError(OrcErrorCode::UnknownORCError);
}

void UnsatisfiedSymbolDependencies::log(raw_ostream &OS) const {
  OS << "In " << JD->getName() << ", failed to materialize " << FailedSymbols
     << ", due to unsatisfied dependencies " << BadDeps;
  if (!Explanation.empty())
    OS << " (" << Explanation << ")";
}

SymbolsNotFound::SymbolsNotFound(std::shared_ptr<SymbolStringPool> SSP,
                                 SymbolNameSet Symbols)
    : SSP(std::move(SSP)) {
  for (auto &Sym : Symbols)
    this->Symbols.push_back(Sym);
  assert(!this->Symbols.empty() && "Can not fail to resolve an empty set");
}

SymbolsNotFound::SymbolsNotFound(std::shared_ptr<SymbolStringPool> SSP,
                                 SymbolNameVector Symbols)
    : SSP(std::move(SSP)), Symbols(std::move(Symbols)) {
  assert(!this->Symbols.empty() && "Can not fail to resolve an empty set");
}

std::error_code SymbolsNotFound::convertToErrorCode() const {
  return orcError(OrcErrorCode::UnknownORCError);
}

void SymbolsNotFound::log(raw_ostream &OS) const {
  OS << "Symbols not found: " << Symbols;
}

SymbolsCouldNotBeRemoved::SymbolsCouldNotBeRemoved(
    std::shared_ptr<SymbolStringPool> SSP, SymbolNameSet Symbols)
    : SSP(std::move(SSP)), Symbols(std::move(Symbols)) {
  assert(!this->Symbols.empty() && "Can not fail to resolve an empty set");
}

std::error_code SymbolsCouldNotBeRemoved::convertToErrorCode() const {
  return orcError(OrcErrorCode::UnknownORCError);
}

void SymbolsCouldNotBeRemoved::log(raw_ostream &OS) const {
  OS << "Symbols could not be removed: " << Symbols;
}

std::error_code MissingSymbolDefinitions::convertToErrorCode() const {
  return orcError(OrcErrorCode::MissingSymbolDefinitions);
}

void MissingSymbolDefinitions::log(raw_ostream &OS) const {
  OS << "Missing definitions in module " << ModuleName
     << ": " << Symbols;
}

std::error_code UnexpectedSymbolDefinitions::convertToErrorCode() const {
  return orcError(OrcErrorCode::UnexpectedSymbolDefinitions);
}

void UnexpectedSymbolDefinitions::log(raw_ostream &OS) const {
  OS << "Unexpected definitions in module " << ModuleName
     << ": " << Symbols;
}

AsynchronousSymbolQuery::AsynchronousSymbolQuery(
    const SymbolLookupSet &Symbols, SymbolState RequiredState,
    SymbolsResolvedCallback NotifyComplete)
    : NotifyComplete(std::move(NotifyComplete)), RequiredState(RequiredState) {
  assert(RequiredState >= SymbolState::Resolved &&
         "Cannot query for a symbols that have not reached the resolve state "
         "yet");

  OutstandingSymbolsCount = Symbols.size();

  for (auto &[Name, Flags] : Symbols)
    ResolvedSymbols[Name] = ExecutorSymbolDef();
}

void AsynchronousSymbolQuery::notifySymbolMetRequiredState(
    const SymbolStringPtr &Name, ExecutorSymbolDef Sym) {
  auto I = ResolvedSymbols.find(Name);
  assert(I != ResolvedSymbols.end() &&
         "Resolving symbol outside the requested set");
  assert(I->second == ExecutorSymbolDef() &&
         "Redundantly resolving symbol Name");

  // If this is a materialization-side-effects-only symbol then drop it,
  // otherwise update its map entry with its resolved address.
  if (Sym.getFlags().hasMaterializationSideEffectsOnly())
    ResolvedSymbols.erase(I);
  else
    I->second = std::move(Sym);
  --OutstandingSymbolsCount;
}

void AsynchronousSymbolQuery::handleComplete(ExecutionSession &ES) {
  assert(OutstandingSymbolsCount == 0 &&
         "Symbols remain, handleComplete called prematurely");

  class RunQueryCompleteTask : public Task {
  public:
    RunQueryCompleteTask(SymbolMap ResolvedSymbols,
                         SymbolsResolvedCallback NotifyComplete)
        : ResolvedSymbols(std::move(ResolvedSymbols)),
          NotifyComplete(std::move(NotifyComplete)) {}
    void printDescription(raw_ostream &OS) override {
      OS << "Execute query complete callback for " << ResolvedSymbols;
    }
    void run() override { NotifyComplete(std::move(ResolvedSymbols)); }

  private:
    SymbolMap ResolvedSymbols;
    SymbolsResolvedCallback NotifyComplete;
  };

  auto T = std::make_unique<RunQueryCompleteTask>(std::move(ResolvedSymbols),
                                                  std::move(NotifyComplete));
  NotifyComplete = SymbolsResolvedCallback();
  ES.dispatchTask(std::move(T));
}

void AsynchronousSymbolQuery::handleFailed(Error Err) {
  assert(QueryRegistrations.empty() && ResolvedSymbols.empty() &&
         OutstandingSymbolsCount == 0 &&
         "Query should already have been abandoned");
  NotifyComplete(std::move(Err));
  NotifyComplete = SymbolsResolvedCallback();
}

void AsynchronousSymbolQuery::addQueryDependence(JITDylib &JD,
                                                 SymbolStringPtr Name) {
  bool Added = QueryRegistrations[&JD].insert(std::move(Name)).second;
  (void)Added;
  assert(Added && "Duplicate dependence notification?");
}

void AsynchronousSymbolQuery::removeQueryDependence(
    JITDylib &JD, const SymbolStringPtr &Name) {
  auto QRI = QueryRegistrations.find(&JD);
  assert(QRI != QueryRegistrations.end() &&
         "No dependencies registered for JD");
  assert(QRI->second.count(Name) && "No dependency on Name in JD");
  QRI->second.erase(Name);
  if (QRI->second.empty())
    QueryRegistrations.erase(QRI);
}

void AsynchronousSymbolQuery::dropSymbol(const SymbolStringPtr &Name) {
  auto I = ResolvedSymbols.find(Name);
  assert(I != ResolvedSymbols.end() &&
         "Redundant removal of weakly-referenced symbol");
  ResolvedSymbols.erase(I);
  --OutstandingSymbolsCount;
}

void AsynchronousSymbolQuery::detach() {
  ResolvedSymbols.clear();
  OutstandingSymbolsCount = 0;
  for (auto &[JD, Syms] : QueryRegistrations)
    JD->detachQueryHelper(*this, Syms);
  QueryRegistrations.clear();
}

AbsoluteSymbolsMaterializationUnit::AbsoluteSymbolsMaterializationUnit(
    SymbolMap Symbols)
    : MaterializationUnit(extractFlags(Symbols)), Symbols(std::move(Symbols)) {}

StringRef AbsoluteSymbolsMaterializationUnit::getName() const {
  return "<Absolute Symbols>";
}

void AbsoluteSymbolsMaterializationUnit::materialize(
    std::unique_ptr<MaterializationResponsibility> R) {
  // Even though these are just absolute symbols we need to check for failure
  // to resolve/emit: the tracker for these symbols may have been removed while
  // the materialization was in flight (e.g. due to a failure in some action
  // triggered by the queries attached to the resolution/emission of these
  // symbols).
  if (auto Err = R->notifyResolved(Symbols)) {
    R->getExecutionSession().reportError(std::move(Err));
    R->failMaterialization();
    return;
  }
  if (auto Err = R->notifyEmitted({})) {
    R->getExecutionSession().reportError(std::move(Err));
    R->failMaterialization();
    return;
  }
}

void AbsoluteSymbolsMaterializationUnit::discard(const JITDylib &JD,
                                                 const SymbolStringPtr &Name) {
  assert(Symbols.count(Name) && "Symbol is not part of this MU");
  Symbols.erase(Name);
}

MaterializationUnit::Interface
AbsoluteSymbolsMaterializationUnit::extractFlags(const SymbolMap &Symbols) {
  SymbolFlagsMap Flags;
  for (const auto &[Name, Def] : Symbols)
    Flags[Name] = Def.getFlags();
  return MaterializationUnit::Interface(std::move(Flags), nullptr);
}

ReExportsMaterializationUnit::ReExportsMaterializationUnit(
    JITDylib *SourceJD, JITDylibLookupFlags SourceJDLookupFlags,
    SymbolAliasMap Aliases)
    : MaterializationUnit(extractFlags(Aliases)), SourceJD(SourceJD),
      SourceJDLookupFlags(SourceJDLookupFlags), Aliases(std::move(Aliases)) {}

StringRef ReExportsMaterializationUnit::getName() const {
  return "<Reexports>";
}

void ReExportsMaterializationUnit::materialize(
    std::unique_ptr<MaterializationResponsibility> R) {

  auto &ES = R->getTargetJITDylib().getExecutionSession();
  JITDylib &TgtJD = R->getTargetJITDylib();
  JITDylib &SrcJD = SourceJD ? *SourceJD : TgtJD;

  // Find the set of requested aliases and aliasees. Return any unrequested
  // aliases back to the JITDylib so as to not prematurely materialize any
  // aliasees.
  auto RequestedSymbols = R->getRequestedSymbols();
  SymbolAliasMap RequestedAliases;

  for (auto &Name : RequestedSymbols) {
    auto I = Aliases.find(Name);
    assert(I != Aliases.end() && "Symbol not found in aliases map?");
    RequestedAliases[Name] = std::move(I->second);
    Aliases.erase(I);
  }

  LLVM_DEBUG({
    ES.runSessionLocked([&]() {
      dbgs() << "materializing reexports: target = " << TgtJD.getName()
             << ", source = " << SrcJD.getName() << " " << RequestedAliases
             << "\n";
    });
  });

  if (!Aliases.empty()) {
    auto Err = SourceJD ? R->replace(reexports(*SourceJD, std::move(Aliases),
                                               SourceJDLookupFlags))
                        : R->replace(symbolAliases(std::move(Aliases)));

    if (Err) {
      // FIXME: Should this be reported / treated as failure to materialize?
      // Or should this be treated as a sanctioned bailing-out?
      ES.reportError(std::move(Err));
      R->failMaterialization();
      return;
    }
  }

  // The OnResolveInfo struct will hold the aliases and responsibility for each
  // query in the list.
  struct OnResolveInfo {
    OnResolveInfo(std::unique_ptr<MaterializationResponsibility> R,
                  SymbolAliasMap Aliases)
        : R(std::move(R)), Aliases(std::move(Aliases)) {}

    std::unique_ptr<MaterializationResponsibility> R;
    SymbolAliasMap Aliases;
    std::vector<SymbolDependenceGroup> SDGs;
  };

  // Build a list of queries to issue. In each round we build a query for the
  // largest set of aliases that we can resolve without encountering a chain of
  // aliases (e.g. Foo -> Bar, Bar -> Baz). Such a chain would deadlock as the
  // query would be waiting on a symbol that it itself had to resolve. Creating
  // a new query for each link in such a chain eliminates the possibility of
  // deadlock. In practice chains are likely to be rare, and this algorithm will
  // usually result in a single query to issue.

  std::vector<std::pair<SymbolLookupSet, std::shared_ptr<OnResolveInfo>>>
      QueryInfos;
  while (!RequestedAliases.empty()) {
    SymbolNameSet ResponsibilitySymbols;
    SymbolLookupSet QuerySymbols;
    SymbolAliasMap QueryAliases;

    // Collect as many aliases as we can without including a chain.
    for (auto &KV : RequestedAliases) {
      // Chain detected. Skip this symbol for this round.
      if (&SrcJD == &TgtJD && (QueryAliases.count(KV.second.Aliasee) ||
                               RequestedAliases.count(KV.second.Aliasee)))
        continue;

      ResponsibilitySymbols.insert(KV.first);
      QuerySymbols.add(KV.second.Aliasee,
                       KV.second.AliasFlags.hasMaterializationSideEffectsOnly()
                           ? SymbolLookupFlags::WeaklyReferencedSymbol
                           : SymbolLookupFlags::RequiredSymbol);
      QueryAliases[KV.first] = std::move(KV.second);
    }

    // Remove the aliases collected this round from the RequestedAliases map.
    for (auto &KV : QueryAliases)
      RequestedAliases.erase(KV.first);

    assert(!QuerySymbols.empty() && "Alias cycle detected!");

    auto NewR = R->delegate(ResponsibilitySymbols);
    if (!NewR) {
      ES.reportError(NewR.takeError());
      R->failMaterialization();
      return;
    }

    auto QueryInfo = std::make_shared<OnResolveInfo>(std::move(*NewR),
                                                     std::move(QueryAliases));
    QueryInfos.push_back(
        make_pair(std::move(QuerySymbols), std::move(QueryInfo)));
  }

  // Issue the queries.
  while (!QueryInfos.empty()) {
    auto QuerySymbols = std::move(QueryInfos.back().first);
    auto QueryInfo = std::move(QueryInfos.back().second);

    QueryInfos.pop_back();

    auto RegisterDependencies = [QueryInfo,
                                 &SrcJD](const SymbolDependenceMap &Deps) {
      // If there were no materializing symbols, just bail out.
      if (Deps.empty())
        return;

      // Otherwise the only deps should be on SrcJD.
      assert(Deps.size() == 1 && Deps.count(&SrcJD) &&
             "Unexpected dependencies for reexports");

      auto &SrcJDDeps = Deps.find(&SrcJD)->second;

      for (auto &[Alias, AliasInfo] : QueryInfo->Aliases)
        if (SrcJDDeps.count(AliasInfo.Aliasee))
          QueryInfo->SDGs.push_back({{Alias}, {{&SrcJD, {AliasInfo.Aliasee}}}});
    };

    auto OnComplete = [QueryInfo](Expected<SymbolMap> Result) {
      auto &ES = QueryInfo->R->getTargetJITDylib().getExecutionSession();
      if (Result) {
        SymbolMap ResolutionMap;
        for (auto &KV : QueryInfo->Aliases) {
          assert((KV.second.AliasFlags.hasMaterializationSideEffectsOnly() ||
                  Result->count(KV.second.Aliasee)) &&
                 "Result map missing entry?");
          // Don't try to resolve materialization-side-effects-only symbols.
          if (KV.second.AliasFlags.hasMaterializationSideEffectsOnly())
            continue;

          ResolutionMap[KV.first] = {(*Result)[KV.second.Aliasee].getAddress(),
                                     KV.second.AliasFlags};
        }
        if (auto Err = QueryInfo->R->notifyResolved(ResolutionMap)) {
          ES.reportError(std::move(Err));
          QueryInfo->R->failMaterialization();
          return;
        }
        if (auto Err = QueryInfo->R->notifyEmitted(QueryInfo->SDGs)) {
          ES.reportError(std::move(Err));
          QueryInfo->R->failMaterialization();
          return;
        }
      } else {
        ES.reportError(Result.takeError());
        QueryInfo->R->failMaterialization();
      }
    };

    ES.lookup(LookupKind::Static,
              JITDylibSearchOrder({{&SrcJD, SourceJDLookupFlags}}),
              QuerySymbols, SymbolState::Resolved, std::move(OnComplete),
              std::move(RegisterDependencies));
  }
}

void ReExportsMaterializationUnit::discard(const JITDylib &JD,
                                           const SymbolStringPtr &Name) {
  assert(Aliases.count(Name) &&
         "Symbol not covered by this MaterializationUnit");
  Aliases.erase(Name);
}

MaterializationUnit::Interface
ReExportsMaterializationUnit::extractFlags(const SymbolAliasMap &Aliases) {
  SymbolFlagsMap SymbolFlags;
  for (auto &KV : Aliases)
    SymbolFlags[KV.first] = KV.second.AliasFlags;

  return MaterializationUnit::Interface(std::move(SymbolFlags), nullptr);
}

Expected<SymbolAliasMap> buildSimpleReexportsAliasMap(JITDylib &SourceJD,
                                                      SymbolNameSet Symbols) {
  SymbolLookupSet LookupSet(Symbols);
  auto Flags = SourceJD.getExecutionSession().lookupFlags(
      LookupKind::Static, {{&SourceJD, JITDylibLookupFlags::MatchAllSymbols}},
      SymbolLookupSet(std::move(Symbols)));

  if (!Flags)
    return Flags.takeError();

  SymbolAliasMap Result;
  for (auto &Name : Symbols) {
    assert(Flags->count(Name) && "Missing entry in flags map");
    Result[Name] = SymbolAliasMapEntry(Name, (*Flags)[Name]);
  }

  return Result;
}

class InProgressLookupState {
public:
  // FIXME: Reduce the number of SymbolStringPtrs here. See
  //        https://github.com/llvm/llvm-project/issues/55576.

  InProgressLookupState(LookupKind K, JITDylibSearchOrder SearchOrder,
                        SymbolLookupSet LookupSet, SymbolState RequiredState)
      : K(K), SearchOrder(std::move(SearchOrder)),
        LookupSet(std::move(LookupSet)), RequiredState(RequiredState) {
    DefGeneratorCandidates = this->LookupSet;
  }
  virtual ~InProgressLookupState() = default;
  virtual void complete(std::unique_ptr<InProgressLookupState> IPLS) = 0;
  virtual void fail(Error Err) = 0;

  LookupKind K;
  JITDylibSearchOrder SearchOrder;
  SymbolLookupSet LookupSet;
  SymbolState RequiredState;

  size_t CurSearchOrderIndex = 0;
  bool NewJITDylib = true;
  SymbolLookupSet DefGeneratorCandidates;
  SymbolLookupSet DefGeneratorNonCandidates;

  enum {
    NotInGenerator,      // Not currently using a generator.
    ResumedForGenerator, // Resumed after being auto-suspended before generator.
    InGenerator          // Currently using generator.
  } GenState = NotInGenerator;
  std::vector<std::weak_ptr<DefinitionGenerator>> CurDefGeneratorStack;
};

class InProgressLookupFlagsState : public InProgressLookupState {
public:
  InProgressLookupFlagsState(
      LookupKind K, JITDylibSearchOrder SearchOrder, SymbolLookupSet LookupSet,
      unique_function<void(Expected<SymbolFlagsMap>)> OnComplete)
      : InProgressLookupState(K, std::move(SearchOrder), std::move(LookupSet),
                              SymbolState::NeverSearched),
        OnComplete(std::move(OnComplete)) {}

  void complete(std::unique_ptr<InProgressLookupState> IPLS) override {
    auto &ES = SearchOrder.front().first->getExecutionSession();
    ES.OL_completeLookupFlags(std::move(IPLS), std::move(OnComplete));
  }

  void fail(Error Err) override { OnComplete(std::move(Err)); }

private:
  unique_function<void(Expected<SymbolFlagsMap>)> OnComplete;
};

class InProgressFullLookupState : public InProgressLookupState {
public:
  InProgressFullLookupState(LookupKind K, JITDylibSearchOrder SearchOrder,
                            SymbolLookupSet LookupSet,
                            SymbolState RequiredState,
                            std::shared_ptr<AsynchronousSymbolQuery> Q,
                            RegisterDependenciesFunction RegisterDependencies)
      : InProgressLookupState(K, std::move(SearchOrder), std::move(LookupSet),
                              RequiredState),
        Q(std::move(Q)), RegisterDependencies(std::move(RegisterDependencies)) {
  }

  void complete(std::unique_ptr<InProgressLookupState> IPLS) override {
    auto &ES = SearchOrder.front().first->getExecutionSession();
    ES.OL_completeLookup(std::move(IPLS), std::move(Q),
                         std::move(RegisterDependencies));
  }

  void fail(Error Err) override {
    Q->detach();
    Q->handleFailed(std::move(Err));
  }

private:
  std::shared_ptr<AsynchronousSymbolQuery> Q;
  RegisterDependenciesFunction RegisterDependencies;
};

ReexportsGenerator::ReexportsGenerator(JITDylib &SourceJD,
                                       JITDylibLookupFlags SourceJDLookupFlags,
                                       SymbolPredicate Allow)
    : SourceJD(SourceJD), SourceJDLookupFlags(SourceJDLookupFlags),
      Allow(std::move(Allow)) {}

Error ReexportsGenerator::tryToGenerate(LookupState &LS, LookupKind K,
                                        JITDylib &JD,
                                        JITDylibLookupFlags JDLookupFlags,
                                        const SymbolLookupSet &LookupSet) {
  assert(&JD != &SourceJD && "Cannot re-export from the same dylib");

  // Use lookupFlags to find the subset of symbols that match our lookup.
  auto Flags = JD.getExecutionSession().lookupFlags(
      K, {{&SourceJD, JDLookupFlags}}, LookupSet);
  if (!Flags)
    return Flags.takeError();

  // Create an alias map.
  orc::SymbolAliasMap AliasMap;
  for (auto &KV : *Flags)
    if (!Allow || Allow(KV.first))
      AliasMap[KV.first] = SymbolAliasMapEntry(KV.first, KV.second);

  if (AliasMap.empty())
    return Error::success();

  // Define the re-exports.
  return JD.define(reexports(SourceJD, AliasMap, SourceJDLookupFlags));
}

LookupState::LookupState(std::unique_ptr<InProgressLookupState> IPLS)
    : IPLS(std::move(IPLS)) {}

void LookupState::reset(InProgressLookupState *IPLS) { this->IPLS.reset(IPLS); }

LookupState::LookupState() = default;
LookupState::LookupState(LookupState &&) = default;
LookupState &LookupState::operator=(LookupState &&) = default;
LookupState::~LookupState() = default;

void LookupState::continueLookup(Error Err) {
  assert(IPLS && "Cannot call continueLookup on empty LookupState");
  auto &ES = IPLS->SearchOrder.begin()->first->getExecutionSession();
  ES.OL_applyQueryPhase1(std::move(IPLS), std::move(Err));
}

DefinitionGenerator::~DefinitionGenerator() {
  std::deque<LookupState> LookupsToFail;
  {
    std::lock_guard<std::mutex> Lock(M);
    std::swap(PendingLookups, LookupsToFail);
    InUse = false;
  }

  for (auto &LS : LookupsToFail)
    LS.continueLookup(make_error<StringError>(
        "Query waiting on DefinitionGenerator that was destroyed",
        inconvertibleErrorCode()));
}

JITDylib::~JITDylib() {
  LLVM_DEBUG(dbgs() << "Destroying JITDylib " << getName() << "\n");
}

Error JITDylib::clear() {
  std::vector<ResourceTrackerSP> TrackersToRemove;
  ES.runSessionLocked([&]() {
    assert(State != Closed && "JD is defunct");
    for (auto &KV : TrackerSymbols)
      TrackersToRemove.push_back(KV.first);
    TrackersToRemove.push_back(getDefaultResourceTracker());
  });

  Error Err = Error::success();
  for (auto &RT : TrackersToRemove)
    Err = joinErrors(std::move(Err), RT->remove());
  return Err;
}

ResourceTrackerSP JITDylib::getDefaultResourceTracker() {
  return ES.runSessionLocked([this] {
    assert(State != Closed && "JD is defunct");
    if (!DefaultTracker)
      DefaultTracker = new ResourceTracker(this);
    return DefaultTracker;
  });
}

ResourceTrackerSP JITDylib::createResourceTracker() {
  return ES.runSessionLocked([this] {
    assert(State == Open && "JD is defunct");
    ResourceTrackerSP RT = new ResourceTracker(this);
    return RT;
  });
}

void JITDylib::removeGenerator(DefinitionGenerator &G) {
  // DefGenerator moved into TmpDG to ensure that it's destroyed outside the
  // session lock (since it may have to send errors to pending queries).
  std::shared_ptr<DefinitionGenerator> TmpDG;

  ES.runSessionLocked([&] {
    assert(State == Open && "JD is defunct");
    auto I = llvm::find_if(DefGenerators,
                           [&](const std::shared_ptr<DefinitionGenerator> &H) {
                             return H.get() == &G;
                           });
    assert(I != DefGenerators.end() && "Generator not found");
    TmpDG = std::move(*I);
    DefGenerators.erase(I);
  });
}

Expected<SymbolFlagsMap>
JITDylib::defineMaterializing(MaterializationResponsibility &FromMR,
                              SymbolFlagsMap SymbolFlags) {

  return ES.runSessionLocked([&]() -> Expected<SymbolFlagsMap> {
    if (FromMR.RT->isDefunct())
      return make_error<ResourceTrackerDefunct>(FromMR.RT);

    std::vector<NonOwningSymbolStringPtr> AddedSyms;
    std::vector<NonOwningSymbolStringPtr> RejectedWeakDefs;

    for (auto SFItr = SymbolFlags.begin(), SFEnd = SymbolFlags.end();
         SFItr != SFEnd; ++SFItr) {

      auto &Name = SFItr->first;
      auto &Flags = SFItr->second;

      auto EntryItr = Symbols.find(Name);

      // If the entry already exists...
      if (EntryItr != Symbols.end()) {

        // If this is a strong definition then error out.
        if (!Flags.isWeak()) {
          // Remove any symbols already added.
          for (auto &S : AddedSyms)
            Symbols.erase(Symbols.find_as(S));

          // FIXME: Return all duplicates.
          return make_error<DuplicateDefinition>(std::string(*Name));
        }

        // Otherwise just make a note to discard this symbol after the loop.
        RejectedWeakDefs.push_back(NonOwningSymbolStringPtr(Name));
        continue;
      } else
        EntryItr =
          Symbols.insert(std::make_pair(Name, SymbolTableEntry(Flags))).first;

      AddedSyms.push_back(NonOwningSymbolStringPtr(Name));
      EntryItr->second.setState(SymbolState::Materializing);
    }

    // Remove any rejected weak definitions from the SymbolFlags map.
    while (!RejectedWeakDefs.empty()) {
      SymbolFlags.erase(SymbolFlags.find_as(RejectedWeakDefs.back()));
      RejectedWeakDefs.pop_back();
    }

    return SymbolFlags;
  });
}

Error JITDylib::replace(MaterializationResponsibility &FromMR,
                        std::unique_ptr<MaterializationUnit> MU) {
  assert(MU != nullptr && "Can not replace with a null MaterializationUnit");
  std::unique_ptr<MaterializationUnit> MustRunMU;
  std::unique_ptr<MaterializationResponsibility> MustRunMR;

  auto Err =
      ES.runSessionLocked([&, this]() -> Error {
        if (FromMR.RT->isDefunct())
          return make_error<ResourceTrackerDefunct>(std::move(FromMR.RT));

#ifndef NDEBUG
        for (auto &KV : MU->getSymbols()) {
          auto SymI = Symbols.find(KV.first);
          assert(SymI != Symbols.end() && "Replacing unknown symbol");
          assert(SymI->second.getState() == SymbolState::Materializing &&
                 "Can not replace a symbol that ha is not materializing");
          assert(!SymI->second.hasMaterializerAttached() &&
                 "Symbol should not have materializer attached already");
          assert(UnmaterializedInfos.count(KV.first) == 0 &&
                 "Symbol being replaced should have no UnmaterializedInfo");
        }
#endif // NDEBUG

        // If the tracker is defunct we need to bail out immediately.

        // If any symbol has pending queries against it then we need to
        // materialize MU immediately.
        for (auto &KV : MU->getSymbols()) {
          auto MII = MaterializingInfos.find(KV.first);
          if (MII != MaterializingInfos.end()) {
            if (MII->second.hasQueriesPending()) {
              MustRunMR = ES.createMaterializationResponsibility(
                  *FromMR.RT, std::move(MU->SymbolFlags),
                  std::move(MU->InitSymbol));
              MustRunMU = std::move(MU);
              return Error::success();
            }
          }
        }

        // Otherwise, make MU responsible for all the symbols.
        auto UMI = std::make_shared<UnmaterializedInfo>(std::move(MU),
                                                        FromMR.RT.get());
        for (auto &KV : UMI->MU->getSymbols()) {
          auto SymI = Symbols.find(KV.first);
          assert(SymI->second.getState() == SymbolState::Materializing &&
                 "Can not replace a symbol that is not materializing");
          assert(!SymI->second.hasMaterializerAttached() &&
                 "Can not replace a symbol that has a materializer attached");
          assert(UnmaterializedInfos.count(KV.first) == 0 &&
                 "Unexpected materializer entry in map");
          SymI->second.setAddress(SymI->second.getAddress());
          SymI->second.setMaterializerAttached(true);

          auto &UMIEntry = UnmaterializedInfos[KV.first];
          assert((!UMIEntry || !UMIEntry->MU) &&
                 "Replacing symbol with materializer still attached");
          UMIEntry = UMI;
        }

        return Error::success();
      });

  if (Err)
    return Err;

  if (MustRunMU) {
    assert(MustRunMR && "MustRunMU set implies MustRunMR set");
    ES.dispatchTask(std::make_unique<MaterializationTask>(
        std::move(MustRunMU), std::move(MustRunMR)));
  } else {
    assert(!MustRunMR && "MustRunMU unset implies MustRunMR unset");
  }

  return Error::success();
}

Expected<std::unique_ptr<MaterializationResponsibility>>
JITDylib::delegate(MaterializationResponsibility &FromMR,
                   SymbolFlagsMap SymbolFlags, SymbolStringPtr InitSymbol) {

  return ES.runSessionLocked(
      [&]() -> Expected<std::unique_ptr<MaterializationResponsibility>> {
        if (FromMR.RT->isDefunct())
          return make_error<ResourceTrackerDefunct>(std::move(FromMR.RT));

        return ES.createMaterializationResponsibility(
            *FromMR.RT, std::move(SymbolFlags), std::move(InitSymbol));
      });
}

SymbolNameSet
JITDylib::getRequestedSymbols(const SymbolFlagsMap &SymbolFlags) const {
  return ES.runSessionLocked([&]() {
    SymbolNameSet RequestedSymbols;

    for (auto &KV : SymbolFlags) {
      assert(Symbols.count(KV.first) && "JITDylib does not cover this symbol?");
      assert(Symbols.find(KV.first)->second.getState() !=
                 SymbolState::NeverSearched &&
             Symbols.find(KV.first)->second.getState() != SymbolState::Ready &&
             "getRequestedSymbols can only be called for symbols that have "
             "started materializing");
      auto I = MaterializingInfos.find(KV.first);
      if (I == MaterializingInfos.end())
        continue;

      if (I->second.hasQueriesPending())
        RequestedSymbols.insert(KV.first);
    }

    return RequestedSymbols;
  });
}

Error JITDylib::resolve(MaterializationResponsibility &MR,
                        const SymbolMap &Resolved) {
  AsynchronousSymbolQuerySet CompletedQueries;

  if (auto Err = ES.runSessionLocked([&, this]() -> Error {
        if (MR.RT->isDefunct())
          return make_error<ResourceTrackerDefunct>(MR.RT);

        if (State != Open)
          return make_error<StringError>("JITDylib " + getName() +
                                             " is defunct",
                                         inconvertibleErrorCode());

        struct WorklistEntry {
          SymbolTable::iterator SymI;
          ExecutorSymbolDef ResolvedSym;
        };

        SymbolNameSet SymbolsInErrorState;
        std::vector<WorklistEntry> Worklist;
        Worklist.reserve(Resolved.size());

        // Build worklist and check for any symbols in the error state.
        for (const auto &KV : Resolved) {

          assert(!KV.second.getFlags().hasError() &&
                 "Resolution result can not have error flag set");

          auto SymI = Symbols.find(KV.first);

          assert(SymI != Symbols.end() && "Symbol not found");
          assert(!SymI->second.hasMaterializerAttached() &&
                 "Resolving symbol with materializer attached?");
          assert(SymI->second.getState() == SymbolState::Materializing &&
                 "Symbol should be materializing");
          assert(SymI->second.getAddress() == ExecutorAddr() &&
                 "Symbol has already been resolved");

          if (SymI->second.getFlags().hasError())
            SymbolsInErrorState.insert(KV.first);
          else {
            auto Flags = KV.second.getFlags();
            Flags &= ~JITSymbolFlags::Common;
            assert(Flags ==
                       (SymI->second.getFlags() & ~JITSymbolFlags::Common) &&
                   "Resolved flags should match the declared flags");

            Worklist.push_back({SymI, {KV.second.getAddress(), Flags}});
          }
        }

        // If any symbols were in the error state then bail out.
        if (!SymbolsInErrorState.empty()) {
          auto FailedSymbolsDepMap = std::make_shared<SymbolDependenceMap>();
          (*FailedSymbolsDepMap)[this] = std::move(SymbolsInErrorState);
          return make_error<FailedToMaterialize>(
              getExecutionSession().getSymbolStringPool(),
              std::move(FailedSymbolsDepMap));
        }

        while (!Worklist.empty()) {
          auto SymI = Worklist.back().SymI;
          auto ResolvedSym = Worklist.back().ResolvedSym;
          Worklist.pop_back();

          auto &Name = SymI->first;

          // Resolved symbols can not be weak: discard the weak flag.
          JITSymbolFlags ResolvedFlags = ResolvedSym.getFlags();
          SymI->second.setAddress(ResolvedSym.getAddress());
          SymI->second.setFlags(ResolvedFlags);
          SymI->second.setState(SymbolState::Resolved);

          auto MII = MaterializingInfos.find(Name);
          if (MII == MaterializingInfos.end())
            continue;

          auto &MI = MII->second;
          for (auto &Q : MI.takeQueriesMeeting(SymbolState::Resolved)) {
            Q->notifySymbolMetRequiredState(Name, ResolvedSym);
            Q->removeQueryDependence(*this, Name);
            if (Q->isComplete())
              CompletedQueries.insert(std::move(Q));
          }
        }

        return Error::success();
      }))
    return Err;

  // Otherwise notify all the completed queries.
  for (auto &Q : CompletedQueries) {
    assert(Q->isComplete() && "Q not completed");
    Q->handleComplete(ES);
  }

  return Error::success();
}

void JITDylib::unlinkMaterializationResponsibility(
    MaterializationResponsibility &MR) {
  ES.runSessionLocked([&]() {
    auto I = TrackerMRs.find(MR.RT.get());
    assert(I != TrackerMRs.end() && "No MRs in TrackerMRs list for RT");
    assert(I->second.count(&MR) && "MR not in TrackerMRs list for RT");
    I->second.erase(&MR);
    if (I->second.empty())
      TrackerMRs.erase(MR.RT.get());
  });
}

void JITDylib::shrinkMaterializationInfoMemory() {
  // DenseMap::erase never shrinks its storage; use clear to heuristically free
  // memory since we may have long-lived JDs after linking is done.

  if (UnmaterializedInfos.empty())
    UnmaterializedInfos.clear();

  if (MaterializingInfos.empty())
    MaterializingInfos.clear();
}

void JITDylib::setLinkOrder(JITDylibSearchOrder NewLinkOrder,
                            bool LinkAgainstThisJITDylibFirst) {
  ES.runSessionLocked([&]() {
    assert(State == Open && "JD is defunct");
    if (LinkAgainstThisJITDylibFirst) {
      LinkOrder.clear();
      if (NewLinkOrder.empty() || NewLinkOrder.front().first != this)
        LinkOrder.push_back(
            std::make_pair(this, JITDylibLookupFlags::MatchAllSymbols));
      llvm::append_range(LinkOrder, NewLinkOrder);
    } else
      LinkOrder = std::move(NewLinkOrder);
  });
}

void JITDylib::addToLinkOrder(const JITDylibSearchOrder &NewLinks) {
  ES.runSessionLocked([&]() {
    for (auto &KV : NewLinks) {
      // Skip elements of NewLinks that are already in the link order.
      if (llvm::is_contained(LinkOrder, KV))
        continue;

      LinkOrder.push_back(std::move(KV));
    }
  });
}

void JITDylib::addToLinkOrder(JITDylib &JD, JITDylibLookupFlags JDLookupFlags) {
  ES.runSessionLocked([&]() { LinkOrder.push_back({&JD, JDLookupFlags}); });
}

void JITDylib::replaceInLinkOrder(JITDylib &OldJD, JITDylib &NewJD,
                                  JITDylibLookupFlags JDLookupFlags) {
  ES.runSessionLocked([&]() {
    assert(State == Open && "JD is defunct");
    for (auto &KV : LinkOrder)
      if (KV.first == &OldJD) {
        KV = {&NewJD, JDLookupFlags};
        break;
      }
  });
}

void JITDylib::removeFromLinkOrder(JITDylib &JD) {
  ES.runSessionLocked([&]() {
    assert(State == Open && "JD is defunct");
    auto I = llvm::find_if(LinkOrder,
                           [&](const JITDylibSearchOrder::value_type &KV) {
                             return KV.first == &JD;
                           });
    if (I != LinkOrder.end())
      LinkOrder.erase(I);
  });
}

Error JITDylib::remove(const SymbolNameSet &Names) {
  return ES.runSessionLocked([&]() -> Error {
    assert(State == Open && "JD is defunct");
    using SymbolMaterializerItrPair =
        std::pair<SymbolTable::iterator, UnmaterializedInfosMap::iterator>;
    std::vector<SymbolMaterializerItrPair> SymbolsToRemove;
    SymbolNameSet Missing;
    SymbolNameSet Materializing;

    for (auto &Name : Names) {
      auto I = Symbols.find(Name);

      // Note symbol missing.
      if (I == Symbols.end()) {
        Missing.insert(Name);
        continue;
      }

      // Note symbol materializing.
      if (I->second.getState() != SymbolState::NeverSearched &&
          I->second.getState() != SymbolState::Ready) {
        Materializing.insert(Name);
        continue;
      }

      auto UMII = I->second.hasMaterializerAttached()
                      ? UnmaterializedInfos.find(Name)
                      : UnmaterializedInfos.end();
      SymbolsToRemove.push_back(std::make_pair(I, UMII));
    }

    // If any of the symbols are not defined, return an error.
    if (!Missing.empty())
      return make_error<SymbolsNotFound>(ES.getSymbolStringPool(),
                                         std::move(Missing));

    // If any of the symbols are currently materializing, return an error.
    if (!Materializing.empty())
      return make_error<SymbolsCouldNotBeRemoved>(ES.getSymbolStringPool(),
                                                  std::move(Materializing));

    // Remove the symbols.
    for (auto &SymbolMaterializerItrPair : SymbolsToRemove) {
      auto UMII = SymbolMaterializerItrPair.second;

      // If there is a materializer attached, call discard.
      if (UMII != UnmaterializedInfos.end()) {
        UMII->second->MU->doDiscard(*this, UMII->first);
        UnmaterializedInfos.erase(UMII);
      }

      auto SymI = SymbolMaterializerItrPair.first;
      Symbols.erase(SymI);
    }

    shrinkMaterializationInfoMemory();

    return Error::success();
  });
}

void JITDylib::dump(raw_ostream &OS) {
  ES.runSessionLocked([&, this]() {
    OS << "JITDylib \"" << getName() << "\" (ES: "
       << format("0x%016" PRIx64, reinterpret_cast<uintptr_t>(&ES))
       << ", State = ";
    switch (State) {
    case Open:
      OS << "Open";
      break;
    case Closing:
      OS << "Closing";
      break;
    case Closed:
      OS << "Closed";
      break;
    }
    OS << ")\n";
    if (State == Closed)
      return;
    OS << "Link order: " << LinkOrder << "\n"
       << "Symbol table:\n";

    // Sort symbols so we get a deterministic order and can check them in tests.
    std::vector<std::pair<SymbolStringPtr, SymbolTableEntry *>> SymbolsSorted;
    for (auto &KV : Symbols)
      SymbolsSorted.emplace_back(KV.first, &KV.second);
    std::sort(SymbolsSorted.begin(), SymbolsSorted.end(),
              [](const auto &L, const auto &R) { return *L.first < *R.first; });

    for (auto &KV : SymbolsSorted) {
      OS << "    \"" << *KV.first << "\": ";
      if (auto Addr = KV.second->getAddress())
        OS << Addr;
      else
        OS << "<not resolved> ";

      OS << " " << KV.second->getFlags() << " " << KV.second->getState();

      if (KV.second->hasMaterializerAttached()) {
        OS << " (Materializer ";
        auto I = UnmaterializedInfos.find(KV.first);
        assert(I != UnmaterializedInfos.end() &&
               "Lazy symbol should have UnmaterializedInfo");
        OS << I->second->MU.get() << ", " << I->second->MU->getName() << ")\n";
      } else
        OS << "\n";
    }

    if (!MaterializingInfos.empty())
      OS << "  MaterializingInfos entries:\n";
    for (auto &KV : MaterializingInfos) {
      OS << "    \"" << *KV.first << "\":\n"
         << "      " << KV.second.pendingQueries().size()
         << " pending queries: { ";
      for (const auto &Q : KV.second.pendingQueries())
        OS << Q.get() << " (" << Q->getRequiredState() << ") ";
      OS << "}\n      Defining EDU: ";
      if (KV.second.DefiningEDU) {
        OS << KV.second.DefiningEDU.get() << " { ";
        for (auto &[Name, Flags] : KV.second.DefiningEDU->Symbols)
          OS << Name << " ";
        OS << "}\n";
        OS << "        Dependencies:\n";
        if (!KV.second.DefiningEDU->Dependencies.empty()) {
          for (auto &[DepJD, Deps] : KV.second.DefiningEDU->Dependencies) {
            OS << "          " << DepJD->getName() << ": [ ";
            for (auto &Dep : Deps)
              OS << Dep << " ";
            OS << "]\n";
          }
        } else
          OS << "          none\n";
      } else
        OS << "none\n";
      OS << "      Dependant EDUs:\n";
      if (!KV.second.DependantEDUs.empty()) {
        for (auto &DependantEDU : KV.second.DependantEDUs) {
          OS << "        " << DependantEDU << ": "
             << DependantEDU->JD->getName() << " { ";
          for (auto &[Name, Flags] : DependantEDU->Symbols)
            OS << Name << " ";
          OS << "}\n";
        }
      } else
        OS << "        none\n";
      assert((Symbols[KV.first].getState() != SymbolState::Ready ||
              (KV.second.pendingQueries().empty() && !KV.second.DefiningEDU &&
               !KV.second.DependantEDUs.empty())) &&
             "Stale materializing info entry");
    }
  });
}

void JITDylib::MaterializingInfo::addQuery(
    std::shared_ptr<AsynchronousSymbolQuery> Q) {

  auto I = llvm::lower_bound(
      llvm::reverse(PendingQueries), Q->getRequiredState(),
      [](const std::shared_ptr<AsynchronousSymbolQuery> &V, SymbolState S) {
        return V->getRequiredState() <= S;
      });
  PendingQueries.insert(I.base(), std::move(Q));
}

void JITDylib::MaterializingInfo::removeQuery(
    const AsynchronousSymbolQuery &Q) {
  // FIXME: Implement 'find_as' for shared_ptr<T>/T*.
  auto I = llvm::find_if(
      PendingQueries, [&Q](const std::shared_ptr<AsynchronousSymbolQuery> &V) {
        return V.get() == &Q;
      });
  assert(I != PendingQueries.end() &&
         "Query is not attached to this MaterializingInfo");
  PendingQueries.erase(I);
}

JITDylib::AsynchronousSymbolQueryList
JITDylib::MaterializingInfo::takeQueriesMeeting(SymbolState RequiredState) {
  AsynchronousSymbolQueryList Result;
  while (!PendingQueries.empty()) {
    if (PendingQueries.back()->getRequiredState() > RequiredState)
      break;

    Result.push_back(std::move(PendingQueries.back()));
    PendingQueries.pop_back();
  }

  return Result;
}

JITDylib::JITDylib(ExecutionSession &ES, std::string Name)
    : JITLinkDylib(std::move(Name)), ES(ES) {
  LinkOrder.push_back({this, JITDylibLookupFlags::MatchAllSymbols});
}

std::pair<JITDylib::AsynchronousSymbolQuerySet,
          std::shared_ptr<SymbolDependenceMap>>
JITDylib::IL_removeTracker(ResourceTracker &RT) {
  // Note: Should be called under the session lock.
  assert(State != Closed && "JD is defunct");

  SymbolNameVector SymbolsToRemove;
  SymbolNameVector SymbolsToFail;

  if (&RT == DefaultTracker.get()) {
    SymbolNameSet TrackedSymbols;
    for (auto &KV : TrackerSymbols)
      for (auto &Sym : KV.second)
        TrackedSymbols.insert(Sym);

    for (auto &KV : Symbols) {
      auto &Sym = KV.first;
      if (!TrackedSymbols.count(Sym))
        SymbolsToRemove.push_back(Sym);
    }

    DefaultTracker.reset();
  } else {
    /// Check for a non-default tracker.
    auto I = TrackerSymbols.find(&RT);
    if (I != TrackerSymbols.end()) {
      SymbolsToRemove = std::move(I->second);
      TrackerSymbols.erase(I);
    }
    // ... if not found this tracker was already defunct. Nothing to do.
  }

  for (auto &Sym : SymbolsToRemove) {
    assert(Symbols.count(Sym) && "Symbol not in symbol table");

    // If there is a MaterializingInfo then collect any queries to fail.
    auto MII = MaterializingInfos.find(Sym);
    if (MII != MaterializingInfos.end())
      SymbolsToFail.push_back(Sym);
  }

  auto Result = ES.IL_failSymbols(*this, std::move(SymbolsToFail));

  // Removed symbols should be taken out of the table altogether.
  for (auto &Sym : SymbolsToRemove) {
    auto I = Symbols.find(Sym);
    assert(I != Symbols.end() && "Symbol not present in table");

    // Remove Materializer if present.
    if (I->second.hasMaterializerAttached()) {
      // FIXME: Should this discard the symbols?
      UnmaterializedInfos.erase(Sym);
    } else {
      assert(!UnmaterializedInfos.count(Sym) &&
             "Symbol has materializer attached");
    }

    Symbols.erase(I);
  }

  shrinkMaterializationInfoMemory();

  return Result;
}

void JITDylib::transferTracker(ResourceTracker &DstRT, ResourceTracker &SrcRT) {
  assert(State != Closed && "JD is defunct");
  assert(&DstRT != &SrcRT && "No-op transfers shouldn't call transferTracker");
  assert(&DstRT.getJITDylib() == this && "DstRT is not for this JITDylib");
  assert(&SrcRT.getJITDylib() == this && "SrcRT is not for this JITDylib");

  // Update trackers for any not-yet materialized units.
  for (auto &KV : UnmaterializedInfos) {
    if (KV.second->RT == &SrcRT)
      KV.second->RT = &DstRT;
  }

  // Update trackers for any active materialization responsibilities.
  {
    auto I = TrackerMRs.find(&SrcRT);
    if (I != TrackerMRs.end()) {
      auto &SrcMRs = I->second;
      auto &DstMRs = TrackerMRs[&DstRT];
      for (auto *MR : SrcMRs)
        MR->RT = &DstRT;
      if (DstMRs.empty())
        DstMRs = std::move(SrcMRs);
      else
        for (auto *MR : SrcMRs)
          DstMRs.insert(MR);
      // Erase SrcRT entry in TrackerMRs. Use &SrcRT key rather than iterator I
      // for this, since I may have been invalidated by 'TrackerMRs[&DstRT]'.
      TrackerMRs.erase(&SrcRT);
    }
  }

  // If we're transfering to the default tracker we just need to delete the
  // tracked symbols for the source tracker.
  if (&DstRT == DefaultTracker.get()) {
    TrackerSymbols.erase(&SrcRT);
    return;
  }

  // If we're transferring from the default tracker we need to find all
  // currently untracked symbols.
  if (&SrcRT == DefaultTracker.get()) {
    assert(!TrackerSymbols.count(&SrcRT) &&
           "Default tracker should not appear in TrackerSymbols");

    SymbolNameVector SymbolsToTrack;

    SymbolNameSet CurrentlyTrackedSymbols;
    for (auto &KV : TrackerSymbols)
      for (auto &Sym : KV.second)
        CurrentlyTrackedSymbols.insert(Sym);

    for (auto &KV : Symbols) {
      auto &Sym = KV.first;
      if (!CurrentlyTrackedSymbols.count(Sym))
        SymbolsToTrack.push_back(Sym);
    }

    TrackerSymbols[&DstRT] = std::move(SymbolsToTrack);
    return;
  }

  auto &DstTrackedSymbols = TrackerSymbols[&DstRT];

  // Finally if neither SrtRT or DstRT are the default tracker then
  // just append DstRT's tracked symbols to SrtRT's.
  auto SI = TrackerSymbols.find(&SrcRT);
  if (SI == TrackerSymbols.end())
    return;

  DstTrackedSymbols.reserve(DstTrackedSymbols.size() + SI->second.size());
  for (auto &Sym : SI->second)
    DstTrackedSymbols.push_back(std::move(Sym));
  TrackerSymbols.erase(SI);
}

Error JITDylib::defineImpl(MaterializationUnit &MU) {
  LLVM_DEBUG({ dbgs() << "  " << MU.getSymbols() << "\n"; });

  SymbolNameSet Duplicates;
  std::vector<SymbolStringPtr> ExistingDefsOverridden;
  std::vector<SymbolStringPtr> MUDefsOverridden;

  for (const auto &KV : MU.getSymbols()) {
    auto I = Symbols.find(KV.first);

    if (I != Symbols.end()) {
      if (KV.second.isStrong()) {
        if (I->second.getFlags().isStrong() ||
            I->second.getState() > SymbolState::NeverSearched)
          Duplicates.insert(KV.first);
        else {
          assert(I->second.getState() == SymbolState::NeverSearched &&
                 "Overridden existing def should be in the never-searched "
                 "state");
          ExistingDefsOverridden.push_back(KV.first);
        }
      } else
        MUDefsOverridden.push_back(KV.first);
    }
  }

  // If there were any duplicate definitions then bail out.
  if (!Duplicates.empty()) {
    LLVM_DEBUG(
        { dbgs() << "  Error: Duplicate symbols " << Duplicates << "\n"; });
    return make_error<DuplicateDefinition>(std::string(**Duplicates.begin()));
  }

  // Discard any overridden defs in this MU.
  LLVM_DEBUG({
    if (!MUDefsOverridden.empty())
      dbgs() << "  Defs in this MU overridden: " << MUDefsOverridden << "\n";
  });
  for (auto &S : MUDefsOverridden)
    MU.doDiscard(*this, S);

  // Discard existing overridden defs.
  LLVM_DEBUG({
    if (!ExistingDefsOverridden.empty())
      dbgs() << "  Existing defs overridden by this MU: " << MUDefsOverridden
             << "\n";
  });
  for (auto &S : ExistingDefsOverridden) {

    auto UMII = UnmaterializedInfos.find(S);
    assert(UMII != UnmaterializedInfos.end() &&
           "Overridden existing def should have an UnmaterializedInfo");
    UMII->second->MU->doDiscard(*this, S);
  }

  // Finally, add the defs from this MU.
  for (auto &KV : MU.getSymbols()) {
    auto &SymEntry = Symbols[KV.first];
    SymEntry.setFlags(KV.second);
    SymEntry.setState(SymbolState::NeverSearched);
    SymEntry.setMaterializerAttached(true);
  }

  return Error::success();
}

void JITDylib::installMaterializationUnit(
    std::unique_ptr<MaterializationUnit> MU, ResourceTracker &RT) {

  /// defineImpl succeeded.
  if (&RT != DefaultTracker.get()) {
    auto &TS = TrackerSymbols[&RT];
    TS.reserve(TS.size() + MU->getSymbols().size());
    for (auto &KV : MU->getSymbols())
      TS.push_back(KV.first);
  }

  auto UMI = std::make_shared<UnmaterializedInfo>(std::move(MU), &RT);
  for (auto &KV : UMI->MU->getSymbols())
    UnmaterializedInfos[KV.first] = UMI;
}

void JITDylib::detachQueryHelper(AsynchronousSymbolQuery &Q,
                                 const SymbolNameSet &QuerySymbols) {
  for (auto &QuerySymbol : QuerySymbols) {
    assert(MaterializingInfos.count(QuerySymbol) &&
           "QuerySymbol does not have MaterializingInfo");
    auto &MI = MaterializingInfos[QuerySymbol];
    MI.removeQuery(Q);
  }
}

Platform::~Platform() = default;

Expected<DenseMap<JITDylib *, SymbolMap>> Platform::lookupInitSymbols(
    ExecutionSession &ES,
    const DenseMap<JITDylib *, SymbolLookupSet> &InitSyms) {

  DenseMap<JITDylib *, SymbolMap> CompoundResult;
  Error CompoundErr = Error::success();
  std::mutex LookupMutex;
  std::condition_variable CV;
  uint64_t Count = InitSyms.size();

  LLVM_DEBUG({
    dbgs() << "Issuing init-symbol lookup:\n";
    for (auto &KV : InitSyms)
      dbgs() << "  " << KV.first->getName() << ": " << KV.second << "\n";
  });

  for (auto &KV : InitSyms) {
    auto *JD = KV.first;
    auto Names = std::move(KV.second);
    ES.lookup(
        LookupKind::Static,
        JITDylibSearchOrder({{JD, JITDylibLookupFlags::MatchAllSymbols}}),
        std::move(Names), SymbolState::Ready,
        [&, JD](Expected<SymbolMap> Result) {
          {
            std::lock_guard<std::mutex> Lock(LookupMutex);
            --Count;
            if (Result) {
              assert(!CompoundResult.count(JD) &&
                     "Duplicate JITDylib in lookup?");
              CompoundResult[JD] = std::move(*Result);
            } else
              CompoundErr =
                  joinErrors(std::move(CompoundErr), Result.takeError());
          }
          CV.notify_one();
        },
        NoDependenciesToRegister);
  }

  std::unique_lock<std::mutex> Lock(LookupMutex);
  CV.wait(Lock, [&] { return Count == 0 || CompoundErr; });

  if (CompoundErr)
    return std::move(CompoundErr);

  return std::move(CompoundResult);
}

void Platform::lookupInitSymbolsAsync(
    unique_function<void(Error)> OnComplete, ExecutionSession &ES,
    const DenseMap<JITDylib *, SymbolLookupSet> &InitSyms) {

  class TriggerOnComplete {
  public:
    using OnCompleteFn = unique_function<void(Error)>;
    TriggerOnComplete(OnCompleteFn OnComplete)
        : OnComplete(std::move(OnComplete)) {}
    ~TriggerOnComplete() { OnComplete(std::move(LookupResult)); }
    void reportResult(Error Err) {
      std::lock_guard<std::mutex> Lock(ResultMutex);
      LookupResult = joinErrors(std::move(LookupResult), std::move(Err));
    }

  private:
    std::mutex ResultMutex;
    Error LookupResult{Error::success()};
    OnCompleteFn OnComplete;
  };

  LLVM_DEBUG({
    dbgs() << "Issuing init-symbol lookup:\n";
    for (auto &KV : InitSyms)
      dbgs() << "  " << KV.first->getName() << ": " << KV.second << "\n";
  });

  auto TOC = std::make_shared<TriggerOnComplete>(std::move(OnComplete));

  for (auto &KV : InitSyms) {
    auto *JD = KV.first;
    auto Names = std::move(KV.second);
    ES.lookup(
        LookupKind::Static,
        JITDylibSearchOrder({{JD, JITDylibLookupFlags::MatchAllSymbols}}),
        std::move(Names), SymbolState::Ready,
        [TOC](Expected<SymbolMap> Result) {
          TOC->reportResult(Result.takeError());
        },
        NoDependenciesToRegister);
  }
}

void MaterializationTask::printDescription(raw_ostream &OS) {
  OS << "Materialization task: " << MU->getName() << " in "
     << MR->getTargetJITDylib().getName();
}

void MaterializationTask::run() { MU->materialize(std::move(MR)); }

void LookupTask::printDescription(raw_ostream &OS) { OS << "Lookup task"; }

void LookupTask::run() { LS.continueLookup(Error::success()); }

ExecutionSession::ExecutionSession(std::unique_ptr<ExecutorProcessControl> EPC)
    : EPC(std::move(EPC)) {
  // Associated EPC and this.
  this->EPC->ES = this;
}

ExecutionSession::~ExecutionSession() {
  // You must call endSession prior to destroying the session.
  assert(!SessionOpen &&
         "Session still open. Did you forget to call endSession?");
}

Error ExecutionSession::endSession() {
  LLVM_DEBUG(dbgs() << "Ending ExecutionSession " << this << "\n");

  auto JDsToRemove = runSessionLocked([&] {

#ifdef EXPENSIVE_CHECKS
    verifySessionState("Entering ExecutionSession::endSession");
#endif

    SessionOpen = false;
    return JDs;
  });

  std::reverse(JDsToRemove.begin(), JDsToRemove.end());

  auto Err = removeJITDylibs(std::move(JDsToRemove));

  Err = joinErrors(std::move(Err), EPC->disconnect());

  return Err;
}

void ExecutionSession::registerResourceManager(ResourceManager &RM) {
  runSessionLocked([&] { ResourceManagers.push_back(&RM); });
}

void ExecutionSession::deregisterResourceManager(ResourceManager &RM) {
  runSessionLocked([&] {
    assert(!ResourceManagers.empty() && "No managers registered");
    if (ResourceManagers.back() == &RM)
      ResourceManagers.pop_back();
    else {
      auto I = llvm::find(ResourceManagers, &RM);
      assert(I != ResourceManagers.end() && "RM not registered");
      ResourceManagers.erase(I);
    }
  });
}

JITDylib *ExecutionSession::getJITDylibByName(StringRef Name) {
  return runSessionLocked([&, this]() -> JITDylib * {
    for (auto &JD : JDs)
      if (JD->getName() == Name)
        return JD.get();
    return nullptr;
  });
}

JITDylib &ExecutionSession::createBareJITDylib(std::string Name) {
  assert(!getJITDylibByName(Name) && "JITDylib with that name already exists");
  return runSessionLocked([&, this]() -> JITDylib & {
    assert(SessionOpen && "Cannot create JITDylib after session is closed");
    JDs.push_back(new JITDylib(*this, std::move(Name)));
    return *JDs.back();
  });
}

Expected<JITDylib &> ExecutionSession::createJITDylib(std::string Name) {
  auto &JD = createBareJITDylib(Name);
  if (P)
    if (auto Err = P->setupJITDylib(JD))
      return std::move(Err);
  return JD;
}

Error ExecutionSession::removeJITDylibs(std::vector<JITDylibSP> JDsToRemove) {
  // Set JD to 'Closing' state and remove JD from the ExecutionSession.
  runSessionLocked([&] {
    for (auto &JD : JDsToRemove) {
      assert(JD->State == JITDylib::Open && "JD already closed");
      JD->State = JITDylib::Closing;
      auto I = llvm::find(JDs, JD);
      assert(I != JDs.end() && "JD does not appear in session JDs");
      JDs.erase(I);
    }
  });

  // Clear JITDylibs and notify the platform.
  Error Err = Error::success();
  for (auto JD : JDsToRemove) {
    Err = joinErrors(std::move(Err), JD->clear());
    if (P)
      Err = joinErrors(std::move(Err), P->teardownJITDylib(*JD));
  }

  // Set JD to closed state. Clear remaining data structures.
  runSessionLocked([&] {
    for (auto &JD : JDsToRemove) {
      assert(JD->State == JITDylib::Closing && "JD should be closing");
      JD->State = JITDylib::Closed;
      assert(JD->Symbols.empty() && "JD.Symbols is not empty after clear");
      assert(JD->UnmaterializedInfos.empty() &&
             "JD.UnmaterializedInfos is not empty after clear");
      assert(JD->MaterializingInfos.empty() &&
             "JD.MaterializingInfos is not empty after clear");
      assert(JD->TrackerSymbols.empty() &&
             "TrackerSymbols is not empty after clear");
      JD->DefGenerators.clear();
      JD->LinkOrder.clear();
    }
  });

  return Err;
}

Expected<std::vector<JITDylibSP>>
JITDylib::getDFSLinkOrder(ArrayRef<JITDylibSP> JDs) {
  if (JDs.empty())
    return std::vector<JITDylibSP>();

  auto &ES = JDs.front()->getExecutionSession();
  return ES.runSessionLocked([&]() -> Expected<std::vector<JITDylibSP>> {
    DenseSet<JITDylib *> Visited;
    std::vector<JITDylibSP> Result;

    for (auto &JD : JDs) {

      if (JD->State != Open)
        return make_error<StringError>(
            "Error building link order: " + JD->getName() + " is defunct",
            inconvertibleErrorCode());
      if (Visited.count(JD.get()))
        continue;

      SmallVector<JITDylibSP, 64> WorkStack;
      WorkStack.push_back(JD);
      Visited.insert(JD.get());

      while (!WorkStack.empty()) {
        Result.push_back(std::move(WorkStack.back()));
        WorkStack.pop_back();

        for (auto &KV : llvm::reverse(Result.back()->LinkOrder)) {
          auto &JD = *KV.first;
          if (!Visited.insert(&JD).second)
            continue;
          WorkStack.push_back(&JD);
        }
      }
    }
    return Result;
  });
}

Expected<std::vector<JITDylibSP>>
JITDylib::getReverseDFSLinkOrder(ArrayRef<JITDylibSP> JDs) {
  auto Result = getDFSLinkOrder(JDs);
  if (Result)
    std::reverse(Result->begin(), Result->end());
  return Result;
}

Expected<std::vector<JITDylibSP>> JITDylib::getDFSLinkOrder() {
  return getDFSLinkOrder({this});
}

Expected<std::vector<JITDylibSP>> JITDylib::getReverseDFSLinkOrder() {
  return getReverseDFSLinkOrder({this});
}

void ExecutionSession::lookupFlags(
    LookupKind K, JITDylibSearchOrder SearchOrder, SymbolLookupSet LookupSet,
    unique_function<void(Expected<SymbolFlagsMap>)> OnComplete) {

  OL_applyQueryPhase1(std::make_unique<InProgressLookupFlagsState>(
                          K, std::move(SearchOrder), std::move(LookupSet),
                          std::move(OnComplete)),
                      Error::success());
}

Expected<SymbolFlagsMap>
ExecutionSession::lookupFlags(LookupKind K, JITDylibSearchOrder SearchOrder,
                              SymbolLookupSet LookupSet) {

  std::promise<MSVCPExpected<SymbolFlagsMap>> ResultP;
  OL_applyQueryPhase1(std::make_unique<InProgressLookupFlagsState>(
                          K, std::move(SearchOrder), std::move(LookupSet),
                          [&ResultP](Expected<SymbolFlagsMap> Result) {
                            ResultP.set_value(std::move(Result));
                          }),
                      Error::success());

  auto ResultF = ResultP.get_future();
  return ResultF.get();
}

void ExecutionSession::lookup(
    LookupKind K, const JITDylibSearchOrder &SearchOrder,
    SymbolLookupSet Symbols, SymbolState RequiredState,
    SymbolsResolvedCallback NotifyComplete,
    RegisterDependenciesFunction RegisterDependencies) {

  LLVM_DEBUG({
    runSessionLocked([&]() {
      dbgs() << "Looking up " << Symbols << " in " << SearchOrder
             << " (required state: " << RequiredState << ")\n";
    });
  });

  // lookup can be re-entered recursively if running on a single thread. Run any
  // outstanding MUs in case this query depends on them, otherwise this lookup
  // will starve waiting for a result from an MU that is stuck in the queue.
  dispatchOutstandingMUs();

  auto Unresolved = std::move(Symbols);
  auto Q = std::make_shared<AsynchronousSymbolQuery>(Unresolved, RequiredState,
                                                     std::move(NotifyComplete));

  auto IPLS = std::make_unique<InProgressFullLookupState>(
      K, SearchOrder, std::move(Unresolved), RequiredState, std::move(Q),
      std::move(RegisterDependencies));

  OL_applyQueryPhase1(std::move(IPLS), Error::success());
}

Expected<SymbolMap>
ExecutionSession::lookup(const JITDylibSearchOrder &SearchOrder,
                         SymbolLookupSet Symbols, LookupKind K,
                         SymbolState RequiredState,
                         RegisterDependenciesFunction RegisterDependencies) {
#if LLVM_ENABLE_THREADS
  // In the threaded case we use promises to return the results.
  std::promise<SymbolMap> PromisedResult;
  Error ResolutionError = Error::success();

  auto NotifyComplete = [&](Expected<SymbolMap> R) {
    if (R)
      PromisedResult.set_value(std::move(*R));
    else {
      ErrorAsOutParameter _(&ResolutionError);
      ResolutionError = R.takeError();
      PromisedResult.set_value(SymbolMap());
    }
  };

#else
  SymbolMap Result;
  Error ResolutionError = Error::success();

  auto NotifyComplete = [&](Expected<SymbolMap> R) {
    ErrorAsOutParameter _(&ResolutionError);
    if (R)
      Result = std::move(*R);
    else
      ResolutionError = R.takeError();
  };
#endif

  // Perform the asynchronous lookup.
  lookup(K, SearchOrder, std::move(Symbols), RequiredState, NotifyComplete,
         RegisterDependencies);

#if LLVM_ENABLE_THREADS
  auto ResultFuture = PromisedResult.get_future();
  auto Result = ResultFuture.get();

  if (ResolutionError)
    return std::move(ResolutionError);

  return std::move(Result);

#else
  if (ResolutionError)
    return std::move(ResolutionError);

  return Result;
#endif
}

Expected<ExecutorSymbolDef>
ExecutionSession::lookup(const JITDylibSearchOrder &SearchOrder,
                         SymbolStringPtr Name, SymbolState RequiredState) {
  SymbolLookupSet Names({Name});

  if (auto ResultMap = lookup(SearchOrder, std::move(Names), LookupKind::Static,
                              RequiredState, NoDependenciesToRegister)) {
    assert(ResultMap->size() == 1 && "Unexpected number of results");
    assert(ResultMap->count(Name) && "Missing result for symbol");
    return std::move(ResultMap->begin()->second);
  } else
    return ResultMap.takeError();
}

Expected<ExecutorSymbolDef>
ExecutionSession::lookup(ArrayRef<JITDylib *> SearchOrder, SymbolStringPtr Name,
                         SymbolState RequiredState) {
  return lookup(makeJITDylibSearchOrder(SearchOrder), Name, RequiredState);
}

Expected<ExecutorSymbolDef>
ExecutionSession::lookup(ArrayRef<JITDylib *> SearchOrder, StringRef Name,
                         SymbolState RequiredState) {
  return lookup(SearchOrder, intern(Name), RequiredState);
}

Error ExecutionSession::registerJITDispatchHandlers(
    JITDylib &JD, JITDispatchHandlerAssociationMap WFs) {

  auto TagAddrs = lookup({{&JD, JITDylibLookupFlags::MatchAllSymbols}},
                         SymbolLookupSet::fromMapKeys(
                             WFs, SymbolLookupFlags::WeaklyReferencedSymbol));
  if (!TagAddrs)
    return TagAddrs.takeError();

  // Associate tag addresses with implementations.
  std::lock_guard<std::mutex> Lock(JITDispatchHandlersMutex);
  for (auto &KV : *TagAddrs) {
    auto TagAddr = KV.second.getAddress();
    if (JITDispatchHandlers.count(TagAddr))
      return make_error<StringError>("Tag " + formatv("{0:x16}", TagAddr) +
                                         " (for " + *KV.first +
                                         ") already registered",
                                     inconvertibleErrorCode());
    auto I = WFs.find(KV.first);
    assert(I != WFs.end() && I->second &&
           "JITDispatchHandler implementation missing");
    JITDispatchHandlers[KV.second.getAddress()] =
        std::make_shared<JITDispatchHandlerFunction>(std::move(I->second));
    LLVM_DEBUG({
      dbgs() << "Associated function tag \"" << *KV.first << "\" ("
             << formatv("{0:x}", KV.second.getAddress()) << ") with handler\n";
    });
  }
  return Error::success();
}

void ExecutionSession::runJITDispatchHandler(SendResultFunction SendResult,
                                             ExecutorAddr HandlerFnTagAddr,
                                             ArrayRef<char> ArgBuffer) {

  std::shared_ptr<JITDispatchHandlerFunction> F;
  {
    std::lock_guard<std::mutex> Lock(JITDispatchHandlersMutex);
    auto I = JITDispatchHandlers.find(HandlerFnTagAddr);
    if (I != JITDispatchHandlers.end())
      F = I->second;
  }

  if (F)
    (*F)(std::move(SendResult), ArgBuffer.data(), ArgBuffer.size());
  else
    SendResult(shared::WrapperFunctionResult::createOutOfBandError(
        ("No function registered for tag " +
         formatv("{0:x16}", HandlerFnTagAddr))
            .str()));
}

void ExecutionSession::dump(raw_ostream &OS) {
  runSessionLocked([this, &OS]() {
    for (auto &JD : JDs)
      JD->dump(OS);
  });
}

#ifdef EXPENSIVE_CHECKS
bool ExecutionSession::verifySessionState(Twine Phase) {
  return runSessionLocked([&]() {
    bool AllOk = true;

    // We'll collect these and verify them later to avoid redundant checks.
    DenseSet<JITDylib::EmissionDepUnit *> EDUsToCheck;

    for (auto &JD : JDs) {

      auto LogFailure = [&]() -> raw_fd_ostream & {
        auto &Stream = errs();
        if (AllOk)
          Stream << "ERROR: Bad ExecutionSession state detected " << Phase
                 << "\n";
        Stream << "  In JITDylib " << JD->getName() << ", ";
        AllOk = false;
        return Stream;
      };

      if (JD->State != JITDylib::Open) {
        LogFailure()
            << "state is not Open, but JD is in ExecutionSession list.";
      }

      // Check symbol table.
      // 1. If the entry state isn't resolved then check that no address has
      //    been set.
      // 2. Check that if the hasMaterializerAttached flag is set then there is
      //    an UnmaterializedInfo entry, and vice-versa.
      for (auto &[Sym, Entry] : JD->Symbols) {
        // Check that unresolved symbols have null addresses.
        if (Entry.getState() < SymbolState::Resolved) {
          if (Entry.getAddress()) {
            LogFailure() << "symbol " << Sym << " has state "
                         << Entry.getState()
                         << " (not-yet-resolved) but non-null address "
                         << Entry.getAddress() << ".\n";
          }
        }

        // Check that the hasMaterializerAttached flag is correct.
        auto UMIItr = JD->UnmaterializedInfos.find(Sym);
        if (Entry.hasMaterializerAttached()) {
          if (UMIItr == JD->UnmaterializedInfos.end()) {
            LogFailure() << "symbol " << Sym
                         << " entry claims materializer attached, but "
                            "UnmaterializedInfos has no corresponding entry.\n";
          }
        } else if (UMIItr != JD->UnmaterializedInfos.end()) {
          LogFailure()
              << "symbol " << Sym
              << " entry claims no materializer attached, but "
                 "UnmaterializedInfos has an unexpected entry for it.\n";
        }
      }

      // Check that every UnmaterializedInfo entry has a corresponding entry
      // in the Symbols table.
      for (auto &[Sym, UMI] : JD->UnmaterializedInfos) {
        auto SymItr = JD->Symbols.find(Sym);
        if (SymItr == JD->Symbols.end()) {
          LogFailure()
              << "symbol " << Sym
              << " has UnmaterializedInfos entry, but no Symbols entry.\n";
        }
      }

      // Check consistency of the MaterializingInfos table.
      for (auto &[Sym, MII] : JD->MaterializingInfos) {

        auto SymItr = JD->Symbols.find(Sym);
        if (SymItr == JD->Symbols.end()) {
          // If there's no Symbols entry for this MaterializingInfos entry then
          // report that.
          LogFailure()
              << "symbol " << Sym
              << " has MaterializingInfos entry, but no Symbols entry.\n";
        } else {
          // Otherwise check consistency between Symbols and MaterializingInfos.

          // Ready symbols should not have MaterializingInfos.
          if (SymItr->second.getState() == SymbolState::Ready) {
            LogFailure()
                << "symbol " << Sym
                << " is in Ready state, should not have MaterializingInfo.\n";
          }

          // Pending queries should be for subsequent states.
          auto CurState = static_cast<SymbolState>(
              static_cast<std::underlying_type_t<SymbolState>>(
                  SymItr->second.getState()) + 1);
          for (auto &Q : MII.PendingQueries) {
            if (Q->getRequiredState() != CurState) {
              if (Q->getRequiredState() > CurState)
                CurState = Q->getRequiredState();
              else
                LogFailure() << "symbol " << Sym
                             << " has stale or misordered queries.\n";
            }
          }

          // If there's a DefiningEDU then check that...
          // 1. The JD matches.
          // 2. The symbol is in the EDU's Symbols map.
          // 3. The symbol table entry is in the Emitted state.
          if (MII.DefiningEDU) {

            EDUsToCheck.insert(MII.DefiningEDU.get());

            if (MII.DefiningEDU->JD != JD.get()) {
              LogFailure() << "symbol " << Sym
                           << " has DefiningEDU with incorrect JD"
                           << (llvm::is_contained(JDs, MII.DefiningEDU->JD)
                                   ? " (JD not currently in ExecutionSession"
                                   : "")
                           << "\n";
            }

            if (SymItr->second.getState() != SymbolState::Emitted) {
              LogFailure()
                  << "symbol " << Sym
                  << " has DefiningEDU, but is not in Emitted state.\n";
            }
          }

          // Check that JDs for any DependantEDUs are also in the session --
          // that guarantees that we'll also visit them during this loop.
          for (auto &DepEDU : MII.DependantEDUs) {
            if (!llvm::is_contained(JDs, DepEDU->JD)) {
              LogFailure() << "symbol " << Sym << " has DependantEDU "
                           << (void *)DepEDU << " with JD (" << DepEDU->JD
                           << ") that isn't in ExecutionSession.\n";
            }
          }
        }
      }
    }

    // Check EDUs.
    for (auto *EDU : EDUsToCheck) {
      assert(EDU->JD->State == JITDylib::Open && "EDU->JD is not Open");

      auto LogFailure = [&]() -> raw_fd_ostream & {
        AllOk = false;
        auto &Stream = errs();
        Stream << "In EDU defining " << EDU->JD->getName() << ": { ";
        for (auto &[Sym, Flags] : EDU->Symbols)
          Stream << Sym << " ";
        Stream << "}, ";
        return Stream;
      };

      if (EDU->Symbols.empty())
        LogFailure() << "no symbols defined.\n";
      else {
        for (auto &[Sym, Flags] : EDU->Symbols) {
          if (!Sym)
            LogFailure() << "null symbol defined.\n";
          else {
            if (!EDU->JD->Symbols.count(SymbolStringPtr(Sym))) {
              LogFailure() << "symbol " << Sym
                           << " isn't present in JD's symbol table.\n";
            }
          }
        }
      }

      for (auto &[DepJD, Symbols] : EDU->Dependencies) {
        if (!llvm::is_contained(JDs, DepJD)) {
          LogFailure() << "dependant symbols listed for JD that isn't in "
                          "ExecutionSession.\n";
        } else {
          for (auto &DepSym : Symbols) {
            if (!DepJD->Symbols.count(SymbolStringPtr(DepSym))) {
              LogFailure()
                  << "dependant symbol " << DepSym
                  << " does not appear in symbol table for dependant JD "
                  << DepJD->getName() << ".\n";
            }
          }
        }
      }
    }

    return AllOk;
  });
}
#endif // EXPENSIVE_CHECKS

void ExecutionSession::dispatchOutstandingMUs() {
  LLVM_DEBUG(dbgs() << "Dispatching MaterializationUnits...\n");
  while (true) {
    std::optional<std::pair<std::unique_ptr<MaterializationUnit>,
                            std::unique_ptr<MaterializationResponsibility>>>
        JMU;

    {
      std::lock_guard<std::recursive_mutex> Lock(OutstandingMUsMutex);
      if (!OutstandingMUs.empty()) {
        JMU.emplace(std::move(OutstandingMUs.back()));
        OutstandingMUs.pop_back();
      }
    }

    if (!JMU)
      break;

    assert(JMU->first && "No MU?");
    LLVM_DEBUG(dbgs() << "  Dispatching \"" << JMU->first->getName() << "\"\n");
    dispatchTask(std::make_unique<MaterializationTask>(std::move(JMU->first),
                                                       std::move(JMU->second)));
  }
  LLVM_DEBUG(dbgs() << "Done dispatching MaterializationUnits.\n");
}

Error ExecutionSession::removeResourceTracker(ResourceTracker &RT) {
  LLVM_DEBUG({
    dbgs() << "In " << RT.getJITDylib().getName() << " removing tracker "
           << formatv("{0:x}", RT.getKeyUnsafe()) << "\n";
  });
  std::vector<ResourceManager *> CurrentResourceManagers;

  JITDylib::AsynchronousSymbolQuerySet QueriesToFail;
  std::shared_ptr<SymbolDependenceMap> FailedSymbols;

  runSessionLocked([&] {
    CurrentResourceManagers = ResourceManagers;
    RT.makeDefunct();
    std::tie(QueriesToFail, FailedSymbols) =
        RT.getJITDylib().IL_removeTracker(RT);
  });

  Error Err = Error::success();

  auto &JD = RT.getJITDylib();
  for (auto *L : reverse(CurrentResourceManagers))
    Err = joinErrors(std::move(Err),
                     L->handleRemoveResources(JD, RT.getKeyUnsafe()));

  for (auto &Q : QueriesToFail)
    Q->handleFailed(
        make_error<FailedToMaterialize>(getSymbolStringPool(), FailedSymbols));

  return Err;
}

void ExecutionSession::transferResourceTracker(ResourceTracker &DstRT,
                                               ResourceTracker &SrcRT) {
  LLVM_DEBUG({
    dbgs() << "In " << SrcRT.getJITDylib().getName()
           << " transfering resources from tracker "
           << formatv("{0:x}", SrcRT.getKeyUnsafe()) << " to tracker "
           << formatv("{0:x}", DstRT.getKeyUnsafe()) << "\n";
  });

  // No-op transfers are allowed and do not invalidate the source.
  if (&DstRT == &SrcRT)
    return;

  assert(&DstRT.getJITDylib() == &SrcRT.getJITDylib() &&
         "Can't transfer resources between JITDylibs");
  runSessionLocked([&]() {
    SrcRT.makeDefunct();
    auto &JD = DstRT.getJITDylib();
    JD.transferTracker(DstRT, SrcRT);
    for (auto *L : reverse(ResourceManagers))
      L->handleTransferResources(JD, DstRT.getKeyUnsafe(),
                                 SrcRT.getKeyUnsafe());
  });
}

void ExecutionSession::destroyResourceTracker(ResourceTracker &RT) {
  runSessionLocked([&]() {
    LLVM_DEBUG({
      dbgs() << "In " << RT.getJITDylib().getName() << " destroying tracker "
             << formatv("{0:x}", RT.getKeyUnsafe()) << "\n";
    });
    if (!RT.isDefunct())
      transferResourceTracker(*RT.getJITDylib().getDefaultResourceTracker(),
                              RT);
  });
}

Error ExecutionSession::IL_updateCandidatesFor(
    JITDylib &JD, JITDylibLookupFlags JDLookupFlags,
    SymbolLookupSet &Candidates, SymbolLookupSet *NonCandidates) {
  return Candidates.forEachWithRemoval(
      [&](const SymbolStringPtr &Name,
          SymbolLookupFlags SymLookupFlags) -> Expected<bool> {
        /// Search for the symbol. If not found then continue without
        /// removal.
        auto SymI = JD.Symbols.find(Name);
        if (SymI == JD.Symbols.end())
          return false;

        // If this is a non-exported symbol and we're matching exported
        // symbols only then remove this symbol from the candidates list.
        //
        // If we're tracking non-candidates then add this to the non-candidate
        // list.
        if (!SymI->second.getFlags().isExported() &&
            JDLookupFlags == JITDylibLookupFlags::MatchExportedSymbolsOnly) {
          if (NonCandidates)
            NonCandidates->add(Name, SymLookupFlags);
          return true;
        }

        // If we match against a materialization-side-effects only symbol
        // then make sure it is weakly-referenced. Otherwise bail out with
        // an error.
        // FIXME: Use a "materialization-side-effects-only symbols must be
        // weakly referenced" specific error here to reduce confusion.
        if (SymI->second.getFlags().hasMaterializationSideEffectsOnly() &&
            SymLookupFlags != SymbolLookupFlags::WeaklyReferencedSymbol)
          return make_error<SymbolsNotFound>(getSymbolStringPool(),
                                             SymbolNameVector({Name}));

        // If we matched against this symbol but it is in the error state
        // then bail out and treat it as a failure to materialize.
        if (SymI->second.getFlags().hasError()) {
          auto FailedSymbolsMap = std::make_shared<SymbolDependenceMap>();
          (*FailedSymbolsMap)[&JD] = {Name};
          return make_error<FailedToMaterialize>(getSymbolStringPool(),
                                                 std::move(FailedSymbolsMap));
        }

        // Otherwise this is a match. Remove it from the candidate set.
        return true;
      });
}

void ExecutionSession::OL_resumeLookupAfterGeneration(
    InProgressLookupState &IPLS) {

  assert(IPLS.GenState != InProgressLookupState::NotInGenerator &&
         "Should not be called for not-in-generator lookups");
  IPLS.GenState = InProgressLookupState::NotInGenerator;

  LookupState LS;

  if (auto DG = IPLS.CurDefGeneratorStack.back().lock()) {
    IPLS.CurDefGeneratorStack.pop_back();
    std::lock_guard<std::mutex> Lock(DG->M);

    // If there are no pending lookups then mark the generator as free and
    // return.
    if (DG->PendingLookups.empty()) {
      DG->InUse = false;
      return;
    }

    // Otherwise resume the next lookup.
    LS = std::move(DG->PendingLookups.front());
    DG->PendingLookups.pop_front();
  }

  if (LS.IPLS) {
    LS.IPLS->GenState = InProgressLookupState::ResumedForGenerator;
    dispatchTask(std::make_unique<LookupTask>(std::move(LS)));
  }
}

void ExecutionSession::OL_applyQueryPhase1(
    std::unique_ptr<InProgressLookupState> IPLS, Error Err) {

  LLVM_DEBUG({
    dbgs() << "Entering OL_applyQueryPhase1:\n"
           << "  Lookup kind: " << IPLS->K << "\n"
           << "  Search order: " << IPLS->SearchOrder
           << ", Current index = " << IPLS->CurSearchOrderIndex
           << (IPLS->NewJITDylib ? " (entering new JITDylib)" : "") << "\n"
           << "  Lookup set: " << IPLS->LookupSet << "\n"
           << "  Definition generator candidates: "
           << IPLS->DefGeneratorCandidates << "\n"
           << "  Definition generator non-candidates: "
           << IPLS->DefGeneratorNonCandidates << "\n";
  });

  if (IPLS->GenState == InProgressLookupState::InGenerator)
    OL_resumeLookupAfterGeneration(*IPLS);

  assert(IPLS->GenState != InProgressLookupState::InGenerator &&
         "Lookup should not be in InGenerator state here");

  // FIXME: We should attach the query as we go: This provides a result in a
  // single pass in the common case where all symbols have already reached the
  // required state. The query could be detached again in the 'fail' method on
  // IPLS. Phase 2 would be reduced to collecting and dispatching the MUs.

  while (IPLS->CurSearchOrderIndex != IPLS->SearchOrder.size()) {

    // If we've been handed an error or received one back from a generator then
    // fail the query. We don't need to unlink: At this stage the query hasn't
    // actually been lodged.
    if (Err)
      return IPLS->fail(std::move(Err));

    // Get the next JITDylib and lookup flags.
    auto &KV = IPLS->SearchOrder[IPLS->CurSearchOrderIndex];
    auto &JD = *KV.first;
    auto JDLookupFlags = KV.second;

    LLVM_DEBUG({
      dbgs() << "Visiting \"" << JD.getName() << "\" (" << JDLookupFlags
             << ") with lookup set " << IPLS->LookupSet << ":\n";
    });

    // If we've just reached a new JITDylib then perform some setup.
    if (IPLS->NewJITDylib) {
      // Add any non-candidates from the last JITDylib (if any) back on to the
      // list of definition candidates for this JITDylib, reset definition
      // non-candidates to the empty set.
      SymbolLookupSet Tmp;
      std::swap(IPLS->DefGeneratorNonCandidates, Tmp);
      IPLS->DefGeneratorCandidates.append(std::move(Tmp));

      LLVM_DEBUG({
        dbgs() << "  First time visiting " << JD.getName()
               << ", resetting candidate sets and building generator stack\n";
      });

      // Build the definition generator stack for this JITDylib.
      runSessionLocked([&] {
        IPLS->CurDefGeneratorStack.reserve(JD.DefGenerators.size());
        for (auto &DG : reverse(JD.DefGenerators))
          IPLS->CurDefGeneratorStack.push_back(DG);
      });

      // Flag that we've done our initialization.
      IPLS->NewJITDylib = false;
    }

    // Remove any generation candidates that are already defined (and match) in
    // this JITDylib.
    runSessionLocked([&] {
      // Update the list of candidates (and non-candidates) for definition
      // generation.
      LLVM_DEBUG(dbgs() << "  Updating candidate set...\n");
      Err = IL_updateCandidatesFor(
          JD, JDLookupFlags, IPLS->DefGeneratorCandidates,
          JD.DefGenerators.empty() ? nullptr
                                   : &IPLS->DefGeneratorNonCandidates);
      LLVM_DEBUG({
        dbgs() << "    Remaining candidates = " << IPLS->DefGeneratorCandidates
               << "\n";
      });

      // If this lookup was resumed after auto-suspension but all candidates
      // have already been generated (by some previous call to the generator)
      // treat the lookup as if it had completed generation.
      if (IPLS->GenState == InProgressLookupState::ResumedForGenerator &&
          IPLS->DefGeneratorCandidates.empty())
        OL_resumeLookupAfterGeneration(*IPLS);
    });

    // If we encountered an error while filtering generation candidates then
    // bail out.
    if (Err)
      return IPLS->fail(std::move(Err));

    /// Apply any definition generators on the stack.
    LLVM_DEBUG({
      if (IPLS->CurDefGeneratorStack.empty())
        LLVM_DEBUG(dbgs() << "  No generators to run for this JITDylib.\n");
      else if (IPLS->DefGeneratorCandidates.empty())
        LLVM_DEBUG(dbgs() << "  No candidates to generate.\n");
      else
        dbgs() << "  Running " << IPLS->CurDefGeneratorStack.size()
               << " remaining generators for "
               << IPLS->DefGeneratorCandidates.size() << " candidates\n";
    });
    while (!IPLS->CurDefGeneratorStack.empty() &&
           !IPLS->DefGeneratorCandidates.empty()) {
      auto DG = IPLS->CurDefGeneratorStack.back().lock();

      if (!DG)
        return IPLS->fail(make_error<StringError>(
            "DefinitionGenerator removed while lookup in progress",
            inconvertibleErrorCode()));

      // At this point the lookup is in either the NotInGenerator state, or in
      // the ResumedForGenerator state.
      // If this lookup is in the NotInGenerator state then check whether the
      // generator is in use. If the generator is not in use then move the
      // lookup to the InGenerator state and continue. If the generator is
      // already in use then just add this lookup to the pending lookups list
      // and bail out.
      // If this lookup is in the ResumedForGenerator state then just move it
      // to InGenerator and continue.
      if (IPLS->GenState == InProgressLookupState::NotInGenerator) {
        std::lock_guard<std::mutex> Lock(DG->M);
        if (DG->InUse) {
          DG->PendingLookups.push_back(std::move(IPLS));
          return;
        }
        DG->InUse = true;
      }

      IPLS->GenState = InProgressLookupState::InGenerator;

      auto K = IPLS->K;
      auto &LookupSet = IPLS->DefGeneratorCandidates;

      // Run the generator. If the generator takes ownership of QA then this
      // will break the loop.
      {
        LLVM_DEBUG(dbgs() << "  Attempting to generate " << LookupSet << "\n");
        LookupState LS(std::move(IPLS));
        Err = DG->tryToGenerate(LS, K, JD, JDLookupFlags, LookupSet);
        IPLS = std::move(LS.IPLS);
      }

      // If the lookup returned then pop the generator stack and unblock the
      // next lookup on this generator (if any).
      if (IPLS)
        OL_resumeLookupAfterGeneration(*IPLS);

      // If there was an error then fail the query.
      if (Err) {
        LLVM_DEBUG({
          dbgs() << "  Error attempting to generate " << LookupSet << "\n";
        });
        assert(IPLS && "LS cannot be retained if error is returned");
        return IPLS->fail(std::move(Err));
      }

      // Otherwise if QA was captured then break the loop.
      if (!IPLS) {
        LLVM_DEBUG(
            { dbgs() << "  LookupState captured. Exiting phase1 for now.\n"; });
        return;
      }

      // Otherwise if we're continuing around the loop then update candidates
      // for the next round.
      runSessionLocked([&] {
        LLVM_DEBUG(dbgs() << "  Updating candidate set post-generation\n");
        Err = IL_updateCandidatesFor(
            JD, JDLookupFlags, IPLS->DefGeneratorCandidates,
            JD.DefGenerators.empty() ? nullptr
                                     : &IPLS->DefGeneratorNonCandidates);
      });

      // If updating candidates failed then fail the query.
      if (Err) {
        LLVM_DEBUG(dbgs() << "  Error encountered while updating candidates\n");
        return IPLS->fail(std::move(Err));
      }
    }

    if (IPLS->DefGeneratorCandidates.empty() &&
        IPLS->DefGeneratorNonCandidates.empty()) {
      // Early out if there are no remaining symbols.
      LLVM_DEBUG(dbgs() << "All symbols matched.\n");
      IPLS->CurSearchOrderIndex = IPLS->SearchOrder.size();
      break;
    } else {
      // If we get here then we've moved on to the next JITDylib with candidates
      // remaining.
      LLVM_DEBUG(dbgs() << "Phase 1 moving to next JITDylib.\n");
      ++IPLS->CurSearchOrderIndex;
      IPLS->NewJITDylib = true;
    }
  }

  // Remove any weakly referenced candidates that could not be found/generated.
  IPLS->DefGeneratorCandidates.remove_if(
      [](const SymbolStringPtr &Name, SymbolLookupFlags SymLookupFlags) {
        return SymLookupFlags == SymbolLookupFlags::WeaklyReferencedSymbol;
      });

  // If we get here then we've finished searching all JITDylibs.
  // If we matched all symbols then move to phase 2, otherwise fail the query
  // with a SymbolsNotFound error.
  if (IPLS->DefGeneratorCandidates.empty()) {
    LLVM_DEBUG(dbgs() << "Phase 1 succeeded.\n");
    IPLS->complete(std::move(IPLS));
  } else {
    LLVM_DEBUG(dbgs() << "Phase 1 failed with unresolved symbols.\n");
    IPLS->fail(make_error<SymbolsNotFound>(
        getSymbolStringPool(), IPLS->DefGeneratorCandidates.getSymbolNames()));
  }
}

void ExecutionSession::OL_completeLookup(
    std::unique_ptr<InProgressLookupState> IPLS,
    std::shared_ptr<AsynchronousSymbolQuery> Q,
    RegisterDependenciesFunction RegisterDependencies) {

  LLVM_DEBUG({
    dbgs() << "Entering OL_completeLookup:\n"
           << "  Lookup kind: " << IPLS->K << "\n"
           << "  Search order: " << IPLS->SearchOrder
           << ", Current index = " << IPLS->CurSearchOrderIndex
           << (IPLS->NewJITDylib ? " (entering new JITDylib)" : "") << "\n"
           << "  Lookup set: " << IPLS->LookupSet << "\n"
           << "  Definition generator candidates: "
           << IPLS->DefGeneratorCandidates << "\n"
           << "  Definition generator non-candidates: "
           << IPLS->DefGeneratorNonCandidates << "\n";
  });

  bool QueryComplete = false;
  DenseMap<JITDylib *, JITDylib::UnmaterializedInfosList> CollectedUMIs;

  auto LodgingErr = runSessionLocked([&]() -> Error {
    for (auto &KV : IPLS->SearchOrder) {
      auto &JD = *KV.first;
      auto JDLookupFlags = KV.second;
      LLVM_DEBUG({
        dbgs() << "Visiting \"" << JD.getName() << "\" (" << JDLookupFlags
               << ") with lookup set " << IPLS->LookupSet << ":\n";
      });

      auto Err = IPLS->LookupSet.forEachWithRemoval(
          [&](const SymbolStringPtr &Name,
              SymbolLookupFlags SymLookupFlags) -> Expected<bool> {
            LLVM_DEBUG({
              dbgs() << "  Attempting to match \"" << Name << "\" ("
                     << SymLookupFlags << ")... ";
            });

            /// Search for the symbol. If not found then continue without
            /// removal.
            auto SymI = JD.Symbols.find(Name);
            if (SymI == JD.Symbols.end()) {
              LLVM_DEBUG(dbgs() << "skipping: not present\n");
              return false;
            }

            // If this is a non-exported symbol and we're matching exported
            // symbols only then skip this symbol without removal.
            if (!SymI->second.getFlags().isExported() &&
                JDLookupFlags ==
                    JITDylibLookupFlags::MatchExportedSymbolsOnly) {
              LLVM_DEBUG(dbgs() << "skipping: not exported\n");
              return false;
            }

            // If we match against a materialization-side-effects only symbol
            // then make sure it is weakly-referenced. Otherwise bail out with
            // an error.
            // FIXME: Use a "materialization-side-effects-only symbols must be
            // weakly referenced" specific error here to reduce confusion.
            if (SymI->second.getFlags().hasMaterializationSideEffectsOnly() &&
                SymLookupFlags != SymbolLookupFlags::WeaklyReferencedSymbol) {
              LLVM_DEBUG({
                dbgs() << "error: "
                          "required, but symbol is has-side-effects-only\n";
              });
              return make_error<SymbolsNotFound>(getSymbolStringPool(),
                                                 SymbolNameVector({Name}));
            }

            // If we matched against this symbol but it is in the error state
            // then bail out and treat it as a failure to materialize.
            if (SymI->second.getFlags().hasError()) {
              LLVM_DEBUG(dbgs() << "error: symbol is in error state\n");
              auto FailedSymbolsMap = std::make_shared<SymbolDependenceMap>();
              (*FailedSymbolsMap)[&JD] = {Name};
              return make_error<FailedToMaterialize>(
                  getSymbolStringPool(), std::move(FailedSymbolsMap));
            }

            // Otherwise this is a match.

            // If this symbol is already in the required state then notify the
            // query, remove the symbol and continue.
            if (SymI->second.getState() >= Q->getRequiredState()) {
              LLVM_DEBUG(dbgs()
                         << "matched, symbol already in required state\n");
              Q->notifySymbolMetRequiredState(Name, SymI->second.getSymbol());
              return true;
            }

            // Otherwise this symbol does not yet meet the required state. Check
            // whether it has a materializer attached, and if so prepare to run
            // it.
            if (SymI->second.hasMaterializerAttached()) {
              assert(SymI->second.getAddress() == ExecutorAddr() &&
                     "Symbol not resolved but already has address?");
              auto UMII = JD.UnmaterializedInfos.find(Name);
              assert(UMII != JD.UnmaterializedInfos.end() &&
                     "Lazy symbol should have UnmaterializedInfo");

              auto UMI = UMII->second;
              assert(UMI->MU && "Materializer should not be null");
              assert(UMI->RT && "Tracker should not be null");
              LLVM_DEBUG({
                dbgs() << "matched, preparing to dispatch MU@" << UMI->MU.get()
                       << " (" << UMI->MU->getName() << ")\n";
              });

              // Move all symbols associated with this MaterializationUnit into
              // materializing state.
              for (auto &KV : UMI->MU->getSymbols()) {
                auto SymK = JD.Symbols.find(KV.first);
                assert(SymK != JD.Symbols.end() &&
                       "No entry for symbol covered by MaterializationUnit");
                SymK->second.setMaterializerAttached(false);
                SymK->second.setState(SymbolState::Materializing);
                JD.UnmaterializedInfos.erase(KV.first);
              }

              // Add MU to the list of MaterializationUnits to be materialized.
              CollectedUMIs[&JD].push_back(std::move(UMI));
            } else
              LLVM_DEBUG(dbgs() << "matched, registering query");

            // Add the query to the PendingQueries list and continue, deleting
            // the element from the lookup set.
            assert(SymI->second.getState() != SymbolState::NeverSearched &&
                   SymI->second.getState() != SymbolState::Ready &&
                   "By this line the symbol should be materializing");
            auto &MI = JD.MaterializingInfos[Name];
            MI.addQuery(Q);
            Q->addQueryDependence(JD, Name);

            return true;
          });

      JD.shrinkMaterializationInfoMemory();

      // Handle failure.
      if (Err) {

        LLVM_DEBUG({
          dbgs() << "Lookup failed. Detaching query and replacing MUs.\n";
        });

        // Detach the query.
        Q->detach();

        // Replace the MUs.
        for (auto &KV : CollectedUMIs) {
          auto &JD = *KV.first;
          for (auto &UMI : KV.second)
            for (auto &KV2 : UMI->MU->getSymbols()) {
              assert(!JD.UnmaterializedInfos.count(KV2.first) &&
                     "Unexpected materializer in map");
              auto SymI = JD.Symbols.find(KV2.first);
              assert(SymI != JD.Symbols.end() && "Missing symbol entry");
              assert(SymI->second.getState() == SymbolState::Materializing &&
                     "Can not replace symbol that is not materializing");
              assert(!SymI->second.hasMaterializerAttached() &&
                     "MaterializerAttached flag should not be set");
              SymI->second.setMaterializerAttached(true);
              JD.UnmaterializedInfos[KV2.first] = UMI;
            }
        }

        return Err;
      }
    }

    LLVM_DEBUG(dbgs() << "Stripping unmatched weakly-referenced symbols\n");
    IPLS->LookupSet.forEachWithRemoval(
        [&](const SymbolStringPtr &Name, SymbolLookupFlags SymLookupFlags) {
          if (SymLookupFlags == SymbolLookupFlags::WeaklyReferencedSymbol) {
            Q->dropSymbol(Name);
            return true;
          } else
            return false;
        });

    if (!IPLS->LookupSet.empty()) {
      LLVM_DEBUG(dbgs() << "Failing due to unresolved symbols\n");
      return make_error<SymbolsNotFound>(getSymbolStringPool(),
                                         IPLS->LookupSet.getSymbolNames());
    }

    // Record whether the query completed.
    QueryComplete = Q->isComplete();

    LLVM_DEBUG({
      dbgs() << "Query successfully "
             << (QueryComplete ? "completed" : "lodged") << "\n";
    });

    // Move the collected MUs to the OutstandingMUs list.
    if (!CollectedUMIs.empty()) {
      std::lock_guard<std::recursive_mutex> Lock(OutstandingMUsMutex);

      LLVM_DEBUG(dbgs() << "Adding MUs to dispatch:\n");
      for (auto &KV : CollectedUMIs) {
        LLVM_DEBUG({
          auto &JD = *KV.first;
          dbgs() << "  For " << JD.getName() << ": Adding " << KV.second.size()
                 << " MUs.\n";
        });
        for (auto &UMI : KV.second) {
          auto MR = createMaterializationResponsibility(
              *UMI->RT, std::move(UMI->MU->SymbolFlags),
              std::move(UMI->MU->InitSymbol));
          OutstandingMUs.push_back(
              std::make_pair(std::move(UMI->MU), std::move(MR)));
        }
      }
    } else
      LLVM_DEBUG(dbgs() << "No MUs to dispatch.\n");

    if (RegisterDependencies && !Q->QueryRegistrations.empty()) {
      LLVM_DEBUG(dbgs() << "Registering dependencies\n");
      RegisterDependencies(Q->QueryRegistrations);
    } else
      LLVM_DEBUG(dbgs() << "No dependencies to register\n");

    return Error::success();
  });

  if (LodgingErr) {
    LLVM_DEBUG(dbgs() << "Failing query\n");
    Q->detach();
    Q->handleFailed(std::move(LodgingErr));
    return;
  }

  if (QueryComplete) {
    LLVM_DEBUG(dbgs() << "Completing query\n");
    Q->handleComplete(*this);
  }

  dispatchOutstandingMUs();
}

void ExecutionSession::OL_completeLookupFlags(
    std::unique_ptr<InProgressLookupState> IPLS,
    unique_function<void(Expected<SymbolFlagsMap>)> OnComplete) {

  auto Result = runSessionLocked([&]() -> Expected<SymbolFlagsMap> {
    LLVM_DEBUG({
      dbgs() << "Entering OL_completeLookupFlags:\n"
             << "  Lookup kind: " << IPLS->K << "\n"
             << "  Search order: " << IPLS->SearchOrder
             << ", Current index = " << IPLS->CurSearchOrderIndex
             << (IPLS->NewJITDylib ? " (entering new JITDylib)" : "") << "\n"
             << "  Lookup set: " << IPLS->LookupSet << "\n"
             << "  Definition generator candidates: "
             << IPLS->DefGeneratorCandidates << "\n"
             << "  Definition generator non-candidates: "
             << IPLS->DefGeneratorNonCandidates << "\n";
    });

    SymbolFlagsMap Result;

    // Attempt to find flags for each symbol.
    for (auto &KV : IPLS->SearchOrder) {
      auto &JD = *KV.first;
      auto JDLookupFlags = KV.second;
      LLVM_DEBUG({
        dbgs() << "Visiting \"" << JD.getName() << "\" (" << JDLookupFlags
               << ") with lookup set " << IPLS->LookupSet << ":\n";
      });

      IPLS->LookupSet.forEachWithRemoval([&](const SymbolStringPtr &Name,
                                             SymbolLookupFlags SymLookupFlags) {
        LLVM_DEBUG({
          dbgs() << "  Attempting to match \"" << Name << "\" ("
                 << SymLookupFlags << ")... ";
        });

        // Search for the symbol. If not found then continue without removing
        // from the lookup set.
        auto SymI = JD.Symbols.find(Name);
        if (SymI == JD.Symbols.end()) {
          LLVM_DEBUG(dbgs() << "skipping: not present\n");
          return false;
        }

        // If this is a non-exported symbol then it doesn't match. Skip it.
        if (!SymI->second.getFlags().isExported() &&
            JDLookupFlags == JITDylibLookupFlags::MatchExportedSymbolsOnly) {
          LLVM_DEBUG(dbgs() << "skipping: not exported\n");
          return false;
        }

        LLVM_DEBUG({
          dbgs() << "matched, \"" << Name << "\" -> " << SymI->second.getFlags()
                 << "\n";
        });
        Result[Name] = SymI->second.getFlags();
        return true;
      });
    }

    // Remove any weakly referenced symbols that haven't been resolved.
    IPLS->LookupSet.remove_if(
        [](const SymbolStringPtr &Name, SymbolLookupFlags SymLookupFlags) {
          return SymLookupFlags == SymbolLookupFlags::WeaklyReferencedSymbol;
        });

    if (!IPLS->LookupSet.empty()) {
      LLVM_DEBUG(dbgs() << "Failing due to unresolved symbols\n");
      return make_error<SymbolsNotFound>(getSymbolStringPool(),
                                         IPLS->LookupSet.getSymbolNames());
    }

    LLVM_DEBUG(dbgs() << "Succeded, result = " << Result << "\n");
    return Result;
  });

  // Run the callback on the result.
  LLVM_DEBUG(dbgs() << "Sending result to handler.\n");
  OnComplete(std::move(Result));
}

void ExecutionSession::OL_destroyMaterializationResponsibility(
    MaterializationResponsibility &MR) {

  assert(MR.SymbolFlags.empty() &&
         "All symbols should have been explicitly materialized or failed");
  MR.JD.unlinkMaterializationResponsibility(MR);
}

SymbolNameSet ExecutionSession::OL_getRequestedSymbols(
    const MaterializationResponsibility &MR) {
  return MR.JD.getRequestedSymbols(MR.SymbolFlags);
}

Error ExecutionSession::OL_notifyResolved(MaterializationResponsibility &MR,
                                          const SymbolMap &Symbols) {
  LLVM_DEBUG({
    dbgs() << "In " << MR.JD.getName() << " resolving " << Symbols << "\n";
  });
#ifndef NDEBUG
  for (auto &KV : Symbols) {
    auto I = MR.SymbolFlags.find(KV.first);
    assert(I != MR.SymbolFlags.end() &&
           "Resolving symbol outside this responsibility set");
    assert(!I->second.hasMaterializationSideEffectsOnly() &&
           "Can't resolve materialization-side-effects-only symbol");
    assert((KV.second.getFlags() & ~JITSymbolFlags::Common) ==
               (I->second & ~JITSymbolFlags::Common) &&
           "Resolving symbol with incorrect flags");
  }
#endif

  return MR.JD.resolve(MR, Symbols);
}

template <typename HandleNewDepFn>
void ExecutionSession::propagateExtraEmitDeps(
    std::deque<JITDylib::EmissionDepUnit *> Worklist, EDUInfosMap &EDUInfos,
    HandleNewDepFn HandleNewDep) {

  // Iterate to a fixed-point to propagate extra-emit dependencies through the
  // EDU graph.
  while (!Worklist.empty()) {
    auto &EDU = *Worklist.front();
    Worklist.pop_front();

    assert(EDUInfos.count(&EDU) && "No info entry for EDU");
    auto &EDUInfo = EDUInfos[&EDU];

    // Propagate new dependencies to users.
    for (auto *UserEDU : EDUInfo.IntraEmitUsers) {

      // UserEDUInfo only present if UserEDU has its own users.
      JITDylib::EmissionDepUnitInfo *UserEDUInfo = nullptr;
      {
        auto UserEDUInfoItr = EDUInfos.find(UserEDU);
        if (UserEDUInfoItr != EDUInfos.end())
          UserEDUInfo = &UserEDUInfoItr->second;
      }

      for (auto &[DepJD, Deps] : EDUInfo.NewDeps) {
        auto &UserEDUDepsForJD = UserEDU->Dependencies[DepJD];
        DenseSet<NonOwningSymbolStringPtr> *UserEDUNewDepsForJD = nullptr;
        for (auto Dep : Deps) {
          if (UserEDUDepsForJD.insert(Dep).second) {
            HandleNewDep(*UserEDU, *DepJD, Dep);
            if (UserEDUInfo) {
              if (!UserEDUNewDepsForJD) {
                // If UserEDU has no new deps then it's not in the worklist
                // yet, so add it.
                if (UserEDUInfo->NewDeps.empty())
                  Worklist.push_back(UserEDU);
                UserEDUNewDepsForJD = &UserEDUInfo->NewDeps[DepJD];
              }
              // Add (DepJD, Dep) to NewDeps.
              UserEDUNewDepsForJD->insert(Dep);
            }
          }
        }
      }
    }

    EDUInfo.NewDeps.clear();
  }
}

// Note: This method modifies the emitted set.
ExecutionSession::EDUInfosMap ExecutionSession::simplifyDepGroups(
    MaterializationResponsibility &MR,
    ArrayRef<SymbolDependenceGroup> EmittedDeps) {

  auto &TargetJD = MR.getTargetJITDylib();

  // 1. Build initial EmissionDepUnit -> EmissionDepUnitInfo and
  //    Symbol -> EmissionDepUnit mappings.
  DenseMap<JITDylib::EmissionDepUnit *, JITDylib::EmissionDepUnitInfo> EDUInfos;
  EDUInfos.reserve(EmittedDeps.size());
  DenseMap<NonOwningSymbolStringPtr, JITDylib::EmissionDepUnit *> EDUForSymbol;
  for (auto &DG : EmittedDeps) {
    assert(!DG.Symbols.empty() && "DepGroup does not cover any symbols");

    // Skip empty EDUs.
    if (DG.Dependencies.empty())
      continue;

    auto TmpEDU = std::make_shared<JITDylib::EmissionDepUnit>(TargetJD);
    auto &EDUInfo = EDUInfos[TmpEDU.get()];
    EDUInfo.EDU = std::move(TmpEDU);
    for (const auto &Symbol : DG.Symbols) {
      NonOwningSymbolStringPtr NonOwningSymbol(Symbol);
      assert(!EDUForSymbol.count(NonOwningSymbol) &&
             "Symbol should not appear in more than one SymbolDependenceGroup");
      assert(MR.getSymbols().count(Symbol) &&
             "Symbol in DepGroups not in the emitted set");
      auto NewlyEmittedItr = MR.getSymbols().find(Symbol);
      EDUInfo.EDU->Symbols[NonOwningSymbol] = NewlyEmittedItr->second;
      EDUForSymbol[NonOwningSymbol] = EDUInfo.EDU.get();
    }
  }

  // 2. Build a "residual" EDU to cover all symbols that have no dependencies.
  {
    DenseMap<NonOwningSymbolStringPtr, JITSymbolFlags> ResidualSymbolFlags;
    for (auto &[Sym, Flags] : MR.getSymbols()) {
      if (!EDUForSymbol.count(NonOwningSymbolStringPtr(Sym)))
        ResidualSymbolFlags[NonOwningSymbolStringPtr(Sym)] = Flags;
    }
    if (!ResidualSymbolFlags.empty()) {
      auto ResidualEDU = std::make_shared<JITDylib::EmissionDepUnit>(TargetJD);
      ResidualEDU->Symbols = std::move(ResidualSymbolFlags);
      auto &ResidualEDUInfo = EDUInfos[ResidualEDU.get()];
      ResidualEDUInfo.EDU = std::move(ResidualEDU);

      // If the residual EDU is the only one then bail out early.
      if (EDUInfos.size() == 1)
        return EDUInfos;

      // Otherwise add the residual EDU to the EDUForSymbol map.
      for (auto &[Sym, Flags] : ResidualEDUInfo.EDU->Symbols)
        EDUForSymbol[Sym] = ResidualEDUInfo.EDU.get();
    }
  }

#ifndef NDEBUG
  assert(EDUForSymbol.size() == MR.getSymbols().size() &&
         "MR symbols not fully covered by EDUs?");
  for (auto &[Sym, Flags] : MR.getSymbols()) {
    assert(EDUForSymbol.count(NonOwningSymbolStringPtr(Sym)) &&
           "Sym in MR not covered by EDU");
  }
#endif // NDEBUG

  // 3. Use the DepGroups array to build a graph of dependencies between
  //    EmissionDepUnits in this finalization. We want to remove these
  //    intra-finalization uses, propagating dependencies on symbols outside
  //    this finalization. Add EDUs to the worklist.
  for (auto &DG : EmittedDeps) {

    // Skip SymbolDependenceGroups with no dependencies.
    if (DG.Dependencies.empty())
      continue;

    assert(EDUForSymbol.count(NonOwningSymbolStringPtr(*DG.Symbols.begin())) &&
           "No EDU for DG");
    auto &EDU =
        *EDUForSymbol.find(NonOwningSymbolStringPtr(*DG.Symbols.begin()))
             ->second;

    for (auto &[DepJD, Deps] : DG.Dependencies) {
      DenseSet<NonOwningSymbolStringPtr> NewDepsForJD;

      assert(!Deps.empty() && "Dependence set for DepJD is empty");

      if (DepJD != &TargetJD) {
        // DepJD is some other JITDylib.There can't be any intra-finalization
        // edges here, so just skip.
        for (auto &Dep : Deps)
          NewDepsForJD.insert(NonOwningSymbolStringPtr(Dep));
      } else {
        // DepJD is the Target JITDylib. Check for intra-finaliztaion edges,
        // skipping any and recording the intra-finalization use instead.
        for (auto &Dep : Deps) {
          NonOwningSymbolStringPtr NonOwningDep(Dep);
          auto I = EDUForSymbol.find(NonOwningDep);
          if (I == EDUForSymbol.end()) {
            if (!MR.getSymbols().count(Dep))
              NewDepsForJD.insert(NonOwningDep);
            continue;
          }

          if (I->second != &EDU)
            EDUInfos[I->second].IntraEmitUsers.insert(&EDU);
        }
      }

      if (!NewDepsForJD.empty())
        EDU.Dependencies[DepJD] = std::move(NewDepsForJD);
    }
  }

  // 4. Build the worklist.
  std::deque<JITDylib::EmissionDepUnit *> Worklist;
  for (auto &[EDU, EDUInfo] : EDUInfos) {
    // If this EDU has extra-finalization dependencies and intra-finalization
    // users then add it to the worklist.
    if (!EDU->Dependencies.empty()) {
      auto I = EDUInfos.find(EDU);
      if (I != EDUInfos.end()) {
        auto &EDUInfo = I->second;
        if (!EDUInfo.IntraEmitUsers.empty()) {
          EDUInfo.NewDeps = EDU->Dependencies;
          Worklist.push_back(EDU);
        }
      }
    }
  }

  // 4. Propagate dependencies through the EDU graph.
  propagateExtraEmitDeps(
      Worklist, EDUInfos,
      [](JITDylib::EmissionDepUnit &, JITDylib &, NonOwningSymbolStringPtr) {});

  return EDUInfos;
}

void ExecutionSession::IL_makeEDUReady(
    std::shared_ptr<JITDylib::EmissionDepUnit> EDU,
    JITDylib::AsynchronousSymbolQuerySet &Queries) {

  // The symbols for this EDU are ready.
  auto &JD = *EDU->JD;

  for (auto &[Sym, Flags] : EDU->Symbols) {
    assert(JD.Symbols.count(SymbolStringPtr(Sym)) &&
           "JD does not have an entry for Sym");
    auto &Entry = JD.Symbols[SymbolStringPtr(Sym)];

    assert(((Entry.getFlags().hasMaterializationSideEffectsOnly() &&
             Entry.getState() == SymbolState::Materializing) ||
            Entry.getState() == SymbolState::Resolved ||
            Entry.getState() == SymbolState::Emitted) &&
           "Emitting from state other than Resolved");

    Entry.setState(SymbolState::Ready);

    auto MII = JD.MaterializingInfos.find(SymbolStringPtr(Sym));

    // Check for pending queries.
    if (MII == JD.MaterializingInfos.end())
      continue;
    auto &MI = MII->second;

    for (auto &Q : MI.takeQueriesMeeting(SymbolState::Ready)) {
      Q->notifySymbolMetRequiredState(SymbolStringPtr(Sym), Entry.getSymbol());
      if (Q->isComplete())
        Queries.insert(Q);
      Q->removeQueryDependence(JD, SymbolStringPtr(Sym));
    }

    JD.MaterializingInfos.erase(MII);
  }

  JD.shrinkMaterializationInfoMemory();
}

void ExecutionSession::IL_makeEDUEmitted(
    std::shared_ptr<JITDylib::EmissionDepUnit> EDU,
    JITDylib::AsynchronousSymbolQuerySet &Queries) {

  // The symbols for this EDU are emitted, but not ready.
  auto &JD = *EDU->JD;

  for (auto &[Sym, Flags] : EDU->Symbols) {
    assert(JD.Symbols.count(SymbolStringPtr(Sym)) &&
           "JD does not have an entry for Sym");
    auto &Entry = JD.Symbols[SymbolStringPtr(Sym)];

    assert(((Entry.getFlags().hasMaterializationSideEffectsOnly() &&
             Entry.getState() == SymbolState::Materializing) ||
            Entry.getState() == SymbolState::Resolved ||
            Entry.getState() == SymbolState::Emitted) &&
           "Emitting from state other than Resolved");

    if (Entry.getState() == SymbolState::Emitted) {
      // This was already emitted, so we can skip the rest of this loop.
#ifndef NDEBUG
      for (auto &[Sym, Flags] : EDU->Symbols) {
        assert(JD.Symbols.count(SymbolStringPtr(Sym)) &&
               "JD does not have an entry for Sym");
        auto &Entry = JD.Symbols[SymbolStringPtr(Sym)];
        assert(Entry.getState() == SymbolState::Emitted &&
               "Symbols for EDU in inconsistent state");
        assert(JD.MaterializingInfos.count(SymbolStringPtr(Sym)) &&
               "Emitted symbol has no MI");
        auto MI = JD.MaterializingInfos[SymbolStringPtr(Sym)];
        assert(MI.takeQueriesMeeting(SymbolState::Emitted).empty() &&
               "Already-emitted symbol has waiting-on-emitted queries");
      }
#endif // NDEBUG
      break;
    }

    Entry.setState(SymbolState::Emitted);
    auto &MI = JD.MaterializingInfos[SymbolStringPtr(Sym)];
    MI.DefiningEDU = EDU;

    for (auto &Q : MI.takeQueriesMeeting(SymbolState::Emitted)) {
      Q->notifySymbolMetRequiredState(SymbolStringPtr(Sym), Entry.getSymbol());
      if (Q->isComplete())
        Queries.insert(Q);
      Q->removeQueryDependence(JD, SymbolStringPtr(Sym));
    }
  }

  for (auto &[DepJD, Deps] : EDU->Dependencies) {
    for (auto &Dep : Deps)
      DepJD->MaterializingInfos[SymbolStringPtr(Dep)].DependantEDUs.insert(
          EDU.get());
  }
}

/// Removes the given dependence from EDU. If EDU's dependence set becomes
/// empty then this function adds an entry for it to the EDUInfos map.
/// Returns true if a new EDUInfosMap entry is added.
bool ExecutionSession::IL_removeEDUDependence(JITDylib::EmissionDepUnit &EDU,
                                              JITDylib &DepJD,
                                              NonOwningSymbolStringPtr DepSym,
                                              EDUInfosMap &EDUInfos) {
  assert(EDU.Dependencies.count(&DepJD) &&
         "JD does not appear in Dependencies of DependantEDU");
  assert(EDU.Dependencies[&DepJD].count(DepSym) &&
         "Symbol does not appear in Dependencies of DependantEDU");
  auto &JDDeps = EDU.Dependencies[&DepJD];
  JDDeps.erase(DepSym);
  if (JDDeps.empty()) {
    EDU.Dependencies.erase(&DepJD);
    if (EDU.Dependencies.empty()) {
      // If the dependencies set has become empty then EDU _may_ be ready
      // (we won't know for sure until we've propagated the extra-emit deps).
      // Create an EDUInfo for it (if it doesn't have one already) so that
      // it'll be visited after propagation.
      auto &DepEDUInfo = EDUInfos[&EDU];
      if (!DepEDUInfo.EDU) {
        assert(EDU.JD->Symbols.count(
                   SymbolStringPtr(EDU.Symbols.begin()->first)) &&
               "Missing symbol entry for first symbol in EDU");
        auto DepEDUFirstMI = EDU.JD->MaterializingInfos.find(
            SymbolStringPtr(EDU.Symbols.begin()->first));
        assert(DepEDUFirstMI != EDU.JD->MaterializingInfos.end() &&
               "Missing MI for first symbol in DependantEDU");
        DepEDUInfo.EDU = DepEDUFirstMI->second.DefiningEDU;
        return true;
      }
    }
  }
  return false;
}

Error ExecutionSession::makeJDClosedError(JITDylib::EmissionDepUnit &EDU,
                                          JITDylib &ClosedJD) {
  SymbolNameSet FailedSymbols;
  for (auto &[Sym, Flags] : EDU.Symbols)
    FailedSymbols.insert(SymbolStringPtr(Sym));
  SymbolDependenceMap BadDeps;
  for (auto &Dep : EDU.Dependencies[&ClosedJD])
    BadDeps[&ClosedJD].insert(SymbolStringPtr(Dep));
  return make_error<UnsatisfiedSymbolDependencies>(
      ClosedJD.getExecutionSession().getSymbolStringPool(), EDU.JD,
      std::move(FailedSymbols), std::move(BadDeps),
      ClosedJD.getName() + " is closed");
}

Error ExecutionSession::makeUnsatisfiedDepsError(JITDylib::EmissionDepUnit &EDU,
                                                 JITDylib &BadJD,
                                                 SymbolNameSet BadDeps) {
  SymbolNameSet FailedSymbols;
  for (auto &[Sym, Flags] : EDU.Symbols)
    FailedSymbols.insert(SymbolStringPtr(Sym));
  SymbolDependenceMap BadDepsMap;
  BadDepsMap[&BadJD] = std::move(BadDeps);
  return make_error<UnsatisfiedSymbolDependencies>(
      BadJD.getExecutionSession().getSymbolStringPool(), &BadJD,
      std::move(FailedSymbols), std::move(BadDepsMap),
      "dependencies removed or in error state");
}

Expected<JITDylib::AsynchronousSymbolQuerySet>
ExecutionSession::IL_emit(MaterializationResponsibility &MR,
                          EDUInfosMap EDUInfos) {

  if (MR.RT->isDefunct())
    return make_error<ResourceTrackerDefunct>(MR.RT);

  auto &TargetJD = MR.getTargetJITDylib();
  if (TargetJD.State != JITDylib::Open)
    return make_error<StringError>("JITDylib " + TargetJD.getName() +
                                       " is defunct",
                                   inconvertibleErrorCode());
#ifdef EXPENSIVE_CHECKS
  verifySessionState("entering ExecutionSession::IL_emit");
#endif

  // Walk all EDUs:
  // 1. Verifying that dependencies are available (not removed or in the error
  //    state.
  // 2. Removing any dependencies that are already Ready.
  // 3. Lifting any EDUs for Emitted symbols into the EDUInfos map.
  // 4. Finding any dependant EDUs and lifting them into the EDUInfos map.
  std::deque<JITDylib::EmissionDepUnit *> Worklist;
  for (auto &[EDU, _] : EDUInfos)
    Worklist.push_back(EDU);

  for (auto *EDU : Worklist) {
    auto *EDUInfo = &EDUInfos[EDU];

    SmallVector<JITDylib *> DepJDsToRemove;
    for (auto &[DepJD, Deps] : EDU->Dependencies) {
      if (DepJD->State != JITDylib::Open)
        return makeJDClosedError(*EDU, *DepJD);

      SymbolNameSet BadDeps;
      SmallVector<NonOwningSymbolStringPtr> DepsToRemove;
      for (auto &Dep : Deps) {
        auto DepEntryItr = DepJD->Symbols.find(SymbolStringPtr(Dep));

        // If this dep has been removed or moved to the error state then add it
        // to the bad deps set. We aggregate these bad deps for more
        // comprehensive error messages.
        if (DepEntryItr == DepJD->Symbols.end() ||
            DepEntryItr->second.getFlags().hasError()) {
          BadDeps.insert(SymbolStringPtr(Dep));
          continue;
        }

        // If this dep isn't emitted yet then just add it to the NewDeps set to
        // be propagated.
        auto &DepEntry = DepEntryItr->second;
        if (DepEntry.getState() < SymbolState::Emitted) {
          EDUInfo->NewDeps[DepJD].insert(Dep);
          continue;
        }

        // This dep has been emitted, so add it to the list to be removed from
        // EDU.
        DepsToRemove.push_back(Dep);

        // If Dep is Ready then there's nothing further to do.
        if (DepEntry.getState() == SymbolState::Ready) {
          assert(!DepJD->MaterializingInfos.count(SymbolStringPtr(Dep)) &&
                 "Unexpected MaterializationInfo attached to ready symbol");
          continue;
        }

        // If we get here thene Dep is Emitted. We need to look up its defining
        // EDU and add this EDU to the defining EDU's list of users (this means
        // creating an EDUInfos entry if the defining EDU doesn't have one
        // already).
        assert(DepJD->MaterializingInfos.count(SymbolStringPtr(Dep)) &&
               "Expected MaterializationInfo for emitted dependency");
        auto &DepMI = DepJD->MaterializingInfos[SymbolStringPtr(Dep)];
        assert(DepMI.DefiningEDU &&
               "Emitted symbol does not have a defining EDU");
        assert(!DepMI.DefiningEDU->Dependencies.empty() &&
               "Emitted symbol has empty dependencies (should be ready)");
        assert(DepMI.DependantEDUs.empty() &&
               "Already-emitted symbol has dependant EDUs?");
        auto &DepEDUInfo = EDUInfos[DepMI.DefiningEDU.get()];
        if (!DepEDUInfo.EDU) {
          // No EDUInfo yet -- build initial entry, and reset the EDUInfo
          // pointer, which we will have invalidated.
          EDUInfo = &EDUInfos[EDU];
          DepEDUInfo.EDU = DepMI.DefiningEDU;
          for (auto &[DepDepJD, DepDeps] : DepEDUInfo.EDU->Dependencies) {
            if (DepDepJD == &TargetJD) {
              for (auto &DepDep : DepDeps)
                if (!MR.getSymbols().count(SymbolStringPtr(DepDep)))
                  DepEDUInfo.NewDeps[DepDepJD].insert(DepDep);
            } else
              DepEDUInfo.NewDeps[DepDepJD] = DepDeps;
          }
        }
        DepEDUInfo.IntraEmitUsers.insert(EDU);
      }

      // Some dependencies were removed or in an error state -- error out.
      if (!BadDeps.empty())
        return makeUnsatisfiedDepsError(*EDU, *DepJD, std::move(BadDeps));

      // Remove the emitted / ready deps from DepJD.
      for (auto &Dep : DepsToRemove)
        Deps.erase(Dep);

      // If there are no further deps in DepJD then flag it for removal too.
      if (Deps.empty())
        DepJDsToRemove.push_back(DepJD);
    }

    // Remove any JDs whose dependence sets have become empty.
    for (auto &DepJD : DepJDsToRemove) {
      assert(EDU->Dependencies.count(DepJD) &&
             "Trying to remove non-existent dep entries");
      EDU->Dependencies.erase(DepJD);
    }

    // Now look for users of this EDU.
    for (auto &[Sym, Flags] : EDU->Symbols) {
      assert(TargetJD.Symbols.count(SymbolStringPtr(Sym)) &&
             "Sym not present in symbol table");
      assert((TargetJD.Symbols[SymbolStringPtr(Sym)].getState() ==
                  SymbolState::Resolved ||
              TargetJD.Symbols[SymbolStringPtr(Sym)]
                  .getFlags()
                  .hasMaterializationSideEffectsOnly()) &&
             "Emitting symbol not in the resolved state");
      assert(!TargetJD.Symbols[SymbolStringPtr(Sym)].getFlags().hasError() &&
             "Symbol is already in an error state");

      auto MII = TargetJD.MaterializingInfos.find(SymbolStringPtr(Sym));
      if (MII == TargetJD.MaterializingInfos.end() ||
          MII->second.DependantEDUs.empty())
        continue;

      for (auto &DependantEDU : MII->second.DependantEDUs) {
        if (IL_removeEDUDependence(*DependantEDU, TargetJD, Sym, EDUInfos))
          EDUInfo = &EDUInfos[EDU];
        EDUInfo->IntraEmitUsers.insert(DependantEDU);
      }
      MII->second.DependantEDUs.clear();
    }
  }

  Worklist.clear();
  for (auto &[EDU, EDUInfo] : EDUInfos) {
    if (!EDUInfo.IntraEmitUsers.empty() && !EDU->Dependencies.empty()) {
      if (EDUInfo.NewDeps.empty())
        EDUInfo.NewDeps = EDU->Dependencies;
      Worklist.push_back(EDU);
    }
  }

  propagateExtraEmitDeps(
      Worklist, EDUInfos,
      [](JITDylib::EmissionDepUnit &EDU, JITDylib &JD,
         NonOwningSymbolStringPtr Sym) {
        JD.MaterializingInfos[SymbolStringPtr(Sym)].DependantEDUs.insert(&EDU);
      });

  JITDylib::AsynchronousSymbolQuerySet CompletedQueries;

  // Extract completed queries and lodge not-yet-ready EDUs in the
  // session.
  for (auto &[EDU, EDUInfo] : EDUInfos) {
    if (EDU->Dependencies.empty())
      IL_makeEDUReady(std::move(EDUInfo.EDU), CompletedQueries);
    else
      IL_makeEDUEmitted(std::move(EDUInfo.EDU), CompletedQueries);
  }

#ifdef EXPENSIVE_CHECKS
  verifySessionState("exiting ExecutionSession::IL_emit");
#endif

  return std::move(CompletedQueries);
}

Error ExecutionSession::OL_notifyEmitted(
    MaterializationResponsibility &MR,
    ArrayRef<SymbolDependenceGroup> DepGroups) {
  LLVM_DEBUG({
    dbgs() << "In " << MR.JD.getName() << " emitting " << MR.SymbolFlags
           << "\n";
    if (!DepGroups.empty()) {
      dbgs() << "  Initial dependencies:\n";
      for (auto &SDG : DepGroups) {
        dbgs() << "    Symbols: " << SDG.Symbols
               << ", Dependencies: " << SDG.Dependencies << "\n";
      }
    }
  });

#ifndef NDEBUG
  SymbolNameSet Visited;
  for (auto &DG : DepGroups) {
    for (auto &Sym : DG.Symbols) {
      assert(MR.SymbolFlags.count(Sym) &&
             "DG contains dependence for symbol outside this MR");
      assert(Visited.insert(Sym).second &&
             "DG contains duplicate entries for Name");
    }
  }
#endif // NDEBUG

  auto EDUInfos = simplifyDepGroups(MR, DepGroups);

  LLVM_DEBUG({
    dbgs() << "  Simplified dependencies:\n";
    for (auto &[EDU, EDUInfo] : EDUInfos) {
      dbgs() << "    Symbols: { ";
      for (auto &[Sym, Flags] : EDU->Symbols)
        dbgs() << Sym << " ";
      dbgs() << "}, Dependencies: { ";
      for (auto &[DepJD, Deps] : EDU->Dependencies) {
        dbgs() << "(" << DepJD->getName() << ", { ";
        for (auto &Dep : Deps)
          dbgs() << Dep << " ";
        dbgs() << "}) ";
      }
      dbgs() << "}\n";
    }
  });

  auto CompletedQueries =
      runSessionLocked([&]() { return IL_emit(MR, EDUInfos); });

  // On error bail out.
  if (!CompletedQueries)
    return CompletedQueries.takeError();

  MR.SymbolFlags.clear();

  // Otherwise notify all the completed queries.
  for (auto &Q : *CompletedQueries) {
    assert(Q->isComplete() && "Q is not complete");
    Q->handleComplete(*this);
  }

  return Error::success();
}

Error ExecutionSession::OL_defineMaterializing(
    MaterializationResponsibility &MR, SymbolFlagsMap NewSymbolFlags) {

  LLVM_DEBUG({
    dbgs() << "In " << MR.JD.getName() << " defining materializing symbols "
           << NewSymbolFlags << "\n";
  });
  if (auto AcceptedDefs =
          MR.JD.defineMaterializing(MR, std::move(NewSymbolFlags))) {
    // Add all newly accepted symbols to this responsibility object.
    for (auto &KV : *AcceptedDefs)
      MR.SymbolFlags.insert(KV);
    return Error::success();
  } else
    return AcceptedDefs.takeError();
}

std::pair<JITDylib::AsynchronousSymbolQuerySet,
          std::shared_ptr<SymbolDependenceMap>>
ExecutionSession::IL_failSymbols(JITDylib &JD,
                                 const SymbolNameVector &SymbolsToFail) {

#ifdef EXPENSIVE_CHECKS
  verifySessionState("entering ExecutionSession::IL_failSymbols");
#endif

  JITDylib::AsynchronousSymbolQuerySet FailedQueries;
  auto FailedSymbolsMap = std::make_shared<SymbolDependenceMap>();
  auto ExtractFailedQueries = [&](JITDylib::MaterializingInfo &MI) {
    JITDylib::AsynchronousSymbolQueryList ToDetach;
    for (auto &Q : MI.pendingQueries()) {
      // Add the query to the list to be failed and detach it.
      FailedQueries.insert(Q);
      ToDetach.push_back(Q);
    }
    for (auto &Q : ToDetach)
      Q->detach();
    assert(!MI.hasQueriesPending() && "Queries still pending after detach");
  };

  for (auto &Name : SymbolsToFail) {
    (*FailedSymbolsMap)[&JD].insert(Name);

    // Look up the symbol to fail.
    auto SymI = JD.Symbols.find(Name);

    // FIXME: Revisit this. We should be able to assert sequencing between
    //        ResourceTracker removal and symbol failure.
    //
    // It's possible that this symbol has already been removed, e.g. if a
    // materialization failure happens concurrently with a ResourceTracker or
    // JITDylib removal. In that case we can safely skip this symbol and
    // continue.
    if (SymI == JD.Symbols.end())
      continue;
    auto &Sym = SymI->second;

    // If the symbol is already in the error state then we must have visited
    // it earlier.
    if (Sym.getFlags().hasError()) {
      assert(!JD.MaterializingInfos.count(Name) &&
             "Symbol in error state still has MaterializingInfo");
      continue;
    }

    // Move the symbol into the error state.
    Sym.setFlags(Sym.getFlags() | JITSymbolFlags::HasError);

    // FIXME: Come up with a sane mapping of state to
    // presence-of-MaterializingInfo so that we can assert presence / absence
    // here, rather than testing it.
    auto MII = JD.MaterializingInfos.find(Name);
    if (MII == JD.MaterializingInfos.end())
      continue;

    auto &MI = MII->second;

    // Collect queries to be failed for this MII.
    ExtractFailedQueries(MI);

    if (MI.DefiningEDU) {
      // If there is a DefiningEDU for this symbol then remove this
      // symbol from it.
      assert(MI.DependantEDUs.empty() &&
             "Symbol with DefiningEDU should not have DependantEDUs");
      assert(Sym.getState() >= SymbolState::Emitted &&
             "Symbol has EDU, should have been emitted");
      assert(MI.DefiningEDU->Symbols.count(NonOwningSymbolStringPtr(Name)) &&
             "Symbol does not appear in its DefiningEDU");
      MI.DefiningEDU->Symbols.erase(NonOwningSymbolStringPtr(Name));

      // Remove this EDU from the dependants lists of its dependencies.
      for (auto &[DepJD, DepSyms] : MI.DefiningEDU->Dependencies) {
        for (auto DepSym : DepSyms) {
          assert(DepJD->Symbols.count(SymbolStringPtr(DepSym)) &&
                 "DepSym not in DepJD");
          assert(DepJD->MaterializingInfos.count(SymbolStringPtr(DepSym)) &&
                 "DepSym has not MaterializingInfo");
          auto &SymMI = DepJD->MaterializingInfos[SymbolStringPtr(DepSym)];
          assert(SymMI.DependantEDUs.count(MI.DefiningEDU.get()) &&
                 "DefiningEDU missing from DependantEDUs list of dependency");
          SymMI.DependantEDUs.erase(MI.DefiningEDU.get());
        }
      }

      MI.DefiningEDU = nullptr;
    } else {
      // Otherwise if there are any EDUs waiting on this symbol then move
      // those symbols to the error state too, and deregister them from the
      // symbols that they depend on.
      // Note: We use a copy of DependantEDUs here since we'll be removing
      // from the original set as we go.
      for (auto &DependantEDU : MI.DependantEDUs) {

        // Remove DependantEDU from all of its users DependantEDUs lists.
        for (auto &[DepJD, DepSyms] : DependantEDU->Dependencies) {
          for (auto DepSym : DepSyms) {
            // Skip self-reference to avoid invalidating the MI.DependantEDUs
            // map. We'll clear this later.
            if (DepJD == &JD && DepSym == Name)
              continue;
            assert(DepJD->Symbols.count(SymbolStringPtr(DepSym)) &&
                   "DepSym not in DepJD?");
            assert(DepJD->MaterializingInfos.count(SymbolStringPtr(DepSym)) &&
                   "DependantEDU not registered with symbol it depends on");
            auto &SymMI = DepJD->MaterializingInfos[SymbolStringPtr(DepSym)];
            assert(SymMI.DependantEDUs.count(DependantEDU) &&
                   "DependantEDU missing from DependantEDUs list");
            SymMI.DependantEDUs.erase(DependantEDU);
          }
        }

        // Move any symbols defined by DependantEDU into the error state and
        // fail any queries waiting on them.
        auto &DepJD = *DependantEDU->JD;
        auto DepEDUSymbols = std::move(DependantEDU->Symbols);
        for (auto &[DepName, Flags] : DepEDUSymbols) {
          auto DepSymItr = DepJD.Symbols.find(SymbolStringPtr(DepName));
          assert(DepSymItr != DepJD.Symbols.end() &&
                 "Symbol not present in table");
          auto &DepSym = DepSymItr->second;

          assert(DepSym.getState() >= SymbolState::Emitted &&
                 "Symbol has EDU, should have been emitted");
          assert(!DepSym.getFlags().hasError() &&
                 "Symbol is already in the error state?");
          DepSym.setFlags(DepSym.getFlags() | JITSymbolFlags::HasError);
          (*FailedSymbolsMap)[&DepJD].insert(SymbolStringPtr(DepName));

          // This symbol has a defining EDU so its MaterializingInfo object must
          // exist.
          auto DepMIItr =
              DepJD.MaterializingInfos.find(SymbolStringPtr(DepName));
          assert(DepMIItr != DepJD.MaterializingInfos.end() &&
                 "Symbol has defining EDU but not MaterializingInfo");
          auto &DepMI = DepMIItr->second;
          assert(DepMI.DefiningEDU.get() == DependantEDU &&
                 "Bad EDU dependence edge");
          assert(DepMI.DependantEDUs.empty() &&
                 "Symbol was emitted, should not have any DependantEDUs");
          ExtractFailedQueries(DepMI);
          DepJD.MaterializingInfos.erase(SymbolStringPtr(DepName));
        }

        DepJD.shrinkMaterializationInfoMemory();
      }

      MI.DependantEDUs.clear();
    }

    assert(!MI.DefiningEDU && "DefiningEDU should have been reset");
    assert(MI.DependantEDUs.empty() &&
           "DependantEDUs should have been removed above");
    assert(!MI.hasQueriesPending() &&
           "Can not delete MaterializingInfo with queries pending");
    JD.MaterializingInfos.erase(Name);
  }

  JD.shrinkMaterializationInfoMemory();

#ifdef EXPENSIVE_CHECKS
  verifySessionState("exiting ExecutionSession::IL_failSymbols");
#endif

  return std::make_pair(std::move(FailedQueries), std::move(FailedSymbolsMap));
}

void ExecutionSession::OL_notifyFailed(MaterializationResponsibility &MR) {

  LLVM_DEBUG({
    dbgs() << "In " << MR.JD.getName() << " failing materialization for "
           << MR.SymbolFlags << "\n";
  });

  if (MR.SymbolFlags.empty())
    return;

  SymbolNameVector SymbolsToFail;
  for (auto &[Name, Flags] : MR.SymbolFlags)
    SymbolsToFail.push_back(Name);
  MR.SymbolFlags.clear();

  JITDylib::AsynchronousSymbolQuerySet FailedQueries;
  std::shared_ptr<SymbolDependenceMap> FailedSymbols;

  std::tie(FailedQueries, FailedSymbols) = runSessionLocked([&]() {
    // If the tracker is defunct then there's nothing to do here.
    if (MR.RT->isDefunct())
      return std::pair<JITDylib::AsynchronousSymbolQuerySet,
                       std::shared_ptr<SymbolDependenceMap>>();
    return IL_failSymbols(MR.getTargetJITDylib(), SymbolsToFail);
  });

  for (auto &Q : FailedQueries)
    Q->handleFailed(
        make_error<FailedToMaterialize>(getSymbolStringPool(), FailedSymbols));
}

Error ExecutionSession::OL_replace(MaterializationResponsibility &MR,
                                   std::unique_ptr<MaterializationUnit> MU) {
  for (auto &KV : MU->getSymbols()) {
    assert(MR.SymbolFlags.count(KV.first) &&
           "Replacing definition outside this responsibility set");
    MR.SymbolFlags.erase(KV.first);
  }

  if (MU->getInitializerSymbol() == MR.InitSymbol)
    MR.InitSymbol = nullptr;

  LLVM_DEBUG(MR.JD.getExecutionSession().runSessionLocked([&]() {
    dbgs() << "In " << MR.JD.getName() << " replacing symbols with " << *MU
           << "\n";
  }););

  return MR.JD.replace(MR, std::move(MU));
}

Expected<std::unique_ptr<MaterializationResponsibility>>
ExecutionSession::OL_delegate(MaterializationResponsibility &MR,
                              const SymbolNameSet &Symbols) {

  SymbolStringPtr DelegatedInitSymbol;
  SymbolFlagsMap DelegatedFlags;

  for (auto &Name : Symbols) {
    auto I = MR.SymbolFlags.find(Name);
    assert(I != MR.SymbolFlags.end() &&
           "Symbol is not tracked by this MaterializationResponsibility "
           "instance");

    DelegatedFlags[Name] = std::move(I->second);
    if (Name == MR.InitSymbol)
      std::swap(MR.InitSymbol, DelegatedInitSymbol);

    MR.SymbolFlags.erase(I);
  }

  return MR.JD.delegate(MR, std::move(DelegatedFlags),
                        std::move(DelegatedInitSymbol));
}

#ifndef NDEBUG
void ExecutionSession::dumpDispatchInfo(Task &T) {
  runSessionLocked([&]() {
    dbgs() << "Dispatching: ";
    T.printDescription(dbgs());
    dbgs() << "\n";
  });
}
#endif // NDEBUG

} // End namespace orc.
} // End namespace llvm.
