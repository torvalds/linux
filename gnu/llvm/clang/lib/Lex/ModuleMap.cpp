//===- ModuleMap.cpp - Describe the layout of modules ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ModuleMap implementation, which describes the layout
// of a module as it relates to headers.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/ModuleMap.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

using namespace clang;

void ModuleMapCallbacks::anchor() {}

void ModuleMap::resolveLinkAsDependencies(Module *Mod) {
  auto PendingLinkAs = PendingLinkAsModule.find(Mod->Name);
  if (PendingLinkAs != PendingLinkAsModule.end()) {
    for (auto &Name : PendingLinkAs->second) {
      auto *M = findModule(Name.getKey());
      if (M)
        M->UseExportAsModuleLinkName = true;
    }
  }
}

void ModuleMap::addLinkAsDependency(Module *Mod) {
  if (findModule(Mod->ExportAsModule))
    Mod->UseExportAsModuleLinkName = true;
  else
    PendingLinkAsModule[Mod->ExportAsModule].insert(Mod->Name);
}

Module::HeaderKind ModuleMap::headerRoleToKind(ModuleHeaderRole Role) {
  switch ((int)Role) {
  case NormalHeader:
    return Module::HK_Normal;
  case PrivateHeader:
    return Module::HK_Private;
  case TextualHeader:
    return Module::HK_Textual;
  case PrivateHeader | TextualHeader:
    return Module::HK_PrivateTextual;
  case ExcludedHeader:
    return Module::HK_Excluded;
  }
  llvm_unreachable("unknown header role");
}

ModuleMap::ModuleHeaderRole
ModuleMap::headerKindToRole(Module::HeaderKind Kind) {
  switch ((int)Kind) {
  case Module::HK_Normal:
    return NormalHeader;
  case Module::HK_Private:
    return PrivateHeader;
  case Module::HK_Textual:
    return TextualHeader;
  case Module::HK_PrivateTextual:
    return ModuleHeaderRole(PrivateHeader | TextualHeader);
  case Module::HK_Excluded:
    return ExcludedHeader;
  }
  llvm_unreachable("unknown header kind");
}

bool ModuleMap::isModular(ModuleHeaderRole Role) {
  return !(Role & (ModuleMap::TextualHeader | ModuleMap::ExcludedHeader));
}

Module::ExportDecl
ModuleMap::resolveExport(Module *Mod,
                         const Module::UnresolvedExportDecl &Unresolved,
                         bool Complain) const {
  // We may have just a wildcard.
  if (Unresolved.Id.empty()) {
    assert(Unresolved.Wildcard && "Invalid unresolved export");
    return Module::ExportDecl(nullptr, true);
  }

  // Resolve the module-id.
  Module *Context = resolveModuleId(Unresolved.Id, Mod, Complain);
  if (!Context)
    return {};

  return Module::ExportDecl(Context, Unresolved.Wildcard);
}

Module *ModuleMap::resolveModuleId(const ModuleId &Id, Module *Mod,
                                   bool Complain) const {
  // Find the starting module.
  Module *Context = lookupModuleUnqualified(Id[0].first, Mod);
  if (!Context) {
    if (Complain)
      Diags.Report(Id[0].second, diag::err_mmap_missing_module_unqualified)
      << Id[0].first << Mod->getFullModuleName();

    return nullptr;
  }

  // Dig into the module path.
  for (unsigned I = 1, N = Id.size(); I != N; ++I) {
    Module *Sub = lookupModuleQualified(Id[I].first, Context);
    if (!Sub) {
      if (Complain)
        Diags.Report(Id[I].second, diag::err_mmap_missing_module_qualified)
        << Id[I].first << Context->getFullModuleName()
        << SourceRange(Id[0].second, Id[I-1].second);

      return nullptr;
    }

    Context = Sub;
  }

  return Context;
}

/// Append to \p Paths the set of paths needed to get to the
/// subframework in which the given module lives.
static void appendSubframeworkPaths(Module *Mod,
                                    SmallVectorImpl<char> &Path) {
  // Collect the framework names from the given module to the top-level module.
  SmallVector<StringRef, 2> Paths;
  for (; Mod; Mod = Mod->Parent) {
    if (Mod->IsFramework)
      Paths.push_back(Mod->Name);
  }

  if (Paths.empty())
    return;

  // Add Frameworks/Name.framework for each subframework.
  for (StringRef Framework : llvm::drop_begin(llvm::reverse(Paths)))
    llvm::sys::path::append(Path, "Frameworks", Framework + ".framework");
}

OptionalFileEntryRef ModuleMap::findHeader(
    Module *M, const Module::UnresolvedHeaderDirective &Header,
    SmallVectorImpl<char> &RelativePathName, bool &NeedsFramework) {
  // Search for the header file within the module's home directory.
  auto Directory = M->Directory;
  SmallString<128> FullPathName(Directory->getName());

  auto GetFile = [&](StringRef Filename) -> OptionalFileEntryRef {
    auto File =
        expectedToOptional(SourceMgr.getFileManager().getFileRef(Filename));
    if (!File || (Header.Size && File->getSize() != *Header.Size) ||
        (Header.ModTime && File->getModificationTime() != *Header.ModTime))
      return std::nullopt;
    return *File;
  };

  auto GetFrameworkFile = [&]() -> OptionalFileEntryRef {
    unsigned FullPathLength = FullPathName.size();
    appendSubframeworkPaths(M, RelativePathName);
    unsigned RelativePathLength = RelativePathName.size();

    // Check whether this file is in the public headers.
    llvm::sys::path::append(RelativePathName, "Headers", Header.FileName);
    llvm::sys::path::append(FullPathName, RelativePathName);
    if (auto File = GetFile(FullPathName))
      return File;

    // Check whether this file is in the private headers.
    // Ideally, private modules in the form 'FrameworkName.Private' should
    // be defined as 'module FrameworkName.Private', and not as
    // 'framework module FrameworkName.Private', since a 'Private.Framework'
    // does not usually exist. However, since both are currently widely used
    // for private modules, make sure we find the right path in both cases.
    if (M->IsFramework && M->Name == "Private")
      RelativePathName.clear();
    else
      RelativePathName.resize(RelativePathLength);
    FullPathName.resize(FullPathLength);
    llvm::sys::path::append(RelativePathName, "PrivateHeaders",
                            Header.FileName);
    llvm::sys::path::append(FullPathName, RelativePathName);
    return GetFile(FullPathName);
  };

  if (llvm::sys::path::is_absolute(Header.FileName)) {
    RelativePathName.clear();
    RelativePathName.append(Header.FileName.begin(), Header.FileName.end());
    return GetFile(Header.FileName);
  }

  if (M->isPartOfFramework())
    return GetFrameworkFile();

  // Lookup for normal headers.
  llvm::sys::path::append(RelativePathName, Header.FileName);
  llvm::sys::path::append(FullPathName, RelativePathName);
  auto NormalHdrFile = GetFile(FullPathName);

  if (!NormalHdrFile && Directory->getName().ends_with(".framework")) {
    // The lack of 'framework' keyword in a module declaration it's a simple
    // mistake we can diagnose when the header exists within the proper
    // framework style path.
    FullPathName.assign(Directory->getName());
    RelativePathName.clear();
    if (GetFrameworkFile()) {
      Diags.Report(Header.FileNameLoc,
                   diag::warn_mmap_incomplete_framework_module_declaration)
          << Header.FileName << M->getFullModuleName();
      NeedsFramework = true;
    }
    return std::nullopt;
  }

  return NormalHdrFile;
}

/// Determine whether the given file name is the name of a builtin
/// header, supplied by Clang to replace, override, or augment existing system
/// headers.
static bool isBuiltinHeaderName(StringRef FileName) {
  return llvm::StringSwitch<bool>(FileName)
           .Case("float.h", true)
           .Case("iso646.h", true)
           .Case("limits.h", true)
           .Case("stdalign.h", true)
           .Case("stdarg.h", true)
           .Case("stdatomic.h", true)
           .Case("stdbool.h", true)
           .Case("stddef.h", true)
           .Case("stdint.h", true)
           .Case("tgmath.h", true)
           .Case("unwind.h", true)
           .Default(false);
}

/// Determine whether the given module name is the name of a builtin
/// module that is cyclic with a system module  on some platforms.
static bool isBuiltInModuleName(StringRef ModuleName) {
  return llvm::StringSwitch<bool>(ModuleName)
           .Case("_Builtin_float", true)
           .Case("_Builtin_inttypes", true)
           .Case("_Builtin_iso646", true)
           .Case("_Builtin_limits", true)
           .Case("_Builtin_stdalign", true)
           .Case("_Builtin_stdarg", true)
           .Case("_Builtin_stdatomic", true)
           .Case("_Builtin_stdbool", true)
           .Case("_Builtin_stddef", true)
           .Case("_Builtin_stdint", true)
           .Case("_Builtin_stdnoreturn", true)
           .Case("_Builtin_tgmath", true)
           .Case("_Builtin_unwind", true)
           .Default(false);
}

void ModuleMap::resolveHeader(Module *Mod,
                              const Module::UnresolvedHeaderDirective &Header,
                              bool &NeedsFramework) {
  SmallString<128> RelativePathName;
  if (OptionalFileEntryRef File =
          findHeader(Mod, Header, RelativePathName, NeedsFramework)) {
    if (Header.IsUmbrella) {
      const DirectoryEntry *UmbrellaDir = &File->getDir().getDirEntry();
      if (Module *UmbrellaMod = UmbrellaDirs[UmbrellaDir])
        Diags.Report(Header.FileNameLoc, diag::err_mmap_umbrella_clash)
          << UmbrellaMod->getFullModuleName();
      else
        // Record this umbrella header.
        setUmbrellaHeaderAsWritten(Mod, *File, Header.FileName,
                                   RelativePathName.str());
    } else {
      Module::Header H = {Header.FileName, std::string(RelativePathName),
                          *File};
      addHeader(Mod, H, headerKindToRole(Header.Kind));
    }
  } else if (Header.HasBuiltinHeader && !Header.Size && !Header.ModTime) {
    // There's a builtin header but no corresponding on-disk header. Assume
    // this was supposed to modularize the builtin header alone.
  } else if (Header.Kind == Module::HK_Excluded) {
    // Ignore missing excluded header files. They're optional anyway.
  } else {
    // If we find a module that has a missing header, we mark this module as
    // unavailable and store the header directive for displaying diagnostics.
    Mod->MissingHeaders.push_back(Header);
    // A missing header with stat information doesn't make the module
    // unavailable; this keeps our behavior consistent as headers are lazily
    // resolved. (Such a module still can't be built though, except from
    // preprocessed source.)
    if (!Header.Size && !Header.ModTime)
      Mod->markUnavailable(/*Unimportable=*/false);
  }
}

bool ModuleMap::resolveAsBuiltinHeader(
    Module *Mod, const Module::UnresolvedHeaderDirective &Header) {
  if (Header.Kind == Module::HK_Excluded ||
      llvm::sys::path::is_absolute(Header.FileName) ||
      Mod->isPartOfFramework() || !Mod->IsSystem || Header.IsUmbrella ||
      !BuiltinIncludeDir || BuiltinIncludeDir == Mod->Directory ||
      !LangOpts.BuiltinHeadersInSystemModules || !isBuiltinHeaderName(Header.FileName))
    return false;

  // This is a system module with a top-level header. This header
  // may have a counterpart (or replacement) in the set of headers
  // supplied by Clang. Find that builtin header.
  SmallString<128> Path;
  llvm::sys::path::append(Path, BuiltinIncludeDir->getName(), Header.FileName);
  auto File = SourceMgr.getFileManager().getOptionalFileRef(Path);
  if (!File)
    return false;

  Module::Header H = {Header.FileName, Header.FileName, *File};
  auto Role = headerKindToRole(Header.Kind);
  addHeader(Mod, H, Role);
  return true;
}

ModuleMap::ModuleMap(SourceManager &SourceMgr, DiagnosticsEngine &Diags,
                     const LangOptions &LangOpts, const TargetInfo *Target,
                     HeaderSearch &HeaderInfo)
    : SourceMgr(SourceMgr), Diags(Diags), LangOpts(LangOpts), Target(Target),
      HeaderInfo(HeaderInfo) {
  MMapLangOpts.LineComment = true;
}

ModuleMap::~ModuleMap() {
  for (auto &M : Modules)
    delete M.getValue();
  for (auto *M : ShadowModules)
    delete M;
}

void ModuleMap::setTarget(const TargetInfo &Target) {
  assert((!this->Target || this->Target == &Target) &&
         "Improper target override");
  this->Target = &Target;
}

/// "Sanitize" a filename so that it can be used as an identifier.
static StringRef sanitizeFilenameAsIdentifier(StringRef Name,
                                              SmallVectorImpl<char> &Buffer) {
  if (Name.empty())
    return Name;

  if (!isValidAsciiIdentifier(Name)) {
    // If we don't already have something with the form of an identifier,
    // create a buffer with the sanitized name.
    Buffer.clear();
    if (isDigit(Name[0]))
      Buffer.push_back('_');
    Buffer.reserve(Buffer.size() + Name.size());
    for (unsigned I = 0, N = Name.size(); I != N; ++I) {
      if (isAsciiIdentifierContinue(Name[I]))
        Buffer.push_back(Name[I]);
      else
        Buffer.push_back('_');
    }

    Name = StringRef(Buffer.data(), Buffer.size());
  }

  while (llvm::StringSwitch<bool>(Name)
#define KEYWORD(Keyword,Conditions) .Case(#Keyword, true)
#define ALIAS(Keyword, AliasOf, Conditions) .Case(Keyword, true)
#include "clang/Basic/TokenKinds.def"
           .Default(false)) {
    if (Name.data() != Buffer.data())
      Buffer.append(Name.begin(), Name.end());
    Buffer.push_back('_');
    Name = StringRef(Buffer.data(), Buffer.size());
  }

  return Name;
}

