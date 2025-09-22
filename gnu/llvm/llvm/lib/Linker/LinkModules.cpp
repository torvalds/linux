//===- lib/Linker/LinkModules.cpp - Module Linker Implementation ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LLVM module linker.
//
//===----------------------------------------------------------------------===//

#include "LinkDiagnosticInfo.h"
#include "llvm-c/Linker.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/Error.h"
using namespace llvm;

namespace {

enum class LinkFrom { Dst, Src, Both };

/// This is an implementation class for the LinkModules function, which is the
/// entrypoint for this file.
class ModuleLinker {
  IRMover &Mover;
  std::unique_ptr<Module> SrcM;

  SetVector<GlobalValue *> ValuesToLink;

  /// For symbol clashes, prefer those from Src.
  unsigned Flags;

  /// List of global value names that should be internalized.
  StringSet<> Internalize;

  /// Function that will perform the actual internalization. The reason for a
  /// callback is that the linker cannot call internalizeModule without
  /// creating a circular dependency between IPO and the linker.
  std::function<void(Module &, const StringSet<> &)> InternalizeCallback;

  /// Used as the callback for lazy linking.
  /// The mover has just hit GV and we have to decide if it, and other members
  /// of the same comdat, should be linked. Every member to be linked is passed
  /// to Add.
  void addLazyFor(GlobalValue &GV, const IRMover::ValueAdder &Add);

  bool shouldOverrideFromSrc() { return Flags & Linker::OverrideFromSrc; }
  bool shouldLinkOnlyNeeded() { return Flags & Linker::LinkOnlyNeeded; }

  bool shouldLinkFromSource(bool &LinkFromSrc, const GlobalValue &Dest,
                            const GlobalValue &Src);

  /// Should we have mover and linker error diag info?
  bool emitError(const Twine &Message) {
    SrcM->getContext().diagnose(LinkDiagnosticInfo(DS_Error, Message));
    return true;
  }

  bool getComdatLeader(Module &M, StringRef ComdatName,
                       const GlobalVariable *&GVar);
  bool computeResultingSelectionKind(StringRef ComdatName,
                                     Comdat::SelectionKind Src,
                                     Comdat::SelectionKind Dst,
                                     Comdat::SelectionKind &Result,
                                     LinkFrom &From);
  DenseMap<const Comdat *, std::pair<Comdat::SelectionKind, LinkFrom>>
      ComdatsChosen;
  bool getComdatResult(const Comdat *SrcC, Comdat::SelectionKind &SK,
                       LinkFrom &From);
  // Keep track of the lazy linked global members of each comdat in source.
  DenseMap<const Comdat *, std::vector<GlobalValue *>> LazyComdatMembers;

  /// Given a global in the source module, return the global in the
  /// destination module that is being linked to, if any.
  GlobalValue *getLinkedToGlobal(const GlobalValue *SrcGV) {
    Module &DstM = Mover.getModule();
    // If the source has no name it can't link.  If it has local linkage,
    // there is no name match-up going on.
    if (!SrcGV->hasName() || GlobalValue::isLocalLinkage(SrcGV->getLinkage()))
      return nullptr;

    // Otherwise see if we have a match in the destination module's symtab.
    GlobalValue *DGV = DstM.getNamedValue(SrcGV->getName());
    if (!DGV)
      return nullptr;

    // If we found a global with the same name in the dest module, but it has
    // internal linkage, we are really not doing any linkage here.
    if (DGV->hasLocalLinkage())
      return nullptr;

    // Otherwise, we do in fact link to the destination global.
    return DGV;
  }

  /// Drop GV if it is a member of a comdat that we are dropping.
  /// This can happen with COFF's largest selection kind.
  void dropReplacedComdat(GlobalValue &GV,
                          const DenseSet<const Comdat *> &ReplacedDstComdats);

  bool linkIfNeeded(GlobalValue &GV, SmallVectorImpl<GlobalValue *> &GVToClone);

public:
  ModuleLinker(IRMover &Mover, std::unique_ptr<Module> SrcM, unsigned Flags,
               std::function<void(Module &, const StringSet<> &)>
                   InternalizeCallback = {})
      : Mover(Mover), SrcM(std::move(SrcM)), Flags(Flags),
        InternalizeCallback(std::move(InternalizeCallback)) {}

