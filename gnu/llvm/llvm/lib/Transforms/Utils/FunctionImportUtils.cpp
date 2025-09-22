//===- lib/Transforms/Utils/FunctionImportUtils.cpp - Importing utilities -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the FunctionImportGlobalProcessing class, used
// to perform the necessary global value handling for function importing.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/FunctionImportUtils.h"
#include "llvm/Support/CommandLine.h"
using namespace llvm;

/// Uses the "source_filename" instead of a Module hash ID for the suffix of
/// promoted locals during LTO. NOTE: This requires that the source filename
/// has a unique name / path to avoid name collisions.
static cl::opt<bool> UseSourceFilenameForPromotedLocals(
    "use-source-filename-for-promoted-locals", cl::Hidden,
    cl::desc("Uses the source file name instead of the Module hash. "
             "This requires that the source filename has a unique name / "
             "path to avoid name collisions."));

/// Checks if we should import SGV as a definition, otherwise import as a
/// declaration.
bool FunctionImportGlobalProcessing::doImportAsDefinition(
    const GlobalValue *SGV) {
  if (!isPerformingImport())
    return false;

  // Only import the globals requested for importing.
  if (!GlobalsToImport->count(const_cast<GlobalValue *>(SGV)))
    return false;

  assert(!isa<GlobalAlias>(SGV) &&
         "Unexpected global alias in the import list.");

  // Otherwise yes.
  return true;
}

bool FunctionImportGlobalProcessing::shouldPromoteLocalToGlobal(
    const GlobalValue *SGV, ValueInfo VI) {
  assert(SGV->hasLocalLinkage());

  // Ifuncs and ifunc alias does not have summary.
  if (isa<GlobalIFunc>(SGV) ||
      (isa<GlobalAlias>(SGV) &&
       isa<GlobalIFunc>(cast<GlobalAlias>(SGV)->getAliaseeObject())))
    return false;

  // Both the imported references and the original local variable must
  // be promoted.
  if (!isPerformingImport() && !isModuleExporting())
    return false;

  if (isPerformingImport()) {
    assert((!GlobalsToImport->count(const_cast<GlobalValue *>(SGV)) ||
            !isNonRenamableLocal(*SGV)) &&
           "Attempting to promote non-renamable local");
    // We don't know for sure yet if we are importing this value (as either
    // a reference or a def), since we are simply walking all values in the
    // module. But by necessity if we end up importing it and it is local,
    // it must be promoted, so unconditionally promote all values in the
    // importing module.
    return true;
  }

  // When exporting, consult the index. We can have more than one local
  // with the same GUID, in the case of same-named locals in different but
  // same-named source files that were compiled in their respective directories
  // (so the source file name and resulting GUID is the same). Find the one
  // in this module.
  auto Summary = ImportIndex.findSummaryInModule(
      VI, SGV->getParent()->getModuleIdentifier());
  assert(Summary && "Missing summary for global value when exporting");
  auto Linkage = Summary->linkage();
  if (!GlobalValue::isLocalLinkage(Linkage)) {
    assert(!isNonRenamableLocal(*SGV) &&
           "Attempting to promote non-renamable local");
    return true;
  }

  return false;
}

#ifndef NDEBUG
bool FunctionImportGlobalProcessing::isNonRenamableLocal(
    const GlobalValue &GV) const {
  if (!GV.hasLocalLinkage())
    return false;
  // This needs to stay in sync with the logic in buildModuleSummaryIndex.
  if (GV.hasSection())
    return true;
  if (Used.count(const_cast<GlobalValue *>(&GV)))
    return true;
  return false;
}
#endif

std::string
FunctionImportGlobalProcessing::getPromotedName(const GlobalValue *SGV) {
  assert(SGV->hasLocalLinkage());

  // For locals that must be promoted to global scope, ensure that
  // the promoted name uniquely identifies the copy in the original module,
  // using the ID assigned during combined index creation.
  if (UseSourceFilenameForPromotedLocals &&
      !SGV->getParent()->getSourceFileName().empty()) {
    SmallString<256> Suffix(SGV->getParent()->getSourceFileName());
    std::replace_if(std::begin(Suffix), std::end(Suffix),
                    [&](char ch) { return !isAlnum(ch); }, '_');
    return ModuleSummaryIndex::getGlobalNameForLocal(
        SGV->getName(), Suffix);
  }

  return ModuleSummaryIndex::getGlobalNameForLocal(
      SGV->getName(),
      ImportIndex.getModuleHash(SGV->getParent()->getModuleIdentifier()));
}

