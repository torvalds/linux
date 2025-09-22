//===- HeaderSearchOptions.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_HEADERSEARCHOPTIONS_H
#define LLVM_CLANG_LEX_HEADERSEARCHOPTIONS_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/HashBuilder.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace clang {

namespace frontend {

/// IncludeDirGroup - Identifies the group an include Entry belongs to,
/// representing its relative positive in the search list.
/// \#include directives whose paths are enclosed by string quotes ("")
/// start searching at the Quoted group (specified by '-iquote'),
/// then search the Angled group, then the System group, etc.
enum IncludeDirGroup {
  /// '\#include ""' paths, added by 'gcc -iquote'.
  Quoted = 0,

  /// Paths for '\#include <>' added by '-I'.
  Angled,

  /// Like Angled, but marks header maps used when building frameworks.
  IndexHeaderMap,

  /// Like Angled, but marks system directories.
  System,

  /// Like System, but headers are implicitly wrapped in extern "C".
  ExternCSystem,

  /// Like System, but only used for C.
  CSystem,

  /// Like System, but only used for C++.
  CXXSystem,

  /// Like System, but only used for ObjC.
  ObjCSystem,

  /// Like System, but only used for ObjC++.
  ObjCXXSystem,

  /// Like System, but searched after the system directories.
  After
};

} // namespace frontend

/// HeaderSearchOptions - Helper class for storing options related to the
/// initialization of the HeaderSearch object.
class HeaderSearchOptions {
public:
  struct Entry {
    std::string Path;
    frontend::IncludeDirGroup Group;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsFramework : 1;

    /// IgnoreSysRoot - This is false if an absolute path should be treated
    /// relative to the sysroot, or true if it should always be the absolute
    /// path.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IgnoreSysRoot : 1;

    Entry(StringRef path, frontend::IncludeDirGroup group, bool isFramework,
          bool ignoreSysRoot)
        : Path(path), Group(group), IsFramework(isFramework),
          IgnoreSysRoot(ignoreSysRoot) {}
  };

  struct SystemHeaderPrefix {
    /// A prefix to be matched against paths in \#include directives.
    std::string Prefix;

    /// True if paths beginning with this prefix should be treated as system
    /// headers.
    bool IsSystemHeader;

    SystemHeaderPrefix(StringRef Prefix, bool IsSystemHeader)
        : Prefix(Prefix), IsSystemHeader(IsSystemHeader) {}
  };

  /// If non-empty, the directory to use as a "virtual system root" for include
  /// paths.
  std::string Sysroot;

  /// User specified include entries.
  std::vector<Entry> UserEntries;

  /// User-specified system header prefixes.
  std::vector<SystemHeaderPrefix> SystemHeaderPrefixes;

  /// The directory which holds the compiler resource files (builtin includes,
  /// etc.).
  std::string ResourceDir;

  /// The directory used for the module cache.
  std::string ModuleCachePath;

  /// The directory used for a user build.
  std::string ModuleUserBuildPath;

  /// The mapping of module names to prebuilt module files.
  std::map<std::string, std::string, std::less<>> PrebuiltModuleFiles;

  /// The directories used to load prebuilt module files.
  std::vector<std::string> PrebuiltModulePaths;

  /// The module/pch container format.
  std::string ModuleFormat;

  /// Whether we should disable the use of the hash string within the
  /// module cache.
  ///
  /// Note: Only used for testing!
  LLVM_PREFERRED_TYPE(bool)
  unsigned DisableModuleHash : 1;

  /// Implicit module maps.  This option is enabld by default when
  /// modules is enabled.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ImplicitModuleMaps : 1;

  /// Set the 'home directory' of a module map file to the current
  /// working directory (or the home directory of the module map file that
  /// contained the 'extern module' directive importing this module map file
  /// if any) rather than the directory containing the module map file.
  //
  /// The home directory is where we look for files named in the module map
  /// file.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModuleMapFileHomeIsCwd : 1;

  /// Set the base path of a built module file to be the current working
  /// directory. This is useful for sharing module files across machines
  /// that build with different paths without having to rewrite all
  /// modulemap files to have working directory relative paths.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModuleFileHomeIsCwd : 1;

  /// Also search for prebuilt implicit modules in the prebuilt module cache
  /// path.
  LLVM_PREFERRED_TYPE(bool)
  unsigned EnablePrebuiltImplicitModules : 1;

  /// The interval (in seconds) between pruning operations.
  ///
  /// This operation is expensive, because it requires Clang to walk through
  /// the directory structure of the module cache, stat()'ing and removing
  /// files.
  ///
  /// The default value is large, e.g., the operation runs once a week.
  unsigned ModuleCachePruneInterval = 7 * 24 * 60 * 60;

  /// The time (in seconds) after which an unused module file will be
  /// considered unused and will, therefore, be pruned.
  ///
  /// When the module cache is pruned, any module file that has not been
  /// accessed in this many seconds will be removed. The default value is
  /// large, e.g., a month, to avoid forcing infrequently-used modules to be
  /// regenerated often.
  unsigned ModuleCachePruneAfter = 31 * 24 * 60 * 60;

  /// The time in seconds when the build session started.
  ///
  /// This time is used by other optimizations in header search and module
  /// loading.
  uint64_t BuildSessionTimestamp = 0;

  /// The set of macro names that should be ignored for the purposes
  /// of computing the module hash.
  llvm::SmallSetVector<llvm::CachedHashString, 16> ModulesIgnoreMacros;