  bool run();
};
} // namespace

static GlobalValue::VisibilityTypes
getMinVisibility(GlobalValue::VisibilityTypes A,
                 GlobalValue::VisibilityTypes B) {
  if (A == GlobalValue::HiddenVisibility || B == GlobalValue::HiddenVisibility)
    return GlobalValue::HiddenVisibility;
  if (A == GlobalValue::ProtectedVisibility ||
      B == GlobalValue::ProtectedVisibility)
    return GlobalValue::ProtectedVisibility;
  return GlobalValue::DefaultVisibility;
}

bool ModuleLinker::getComdatLeader(Module &M, StringRef ComdatName,
                                   const GlobalVariable *&GVar) {
  const GlobalValue *GVal = M.getNamedValue(ComdatName);
  if (const auto *GA = dyn_cast_or_null<GlobalAlias>(GVal)) {
    GVal = GA->getAliaseeObject();
    if (!GVal)
      // We cannot resolve the size of the aliasee yet.
      return emitError("Linking COMDATs named '" + ComdatName +
                       "': COMDAT key involves incomputable alias size.");
  }

  GVar = dyn_cast_or_null<GlobalVariable>(GVal);
  if (!GVar)
    return emitError(
        "Linking COMDATs named '" + ComdatName +
        "': GlobalVariable required for data dependent selection!");

  return false;
}

bool ModuleLinker::computeResultingSelectionKind(StringRef ComdatName,
                                                 Comdat::SelectionKind Src,
                                                 Comdat::SelectionKind Dst,
                                                 Comdat::SelectionKind &Result,
                                                 LinkFrom &From) {
  Module &DstM = Mover.getModule();
  // The ability to mix Comdat::SelectionKind::Any with
  // Comdat::SelectionKind::Largest is a behavior that comes from COFF.
  bool DstAnyOrLargest = Dst == Comdat::SelectionKind::Any ||
                         Dst == Comdat::SelectionKind::Largest;
  bool SrcAnyOrLargest = Src == Comdat::SelectionKind::Any ||
                         Src == Comdat::SelectionKind::Largest;
  if (DstAnyOrLargest && SrcAnyOrLargest) {
    if (Dst == Comdat::SelectionKind::Largest ||
        Src == Comdat::SelectionKind::Largest)
      Result = Comdat::SelectionKind::Largest;
    else
      Result = Comdat::SelectionKind::Any;
  } else if (Src == Dst) {
    Result = Dst;
  } else {
    return emitError("Linking COMDATs named '" + ComdatName +
                     "': invalid selection kinds!");
  }

  switch (Result) {
  case Comdat::SelectionKind::Any:
    // Go with Dst.
    From = LinkFrom::Dst;
    break;
  case Comdat::SelectionKind::NoDeduplicate:
    From = LinkFrom::Both;
    break;
  case Comdat::SelectionKind::ExactMatch:
  case Comdat::SelectionKind::Largest:
  case Comdat::SelectionKind::SameSize: {
    const GlobalVariable *DstGV;
    const GlobalVariable *SrcGV;
    if (getComdatLeader(DstM, ComdatName, DstGV) ||
        getComdatLeader(*SrcM, ComdatName, SrcGV))
      return true;

    const DataLayout &DstDL = DstM.getDataLayout();
    const DataLayout &SrcDL = SrcM->getDataLayout();
    uint64_t DstSize = DstDL.getTypeAllocSize(DstGV->getValueType());
    uint64_t SrcSize = SrcDL.getTypeAllocSize(SrcGV->getValueType());
    if (Result == Comdat::SelectionKind::ExactMatch) {
      if (SrcGV->getInitializer() != DstGV->getInitializer())
        return emitError("Linking COMDATs named '" + ComdatName +
                         "': ExactMatch violated!");
      From = LinkFrom::Dst;
    } else if (Result == Comdat::SelectionKind::Largest) {
      From = SrcSize > DstSize ? LinkFrom::Src : LinkFrom::Dst;
    } else if (Result == Comdat::SelectionKind::SameSize) {
      if (SrcSize != DstSize)
        return emitError("Linking COMDATs named '" + ComdatName +
                         "': SameSize violated!");
      From = LinkFrom::Dst;
    } else {
      llvm_unreachable("unknown selection kind");
    }
    break;
  }
  }

  return false;
}