GlobalValue::LinkageTypes
FunctionImportGlobalProcessing::getLinkage(const GlobalValue *SGV,
                                           bool DoPromote) {
  // Any local variable that is referenced by an exported function needs
  // to be promoted to global scope. Since we don't currently know which
  // functions reference which local variables/functions, we must treat
  // all as potentially exported if this module is exporting anything.
  if (isModuleExporting()) {
    if (SGV->hasLocalLinkage() && DoPromote)
      return GlobalValue::ExternalLinkage;
    return SGV->getLinkage();
  }

  // Otherwise, if we aren't importing, no linkage change is needed.
  if (!isPerformingImport())
    return SGV->getLinkage();

  switch (SGV->getLinkage()) {
  case GlobalValue::LinkOnceODRLinkage:
  case GlobalValue::ExternalLinkage:
    // External and linkonce definitions are converted to available_externally
    // definitions upon import, so that they are available for inlining
    // and/or optimization, but are turned into declarations later
    // during the EliminateAvailableExternally pass.
    if (doImportAsDefinition(SGV) && !isa<GlobalAlias>(SGV))
      return GlobalValue::AvailableExternallyLinkage;
    // An imported external declaration stays external.
    return SGV->getLinkage();

  case GlobalValue::AvailableExternallyLinkage:
    // An imported available_externally definition converts
    // to external if imported as a declaration.
    if (!doImportAsDefinition(SGV))
      return GlobalValue::ExternalLinkage;
    // An imported available_externally declaration stays that way.
    return SGV->getLinkage();

  case GlobalValue::LinkOnceAnyLinkage:
  case GlobalValue::WeakAnyLinkage:
    // Can't import linkonce_any/weak_any definitions correctly, or we might
    // change the program semantics, since the linker will pick the first
    // linkonce_any/weak_any definition and importing would change the order
    // they are seen by the linker. The module linking caller needs to enforce
    // this.
    assert(!doImportAsDefinition(SGV));
    // If imported as a declaration, it becomes external_weak.
    return SGV->getLinkage();

  case GlobalValue::WeakODRLinkage:
    // For weak_odr linkage, there is a guarantee that all copies will be
    // equivalent, so the issue described above for weak_any does not exist,
    // and the definition can be imported. It can be treated similarly
    // to an imported externally visible global value.
    if (doImportAsDefinition(SGV) && !isa<GlobalAlias>(SGV))
      return GlobalValue::AvailableExternallyLinkage;
    else
      return GlobalValue::ExternalLinkage;

  case GlobalValue::AppendingLinkage:
    // It would be incorrect to import an appending linkage variable,
    // since it would cause global constructors/destructors to be
    // executed multiple times. This should have already been handled
    // by linkIfNeeded, and we will assert in shouldLinkFromSource
    // if we try to import, so we simply return AppendingLinkage.
    return GlobalValue::AppendingLinkage;

  case GlobalValue::InternalLinkage:
  case GlobalValue::PrivateLinkage:
    // If we are promoting the local to global scope, it is handled
    // similarly to a normal externally visible global.
    if (DoPromote) {
      if (doImportAsDefinition(SGV) && !isa<GlobalAlias>(SGV))
        return GlobalValue::AvailableExternallyLinkage;
      else
        return GlobalValue::ExternalLinkage;
    }
    // A non-promoted imported local definition stays local.
    // The ThinLTO pass will eventually force-import their definitions.
    return SGV->getLinkage();

  case GlobalValue::ExternalWeakLinkage:
    // External weak doesn't apply to definitions, must be a declaration.
    assert(!doImportAsDefinition(SGV));
    // Linkage stays external_weak.
    return SGV->getLinkage();

  case GlobalValue::CommonLinkage:
    // Linkage stays common on definitions.
    // The ThinLTO pass will eventually force-import their definitions.
    return SGV->getLinkage();
  }

  llvm_unreachable("unknown linkage type");
}

