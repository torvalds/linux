//===-- RTDyldObjectLinkingLayer.cpp - RuntimeDyld backed ORC ObjectLayer -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/Object/COFF.h"

namespace {

using namespace llvm;
using namespace llvm::orc;

class JITDylibSearchOrderResolver : public JITSymbolResolver {
public:
  JITDylibSearchOrderResolver(MaterializationResponsibility &MR,
                              SymbolDependenceMap &Deps)
      : MR(MR), Deps(Deps) {}

  void lookup(const LookupSet &Symbols, OnResolvedFunction OnResolved) override {
    auto &ES = MR.getTargetJITDylib().getExecutionSession();
    SymbolLookupSet InternedSymbols;

    // Intern the requested symbols: lookup takes interned strings.
    for (auto &S : Symbols)
      InternedSymbols.add(ES.intern(S));

    // Build an OnResolve callback to unwrap the interned strings and pass them
    // to the OnResolved callback.
    auto OnResolvedWithUnwrap =
        [OnResolved = std::move(OnResolved)](
            Expected<SymbolMap> InternedResult) mutable {
          if (!InternedResult) {
            OnResolved(InternedResult.takeError());
            return;
          }

          LookupResult Result;
          for (auto &KV : *InternedResult)
            Result[*KV.first] = {KV.second.getAddress().getValue(),
                                 KV.second.getFlags()};
          OnResolved(Result);
        };

    JITDylibSearchOrder LinkOrder;
    MR.getTargetJITDylib().withLinkOrderDo(
        [&](const JITDylibSearchOrder &LO) { LinkOrder = LO; });
    ES.lookup(
        LookupKind::Static, LinkOrder, InternedSymbols, SymbolState::Resolved,
        std::move(OnResolvedWithUnwrap),
        [this](const SymbolDependenceMap &LookupDeps) { Deps = LookupDeps; });
  }

  Expected<LookupSet> getResponsibilitySet(const LookupSet &Symbols) override {
    LookupSet Result;

    for (auto &KV : MR.getSymbols()) {
      if (Symbols.count(*KV.first))
        Result.insert(*KV.first);
    }

    return Result;
  }

private:
  MaterializationResponsibility &MR;
  SymbolDependenceMap &Deps;
};

} // end anonymous namespace

