//===--- SemaModule.cpp - Semantic Analysis for Modules -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for modules (C++ modules syntax,
//  Objective-C modules syntax, and Clang header modules).
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/SemaInternal.h"
#include "llvm/ADT/StringExtras.h"
#include <optional>

using namespace clang;
using namespace sema;

static void checkModuleImportContext(Sema &S, Module *M,
                                     SourceLocation ImportLoc, DeclContext *DC,
                                     bool FromInclude = false) {
  SourceLocation ExternCLoc;

  if (auto *LSD = dyn_cast<LinkageSpecDecl>(DC)) {
    switch (LSD->getLanguage()) {
    case LinkageSpecLanguageIDs::C:
      if (ExternCLoc.isInvalid())
        ExternCLoc = LSD->getBeginLoc();
      break;
    case LinkageSpecLanguageIDs::CXX:
      break;
    }
    DC = LSD->getParent();
  }

  while (isa<LinkageSpecDecl>(DC) || isa<ExportDecl>(DC))
    DC = DC->getParent();

  if (!isa<TranslationUnitDecl>(DC)) {
    S.Diag(ImportLoc, (FromInclude && S.isModuleVisible(M))
                          ? diag::ext_module_import_not_at_top_level_noop
                          : diag::err_module_import_not_at_top_level_fatal)
        << M->getFullModuleName() << DC;
    S.Diag(cast<Decl>(DC)->getBeginLoc(),
           diag::note_module_import_not_at_top_level)
        << DC;
  } else if (!M->IsExternC && ExternCLoc.isValid()) {
    S.Diag(ImportLoc, diag::ext_module_import_in_extern_c)
      << M->getFullModuleName();
    S.Diag(ExternCLoc, diag::note_extern_c_begins_here);
  }
}

// We represent the primary and partition names as 'Paths' which are sections
// of the hierarchical access path for a clang module.  However for C++20
// the periods in a name are just another character, and we will need to
// flatten them into a string.
static std::string stringFromPath(ModuleIdPath Path) {
  std::string Name;
  if (Path.empty())
    return Name;

  for (auto &Piece : Path) {
    if (!Name.empty())
      Name += ".";
    Name += Piece.first->getName();
  }
  return Name;
}

/// Helper function for makeTransitiveImportsVisible to decide whether
/// the \param Imported module unit is in the same module with the \param
/// CurrentModule.
/// \param FoundPrimaryModuleInterface is a helper parameter to record the
/// primary module interface unit corresponding to the module \param
/// CurrentModule. Since currently it is expensive to decide whether two module
/// units come from the same module by comparing the module name.
static bool
isImportingModuleUnitFromSameModule(ASTContext &Ctx, Module *Imported,
                                    Module *CurrentModule,
                                    Module *&FoundPrimaryModuleInterface) {
  if (!Imported->isNamedModule())
    return false;

  // The a partition unit we're importing must be in the same module of the
  // current module.
  if (Imported->isModulePartition())
    return true;

  // If we found the primary module interface during the search process, we can
  // return quickly to avoid expensive string comparison.
  if (FoundPrimaryModuleInterface)
    return Imported == FoundPrimaryModuleInterface;

  if (!CurrentModule)
    return false;

  // Then the imported module must be a primary module interface unit.  It
  // is only allowed to import the primary module interface unit from the same
  // module in the implementation unit and the implementation partition unit.

  // Since we'll handle implementation unit above. We can only care
  // about the implementation partition unit here.
  if (!CurrentModule->isModulePartitionImplementation())
    return false;

  if (Ctx.isInSameModule(Imported, CurrentModule)) {
    assert(!FoundPrimaryModuleInterface ||
           FoundPrimaryModuleInterface == Imported);
    FoundPrimaryModuleInterface = Imported;
    return true;
  }

  return false;
}

/// [module.import]p7:
///   Additionally, when a module-import-declaration in a module unit of some
///   module M imports another module unit U of M, it also imports all
///   translation units imported by non-exported module-import-declarations in
///   the module unit purview of U. These rules can in turn lead to the
///   importation of yet more translation units.
static void
makeTransitiveImportsVisible(ASTContext &Ctx, VisibleModuleSet &VisibleModules,
                             Module *Imported, Module *CurrentModule,
                             SourceLocation ImportLoc,
                             bool IsImportingPrimaryModuleInterface = false) {
  assert(Imported->isNamedModule() &&
         "'makeTransitiveImportsVisible()' is intended for standard C++ named "
         "modules only.");

  llvm::SmallVector<Module *, 4> Worklist;
  Worklist.push_back(Imported);

  Module *FoundPrimaryModuleInterface =
      IsImportingPrimaryModuleInterface ? Imported : nullptr;

  while (!Worklist.empty()) {
    Module *Importing = Worklist.pop_back_val();

    if (VisibleModules.isVisible(Importing))
      continue;

    // FIXME: The ImportLoc here is not meaningful. It may be problematic if we
    // use the sourcelocation loaded from the visible modules.
    VisibleModules.setVisible(Importing, ImportLoc);

    if (isImportingModuleUnitFromSameModule(Ctx, Importing, CurrentModule,
                                            FoundPrimaryModuleInterface))
      for (Module *TransImported : Importing->Imports)
        if (!VisibleModules.isVisible(TransImported))
          Worklist.push_back(TransImported);
  }
}