bool ModuleMap::isBuiltinHeader(FileEntryRef File) {
  return File.getDir() == BuiltinIncludeDir && LangOpts.BuiltinHeadersInSystemModules &&
         isBuiltinHeaderName(llvm::sys::path::filename(File.getName()));
}

bool ModuleMap::shouldImportRelativeToBuiltinIncludeDir(StringRef FileName,
                                                        Module *Module) const {
  return LangOpts.BuiltinHeadersInSystemModules && BuiltinIncludeDir &&
         Module->IsSystem && !Module->isPartOfFramework() &&
         isBuiltinHeaderName(FileName);
}

ModuleMap::HeadersMap::iterator ModuleMap::findKnownHeader(FileEntryRef File) {
  resolveHeaderDirectives(File);
  HeadersMap::iterator Known = Headers.find(File);
  if (HeaderInfo.getHeaderSearchOpts().ImplicitModuleMaps &&
      Known == Headers.end() && ModuleMap::isBuiltinHeader(File)) {
    HeaderInfo.loadTopLevelSystemModules();
    return Headers.find(File);
  }
  return Known;
}

ModuleMap::KnownHeader ModuleMap::findHeaderInUmbrellaDirs(
    FileEntryRef File, SmallVectorImpl<DirectoryEntryRef> &IntermediateDirs) {
  if (UmbrellaDirs.empty())
    return {};

  OptionalDirectoryEntryRef Dir = File.getDir();

  // Note: as an egregious but useful hack we use the real path here, because
  // frameworks moving from top-level frameworks to embedded frameworks tend
  // to be symlinked from the top-level location to the embedded location,
  // and we need to resolve lookups as if we had found the embedded location.
  StringRef DirName = SourceMgr.getFileManager().getCanonicalName(*Dir);

  // Keep walking up the directory hierarchy, looking for a directory with
  // an umbrella header.
  do {
    auto KnownDir = UmbrellaDirs.find(*Dir);
    if (KnownDir != UmbrellaDirs.end())
      return KnownHeader(KnownDir->second, NormalHeader);

    IntermediateDirs.push_back(*Dir);

    // Retrieve our parent path.
    DirName = llvm::sys::path::parent_path(DirName);
    if (DirName.empty())
      break;

    // Resolve the parent path to a directory entry.
    Dir = SourceMgr.getFileManager().getOptionalDirectoryRef(DirName);
  } while (Dir);
  return {};
}

static bool violatesPrivateInclude(Module *RequestingModule,
                                   const FileEntry *IncFileEnt,
                                   ModuleMap::KnownHeader Header) {
#ifndef NDEBUG
  if (Header.getRole() & ModuleMap::PrivateHeader) {
    // Check for consistency between the module header role
    // as obtained from the lookup and as obtained from the module.
    // This check is not cheap, so enable it only for debugging.
    bool IsPrivate = false;
    SmallVectorImpl<Module::Header> *HeaderList[] = {
        &Header.getModule()->Headers[Module::HK_Private],
        &Header.getModule()->Headers[Module::HK_PrivateTextual]};
    for (auto *Hs : HeaderList)
      IsPrivate |= llvm::any_of(
          *Hs, [&](const Module::Header &H) { return H.Entry == IncFileEnt; });
    assert(IsPrivate && "inconsistent headers and roles");
  }
#endif
  return !Header.isAccessibleFrom(RequestingModule);
}

static Module *getTopLevelOrNull(Module *M) {
  return M ? M->getTopLevelModule() : nullptr;
}

void ModuleMap::diagnoseHeaderInclusion(Module *RequestingModule,
                                        bool RequestingModuleIsModuleInterface,
                                        SourceLocation FilenameLoc,
                                        StringRef Filename, FileEntryRef File) {
  // No errors for indirect modules. This may be a bit of a problem for modules
  // with no source files.
  if (getTopLevelOrNull(RequestingModule) != getTopLevelOrNull(SourceModule))
    return;

  if (RequestingModule) {
    resolveUses(RequestingModule, /*Complain=*/false);
    resolveHeaderDirectives(RequestingModule, /*File=*/std::nullopt);
  }

  bool Excluded = false;
  Module *Private = nullptr;
  Module *NotUsed = nullptr;

  HeadersMap::iterator Known = findKnownHeader(File);
  if (Known != Headers.end()) {
    for (const KnownHeader &Header : Known->second) {
      // Excluded headers don't really belong to a module.
      if (Header.getRole() == ModuleMap::ExcludedHeader) {
        Excluded = true;
        continue;
      }

      // Remember private headers for later printing of a diagnostic.
      if (violatesPrivateInclude(RequestingModule, File, Header)) {
        Private = Header.getModule();
        continue;
      }

      // If uses need to be specified explicitly, we are only allowed to return
      // modules that are explicitly used by the requesting module.
      if (RequestingModule && LangOpts.ModulesDeclUse &&
          !RequestingModule->directlyUses(Header.getModule())) {
        NotUsed = Header.getModule();
        continue;
      }

      // We have found a module that we can happily use.
      return;
    }

    Excluded = true;
  }

  // We have found a header, but it is private.
  if (Private) {
    Diags.Report(FilenameLoc, diag::warn_use_of_private_header_outside_module)
        << Filename;
    return;
  }

  // We have found a module, but we don't use it.
  if (NotUsed) {
    Diags.Report(FilenameLoc, diag::err_undeclared_use_of_module_indirect)
        << RequestingModule->getTopLevelModule()->Name << Filename
        << NotUsed->Name;
    return;
  }

  if (Excluded || isHeaderInUmbrellaDirs(File))
    return;

  // At this point, only non-modular includes remain.

  if (RequestingModule && LangOpts.ModulesStrictDeclUse) {
    Diags.Report(FilenameLoc, diag::err_undeclared_use_of_module)
        << RequestingModule->getTopLevelModule()->Name << Filename;
  } else if (RequestingModule && RequestingModuleIsModuleInterface &&
             LangOpts.isCompilingModule()) {
    // Do not diagnose when we are not compiling a module.
    diag::kind DiagID = RequestingModule->getTopLevelModule()->IsFramework ?
        diag::warn_non_modular_include_in_framework_module :
        diag::warn_non_modular_include_in_module;
    Diags.Report(FilenameLoc, DiagID) << RequestingModule->getFullModuleName()
        << File.getName();
  }
}

static bool isBetterKnownHeader(const ModuleMap::KnownHeader &New,
                                const ModuleMap::KnownHeader &Old) {
  // Prefer available modules.
  // FIXME: Considering whether the module is available rather than merely
  // importable is non-hermetic and can result in surprising behavior for
  // prebuilt modules. Consider only checking for importability here.
  if (New.getModule()->isAvailable() && !Old.getModule()->isAvailable())
    return true;

  // Prefer a public header over a private header.
  if ((New.getRole() & ModuleMap::PrivateHeader) !=
      (Old.getRole() & ModuleMap::PrivateHeader))
    return !(New.getRole() & ModuleMap::PrivateHeader);

  // Prefer a non-textual header over a textual header.
  if ((New.getRole() & ModuleMap::TextualHeader) !=
      (Old.getRole() & ModuleMap::TextualHeader))
    return !(New.getRole() & ModuleMap::TextualHeader);

  // Prefer a non-excluded header over an excluded header.
  if ((New.getRole() == ModuleMap::ExcludedHeader) !=
      (Old.getRole() == ModuleMap::ExcludedHeader))
    return New.getRole() != ModuleMap::ExcludedHeader;

  // Don't have a reason to choose between these. Just keep the first one.
  return false;
}

ModuleMap::KnownHeader ModuleMap::findModuleForHeader(FileEntryRef File,
                                                      bool AllowTextual,
                                                      bool AllowExcluded) {
  auto MakeResult = [&](ModuleMap::KnownHeader R) -> ModuleMap::KnownHeader {
    if (!AllowTextual && R.getRole() & ModuleMap::TextualHeader)
      return {};
    return R;
  };

  HeadersMap::iterator Known = findKnownHeader(File);
  if (Known != Headers.end()) {
    ModuleMap::KnownHeader Result;
    // Iterate over all modules that 'File' is part of to find the best fit.
    for (KnownHeader &H : Known->second) {
      // Cannot use a module if the header is excluded in it.
      if (!AllowExcluded && H.getRole() == ModuleMap::ExcludedHeader)
        continue;
      // Prefer a header from the source module over all others.
      if (H.getModule()->getTopLevelModule() == SourceModule)
        return MakeResult(H);
      if (!Result || isBetterKnownHeader(H, Result))
        Result = H;
    }
    return MakeResult(Result);
  }

  return MakeResult(findOrCreateModuleForHeaderInUmbrellaDir(File));
}

ModuleMap::KnownHeader
ModuleMap::findOrCreateModuleForHeaderInUmbrellaDir(FileEntryRef File) {
  assert(!Headers.count(File) && "already have a module for this header");

  SmallVector<DirectoryEntryRef, 2> SkippedDirs;
  KnownHeader H = findHeaderInUmbrellaDirs(File, SkippedDirs);
  if (H) {
    Module *Result = H.getModule();

    // Search up the module stack until we find a module with an umbrella
    // directory.
    Module *UmbrellaModule = Result;
    while (!UmbrellaModule->getEffectiveUmbrellaDir() && UmbrellaModule->Parent)
      UmbrellaModule = UmbrellaModule->Parent;

    if (UmbrellaModule->InferSubmodules) {
      FileID UmbrellaModuleMap = getModuleMapFileIDForUniquing(UmbrellaModule);

      // Infer submodules for each of the directories we found between
      // the directory of the umbrella header and the directory where
      // the actual header is located.
      bool Explicit = UmbrellaModule->InferExplicitSubmodules;

      for (DirectoryEntryRef SkippedDir : llvm::reverse(SkippedDirs)) {
        // Find or create the module that corresponds to this directory name.
        SmallString<32> NameBuf;
        StringRef Name = sanitizeFilenameAsIdentifier(
            llvm::sys::path::stem(SkippedDir.getName()), NameBuf);
        Result = findOrCreateModule(Name, Result, /*IsFramework=*/false,
                                    Explicit).first;
        InferredModuleAllowedBy[Result] = UmbrellaModuleMap;
        Result->IsInferred = true;

        // Associate the module and the directory.
        UmbrellaDirs[SkippedDir] = Result;

        // If inferred submodules export everything they import, add a
        // wildcard to the set of exports.
        if (UmbrellaModule->InferExportWildcard && Result->Exports.empty())
          Result->Exports.push_back(Module::ExportDecl(nullptr, true));
      }

      // Infer a submodule with the same name as this header file.
      SmallString<32> NameBuf;
      StringRef Name = sanitizeFilenameAsIdentifier(
                         llvm::sys::path::stem(File.getName()), NameBuf);
      Result = findOrCreateModule(Name, Result, /*IsFramework=*/false,
                                  Explicit).first;
      InferredModuleAllowedBy[Result] = UmbrellaModuleMap;
      Result->IsInferred = true;
      Result->addTopHeader(File);

      // If inferred submodules export everything they import, add a
      // wildcard to the set of exports.
      if (UmbrellaModule->InferExportWildcard && Result->Exports.empty())
        Result->Exports.push_back(Module::ExportDecl(nullptr, true));
    } else {
      // Record each of the directories we stepped through as being part of
      // the module we found, since the umbrella header covers them all.
      for (unsigned I = 0, N = SkippedDirs.size(); I != N; ++I)
        UmbrellaDirs[SkippedDirs[I]] = Result;
    }

    KnownHeader Header(Result, NormalHeader);
    Headers[File].push_back(Header);
    return Header;
  }

  return {};
}

ArrayRef<ModuleMap::KnownHeader>
ModuleMap::findAllModulesForHeader(FileEntryRef File) {
  HeadersMap::iterator Known = findKnownHeader(File);
  if (Known != Headers.end())
    return Known->second;

  if (findOrCreateModuleForHeaderInUmbrellaDir(File))
    return Headers.find(File)->second;

  return std::nullopt;
}

ArrayRef<ModuleMap::KnownHeader>
ModuleMap::findResolvedModulesForHeader(FileEntryRef File) const {
  // FIXME: Is this necessary?
  resolveHeaderDirectives(File);
  auto It = Headers.find(File);
  if (It == Headers.end())
    return std::nullopt;
  return It->second;
}

bool ModuleMap::isHeaderInUnavailableModule(FileEntryRef Header) const {
  return isHeaderUnavailableInModule(Header, nullptr);
}

