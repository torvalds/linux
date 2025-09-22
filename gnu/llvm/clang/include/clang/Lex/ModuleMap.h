//===- ModuleMap.h - Describe the layout of modules -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ModuleMap interface, which describes the layout of a
// module as it relates to headers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_MODULEMAP_H
#define LLVM_CLANG_LEX_MODULEMAP_H

#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/Twine.h"
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace clang {

class DiagnosticsEngine;
class DirectoryEntry;
class FileEntry;
class FileManager;
class HeaderSearch;
class SourceManager;

/// A mechanism to observe the actions of the module map parser as it
/// reads module map files.
class ModuleMapCallbacks {
  virtual void anchor();

public:
  virtual ~ModuleMapCallbacks() = default;

  /// Called when a module map file has been read.
  ///
  /// \param FileStart A SourceLocation referring to the start of the file's
  /// contents.
  /// \param File The file itself.
  /// \param IsSystem Whether this is a module map from a system include path.
  virtual void moduleMapFileRead(SourceLocation FileStart, FileEntryRef File,
                                 bool IsSystem) {}

  /// Called when a header is added during module map parsing.
  ///
  /// \param Filename The header file itself.
  virtual void moduleMapAddHeader(StringRef Filename) {}

  /// Called when an umbrella header is added during module map parsing.
  ///
  /// \param Header The umbrella header to collect.
  virtual void moduleMapAddUmbrellaHeader(FileEntryRef Header) {}
};

class ModuleMap {
  SourceManager &SourceMgr;
  DiagnosticsEngine &Diags;
  const LangOptions &LangOpts;
  const TargetInfo *Target;
  HeaderSearch &HeaderInfo;

  llvm::SmallVector<std::unique_ptr<ModuleMapCallbacks>, 1> Callbacks;

  /// The directory used for Clang-supplied, builtin include headers,
  /// such as "stdint.h".
  OptionalDirectoryEntryRef BuiltinIncludeDir;

  /// Language options used to parse the module map itself.
  ///
  /// These are always simple C language options.
  LangOptions MMapLangOpts;

  /// The module that the main source file is associated with (the module
  /// named LangOpts::CurrentModule, if we've loaded it).
  Module *SourceModule = nullptr;

  /// Submodules of the current module that have not yet been attached to it.
  /// (Ownership is transferred if/when we create an enclosing module.)
  llvm::SmallVector<std::unique_ptr<Module>, 8> PendingSubmodules;

  /// The top-level modules that are known.
  llvm::StringMap<Module *> Modules;

  /// Module loading cache that includes submodules, indexed by IdentifierInfo.
  /// nullptr is stored for modules that are known to fail to load.
  llvm::DenseMap<const IdentifierInfo *, Module *> CachedModuleLoads;

  /// Shadow modules created while building this module map.
  llvm::SmallVector<Module*, 2> ShadowModules;

  /// The number of modules we have created in total.
  unsigned NumCreatedModules = 0;

  /// In case a module has a export_as entry, it might have a pending link
  /// name to be determined if that module is imported.
  llvm::StringMap<llvm::StringSet<>> PendingLinkAsModule;

public:
  /// Use PendingLinkAsModule information to mark top level link names that
  /// are going to be replaced by export_as aliases.
  void resolveLinkAsDependencies(Module *Mod);

  /// Make module to use export_as as the link dependency name if enough
  /// information is available or add it to a pending list otherwise.
  void addLinkAsDependency(Module *Mod);

  /// Flags describing the role of a module header.
  enum ModuleHeaderRole {
    /// This header is normally included in the module.
    NormalHeader  = 0x0,

    /// This header is included but private.
    PrivateHeader = 0x1,

    /// This header is part of the module (for layering purposes) but
    /// should be textually included.
    TextualHeader = 0x2,

    /// This header is explicitly excluded from the module.
    ExcludedHeader = 0x4,

    // Caution: Adding an enumerator needs other changes.
    // Adjust the number of bits for KnownHeader::Storage.
    // Adjust the HeaderFileInfoTrait::ReadData streaming.
    // Adjust the HeaderFileInfoTrait::EmitData streaming.
    // Adjust ModuleMap::addHeader.
  };

  /// Convert a header kind to a role. Requires Kind to not be HK_Excluded.
  static ModuleHeaderRole headerKindToRole(Module::HeaderKind Kind);

  /// Convert a header role to a kind.
  static Module::HeaderKind headerRoleToKind(ModuleHeaderRole Role);

  /// Check if the header with the given role is a modular one.
  static bool isModular(ModuleHeaderRole Role);

  /// A header that is known to reside within a given module,
  /// whether it was included or excluded.
  class KnownHeader {
    llvm::PointerIntPair<Module *, 3, ModuleHeaderRole> Storage;

  public:
    KnownHeader() : Storage(nullptr, NormalHeader) {}
    KnownHeader(Module *M, ModuleHeaderRole Role) : Storage(M, Role) {}

    friend bool operator==(const KnownHeader &A, const KnownHeader &B) {
      return A.Storage == B.Storage;
    }
    friend bool operator!=(const KnownHeader &A, const KnownHeader &B) {
      return A.Storage != B.Storage;
    }

    /// Retrieve the module the header is stored in.
    Module *getModule() const { return Storage.getPointer(); }

    /// The role of this header within the module.
    ModuleHeaderRole getRole() const { return Storage.getInt(); }

    /// Whether this header is available in the module.
    bool isAvailable() const {
      return getRole() != ExcludedHeader && getModule()->isAvailable();
    }

    /// Whether this header is accessible from the specified module.
    bool isAccessibleFrom(Module *M) const {
      return !(getRole() & PrivateHeader) ||
             (M && M->getTopLevelModule() == getModule()->getTopLevelModule());
    }

    // Whether this known header is valid (i.e., it has an
    // associated module).
    explicit operator bool() const {
      return Storage.getPointer() != nullptr;
    }
  };

  using AdditionalModMapsSet = llvm::DenseSet<FileEntryRef>;

private:
  friend class ModuleMapParser;

  using HeadersMap = llvm::DenseMap<FileEntryRef, SmallVector<KnownHeader, 1>>;

  /// Mapping from each header to the module that owns the contents of
  /// that header.
  HeadersMap Headers;

  /// Map from file sizes to modules with lazy header directives of that size.
  mutable llvm::DenseMap<off_t, llvm::TinyPtrVector<Module*>> LazyHeadersBySize;

  /// Map from mtimes to modules with lazy header directives with those mtimes.
  mutable llvm::DenseMap<time_t, llvm::TinyPtrVector<Module*>>
              LazyHeadersByModTime;

  /// Mapping from directories with umbrella headers to the module
  /// that is generated from the umbrella header.
  ///
  /// This mapping is used to map headers that haven't explicitly been named
  /// in the module map over to the module that includes them via its umbrella
  /// header.
  llvm::DenseMap<const DirectoryEntry *, Module *> UmbrellaDirs;

  /// A generation counter that is used to test whether modules of the
  /// same name may shadow or are illegal redefinitions.
  ///
  /// Modules from earlier scopes may shadow modules from later ones.
  /// Modules from the same scope may not have the same name.
  unsigned CurrentModuleScopeID = 0;

  llvm::DenseMap<Module *, unsigned> ModuleScopeIDs;

  /// The set of attributes that can be attached to a module.
  struct Attributes {
    /// Whether this is a system module.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsSystem : 1;

    /// Whether this is an extern "C" module.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsExternC : 1;

    /// Whether this is an exhaustive set of configuration macros.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsExhaustive : 1;

    /// Whether files in this module can only include non-modular headers
    /// and headers from used modules.
    LLVM_PREFERRED_TYPE(bool)
    unsigned NoUndeclaredIncludes : 1;

    Attributes()
        : IsSystem(false), IsExternC(false), IsExhaustive(false),
          NoUndeclaredIncludes(false) {}
  };

  /// A directory for which framework modules can be inferred.
  struct InferredDirectory {
    /// Whether to infer modules from this directory.
    LLVM_PREFERRED_TYPE(bool)
    unsigned InferModules : 1;

    /// The attributes to use for inferred modules.
    Attributes Attrs;

    /// If \c InferModules is non-zero, the module map file that allowed
    /// inferred modules.  Otherwise, invalid.
    FileID ModuleMapFID;

    /// The names of modules that cannot be inferred within this
    /// directory.
    SmallVector<std::string, 2> ExcludedModules;

    InferredDirectory() : InferModules(false) {}
  };

  /// A mapping from directories to information about inferring
  /// framework modules from within those directories.
  llvm::DenseMap<const DirectoryEntry *, InferredDirectory> InferredDirectories;

  /// A mapping from an inferred module to the module map that allowed the
  /// inference.
  llvm::DenseMap<const Module *, FileID> InferredModuleAllowedBy;

  llvm::DenseMap<const Module *, AdditionalModMapsSet> AdditionalModMaps;

  /// Describes whether we haved parsed a particular file as a module
  /// map.
  llvm::DenseMap<const FileEntry *, bool> ParsedModuleMap;

  /// Resolve the given export declaration into an actual export
  /// declaration.
  ///
  /// \param Mod The module in which we're resolving the export declaration.
  ///
  /// \param Unresolved The export declaration to resolve.
  ///
  /// \param Complain Whether this routine should complain about unresolvable
  /// exports.
  ///
  /// \returns The resolved export declaration, which will have a NULL pointer
  /// if the export could not be resolved.
  Module::ExportDecl
  resolveExport(Module *Mod, const Module::UnresolvedExportDecl &Unresolved,
                bool Complain) const;

  /// Resolve the given module id to an actual module.
  ///
  /// \param Id The module-id to resolve.
  ///
  /// \param Mod The module in which we're resolving the module-id.
  ///
  /// \param Complain Whether this routine should complain about unresolvable
  /// module-ids.
  ///
  /// \returns The resolved module, or null if the module-id could not be
  /// resolved.
  Module *resolveModuleId(const ModuleId &Id, Module *Mod, bool Complain) const;

  /// Add an unresolved header to a module.
  ///
  /// \param Mod The module in which we're adding the unresolved header
  ///        directive.
  /// \param Header The unresolved header directive.
  /// \param NeedsFramework If Mod is not a framework but a missing header would
  ///        be found in case Mod was, set it to true. False otherwise.
  void addUnresolvedHeader(Module *Mod,
                           Module::UnresolvedHeaderDirective Header,
                           bool &NeedsFramework);

  /// Look up the given header directive to find an actual header file.
  ///
  /// \param M The module in which we're resolving the header directive.
  /// \param Header The header directive to resolve.
  /// \param RelativePathName Filled in with the relative path name from the
  ///        module to the resolved header.
  /// \param NeedsFramework If M is not a framework but a missing header would
  ///        be found in case M was, set it to true. False otherwise.
  /// \return The resolved file, if any.
  OptionalFileEntryRef
  findHeader(Module *M, const Module::UnresolvedHeaderDirective &Header,
             SmallVectorImpl<char> &RelativePathName, bool &NeedsFramework);

  /// Resolve the given header directive.
  ///
  /// \param M The module in which we're resolving the header directive.
  /// \param Header The header directive to resolve.
  /// \param NeedsFramework If M is not a framework but a missing header would
  ///        be found in case M was, set it to true. False otherwise.
  void resolveHeader(Module *M, const Module::UnresolvedHeaderDirective &Header,
                     bool &NeedsFramework);

  /// Attempt to resolve the specified header directive as naming a builtin
  /// header.
  /// \return \c true if a corresponding builtin header was found.
  bool resolveAsBuiltinHeader(Module *M,
                              const Module::UnresolvedHeaderDirective &Header);

  /// Looks up the modules that \p File corresponds to.
  ///
  /// If \p File represents a builtin header within Clang's builtin include
  /// directory, this also loads all of the module maps to see if it will get
  /// associated with a specific module (e.g. in /usr/include).
  HeadersMap::iterator findKnownHeader(FileEntryRef File);

  /// Searches for a module whose umbrella directory contains \p File.
  ///
  /// \param File The header to search for.
  ///
  /// \param IntermediateDirs On success, contains the set of directories
  /// searched before finding \p File.
  KnownHeader findHeaderInUmbrellaDirs(
      FileEntryRef File, SmallVectorImpl<DirectoryEntryRef> &IntermediateDirs);

  /// Given that \p File is not in the Headers map, look it up within
  /// umbrella directories and find or create a module for it.
  KnownHeader findOrCreateModuleForHeaderInUmbrellaDir(FileEntryRef File);

  /// A convenience method to determine if \p File is (possibly nested)
  /// in an umbrella directory.
  bool isHeaderInUmbrellaDirs(FileEntryRef File) {
    SmallVector<DirectoryEntryRef, 2> IntermediateDirs;
    return static_cast<bool>(findHeaderInUmbrellaDirs(File, IntermediateDirs));
  }

  Module *inferFrameworkModule(DirectoryEntryRef FrameworkDir, Attributes Attrs,
                               Module *Parent);

public:
  /// Construct a new module map.
  ///
  /// \param SourceMgr The source manager used to find module files and headers.
  /// This source manager should be shared with the header-search mechanism,
  /// since they will refer to the same headers.
  ///
  /// \param Diags A diagnostic engine used for diagnostics.
  ///
  /// \param LangOpts Language options for this translation unit.
  ///
  /// \param Target The target for this translation unit.
  ModuleMap(SourceManager &SourceMgr, DiagnosticsEngine &Diags,
            const LangOptions &LangOpts, const TargetInfo *Target,
            HeaderSearch &HeaderInfo);

  /// Destroy the module map.
  ~ModuleMap();

  /// Set the target information.
  void setTarget(const TargetInfo &Target);

  /// Set the directory that contains Clang-supplied include files, such as our
  /// stdarg.h or tgmath.h.
  void setBuiltinIncludeDir(DirectoryEntryRef Dir) { BuiltinIncludeDir = Dir; }

  /// Get the directory that contains Clang-supplied include files.
  OptionalDirectoryEntryRef getBuiltinDir() const { return BuiltinIncludeDir; }

  /// Is this a compiler builtin header?
  bool isBuiltinHeader(FileEntryRef File);

  bool shouldImportRelativeToBuiltinIncludeDir(StringRef FileName,
                                               Module *Module) const;

  /// Add a module map callback.
  void addModuleMapCallbacks(std::unique_ptr<ModuleMapCallbacks> Callback) {
    Callbacks.push_back(std::move(Callback));
  }

  /// Retrieve the module that owns the given header file, if any. Note that
  /// this does not implicitly load module maps, except for builtin headers,
  /// and does not consult the external source. (Those checks are the
  /// responsibility of \ref HeaderSearch.)
  ///
  /// \param File The header file that is likely to be included.
  ///
  /// \param AllowTextual If \c true and \p File is a textual header, return
  /// its owning module. Otherwise, no KnownHeader will be returned if the
  /// file is only known as a textual header.
  ///
  /// \returns The module KnownHeader, which provides the module that owns the
  /// given header file.  The KnownHeader is default constructed to indicate
  /// that no module owns this header file.
  KnownHeader findModuleForHeader(FileEntryRef File, bool AllowTextual = false,
                                  bool AllowExcluded = false);

  /// Retrieve all the modules that contain the given header file. Note that
  /// this does not implicitly load module maps, except for builtin headers,
  /// and does not consult the external source. (Those checks are the
  /// responsibility of \ref HeaderSearch.)
  ///
  /// Typically, \ref findModuleForHeader should be used instead, as it picks
  /// the preferred module for the header.
  ArrayRef<KnownHeader> findAllModulesForHeader(FileEntryRef File);

  /// Like \ref findAllModulesForHeader, but do not attempt to infer module
  /// ownership from umbrella headers if we've not already done so.
  ArrayRef<KnownHeader> findResolvedModulesForHeader(FileEntryRef File) const;

  /// Resolve all lazy header directives for the specified file.
  ///
  /// This ensures that the HeaderFileInfo on HeaderSearch is up to date. This
  /// is effectively internal, but is exposed so HeaderSearch can call it.
  void resolveHeaderDirectives(const FileEntry *File) const;

  /// Resolve lazy header directives for the specified module. If File is
  /// provided, only headers with same size and modtime are resolved. If File
  /// is not set, all headers are resolved.
  void resolveHeaderDirectives(Module *Mod,
                               std::optional<const FileEntry *> File) const;

  /// Reports errors if a module must not include a specific file.
  ///
  /// \param RequestingModule The module including a file.
  ///
  /// \param RequestingModuleIsModuleInterface \c true if the inclusion is in
  ///        the interface of RequestingModule, \c false if it's in the
  ///        implementation of RequestingModule. Value is ignored and
  ///        meaningless if RequestingModule is nullptr.
  ///
  /// \param FilenameLoc The location of the inclusion's filename.
  ///
  /// \param Filename The included filename as written.
  ///
  /// \param File The included file.
  void diagnoseHeaderInclusion(Module *RequestingModule,
                               bool RequestingModuleIsModuleInterface,
                               SourceLocation FilenameLoc, StringRef Filename,
                               FileEntryRef File);

  /// Determine whether the given header is part of a module
  /// marked 'unavailable'.
  bool isHeaderInUnavailableModule(FileEntryRef Header) const;

  /// Determine whether the given header is unavailable as part
  /// of the specified module.
  bool isHeaderUnavailableInModule(FileEntryRef Header,
                                   const Module *RequestingModule) const;

  /// Retrieve a module with the given name.
  ///
  /// \param Name The name of the module to look up.
  ///
  /// \returns The named module, if known; otherwise, returns null.
  Module *findModule(StringRef Name) const;

  /// Retrieve a module with the given name using lexical name lookup,
  /// starting at the given context.
  ///
  /// \param Name The name of the module to look up.
  ///
  /// \param Context The module context, from which we will perform lexical
  /// name lookup.
  ///
  /// \returns The named module, if known; otherwise, returns null.
  Module *lookupModuleUnqualified(StringRef Name, Module *Context) const;

  /// Retrieve a module with the given name within the given context,
  /// using direct (qualified) name lookup.
  ///
  /// \param Name The name of the module to look up.
  ///
  /// \param Context The module for which we will look for a submodule. If
  /// null, we will look for a top-level module.
  ///
  /// \returns The named submodule, if known; otherwose, returns null.
  Module *lookupModuleQualified(StringRef Name, Module *Context) const;

  /// Find a new module or submodule, or create it if it does not already
  /// exist.
  ///
  /// \param Name The name of the module to find or create.
  ///
  /// \param Parent The module that will act as the parent of this submodule,
  /// or nullptr to indicate that this is a top-level module.
  ///
  /// \param IsFramework Whether this is a framework module.
  ///
  /// \param IsExplicit Whether this is an explicit submodule.
  ///
  /// \returns The found or newly-created module, along with a boolean value
  /// that will be true if the module is newly-created.
  std::pair<Module *, bool> findOrCreateModule(StringRef Name, Module *Parent,
                                               bool IsFramework,
                                               bool IsExplicit);

  /// Create a global module fragment for a C++ module unit.
  ///
  /// We model the global module fragment as a submodule of the module
  /// interface unit. Unfortunately, we can't create the module interface
  /// unit's Module until later, because we don't know what it will be called
  /// usually. See C++20 [module.unit]/7.2 for the case we could know its
  /// parent.
  Module *createGlobalModuleFragmentForModuleUnit(SourceLocation Loc,
                                                  Module *Parent = nullptr);
  Module *createImplicitGlobalModuleFragmentForModuleUnit(SourceLocation Loc,
                                                          Module *Parent);

  /// Create a global module fragment for a C++ module interface unit.
  Module *createPrivateModuleFragmentForInterfaceUnit(Module *Parent,
                                                      SourceLocation Loc);

  /// Create a new C++ module with the specified kind, and reparent any pending
  /// global module fragment(s) to it.
  Module *createModuleUnitWithKind(SourceLocation Loc, StringRef Name,
                                   Module::ModuleKind Kind);

  /// Create a new module for a C++ module interface unit.
  /// The module must not already exist, and will be configured for the current
  /// compilation.
  ///
  /// Note that this also sets the current module to the newly-created module.
  ///
  /// \returns The newly-created module.
  Module *createModuleForInterfaceUnit(SourceLocation Loc, StringRef Name);

  /// Create a new module for a C++ module implementation unit.
  /// The interface module for this implementation (implicitly imported) must
  /// exist and be loaded and present in the modules map.
  ///
  /// \returns The newly-created module.
  Module *createModuleForImplementationUnit(SourceLocation Loc, StringRef Name);

  /// Create a C++20 header unit.
  Module *createHeaderUnit(SourceLocation Loc, StringRef Name,
                           Module::Header H);

  /// Infer the contents of a framework module map from the given
  /// framework directory.
  Module *inferFrameworkModule(DirectoryEntryRef FrameworkDir, bool IsSystem,
                               Module *Parent);

  /// Create a new top-level module that is shadowed by
  /// \p ShadowingModule.
  Module *createShadowedModule(StringRef Name, bool IsFramework,
                               Module *ShadowingModule);

  /// Creates a new declaration scope for module names, allowing
  /// previously defined modules to shadow definitions from the new scope.
  ///
  /// \note Module names from earlier scopes will shadow names from the new
  /// scope, which is the opposite of how shadowing works for variables.
  void finishModuleDeclarationScope() { CurrentModuleScopeID += 1; }

  bool mayShadowNewModule(Module *ExistingModule) {
    assert(!ExistingModule->Parent && "expected top-level module");
    assert(ModuleScopeIDs.count(ExistingModule) && "unknown module");
    return ModuleScopeIDs[ExistingModule] < CurrentModuleScopeID;
  }

  /// Check whether a framework module can be inferred in the given directory.
  bool canInferFrameworkModule(const DirectoryEntry *Dir) const {
    auto It = InferredDirectories.find(Dir);
    return It != InferredDirectories.end() && It->getSecond().InferModules;
  }

  /// Retrieve the module map file containing the definition of the given
  /// module.
  ///
  /// \param Module The module whose module map file will be returned, if known.
  ///
  /// \returns The FileID for the module map file containing the given module,
  /// invalid if the module definition was inferred.
  FileID getContainingModuleMapFileID(const Module *Module) const;
  OptionalFileEntryRef getContainingModuleMapFile(const Module *Module) const;

  /// Get the module map file that (along with the module name) uniquely
  /// identifies this module.
  ///
  /// The particular module that \c Name refers to may depend on how the module
  /// was found in header search. However, the combination of \c Name and
  /// this module map will be globally unique for top-level modules. In the case
  /// of inferred modules, returns the module map that allowed the inference
  /// (e.g. contained 'module *'). Otherwise, returns
  /// getContainingModuleMapFile().
  FileID getModuleMapFileIDForUniquing(const Module *M) const;
  OptionalFileEntryRef getModuleMapFileForUniquing(const Module *M) const;

  void setInferredModuleAllowedBy(Module *M, FileID ModMapFID);

  /// Canonicalize \p Path in a manner suitable for a module map file. In
  /// particular, this canonicalizes the parent directory separately from the
  /// filename so that it does not affect header resolution relative to the
  /// modulemap.
  ///
  /// \returns an error code if any filesystem operations failed. In this case
  /// \p Path is not modified.
  std::error_code canonicalizeModuleMapPath(SmallVectorImpl<char> &Path);

  /// Get any module map files other than getModuleMapFileForUniquing(M)
  /// that define submodules of a top-level module \p M. This is cheaper than
  /// getting the module map file for each submodule individually, since the
  /// expected number of results is very small.
  AdditionalModMapsSet *getAdditionalModuleMapFiles(const Module *M) {
    auto I = AdditionalModMaps.find(M);
    if (I == AdditionalModMaps.end())
      return nullptr;
    return &I->second;
  }

  void addAdditionalModuleMapFile(const Module *M, FileEntryRef ModuleMap);

  /// Resolve all of the unresolved exports in the given module.
  ///
  /// \param Mod The module whose exports should be resolved.
  ///
  /// \param Complain Whether to emit diagnostics for failures.
  ///
  /// \returns true if any errors were encountered while resolving exports,
  /// false otherwise.
  bool resolveExports(Module *Mod, bool Complain);

  /// Resolve all of the unresolved uses in the given module.
  ///
  /// \param Mod The module whose uses should be resolved.
  ///
  /// \param Complain Whether to emit diagnostics for failures.
  ///
  /// \returns true if any errors were encountered while resolving uses,
  /// false otherwise.
  bool resolveUses(Module *Mod, bool Complain);

  /// Resolve all of the unresolved conflicts in the given module.
  ///
  /// \param Mod The module whose conflicts should be resolved.
  ///
  /// \param Complain Whether to emit diagnostics for failures.
  ///
  /// \returns true if any errors were encountered while resolving conflicts,
  /// false otherwise.
  bool resolveConflicts(Module *Mod, bool Complain);

  /// Sets the umbrella header of the given module to the given header.
  void
  setUmbrellaHeaderAsWritten(Module *Mod, FileEntryRef UmbrellaHeader,
                             const Twine &NameAsWritten,
                             const Twine &PathRelativeToRootModuleDirectory);

  /// Sets the umbrella directory of the given module to the given directory.
  void setUmbrellaDirAsWritten(Module *Mod, DirectoryEntryRef UmbrellaDir,
                               const Twine &NameAsWritten,
                               const Twine &PathRelativeToRootModuleDirectory);

  /// Adds this header to the given module.
  /// \param Role The role of the header wrt the module.
  void addHeader(Module *Mod, Module::Header Header,
                 ModuleHeaderRole Role, bool Imported = false);

  /// Parse the given module map file, and record any modules we
  /// encounter.
  ///
  /// \param File The file to be parsed.
  ///
  /// \param IsSystem Whether this module map file is in a system header
  /// directory, and therefore should be considered a system module.
  ///
  /// \param HomeDir The directory in which relative paths within this module
  ///        map file will be resolved.
  ///
  /// \param ID The FileID of the file to process, if we've already entered it.
  ///
  /// \param Offset [inout] On input the offset at which to start parsing. On
  ///        output, the offset at which the module map terminated.
  ///
  /// \param ExternModuleLoc The location of the "extern module" declaration
  ///        that caused us to load this module map file, if any.
  ///
  /// \returns true if an error occurred, false otherwise.
  bool parseModuleMapFile(FileEntryRef File, bool IsSystem,
                          DirectoryEntryRef HomeDir, FileID ID = FileID(),
                          unsigned *Offset = nullptr,
                          SourceLocation ExternModuleLoc = SourceLocation());

  /// Dump the contents of the module map, for debugging purposes.
  void dump();

  using module_iterator = llvm::StringMap<Module *>::const_iterator;

  module_iterator module_begin() const { return Modules.begin(); }
  module_iterator module_end()   const { return Modules.end(); }
  llvm::iterator_range<module_iterator> modules() const {
    return {module_begin(), module_end()};
  }

  /// Cache a module load.  M might be nullptr.
  void cacheModuleLoad(const IdentifierInfo &II, Module *M) {
    CachedModuleLoads[&II] = M;
  }

  /// Return a cached module load.
  std::optional<Module *> getCachedModuleLoad(const IdentifierInfo &II) {
    auto I = CachedModuleLoads.find(&II);
    if (I == CachedModuleLoads.end())
      return std::nullopt;
    return I->second;
  }
};

} // namespace clang

#endif // LLVM_CLANG_LEX_MODULEMAP_H