Sema::DeclGroupPtrTy
Sema::ActOnGlobalModuleFragmentDecl(SourceLocation ModuleLoc) {
  // We start in the global module;
  Module *GlobalModule =
      PushGlobalModuleFragment(ModuleLoc);

  // All declarations created from now on are owned by the global module.
  auto *TU = Context.getTranslationUnitDecl();
  // [module.global.frag]p2
  // A global-module-fragment specifies the contents of the global module
  // fragment for a module unit. The global module fragment can be used to
  // provide declarations that are attached to the global module and usable
  // within the module unit.
  //
  // So the declations in the global module shouldn't be visible by default.
  TU->setModuleOwnershipKind(Decl::ModuleOwnershipKind::ReachableWhenImported);
  TU->setLocalOwningModule(GlobalModule);

  // FIXME: Consider creating an explicit representation of this declaration.
  return nullptr;
}

void Sema::HandleStartOfHeaderUnit() {
  assert(getLangOpts().CPlusPlusModules &&
         "Header units are only valid for C++20 modules");
  SourceLocation StartOfTU =
      SourceMgr.getLocForStartOfFile(SourceMgr.getMainFileID());

  StringRef HUName = getLangOpts().CurrentModule;
  if (HUName.empty()) {
    HUName =
        SourceMgr.getFileEntryRefForID(SourceMgr.getMainFileID())->getName();
    const_cast<LangOptions &>(getLangOpts()).CurrentModule = HUName.str();
  }

  // TODO: Make the C++20 header lookup independent.
  // When the input is pre-processed source, we need a file ref to the original
  // file for the header map.
  auto F = SourceMgr.getFileManager().getOptionalFileRef(HUName);
  // For the sake of error recovery (if someone has moved the original header
  // after creating the pre-processed output) fall back to obtaining the file
  // ref for the input file, which must be present.
  if (!F)
    F = SourceMgr.getFileEntryRefForID(SourceMgr.getMainFileID());
  assert(F && "failed to find the header unit source?");
  Module::Header H{HUName.str(), HUName.str(), *F};
  auto &Map = PP.getHeaderSearchInfo().getModuleMap();
  Module *Mod = Map.createHeaderUnit(StartOfTU, HUName, H);
  assert(Mod && "module creation should not fail");
  ModuleScopes.push_back({}); // No GMF
  ModuleScopes.back().BeginLoc = StartOfTU;
  ModuleScopes.back().Module = Mod;
  VisibleModules.setVisible(Mod, StartOfTU);

  // From now on, we have an owning module for all declarations we see.
  // All of these are implicitly exported.
  auto *TU = Context.getTranslationUnitDecl();
  TU->setModuleOwnershipKind(Decl::ModuleOwnershipKind::Visible);
  TU->setLocalOwningModule(Mod);
}

/// Tests whether the given identifier is reserved as a module name and
/// diagnoses if it is. Returns true if a diagnostic is emitted and false
/// otherwise.
static bool DiagReservedModuleName(Sema &S, const IdentifierInfo *II,
                                   SourceLocation Loc) {
  enum {
    Valid = -1,
    Invalid = 0,
    Reserved = 1,
  } Reason = Valid;

  if (II->isStr("module") || II->isStr("import"))
    Reason = Invalid;
  else if (II->isReserved(S.getLangOpts()) !=
           ReservedIdentifierStatus::NotReserved)
    Reason = Reserved;

  // If the identifier is reserved (not invalid) but is in a system header,
  // we do not diagnose (because we expect system headers to use reserved
  // identifiers).
  if (Reason == Reserved && S.getSourceManager().isInSystemHeader(Loc))
    Reason = Valid;

  switch (Reason) {
  case Valid:
    return false;
  case Invalid:
    return S.Diag(Loc, diag::err_invalid_module_name) << II;
  case Reserved:
    S.Diag(Loc, diag::warn_reserved_module_name) << II;
    return false;
  }
  llvm_unreachable("fell off a fully covered switch");
}