bool ModuleMap::isHeaderUnavailableInModule(
    FileEntryRef Header, const Module *RequestingModule) const {
  resolveHeaderDirectives(Header);
  HeadersMap::const_iterator Known = Headers.find(Header);
  if (Known != Headers.end()) {
    for (SmallVectorImpl<KnownHeader>::const_iterator
             I = Known->second.begin(),
             E = Known->second.end();
         I != E; ++I) {

      if (I->getRole() == ModuleMap::ExcludedHeader)
        continue;

      if (I->isAvailable() &&
          (!RequestingModule ||
           I->getModule()->isSubModuleOf(RequestingModule))) {
        // When no requesting module is available, the caller is looking if a
        // header is part a module by only looking into the module map. This is
        // done by warn_uncovered_module_header checks; don't consider textual
        // headers part of it in this mode, otherwise we get misleading warnings
        // that a umbrella header is not including a textual header.
        if (!RequestingModule && I->getRole() == ModuleMap::TextualHeader)
          continue;
        return false;
      }
    }
    return true;
  }

  OptionalDirectoryEntryRef Dir = Header.getDir();
  SmallVector<DirectoryEntryRef, 2> SkippedDirs;
  StringRef DirName = Dir->getName();

  auto IsUnavailable = [&](const Module *M) {
    return !M->isAvailable() && (!RequestingModule ||
                                 M->isSubModuleOf(RequestingModule));
  };

  // Keep walking up the directory hierarchy, looking for a directory with
  // an umbrella header.
  do {
    auto KnownDir = UmbrellaDirs.find(*Dir);
    if (KnownDir != UmbrellaDirs.end()) {
      Module *Found = KnownDir->second;
      if (IsUnavailable(Found))
        return true;

      // Search up the module stack until we find a module with an umbrella
      // directory.
      Module *UmbrellaModule = Found;
      while (!UmbrellaModule->getEffectiveUmbrellaDir() &&
             UmbrellaModule->Parent)
        UmbrellaModule = UmbrellaModule->Parent;

      if (UmbrellaModule->InferSubmodules) {
        for (DirectoryEntryRef SkippedDir : llvm::reverse(SkippedDirs)) {
          // Find or create the module that corresponds to this directory name.
          SmallString<32> NameBuf;
          StringRef Name = sanitizeFilenameAsIdentifier(
              llvm::sys::path::stem(SkippedDir.getName()), NameBuf);
          Found = lookupModuleQualified(Name, Found);
          if (!Found)
            return false;
          if (IsUnavailable(Found))
            return true;
        }

        // Infer a submodule with the same name as this header file.
        SmallString<32> NameBuf;
        StringRef Name = sanitizeFilenameAsIdentifier(
                           llvm::sys::path::stem(Header.getName()),
                           NameBuf);
        Found = lookupModuleQualified(Name, Found);
        if (!Found)
          return false;
      }

      return IsUnavailable(Found);
    }

    SkippedDirs.push_back(*Dir);

    // Retrieve our parent path.
    DirName = llvm::sys::path::parent_path(DirName);
    if (DirName.empty())
      break;

    // Resolve the parent path to a directory entry.
    Dir = SourceMgr.getFileManager().getOptionalDirectoryRef(DirName);
  } while (Dir);

  return false;
}

Module *ModuleMap::findModule(StringRef Name) const {
  llvm::StringMap<Module *>::const_iterator Known = Modules.find(Name);
  if (Known != Modules.end())
    return Known->getValue();

  return nullptr;
}

Module *ModuleMap::lookupModuleUnqualified(StringRef Name,
                                           Module *Context) const {
  for(; Context; Context = Context->Parent) {
    if (Module *Sub = lookupModuleQualified(Name, Context))
      return Sub;
  }

  return findModule(Name);
}

Module *ModuleMap::lookupModuleQualified(StringRef Name, Module *Context) const{
  if (!Context)
    return findModule(Name);

  return Context->findSubmodule(Name);
}

std::pair<Module *, bool> ModuleMap::findOrCreateModule(StringRef Name,
                                                        Module *Parent,
                                                        bool IsFramework,
                                                        bool IsExplicit) {
  // Try to find an existing module with this name.
  if (Module *Sub = lookupModuleQualified(Name, Parent))
    return std::make_pair(Sub, false);

  // Create a new module with this name.
  Module *Result = new Module(Name, SourceLocation(), Parent, IsFramework,
                              IsExplicit, NumCreatedModules++);
  if (!Parent) {
    if (LangOpts.CurrentModule == Name)
      SourceModule = Result;
    Modules[Name] = Result;
    ModuleScopeIDs[Result] = CurrentModuleScopeID;
  }
  return std::make_pair(Result, true);
}

Module *ModuleMap::createGlobalModuleFragmentForModuleUnit(SourceLocation Loc,
                                                           Module *Parent) {
  auto *Result = new Module("<global>", Loc, Parent, /*IsFramework*/ false,
                            /*IsExplicit*/ true, NumCreatedModules++);
  Result->Kind = Module::ExplicitGlobalModuleFragment;
  // If the created module isn't owned by a parent, send it to PendingSubmodules
  // to wait for its parent.
  if (!Result->Parent)
    PendingSubmodules.emplace_back(Result);
  return Result;
}

Module *
ModuleMap::createImplicitGlobalModuleFragmentForModuleUnit(SourceLocation Loc,
                                                           Module *Parent) {
  assert(Parent && "We should only create an implicit global module fragment "
                   "in a module purview");
  // Note: Here the `IsExplicit` parameter refers to the semantics in clang
  // modules. All the non-explicit submodules in clang modules will be exported
  // too. Here we simplify the implementation by using the concept.
  auto *Result =
      new Module("<implicit global>", Loc, Parent, /*IsFramework=*/false,
                 /*IsExplicit=*/false, NumCreatedModules++);
  Result->Kind = Module::ImplicitGlobalModuleFragment;
  return Result;
}

Module *
ModuleMap::createPrivateModuleFragmentForInterfaceUnit(Module *Parent,
                                                       SourceLocation Loc) {
  auto *Result =
      new Module("<private>", Loc, Parent, /*IsFramework*/ false,
                 /*IsExplicit*/ true, NumCreatedModules++);
  Result->Kind = Module::PrivateModuleFragment;
  return Result;
}

Module *ModuleMap::createModuleUnitWithKind(SourceLocation Loc, StringRef Name,
                                            Module::ModuleKind Kind) {
  auto *Result =
      new Module(Name, Loc, nullptr, /*IsFramework*/ false,
                 /*IsExplicit*/ false, NumCreatedModules++);
  Result->Kind = Kind;

  // Reparent any current global module fragment as a submodule of this module.
  for (auto &Submodule : PendingSubmodules) {
    Submodule->setParent(Result);
    Submodule.release(); // now owned by parent
  }
  PendingSubmodules.clear();
  return Result;
}

Module *ModuleMap::createModuleForInterfaceUnit(SourceLocation Loc,
                                                StringRef Name) {
  assert(LangOpts.CurrentModule == Name && "module name mismatch");
  assert(!Modules[Name] && "redefining existing module");

  auto *Result =
      createModuleUnitWithKind(Loc, Name, Module::ModuleInterfaceUnit);
  Modules[Name] = SourceModule = Result;

  // Mark the main source file as being within the newly-created module so that
  // declarations and macros are properly visibility-restricted to it.
  auto MainFile = SourceMgr.getFileEntryRefForID(SourceMgr.getMainFileID());
  assert(MainFile && "no input file for module interface");
  Headers[*MainFile].push_back(KnownHeader(Result, PrivateHeader));

  return Result;
}

Module *ModuleMap::createModuleForImplementationUnit(SourceLocation Loc,
                                                     StringRef Name) {
  assert(LangOpts.CurrentModule == Name && "module name mismatch");
  // The interface for this implementation must exist and be loaded.
  assert(Modules[Name] && Modules[Name]->Kind == Module::ModuleInterfaceUnit &&
         "creating implementation module without an interface");

  // Create an entry in the modules map to own the implementation unit module.
  // User module names must not start with a period (so that this cannot clash
  // with any legal user-defined module name).
  StringRef IName = ".ImplementationUnit";
  assert(!Modules[IName] && "multiple implementation units?");

  auto *Result =
      createModuleUnitWithKind(Loc, Name, Module::ModuleImplementationUnit);
  Modules[IName] = SourceModule = Result;

  // Check that the main file is present.
  assert(SourceMgr.getFileEntryForID(SourceMgr.getMainFileID()) &&
         "no input file for module implementation");

  return Result;
}

Module *ModuleMap::createHeaderUnit(SourceLocation Loc, StringRef Name,
                                    Module::Header H) {
  assert(LangOpts.CurrentModule == Name && "module name mismatch");
  assert(!Modules[Name] && "redefining existing module");

  auto *Result = new Module(Name, Loc, nullptr, /*IsFramework*/ false,
                            /*IsExplicit*/ false, NumCreatedModules++);
  Result->Kind = Module::ModuleHeaderUnit;
  Modules[Name] = SourceModule = Result;
  addHeader(Result, H, NormalHeader);
  return Result;
}

/// For a framework module, infer the framework against which we
/// should link.
static void inferFrameworkLink(Module *Mod) {
  assert(Mod->IsFramework && "Can only infer linking for framework modules");
  assert(!Mod->isSubFramework() &&
         "Can only infer linking for top-level frameworks");

  StringRef FrameworkName(Mod->Name);
  FrameworkName.consume_back("_Private");
  Mod->LinkLibraries.push_back(Module::LinkLibrary(FrameworkName.str(),
                                                   /*IsFramework=*/true));
}

Module *ModuleMap::inferFrameworkModule(DirectoryEntryRef FrameworkDir,
                                        bool IsSystem, Module *Parent) {
  Attributes Attrs;
  Attrs.IsSystem = IsSystem;
  return inferFrameworkModule(FrameworkDir, Attrs, Parent);
}

Module *ModuleMap::inferFrameworkModule(DirectoryEntryRef FrameworkDir,
                                        Attributes Attrs, Module *Parent) {
  // Note: as an egregious but useful hack we use the real path here, because
  // we might be looking at an embedded framework that symlinks out to a
  // top-level framework, and we need to infer as if we were naming the
  // top-level framework.
  StringRef FrameworkDirName =
      SourceMgr.getFileManager().getCanonicalName(FrameworkDir);

  // In case this is a case-insensitive filesystem, use the canonical
  // directory name as the ModuleName, since modules are case-sensitive.
  // FIXME: we should be able to give a fix-it hint for the correct spelling.
  SmallString<32> ModuleNameStorage;
  StringRef ModuleName = sanitizeFilenameAsIdentifier(
      llvm::sys::path::stem(FrameworkDirName), ModuleNameStorage);

  // Check whether we've already found this module.
  if (Module *Mod = lookupModuleQualified(ModuleName, Parent))
    return Mod;

  FileManager &FileMgr = SourceMgr.getFileManager();

  // If the framework has a parent path from which we're allowed to infer
  // a framework module, do so.
  FileID ModuleMapFID;
  if (!Parent) {
    // Determine whether we're allowed to infer a module map.
    bool canInfer = false;
    if (llvm::sys::path::has_parent_path(FrameworkDirName)) {
      // Figure out the parent path.
      StringRef Parent = llvm::sys::path::parent_path(FrameworkDirName);
      if (auto ParentDir = FileMgr.getOptionalDirectoryRef(Parent)) {
        // Check whether we have already looked into the parent directory
        // for a module map.
        llvm::DenseMap<const DirectoryEntry *, InferredDirectory>::const_iterator
          inferred = InferredDirectories.find(*ParentDir);
        if (inferred == InferredDirectories.end()) {
          // We haven't looked here before. Load a module map, if there is
          // one.
          bool IsFrameworkDir = Parent.ends_with(".framework");
          if (OptionalFileEntryRef ModMapFile =
                  HeaderInfo.lookupModuleMapFile(*ParentDir, IsFrameworkDir)) {
            parseModuleMapFile(*ModMapFile, Attrs.IsSystem, *ParentDir);
            inferred = InferredDirectories.find(*ParentDir);
          }

          if (inferred == InferredDirectories.end())
            inferred = InferredDirectories.insert(
                         std::make_pair(*ParentDir, InferredDirectory())).first;
        }

        if (inferred->second.InferModules) {
          // We're allowed to infer for this directory, but make sure it's okay
          // to infer this particular module.
          StringRef Name = llvm::sys::path::stem(FrameworkDirName);
          canInfer =
              !llvm::is_contained(inferred->second.ExcludedModules, Name);

          Attrs.IsSystem |= inferred->second.Attrs.IsSystem;
          Attrs.IsExternC |= inferred->second.Attrs.IsExternC;
          Attrs.IsExhaustive |= inferred->second.Attrs.IsExhaustive;
          Attrs.NoUndeclaredIncludes |=
              inferred->second.Attrs.NoUndeclaredIncludes;
          ModuleMapFID = inferred->second.ModuleMapFID;
        }
      }
    }

    // If we're not allowed to infer a framework module, don't.
    if (!canInfer)
      return nullptr;
  } else {
    ModuleMapFID = getModuleMapFileIDForUniquing(Parent);
  }

  // Look for an umbrella header.
  SmallString<128> UmbrellaName = FrameworkDir.getName();
  llvm::sys::path::append(UmbrellaName, "Headers", ModuleName + ".h");
  auto UmbrellaHeader = FileMgr.getOptionalFileRef(UmbrellaName);

  // FIXME: If there's no umbrella header, we could probably scan the
  // framework to load *everything*. But, it's not clear that this is a good
  // idea.
  if (!UmbrellaHeader)
    return nullptr;

  Module *Result = new Module(ModuleName, SourceLocation(), Parent,
                              /*IsFramework=*/true, /*IsExplicit=*/false,
                              NumCreatedModules++);
  InferredModuleAllowedBy[Result] = ModuleMapFID;
  Result->IsInferred = true;
  if (!Parent) {
    if (LangOpts.CurrentModule == ModuleName)
      SourceModule = Result;
    Modules[ModuleName] = Result;
    ModuleScopeIDs[Result] = CurrentModuleScopeID;
  }

  Result->IsSystem |= Attrs.IsSystem;
  Result->IsExternC |= Attrs.IsExternC;
  Result->ConfigMacrosExhaustive |= Attrs.IsExhaustive;
  Result->NoUndeclaredIncludes |= Attrs.NoUndeclaredIncludes;
  Result->Directory = FrameworkDir;

  // Chop off the first framework bit, as that is implied.
  StringRef RelativePath = UmbrellaName.str().substr(
      Result->getTopLevelModule()->Directory->getName().size());
  RelativePath = llvm::sys::path::relative_path(RelativePath);

  // umbrella header "umbrella-header-name"
  setUmbrellaHeaderAsWritten(Result, *UmbrellaHeader, ModuleName + ".h",
                             RelativePath);

  // export *
  Result->Exports.push_back(Module::ExportDecl(nullptr, true));

  // module * { export * }
  Result->InferSubmodules = true;
  Result->InferExportWildcard = true;

  // Look for subframeworks.
  std::error_code EC;
  SmallString<128> SubframeworksDirName = FrameworkDir.getName();
  llvm::sys::path::append(SubframeworksDirName, "Frameworks");
  llvm::sys::path::native(SubframeworksDirName);
  llvm::vfs::FileSystem &FS = FileMgr.getVirtualFileSystem();
  for (llvm::vfs::directory_iterator
           Dir = FS.dir_begin(SubframeworksDirName, EC),
           DirEnd;
       Dir != DirEnd && !EC; Dir.increment(EC)) {
    if (!StringRef(Dir->path()).ends_with(".framework"))
      continue;

    if (auto SubframeworkDir = FileMgr.getOptionalDirectoryRef(Dir->path())) {
      // Note: as an egregious but useful hack, we use the real path here and
      // check whether it is actually a subdirectory of the parent directory.
      // This will not be the case if the 'subframework' is actually a symlink
      // out to a top-level framework.
      StringRef SubframeworkDirName =
          FileMgr.getCanonicalName(*SubframeworkDir);
      bool FoundParent = false;
      do {
        // Get the parent directory name.
        SubframeworkDirName
          = llvm::sys::path::parent_path(SubframeworkDirName);
        if (SubframeworkDirName.empty())
          break;

        if (auto SubDir = FileMgr.getDirectory(SubframeworkDirName)) {
          if (*SubDir == FrameworkDir) {
            FoundParent = true;
            break;
          }
        }
      } while (true);

      if (!FoundParent)
        continue;

      // FIXME: Do we want to warn about subframeworks without umbrella headers?
      inferFrameworkModule(*SubframeworkDir, Attrs, Result);
    }
  }

  // If the module is a top-level framework, automatically link against the
  // framework.
  if (!Result->isSubFramework())
    inferFrameworkLink(Result);

  return Result;
}

