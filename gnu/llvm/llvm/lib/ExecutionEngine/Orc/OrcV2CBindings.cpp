//===--------------- OrcV2CBindings.cpp - C bindings OrcV2 APIs -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm-c/LLJIT.h"
#include "llvm-c/Orc.h"
#include "llvm-c/OrcEE.h"
#include "llvm-c/TargetMachine.h"

#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ObjectTransformLayer.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"

using namespace llvm;
using namespace llvm::orc;

namespace llvm {
namespace orc {

class InProgressLookupState;

class OrcV2CAPIHelper {
public:
  static InProgressLookupState *extractLookupState(LookupState &LS) {
    return LS.IPLS.release();
  }

  static void resetLookupState(LookupState &LS, InProgressLookupState *IPLS) {
    return LS.reset(IPLS);
  }
};

} // namespace orc
} // namespace llvm

inline LLVMOrcSymbolStringPoolEntryRef wrap(SymbolStringPoolEntryUnsafe E) {
  return reinterpret_cast<LLVMOrcSymbolStringPoolEntryRef>(E.rawPtr());
}

inline SymbolStringPoolEntryUnsafe unwrap(LLVMOrcSymbolStringPoolEntryRef E) {
  return reinterpret_cast<SymbolStringPoolEntryUnsafe::PoolEntry *>(E);
}