Sema::DeclGroupPtrTy
Sema::ActOnModuleDecl(SourceLocation StartLoc, SourceLocation ModuleLoc,
                      ModuleDeclKind MDK, ModuleIdPath Path,
                      ModuleIdPath Partition, ModuleImportState &ImportState) {
  assert(getLangOpts().CPlusPlusModules &&
         "should only have module decl in standard C++ modules");

  bool IsFirstDecl = ImportState == ModuleImportState::FirstDecl;
  bool SeenGMF = ImportState == ModuleImportState::GlobalFragment;
  // If any of the steps here fail, we count that as invalidating C++20
  // module state;
  ImportState = ModuleImportState::NotACXX20Module;

  bool IsPartition = !Partition.empty();
  if (IsPartition)
    switch (MDK) {
    case ModuleDeclKind::Implementation:
      MDK = ModuleDeclKind::PartitionImplementation;
      break;
    case ModuleDeclKind::Interface:
      MDK = ModuleDeclKind::PartitionInterface;
      break;
    default:
      llvm_unreachable("how did we get a partition type set?");
    }

  // A (non-partition) module implementation unit requires that we are not
  // compiling a module of any kind.  A partition implementation emits an
  // interface (and the AST for the implementation), which will subsequently
  // be consumed to emit a binary.
  // A module interface unit requires that we are not compiling a module map.
  switch (getLangOpts().getCompilingModule()) {
  case LangOptions::CMK_None:
    // It's OK to compile a module interface as a normal translation unit.
    break;

  case LangOptions::CMK_ModuleInterface:
    if (MDK != ModuleDeclKind::Implementation)
      break;

    // We were asked to compile a module interface unit but this is a module
    // implementation unit.
    Diag(ModuleLoc, diag::err_module_interface_implementation_mismatch)
      << FixItHint::CreateInsertion(ModuleLoc, "export ");
    MDK = ModuleDeclKind::Interface;
    break;

  case LangOptions::CMK_ModuleMap:
    Diag(ModuleLoc, diag::err_module_decl_in_module_map_module);
    return nullptr;

  case LangOptions::CMK_HeaderUnit:
    Diag(ModuleLoc, diag::err_module_decl_in_header_unit);
    return nullptr;
  }

  assert(ModuleScopes.size() <= 1 && "expected to be at global module scope");

  // FIXME: Most of this work should be done by the preprocessor rather than
  // here, in order to support macro import.

  // Only one module-declaration is permitted per source file.
  if (isCurrentModulePurview()) {
    Diag(ModuleLoc, diag::err_module_redeclaration);
    Diag(VisibleModules.getImportLoc(ModuleScopes.back().Module),
         diag::note_prev_module_declaration);
    return nullptr;
  }

  assert((!getLangOpts().CPlusPlusModules ||
          SeenGMF == (bool)this->TheGlobalModuleFragment) &&
         "mismatched global module state");

  // In C++20, the module-declaration must be the first declaration if there
  // is no global module fragment.
  if (getLangOpts().CPlusPlusModules && !IsFirstDecl && !SeenGMF) {
    Diag(ModuleLoc, diag::err_module_decl_not_at_start);
    SourceLocation BeginLoc =
        ModuleScopes.empty()
            ? SourceMgr.getLocForStartOfFile(SourceMgr.getMainFileID())
            : ModuleScopes.back().BeginLoc;
    if (BeginLoc.isValid()) {
      Diag(BeginLoc, diag::note_global_module_introducer_missing)
          << FixItHint::CreateInsertion(BeginLoc, "module;\n");
    }
  }

  // C++23 [module.unit]p1: ... The identifiers module and import shall not
  // appear as identifiers in a module-name or module-partition. All
  // module-names either beginning with an identifier consisting of std
  // followed by zero or more digits or containing a reserved identifier
  // ([lex.name]) are reserved and shall not be specified in a
  // module-declaration; no diagnostic is required.

  // Test the first part of the path to see if it's std[0-9]+ but allow the
  // name in a system header.
  StringRef FirstComponentName = Path[0].first->getName();
  if (!getSourceManager().isInSystemHeader(Path[0].second) &&
      (FirstComponentName == "std" ||
       (FirstComponentName.starts_with("std") &&
        llvm::all_of(FirstComponentName.drop_front(3), &llvm::isDigit))))
    Diag(Path[0].second, diag::warn_reserved_module_name) << Path[0].first;

  // Then test all of the components in the path to see if any of them are
  // using another kind of reserved or invalid identifier.
  for (auto Part : Path) {
    if (DiagReservedModuleName(*this, Part.first, Part.second))
      return nullptr;
  }

  // Flatten the dots in a module name. Unlike Clang's hierarchical module map
  // modules, the dots here are just another character that can appear in a
  // module name.
  std::string ModuleName = stringFromPath(Path);
  if (IsPartition) {
    ModuleName += ":";
    ModuleName += stringFromPath(Partition);
  }
  // If a module name was explicitly specified on the command line, it must be
  // correct.
  if (!getLangOpts().CurrentModule.empty() &&
      getLangOpts().CurrentModule != ModuleName) {
    Diag(Path.front().second, diag::err_current_module_name_mismatch)
        << SourceRange(Path.front().second, IsPartition
                                                ? Partition.back().second
                                                : Path.back().second)
        << getLangOpts().CurrentModule;
    return nullptr;
  }
  const_cast<LangOptions&>(getLangOpts()).CurrentModule = ModuleName;

  auto &Map = PP.getHeaderSearchInfo().getModuleMap();
  Module *Mod;                 // The module we are creating.
  Module *Interface = nullptr; // The interface for an implementation.
  switch (MDK) {
  case ModuleDeclKind::Interface:
  case ModuleDeclKind::PartitionInterface: {
    // We can't have parsed or imported a definition of this module or parsed a
    // module map defining it already.
    if (auto *M = Map.findModule(ModuleName)) {
      Diag(Path[0].second, diag::err_module_redefinition) << ModuleName;
      if (M->DefinitionLoc.isValid())
        Diag(M->DefinitionLoc, diag::note_prev_module_definition);
      else if (OptionalFileEntryRef FE = M->getASTFile())
        Diag(M->DefinitionLoc, diag::note_prev_module_definition_from_ast_file)
            << FE->getName();
      Mod = M;
      break;
    }

    // Create a Module for the module that we're defining.
    Mod = Map.createModuleForInterfaceUnit(ModuleLoc, ModuleName);
    if (MDK == ModuleDeclKind::PartitionInterface)
      Mod->Kind = Module::ModulePartitionInterface;
    assert(Mod && "module creation should not fail");
    break;
  }

  case ModuleDeclKind::Implementation: {
    // C++20 A module-declaration that contains neither an export-
    // keyword nor a module-partition implicitly imports the primary
    // module interface unit of the module as if by a module-import-
    // declaration.
    std::pair<IdentifierInfo *, SourceLocation> ModuleNameLoc(
        PP.getIdentifierInfo(ModuleName), Path[0].second);

    // The module loader will assume we're trying to import the module that
    // we're building if `LangOpts.CurrentModule` equals to 'ModuleName'.
    // Change the value for `LangOpts.CurrentModule` temporarily to make the
    // module loader work properly.
    const_cast<LangOptions &>(getLangOpts()).CurrentModule = "";
    Interface = getModuleLoader().loadModule(ModuleLoc, {ModuleNameLoc},
                                             Module::AllVisible,
                                             /*IsInclusionDirective=*/false);
    const_cast<LangOptions&>(getLangOpts()).CurrentModule = ModuleName;

    if (!Interface) {
      Diag(ModuleLoc, diag::err_module_not_defined) << ModuleName;
      // Create an empty module interface unit for error recovery.
      Mod = Map.createModuleForInterfaceUnit(ModuleLoc, ModuleName);
    } else {
      Mod = Map.createModuleForImplementationUnit(ModuleLoc, ModuleName);
    }
  } break;

  case ModuleDeclKind::PartitionImplementation:
    // Create an interface, but note that it is an implementation
    // unit.
    Mod = Map.createModuleForInterfaceUnit(ModuleLoc, ModuleName);
    Mod->Kind = Module::ModulePartitionImplementation;
    break;
  }

  if (!this->TheGlobalModuleFragment) {
    ModuleScopes.push_back({});
    if (getLangOpts().ModulesLocalVisibility)
      ModuleScopes.back().OuterVisibleModules = std::move(VisibleModules);
  } else {
    // We're done with the global module fragment now.
    ActOnEndOfTranslationUnitFragment(TUFragmentKind::Global);
  }

  // Switch from the global module fragment (if any) to the named module.
  ModuleScopes.back().BeginLoc = StartLoc;
  ModuleScopes.back().Module = Mod;
  VisibleModules.setVisible(Mod, ModuleLoc);

  // From now on, we have an owning module for all declarations we see.
  // In C++20 modules, those declaration would be reachable when imported
  // unless explicitily exported.
  // Otherwise, those declarations are module-private unless explicitly
  // exported.
  auto *TU = Context.getTranslationUnitDecl();
  TU->setModuleOwnershipKind(Decl::ModuleOwnershipKind::ReachableWhenImported);
  TU->setLocalOwningModule(Mod);

  // We are in the module purview, but before any other (non import)
  // statements, so imports are allowed.
  ImportState = ModuleImportState::ImportAllowed;

  getASTContext().setCurrentNamedModule(Mod);

  if (auto *Listener = getASTMutationListener())
    Listener->EnteringModulePurview();

  // We already potentially made an implicit import (in the case of a module
  // implementation unit importing its interface).  Make this module visible
  // and return the import decl to be added to the current TU.
  if (Interface) {

    makeTransitiveImportsVisible(getASTContext(), VisibleModules, Interface,
                                 Mod, ModuleLoc,
                                 /*IsImportingPrimaryModuleInterface=*/true);

    // Make the import decl for the interface in the impl module.
    ImportDecl *Import = ImportDecl::Create(Context, CurContext, ModuleLoc,
                                            Interface, Path[0].second);
    CurContext->addDecl(Import);

    // Sequence initialization of the imported module before that of the current
    // module, if any.
    Context.addModuleInitializer(ModuleScopes.back().Module, Import);
    Mod->Imports.insert(Interface); // As if we imported it.
    // Also save this as a shortcut to checking for decls in the interface
    ThePrimaryInterface = Interface;
    // If we made an implicit import of the module interface, then return the
    // imported module decl.
    return ConvertDeclToDeclGroup(Import);
  }

  return nullptr;
}