void FunctionImportGlobalProcessing::processGlobalForThinLTO(GlobalValue &GV) {

  ValueInfo VI;
  if (GV.hasName()) {
    VI = ImportIndex.getValueInfo(GV.getGUID());
    // Set synthetic function entry counts.
    if (VI && ImportIndex.hasSyntheticEntryCounts()) {
      if (Function *F = dyn_cast<Function>(&GV)) {
        if (!F->isDeclaration()) {
          for (const auto &S : VI.getSummaryList()) {
            auto *FS = cast<FunctionSummary>(S->getBaseObject());
            if (FS->modulePath() == M.getModuleIdentifier()) {
              F->setEntryCount(Function::ProfileCount(FS->entryCount(),
                                                      Function::PCT_Synthetic));
              break;
            }
          }
        }
      }
    }
  }

  // We should always have a ValueInfo (i.e. GV in index) for definitions when
  // we are exporting, and also when importing that value.
  assert(VI || GV.isDeclaration() ||
         (isPerformingImport() && !doImportAsDefinition(&GV)));

  // Mark read/write-only variables which can be imported with specific
  // attribute. We can't internalize them now because IRMover will fail
  // to link variable definitions to their external declarations during
  // ThinLTO import. We'll internalize read-only variables later, after
  // import is finished. See internalizeGVsAfterImport.
  //
  // If global value dead stripping is not enabled in summary then
  // propagateConstants hasn't been run. We can't internalize GV
  // in such case.
  if (!GV.isDeclaration() && VI && ImportIndex.withAttributePropagation()) {
    if (GlobalVariable *V = dyn_cast<GlobalVariable>(&GV)) {
      // We can have more than one local with the same GUID, in the case of
      // same-named locals in different but same-named source files that were
      // compiled in their respective directories (so the source file name
      // and resulting GUID is the same). Find the one in this module.
      // Handle the case where there is no summary found in this module. That
      // can happen in the distributed ThinLTO backend, because the index only
      // contains summaries from the source modules if they are being imported.
      // We might have a non-null VI and get here even in that case if the name
      // matches one in this module (e.g. weak or appending linkage).
      auto *GVS = dyn_cast_or_null<GlobalVarSummary>(
          ImportIndex.findSummaryInModule(VI, M.getModuleIdentifier()));
      if (GVS &&
          (ImportIndex.isReadOnly(GVS) || ImportIndex.isWriteOnly(GVS))) {
        V->addAttribute("thinlto-internalize");
        // Objects referenced by writeonly GV initializer should not be
        // promoted, because there is no any kind of read access to them
        // on behalf of this writeonly GV. To avoid promotion we convert
        // GV initializer to 'zeroinitializer'. This effectively drops
        // references in IR module (not in combined index), so we can
        // ignore them when computing import. We do not export references
        // of writeonly object. See computeImportForReferencedGlobals
        if (ImportIndex.isWriteOnly(GVS))
          V->setInitializer(Constant::getNullValue(V->getValueType()));
      }
    }
  }

  if (GV.hasLocalLinkage() && shouldPromoteLocalToGlobal(&GV, VI)) {
    // Save the original name string before we rename GV below.
    auto Name = GV.getName().str();
    GV.setName(getPromotedName(&GV));
    GV.setLinkage(getLinkage(&GV, /* DoPromote */ true));
    assert(!GV.hasLocalLinkage());
    GV.setVisibility(GlobalValue::HiddenVisibility);

    // If we are renaming a COMDAT leader, ensure that we record the COMDAT
    // for later renaming as well. This is required for COFF.
    if (const auto *C = GV.getComdat())
      if (C->getName() == Name)
        RenamedComdats.try_emplace(C, M.getOrInsertComdat(GV.getName()));
  } else
    GV.setLinkage(getLinkage(&GV, /* DoPromote */ false));

  // When ClearDSOLocalOnDeclarations is true, clear dso_local if GV is
  // converted to a declaration, to disable direct access. Don't do this if GV
  // is implicitly dso_local due to a non-default visibility.
  if (ClearDSOLocalOnDeclarations &&
      (GV.isDeclarationForLinker() ||
       (isPerformingImport() && !doImportAsDefinition(&GV))) &&
      !GV.isImplicitDSOLocal()) {
    GV.setDSOLocal(false);
  } else if (VI && VI.isDSOLocal(ImportIndex.withDSOLocalPropagation())) {
    // If all summaries are dso_local, symbol gets resolved to a known local
    // definition.
    GV.setDSOLocal(true);
    if (GV.hasDLLImportStorageClass())
      GV.setDLLStorageClass(GlobalValue::DefaultStorageClass);
  }

  // Remove functions imported as available externally defs from comdats,
  // as this is a declaration for the linker, and will be dropped eventually.
  // It is illegal for comdats to contain declarations.
  auto *GO = dyn_cast<GlobalObject>(&GV);
  if (GO && GO->isDeclarationForLinker() && GO->hasComdat()) {
    // The IRMover should not have placed any imported declarations in
    // a comdat, so the only declaration that should be in a comdat
    // at this point would be a definition imported as available_externally.
    assert(GO->hasAvailableExternallyLinkage() &&
           "Expected comdat on definition (possibly available external)");
    GO->setComdat(nullptr);
  }
}

void FunctionImportGlobalProcessing::processGlobalsForThinLTO() {
  for (GlobalVariable &GV : M.globals())
    processGlobalForThinLTO(GV);
  for (Function &SF : M)
    processGlobalForThinLTO(SF);
  for (GlobalAlias &GA : M.aliases())
    processGlobalForThinLTO(GA);

  // Replace any COMDATS that required renaming (because the COMDAT leader was
  // promoted and renamed).
  if (!RenamedComdats.empty())
    for (auto &GO : M.global_objects())
      if (auto *C = GO.getComdat()) {
        auto Replacement = RenamedComdats.find(C);
        if (Replacement != RenamedComdats.end())
          GO.setComdat(Replacement->second);
      }
}

bool FunctionImportGlobalProcessing::run() {
  processGlobalsForThinLTO();
  return false;
}

bool llvm::renameModuleForThinLTO(Module &M, const ModuleSummaryIndex &Index,
                                  bool ClearDSOLocalOnDeclarations,
                                  SetVector<GlobalValue *> *GlobalsToImport) {
  FunctionImportGlobalProcessing ThinLTOProcessing(M, Index, GlobalsToImport,
                                                   ClearDSOLocalOnDeclarations);
  return ThinLTOProcessing.run();
}