bool ModuleLinker::getComdatResult(const Comdat *SrcC,
                                   Comdat::SelectionKind &Result,
                                   LinkFrom &From) {
  Module &DstM = Mover.getModule();
  Comdat::SelectionKind SSK = SrcC->getSelectionKind();
  StringRef ComdatName = SrcC->getName();
  Module::ComdatSymTabType &ComdatSymTab = DstM.getComdatSymbolTable();
  Module::ComdatSymTabType::iterator DstCI = ComdatSymTab.find(ComdatName);

  if (DstCI == ComdatSymTab.end()) {
    // Use the comdat if it is only available in one of the modules.
    From = LinkFrom::Src;
    Result = SSK;
    return false;
  }

  const Comdat *DstC = &DstCI->second;
  Comdat::SelectionKind DSK = DstC->getSelectionKind();
  return computeResultingSelectionKind(ComdatName, SSK, DSK, Result, From);
}

bool ModuleLinker::shouldLinkFromSource(bool &LinkFromSrc,
                                        const GlobalValue &Dest,
                                        const GlobalValue &Src) {

  // Should we unconditionally use the Src?
  if (shouldOverrideFromSrc()) {
    LinkFromSrc = true;
    return false;
  }

  // We always have to add Src if it has appending linkage.
  if (Src.hasAppendingLinkage() || Dest.hasAppendingLinkage()) {
    LinkFromSrc = true;
    return false;
  }

  bool SrcIsDeclaration = Src.isDeclarationForLinker();
  bool DestIsDeclaration = Dest.isDeclarationForLinker();

  if (SrcIsDeclaration) {
    // If Src is external or if both Src & Dest are external..  Just link the
    // external globals, we aren't adding anything.
    if (Src.hasDLLImportStorageClass()) {
      // If one of GVs is marked as DLLImport, result should be dllimport'ed.
      LinkFromSrc = DestIsDeclaration;
      return false;
    }
    // If the Dest is weak, use the source linkage.
    if (Dest.hasExternalWeakLinkage()) {
      LinkFromSrc = true;
      return false;
    }
    // Link an available_externally over a declaration.
    LinkFromSrc = !Src.isDeclaration() && Dest.isDeclaration();
    return false;
  }

  if (DestIsDeclaration) {
    // If Dest is external but Src is not:
    LinkFromSrc = true;
    return false;
  }

  if (Src.hasCommonLinkage()) {
    if (Dest.hasLinkOnceLinkage() || Dest.hasWeakLinkage()) {
      LinkFromSrc = true;
      return false;
    }

    if (!Dest.hasCommonLinkage()) {
      LinkFromSrc = false;
      return false;
    }

    const DataLayout &DL = Dest.getDataLayout();
    uint64_t DestSize = DL.getTypeAllocSize(Dest.getValueType());
    uint64_t SrcSize = DL.getTypeAllocSize(Src.getValueType());
    LinkFromSrc = SrcSize > DestSize;
    return false;
  }

  if (Src.isWeakForLinker()) {
    assert(!Dest.hasExternalWeakLinkage());
    assert(!Dest.hasAvailableExternallyLinkage());

    if (Dest.hasLinkOnceLinkage() && Src.hasWeakLinkage()) {
      LinkFromSrc = true;
      return false;
    }

    LinkFromSrc = false;
    return false;
  }

  if (Dest.isWeakForLinker()) {
    assert(Src.hasExternalLinkage());
    LinkFromSrc = true;
    return false;
  }

  assert(!Src.hasExternalWeakLinkage());
  assert(!Dest.hasExternalWeakLinkage());
  assert(Dest.hasExternalLinkage() && Src.hasExternalLinkage() &&
         "Unexpected linkage type!");
  return emitError("Linking globals named '" + Src.getName() +
                   "': symbol multiply defined!");
}