Sema::DeclGroupPtrTy
Sema::ActOnPrivateModuleFragmentDecl(SourceLocation ModuleLoc,
                                     SourceLocation PrivateLoc) {
  // C++20 [basic.link]/2:
  //   A private-module-fragment shall appear only in a primary module
  //   interface unit.
  switch (ModuleScopes.empty() ? Module::ExplicitGlobalModuleFragment
                               : ModuleScopes.back().Module->Kind) {
  case Module::ModuleMapModule:
  case Module::ExplicitGlobalModuleFragment:
  case Module::ImplicitGlobalModuleFragment:
  case Module::ModulePartitionImplementation:
  case Module::ModulePartitionInterface:
  case Module::ModuleHeaderUnit:
    Diag(PrivateLoc, diag::err_private_module_fragment_not_module);
    return nullptr;

  case Module::PrivateModuleFragment:
    Diag(PrivateLoc, diag::err_private_module_fragment_redefined);
    Diag(ModuleScopes.back().BeginLoc, diag::note_previous_definition);
    return nullptr;

  case Module::ModuleImplementationUnit:
    Diag(PrivateLoc, diag::err_private_module_fragment_not_module_interface);
    Diag(ModuleScopes.back().BeginLoc,
         diag::note_not_module_interface_add_export)
        << FixItHint::CreateInsertion(ModuleScopes.back().BeginLoc, "export ");
    return nullptr;

  case Module::ModuleInterfaceUnit:
    break;
  }

  // FIXME: Check that this translation unit does not import any partitions;
  // such imports would violate [basic.link]/2's "shall be the only module unit"
  // restriction.

  // We've finished the public fragment of the translation unit.
  ActOnEndOfTranslationUnitFragment(TUFragmentKind::Normal);

  auto &Map = PP.getHeaderSearchInfo().getModuleMap();
  Module *PrivateModuleFragment =
      Map.createPrivateModuleFragmentForInterfaceUnit(
          ModuleScopes.back().Module, PrivateLoc);
  assert(PrivateModuleFragment && "module creation should not fail");

  // Enter the scope of the private module fragment.
  ModuleScopes.push_back({});
  ModuleScopes.back().BeginLoc = ModuleLoc;
  ModuleScopes.back().Module = PrivateModuleFragment;
  VisibleModules.setVisible(PrivateModuleFragment, ModuleLoc);

  // All declarations created from now on are scoped to the private module
  // fragment (and are neither visible nor reachable in importers of the module
  // interface).
  auto *TU = Context.getTranslationUnitDecl();
  TU->setModuleOwnershipKind(Decl::ModuleOwnershipKind::ModulePrivate);
  TU->setLocalOwningModule(PrivateModuleFragment);

  // FIXME: Consider creating an explicit representation of this declaration.
  return nullptr;
}