Module *ModuleMap::createShadowedModule(StringRef Name, bool IsFramework,
                                        Module *ShadowingModule) {

  // Create a new module with this name.
  Module *Result =
      new Module(Name, SourceLocation(), /*Parent=*/nullptr, IsFramework,
                 /*IsExplicit=*/false, NumCreatedModules++);
  Result->ShadowingModule = ShadowingModule;
  Result->markUnavailable(/*Unimportable*/true);
  ModuleScopeIDs[Result] = CurrentModuleScopeID;
  ShadowModules.push_back(Result);

  return Result;
}

void ModuleMap::setUmbrellaHeaderAsWritten(
    Module *Mod, FileEntryRef UmbrellaHeader, const Twine &NameAsWritten,
    const Twine &PathRelativeToRootModuleDirectory) {
  Headers[UmbrellaHeader].push_back(KnownHeader(Mod, NormalHeader));
  Mod->Umbrella = UmbrellaHeader;
  Mod->UmbrellaAsWritten = NameAsWritten.str();
  Mod->UmbrellaRelativeToRootModuleDirectory =
      PathRelativeToRootModuleDirectory.str();
  UmbrellaDirs[UmbrellaHeader.getDir()] = Mod;

  // Notify callbacks that we just added a new header.
  for (const auto &Cb : Callbacks)
    Cb->moduleMapAddUmbrellaHeader(UmbrellaHeader);
}

void ModuleMap::setUmbrellaDirAsWritten(
    Module *Mod, DirectoryEntryRef UmbrellaDir, const Twine &NameAsWritten,
    const Twine &PathRelativeToRootModuleDirectory) {
  Mod->Umbrella = UmbrellaDir;
  Mod->UmbrellaAsWritten = NameAsWritten.str();
  Mod->UmbrellaRelativeToRootModuleDirectory =
      PathRelativeToRootModuleDirectory.str();
  UmbrellaDirs[UmbrellaDir] = Mod;
}

void ModuleMap::addUnresolvedHeader(Module *Mod,
                                    Module::UnresolvedHeaderDirective Header,
                                    bool &NeedsFramework) {
  // If there is a builtin counterpart to this file, add it now so it can
  // wrap the system header.
  if (resolveAsBuiltinHeader(Mod, Header)) {
    // If we have both a builtin and system version of the file, the
    // builtin version may want to inject macros into the system header, so
    // force the system header to be treated as a textual header in this
    // case.
    Header.Kind = headerRoleToKind(ModuleMap::ModuleHeaderRole(
        headerKindToRole(Header.Kind) | ModuleMap::TextualHeader));
    Header.HasBuiltinHeader = true;
  }

  // If possible, don't stat the header until we need to. This requires the
  // user to have provided us with some stat information about the file.
  // FIXME: Add support for lazily stat'ing umbrella headers and excluded
  // headers.
  if ((Header.Size || Header.ModTime) && !Header.IsUmbrella &&
      Header.Kind != Module::HK_Excluded) {
    // We expect more variation in mtime than size, so if we're given both,
    // use the mtime as the key.
    if (Header.ModTime)
      LazyHeadersByModTime[*Header.ModTime].push_back(Mod);
    else
      LazyHeadersBySize[*Header.Size].push_back(Mod);
    Mod->UnresolvedHeaders.push_back(Header);
    return;
  }

  // We don't have stat information or can't defer looking this file up.
  // Perform the lookup now.
  resolveHeader(Mod, Header, NeedsFramework);
}

void ModuleMap::resolveHeaderDirectives(const FileEntry *File) const {
  auto BySize = LazyHeadersBySize.find(File->getSize());
  if (BySize != LazyHeadersBySize.end()) {
    for (auto *M : BySize->second)
      resolveHeaderDirectives(M, File);
    LazyHeadersBySize.erase(BySize);
  }

  auto ByModTime = LazyHeadersByModTime.find(File->getModificationTime());
  if (ByModTime != LazyHeadersByModTime.end()) {
    for (auto *M : ByModTime->second)
      resolveHeaderDirectives(M, File);
    LazyHeadersByModTime.erase(ByModTime);
  }
}

void ModuleMap::resolveHeaderDirectives(
    Module *Mod, std::optional<const FileEntry *> File) const {
  bool NeedsFramework = false;
  SmallVector<Module::UnresolvedHeaderDirective, 1> NewHeaders;
  const auto Size = File ? (*File)->getSize() : 0;
  const auto ModTime = File ? (*File)->getModificationTime() : 0;

  for (auto &Header : Mod->UnresolvedHeaders) {
    if (File && ((Header.ModTime && Header.ModTime != ModTime) ||
                 (Header.Size && Header.Size != Size)))
      NewHeaders.push_back(Header);
    else
      // This operation is logically const; we're just changing how we represent
      // the header information for this file.
      const_cast<ModuleMap *>(this)->resolveHeader(Mod, Header, NeedsFramework);
  }
  Mod->UnresolvedHeaders.swap(NewHeaders);
}

void ModuleMap::addHeader(Module *Mod, Module::Header Header,
                          ModuleHeaderRole Role, bool Imported) {
  KnownHeader KH(Mod, Role);

  // Only add each header to the headers list once.
  // FIXME: Should we diagnose if a header is listed twice in the
  // same module definition?
  auto &HeaderList = Headers[Header.Entry];
  if (llvm::is_contained(HeaderList, KH))
    return;

  HeaderList.push_back(KH);
  Mod->Headers[headerRoleToKind(Role)].push_back(Header);

  bool isCompilingModuleHeader = Mod->isForBuilding(LangOpts);
  if (!Imported || isCompilingModuleHeader) {
    // When we import HeaderFileInfo, the external source is expected to
    // set the isModuleHeader flag itself.
    HeaderInfo.MarkFileModuleHeader(Header.Entry, Role,
                                    isCompilingModuleHeader);
  }

  // Notify callbacks that we just added a new header.
  for (const auto &Cb : Callbacks)
    Cb->moduleMapAddHeader(Header.Entry.getName());
}

FileID ModuleMap::getContainingModuleMapFileID(const Module *Module) const {
  if (Module->DefinitionLoc.isInvalid())
    return {};

  return SourceMgr.getFileID(Module->DefinitionLoc);
}

OptionalFileEntryRef
ModuleMap::getContainingModuleMapFile(const Module *Module) const {
  return SourceMgr.getFileEntryRefForID(getContainingModuleMapFileID(Module));
}

FileID ModuleMap::getModuleMapFileIDForUniquing(const Module *M) const {
  if (M->IsInferred) {
    assert(InferredModuleAllowedBy.count(M) && "missing inferred module map");
    return InferredModuleAllowedBy.find(M)->second;
  }
  return getContainingModuleMapFileID(M);
}

OptionalFileEntryRef
ModuleMap::getModuleMapFileForUniquing(const Module *M) const {
  return SourceMgr.getFileEntryRefForID(getModuleMapFileIDForUniquing(M));
}

void ModuleMap::setInferredModuleAllowedBy(Module *M, FileID ModMapFID) {
  assert(M->IsInferred && "module not inferred");
  InferredModuleAllowedBy[M] = ModMapFID;
}

std::error_code
ModuleMap::canonicalizeModuleMapPath(SmallVectorImpl<char> &Path) {
  StringRef Dir = llvm::sys::path::parent_path({Path.data(), Path.size()});

  // Do not canonicalize within the framework; the module map parser expects
  // Modules/ not Versions/A/Modules.
  if (llvm::sys::path::filename(Dir) == "Modules") {
    StringRef Parent = llvm::sys::path::parent_path(Dir);
    if (Parent.ends_with(".framework"))
      Dir = Parent;
  }

  FileManager &FM = SourceMgr.getFileManager();
  auto DirEntry = FM.getDirectoryRef(Dir.empty() ? "." : Dir);
  if (!DirEntry)
    return llvm::errorToErrorCode(DirEntry.takeError());

  // Canonicalize the directory.
  StringRef CanonicalDir = FM.getCanonicalName(*DirEntry);
  if (CanonicalDir != Dir)
    llvm::sys::path::replace_path_prefix(Path, Dir, CanonicalDir);

  // In theory, the filename component should also be canonicalized if it
  // on a case-insensitive filesystem. However, the extra canonicalization is
  // expensive and if clang looked up the filename it will always be lowercase.

  // Remove ., remove redundant separators, and switch to native separators.
  // This is needed for separators between CanonicalDir and the filename.
  llvm::sys::path::remove_dots(Path);

  return std::error_code();
}

void ModuleMap::addAdditionalModuleMapFile(const Module *M,
                                           FileEntryRef ModuleMap) {
  AdditionalModMaps[M].insert(ModuleMap);
}

LLVM_DUMP_METHOD void ModuleMap::dump() {
  llvm::errs() << "Modules:";
  for (llvm::StringMap<Module *>::iterator M = Modules.begin(),
                                        MEnd = Modules.end();
       M != MEnd; ++M)
    M->getValue()->print(llvm::errs(), 2);

  llvm::errs() << "Headers:";
  for (HeadersMap::iterator H = Headers.begin(), HEnd = Headers.end();
       H != HEnd; ++H) {
    llvm::errs() << "  \"" << H->first.getName() << "\" -> ";
    for (SmallVectorImpl<KnownHeader>::const_iterator I = H->second.begin(),
                                                      E = H->second.end();
         I != E; ++I) {
      if (I != H->second.begin())
        llvm::errs() << ",";
      llvm::errs() << I->getModule()->getFullModuleName();
    }
    llvm::errs() << "\n";
  }
}

bool ModuleMap::resolveExports(Module *Mod, bool Complain) {
  auto Unresolved = std::move(Mod->UnresolvedExports);
  Mod->UnresolvedExports.clear();
  for (auto &UE : Unresolved) {
    Module::ExportDecl Export = resolveExport(Mod, UE, Complain);
    if (Export.getPointer() || Export.getInt())
      Mod->Exports.push_back(Export);
    else
      Mod->UnresolvedExports.push_back(UE);
  }
  return !Mod->UnresolvedExports.empty();
}

bool ModuleMap::resolveUses(Module *Mod, bool Complain) {
  auto *Top = Mod->getTopLevelModule();
  auto Unresolved = std::move(Top->UnresolvedDirectUses);
  Top->UnresolvedDirectUses.clear();
  for (auto &UDU : Unresolved) {
    Module *DirectUse = resolveModuleId(UDU, Top, Complain);
    if (DirectUse)
      Top->DirectUses.push_back(DirectUse);
    else
      Top->UnresolvedDirectUses.push_back(UDU);
  }
  return !Top->UnresolvedDirectUses.empty();
}

bool ModuleMap::resolveConflicts(Module *Mod, bool Complain) {
  auto Unresolved = std::move(Mod->UnresolvedConflicts);
  Mod->UnresolvedConflicts.clear();
  for (auto &UC : Unresolved) {
    if (Module *OtherMod = resolveModuleId(UC.Id, Mod, Complain)) {
      Module::Conflict Conflict;
      Conflict.Other = OtherMod;
      Conflict.Message = UC.Message;
      Mod->Conflicts.push_back(Conflict);
    } else
      Mod->UnresolvedConflicts.push_back(UC);
  }
  return !Mod->UnresolvedConflicts.empty();
}

//----------------------------------------------------------------------------//
// Module map file parser
//----------------------------------------------------------------------------//

namespace clang {

  /// A token in a module map file.
  struct MMToken {
    enum TokenKind {
      Comma,
      ConfigMacros,
      Conflict,
      EndOfFile,
      HeaderKeyword,
      Identifier,
      Exclaim,
      ExcludeKeyword,
      ExplicitKeyword,
      ExportKeyword,
      ExportAsKeyword,
      ExternKeyword,
      FrameworkKeyword,
      LinkKeyword,
      ModuleKeyword,
      Period,
      PrivateKeyword,
      UmbrellaKeyword,
      UseKeyword,
      RequiresKeyword,
      Star,
      StringLiteral,
      IntegerLiteral,
      TextualKeyword,
      LBrace,
      RBrace,
      LSquare,
      RSquare
    } Kind;

    SourceLocation::UIntTy Location;
    unsigned StringLength;
    union {
      // If Kind != IntegerLiteral.
      const char *StringData;

      // If Kind == IntegerLiteral.
      uint64_t IntegerValue;
    };

    void clear() {
      Kind = EndOfFile;
      Location = 0;
      StringLength = 0;
      StringData = nullptr;
    }

    bool is(TokenKind K) const { return Kind == K; }