bool ModuleLinker::linkIfNeeded(GlobalValue &GV,
                                SmallVectorImpl<GlobalValue *> &GVToClone) {
  GlobalValue *DGV = getLinkedToGlobal(&GV);

  if (shouldLinkOnlyNeeded()) {
    // Always import variables with appending linkage.
    if (!GV.hasAppendingLinkage()) {
      // Don't import globals unless they are referenced by the destination
      // module.
      if (!DGV)
        return false;
      // Don't import globals that are already defined in the destination module
      if (!DGV->isDeclaration())
        return false;
    }
  }

  if (DGV && !GV.hasLocalLinkage() && !GV.hasAppendingLinkage()) {
    auto *DGVar = dyn_cast<GlobalVariable>(DGV);
    auto *SGVar = dyn_cast<GlobalVariable>(&GV);
    if (DGVar && SGVar) {
      if (DGVar->isDeclaration() && SGVar->isDeclaration() &&
          (!DGVar->isConstant() || !SGVar->isConstant())) {
        DGVar->setConstant(false);
        SGVar->setConstant(false);
      }
      if (DGVar->hasCommonLinkage() && SGVar->hasCommonLinkage()) {
        MaybeAlign DAlign = DGVar->getAlign();
        MaybeAlign SAlign = SGVar->getAlign();
        MaybeAlign Align = std::nullopt;
        if (DAlign || SAlign)
          Align = std::max(DAlign.valueOrOne(), SAlign.valueOrOne());

        SGVar->setAlignment(Align);
        DGVar->setAlignment(Align);
      }
    }

    GlobalValue::VisibilityTypes Visibility =
        getMinVisibility(DGV->getVisibility(), GV.getVisibility());
    DGV->setVisibility(Visibility);
    GV.setVisibility(Visibility);

    GlobalValue::UnnamedAddr UnnamedAddr = GlobalValue::getMinUnnamedAddr(
        DGV->getUnnamedAddr(), GV.getUnnamedAddr());
    DGV->setUnnamedAddr(UnnamedAddr);
    GV.setUnnamedAddr(UnnamedAddr);
  }

  if (!DGV && !shouldOverrideFromSrc() &&
      (GV.hasLocalLinkage() || GV.hasLinkOnceLinkage() ||
       GV.hasAvailableExternallyLinkage()))
    return false;

  if (GV.isDeclaration())
    return false;

  LinkFrom ComdatFrom = LinkFrom::Dst;
  if (const Comdat *SC = GV.getComdat()) {
    std::tie(std::ignore, ComdatFrom) = ComdatsChosen[SC];
    if (ComdatFrom == LinkFrom::Dst)
      return false;
  }

  bool LinkFromSrc = true;
  if (DGV && shouldLinkFromSource(LinkFromSrc, *DGV, GV))
    return true;
  if (DGV && ComdatFrom == LinkFrom::Both)
    GVToClone.push_back(LinkFromSrc ? DGV : &GV);
  if (LinkFromSrc)
    ValuesToLink.insert(&GV);
  return false;
}

void ModuleLinker::addLazyFor(GlobalValue &GV, const IRMover::ValueAdder &Add) {
  // Add these to the internalize list
  if (!GV.hasLinkOnceLinkage() && !GV.hasAvailableExternallyLinkage() &&
      !shouldLinkOnlyNeeded())
    return;

  if (InternalizeCallback)
    Internalize.insert(GV.getName());
  Add(GV);

  const Comdat *SC = GV.getComdat();
  if (!SC)
    return;
  for (GlobalValue *GV2 : LazyComdatMembers[SC]) {
    GlobalValue *DGV = getLinkedToGlobal(GV2);
    bool LinkFromSrc = true;
    if (DGV && shouldLinkFromSource(LinkFromSrc, *DGV, *GV2))
      return;
    if (!LinkFromSrc)
      continue;
    if (InternalizeCallback)
      Internalize.insert(GV2->getName());
    Add(*GV2);
  }
}