DeclResult Sema::ActOnModuleImport(SourceLocation StartLoc,
                                   SourceLocation ExportLoc,
                                   SourceLocation ImportLoc, ModuleIdPath Path,
                                   bool IsPartition) {
  assert((!IsPartition || getLangOpts().CPlusPlusModules) &&
         "partition seen in non-C++20 code?");

  // For a C++20 module name, flatten into a single identifier with the source
  // location of the first component.
  std::pair<IdentifierInfo *, SourceLocation> ModuleNameLoc;

  std::string ModuleName;
  if (IsPartition) {
    // We already checked that we are in a module purview in the parser.
    assert(!ModuleScopes.empty() && "in a module purview, but no module?");
    Module *NamedMod = ModuleScopes.back().Module;
    // If we are importing into a partition, find the owning named module,
    // otherwise, the name of the importing named module.
    ModuleName = NamedMod->getPrimaryModuleInterfaceName().str();
    ModuleName += ":";
    ModuleName += stringFromPath(Path);
    ModuleNameLoc = {PP.getIdentifierInfo(ModuleName), Path[0].second};
    Path = ModuleIdPath(ModuleNameLoc);
  } else if (getLangOpts().CPlusPlusModules) {
    ModuleName = stringFromPath(Path);
    ModuleNameLoc = {PP.getIdentifierInfo(ModuleName), Path[0].second};
    Path = ModuleIdPath(ModuleNameLoc);
  }

  // Diagnose self-import before attempting a load.
  // [module.import]/9
  // A module implementation unit of a module M that is not a module partition
  // shall not contain a module-import-declaration nominating M.
  // (for an implementation, the module interface is imported implicitly,
  //  but that's handled in the module decl code).

  if (getLangOpts().CPlusPlusModules && isCurrentModulePurview() &&
      getCurrentModule()->Name == ModuleName) {
    Diag(ImportLoc, diag::err_module_self_import_cxx20)
        << ModuleName << currentModuleIsImplementation();
    return true;
  }

  Module *Mod = getModuleLoader().loadModule(
      ImportLoc, Path, Module::AllVisible, /*IsInclusionDirective=*/false);
  if (!Mod)
    return true;

  if (!Mod->isInterfaceOrPartition() && !ModuleName.empty() &&
      !getLangOpts().ObjC) {
    Diag(ImportLoc, diag::err_module_import_non_interface_nor_parition)
        << ModuleName;
    return true;
  }

  return ActOnModuleImport(StartLoc, ExportLoc, ImportLoc, Mod, Path);
}

/// Determine whether \p D is lexically within an export-declaration.
static const ExportDecl *getEnclosingExportDecl(const Decl *D) {
  for (auto *DC = D->getLexicalDeclContext(); DC; DC = DC->getLexicalParent())
    if (auto *ED = dyn_cast<ExportDecl>(DC))
      return ED;
  return nullptr;
}

DeclResult Sema::ActOnModuleImport(SourceLocation StartLoc,
                                   SourceLocation ExportLoc,
                                   SourceLocation ImportLoc, Module *Mod,
                                   ModuleIdPath Path) {
  if (Mod->isHeaderUnit())
    Diag(ImportLoc, diag::warn_experimental_header_unit);

  if (Mod->isNamedModule())
    makeTransitiveImportsVisible(getASTContext(), VisibleModules, Mod,
                                 getCurrentModule(), ImportLoc);
  else
    VisibleModules.setVisible(Mod, ImportLoc);

  checkModuleImportContext(*this, Mod, ImportLoc, CurContext);

  // FIXME: we should support importing a submodule within a different submodule
  // of the same top-level module. Until we do, make it an error rather than
  // silently ignoring the import.
  // FIXME: Should we warn on a redundant import of the current module?
  if (Mod->isForBuilding(getLangOpts())) {
    Diag(ImportLoc, getLangOpts().isCompilingModule()
                        ? diag::err_module_self_import
                        : diag::err_module_import_in_implementation)
        << Mod->getFullModuleName() << getLangOpts().CurrentModule;
  }

  SmallVector<SourceLocation, 2> IdentifierLocs;

  if (Path.empty()) {
    // If this was a header import, pad out with dummy locations.
    // FIXME: Pass in and use the location of the header-name token in this
    // case.
    for (Module *ModCheck = Mod; ModCheck; ModCheck = ModCheck->Parent)
      IdentifierLocs.push_back(SourceLocation());
  } else if (getLangOpts().CPlusPlusModules && !Mod->Parent) {
    // A single identifier for the whole name.
    IdentifierLocs.push_back(Path[0].second);
  } else {
    Module *ModCheck = Mod;
    for (unsigned I = 0, N = Path.size(); I != N; ++I) {
      // If we've run out of module parents, just drop the remaining
      // identifiers.  We need the length to be consistent.
      if (!ModCheck)
        break;
      ModCheck = ModCheck->Parent;

      IdentifierLocs.push_back(Path[I].second);
    }
  }

  ImportDecl *Import = ImportDecl::Create(Context, CurContext, StartLoc,
                                          Mod, IdentifierLocs);
  CurContext->addDecl(Import);

  // Sequence initialization of the imported module before that of the current
  // module, if any.
  if (!ModuleScopes.empty())
    Context.addModuleInitializer(ModuleScopes.back().Module, Import);

  // A module (partition) implementation unit shall not be exported.
  if (getLangOpts().CPlusPlusModules && ExportLoc.isValid() &&
      Mod->Kind == Module::ModuleKind::ModulePartitionImplementation) {
    Diag(ExportLoc, diag::err_export_partition_impl)
        << SourceRange(ExportLoc, Path.back().second);
  } else if (!ModuleScopes.empty() && !currentModuleIsImplementation()) {
    // Re-export the module if the imported module is exported.
    // Note that we don't need to add re-exported module to Imports field
    // since `Exports` implies the module is imported already.
    if (ExportLoc.isValid() || getEnclosingExportDecl(Import))
      getCurrentModule()->Exports.emplace_back(Mod, false);
    else
      getCurrentModule()->Imports.insert(Mod);
  } else if (ExportLoc.isValid()) {
    // [module.interface]p1:
    // An export-declaration shall inhabit a namespace scope and appear in the
    // purview of a module interface unit.
    Diag(ExportLoc, diag::err_export_not_in_module_interface);
  }

  return Import;
}