    SourceLocation getLocation() const {
      return SourceLocation::getFromRawEncoding(Location);
    }

    uint64_t getInteger() const {
      return Kind == IntegerLiteral ? IntegerValue : 0;
    }

    StringRef getString() const {
      return Kind == IntegerLiteral ? StringRef()
                                    : StringRef(StringData, StringLength);
    }
  };

  class ModuleMapParser {
    Lexer &L;
    SourceManager &SourceMgr;

    /// Default target information, used only for string literal
    /// parsing.
    const TargetInfo *Target;

    DiagnosticsEngine &Diags;
    ModuleMap &Map;

    /// The current module map file.
    FileID ModuleMapFID;

    /// Source location of most recent parsed module declaration
    SourceLocation CurrModuleDeclLoc;

    /// The directory that file names in this module map file should
    /// be resolved relative to.
    DirectoryEntryRef Directory;

    /// Whether this module map is in a system header directory.
    bool IsSystem;

    /// Whether an error occurred.
    bool HadError = false;

    /// Stores string data for the various string literals referenced
    /// during parsing.
    llvm::BumpPtrAllocator StringData;

    /// The current token.
    MMToken Tok;

    /// The active module.
    Module *ActiveModule = nullptr;

    /// Whether a module uses the 'requires excluded' hack to mark its
    /// contents as 'textual'.
    ///
    /// On older Darwin SDK versions, 'requires excluded' is used to mark the
    /// contents of the Darwin.C.excluded (assert.h) and Tcl.Private modules as
    /// non-modular headers.  For backwards compatibility, we continue to
    /// support this idiom for just these modules, and map the headers to
    /// 'textual' to match the original intent.
    llvm::SmallPtrSet<Module *, 2> UsesRequiresExcludedHack;

    /// Consume the current token and return its location.
    SourceLocation consumeToken();

    /// Skip tokens until we reach the a token with the given kind
    /// (or the end of the file).
    void skipUntil(MMToken::TokenKind K);

    bool parseModuleId(ModuleId &Id);
    void parseModuleDecl();
    void parseExternModuleDecl();
    void parseRequiresDecl();
    void parseHeaderDecl(MMToken::TokenKind, SourceLocation LeadingLoc);
    void parseUmbrellaDirDecl(SourceLocation UmbrellaLoc);
    void parseExportDecl();
    void parseExportAsDecl();
    void parseUseDecl();
    void parseLinkDecl();
    void parseConfigMacros();
    void parseConflict();
    void parseInferredModuleDecl(bool Framework, bool Explicit);

    /// Private modules are canonicalized as Foo_Private. Clang provides extra
    /// module map search logic to find the appropriate private module when PCH
    /// is used with implicit module maps. Warn when private modules are written
    /// in other ways (FooPrivate and Foo.Private), providing notes and fixits.
    void diagnosePrivateModules(SourceLocation ExplicitLoc,
                                SourceLocation FrameworkLoc);

    using Attributes = ModuleMap::Attributes;

    bool parseOptionalAttributes(Attributes &Attrs);

  public:
    ModuleMapParser(Lexer &L, SourceManager &SourceMgr,
                    const TargetInfo *Target, DiagnosticsEngine &Diags,
                    ModuleMap &Map, FileID ModuleMapFID,
                    DirectoryEntryRef Directory, bool IsSystem)
        : L(L), SourceMgr(SourceMgr), Target(Target), Diags(Diags), Map(Map),
          ModuleMapFID(ModuleMapFID), Directory(Directory), IsSystem(IsSystem) {
      Tok.clear();
      consumeToken();
    }

    bool parseModuleMapFile();

    bool terminatedByDirective() { return false; }
    SourceLocation getLocation() { return Tok.getLocation(); }
  };

} // namespace clang

SourceLocation ModuleMapParser::consumeToken() {
  SourceLocation Result = Tok.getLocation();

retry:
  Tok.clear();
  Token LToken;
  L.LexFromRawLexer(LToken);
  Tok.Location = LToken.getLocation().getRawEncoding();
  switch (LToken.getKind()) {
  case tok::raw_identifier: {
    StringRef RI = LToken.getRawIdentifier();
    Tok.StringData = RI.data();
    Tok.StringLength = RI.size();
    Tok.Kind = llvm::StringSwitch<MMToken::TokenKind>(RI)
                 .Case("config_macros", MMToken::ConfigMacros)
                 .Case("conflict", MMToken::Conflict)
                 .Case("exclude", MMToken::ExcludeKeyword)
                 .Case("explicit", MMToken::ExplicitKeyword)
                 .Case("export", MMToken::ExportKeyword)
                 .Case("export_as", MMToken::ExportAsKeyword)
                 .Case("extern", MMToken::ExternKeyword)
                 .Case("framework", MMToken::FrameworkKeyword)
                 .Case("header", MMToken::HeaderKeyword)
                 .Case("link", MMToken::LinkKeyword)
                 .Case("module", MMToken::ModuleKeyword)
                 .Case("private", MMToken::PrivateKeyword)
                 .Case("requires", MMToken::RequiresKeyword)
                 .Case("textual", MMToken::TextualKeyword)
                 .Case("umbrella", MMToken::UmbrellaKeyword)
                 .Case("use", MMToken::UseKeyword)
                 .Default(MMToken::Identifier);
    break;
  }

  case tok::comma:
    Tok.Kind = MMToken::Comma;
    break;

  case tok::eof:
    Tok.Kind = MMToken::EndOfFile;
    break;

  case tok::l_brace:
    Tok.Kind = MMToken::LBrace;
    break;

  case tok::l_square:
    Tok.Kind = MMToken::LSquare;
    break;

  case tok::period:
    Tok.Kind = MMToken::Period;
    break;

  case tok::r_brace:
    Tok.Kind = MMToken::RBrace;
    break;

  case tok::r_square:
    Tok.Kind = MMToken::RSquare;
    break;

  case tok::star:
    Tok.Kind = MMToken::Star;
    break;

  case tok::exclaim:
    Tok.Kind = MMToken::Exclaim;
    break;

  case tok::string_literal: {
    if (LToken.hasUDSuffix()) {
      Diags.Report(LToken.getLocation(), diag::err_invalid_string_udl);
      HadError = true;
      goto retry;
    }

    // Parse the string literal.
    LangOptions LangOpts;
    StringLiteralParser StringLiteral(LToken, SourceMgr, LangOpts, *Target);
    if (StringLiteral.hadError)
      goto retry;

    // Copy the string literal into our string data allocator.
    unsigned Length = StringLiteral.GetStringLength();
    char *Saved = StringData.Allocate<char>(Length + 1);
    memcpy(Saved, StringLiteral.GetString().data(), Length);
    Saved[Length] = 0;

    // Form the token.
    Tok.Kind = MMToken::StringLiteral;
    Tok.StringData = Saved;
    Tok.StringLength = Length;
    break;
  }

  case tok::numeric_constant: {
    // We don't support any suffixes or other complications.
    SmallString<32> SpellingBuffer;
    SpellingBuffer.resize(LToken.getLength() + 1);
    const char *Start = SpellingBuffer.data();
    unsigned Length =
        Lexer::getSpelling(LToken, Start, SourceMgr, Map.LangOpts);
    uint64_t Value;
    if (StringRef(Start, Length).getAsInteger(0, Value)) {
      Diags.Report(Tok.getLocation(), diag::err_mmap_unknown_token);
      HadError = true;
      goto retry;
    }

    Tok.Kind = MMToken::IntegerLiteral;
    Tok.IntegerValue = Value;
    break;
  }

  case tok::comment:
    goto retry;

  case tok::hash:
    // A module map can be terminated prematurely by
    //   #pragma clang module contents
    // When building the module, we'll treat the rest of the file as the
    // contents of the module.
    {
      auto NextIsIdent = [&](StringRef Str) -> bool {
        L.LexFromRawLexer(LToken);
        return !LToken.isAtStartOfLine() && LToken.is(tok::raw_identifier) &&
               LToken.getRawIdentifier() == Str;
      };
      if (NextIsIdent("pragma") && NextIsIdent("clang") &&
          NextIsIdent("module") && NextIsIdent("contents")) {
        Tok.Kind = MMToken::EndOfFile;
        break;
      }
    }
    [[fallthrough]];

  default:
    Diags.Report(Tok.getLocation(), diag::err_mmap_unknown_token);
    HadError = true;
    goto retry;
  }

  return Result;
}

void ModuleMapParser::skipUntil(MMToken::TokenKind K) {
  unsigned braceDepth = 0;
  unsigned squareDepth = 0;
  do {
    switch (Tok.Kind) {
    case MMToken::EndOfFile:
      return;

    case MMToken::LBrace:
      if (Tok.is(K) && braceDepth == 0 && squareDepth == 0)
        return;

      ++braceDepth;
      break;

    case MMToken::LSquare:
      if (Tok.is(K) && braceDepth == 0 && squareDepth == 0)
        return;

      ++squareDepth;
      break;

    case MMToken::RBrace:
      if (braceDepth > 0)
        --braceDepth;
      else if (Tok.is(K))
        return;
      break;

    case MMToken::RSquare:
      if (squareDepth > 0)
        --squareDepth;
      else if (Tok.is(K))
        return;
      break;

    default:
      if (braceDepth == 0 && squareDepth == 0 && Tok.is(K))
        return;
      break;
    }

   consumeToken();
  } while (true);
}

/// Parse a module-id.
///
///   module-id:
///     identifier
///     identifier '.' module-id
///
/// \returns true if an error occurred, false otherwise.
bool ModuleMapParser::parseModuleId(ModuleId &Id) {
  Id.clear();
  do {
    if (Tok.is(MMToken::Identifier) || Tok.is(MMToken::StringLiteral)) {
      Id.push_back(
          std::make_pair(std::string(Tok.getString()), Tok.getLocation()));
      consumeToken();
    } else {
      Diags.Report(Tok.getLocation(), diag::err_mmap_expected_module_name);
      return true;
    }

    if (!Tok.is(MMToken::Period))
      break;

    consumeToken();
  } while (true);

  return false;
}

namespace {

  /// Enumerates the known attributes.
  enum AttributeKind {
    /// An unknown attribute.
    AT_unknown,

    /// The 'system' attribute.
    AT_system,

    /// The 'extern_c' attribute.
    AT_extern_c,

    /// The 'exhaustive' attribute.
    AT_exhaustive,

    /// The 'no_undeclared_includes' attribute.
    AT_no_undeclared_includes
  };

} // namespace

/// Private modules are canonicalized as Foo_Private. Clang provides extra
/// module map search logic to find the appropriate private module when PCH
/// is used with implicit module maps. Warn when private modules are written
/// in other ways (FooPrivate and Foo.Private), providing notes and fixits.
void ModuleMapParser::diagnosePrivateModules(SourceLocation ExplicitLoc,
                                             SourceLocation FrameworkLoc) {
  auto GenNoteAndFixIt = [&](StringRef BadName, StringRef Canonical,
                             const Module *M, SourceRange ReplLoc) {
    auto D = Diags.Report(ActiveModule->DefinitionLoc,
                          diag::note_mmap_rename_top_level_private_module);
    D << BadName << M->Name;
    D << FixItHint::CreateReplacement(ReplLoc, Canonical);
  };

  for (auto E = Map.module_begin(); E != Map.module_end(); ++E) {
    auto const *M = E->getValue();
    if (M->Directory != ActiveModule->Directory)
      continue;

    SmallString<128> FullName(ActiveModule->getFullModuleName());
    if (!FullName.starts_with(M->Name) && !FullName.ends_with("Private"))
      continue;
    SmallString<128> FixedPrivModDecl;
    SmallString<128> Canonical(M->Name);
    Canonical.append("_Private");

    // Foo.Private -> Foo_Private
    if (ActiveModule->Parent && ActiveModule->Name == "Private" && !M->Parent &&
        M->Name == ActiveModule->Parent->Name) {
      Diags.Report(ActiveModule->DefinitionLoc,
                   diag::warn_mmap_mismatched_private_submodule)
          << FullName;

      SourceLocation FixItInitBegin = CurrModuleDeclLoc;
      if (FrameworkLoc.isValid())
        FixItInitBegin = FrameworkLoc;
      if (ExplicitLoc.isValid())
        FixItInitBegin = ExplicitLoc;

      if (FrameworkLoc.isValid() || ActiveModule->Parent->IsFramework)
        FixedPrivModDecl.append("framework ");
      FixedPrivModDecl.append("module ");
      FixedPrivModDecl.append(Canonical);

      GenNoteAndFixIt(FullName, FixedPrivModDecl, M,
                      SourceRange(FixItInitBegin, ActiveModule->DefinitionLoc));
      continue;
    }

    // FooPrivate and whatnots -> Foo_Private
    if (!ActiveModule->Parent && !M->Parent && M->Name != ActiveModule->Name &&
        ActiveModule->Name != Canonical) {
      Diags.Report(ActiveModule->DefinitionLoc,
                   diag::warn_mmap_mismatched_private_module_name)
          << ActiveModule->Name;
      GenNoteAndFixIt(ActiveModule->Name, Canonical, M,
                      SourceRange(ActiveModule->DefinitionLoc));
    }
  }
}

