//===-- Speculation.h - Speculative Compilation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains the definition to support speculative compilation when laziness is
// enabled.
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SPECULATION_H
#define LLVM_EXECUTIONENGINE_ORC_SPECULATION_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/DebugUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/Support/Debug.h"
#include <mutex>
#include <type_traits>
#include <utility>

namespace llvm {
namespace orc {

class Speculator;

// Track the Impls (JITDylib,Symbols) of Symbols while lazy call through
// trampolines are created. Operations are guarded by locks tp ensure that Imap
// stays in consistent state after read/write

class ImplSymbolMap {
  friend class Speculator;

public:
  using AliaseeDetails = std::pair<SymbolStringPtr, JITDylib *>;
  using Alias = SymbolStringPtr;
  using ImapTy = DenseMap<Alias, AliaseeDetails>;
  void trackImpls(SymbolAliasMap ImplMaps, JITDylib *SrcJD);

private:
  // FIX ME: find a right way to distinguish the pre-compile Symbols, and update
  // the callsite
  std::optional<AliaseeDetails> getImplFor(const SymbolStringPtr &StubSymbol) {
    std::lock_guard<std::mutex> Lockit(ConcurrentAccess);
    auto Position = Maps.find(StubSymbol);
    if (Position != Maps.end())
      return Position->getSecond();
    else
      return std::nullopt;
  }

  std::mutex ConcurrentAccess;
  ImapTy Maps;
};

// Defines Speculator Concept,
class Speculator {
public:
  using TargetFAddr = ExecutorAddr;
  using FunctionCandidatesMap = DenseMap<SymbolStringPtr, SymbolNameSet>;
  using StubAddrLikelies = DenseMap<TargetFAddr, SymbolNameSet>;

private:
  void registerSymbolsWithAddr(TargetFAddr ImplAddr,
                               SymbolNameSet likelySymbols) {
    std::lock_guard<std::mutex> Lockit(ConcurrentAccess);
    GlobalSpecMap.insert({ImplAddr, std::move(likelySymbols)});
  }

  void launchCompile(ExecutorAddr FAddr) {
    SymbolNameSet CandidateSet;
    // Copy CandidateSet is necessary, to avoid unsynchronized access to
    // the datastructure.
    {
      std::lock_guard<std::mutex> Lockit(ConcurrentAccess);
      auto It = GlobalSpecMap.find(FAddr);
      if (It == GlobalSpecMap.end())
        return;
      CandidateSet = It->getSecond();
    }

    SymbolDependenceMap SpeculativeLookUpImpls;

    for (auto &Callee : CandidateSet) {
      auto ImplSymbol = AliaseeImplTable.getImplFor(Callee);
      // try to distinguish already compiled & library symbols
      if (!ImplSymbol)
        continue;
      const auto &ImplSymbolName = ImplSymbol->first;
      JITDylib *ImplJD = ImplSymbol->second;
      auto &SymbolsInJD = SpeculativeLookUpImpls[ImplJD];
      SymbolsInJD.insert(ImplSymbolName);
    }

    DEBUG_WITH_TYPE("orc", {
      for (auto &I : SpeculativeLookUpImpls) {
        llvm::dbgs() << "\n In " << I.first->getName() << " JITDylib ";
        for (auto &N : I.second)
          llvm::dbgs() << "\n Likely Symbol : " << N;
      }
    });

    // for a given symbol, there may be no symbol qualified for speculatively
    // compile try to fix this before jumping to this code if possible.
    for (auto &LookupPair : SpeculativeLookUpImpls)
      ES.lookup(
          LookupKind::Static,
          makeJITDylibSearchOrder(LookupPair.first,
                                  JITDylibLookupFlags::MatchAllSymbols),
          SymbolLookupSet(LookupPair.second), SymbolState::Ready,
          [this](Expected<SymbolMap> Result) {
            if (auto Err = Result.takeError())
              ES.reportError(std::move(Err));
          },
          NoDependenciesToRegister);
  }

public:
  Speculator(ImplSymbolMap &Impl, ExecutionSession &ref)
      : AliaseeImplTable(Impl), ES(ref), GlobalSpecMap(0) {}
  Speculator(const Speculator &) = delete;
  Speculator(Speculator &&) = delete;
  Speculator &operator=(const Speculator &) = delete;
  Speculator &operator=(Speculator &&) = delete;