void Sema::ActOnAnnotModuleInclude(SourceLocation DirectiveLoc, Module *Mod) {
  checkModuleImportContext(*this, Mod, DirectiveLoc, CurContext, true);
  BuildModuleInclude(DirectiveLoc, Mod);
}

void Sema::BuildModuleInclude(SourceLocation DirectiveLoc, Module *Mod) {
  // Determine whether we're in the #include buffer for a module. The #includes
  // in that buffer do not qualify as module imports; they're just an
  // implementation detail of us building the module.
  //
  // FIXME: Should we even get ActOnAnnotModuleInclude calls for those?
  bool IsInModuleIncludes =
      TUKind == TU_ClangModule &&
      getSourceManager().isWrittenInMainFile(DirectiveLoc);

  // If we are really importing a module (not just checking layering) due to an
  // #include in the main file, synthesize an ImportDecl.
  if (getLangOpts().Modules && !IsInModuleIncludes) {
    TranslationUnitDecl *TU = getASTContext().getTranslationUnitDecl();
    ImportDecl *ImportD = ImportDecl::CreateImplicit(getASTContext(), TU,
                                                     DirectiveLoc, Mod,
                                                     DirectiveLoc);
    if (!ModuleScopes.empty())
      Context.addModuleInitializer(ModuleScopes.back().Module, ImportD);
    TU->addDecl(ImportD);
    Consumer.HandleImplicitImportDecl(ImportD);
  }

  getModuleLoader().makeModuleVisible(Mod, Module::AllVisible, DirectiveLoc);
  VisibleModules.setVisible(Mod, DirectiveLoc);

  if (getLangOpts().isCompilingModule()) {
    Module *ThisModule = PP.getHeaderSearchInfo().lookupModule(
        getLangOpts().CurrentModule, DirectiveLoc, false, false);
    (void)ThisModule;
    assert(ThisModule && "was expecting a module if building one");
  }
}

void Sema::ActOnAnnotModuleBegin(SourceLocation DirectiveLoc, Module *Mod) {
  checkModuleImportContext(*this, Mod, DirectiveLoc, CurContext, true);

  ModuleScopes.push_back({});
  ModuleScopes.back().Module = Mod;
  if (getLangOpts().ModulesLocalVisibility)
    ModuleScopes.back().OuterVisibleModules = std::move(VisibleModules);

  VisibleModules.setVisible(Mod, DirectiveLoc);

  // The enclosing context is now part of this module.
  // FIXME: Consider creating a child DeclContext to hold the entities
  // lexically within the module.
  if (getLangOpts().trackLocalOwningModule()) {
    for (auto *DC = CurContext; DC; DC = DC->getLexicalParent()) {
      cast<Decl>(DC)->setModuleOwnershipKind(
          getLangOpts().ModulesLocalVisibility
              ? Decl::ModuleOwnershipKind::VisibleWhenImported
              : Decl::ModuleOwnershipKind::Visible);
      cast<Decl>(DC)->setLocalOwningModule(Mod);
    }
  }
}

void Sema::ActOnAnnotModuleEnd(SourceLocation EomLoc, Module *Mod) {
  if (getLangOpts().ModulesLocalVisibility) {
    VisibleModules = std::move(ModuleScopes.back().OuterVisibleModules);
    // Leaving a module hides namespace names, so our visible namespace cache
    // is now out of date.
    VisibleNamespaceCache.clear();
  }

  assert(!ModuleScopes.empty() && ModuleScopes.back().Module == Mod &&
         "left the wrong module scope");
  ModuleScopes.pop_back();

  // We got to the end of processing a local module. Create an
  // ImportDecl as we would for an imported module.
  FileID File = getSourceManager().getFileID(EomLoc);
  SourceLocation DirectiveLoc;
  if (EomLoc == getSourceManager().getLocForEndOfFile(File)) {
    // We reached the end of a #included module header. Use the #include loc.
    assert(File != getSourceManager().getMainFileID() &&
           "end of submodule in main source file");
    DirectiveLoc = getSourceManager().getIncludeLoc(File);
  } else {
    // We reached an EOM pragma. Use the pragma location.
    DirectiveLoc = EomLoc;
  }
  BuildModuleInclude(DirectiveLoc, Mod);

  // Any further declarations are in whatever module we returned to.
  if (getLangOpts().trackLocalOwningModule()) {
    // The parser guarantees that this is the same context that we entered
    // the module within.
    for (auto *DC = CurContext; DC; DC = DC->getLexicalParent()) {
      cast<Decl>(DC)->setLocalOwningModule(getCurrentModule());
      if (!getCurrentModule())
        cast<Decl>(DC)->setModuleOwnershipKind(
            Decl::ModuleOwnershipKind::Unowned);
    }
  }
}