/// Parse a module declaration.
///
///   module-declaration:
///     'extern' 'module' module-id string-literal
///     'explicit'[opt] 'framework'[opt] 'module' module-id attributes[opt]
///       { module-member* }
///
///   module-member:
///     requires-declaration
///     header-declaration
///     submodule-declaration
///     export-declaration
///     export-as-declaration
///     link-declaration
///
///   submodule-declaration:
///     module-declaration
///     inferred-submodule-declaration
void ModuleMapParser::parseModuleDecl() {
  assert(Tok.is(MMToken::ExplicitKeyword) || Tok.is(MMToken::ModuleKeyword) ||
         Tok.is(MMToken::FrameworkKeyword) || Tok.is(MMToken::ExternKeyword));
  if (Tok.is(MMToken::ExternKeyword)) {
    parseExternModuleDecl();
    return;
  }

  // Parse 'explicit' or 'framework' keyword, if present.
  SourceLocation ExplicitLoc;
  SourceLocation FrameworkLoc;
  bool Explicit = false;
  bool Framework = false;

  // Parse 'explicit' keyword, if present.
  if (Tok.is(MMToken::ExplicitKeyword)) {
    ExplicitLoc = consumeToken();
    Explicit = true;
  }

  // Parse 'framework' keyword, if present.
  if (Tok.is(MMToken::FrameworkKeyword)) {
    FrameworkLoc = consumeToken();
    Framework = true;
  }

  // Parse 'module' keyword.
  if (!Tok.is(MMToken::ModuleKeyword)) {
    Diags.Report(Tok.getLocation(), diag::err_mmap_expected_module);
    consumeToken();
    HadError = true;
    return;
  }
  CurrModuleDeclLoc = consumeToken(); // 'module' keyword

  // If we have a wildcard for the module name, this is an inferred submodule.
  // Parse it.
  if (Tok.is(MMToken::Star))
    return parseInferredModuleDecl(Framework, Explicit);

  // Parse the module name.
  ModuleId Id;
  if (parseModuleId(Id)) {
    HadError = true;
    return;
  }

  if (ActiveModule) {
    if (Id.size() > 1) {
      Diags.Report(Id.front().second, diag::err_mmap_nested_submodule_id)
        << SourceRange(Id.front().second, Id.back().second);

      HadError = true;
      return;
    }
  } else if (Id.size() == 1 && Explicit) {
    // Top-level modules can't be explicit.
    Diags.Report(ExplicitLoc, diag::err_mmap_explicit_top_level);
    Explicit = false;
    ExplicitLoc = SourceLocation();
    HadError = true;
  }

  Module *PreviousActiveModule = ActiveModule;
  if (Id.size() > 1) {
    // This module map defines a submodule. Go find the module of which it
    // is a submodule.
    ActiveModule = nullptr;
    const Module *TopLevelModule = nullptr;
    for (unsigned I = 0, N = Id.size() - 1; I != N; ++I) {
      if (Module *Next = Map.lookupModuleQualified(Id[I].first, ActiveModule)) {
        if (I == 0)
          TopLevelModule = Next;
        ActiveModule = Next;
        continue;
      }

      Diags.Report(Id[I].second, diag::err_mmap_missing_parent_module)
          << Id[I].first << (ActiveModule != nullptr)
          << (ActiveModule
                  ? ActiveModule->getTopLevelModule()->getFullModuleName()
                  : "");
      HadError = true;
    }

    if (TopLevelModule &&
        ModuleMapFID != Map.getContainingModuleMapFileID(TopLevelModule)) {
      assert(ModuleMapFID !=
                 Map.getModuleMapFileIDForUniquing(TopLevelModule) &&
             "submodule defined in same file as 'module *' that allowed its "
             "top-level module");
      Map.addAdditionalModuleMapFile(
          TopLevelModule, *SourceMgr.getFileEntryRefForID(ModuleMapFID));
    }
  }

  StringRef ModuleName = Id.back().first;
  SourceLocation ModuleNameLoc = Id.back().second;

  // Parse the optional attribute list.
  Attributes Attrs;
  if (parseOptionalAttributes(Attrs))
    return;

  // Parse the opening brace.
  if (!Tok.is(MMToken::LBrace)) {
    Diags.Report(Tok.getLocation(), diag::err_mmap_expected_lbrace)
      << ModuleName;
    HadError = true;
    return;
  }
  SourceLocation LBraceLoc = consumeToken();

  // Determine whether this (sub)module has already been defined.
  Module *ShadowingModule = nullptr;
  if (Module *Existing = Map.lookupModuleQualified(ModuleName, ActiveModule)) {
    // We might see a (re)definition of a module that we already have a
    // definition for in four cases:
    //  - If we loaded one definition from an AST file and we've just found a
    //    corresponding definition in a module map file, or
    bool LoadedFromASTFile = Existing->IsFromModuleFile;
    //  - If we previously inferred this module from different module map file.
    bool Inferred = Existing->IsInferred;
    //  - If we're building a framework that vends a module map, we might've
    //    previously seen the one in intermediate products and now the system
    //    one.
    // FIXME: If we're parsing module map file that looks like this:
    //          framework module FW { ... }
    //          module FW.Sub { ... }
    //        We can't check the framework qualifier, since it's not attached to
    //        the definition of Sub. Checking that qualifier on \c Existing is
    //        not correct either, since we might've previously seen:
    //          module FW { ... }
    //          module FW.Sub { ... }
    //        We should enforce consistency of redefinitions so that we can rely
    //        that \c Existing is part of a framework iff the redefinition of FW
    //        we have just skipped had it too. Once we do that, stop checking
    //        the local framework qualifier and only rely on \c Existing.
    bool PartOfFramework = Framework || Existing->isPartOfFramework();
    //  - If we're building a (preprocessed) module and we've just loaded the
    //    module map file from which it was created.
    bool ParsedAsMainInput =
        Map.LangOpts.getCompilingModule() == LangOptions::CMK_ModuleMap &&
        Map.LangOpts.CurrentModule == ModuleName &&
        SourceMgr.getDecomposedLoc(ModuleNameLoc).first !=
            SourceMgr.getDecomposedLoc(Existing->DefinitionLoc).first;
    if (LoadedFromASTFile || Inferred || PartOfFramework || ParsedAsMainInput) {
      ActiveModule = PreviousActiveModule;
      // Skip the module definition.
      skipUntil(MMToken::RBrace);
      if (Tok.is(MMToken::RBrace))
        consumeToken();
      else {
        Diags.Report(Tok.getLocation(), diag::err_mmap_expected_rbrace);
        Diags.Report(LBraceLoc, diag::note_mmap_lbrace_match);
        HadError = true;
      }
      return;
    }

    if (!Existing->Parent && Map.mayShadowNewModule(Existing)) {
      ShadowingModule = Existing;
    } else {
      // This is not a shawdowed module decl, it is an illegal redefinition.
      Diags.Report(ModuleNameLoc, diag::err_mmap_module_redefinition)
          << ModuleName;
      Diags.Report(Existing->DefinitionLoc, diag::note_mmap_prev_definition);

      // Skip the module definition.
      skipUntil(MMToken::RBrace);
      if (Tok.is(MMToken::RBrace))
        consumeToken();

      HadError = true;
      return;
    }
  }

  // Start defining this module.
  if (ShadowingModule) {
    ActiveModule =
        Map.createShadowedModule(ModuleName, Framework, ShadowingModule);
  } else {
    ActiveModule =
        Map.findOrCreateModule(ModuleName, ActiveModule, Framework, Explicit)
            .first;
  }

  ActiveModule->DefinitionLoc = ModuleNameLoc;
  if (Attrs.IsSystem || IsSystem)
    ActiveModule->IsSystem = true;
  if (Attrs.IsExternC)
    ActiveModule->IsExternC = true;
  if (Attrs.NoUndeclaredIncludes)
    ActiveModule->NoUndeclaredIncludes = true;
  ActiveModule->Directory = Directory;

  StringRef MapFileName(
      SourceMgr.getFileEntryRefForID(ModuleMapFID)->getName());
  if (MapFileName.ends_with("module.private.modulemap") ||
      MapFileName.ends_with("module_private.map")) {
    ActiveModule->ModuleMapIsPrivate = true;
  }

  // Private modules named as FooPrivate, Foo.Private or similar are likely a
  // user error; provide warnings, notes and fixits to direct users to use
  // Foo_Private instead.
  SourceLocation StartLoc =
      SourceMgr.getLocForStartOfFile(SourceMgr.getMainFileID());
  if (Map.HeaderInfo.getHeaderSearchOpts().ImplicitModuleMaps &&
      !Diags.isIgnored(diag::warn_mmap_mismatched_private_submodule,
                       StartLoc) &&
      !Diags.isIgnored(diag::warn_mmap_mismatched_private_module_name,
                       StartLoc) &&
      ActiveModule->ModuleMapIsPrivate)
    diagnosePrivateModules(ExplicitLoc, FrameworkLoc);

  bool Done = false;
  do {
    switch (Tok.Kind) {
    case MMToken::EndOfFile:
    case MMToken::RBrace:
      Done = true;
      break;

    case MMToken::ConfigMacros:
      parseConfigMacros();
      break;

    case MMToken::Conflict:
      parseConflict();
      break;

    case MMToken::ExplicitKeyword:
    case MMToken::ExternKeyword:
    case MMToken::FrameworkKeyword:
    case MMToken::ModuleKeyword:
      parseModuleDecl();
      break;

    case MMToken::ExportKeyword:
      parseExportDecl();
      break;

    case MMToken::ExportAsKeyword:
      parseExportAsDecl();
      break;

    case MMToken::UseKeyword:
      parseUseDecl();
      break;

    case MMToken::RequiresKeyword:
      parseRequiresDecl();
      break;

    case MMToken::TextualKeyword:
      parseHeaderDecl(MMToken::TextualKeyword, consumeToken());
      break;

    case MMToken::UmbrellaKeyword: {
      SourceLocation UmbrellaLoc = consumeToken();
      if (Tok.is(MMToken::HeaderKeyword))
        parseHeaderDecl(MMToken::UmbrellaKeyword, UmbrellaLoc);
      else
        parseUmbrellaDirDecl(UmbrellaLoc);
      break;
    }

    case MMToken::ExcludeKeyword:
      parseHeaderDecl(MMToken::ExcludeKeyword, consumeToken());
      break;

    case MMToken::PrivateKeyword:
      parseHeaderDecl(MMToken::PrivateKeyword, consumeToken());
      break;

    case MMToken::HeaderKeyword:
      parseHeaderDecl(MMToken::HeaderKeyword, consumeToken());
      break;

    case MMToken::LinkKeyword:
      parseLinkDecl();
      break;

    default:
      Diags.Report(Tok.getLocation(), diag::err_mmap_expected_member);
      consumeToken();
      break;
    }
  } while (!Done);

  if (Tok.is(MMToken::RBrace))
    consumeToken();
  else {
    Diags.Report(Tok.getLocation(), diag::err_mmap_expected_rbrace);
    Diags.Report(LBraceLoc, diag::note_mmap_lbrace_match);
    HadError = true;
  }

  // If the active module is a top-level framework, and there are no link
  // libraries, automatically link against the framework.
  if (ActiveModule->IsFramework && !ActiveModule->isSubFramework() &&
      ActiveModule->LinkLibraries.empty())
    inferFrameworkLink(ActiveModule);

  // If the module meets all requirements but is still unavailable, mark the
  // whole tree as unavailable to prevent it from building.
  if (!ActiveModule->IsAvailable && !ActiveModule->IsUnimportable &&
      ActiveModule->Parent) {
    ActiveModule->getTopLevelModule()->markUnavailable(/*Unimportable=*/false);
    ActiveModule->getTopLevelModule()->MissingHeaders.append(
      ActiveModule->MissingHeaders.begin(), ActiveModule->MissingHeaders.end());
  }

  // We're done parsing this module. Pop back to the previous module.
  ActiveModule = PreviousActiveModule;
}

/// Parse an extern module declaration.
///
///   extern module-declaration:
///     'extern' 'module' module-id string-literal
void ModuleMapParser::parseExternModuleDecl() {
  assert(Tok.is(MMToken::ExternKeyword));
  SourceLocation ExternLoc = consumeToken(); // 'extern' keyword

  // Parse 'module' keyword.
  if (!Tok.is(MMToken::ModuleKeyword)) {
    Diags.Report(Tok.getLocation(), diag::err_mmap_expected_module);
    consumeToken();
    HadError = true;
    return;
  }
  consumeToken(); // 'module' keyword

  // Parse the module name.
  ModuleId Id;
  if (parseModuleId(Id)) {
    HadError = true;
    return;
  }

  // Parse the referenced module map file name.
  if (!Tok.is(MMToken::StringLiteral)) {
    Diags.Report(Tok.getLocation(), diag::err_mmap_expected_mmap_file);
    HadError = true;
    return;
  }
  std::string FileName = std::string(Tok.getString());
  consumeToken(); // filename

  StringRef FileNameRef = FileName;
  SmallString<128> ModuleMapFileName;
  if (llvm::sys::path::is_relative(FileNameRef)) {
    ModuleMapFileName += Directory.getName();
    llvm::sys::path::append(ModuleMapFileName, FileName);
    FileNameRef = ModuleMapFileName;
  }
  if (auto File = SourceMgr.getFileManager().getOptionalFileRef(FileNameRef))
    Map.parseModuleMapFile(
        *File, IsSystem,
        Map.HeaderInfo.getHeaderSearchOpts().ModuleMapFileHomeIsCwd
            ? Directory
            : File->getDir(),
        FileID(), nullptr, ExternLoc);
}

/// Whether to add the requirement \p Feature to the module \p M.
///
/// This preserves backwards compatibility for two hacks in the Darwin system
/// module map files:
///
/// 1. The use of 'requires excluded' to make headers non-modular, which
///    should really be mapped to 'textual' now that we have this feature.  We
///    drop the 'excluded' requirement, and set \p IsRequiresExcludedHack to
///    true.  Later, this bit will be used to map all the headers inside this
///    module to 'textual'.
///
///    This affects Darwin.C.excluded (for assert.h) and Tcl.Private.
///
/// 2. Removes a bogus cplusplus requirement from IOKit.avc.  This requirement
///    was never correct and causes issues now that we check it, so drop it.
static bool shouldAddRequirement(Module *M, StringRef Feature,
                                 bool &IsRequiresExcludedHack) {
  if (Feature == "excluded" &&
      (M->fullModuleNameIs({"Darwin", "C", "excluded"}) ||
       M->fullModuleNameIs({"Tcl", "Private"}))) {
    IsRequiresExcludedHack = true;
    return false;
  } else if (Feature == "cplusplus" && M->fullModuleNameIs({"IOKit", "avc"})) {
    return false;
  }

  return true;
}

