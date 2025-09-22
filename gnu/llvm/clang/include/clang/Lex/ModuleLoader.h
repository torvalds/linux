//===- ModuleLoader.h - Module Loader Interface -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ModuleLoader interface, which is responsible for
//  loading named modules.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_MODULELOADER_H
#define LLVM_CLANG_LEX_MODULELOADER_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/StringRef.h"
#include <utility>

namespace clang {

class GlobalModuleIndex;
class IdentifierInfo;

/// A sequence of identifier/location pairs used to describe a particular
/// module or submodule, e.g., std.vector.
using ModuleIdPath = ArrayRef<std::pair<IdentifierInfo *, SourceLocation>>;

/// Describes the result of attempting to load a module.
class ModuleLoadResult {
public:
  enum LoadResultKind {
    // We either succeeded or failed to load the named module.
    Normal,

    // The module exists, but does not actually contain the named submodule.
    // This should only happen if the named submodule was inferred from an
    // umbrella directory, but not actually part of the umbrella header.
    MissingExpected,

    // The module exists but cannot be imported due to a configuration mismatch.
    ConfigMismatch,
  };
  llvm::PointerIntPair<Module *, 2, LoadResultKind> Storage;

  ModuleLoadResult() = default;
  ModuleLoadResult(Module *M) : Storage(M, Normal) {}
  ModuleLoadResult(LoadResultKind Kind) : Storage(nullptr, Kind) {}
  ModuleLoadResult(Module *M, LoadResultKind Kind) : Storage(M, Kind) {}

  operator bool() const {
    return Storage.getInt() == Normal && Storage.getPointer();
  }

  operator Module *() const { return Storage.getPointer(); }

  /// Determines whether this is a normal return, whether or not loading the
  /// module was successful.
  bool isNormal() const { return Storage.getInt() == Normal; }

  /// Determines whether the module, which failed to load, was
  /// actually a submodule that we expected to see (based on implying the
  /// submodule from header structure), but didn't materialize in the actual
  /// module.
  bool isMissingExpected() const { return Storage.getInt() == MissingExpected; }

  /// Determines whether the module failed to load due to a configuration
  /// mismatch with an explicitly-named .pcm file from the command line.
  bool isConfigMismatch() const { return Storage.getInt() == ConfigMismatch; }
};

/// Abstract interface for a module loader.
///
/// This abstract interface describes a module loader, which is responsible
/// for resolving a module name (e.g., "std") to an actual module file, and
/// then loading that module.
class ModuleLoader {
  // Building a module if true.
  bool BuildingModule;

public:
  explicit ModuleLoader(bool BuildingModule = false)
      : BuildingModule(BuildingModule) {}

  virtual ~ModuleLoader();

  /// Returns true if this instance is building a module.
  bool buildingModule() const {
    return BuildingModule;
  }

  /// Flag indicating whether this instance is building a module.
  void setBuildingModule(bool BuildingModuleFlag) {
    BuildingModule = BuildingModuleFlag;
  }

  /// Attempt to load the given module.
  ///
  /// This routine attempts to load the module described by the given
  /// parameters.  If there is a module cache, this may implicitly compile the
  /// module before loading it.
  ///
  /// \param ImportLoc The location of the 'import' keyword.
  ///
  /// \param Path The identifiers (and their locations) of the module
  /// "path", e.g., "std.vector" would be split into "std" and "vector".
  ///
  /// \param Visibility The visibility provided for the names in the loaded
  /// module.
  ///
  /// \param IsInclusionDirective Indicates that this module is being loaded
  /// implicitly, due to the presence of an inclusion directive. Otherwise,
  /// it is being loaded due to an import declaration.
  ///
  /// \returns If successful, returns the loaded module. Otherwise, returns
  /// NULL to indicate that the module could not be loaded.
  virtual ModuleLoadResult loadModule(SourceLocation ImportLoc,
                                      ModuleIdPath Path,
                                      Module::NameVisibilityKind Visibility,
                                      bool IsInclusionDirective) = 0;

  /// Attempt to create the given module from the specified source buffer.
  /// Does not load the module or make any submodule visible; for that, use
  /// loadModule and makeModuleVisible.
  ///
  /// \param Loc The location at which to create the module.
  /// \param ModuleName The name of the module to create.
  /// \param Source The source of the module: a (preprocessed) module map.
  virtual void createModuleFromSource(SourceLocation Loc, StringRef ModuleName,
                                      StringRef Source) = 0;

  /// Make the given module visible.
  virtual void makeModuleVisible(Module *Mod,
                                 Module::NameVisibilityKind Visibility,
                                 SourceLocation ImportLoc) = 0;

  /// Load, create, or return global module.
  /// This function returns an existing global module index, if one
  /// had already been loaded or created, or loads one if it
  /// exists, or creates one if it doesn't exist.
  /// Also, importantly, if the index doesn't cover all the modules
  /// in the module map, it will be update to do so here, because
  /// of its use in searching for needed module imports and
  /// associated fixit messages.
  /// \param TriggerLoc The location for what triggered the load.
  /// \returns Returns null if load failed.
  virtual GlobalModuleIndex *loadGlobalModuleIndex(
                                                SourceLocation TriggerLoc) = 0;

  /// Check global module index for missing imports.
  /// \param Name The symbol name to look for.
  /// \param TriggerLoc The location for what triggered the load.
  /// \returns Returns true if any modules with that symbol found.
  virtual bool lookupMissingImports(StringRef Name,
                                    SourceLocation TriggerLoc) = 0;

  bool HadFatalFailure = false;
};

/// A module loader that doesn't know how to create or load modules.
class TrivialModuleLoader : public ModuleLoader {
public:
  ModuleLoadResult loadModule(SourceLocation ImportLoc, ModuleIdPath Path,
                              Module::NameVisibilityKind Visibility,
                              bool IsInclusionDirective) override {
    return {};
  }

  void createModuleFromSource(SourceLocation ImportLoc, StringRef ModuleName,
                              StringRef Source) override {}

  void makeModuleVisible(Module *Mod, Module::NameVisibilityKind Visibility,
                         SourceLocation ImportLoc) override {}

  GlobalModuleIndex *loadGlobalModuleIndex(SourceLocation TriggerLoc) override {
    return nullptr;
  }

  bool lookupMissingImports(StringRef Name,
                            SourceLocation TriggerLoc) override {
    return false;
  }
};

} // namespace clang

#endif // LLVM_CLANG_LEX_MODULELOADER_H