void Sema::createImplicitModuleImportForErrorRecovery(SourceLocation Loc,
                                                      Module *Mod) {
  // Bail if we're not allowed to implicitly import a module here.
  if (isSFINAEContext() || !getLangOpts().ModulesErrorRecovery ||
      VisibleModules.isVisible(Mod))
    return;

  // Create the implicit import declaration.
  TranslationUnitDecl *TU = getASTContext().getTranslationUnitDecl();
  ImportDecl *ImportD = ImportDecl::CreateImplicit(getASTContext(), TU,
                                                   Loc, Mod, Loc);
  TU->addDecl(ImportD);
  Consumer.HandleImplicitImportDecl(ImportD);

  // Make the module visible.
  getModuleLoader().makeModuleVisible(Mod, Module::AllVisible, Loc);
  VisibleModules.setVisible(Mod, Loc);
}

Decl *Sema::ActOnStartExportDecl(Scope *S, SourceLocation ExportLoc,
                                 SourceLocation LBraceLoc) {
  ExportDecl *D = ExportDecl::Create(Context, CurContext, ExportLoc);

  // Set this temporarily so we know the export-declaration was braced.
  D->setRBraceLoc(LBraceLoc);

  CurContext->addDecl(D);
  PushDeclContext(S, D);

  // C++2a [module.interface]p1:
  //   An export-declaration shall appear only [...] in the purview of a module
  //   interface unit. An export-declaration shall not appear directly or
  //   indirectly within [...] a private-module-fragment.
  if (!getLangOpts().HLSL) {
    if (!isCurrentModulePurview()) {
      Diag(ExportLoc, diag::err_export_not_in_module_interface) << 0;
      D->setInvalidDecl();
      return D;
    } else if (currentModuleIsImplementation()) {
      Diag(ExportLoc, diag::err_export_not_in_module_interface) << 1;
      Diag(ModuleScopes.back().BeginLoc,
          diag::note_not_module_interface_add_export)
          << FixItHint::CreateInsertion(ModuleScopes.back().BeginLoc, "export ");
      D->setInvalidDecl();
      return D;
    } else if (ModuleScopes.back().Module->Kind ==
              Module::PrivateModuleFragment) {
      Diag(ExportLoc, diag::err_export_in_private_module_fragment);
      Diag(ModuleScopes.back().BeginLoc, diag::note_private_module_fragment);
      D->setInvalidDecl();
      return D;
    }
  }

  for (const DeclContext *DC = CurContext; DC; DC = DC->getLexicalParent()) {
    if (const auto *ND = dyn_cast<NamespaceDecl>(DC)) {
      //   An export-declaration shall not appear directly or indirectly within
      //   an unnamed namespace [...]
      if (ND->isAnonymousNamespace()) {
        Diag(ExportLoc, diag::err_export_within_anonymous_namespace);
        Diag(ND->getLocation(), diag::note_anonymous_namespace);
        // Don't diagnose internal-linkage declarations in this region.
        D->setInvalidDecl();
        return D;
      }

      //   A declaration is exported if it is [...] a namespace-definition
      //   that contains an exported declaration.
      //
      // Defer exporting the namespace until after we leave it, in order to
      // avoid marking all subsequent declarations in the namespace as exported.
      if (!getLangOpts().HLSL && !DeferredExportedNamespaces.insert(ND).second)
        break;
    }
  }

  //   [...] its declaration or declaration-seq shall not contain an
  //   export-declaration.
  if (auto *ED = getEnclosingExportDecl(D)) {
    Diag(ExportLoc, diag::err_export_within_export);
    if (ED->hasBraces())
      Diag(ED->getLocation(), diag::note_export);
    D->setInvalidDecl();
    return D;
  }

  if (!getLangOpts().HLSL)
    D->setModuleOwnershipKind(Decl::ModuleOwnershipKind::VisibleWhenImported);

  return D;
}

static bool checkExportedDecl(Sema &, Decl *, SourceLocation);

/// Check that it's valid to export all the declarations in \p DC.
static bool checkExportedDeclContext(Sema &S, DeclContext *DC,
                                     SourceLocation BlockStart) {
  bool AllUnnamed = true;
  for (auto *D : DC->decls())
    AllUnnamed &= checkExportedDecl(S, D, BlockStart);
  return AllUnnamed;
}