void ModuleLinker::dropReplacedComdat(
    GlobalValue &GV, const DenseSet<const Comdat *> &ReplacedDstComdats) {
  Comdat *C = GV.getComdat();
  if (!C)
    return;
  if (!ReplacedDstComdats.count(C))
    return;
  if (GV.use_empty()) {
    GV.eraseFromParent();
    return;
  }

  if (auto *F = dyn_cast<Function>(&GV)) {
    F->deleteBody();
  } else if (auto *Var = dyn_cast<GlobalVariable>(&GV)) {
    Var->setInitializer(nullptr);
  } else {
    auto &Alias = cast<GlobalAlias>(GV);
    Module &M = *Alias.getParent();
    GlobalValue *Declaration;
    if (auto *FTy = dyn_cast<FunctionType>(Alias.getValueType())) {
      Declaration = Function::Create(FTy, GlobalValue::ExternalLinkage, "", &M);
    } else {
      Declaration =
          new GlobalVariable(M, Alias.getValueType(), /*isConstant*/ false,
                             GlobalValue::ExternalLinkage,
                             /*Initializer*/ nullptr);
    }
    Declaration->takeName(&Alias);
    Alias.replaceAllUsesWith(Declaration);
    Alias.eraseFromParent();
  }
}

bool ModuleLinker::run() {
  Module &DstM = Mover.getModule();
  DenseSet<const Comdat *> ReplacedDstComdats;
  DenseSet<const Comdat *> NonPrevailingComdats;

  for (const auto &SMEC : SrcM->getComdatSymbolTable()) {
    const Comdat &C = SMEC.getValue();
    if (ComdatsChosen.count(&C))
      continue;
    Comdat::SelectionKind SK;
    LinkFrom From;
    if (getComdatResult(&C, SK, From))
      return true;
    ComdatsChosen[&C] = std::make_pair(SK, From);

    if (From == LinkFrom::Dst)
      NonPrevailingComdats.insert(&C);

    if (From != LinkFrom::Src)
      continue;

    Module::ComdatSymTabType &ComdatSymTab = DstM.getComdatSymbolTable();
    Module::ComdatSymTabType::iterator DstCI = ComdatSymTab.find(C.getName());
    if (DstCI == ComdatSymTab.end())
      continue;

    // The source comdat is replacing the dest one.
    const Comdat *DstC = &DstCI->second;
    ReplacedDstComdats.insert(DstC);
  }

  // Alias have to go first, since we are not able to find their comdats
  // otherwise.
  for (GlobalAlias &GV : llvm::make_early_inc_range(DstM.aliases()))
    dropReplacedComdat(GV, ReplacedDstComdats);

  for (GlobalVariable &GV : llvm::make_early_inc_range(DstM.globals()))
    dropReplacedComdat(GV, ReplacedDstComdats);

  for (Function &GV : llvm::make_early_inc_range(DstM))
    dropReplacedComdat(GV, ReplacedDstComdats);

  if (!NonPrevailingComdats.empty()) {
    DenseSet<GlobalObject *> AliasedGlobals;
    for (auto &GA : SrcM->aliases())
      if (GlobalObject *GO = GA.getAliaseeObject(); GO && GO->getComdat())
        AliasedGlobals.insert(GO);
    for (const Comdat *C : NonPrevailingComdats) {
      SmallVector<GlobalObject *> ToUpdate;
      for (GlobalObject *GO : C->getUsers())
        if (GO->hasPrivateLinkage() && !AliasedGlobals.contains(GO))
          ToUpdate.push_back(GO);
      for (GlobalObject *GO : ToUpdate) {
        GO->setLinkage(GlobalValue::AvailableExternallyLinkage);
        GO->setComdat(nullptr);
      }
    }
  }

  for (GlobalVariable &GV : SrcM->globals())
    if (GV.hasLinkOnceLinkage())
      if (const Comdat *SC = GV.getComdat())
        LazyComdatMembers[SC].push_back(&GV);

  for (Function &SF : *SrcM)
    if (SF.hasLinkOnceLinkage())
      if (const Comdat *SC = SF.getComdat())
        LazyComdatMembers[SC].push_back(&SF);

  for (GlobalAlias &GA : SrcM->aliases())
    if (GA.hasLinkOnceLinkage())
      if (const Comdat *SC = GA.getComdat())
        LazyComdatMembers[SC].push_back(&GA);

  // Insert all of the globals in src into the DstM module... without linking
  // initializers (which could refer to functions not yet mapped over).
  SmallVector<GlobalValue *, 0> GVToClone;
  for (GlobalVariable &GV : SrcM->globals())
    if (linkIfNeeded(GV, GVToClone))
      return true;

  for (Function &SF : *SrcM)
    if (linkIfNeeded(SF, GVToClone))
      return true;

  for (GlobalAlias &GA : SrcM->aliases())
    if (linkIfNeeded(GA, GVToClone))
      return true;

  for (GlobalIFunc &GI : SrcM->ifuncs())
    if (linkIfNeeded(GI, GVToClone))
      return true;

  // For a variable in a comdat nodeduplicate, its initializer should be
  // preserved (its content may be implicitly used by other members) even if
  // symbol resolution does not pick it. Clone it into an unnamed private
  // variable.
  for (GlobalValue *GV : GVToClone) {
    if (auto *Var = dyn_cast<GlobalVariable>(GV)) {
      auto *NewVar = new GlobalVariable(*Var->getParent(), Var->getValueType(),
                                        Var->isConstant(), Var->getLinkage(),
                                        Var->getInitializer());
      NewVar->copyAttributesFrom(Var);
      NewVar->setVisibility(GlobalValue::DefaultVisibility);
      NewVar->setLinkage(GlobalValue::PrivateLinkage);
      NewVar->setDSOLocal(true);
      NewVar->setComdat(Var->getComdat());
      if (Var->getParent() != &Mover.getModule())
        ValuesToLink.insert(NewVar);
    } else {
      emitError("linking '" + GV->getName() +
                "': non-variables in comdat nodeduplicate are not handled");
    }
  }

  for (unsigned I = 0; I < ValuesToLink.size(); ++I) {
    GlobalValue *GV = ValuesToLink[I];
    const Comdat *SC = GV->getComdat();
    if (!SC)
      continue;
    for (GlobalValue *GV2 : LazyComdatMembers[SC]) {
      GlobalValue *DGV = getLinkedToGlobal(GV2);
      bool LinkFromSrc = true;
      if (DGV && shouldLinkFromSource(LinkFromSrc, *DGV, *GV2))
        return true;
      if (LinkFromSrc)
        ValuesToLink.insert(GV2);
    }
  }

  if (InternalizeCallback) {
    for (GlobalValue *GV : ValuesToLink)
      Internalize.insert(GV->getName());
  }

  // FIXME: Propagate Errors through to the caller instead of emitting
  // diagnostics.
  bool HasErrors = false;
  if (Error E =
          Mover.move(std::move(SrcM), ValuesToLink.getArrayRef(),
                     IRMover::LazyCallback(
                         [this](GlobalValue &GV, IRMover::ValueAdder Add) {
                           addLazyFor(GV, Add);
                         }),
                     /* IsPerformingImport */ false)) {
    handleAllErrors(std::move(E), [&](ErrorInfoBase &EIB) {
      DstM.getContext().diagnose(LinkDiagnosticInfo(DS_Error, EIB.message()));
      HasErrors = true;
    });
  }
  if (HasErrors)
    return true;

  if (InternalizeCallback)
    InternalizeCallback(DstM, Internalize);

  return false;
}