  /// Define symbols for this Speculator object (__orc_speculator) and the
  /// speculation runtime entry point symbol (__orc_speculate_for) in the
  /// given JITDylib.
  Error addSpeculationRuntime(JITDylib &JD, MangleAndInterner &Mangle);

  // Speculatively compile likely functions for the given Stub Address.
  // destination of __orc_speculate_for jump
  void speculateFor(TargetFAddr StubAddr) { launchCompile(StubAddr); }

  // FIXME : Register with Stub Address, after JITLink Fix.
  void registerSymbols(FunctionCandidatesMap Candidates, JITDylib *JD) {
    for (auto &SymPair : Candidates) {
      auto Target = SymPair.first;
      auto Likely = SymPair.second;

      auto OnReadyFixUp = [Likely, Target,
                           this](Expected<SymbolMap> ReadySymbol) {
        if (ReadySymbol) {
          auto RDef = (*ReadySymbol)[Target];
          registerSymbolsWithAddr(RDef.getAddress(), std::move(Likely));
        } else
          this->getES().reportError(ReadySymbol.takeError());
      };
      // Include non-exported symbols also.
      ES.lookup(
          LookupKind::Static,
          makeJITDylibSearchOrder(JD, JITDylibLookupFlags::MatchAllSymbols),
          SymbolLookupSet(Target, SymbolLookupFlags::WeaklyReferencedSymbol),
          SymbolState::Ready, OnReadyFixUp, NoDependenciesToRegister);
    }
  }

  ExecutionSession &getES() { return ES; }

private:
  static void speculateForEntryPoint(Speculator *Ptr, uint64_t StubId);
  std::mutex ConcurrentAccess;
  ImplSymbolMap &AliaseeImplTable;
  ExecutionSession &ES;
  StubAddrLikelies GlobalSpecMap;
};

class IRSpeculationLayer : public IRLayer {
public:
  using IRlikiesStrRef =
      std::optional<DenseMap<StringRef, DenseSet<StringRef>>>;
  using ResultEval = std::function<IRlikiesStrRef(Function &)>;
  using TargetAndLikelies = DenseMap<SymbolStringPtr, SymbolNameSet>;

  IRSpeculationLayer(ExecutionSession &ES, IRLayer &BaseLayer, Speculator &Spec,
                     MangleAndInterner &Mangle, ResultEval Interpreter)
      : IRLayer(ES, BaseLayer.getManglingOptions()), NextLayer(BaseLayer),
        S(Spec), Mangle(Mangle), QueryAnalysis(Interpreter) {}

  void emit(std::unique_ptr<MaterializationResponsibility> R,
            ThreadSafeModule TSM) override;

private:
  TargetAndLikelies
  internToJITSymbols(DenseMap<StringRef, DenseSet<StringRef>> IRNames) {
    assert(!IRNames.empty() && "No IRNames received to Intern?");
    TargetAndLikelies InternedNames;
    for (auto &NamePair : IRNames) {
      DenseSet<SymbolStringPtr> TargetJITNames;
      for (auto &TargetNames : NamePair.second)
        TargetJITNames.insert(Mangle(TargetNames));
      InternedNames[Mangle(NamePair.first)] = std::move(TargetJITNames);
    }
    return InternedNames;
  }

  IRLayer &NextLayer;
  Speculator &S;
  MangleAndInterner &Mangle;
  ResultEval QueryAnalysis;
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SPECULATION_H