/// Check that it's valid to export \p D.
static bool checkExportedDecl(Sema &S, Decl *D, SourceLocation BlockStart) {

  // HLSL: export declaration is valid only on functions
  if (S.getLangOpts().HLSL) {
    // Export-within-export was already diagnosed in ActOnStartExportDecl
    if (!dyn_cast<FunctionDecl>(D) && !dyn_cast<ExportDecl>(D)) {
      S.Diag(D->getBeginLoc(), diag::err_hlsl_export_not_on_function);
      D->setInvalidDecl();
      return false;
    }
  }

  //  C++20 [module.interface]p3:
  //   [...] it shall not declare a name with internal linkage.
  bool HasName = false;
  if (auto *ND = dyn_cast<NamedDecl>(D)) {
    // Don't diagnose anonymous union objects; we'll diagnose their members
    // instead.
    HasName = (bool)ND->getDeclName();
    if (HasName && ND->getFormalLinkage() == Linkage::Internal) {
      S.Diag(ND->getLocation(), diag::err_export_internal) << ND;
      if (BlockStart.isValid())
        S.Diag(BlockStart, diag::note_export);
      return false;
    }
  }

  // C++2a [module.interface]p5:
  //   all entities to which all of the using-declarators ultimately refer
  //   shall have been introduced with a name having external linkage
  if (auto *USD = dyn_cast<UsingShadowDecl>(D)) {
    NamedDecl *Target = USD->getUnderlyingDecl();
    Linkage Lk = Target->getFormalLinkage();
    if (Lk == Linkage::Internal || Lk == Linkage::Module) {
      S.Diag(USD->getLocation(), diag::err_export_using_internal)
          << (Lk == Linkage::Internal ? 0 : 1) << Target;
      S.Diag(Target->getLocation(), diag::note_using_decl_target);
      if (BlockStart.isValid())
        S.Diag(BlockStart, diag::note_export);
      return false;
    }
  }

  // Recurse into namespace-scope DeclContexts. (Only namespace-scope
  // declarations are exported).
  if (auto *DC = dyn_cast<DeclContext>(D)) {
    if (!isa<NamespaceDecl>(D))
      return true;

    if (auto *ND = dyn_cast<NamedDecl>(D)) {
      if (!ND->getDeclName()) {
        S.Diag(ND->getLocation(), diag::err_export_anon_ns_internal);
        if (BlockStart.isValid())
          S.Diag(BlockStart, diag::note_export);
        return false;
      } else if (!DC->decls().empty() &&
                 DC->getRedeclContext()->isFileContext()) {
        return checkExportedDeclContext(S, DC, BlockStart);
      }
    }
  }
  return true;
}

Decl *Sema::ActOnFinishExportDecl(Scope *S, Decl *D, SourceLocation RBraceLoc) {
  auto *ED = cast<ExportDecl>(D);
  if (RBraceLoc.isValid())
    ED->setRBraceLoc(RBraceLoc);

  PopDeclContext();

  if (!D->isInvalidDecl()) {
    SourceLocation BlockStart =
        ED->hasBraces() ? ED->getBeginLoc() : SourceLocation();
    for (auto *Child : ED->decls()) {
      checkExportedDecl(*this, Child, BlockStart);
      if (auto *FD = dyn_cast<FunctionDecl>(Child)) {
        // [dcl.inline]/7
        // If an inline function or variable that is attached to a named module
        // is declared in a definition domain, it shall be defined in that
        // domain.
        // So, if the current declaration does not have a definition, we must
        // check at the end of the TU (or when the PMF starts) to see that we
        // have a definition at that point.
        if (FD->isInlineSpecified() && !FD->isDefined())
          PendingInlineFuncDecls.insert(FD);
      }
    }
  }

  // Anything exported from a module should never be considered unused.
  for (auto *Exported : ED->decls())
    Exported->markUsed(getASTContext());

  return D;
}

Module *Sema::PushGlobalModuleFragment(SourceLocation BeginLoc) {
  // We shouldn't create new global module fragment if there is already
  // one.
  if (!TheGlobalModuleFragment) {
    ModuleMap &Map = PP.getHeaderSearchInfo().getModuleMap();
    TheGlobalModuleFragment = Map.createGlobalModuleFragmentForModuleUnit(
        BeginLoc, getCurrentModule());
  }

  assert(TheGlobalModuleFragment && "module creation should not fail");

  // Enter the scope of the global module.
  ModuleScopes.push_back({BeginLoc, TheGlobalModuleFragment,
                          /*OuterVisibleModules=*/{}});
  VisibleModules.setVisible(TheGlobalModuleFragment, BeginLoc);

  return TheGlobalModuleFragment;
}

void Sema::PopGlobalModuleFragment() {
  assert(!ModuleScopes.empty() &&
         getCurrentModule()->isExplicitGlobalModule() &&
         "left the wrong module scope, which is not global module fragment");
  ModuleScopes.pop_back();
}

Module *Sema::PushImplicitGlobalModuleFragment(SourceLocation BeginLoc) {
  if (!TheImplicitGlobalModuleFragment) {
    ModuleMap &Map = PP.getHeaderSearchInfo().getModuleMap();
    TheImplicitGlobalModuleFragment =
        Map.createImplicitGlobalModuleFragmentForModuleUnit(BeginLoc,
                                                            getCurrentModule());
  }
  assert(TheImplicitGlobalModuleFragment && "module creation should not fail");

  // Enter the scope of the global module.
  ModuleScopes.push_back({BeginLoc, TheImplicitGlobalModuleFragment,
                          /*OuterVisibleModules=*/{}});
  VisibleModules.setVisible(TheImplicitGlobalModuleFragment, BeginLoc);
  return TheImplicitGlobalModuleFragment;
}

void Sema::PopImplicitGlobalModuleFragment() {
  assert(!ModuleScopes.empty() &&
         getCurrentModule()->isImplicitGlobalModule() &&
         "left the wrong module scope, which is not global module fragment");
  ModuleScopes.pop_back();
}

bool Sema::isCurrentModulePurview() const {
  if (!getCurrentModule())
    return false;

  /// Does this Module scope describe part of the purview of a standard named
  /// C++ module?
  switch (getCurrentModule()->Kind) {
  case Module::ModuleInterfaceUnit:
  case Module::ModuleImplementationUnit:
  case Module::ModulePartitionInterface:
  case Module::ModulePartitionImplementation:
  case Module::PrivateModuleFragment:
  case Module::ImplicitGlobalModuleFragment:
    return true;
  default:
    return false;
  }
}