Linker::Linker(Module &M) : Mover(M) {}

bool Linker::linkInModule(
    std::unique_ptr<Module> Src, unsigned Flags,
    std::function<void(Module &, const StringSet<> &)> InternalizeCallback) {
  ModuleLinker ModLinker(Mover, std::move(Src), Flags,
                         std::move(InternalizeCallback));
  return ModLinker.run();
}

//===----------------------------------------------------------------------===//
// LinkModules entrypoint.
//===----------------------------------------------------------------------===//

/// This function links two modules together, with the resulting Dest module
/// modified to be the composite of the two input modules. If an error occurs,
/// true is returned and ErrorMsg (if not null) is set to indicate the problem.
/// Upon failure, the Dest module could be in a modified state, and shouldn't be
/// relied on to be consistent.
bool Linker::linkModules(
    Module &Dest, std::unique_ptr<Module> Src, unsigned Flags,
    std::function<void(Module &, const StringSet<> &)> InternalizeCallback) {
  Linker L(Dest);
  return L.linkInModule(std::move(Src), Flags, std::move(InternalizeCallback));
}

//===----------------------------------------------------------------------===//
// C API.
//===----------------------------------------------------------------------===//

LLVMBool LLVMLinkModules2(LLVMModuleRef Dest, LLVMModuleRef Src) {
  Module *D = unwrap(Dest);
  std::unique_ptr<Module> M(unwrap(Src));
  return Linker::linkModules(*D, std::move(M));
}