/// Parse a requires declaration.
///
///   requires-declaration:
///     'requires' feature-list
///
///   feature-list:
///     feature ',' feature-list
///     feature
///
///   feature:
///     '!'[opt] identifier
void ModuleMapParser::parseRequiresDecl() {
  assert(Tok.is(MMToken::RequiresKeyword));

  // Parse 'requires' keyword.
  consumeToken();

  // Parse the feature-list.
  do {
    bool RequiredState = true;
    if (Tok.is(MMToken::Exclaim)) {
      RequiredState = false;
      consumeToken();
    }

    if (!Tok.is(MMToken::Identifier)) {
      Diags.Report(Tok.getLocation(), diag::err_mmap_expected_feature);
      HadError = true;
      return;
    }

    // Consume the feature name.
    std::string Feature = std::string(Tok.getString());
    consumeToken();

    bool IsRequiresExcludedHack = false;
    bool ShouldAddRequirement =
        shouldAddRequirement(ActiveModule, Feature, IsRequiresExcludedHack);

    if (IsRequiresExcludedHack)
      UsesRequiresExcludedHack.insert(ActiveModule);

    if (ShouldAddRequirement) {
      // Add this feature.
      ActiveModule->addRequirement(Feature, RequiredState, Map.LangOpts,
                                   *Map.Target);
    }

    if (!Tok.is(MMToken::Comma))
      break;

    // Consume the comma.
    consumeToken();
  } while (true);
}

/// Parse a header declaration.
///
///   header-declaration:
///     'textual'[opt] 'header' string-literal
///     'private' 'textual'[opt] 'header' string-literal
///     'exclude' 'header' string-literal
///     'umbrella' 'header' string-literal
///
/// FIXME: Support 'private textual header'.
void ModuleMapParser::parseHeaderDecl(MMToken::TokenKind LeadingToken,
                                      SourceLocation LeadingLoc) {
  // We've already consumed the first token.
  ModuleMap::ModuleHeaderRole Role = ModuleMap::NormalHeader;

  if (LeadingToken == MMToken::PrivateKeyword) {
    Role = ModuleMap::PrivateHeader;
    // 'private' may optionally be followed by 'textual'.
    if (Tok.is(MMToken::TextualKeyword)) {
      LeadingToken = Tok.Kind;
      consumeToken();
    }
  } else if (LeadingToken == MMToken::ExcludeKeyword) {
    Role = ModuleMap::ExcludedHeader;
  }

  if (LeadingToken == MMToken::TextualKeyword)
    Role = ModuleMap::ModuleHeaderRole(Role | ModuleMap::TextualHeader);

  if (UsesRequiresExcludedHack.count(ActiveModule)) {
    // Mark this header 'textual' (see doc comment for
    // Module::UsesRequiresExcludedHack).
    Role = ModuleMap::ModuleHeaderRole(Role | ModuleMap::TextualHeader);
  }

  if (LeadingToken != MMToken::HeaderKeyword) {
    if (!Tok.is(MMToken::HeaderKeyword)) {
      Diags.Report(Tok.getLocation(), diag::err_mmap_expected_header)
          << (LeadingToken == MMToken::PrivateKeyword ? "private" :
              LeadingToken == MMToken::ExcludeKeyword ? "exclude" :
              LeadingToken == MMToken::TextualKeyword ? "textual" : "umbrella");
      return;
    }
    consumeToken();
  }

  // Parse the header name.
  if (!Tok.is(MMToken::StringLiteral)) {
    Diags.Report(Tok.getLocation(), diag::err_mmap_expected_header)
      << "header";
    HadError = true;
    return;
  }
  Module::UnresolvedHeaderDirective Header;
  Header.FileName = std::string(Tok.getString());
  Header.FileNameLoc = consumeToken();
  Header.IsUmbrella = LeadingToken == MMToken::UmbrellaKeyword;
  Header.Kind = Map.headerRoleToKind(Role);

  // Check whether we already have an umbrella.
  if (Header.IsUmbrella &&
      !std::holds_alternative<std::monostate>(ActiveModule->Umbrella)) {
    Diags.Report(Header.FileNameLoc, diag::err_mmap_umbrella_clash)
      << ActiveModule->getFullModuleName();
    HadError = true;
    return;
  }

  // If we were given stat information, parse it so we can skip looking for
  // the file.
  if (Tok.is(MMToken::LBrace)) {
    SourceLocation LBraceLoc = consumeToken();

    while (!Tok.is(MMToken::RBrace) && !Tok.is(MMToken::EndOfFile)) {
      enum Attribute { Size, ModTime, Unknown };
      StringRef Str = Tok.getString();
      SourceLocation Loc = consumeToken();
      switch (llvm::StringSwitch<Attribute>(Str)
                  .Case("size", Size)
                  .Case("mtime", ModTime)
                  .Default(Unknown)) {
      case Size:
        if (Header.Size)
          Diags.Report(Loc, diag::err_mmap_duplicate_header_attribute) << Str;
        if (!Tok.is(MMToken::IntegerLiteral)) {
          Diags.Report(Tok.getLocation(),
                       diag::err_mmap_invalid_header_attribute_value) << Str;
          skipUntil(MMToken::RBrace);
          break;
        }
        Header.Size = Tok.getInteger();
        consumeToken();
        break;

      case ModTime:
        if (Header.ModTime)
          Diags.Report(Loc, diag::err_mmap_duplicate_header_attribute) << Str;
        if (!Tok.is(MMToken::IntegerLiteral)) {
          Diags.Report(Tok.getLocation(),
                       diag::err_mmap_invalid_header_attribute_value) << Str;
          skipUntil(MMToken::RBrace);
          break;
        }
        Header.ModTime = Tok.getInteger();
        consumeToken();
        break;

      case Unknown:
        Diags.Report(Loc, diag::err_mmap_expected_header_attribute);
        skipUntil(MMToken::RBrace);
        break;
      }
    }

    if (Tok.is(MMToken::RBrace))
      consumeToken();
    else {
      Diags.Report(Tok.getLocation(), diag::err_mmap_expected_rbrace);
      Diags.Report(LBraceLoc, diag::note_mmap_lbrace_match);
      HadError = true;
    }
  }

  bool NeedsFramework = false;
  // Don't add headers to the builtin modules if the builtin headers belong to
  // the system modules, with the exception of __stddef_max_align_t.h which
  // always had its own module.
  if (!Map.LangOpts.BuiltinHeadersInSystemModules ||
      !isBuiltInModuleName(ActiveModule->getTopLevelModuleName()) ||
      ActiveModule->fullModuleNameIs({"_Builtin_stddef", "max_align_t"}))
    Map.addUnresolvedHeader(ActiveModule, std::move(Header), NeedsFramework);

  if (NeedsFramework)
    Diags.Report(CurrModuleDeclLoc, diag::note_mmap_add_framework_keyword)
      << ActiveModule->getFullModuleName()
      << FixItHint::CreateReplacement(CurrModuleDeclLoc, "framework module");
}

static bool compareModuleHeaders(const Module::Header &A,
                                 const Module::Header &B) {
  return A.NameAsWritten < B.NameAsWritten;
}

/// Parse an umbrella directory declaration.
///
///   umbrella-dir-declaration:
///     umbrella string-literal
void ModuleMapParser::parseUmbrellaDirDecl(SourceLocation UmbrellaLoc) {
  // Parse the directory name.
  if (!Tok.is(MMToken::StringLiteral)) {
    Diags.Report(Tok.getLocation(), diag::err_mmap_expected_header)
      << "umbrella";
    HadError = true;
    return;
  }

  std::string DirName = std::string(Tok.getString());
  std::string DirNameAsWritten = DirName;
  SourceLocation DirNameLoc = consumeToken();

  // Check whether we already have an umbrella.
  if (!std::holds_alternative<std::monostate>(ActiveModule->Umbrella)) {
    Diags.Report(DirNameLoc, diag::err_mmap_umbrella_clash)
      << ActiveModule->getFullModuleName();
    HadError = true;
    return;
  }

  // Look for this file.
  OptionalDirectoryEntryRef Dir;
  if (llvm::sys::path::is_absolute(DirName)) {
    Dir = SourceMgr.getFileManager().getOptionalDirectoryRef(DirName);
  } else {
    SmallString<128> PathName;
    PathName = Directory.getName();
    llvm::sys::path::append(PathName, DirName);
    Dir = SourceMgr.getFileManager().getOptionalDirectoryRef(PathName);
  }

  if (!Dir) {
    Diags.Report(DirNameLoc, diag::warn_mmap_umbrella_dir_not_found)
      << DirName;
    return;
  }

  if (UsesRequiresExcludedHack.count(ActiveModule)) {
    // Mark this header 'textual' (see doc comment for
    // ModuleMapParser::UsesRequiresExcludedHack). Although iterating over the
    // directory is relatively expensive, in practice this only applies to the
    // uncommonly used Tcl module on Darwin platforms.
    std::error_code EC;
    SmallVector<Module::Header, 6> Headers;
    llvm::vfs::FileSystem &FS =
        SourceMgr.getFileManager().getVirtualFileSystem();
    for (llvm::vfs::recursive_directory_iterator I(FS, Dir->getName(), EC), E;
         I != E && !EC; I.increment(EC)) {
      if (auto FE = SourceMgr.getFileManager().getOptionalFileRef(I->path())) {
        Module::Header Header = {"", std::string(I->path()), *FE};
        Headers.push_back(std::move(Header));
      }
    }

    // Sort header paths so that the pcm doesn't depend on iteration order.
    std::stable_sort(Headers.begin(), Headers.end(), compareModuleHeaders);

    for (auto &Header : Headers)
      Map.addHeader(ActiveModule, std::move(Header), ModuleMap::TextualHeader);
    return;
  }

  if (Module *OwningModule = Map.UmbrellaDirs[*Dir]) {
    Diags.Report(UmbrellaLoc, diag::err_mmap_umbrella_clash)
      << OwningModule->getFullModuleName();
    HadError = true;
    return;
  }

  // Record this umbrella directory.
  Map.setUmbrellaDirAsWritten(ActiveModule, *Dir, DirNameAsWritten, DirName);
}

/// Parse a module export declaration.
///
///   export-declaration:
///     'export' wildcard-module-id
///
///   wildcard-module-id:
///     identifier
///     '*'
///     identifier '.' wildcard-module-id
void ModuleMapParser::parseExportDecl() {
  assert(Tok.is(MMToken::ExportKeyword));
  SourceLocation ExportLoc = consumeToken();

  // Parse the module-id with an optional wildcard at the end.
  ModuleId ParsedModuleId;
  bool Wildcard = false;
  do {
    // FIXME: Support string-literal module names here.
    if (Tok.is(MMToken::Identifier)) {
      ParsedModuleId.push_back(
          std::make_pair(std::string(Tok.getString()), Tok.getLocation()));
      consumeToken();

      if (Tok.is(MMToken::Period)) {
        consumeToken();
        continue;
      }

      break;
    }

    if(Tok.is(MMToken::Star)) {
      Wildcard = true;
      consumeToken();
      break;
    }

    Diags.Report(Tok.getLocation(), diag::err_mmap_module_id);
    HadError = true;
    return;
  } while (true);

  Module::UnresolvedExportDecl Unresolved = {
    ExportLoc, ParsedModuleId, Wildcard
  };
  ActiveModule->UnresolvedExports.push_back(Unresolved);
}

/// Parse a module export_as declaration.
///
///   export-as-declaration:
///     'export_as' identifier
void ModuleMapParser::parseExportAsDecl() {
  assert(Tok.is(MMToken::ExportAsKeyword));
  consumeToken();

  if (!Tok.is(MMToken::Identifier)) {
    Diags.Report(Tok.getLocation(), diag::err_mmap_module_id);
    HadError = true;
    return;
  }

  if (ActiveModule->Parent) {
    Diags.Report(Tok.getLocation(), diag::err_mmap_submodule_export_as);
    consumeToken();
    return;
  }

  if (!ActiveModule->ExportAsModule.empty()) {
    if (ActiveModule->ExportAsModule == Tok.getString()) {
      Diags.Report(Tok.getLocation(), diag::warn_mmap_redundant_export_as)
        << ActiveModule->Name << Tok.getString();
    } else {
      Diags.Report(Tok.getLocation(), diag::err_mmap_conflicting_export_as)
        << ActiveModule->Name << ActiveModule->ExportAsModule
        << Tok.getString();
    }
  }

  ActiveModule->ExportAsModule = std::string(Tok.getString());
  Map.addLinkAsDependency(ActiveModule);

  consumeToken();
}

/// Parse a module use declaration.
///
///   use-declaration:
///     'use' wildcard-module-id
void ModuleMapParser::parseUseDecl() {
  assert(Tok.is(MMToken::UseKeyword));
  auto KWLoc = consumeToken();
  // Parse the module-id.
  ModuleId ParsedModuleId;
  parseModuleId(ParsedModuleId);

  if (ActiveModule->Parent)
    Diags.Report(KWLoc, diag::err_mmap_use_decl_submodule);
  else
    ActiveModule->UnresolvedDirectUses.push_back(ParsedModuleId);
}

/// Parse a link declaration.
///
///   module-declaration:
///     'link' 'framework'[opt] string-literal
void ModuleMapParser::parseLinkDecl() {
  assert(Tok.is(MMToken::LinkKeyword));
  SourceLocation LinkLoc = consumeToken();

  // Parse the optional 'framework' keyword.
  bool IsFramework = false;
  if (Tok.is(MMToken::FrameworkKeyword)) {
    consumeToken();
    IsFramework = true;
  }

  // Parse the library name
  if (!Tok.is(MMToken::StringLiteral)) {
    Diags.Report(Tok.getLocation(), diag::err_mmap_expected_library_name)
      << IsFramework << SourceRange(LinkLoc);
    HadError = true;
    return;
  }

  std::string LibraryName = std::string(Tok.getString());
  consumeToken();
  ActiveModule->LinkLibraries.push_back(Module::LinkLibrary(LibraryName,
                                                            IsFramework));
}