namespace llvm {
namespace orc {

char RTDyldObjectLinkingLayer::ID;

using BaseT = RTTIExtends<RTDyldObjectLinkingLayer, ObjectLayer>;

RTDyldObjectLinkingLayer::RTDyldObjectLinkingLayer(
    ExecutionSession &ES, GetMemoryManagerFunction GetMemoryManager)
    : BaseT(ES), GetMemoryManager(std::move(GetMemoryManager)) {
  ES.registerResourceManager(*this);
}

RTDyldObjectLinkingLayer::~RTDyldObjectLinkingLayer() {
  assert(MemMgrs.empty() && "Layer destroyed with resources still attached");
}

void RTDyldObjectLinkingLayer::emit(
    std::unique_ptr<MaterializationResponsibility> R,
    std::unique_ptr<MemoryBuffer> O) {
  assert(O && "Object must not be null");

  auto &ES = getExecutionSession();

  auto Obj = object::ObjectFile::createObjectFile(*O);

  if (!Obj) {
    getExecutionSession().reportError(Obj.takeError());
    R->failMaterialization();
    return;
  }

  // Collect the internal symbols from the object file: We will need to
  // filter these later.
  auto InternalSymbols = std::make_shared<std::set<StringRef>>();
  {
    SymbolFlagsMap ExtraSymbolsToClaim;
    for (auto &Sym : (*Obj)->symbols()) {

      // Skip file symbols.
      if (auto SymType = Sym.getType()) {
        if (*SymType == object::SymbolRef::ST_File)
          continue;
      } else {
        ES.reportError(SymType.takeError());
        R->failMaterialization();
        return;
      }

      Expected<uint32_t> SymFlagsOrErr = Sym.getFlags();
      if (!SymFlagsOrErr) {
        // TODO: Test this error.
        ES.reportError(SymFlagsOrErr.takeError());
        R->failMaterialization();
        return;
      }

      // Try to claim responsibility of weak symbols
      // if AutoClaimObjectSymbols flag is set.
      if (AutoClaimObjectSymbols &&
          (*SymFlagsOrErr & object::BasicSymbolRef::SF_Weak)) {
        auto SymName = Sym.getName();
        if (!SymName) {
          ES.reportError(SymName.takeError());
          R->failMaterialization();
          return;
        }

        // Already included in responsibility set, skip it
        SymbolStringPtr SymbolName = ES.intern(*SymName);
        if (R->getSymbols().count(SymbolName))
          continue;

        auto SymFlags = JITSymbolFlags::fromObjectSymbol(Sym);
        if (!SymFlags) {
          ES.reportError(SymFlags.takeError());
          R->failMaterialization();
          return;
        }

        ExtraSymbolsToClaim[SymbolName] = *SymFlags;
        continue;
      }

      // Don't include symbols that aren't global.
      if (!(*SymFlagsOrErr & object::BasicSymbolRef::SF_Global)) {
        if (auto SymName = Sym.getName())
          InternalSymbols->insert(*SymName);
        else {
          ES.reportError(SymName.takeError());
          R->failMaterialization();
          return;
        }
      }
    }

    if (!ExtraSymbolsToClaim.empty()) {
      if (auto Err = R->defineMaterializing(ExtraSymbolsToClaim)) {
        ES.reportError(std::move(Err));
        R->failMaterialization();
      }
    }
  }

  auto MemMgr = GetMemoryManager();
  auto &MemMgrRef = *MemMgr;

  // Switch to shared ownership of MR so that it can be captured by both
  // lambdas below.
  std::shared_ptr<MaterializationResponsibility> SharedR(std::move(R));
  auto Deps = std::make_unique<SymbolDependenceMap>();

  JITDylibSearchOrderResolver Resolver(*SharedR, *Deps);

  jitLinkForORC(
      object::OwningBinary<object::ObjectFile>(std::move(*Obj), std::move(O)),
      MemMgrRef, Resolver, ProcessAllSections,
      [this, SharedR, &MemMgrRef, InternalSymbols](
          const object::ObjectFile &Obj,
          RuntimeDyld::LoadedObjectInfo &LoadedObjInfo,
          std::map<StringRef, JITEvaluatedSymbol> ResolvedSymbols) {
        return onObjLoad(*SharedR, Obj, MemMgrRef, LoadedObjInfo,
                         ResolvedSymbols, *InternalSymbols);
      },
      [this, SharedR, MemMgr = std::move(MemMgr), Deps = std::move(Deps)](
          object::OwningBinary<object::ObjectFile> Obj,
          std::unique_ptr<RuntimeDyld::LoadedObjectInfo> LoadedObjInfo,
          Error Err) mutable {
        onObjEmit(*SharedR, std::move(Obj), std::move(MemMgr),
                  std::move(LoadedObjInfo), std::move(Deps), std::move(Err));
      });
}

void RTDyldObjectLinkingLayer::registerJITEventListener(JITEventListener &L) {
  std::lock_guard<std::mutex> Lock(RTDyldLayerMutex);
  assert(!llvm::is_contained(EventListeners, &L) &&
         "Listener has already been registered");
  EventListeners.push_back(&L);
}

void RTDyldObjectLinkingLayer::unregisterJITEventListener(JITEventListener &L) {
  std::lock_guard<std::mutex> Lock(RTDyldLayerMutex);
  auto I = llvm::find(EventListeners, &L);
  assert(I != EventListeners.end() && "Listener not registered");
  EventListeners.erase(I);
}

Error RTDyldObjectLinkingLayer::onObjLoad(
    MaterializationResponsibility &R, const object::ObjectFile &Obj,
    RuntimeDyld::MemoryManager &MemMgr,
    RuntimeDyld::LoadedObjectInfo &LoadedObjInfo,
    std::map<StringRef, JITEvaluatedSymbol> Resolved,
    std::set<StringRef> &InternalSymbols) {
  SymbolFlagsMap ExtraSymbolsToClaim;
  SymbolMap Symbols;

  // Hack to support COFF constant pool comdats introduced during compilation:
  // (See http://llvm.org/PR40074)
  if (auto *COFFObj = dyn_cast<object::COFFObjectFile>(&Obj)) {
    auto &ES = getExecutionSession();

    // For all resolved symbols that are not already in the responsibility set:
    // check whether the symbol is in a comdat section and if so mark it as
    // weak.
    for (auto &Sym : COFFObj->symbols()) {
      // getFlags() on COFF symbols can't fail.
      uint32_t SymFlags = cantFail(Sym.getFlags());
      if (SymFlags & object::BasicSymbolRef::SF_Undefined)
        continue;
      auto Name = Sym.getName();
      if (!Name)
        return Name.takeError();
      auto I = Resolved.find(*Name);

      // Skip unresolved symbols, internal symbols, and symbols that are
      // already in the responsibility set.
      if (I == Resolved.end() || InternalSymbols.count(*Name) ||
          R.getSymbols().count(ES.intern(*Name)))
        continue;
      auto Sec = Sym.getSection();
      if (!Sec)
        return Sec.takeError();
      if (*Sec == COFFObj->section_end())
        continue;
      auto &COFFSec = *COFFObj->getCOFFSection(**Sec);
      if (COFFSec.Characteristics & COFF::IMAGE_SCN_LNK_COMDAT)
        I->second.setFlags(I->second.getFlags() | JITSymbolFlags::Weak);
    }

    // Handle any aliases.
    for (auto &Sym : COFFObj->symbols()) {
      uint32_t SymFlags = cantFail(Sym.getFlags());
      if (SymFlags & object::BasicSymbolRef::SF_Undefined)
        continue;
      auto Name = Sym.getName();
      if (!Name)
        return Name.takeError();
      auto I = Resolved.find(*Name);

      // Skip already-resolved symbols, and symbols that we're not responsible
      // for.
      if (I != Resolved.end() || !R.getSymbols().count(ES.intern(*Name)))
        continue;

      // Skip anything other than weak externals.
      auto COFFSym = COFFObj->getCOFFSymbol(Sym);
      if (!COFFSym.isWeakExternal())
        continue;
      auto *WeakExternal = COFFSym.getAux<object::coff_aux_weak_external>();
      if (WeakExternal->Characteristics != COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS)
        continue;

      // We found an alias. Reuse the resolution of the alias target for the
      // alias itself.
      Expected<object::COFFSymbolRef> TargetSymbol =
          COFFObj->getSymbol(WeakExternal->TagIndex);
      if (!TargetSymbol)
        return TargetSymbol.takeError();
      Expected<StringRef> TargetName = COFFObj->getSymbolName(*TargetSymbol);
      if (!TargetName)
        return TargetName.takeError();
      auto J = Resolved.find(*TargetName);
      if (J == Resolved.end())
        return make_error<StringError>("Could alias target " + *TargetName +
                                           " not resolved",
                                       inconvertibleErrorCode());
      Resolved[*Name] = J->second;
    }
  }

  for (auto &KV : Resolved) {
    // Scan the symbols and add them to the Symbols map for resolution.

    // We never claim internal symbols.
    if (InternalSymbols.count(KV.first))
      continue;

    auto InternedName = getExecutionSession().intern(KV.first);
    auto Flags = KV.second.getFlags();
    auto I = R.getSymbols().find(InternedName);
    if (I != R.getSymbols().end()) {
      // Override object flags and claim responsibility for symbols if
      // requested.
      if (OverrideObjectFlags)
        Flags = I->second;
      else {
        // RuntimeDyld/MCJIT's weak tracking isn't compatible with ORC's. Even
        // if we're not overriding flags in general we should set the weak flag
        // according to the MaterializationResponsibility object symbol table.
        if (I->second.isWeak())
          Flags |= JITSymbolFlags::Weak;
      }
    } else if (AutoClaimObjectSymbols)
      ExtraSymbolsToClaim[InternedName] = Flags;

    Symbols[InternedName] = {ExecutorAddr(KV.second.getAddress()), Flags};
  }

  if (!ExtraSymbolsToClaim.empty()) {
    if (auto Err = R.defineMaterializing(ExtraSymbolsToClaim))
      return Err;

    // If we claimed responsibility for any weak symbols but were rejected then
    // we need to remove them from the resolved set.
    for (auto &KV : ExtraSymbolsToClaim)
      if (KV.second.isWeak() && !R.getSymbols().count(KV.first))
        Symbols.erase(KV.first);
  }

  if (auto Err = R.notifyResolved(Symbols)) {
    R.failMaterialization();
    return Err;
  }

  if (NotifyLoaded)
    NotifyLoaded(R, Obj, LoadedObjInfo);

  return Error::success();
}

void RTDyldObjectLinkingLayer::onObjEmit(
    MaterializationResponsibility &R,
    object::OwningBinary<object::ObjectFile> O,
    std::unique_ptr<RuntimeDyld::MemoryManager> MemMgr,
    std::unique_ptr<RuntimeDyld::LoadedObjectInfo> LoadedObjInfo,
    std::unique_ptr<SymbolDependenceMap> Deps, Error Err) {
  if (Err) {
    getExecutionSession().reportError(std::move(Err));
    R.failMaterialization();
    return;
  }

  SymbolDependenceGroup SDG;
  for (auto &[Sym, Flags] : R.getSymbols())
    SDG.Symbols.insert(Sym);
  SDG.Dependencies = std::move(*Deps);

  if (auto Err = R.notifyEmitted(SDG)) {
    getExecutionSession().reportError(std::move(Err));
    R.failMaterialization();
    return;
  }

  std::unique_ptr<object::ObjectFile> Obj;
  std::unique_ptr<MemoryBuffer> ObjBuffer;
  std::tie(Obj, ObjBuffer) = O.takeBinary();

  // Run EventListener notifyLoaded callbacks.
  {
    std::lock_guard<std::mutex> Lock(RTDyldLayerMutex);
    for (auto *L : EventListeners)
      L->notifyObjectLoaded(pointerToJITTargetAddress(MemMgr.get()), *Obj,
                            *LoadedObjInfo);
  }

  if (NotifyEmitted)
    NotifyEmitted(R, std::move(ObjBuffer));

  if (auto Err = R.withResourceKeyDo(
          [&](ResourceKey K) { MemMgrs[K].push_back(std::move(MemMgr)); })) {
    getExecutionSession().reportError(std::move(Err));
    R.failMaterialization();
  }
}

Error RTDyldObjectLinkingLayer::handleRemoveResources(JITDylib &JD,
                                                      ResourceKey K) {

  std::vector<MemoryManagerUP> MemMgrsToRemove;

  getExecutionSession().runSessionLocked([&] {
    auto I = MemMgrs.find(K);
    if (I != MemMgrs.end()) {
      std::swap(MemMgrsToRemove, I->second);
      MemMgrs.erase(I);
    }
  });

  {
    std::lock_guard<std::mutex> Lock(RTDyldLayerMutex);
    for (auto &MemMgr : MemMgrsToRemove) {
      for (auto *L : EventListeners)
        L->notifyFreeingObject(pointerToJITTargetAddress(MemMgr.get()));
      MemMgr->deregisterEHFrames();
    }
  }

  return Error::success();
}

void RTDyldObjectLinkingLayer::handleTransferResources(JITDylib &JD,
                                                       ResourceKey DstKey,
                                                       ResourceKey SrcKey) {
  auto I = MemMgrs.find(SrcKey);
  if (I != MemMgrs.end()) {
    auto &SrcMemMgrs = I->second;
    auto &DstMemMgrs = MemMgrs[DstKey];
    DstMemMgrs.reserve(DstMemMgrs.size() + SrcMemMgrs.size());
    for (auto &MemMgr : SrcMemMgrs)
      DstMemMgrs.push_back(std::move(MemMgr));

    // Erase SrcKey entry using value rather than iterator I: I may have been
    // invalidated when we looked up DstKey.
    MemMgrs.erase(SrcKey);
  }
}

} // End namespace orc.
} // End namespace llvm.