  /// The set of user-provided virtual filesystem overlay files.
  std::vector<std::string> VFSOverlayFiles;

  /// Include the compiler builtin includes.
  LLVM_PREFERRED_TYPE(bool)
  unsigned UseBuiltinIncludes : 1;

  /// Include the system standard include search directories.
  LLVM_PREFERRED_TYPE(bool)
  unsigned UseStandardSystemIncludes : 1;

  /// Include the system standard C++ library include search directories.
  LLVM_PREFERRED_TYPE(bool)
  unsigned UseStandardCXXIncludes : 1;

  /// Use libc++ instead of the default libstdc++.
  LLVM_PREFERRED_TYPE(bool)
  unsigned UseLibcxx : 1;

  /// Whether header search information should be output as for -v.
  LLVM_PREFERRED_TYPE(bool)
  unsigned Verbose : 1;

  /// If true, skip verifying input files used by modules if the
  /// module was already verified during this build session (see
  /// \c BuildSessionTimestamp).
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModulesValidateOncePerBuildSession : 1;

  /// Whether to validate system input files when a module is loaded.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModulesValidateSystemHeaders : 1;

  // Whether the content of input files should be hashed and used to
  // validate consistency.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ValidateASTInputFilesContent : 1;

  // Whether the input files from C++20 Modules should be checked.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ForceCheckCXX20ModulesInputFiles : 1;

  /// Whether the module includes debug information (-gmodules).
  LLVM_PREFERRED_TYPE(bool)
  unsigned UseDebugInfo : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned ModulesValidateDiagnosticOptions : 1;

  /// Whether to entirely skip writing diagnostic options.
  /// Primarily used to speed up deserialization during dependency scanning.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModulesSkipDiagnosticOptions : 1;

  /// Whether to entirely skip writing header search paths.
  /// Primarily used to speed up deserialization during dependency scanning.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModulesSkipHeaderSearchPaths : 1;

  /// Whether to entirely skip writing pragma diagnostic mappings.
  /// Primarily used to speed up deserialization during dependency scanning.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModulesSkipPragmaDiagnosticMappings : 1;

  /// Whether to prune non-affecting module map files from PCM files.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModulesPruneNonAffectingModuleMaps : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned ModulesHashContent : 1;

  /// Whether we should include all things that could impact the module in the
  /// hash.
  ///
  /// This includes things like the full header search path, and enabled
  /// diagnostics.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModulesStrictContextHash : 1;

  /// Whether to include ivfsoverlay usage information in written AST files.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ModulesIncludeVFSUsage : 1;

  HeaderSearchOptions(StringRef _Sysroot = "/")
      : Sysroot(_Sysroot), ModuleFormat("raw"), DisableModuleHash(false),
        ImplicitModuleMaps(false), ModuleMapFileHomeIsCwd(false),
        ModuleFileHomeIsCwd(false), EnablePrebuiltImplicitModules(false),
        UseBuiltinIncludes(true), UseStandardSystemIncludes(true),
        UseStandardCXXIncludes(true), UseLibcxx(false), Verbose(false),
        ModulesValidateOncePerBuildSession(false),
        ModulesValidateSystemHeaders(false),
        ValidateASTInputFilesContent(false),
        ForceCheckCXX20ModulesInputFiles(false), UseDebugInfo(false),
        ModulesValidateDiagnosticOptions(true),
        ModulesSkipDiagnosticOptions(false),
        ModulesSkipHeaderSearchPaths(false),
        ModulesSkipPragmaDiagnosticMappings(false),
        ModulesPruneNonAffectingModuleMaps(true), ModulesHashContent(false),
        ModulesStrictContextHash(false), ModulesIncludeVFSUsage(false) {}

  /// AddPath - Add the \p Path path to the specified \p Group list.
  void AddPath(StringRef Path, frontend::IncludeDirGroup Group,
               bool IsFramework, bool IgnoreSysRoot) {
    UserEntries.emplace_back(Path, Group, IsFramework, IgnoreSysRoot);
  }

  /// AddSystemHeaderPrefix - Override whether \#include directives naming a
  /// path starting with \p Prefix should be considered as naming a system
  /// header.
  void AddSystemHeaderPrefix(StringRef Prefix, bool IsSystemHeader) {
    SystemHeaderPrefixes.emplace_back(Prefix, IsSystemHeader);
  }

  void AddVFSOverlayFile(StringRef Name) {
    VFSOverlayFiles.push_back(std::string(Name));
  }

  void AddPrebuiltModulePath(StringRef Name) {
    PrebuiltModulePaths.push_back(std::string(Name));
  }
};

template <typename HasherT, llvm::endianness Endianness>
inline void addHash(llvm::HashBuilder<HasherT, Endianness> &HBuilder,
                    const HeaderSearchOptions::Entry &E) {
  HBuilder.add(E.Path, E.Group, E.IsFramework, E.IgnoreSysRoot);
}

template <typename HasherT, llvm::endianness Endianness>
inline void addHash(llvm::HashBuilder<HasherT, Endianness> &HBuilder,
                    const HeaderSearchOptions::SystemHeaderPrefix &SHP) {
  HBuilder.add(SHP.Prefix, SHP.IsSystemHeader);
}

} // namespace clang

#endif // LLVM_CLANG_LEX_HEADERSEARCHOPTIONS_H