/// Parse a configuration macro declaration.
///
///   module-declaration:
///     'config_macros' attributes[opt] config-macro-list?
///
///   config-macro-list:
///     identifier (',' identifier)?
void ModuleMapParser::parseConfigMacros() {
  assert(Tok.is(MMToken::ConfigMacros));
  SourceLocation ConfigMacrosLoc = consumeToken();

  // Only top-level modules can have configuration macros.
  if (ActiveModule->Parent) {
    Diags.Report(ConfigMacrosLoc, diag::err_mmap_config_macro_submodule);
  }

  // Parse the optional attributes.
  Attributes Attrs;
  if (parseOptionalAttributes(Attrs))
    return;

  if (Attrs.IsExhaustive && !ActiveModule->Parent) {
    ActiveModule->ConfigMacrosExhaustive = true;
  }

  // If we don't have an identifier, we're done.
  // FIXME: Support macros with the same name as a keyword here.
  if (!Tok.is(MMToken::Identifier))
    return;

  // Consume the first identifier.
  if (!ActiveModule->Parent) {
    ActiveModule->ConfigMacros.push_back(Tok.getString().str());
  }
  consumeToken();

  do {
    // If there's a comma, consume it.
    if (!Tok.is(MMToken::Comma))
      break;
    consumeToken();

    // We expect to see a macro name here.
    // FIXME: Support macros with the same name as a keyword here.
    if (!Tok.is(MMToken::Identifier)) {
      Diags.Report(Tok.getLocation(), diag::err_mmap_expected_config_macro);
      break;
    }

    // Consume the macro name.
    if (!ActiveModule->Parent) {
      ActiveModule->ConfigMacros.push_back(Tok.getString().str());
    }
    consumeToken();
  } while (true);
}

/// Format a module-id into a string.
static std::string formatModuleId(const ModuleId &Id) {
  std::string result;
  {
    llvm::raw_string_ostream OS(result);

    for (unsigned I = 0, N = Id.size(); I != N; ++I) {
      if (I)
        OS << ".";
      OS << Id[I].first;
    }
  }

  return result;
}

/// Parse a conflict declaration.
///
///   module-declaration:
///     'conflict' module-id ',' string-literal
void ModuleMapParser::parseConflict() {
  assert(Tok.is(MMToken::Conflict));
  SourceLocation ConflictLoc = consumeToken();
  Module::UnresolvedConflict Conflict;

  // Parse the module-id.
  if (parseModuleId(Conflict.Id))
    return;

  // Parse the ','.
  if (!Tok.is(MMToken::Comma)) {
    Diags.Report(Tok.getLocation(), diag::err_mmap_expected_conflicts_comma)
      << SourceRange(ConflictLoc);
    return;
  }
  consumeToken();

  // Parse the message.
  if (!Tok.is(MMToken::StringLiteral)) {
    Diags.Report(Tok.getLocation(), diag::err_mmap_expected_conflicts_message)
      << formatModuleId(Conflict.Id);
    return;
  }
  Conflict.Message = Tok.getString().str();
  consumeToken();

  // Add this unresolved conflict.
  ActiveModule->UnresolvedConflicts.push_back(Conflict);
}

/// Parse an inferred module declaration (wildcard modules).
///
///   module-declaration:
///     'explicit'[opt] 'framework'[opt] 'module' * attributes[opt]
///       { inferred-module-member* }
///
///   inferred-module-member:
///     'export' '*'
///     'exclude' identifier
void ModuleMapParser::parseInferredModuleDecl(bool Framework, bool Explicit) {
  assert(Tok.is(MMToken::Star));
  SourceLocation StarLoc = consumeToken();
  bool Failed = false;

  // Inferred modules must be submodules.
  if (!ActiveModule && !Framework) {
    Diags.Report(StarLoc, diag::err_mmap_top_level_inferred_submodule);
    Failed = true;
  }

  if (ActiveModule) {
    // Inferred modules must have umbrella directories.
    if (!Failed && ActiveModule->IsAvailable &&
        !ActiveModule->getEffectiveUmbrellaDir()) {
      Diags.Report(StarLoc, diag::err_mmap_inferred_no_umbrella);
      Failed = true;
    }

    // Check for redefinition of an inferred module.
    if (!Failed && ActiveModule->InferSubmodules) {
      Diags.Report(StarLoc, diag::err_mmap_inferred_redef);
      if (ActiveModule->InferredSubmoduleLoc.isValid())
        Diags.Report(ActiveModule->InferredSubmoduleLoc,
                     diag::note_mmap_prev_definition);
      Failed = true;
    }

    // Check for the 'framework' keyword, which is not permitted here.
    if (Framework) {
      Diags.Report(StarLoc, diag::err_mmap_inferred_framework_submodule);
      Framework = false;
    }
  } else if (Explicit) {
    Diags.Report(StarLoc, diag::err_mmap_explicit_inferred_framework);
    Explicit = false;
  }

  // If there were any problems with this inferred submodule, skip its body.
  if (Failed) {
    if (Tok.is(MMToken::LBrace)) {
      consumeToken();
      skipUntil(MMToken::RBrace);
      if (Tok.is(MMToken::RBrace))
        consumeToken();
    }
    HadError = true;
    return;
  }

  // Parse optional attributes.
  Attributes Attrs;
  if (parseOptionalAttributes(Attrs))
    return;

  if (ActiveModule) {
    // Note that we have an inferred submodule.
    ActiveModule->InferSubmodules = true;
    ActiveModule->InferredSubmoduleLoc = StarLoc;
    ActiveModule->InferExplicitSubmodules = Explicit;
  } else {
    // We'll be inferring framework modules for this directory.
    Map.InferredDirectories[Directory].InferModules = true;
    Map.InferredDirectories[Directory].Attrs = Attrs;
    Map.InferredDirectories[Directory].ModuleMapFID = ModuleMapFID;
    // FIXME: Handle the 'framework' keyword.
  }

  // Parse the opening brace.
  if (!Tok.is(MMToken::LBrace)) {
    Diags.Report(Tok.getLocation(), diag::err_mmap_expected_lbrace_wildcard);
    HadError = true;
    return;
  }
  SourceLocation LBraceLoc = consumeToken();

  // Parse the body of the inferred submodule.
  bool Done = false;
  do {
    switch (Tok.Kind) {
    case MMToken::EndOfFile:
    case MMToken::RBrace:
      Done = true;
      break;

    case MMToken::ExcludeKeyword:
      if (ActiveModule) {
        Diags.Report(Tok.getLocation(), diag::err_mmap_expected_inferred_member)
          << (ActiveModule != nullptr);
        consumeToken();
        break;
      }

      consumeToken();
      // FIXME: Support string-literal module names here.
      if (!Tok.is(MMToken::Identifier)) {
        Diags.Report(Tok.getLocation(), diag::err_mmap_missing_exclude_name);
        break;
      }

      Map.InferredDirectories[Directory].ExcludedModules.push_back(
          std::string(Tok.getString()));
      consumeToken();
      break;

    case MMToken::ExportKeyword:
      if (!ActiveModule) {
        Diags.Report(Tok.getLocation(), diag::err_mmap_expected_inferred_member)
          << (ActiveModule != nullptr);
        consumeToken();
        break;
      }

      consumeToken();
      if (Tok.is(MMToken::Star))
        ActiveModule->InferExportWildcard = true;
      else
        Diags.Report(Tok.getLocation(),
                     diag::err_mmap_expected_export_wildcard);
      consumeToken();
      break;

    case MMToken::ExplicitKeyword:
    case MMToken::ModuleKeyword:
    case MMToken::HeaderKeyword:
    case MMToken::PrivateKeyword:
    case MMToken::UmbrellaKeyword:
    default:
      Diags.Report(Tok.getLocation(), diag::err_mmap_expected_inferred_member)
          << (ActiveModule != nullptr);
      consumeToken();
      break;
    }
  } while (!Done);

  if (Tok.is(MMToken::RBrace))
    consumeToken();
  else {
    Diags.Report(Tok.getLocation(), diag::err_mmap_expected_rbrace);
    Diags.Report(LBraceLoc, diag::note_mmap_lbrace_match);
    HadError = true;
  }
}

/// Parse optional attributes.
///
///   attributes:
///     attribute attributes
///     attribute
///
///   attribute:
///     [ identifier ]
///
/// \param Attrs Will be filled in with the parsed attributes.
///
/// \returns true if an error occurred, false otherwise.
bool ModuleMapParser::parseOptionalAttributes(Attributes &Attrs) {
  bool HadError = false;

  while (Tok.is(MMToken::LSquare)) {
    // Consume the '['.
    SourceLocation LSquareLoc = consumeToken();

    // Check whether we have an attribute name here.
    if (!Tok.is(MMToken::Identifier)) {
      Diags.Report(Tok.getLocation(), diag::err_mmap_expected_attribute);
      skipUntil(MMToken::RSquare);
      if (Tok.is(MMToken::RSquare))
        consumeToken();
      HadError = true;
    }

    // Decode the attribute name.
    AttributeKind Attribute
      = llvm::StringSwitch<AttributeKind>(Tok.getString())
          .Case("exhaustive", AT_exhaustive)
          .Case("extern_c", AT_extern_c)
          .Case("no_undeclared_includes", AT_no_undeclared_includes)
          .Case("system", AT_system)
          .Default(AT_unknown);
    switch (Attribute) {
    case AT_unknown:
      Diags.Report(Tok.getLocation(), diag::warn_mmap_unknown_attribute)
        << Tok.getString();
      break;

    case AT_system:
      Attrs.IsSystem = true;
      break;

    case AT_extern_c:
      Attrs.IsExternC = true;
      break;

    case AT_exhaustive:
      Attrs.IsExhaustive = true;
      break;

    case AT_no_undeclared_includes:
      Attrs.NoUndeclaredIncludes = true;
      break;
    }
    consumeToken();

    // Consume the ']'.
    if (!Tok.is(MMToken::RSquare)) {
      Diags.Report(Tok.getLocation(), diag::err_mmap_expected_rsquare);
      Diags.Report(LSquareLoc, diag::note_mmap_lsquare_match);
      skipUntil(MMToken::RSquare);
      HadError = true;
    }

    if (Tok.is(MMToken::RSquare))
      consumeToken();
  }

  return HadError;
}

/// Parse a module map file.
///
///   module-map-file:
///     module-declaration*
bool ModuleMapParser::parseModuleMapFile() {
  do {
    switch (Tok.Kind) {
    case MMToken::EndOfFile:
      return HadError;

    case MMToken::ExplicitKeyword:
    case MMToken::ExternKeyword:
    case MMToken::ModuleKeyword:
    case MMToken::FrameworkKeyword:
      parseModuleDecl();
      break;

    case MMToken::Comma:
    case MMToken::ConfigMacros:
    case MMToken::Conflict:
    case MMToken::Exclaim:
    case MMToken::ExcludeKeyword:
    case MMToken::ExportKeyword:
    case MMToken::ExportAsKeyword:
    case MMToken::HeaderKeyword:
    case MMToken::Identifier:
    case MMToken::LBrace:
    case MMToken::LinkKeyword:
    case MMToken::LSquare:
    case MMToken::Period:
    case MMToken::PrivateKeyword:
    case MMToken::RBrace:
    case MMToken::RSquare:
    case MMToken::RequiresKeyword:
    case MMToken::Star:
    case MMToken::StringLiteral:
    case MMToken::IntegerLiteral:
    case MMToken::TextualKeyword:
    case MMToken::UmbrellaKeyword:
    case MMToken::UseKeyword:
      Diags.Report(Tok.getLocation(), diag::err_mmap_expected_module);
      HadError = true;
      consumeToken();
      break;
    }
  } while (true);
}

bool ModuleMap::parseModuleMapFile(FileEntryRef File, bool IsSystem,
                                   DirectoryEntryRef Dir, FileID ID,
                                   unsigned *Offset,
                                   SourceLocation ExternModuleLoc) {
  assert(Target && "Missing target information");
  llvm::DenseMap<const FileEntry *, bool>::iterator Known
    = ParsedModuleMap.find(File);
  if (Known != ParsedModuleMap.end())
    return Known->second;

  // If the module map file wasn't already entered, do so now.
  if (ID.isInvalid()) {
    auto FileCharacter =
        IsSystem ? SrcMgr::C_System_ModuleMap : SrcMgr::C_User_ModuleMap;
    ID = SourceMgr.createFileID(File, ExternModuleLoc, FileCharacter);
  }

  assert(Target && "Missing target information");
  std::optional<llvm::MemoryBufferRef> Buffer = SourceMgr.getBufferOrNone(ID);
  if (!Buffer)
    return ParsedModuleMap[File] = true;
  assert((!Offset || *Offset <= Buffer->getBufferSize()) &&
         "invalid buffer offset");

  // Parse this module map file.
  Lexer L(SourceMgr.getLocForStartOfFile(ID), MMapLangOpts,
          Buffer->getBufferStart(),
          Buffer->getBufferStart() + (Offset ? *Offset : 0),
          Buffer->getBufferEnd());
  SourceLocation Start = L.getSourceLocation();
  ModuleMapParser Parser(L, SourceMgr, Target, Diags, *this, ID, Dir, IsSystem);
  bool Result = Parser.parseModuleMapFile();
  ParsedModuleMap[File] = Result;

  if (Offset) {
    auto Loc = SourceMgr.getDecomposedLoc(Parser.getLocation());
    assert(Loc.first == ID && "stopped in a different file?");
    *Offset = Loc.second;
  }

  // Notify callbacks that we parsed it.
  for (const auto &Cb : Callbacks)
    Cb->moduleMapFileRead(Start, File, IsSystem);

  return Result;
}