DEFINE_SIMPLE_CONVERSION_FUNCTIONS(ExecutionSession, LLVMOrcExecutionSessionRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(SymbolStringPool, LLVMOrcSymbolStringPoolRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(MaterializationUnit,
                                   LLVMOrcMaterializationUnitRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(MaterializationResponsibility,
                                   LLVMOrcMaterializationResponsibilityRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(JITDylib, LLVMOrcJITDylibRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(ResourceTracker, LLVMOrcResourceTrackerRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(DefinitionGenerator,
                                   LLVMOrcDefinitionGeneratorRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(InProgressLookupState, LLVMOrcLookupStateRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(ThreadSafeContext,
                                   LLVMOrcThreadSafeContextRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(ThreadSafeModule, LLVMOrcThreadSafeModuleRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(JITTargetMachineBuilder,
                                   LLVMOrcJITTargetMachineBuilderRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(ObjectLayer, LLVMOrcObjectLayerRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(IRTransformLayer, LLVMOrcIRTransformLayerRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(ObjectTransformLayer,
                                   LLVMOrcObjectTransformLayerRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(DumpObjects, LLVMOrcDumpObjectsRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(IndirectStubsManager,
                                   LLVMOrcIndirectStubsManagerRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(LazyCallThroughManager,
                                   LLVMOrcLazyCallThroughManagerRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(LLJITBuilder, LLVMOrcLLJITBuilderRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(LLJIT, LLVMOrcLLJITRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(TargetMachine, LLVMTargetMachineRef)

namespace {

class OrcCAPIMaterializationUnit : public llvm::orc::MaterializationUnit {
public:
  OrcCAPIMaterializationUnit(
      std::string Name, SymbolFlagsMap InitialSymbolFlags,
      SymbolStringPtr InitSymbol, void *Ctx,
      LLVMOrcMaterializationUnitMaterializeFunction Materialize,
      LLVMOrcMaterializationUnitDiscardFunction Discard,
      LLVMOrcMaterializationUnitDestroyFunction Destroy)
      : llvm::orc::MaterializationUnit(
            Interface(std::move(InitialSymbolFlags), std::move(InitSymbol))),
        Name(std::move(Name)), Ctx(Ctx), Materialize(Materialize),
        Discard(Discard), Destroy(Destroy) {}

  ~OrcCAPIMaterializationUnit() {
    if (Ctx)
      Destroy(Ctx);
  }

  StringRef getName() const override { return Name; }

  void materialize(std::unique_ptr<MaterializationResponsibility> R) override {
    void *Tmp = Ctx;
    Ctx = nullptr;
    Materialize(Tmp, wrap(R.release()));
  }

private:
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override {
    Discard(Ctx, wrap(&JD), wrap(SymbolStringPoolEntryUnsafe::from(Name)));
  }

  std::string Name;
  void *Ctx = nullptr;
  LLVMOrcMaterializationUnitMaterializeFunction Materialize = nullptr;
  LLVMOrcMaterializationUnitDiscardFunction Discard = nullptr;
  LLVMOrcMaterializationUnitDestroyFunction Destroy = nullptr;
};

static JITSymbolFlags toJITSymbolFlags(LLVMJITSymbolFlags F) {

  JITSymbolFlags JSF;

  if (F.GenericFlags & LLVMJITSymbolGenericFlagsExported)
    JSF |= JITSymbolFlags::Exported;
  if (F.GenericFlags & LLVMJITSymbolGenericFlagsWeak)
    JSF |= JITSymbolFlags::Weak;
  if (F.GenericFlags & LLVMJITSymbolGenericFlagsCallable)
    JSF |= JITSymbolFlags::Callable;
  if (F.GenericFlags & LLVMJITSymbolGenericFlagsMaterializationSideEffectsOnly)
    JSF |= JITSymbolFlags::MaterializationSideEffectsOnly;

  JSF.getTargetFlags() = F.TargetFlags;

  return JSF;
}

static LLVMJITSymbolFlags fromJITSymbolFlags(JITSymbolFlags JSF) {
  LLVMJITSymbolFlags F = {0, 0};
  if (JSF & JITSymbolFlags::Exported)
    F.GenericFlags |= LLVMJITSymbolGenericFlagsExported;
  if (JSF & JITSymbolFlags::Weak)
    F.GenericFlags |= LLVMJITSymbolGenericFlagsWeak;
  if (JSF & JITSymbolFlags::Callable)
    F.GenericFlags |= LLVMJITSymbolGenericFlagsCallable;
  if (JSF & JITSymbolFlags::MaterializationSideEffectsOnly)
    F.GenericFlags |= LLVMJITSymbolGenericFlagsMaterializationSideEffectsOnly;

  F.TargetFlags = JSF.getTargetFlags();

  return F;
}

static SymbolNameSet toSymbolNameSet(LLVMOrcCSymbolsList Symbols) {
  SymbolNameSet Result;
  Result.reserve(Symbols.Length);
  for (size_t I = 0; I != Symbols.Length; ++I)
    Result.insert(unwrap(Symbols.Symbols[I]).moveToSymbolStringPtr());
  return Result;
}

static SymbolMap toSymbolMap(LLVMOrcCSymbolMapPairs Syms, size_t NumPairs) {
  SymbolMap SM;
  for (size_t I = 0; I != NumPairs; ++I) {
    JITSymbolFlags Flags = toJITSymbolFlags(Syms[I].Sym.Flags);
    SM[unwrap(Syms[I].Name).moveToSymbolStringPtr()] = {
        ExecutorAddr(Syms[I].Sym.Address), Flags};
  }
  return SM;
}

static SymbolDependenceMap
toSymbolDependenceMap(LLVMOrcCDependenceMapPairs Pairs, size_t NumPairs) {
  SymbolDependenceMap SDM;
  for (size_t I = 0; I != NumPairs; ++I) {
    JITDylib *JD = unwrap(Pairs[I].JD);
    SymbolNameSet Names;

    for (size_t J = 0; J != Pairs[I].Names.Length; ++J) {
      auto Sym = Pairs[I].Names.Symbols[J];
      Names.insert(unwrap(Sym).moveToSymbolStringPtr());
    }
    SDM[JD] = Names;
  }
  return SDM;
}

static LookupKind toLookupKind(LLVMOrcLookupKind K) {
  switch (K) {
  case LLVMOrcLookupKindStatic:
    return LookupKind::Static;
  case LLVMOrcLookupKindDLSym:
    return LookupKind::DLSym;
  }
  llvm_unreachable("unrecognized LLVMOrcLookupKind value");
}

static LLVMOrcLookupKind fromLookupKind(LookupKind K) {
  switch (K) {
  case LookupKind::Static:
    return LLVMOrcLookupKindStatic;
  case LookupKind::DLSym:
    return LLVMOrcLookupKindDLSym;
  }
  llvm_unreachable("unrecognized LookupKind value");
}

static JITDylibLookupFlags
toJITDylibLookupFlags(LLVMOrcJITDylibLookupFlags LF) {
  switch (LF) {
  case LLVMOrcJITDylibLookupFlagsMatchExportedSymbolsOnly:
    return JITDylibLookupFlags::MatchExportedSymbolsOnly;
  case LLVMOrcJITDylibLookupFlagsMatchAllSymbols:
    return JITDylibLookupFlags::MatchAllSymbols;
  }
  llvm_unreachable("unrecognized LLVMOrcJITDylibLookupFlags value");
}

static LLVMOrcJITDylibLookupFlags
fromJITDylibLookupFlags(JITDylibLookupFlags LF) {
  switch (LF) {
  case JITDylibLookupFlags::MatchExportedSymbolsOnly:
    return LLVMOrcJITDylibLookupFlagsMatchExportedSymbolsOnly;
  case JITDylibLookupFlags::MatchAllSymbols:
    return LLVMOrcJITDylibLookupFlagsMatchAllSymbols;
  }
  llvm_unreachable("unrecognized JITDylibLookupFlags value");
}

static SymbolLookupFlags toSymbolLookupFlags(LLVMOrcSymbolLookupFlags SLF) {
  switch (SLF) {
  case LLVMOrcSymbolLookupFlagsRequiredSymbol:
    return SymbolLookupFlags::RequiredSymbol;
  case LLVMOrcSymbolLookupFlagsWeaklyReferencedSymbol:
    return SymbolLookupFlags::WeaklyReferencedSymbol;
  }
  llvm_unreachable("unrecognized LLVMOrcSymbolLookupFlags value");
}

static LLVMOrcSymbolLookupFlags fromSymbolLookupFlags(SymbolLookupFlags SLF) {
  switch (SLF) {
  case SymbolLookupFlags::RequiredSymbol:
    return LLVMOrcSymbolLookupFlagsRequiredSymbol;
  case SymbolLookupFlags::WeaklyReferencedSymbol:
    return LLVMOrcSymbolLookupFlagsWeaklyReferencedSymbol;
  }
  llvm_unreachable("unrecognized SymbolLookupFlags value");
}

static LLVMJITEvaluatedSymbol
fromExecutorSymbolDef(const ExecutorSymbolDef &S) {
  return {S.getAddress().getValue(), fromJITSymbolFlags(S.getFlags())};
}

} // end anonymous namespace

namespace llvm {
namespace orc {

class CAPIDefinitionGenerator final : public DefinitionGenerator {
public:
  CAPIDefinitionGenerator(
      LLVMOrcDisposeCAPIDefinitionGeneratorFunction Dispose, void *Ctx,
      LLVMOrcCAPIDefinitionGeneratorTryToGenerateFunction TryToGenerate)
      : Dispose(Dispose), Ctx(Ctx), TryToGenerate(TryToGenerate) {}

  ~CAPIDefinitionGenerator() {
    if (Dispose)
      Dispose(Ctx);
  }

  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &LookupSet) override {

    // Take the lookup state.
    LLVMOrcLookupStateRef LSR = ::wrap(OrcV2CAPIHelper::extractLookupState(LS));

    // Translate the lookup kind.
    LLVMOrcLookupKind CLookupKind = fromLookupKind(K);

    // Translate the JITDylibLookupFlags.
    LLVMOrcJITDylibLookupFlags CJDLookupFlags =
        fromJITDylibLookupFlags(JDLookupFlags);

    // Translate the lookup set.
    std::vector<LLVMOrcCLookupSetElement> CLookupSet;
    CLookupSet.reserve(LookupSet.size());
    for (auto &KV : LookupSet) {
      LLVMOrcSymbolStringPoolEntryRef Name =
          ::wrap(SymbolStringPoolEntryUnsafe::from(KV.first));
      LLVMOrcSymbolLookupFlags SLF = fromSymbolLookupFlags(KV.second);
      CLookupSet.push_back({Name, SLF});
    }

    // Run the C TryToGenerate function.
    auto Err = unwrap(TryToGenerate(::wrap(this), Ctx, &LSR, CLookupKind,
                                    ::wrap(&JD), CJDLookupFlags,
                                    CLookupSet.data(), CLookupSet.size()));

    // Restore the lookup state.
    OrcV2CAPIHelper::resetLookupState(LS, ::unwrap(LSR));

    return Err;
  }

private:
  LLVMOrcDisposeCAPIDefinitionGeneratorFunction Dispose;
  void *Ctx;
  LLVMOrcCAPIDefinitionGeneratorTryToGenerateFunction TryToGenerate;
};

} // end namespace orc
} // end namespace llvm

void LLVMOrcExecutionSessionSetErrorReporter(
    LLVMOrcExecutionSessionRef ES, LLVMOrcErrorReporterFunction ReportError,
    void *Ctx) {
  unwrap(ES)->setErrorReporter(
      [=](Error Err) { ReportError(Ctx, wrap(std::move(Err))); });
}

LLVMOrcSymbolStringPoolRef
LLVMOrcExecutionSessionGetSymbolStringPool(LLVMOrcExecutionSessionRef ES) {
  return wrap(
      unwrap(ES)->getExecutorProcessControl().getSymbolStringPool().get());
}

void LLVMOrcSymbolStringPoolClearDeadEntries(LLVMOrcSymbolStringPoolRef SSP) {
  unwrap(SSP)->clearDeadEntries();
}

LLVMOrcSymbolStringPoolEntryRef
LLVMOrcExecutionSessionIntern(LLVMOrcExecutionSessionRef ES, const char *Name) {
  return wrap(SymbolStringPoolEntryUnsafe::take(unwrap(ES)->intern(Name)));
}

void LLVMOrcExecutionSessionLookup(
    LLVMOrcExecutionSessionRef ES, LLVMOrcLookupKind K,
    LLVMOrcCJITDylibSearchOrder SearchOrder, size_t SearchOrderSize,
    LLVMOrcCLookupSet Symbols, size_t SymbolsSize,
    LLVMOrcExecutionSessionLookupHandleResultFunction HandleResult, void *Ctx) {
  assert(ES && "ES cannot be null");
  assert(SearchOrder && "SearchOrder cannot be null");
  assert(Symbols && "Symbols cannot be null");
  assert(HandleResult && "HandleResult cannot be null");

  JITDylibSearchOrder SO;
  for (size_t I = 0; I != SearchOrderSize; ++I)
    SO.push_back({unwrap(SearchOrder[I].JD),
                  toJITDylibLookupFlags(SearchOrder[I].JDLookupFlags)});

  SymbolLookupSet SLS;
  for (size_t I = 0; I != SymbolsSize; ++I)
    SLS.add(unwrap(Symbols[I].Name).moveToSymbolStringPtr(),
            toSymbolLookupFlags(Symbols[I].LookupFlags));

  unwrap(ES)->lookup(
      toLookupKind(K), SO, std::move(SLS), SymbolState::Ready,
      [HandleResult, Ctx](Expected<SymbolMap> Result) {
        if (Result) {
          SmallVector<LLVMOrcCSymbolMapPair> CResult;
          for (auto &KV : *Result)
            CResult.push_back(LLVMOrcCSymbolMapPair{
                wrap(SymbolStringPoolEntryUnsafe::from(KV.first)),
                fromExecutorSymbolDef(KV.second)});
          HandleResult(LLVMErrorSuccess, CResult.data(), CResult.size(), Ctx);
        } else
          HandleResult(wrap(Result.takeError()), nullptr, 0, Ctx);
      },
      NoDependenciesToRegister);
}

void LLVMOrcRetainSymbolStringPoolEntry(LLVMOrcSymbolStringPoolEntryRef S) {
  unwrap(S).retain();
}

void LLVMOrcReleaseSymbolStringPoolEntry(LLVMOrcSymbolStringPoolEntryRef S) {
  unwrap(S).release();
}

const char *LLVMOrcSymbolStringPoolEntryStr(LLVMOrcSymbolStringPoolEntryRef S) {
  return unwrap(S).rawPtr()->getKey().data();
}

LLVMOrcResourceTrackerRef
LLVMOrcJITDylibCreateResourceTracker(LLVMOrcJITDylibRef JD) {
  auto RT = unwrap(JD)->createResourceTracker();
  // Retain the pointer for the C API client.
  RT->Retain();
  return wrap(RT.get());
}

LLVMOrcResourceTrackerRef
LLVMOrcJITDylibGetDefaultResourceTracker(LLVMOrcJITDylibRef JD) {
  auto RT = unwrap(JD)->getDefaultResourceTracker();
  // Retain the pointer for the C API client.
  return wrap(RT.get());
}

void LLVMOrcReleaseResourceTracker(LLVMOrcResourceTrackerRef RT) {
  ResourceTrackerSP TmpRT(unwrap(RT));
  TmpRT->Release();
}

void LLVMOrcResourceTrackerTransferTo(LLVMOrcResourceTrackerRef SrcRT,
                                      LLVMOrcResourceTrackerRef DstRT) {
  ResourceTrackerSP TmpRT(unwrap(SrcRT));
  TmpRT->transferTo(*unwrap(DstRT));
}

LLVMErrorRef LLVMOrcResourceTrackerRemove(LLVMOrcResourceTrackerRef RT) {
  ResourceTrackerSP TmpRT(unwrap(RT));
  return wrap(TmpRT->remove());
}

void LLVMOrcDisposeDefinitionGenerator(LLVMOrcDefinitionGeneratorRef DG) {
  std::unique_ptr<DefinitionGenerator> TmpDG(unwrap(DG));
}

void LLVMOrcDisposeMaterializationUnit(LLVMOrcMaterializationUnitRef MU) {
  std::unique_ptr<MaterializationUnit> TmpMU(unwrap(MU));
}

LLVMOrcMaterializationUnitRef LLVMOrcCreateCustomMaterializationUnit(
    const char *Name, void *Ctx, LLVMOrcCSymbolFlagsMapPairs Syms,
    size_t NumSyms, LLVMOrcSymbolStringPoolEntryRef InitSym,
    LLVMOrcMaterializationUnitMaterializeFunction Materialize,
    LLVMOrcMaterializationUnitDiscardFunction Discard,
    LLVMOrcMaterializationUnitDestroyFunction Destroy) {
  SymbolFlagsMap SFM;
  for (size_t I = 0; I != NumSyms; ++I)
    SFM[unwrap(Syms[I].Name).moveToSymbolStringPtr()] =
        toJITSymbolFlags(Syms[I].Flags);

  auto IS = unwrap(InitSym).moveToSymbolStringPtr();

  return wrap(new OrcCAPIMaterializationUnit(
      Name, std::move(SFM), std::move(IS), Ctx, Materialize, Discard, Destroy));
}

LLVMOrcMaterializationUnitRef
LLVMOrcAbsoluteSymbols(LLVMOrcCSymbolMapPairs Syms, size_t NumPairs) {
  SymbolMap SM = toSymbolMap(Syms, NumPairs);
  return wrap(absoluteSymbols(std::move(SM)).release());
}

LLVMOrcMaterializationUnitRef LLVMOrcLazyReexports(
    LLVMOrcLazyCallThroughManagerRef LCTM, LLVMOrcIndirectStubsManagerRef ISM,
    LLVMOrcJITDylibRef SourceJD, LLVMOrcCSymbolAliasMapPairs CallableAliases,
    size_t NumPairs) {

  SymbolAliasMap SAM;
  for (size_t I = 0; I != NumPairs; ++I) {
    auto pair = CallableAliases[I];
    JITSymbolFlags Flags = toJITSymbolFlags(pair.Entry.Flags);
    SymbolStringPtr Name = unwrap(pair.Entry.Name).moveToSymbolStringPtr();
    SAM[unwrap(pair.Name).moveToSymbolStringPtr()] =
        SymbolAliasMapEntry(Name, Flags);
  }

  return wrap(lazyReexports(*unwrap(LCTM), *unwrap(ISM), *unwrap(SourceJD),
                            std::move(SAM))
                  .release());
}

void LLVMOrcDisposeMaterializationResponsibility(
    LLVMOrcMaterializationResponsibilityRef MR) {
  std::unique_ptr<MaterializationResponsibility> TmpMR(unwrap(MR));
}

LLVMOrcJITDylibRef LLVMOrcMaterializationResponsibilityGetTargetDylib(
    LLVMOrcMaterializationResponsibilityRef MR) {
  return wrap(&unwrap(MR)->getTargetJITDylib());
}

LLVMOrcExecutionSessionRef
LLVMOrcMaterializationResponsibilityGetExecutionSession(
    LLVMOrcMaterializationResponsibilityRef MR) {
  return wrap(&unwrap(MR)->getExecutionSession());
}

LLVMOrcCSymbolFlagsMapPairs LLVMOrcMaterializationResponsibilityGetSymbols(
    LLVMOrcMaterializationResponsibilityRef MR, size_t *NumPairs) {

  auto Symbols = unwrap(MR)->getSymbols();
  LLVMOrcCSymbolFlagsMapPairs Result = static_cast<LLVMOrcCSymbolFlagsMapPairs>(
      safe_malloc(Symbols.size() * sizeof(LLVMOrcCSymbolFlagsMapPair)));
  size_t I = 0;
  for (auto const &pair : Symbols) {
    auto Name = wrap(SymbolStringPoolEntryUnsafe::from(pair.first));
    auto Flags = pair.second;
    Result[I] = {Name, fromJITSymbolFlags(Flags)};
    I++;
  }
  *NumPairs = Symbols.size();
  return Result;
}

void LLVMOrcDisposeCSymbolFlagsMap(LLVMOrcCSymbolFlagsMapPairs Pairs) {
  free(Pairs);
}

LLVMOrcSymbolStringPoolEntryRef
LLVMOrcMaterializationResponsibilityGetInitializerSymbol(
    LLVMOrcMaterializationResponsibilityRef MR) {
  auto Sym = unwrap(MR)->getInitializerSymbol();
  return wrap(SymbolStringPoolEntryUnsafe::from(Sym));
}

LLVMOrcSymbolStringPoolEntryRef *
LLVMOrcMaterializationResponsibilityGetRequestedSymbols(
    LLVMOrcMaterializationResponsibilityRef MR, size_t *NumSymbols) {

  auto Symbols = unwrap(MR)->getRequestedSymbols();
  LLVMOrcSymbolStringPoolEntryRef *Result =
      static_cast<LLVMOrcSymbolStringPoolEntryRef *>(safe_malloc(
          Symbols.size() * sizeof(LLVMOrcSymbolStringPoolEntryRef)));
  size_t I = 0;
  for (auto &Name : Symbols) {
    Result[I] = wrap(SymbolStringPoolEntryUnsafe::from(Name));
    I++;
  }
  *NumSymbols = Symbols.size();
  return Result;
}

void LLVMOrcDisposeSymbols(LLVMOrcSymbolStringPoolEntryRef *Symbols) {
  free(Symbols);
}

LLVMErrorRef LLVMOrcMaterializationResponsibilityNotifyResolved(
    LLVMOrcMaterializationResponsibilityRef MR, LLVMOrcCSymbolMapPairs Symbols,
    size_t NumSymbols) {
  SymbolMap SM = toSymbolMap(Symbols, NumSymbols);
  return wrap(unwrap(MR)->notifyResolved(std::move(SM)));
}

LLVMErrorRef LLVMOrcMaterializationResponsibilityNotifyEmitted(
    LLVMOrcMaterializationResponsibilityRef MR,
    LLVMOrcCSymbolDependenceGroup *SymbolDepGroups, size_t NumSymbolDepGroups) {
  std::vector<SymbolDependenceGroup> SDGs;
  SDGs.reserve(NumSymbolDepGroups);
  for (size_t I = 0; I != NumSymbolDepGroups; ++I) {
    SDGs.push_back(SymbolDependenceGroup());
    auto &SDG = SDGs.back();
    SDG.Symbols = toSymbolNameSet(SymbolDepGroups[I].Symbols);
    SDG.Dependencies = toSymbolDependenceMap(
        SymbolDepGroups[I].Dependencies, SymbolDepGroups[I].NumDependencies);
  }
  return wrap(unwrap(MR)->notifyEmitted(SDGs));
}

LLVMErrorRef LLVMOrcMaterializationResponsibilityDefineMaterializing(
    LLVMOrcMaterializationResponsibilityRef MR,
    LLVMOrcCSymbolFlagsMapPairs Syms, size_t NumSyms) {
  SymbolFlagsMap SFM;
  for (size_t I = 0; I != NumSyms; ++I)
    SFM[unwrap(Syms[I].Name).moveToSymbolStringPtr()] =
        toJITSymbolFlags(Syms[I].Flags);

  return wrap(unwrap(MR)->defineMaterializing(std::move(SFM)));
}

LLVMErrorRef LLVMOrcMaterializationResponsibilityReplace(
    LLVMOrcMaterializationResponsibilityRef MR,
    LLVMOrcMaterializationUnitRef MU) {
  std::unique_ptr<MaterializationUnit> TmpMU(unwrap(MU));
  return wrap(unwrap(MR)->replace(std::move(TmpMU)));
}

LLVMErrorRef LLVMOrcMaterializationResponsibilityDelegate(
    LLVMOrcMaterializationResponsibilityRef MR,
    LLVMOrcSymbolStringPoolEntryRef *Symbols, size_t NumSymbols,
    LLVMOrcMaterializationResponsibilityRef *Result) {
  SymbolNameSet Syms;
  for (size_t I = 0; I != NumSymbols; I++) {
    Syms.insert(unwrap(Symbols[I]).moveToSymbolStringPtr());
  }
  auto OtherMR = unwrap(MR)->delegate(Syms);

  if (!OtherMR) {
    return wrap(OtherMR.takeError());
  }
  *Result = wrap(OtherMR->release());
  return LLVMErrorSuccess;
}

void LLVMOrcMaterializationResponsibilityFailMaterialization(
    LLVMOrcMaterializationResponsibilityRef MR) {
  unwrap(MR)->failMaterialization();
}

void LLVMOrcIRTransformLayerEmit(LLVMOrcIRTransformLayerRef IRLayer,
                                 LLVMOrcMaterializationResponsibilityRef MR,
                                 LLVMOrcThreadSafeModuleRef TSM) {
  std::unique_ptr<ThreadSafeModule> TmpTSM(unwrap(TSM));
  unwrap(IRLayer)->emit(
      std::unique_ptr<MaterializationResponsibility>(unwrap(MR)),
      std::move(*TmpTSM));
}

LLVMOrcJITDylibRef
LLVMOrcExecutionSessionCreateBareJITDylib(LLVMOrcExecutionSessionRef ES,
                                          const char *Name) {
  return wrap(&unwrap(ES)->createBareJITDylib(Name));
}

LLVMErrorRef
LLVMOrcExecutionSessionCreateJITDylib(LLVMOrcExecutionSessionRef ES,
                                      LLVMOrcJITDylibRef *Result,
                                      const char *Name) {
  auto JD = unwrap(ES)->createJITDylib(Name);
  if (!JD)
    return wrap(JD.takeError());
  *Result = wrap(&*JD);
  return LLVMErrorSuccess;
}

LLVMOrcJITDylibRef
LLVMOrcExecutionSessionGetJITDylibByName(LLVMOrcExecutionSessionRef ES,
                                         const char *Name) {
  return wrap(unwrap(ES)->getJITDylibByName(Name));
}

LLVMErrorRef LLVMOrcJITDylibDefine(LLVMOrcJITDylibRef JD,
                                   LLVMOrcMaterializationUnitRef MU) {
  std::unique_ptr<MaterializationUnit> TmpMU(unwrap(MU));

  if (auto Err = unwrap(JD)->define(TmpMU)) {
    TmpMU.release();
    return wrap(std::move(Err));
  }
  return LLVMErrorSuccess;
}

LLVMErrorRef LLVMOrcJITDylibClear(LLVMOrcJITDylibRef JD) {
  return wrap(unwrap(JD)->clear());
}

void LLVMOrcJITDylibAddGenerator(LLVMOrcJITDylibRef JD,
                                 LLVMOrcDefinitionGeneratorRef DG) {
  unwrap(JD)->addGenerator(std::unique_ptr<DefinitionGenerator>(unwrap(DG)));
}

LLVMOrcDefinitionGeneratorRef LLVMOrcCreateCustomCAPIDefinitionGenerator(
    LLVMOrcCAPIDefinitionGeneratorTryToGenerateFunction F, void *Ctx,
    LLVMOrcDisposeCAPIDefinitionGeneratorFunction Dispose) {
  auto DG = std::make_unique<CAPIDefinitionGenerator>(Dispose, Ctx, F);
  return wrap(DG.release());
}

void LLVMOrcLookupStateContinueLookup(LLVMOrcLookupStateRef S,
                                      LLVMErrorRef Err) {
  LookupState LS;
  OrcV2CAPIHelper::resetLookupState(LS, ::unwrap(S));
  LS.continueLookup(unwrap(Err));
}

LLVMErrorRef LLVMOrcCreateDynamicLibrarySearchGeneratorForProcess(
    LLVMOrcDefinitionGeneratorRef *Result, char GlobalPrefix,
    LLVMOrcSymbolPredicate Filter, void *FilterCtx) {
  assert(Result && "Result can not be null");
  assert((Filter || !FilterCtx) &&
         "if Filter is null then FilterCtx must also be null");

  DynamicLibrarySearchGenerator::SymbolPredicate Pred;
  if (Filter)
    Pred = [=](const SymbolStringPtr &Name) -> bool {
      return Filter(FilterCtx, wrap(SymbolStringPoolEntryUnsafe::from(Name)));
    };

  auto ProcessSymsGenerator =
      DynamicLibrarySearchGenerator::GetForCurrentProcess(GlobalPrefix, Pred);

  if (!ProcessSymsGenerator) {
    *Result = nullptr;
    return wrap(ProcessSymsGenerator.takeError());
  }

  *Result = wrap(ProcessSymsGenerator->release());
  return LLVMErrorSuccess;
}

LLVMErrorRef LLVMOrcCreateDynamicLibrarySearchGeneratorForPath(
    LLVMOrcDefinitionGeneratorRef *Result, const char *FileName,
    char GlobalPrefix, LLVMOrcSymbolPredicate Filter, void *FilterCtx) {
  assert(Result && "Result can not be null");
  assert(FileName && "FileName can not be null");
  assert((Filter || !FilterCtx) &&
         "if Filter is null then FilterCtx must also be null");

  DynamicLibrarySearchGenerator::SymbolPredicate Pred;
  if (Filter)
    Pred = [=](const SymbolStringPtr &Name) -> bool {
      return Filter(FilterCtx, wrap(SymbolStringPoolEntryUnsafe::from(Name)));
    };

  auto LibrarySymsGenerator =
      DynamicLibrarySearchGenerator::Load(FileName, GlobalPrefix, Pred);

  if (!LibrarySymsGenerator) {
    *Result = nullptr;
    return wrap(LibrarySymsGenerator.takeError());
  }

  *Result = wrap(LibrarySymsGenerator->release());
  return LLVMErrorSuccess;
}

LLVMErrorRef LLVMOrcCreateStaticLibrarySearchGeneratorForPath(
    LLVMOrcDefinitionGeneratorRef *Result, LLVMOrcObjectLayerRef ObjLayer,
    const char *FileName) {
  assert(Result && "Result can not be null");
  assert(FileName && "Filename can not be null");
  assert(ObjLayer && "ObjectLayer can not be null");

  auto LibrarySymsGenerator =
      StaticLibraryDefinitionGenerator::Load(*unwrap(ObjLayer), FileName);
  if (!LibrarySymsGenerator) {
    *Result = nullptr;
    return wrap(LibrarySymsGenerator.takeError());
  }
  *Result = wrap(LibrarySymsGenerator->release());
  return LLVMErrorSuccess;
}

LLVMOrcThreadSafeContextRef LLVMOrcCreateNewThreadSafeContext(void) {
  return wrap(new ThreadSafeContext(std::make_unique<LLVMContext>()));
}

LLVMContextRef
LLVMOrcThreadSafeContextGetContext(LLVMOrcThreadSafeContextRef TSCtx) {
  return wrap(unwrap(TSCtx)->getContext());
}

void LLVMOrcDisposeThreadSafeContext(LLVMOrcThreadSafeContextRef TSCtx) {
  delete unwrap(TSCtx);
}

LLVMErrorRef
LLVMOrcThreadSafeModuleWithModuleDo(LLVMOrcThreadSafeModuleRef TSM,
                                    LLVMOrcGenericIRModuleOperationFunction F,
                                    void *Ctx) {
  return wrap(unwrap(TSM)->withModuleDo(
      [&](Module &M) { return unwrap(F(Ctx, wrap(&M))); }));
}

LLVMOrcThreadSafeModuleRef
LLVMOrcCreateNewThreadSafeModule(LLVMModuleRef M,
                                 LLVMOrcThreadSafeContextRef TSCtx) {
  return wrap(
      new ThreadSafeModule(std::unique_ptr<Module>(unwrap(M)), *unwrap(TSCtx)));
}

void LLVMOrcDisposeThreadSafeModule(LLVMOrcThreadSafeModuleRef TSM) {
  delete unwrap(TSM);
}

LLVMErrorRef LLVMOrcJITTargetMachineBuilderDetectHost(
    LLVMOrcJITTargetMachineBuilderRef *Result) {
  assert(Result && "Result can not be null");

  auto JTMB = JITTargetMachineBuilder::detectHost();
  if (!JTMB) {
    Result = nullptr;
    return wrap(JTMB.takeError());
  }

  *Result = wrap(new JITTargetMachineBuilder(std::move(*JTMB)));
  return LLVMErrorSuccess;
}

LLVMOrcJITTargetMachineBuilderRef
LLVMOrcJITTargetMachineBuilderCreateFromTargetMachine(LLVMTargetMachineRef TM) {
  auto *TemplateTM = unwrap(TM);

  auto JTMB =
      std::make_unique<JITTargetMachineBuilder>(TemplateTM->getTargetTriple());

  (*JTMB)
      .setCPU(TemplateTM->getTargetCPU().str())
      .setRelocationModel(TemplateTM->getRelocationModel())
      .setCodeModel(TemplateTM->getCodeModel())
      .setCodeGenOptLevel(TemplateTM->getOptLevel())
      .setFeatures(TemplateTM->getTargetFeatureString())
      .setOptions(TemplateTM->Options);

  LLVMDisposeTargetMachine(TM);

  return wrap(JTMB.release());
}

void LLVMOrcDisposeJITTargetMachineBuilder(
    LLVMOrcJITTargetMachineBuilderRef JTMB) {
  delete unwrap(JTMB);
}

char *LLVMOrcJITTargetMachineBuilderGetTargetTriple(
    LLVMOrcJITTargetMachineBuilderRef JTMB) {
  auto Tmp = unwrap(JTMB)->getTargetTriple().str();
  char *TargetTriple = (char *)malloc(Tmp.size() + 1);
  strcpy(TargetTriple, Tmp.c_str());
  return TargetTriple;
}

void LLVMOrcJITTargetMachineBuilderSetTargetTriple(
    LLVMOrcJITTargetMachineBuilderRef JTMB, const char *TargetTriple) {
  unwrap(JTMB)->getTargetTriple() = Triple(TargetTriple);
}

LLVMErrorRef LLVMOrcObjectLayerAddObjectFile(LLVMOrcObjectLayerRef ObjLayer,
                                             LLVMOrcJITDylibRef JD,
                                             LLVMMemoryBufferRef ObjBuffer) {
  return wrap(unwrap(ObjLayer)->add(
      *unwrap(JD), std::unique_ptr<MemoryBuffer>(unwrap(ObjBuffer))));
}

LLVMErrorRef LLVMOrcObjectLayerAddObjectFileWithRT(LLVMOrcObjectLayerRef ObjLayer,
                                                   LLVMOrcResourceTrackerRef RT,
                                                   LLVMMemoryBufferRef ObjBuffer) {
  return wrap(
      unwrap(ObjLayer)->add(ResourceTrackerSP(unwrap(RT)),
                            std::unique_ptr<MemoryBuffer>(unwrap(ObjBuffer))));
}

void LLVMOrcObjectLayerEmit(LLVMOrcObjectLayerRef ObjLayer,
                            LLVMOrcMaterializationResponsibilityRef R,
                            LLVMMemoryBufferRef ObjBuffer) {
  unwrap(ObjLayer)->emit(
      std::unique_ptr<MaterializationResponsibility>(unwrap(R)),
      std::unique_ptr<MemoryBuffer>(unwrap(ObjBuffer)));
}

void LLVMOrcDisposeObjectLayer(LLVMOrcObjectLayerRef ObjLayer) {
  delete unwrap(ObjLayer);
}

void LLVMOrcIRTransformLayerSetTransform(
    LLVMOrcIRTransformLayerRef IRTransformLayer,
    LLVMOrcIRTransformLayerTransformFunction TransformFunction, void *Ctx) {
  unwrap(IRTransformLayer)
      ->setTransform(
          [=](ThreadSafeModule TSM,
              MaterializationResponsibility &R) -> Expected<ThreadSafeModule> {
            LLVMOrcThreadSafeModuleRef TSMRef =
                wrap(new ThreadSafeModule(std::move(TSM)));
            if (LLVMErrorRef Err = TransformFunction(Ctx, &TSMRef, wrap(&R))) {
              assert(!TSMRef && "TSMRef was not reset to null on error");
              return unwrap(Err);
            }
            assert(TSMRef && "Transform succeeded, but TSMRef was set to null");
            ThreadSafeModule Result = std::move(*unwrap(TSMRef));
            LLVMOrcDisposeThreadSafeModule(TSMRef);
            return std::move(Result);
          });
}

void LLVMOrcObjectTransformLayerSetTransform(
    LLVMOrcObjectTransformLayerRef ObjTransformLayer,
    LLVMOrcObjectTransformLayerTransformFunction TransformFunction, void *Ctx) {
  unwrap(ObjTransformLayer)
      ->setTransform([TransformFunction, Ctx](std::unique_ptr<MemoryBuffer> Obj)
                         -> Expected<std::unique_ptr<MemoryBuffer>> {
        LLVMMemoryBufferRef ObjBuffer = wrap(Obj.release());
        if (LLVMErrorRef Err = TransformFunction(Ctx, &ObjBuffer)) {
          assert(!ObjBuffer && "ObjBuffer was not reset to null on error");
          return unwrap(Err);
        }
        return std::unique_ptr<MemoryBuffer>(unwrap(ObjBuffer));
      });
}

LLVMOrcDumpObjectsRef LLVMOrcCreateDumpObjects(const char *DumpDir,
                                               const char *IdentifierOverride) {
  assert(DumpDir && "DumpDir should not be null");
  assert(IdentifierOverride && "IdentifierOverride should not be null");
  return wrap(new DumpObjects(DumpDir, IdentifierOverride));
}

void LLVMOrcDisposeDumpObjects(LLVMOrcDumpObjectsRef DumpObjects) {
  delete unwrap(DumpObjects);
}

LLVMErrorRef LLVMOrcDumpObjects_CallOperator(LLVMOrcDumpObjectsRef DumpObjects,
                                             LLVMMemoryBufferRef *ObjBuffer) {
  std::unique_ptr<MemoryBuffer> OB(unwrap(*ObjBuffer));
  if (auto Result = (*unwrap(DumpObjects))(std::move(OB))) {
    *ObjBuffer = wrap(Result->release());
    return LLVMErrorSuccess;
  } else {
    *ObjBuffer = nullptr;
    return wrap(Result.takeError());
  }
}

LLVMOrcLLJITBuilderRef LLVMOrcCreateLLJITBuilder(void) {
  return wrap(new LLJITBuilder());
}

void LLVMOrcDisposeLLJITBuilder(LLVMOrcLLJITBuilderRef Builder) {
  delete unwrap(Builder);
}

void LLVMOrcLLJITBuilderSetJITTargetMachineBuilder(
    LLVMOrcLLJITBuilderRef Builder, LLVMOrcJITTargetMachineBuilderRef JTMB) {
  unwrap(Builder)->setJITTargetMachineBuilder(std::move(*unwrap(JTMB)));
  LLVMOrcDisposeJITTargetMachineBuilder(JTMB);
}

void LLVMOrcLLJITBuilderSetObjectLinkingLayerCreator(
    LLVMOrcLLJITBuilderRef Builder,
    LLVMOrcLLJITBuilderObjectLinkingLayerCreatorFunction F, void *Ctx) {
  unwrap(Builder)->setObjectLinkingLayerCreator(
      [=](ExecutionSession &ES, const Triple &TT) {
        auto TTStr = TT.str();
        return std::unique_ptr<ObjectLayer>(
            unwrap(F(Ctx, wrap(&ES), TTStr.c_str())));
      });
}

LLVMErrorRef LLVMOrcCreateLLJIT(LLVMOrcLLJITRef *Result,
                                LLVMOrcLLJITBuilderRef Builder) {
  assert(Result && "Result can not be null");

  if (!Builder)
    Builder = LLVMOrcCreateLLJITBuilder();

  auto J = unwrap(Builder)->create();
  LLVMOrcDisposeLLJITBuilder(Builder);

  if (!J) {
    Result = nullptr;
    return wrap(J.takeError());
  }

  *Result = wrap(J->release());
  return LLVMErrorSuccess;
}

LLVMErrorRef LLVMOrcDisposeLLJIT(LLVMOrcLLJITRef J) {
  delete unwrap(J);
  return LLVMErrorSuccess;
}

LLVMOrcExecutionSessionRef LLVMOrcLLJITGetExecutionSession(LLVMOrcLLJITRef J) {
  return wrap(&unwrap(J)->getExecutionSession());
}

LLVMOrcJITDylibRef LLVMOrcLLJITGetMainJITDylib(LLVMOrcLLJITRef J) {
  return wrap(&unwrap(J)->getMainJITDylib());
}

const char *LLVMOrcLLJITGetTripleString(LLVMOrcLLJITRef J) {
  return unwrap(J)->getTargetTriple().str().c_str();
}

char LLVMOrcLLJITGetGlobalPrefix(LLVMOrcLLJITRef J) {
  return unwrap(J)->getDataLayout().getGlobalPrefix();
}

LLVMOrcSymbolStringPoolEntryRef
LLVMOrcLLJITMangleAndIntern(LLVMOrcLLJITRef J, const char *UnmangledName) {
  return wrap(SymbolStringPoolEntryUnsafe::take(
      unwrap(J)->mangleAndIntern(UnmangledName)));
}

LLVMErrorRef LLVMOrcLLJITAddObjectFile(LLVMOrcLLJITRef J, LLVMOrcJITDylibRef JD,
                                       LLVMMemoryBufferRef ObjBuffer) {
  return wrap(unwrap(J)->addObjectFile(
      *unwrap(JD), std::unique_ptr<MemoryBuffer>(unwrap(ObjBuffer))));
}

LLVMErrorRef LLVMOrcLLJITAddObjectFileWithRT(LLVMOrcLLJITRef J,
                                             LLVMOrcResourceTrackerRef RT,
                                             LLVMMemoryBufferRef ObjBuffer) {
  return wrap(unwrap(J)->addObjectFile(
      ResourceTrackerSP(unwrap(RT)),
      std::unique_ptr<MemoryBuffer>(unwrap(ObjBuffer))));
}

LLVMErrorRef LLVMOrcLLJITAddLLVMIRModule(LLVMOrcLLJITRef J,
                                         LLVMOrcJITDylibRef JD,
                                         LLVMOrcThreadSafeModuleRef TSM) {
  std::unique_ptr<ThreadSafeModule> TmpTSM(unwrap(TSM));
  return wrap(unwrap(J)->addIRModule(*unwrap(JD), std::move(*TmpTSM)));
}

LLVMErrorRef LLVMOrcLLJITAddLLVMIRModuleWithRT(LLVMOrcLLJITRef J,
                                               LLVMOrcResourceTrackerRef RT,
                                               LLVMOrcThreadSafeModuleRef TSM) {
  std::unique_ptr<ThreadSafeModule> TmpTSM(unwrap(TSM));
  return wrap(unwrap(J)->addIRModule(ResourceTrackerSP(unwrap(RT)),
                                     std::move(*TmpTSM)));
}

LLVMErrorRef LLVMOrcLLJITLookup(LLVMOrcLLJITRef J,
                                LLVMOrcJITTargetAddress *Result,
                                const char *Name) {
  assert(Result && "Result can not be null");

  auto Sym = unwrap(J)->lookup(Name);
  if (!Sym) {
    *Result = 0;
    return wrap(Sym.takeError());
  }

  *Result = Sym->getValue();
  return LLVMErrorSuccess;
}

LLVMOrcObjectLayerRef LLVMOrcLLJITGetObjLinkingLayer(LLVMOrcLLJITRef J) {
  return wrap(&unwrap(J)->getObjLinkingLayer());
}

LLVMOrcObjectTransformLayerRef
LLVMOrcLLJITGetObjTransformLayer(LLVMOrcLLJITRef J) {
  return wrap(&unwrap(J)->getObjTransformLayer());
}

LLVMOrcObjectLayerRef
LLVMOrcCreateRTDyldObjectLinkingLayerWithSectionMemoryManager(
    LLVMOrcExecutionSessionRef ES) {
  assert(ES && "ES must not be null");
  return wrap(new RTDyldObjectLinkingLayer(
      *unwrap(ES), [] { return std::make_unique<SectionMemoryManager>(); }));
}

LLVMOrcObjectLayerRef
LLVMOrcCreateRTDyldObjectLinkingLayerWithMCJITMemoryManagerLikeCallbacks(
    LLVMOrcExecutionSessionRef ES, void *CreateContextCtx,
    LLVMMemoryManagerCreateContextCallback CreateContext,
    LLVMMemoryManagerNotifyTerminatingCallback NotifyTerminating,
    LLVMMemoryManagerAllocateCodeSectionCallback AllocateCodeSection,
    LLVMMemoryManagerAllocateDataSectionCallback AllocateDataSection,
    LLVMMemoryManagerFinalizeMemoryCallback FinalizeMemory,
    LLVMMemoryManagerDestroyCallback Destroy) {

  struct MCJITMemoryManagerLikeCallbacks {
    MCJITMemoryManagerLikeCallbacks() = default;
    MCJITMemoryManagerLikeCallbacks(
        void *CreateContextCtx,
        LLVMMemoryManagerCreateContextCallback CreateContext,
        LLVMMemoryManagerNotifyTerminatingCallback NotifyTerminating,
        LLVMMemoryManagerAllocateCodeSectionCallback AllocateCodeSection,
        LLVMMemoryManagerAllocateDataSectionCallback AllocateDataSection,
        LLVMMemoryManagerFinalizeMemoryCallback FinalizeMemory,
        LLVMMemoryManagerDestroyCallback Destroy)
        : CreateContextCtx(CreateContextCtx), CreateContext(CreateContext),
          NotifyTerminating(NotifyTerminating),
          AllocateCodeSection(AllocateCodeSection),
          AllocateDataSection(AllocateDataSection),
          FinalizeMemory(FinalizeMemory), Destroy(Destroy) {}

    MCJITMemoryManagerLikeCallbacks(MCJITMemoryManagerLikeCallbacks &&Other) {
      std::swap(CreateContextCtx, Other.CreateContextCtx);
      std::swap(CreateContext, Other.CreateContext);
      std::swap(NotifyTerminating, Other.NotifyTerminating);
      std::swap(AllocateCodeSection, Other.AllocateCodeSection);
      std::swap(AllocateDataSection, Other.AllocateDataSection);
      std::swap(FinalizeMemory, Other.FinalizeMemory);
      std::swap(Destroy, Other.Destroy);
    }

    ~MCJITMemoryManagerLikeCallbacks() {
      if (NotifyTerminating)
        NotifyTerminating(CreateContextCtx);
    }

    void *CreateContextCtx = nullptr;
    LLVMMemoryManagerCreateContextCallback CreateContext = nullptr;
    LLVMMemoryManagerNotifyTerminatingCallback NotifyTerminating = nullptr;
    LLVMMemoryManagerAllocateCodeSectionCallback AllocateCodeSection = nullptr;
    LLVMMemoryManagerAllocateDataSectionCallback AllocateDataSection = nullptr;
    LLVMMemoryManagerFinalizeMemoryCallback FinalizeMemory = nullptr;
    LLVMMemoryManagerDestroyCallback Destroy = nullptr;
  };

  class MCJITMemoryManagerLikeCallbacksMemMgr : public RTDyldMemoryManager {
  public:
    MCJITMemoryManagerLikeCallbacksMemMgr(
        const MCJITMemoryManagerLikeCallbacks &CBs)
        : CBs(CBs) {
      Opaque = CBs.CreateContext(CBs.CreateContextCtx);
    }
    ~MCJITMemoryManagerLikeCallbacksMemMgr() override { CBs.Destroy(Opaque); }

    uint8_t *allocateCodeSection(uintptr_t Size, unsigned Alignment,
                                 unsigned SectionID,
                                 StringRef SectionName) override {
      return CBs.AllocateCodeSection(Opaque, Size, Alignment, SectionID,
                                     SectionName.str().c_str());
    }

    uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                                 unsigned SectionID, StringRef SectionName,
                                 bool isReadOnly) override {
      return CBs.AllocateDataSection(Opaque, Size, Alignment, SectionID,
                                     SectionName.str().c_str(), isReadOnly);
    }

    bool finalizeMemory(std::string *ErrMsg) override {
      char *ErrMsgCString = nullptr;
      bool Result = CBs.FinalizeMemory(Opaque, &ErrMsgCString);
      assert((Result || !ErrMsgCString) &&
             "Did not expect an error message if FinalizeMemory succeeded");
      if (ErrMsgCString) {
        if (ErrMsg)
          *ErrMsg = ErrMsgCString;
        free(ErrMsgCString);
      }
      return Result;
    }

  private:
    const MCJITMemoryManagerLikeCallbacks &CBs;
    void *Opaque = nullptr;
  };

  assert(ES && "ES must not be null");
  assert(CreateContext && "CreateContext must not be null");
  assert(NotifyTerminating && "NotifyTerminating must not be null");
  assert(AllocateCodeSection && "AllocateCodeSection must not be null");
  assert(AllocateDataSection && "AllocateDataSection must not be null");
  assert(FinalizeMemory && "FinalizeMemory must not be null");
  assert(Destroy && "Destroy must not be null");

  MCJITMemoryManagerLikeCallbacks CBs(
      CreateContextCtx, CreateContext, NotifyTerminating, AllocateCodeSection,
      AllocateDataSection, FinalizeMemory, Destroy);

  return wrap(new RTDyldObjectLinkingLayer(*unwrap(ES), [CBs = std::move(CBs)] {
    return std::make_unique<MCJITMemoryManagerLikeCallbacksMemMgr>(CBs);
  }));

  return nullptr;
}

void LLVMOrcRTDyldObjectLinkingLayerRegisterJITEventListener(
    LLVMOrcObjectLayerRef RTDyldObjLinkingLayer,
    LLVMJITEventListenerRef Listener) {
  assert(RTDyldObjLinkingLayer && "RTDyldObjLinkingLayer must not be null");
  assert(Listener && "Listener must not be null");
  reinterpret_cast<RTDyldObjectLinkingLayer *>(unwrap(RTDyldObjLinkingLayer))
      ->registerJITEventListener(*unwrap(Listener));
}

LLVMOrcIRTransformLayerRef LLVMOrcLLJITGetIRTransformLayer(LLVMOrcLLJITRef J) {
  return wrap(&unwrap(J)->getIRTransformLayer());
}

const char *LLVMOrcLLJITGetDataLayoutStr(LLVMOrcLLJITRef J) {
  return unwrap(J)->getDataLayout().getStringRepresentation().c_str();
}

LLVMOrcIndirectStubsManagerRef
LLVMOrcCreateLocalIndirectStubsManager(const char *TargetTriple) {
  auto builder = createLocalIndirectStubsManagerBuilder(Triple(TargetTriple));
  return wrap(builder().release());
}

void LLVMOrcDisposeIndirectStubsManager(LLVMOrcIndirectStubsManagerRef ISM) {
  std::unique_ptr<IndirectStubsManager> TmpISM(unwrap(ISM));
}

LLVMErrorRef LLVMOrcCreateLocalLazyCallThroughManager(
    const char *TargetTriple, LLVMOrcExecutionSessionRef ES,
    LLVMOrcJITTargetAddress ErrorHandlerAddr,
    LLVMOrcLazyCallThroughManagerRef *Result) {
  auto LCTM = createLocalLazyCallThroughManager(
      Triple(TargetTriple), *unwrap(ES), ExecutorAddr(ErrorHandlerAddr));

  if (!LCTM)
    return wrap(LCTM.takeError());
  *Result = wrap(LCTM->release());
  return LLVMErrorSuccess;
}

void LLVMOrcDisposeLazyCallThroughManager(
    LLVMOrcLazyCallThroughManagerRef LCM) {
  std::unique_ptr<LazyCallThroughManager> TmpLCM(unwrap(LCM));
}
