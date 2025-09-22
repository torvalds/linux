//===- ModuleDepCollector.h - Callbacks to collect deps ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_MODULEDEPCOLLECTOR_H
#define LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_MODULEDEPCOLLECTOR_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Tooling/DependencyScanning/DependencyScanningService.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace clang {
namespace tooling {
namespace dependencies {

class DependencyActionController;
class DependencyConsumer;

/// Modular dependency that has already been built prior to the dependency scan.
struct PrebuiltModuleDep {
  std::string ModuleName;
  std::string PCMFile;
  std::string ModuleMapFile;

  explicit PrebuiltModuleDep(const Module *M)
      : ModuleName(M->getTopLevelModuleName()),
        PCMFile(M->getASTFile()->getName()),
        ModuleMapFile(M->PresumedModuleMapFile) {}
};

/// This is used to identify a specific module.
struct ModuleID {
  /// The name of the module. This may include `:` for C++20 module partitions,
  /// or a header-name for C++20 header units.
  std::string ModuleName;

  /// The context hash of a module represents the compiler options that affect
  /// the resulting command-line invocation.
  ///
  /// Modules with the same name and ContextHash but different invocations could
  /// cause non-deterministic build results.
  ///
  /// Modules with the same name but a different \c ContextHash should be
  /// treated as separate modules for the purpose of a build.
  std::string ContextHash;

  bool operator==(const ModuleID &Other) const {
    return std::tie(ModuleName, ContextHash) ==
           std::tie(Other.ModuleName, Other.ContextHash);
  }

  bool operator<(const ModuleID& Other) const {
    return std::tie(ModuleName, ContextHash) <
           std::tie(Other.ModuleName, Other.ContextHash);
  }
};

/// P1689ModuleInfo - Represents the needed information of standard C++20
/// modules for P1689 format.
struct P1689ModuleInfo {
  /// The name of the module. This may include `:` for partitions.
  std::string ModuleName;

  /// Optional. The source path to the module.
  std::string SourcePath;

  /// If this module is a standard c++ interface unit.
  bool IsStdCXXModuleInterface = true;

  enum class ModuleType {
    NamedCXXModule
    // To be supported
    // AngleHeaderUnit,
    // QuoteHeaderUnit
  };
  ModuleType Type = ModuleType::NamedCXXModule;
};

/// An output from a module compilation, such as the path of the module file.
enum class ModuleOutputKind {
  /// The module file (.pcm). Required.
  ModuleFile,
  /// The path of the dependency file (.d), if any.
  DependencyFile,
  /// The null-separated list of names to use as the targets in the dependency
  /// file, if any. Defaults to the value of \c ModuleFile, as in the driver.
  DependencyTargets,
  /// The path of the serialized diagnostic file (.dia), if any.
  DiagnosticSerializationFile,
};

struct ModuleDeps {
  /// The identifier of the module.
  ModuleID ID;

  /// Whether this is a "system" module.
  bool IsSystem;

  /// The path to the modulemap file which defines this module.
  ///
  /// This can be used to explicitly build this module. This file will
  /// additionally appear in \c FileDeps as a dependency.
  std::string ClangModuleMapFile;

  /// A collection of absolute paths to files that this module directly depends
  /// on, not including transitive dependencies.
  llvm::StringSet<> FileDeps;

  /// A collection of absolute paths to module map files that this module needs
  /// to know about. The ordering is significant.
  std::vector<std::string> ModuleMapFileDeps;

  /// A collection of prebuilt modular dependencies this module directly depends
  /// on, not including transitive dependencies.
  std::vector<PrebuiltModuleDep> PrebuiltModuleDeps;

  /// A list of module identifiers this module directly depends on, not
  /// including transitive dependencies.
  ///
  /// This may include modules with a different context hash when it can be
  /// determined that the differences are benign for this compilation.
  std::vector<ModuleID> ClangModuleDeps;

  /// The set of libraries or frameworks to link against when
  /// an entity from this module is used.
  llvm::SmallVector<Module::LinkLibrary, 2> LinkLibraries;

  /// Get (or compute) the compiler invocation that can be used to build this
  /// module. Does not include argv[0].
  const std::vector<std::string> &getBuildArguments();

private:
  friend class ModuleDepCollectorPP;

  std::variant<std::monostate, CowCompilerInvocation, std::vector<std::string>>
      BuildInfo;
};

using PrebuiltModuleVFSMapT = llvm::StringMap<llvm::StringSet<>>;

class ModuleDepCollector;

/// Callback that records textual includes and direct modular includes/imports
/// during preprocessing. At the end of the main file, it also collects
/// transitive modular dependencies and passes everything to the
/// \c DependencyConsumer of the parent \c ModuleDepCollector.
class ModuleDepCollectorPP final : public PPCallbacks {
public:
  ModuleDepCollectorPP(ModuleDepCollector &MDC) : MDC(MDC) {}

  void LexedFileChanged(FileID FID, LexedFileChangeReason Reason,
                        SrcMgr::CharacteristicKind FileType, FileID PrevFID,
                        SourceLocation Loc) override;
  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange,
                          OptionalFileEntryRef File, StringRef SearchPath,
                          StringRef RelativePath, const Module *SuggestedModule,
                          bool ModuleImported,
                          SrcMgr::CharacteristicKind FileType) override;
  void moduleImport(SourceLocation ImportLoc, ModuleIdPath Path,
                    const Module *Imported) override;

  void EndOfMainFile() override;

private:
  /// The parent dependency collector.
  ModuleDepCollector &MDC;

  void handleImport(const Module *Imported);

  /// Adds direct modular dependencies that have already been built to the
  /// ModuleDeps instance.
  void
  addAllSubmodulePrebuiltDeps(const Module *M, ModuleDeps &MD,
                              llvm::DenseSet<const Module *> &SeenSubmodules);
  void addModulePrebuiltDeps(const Module *M, ModuleDeps &MD,
                             llvm::DenseSet<const Module *> &SeenSubmodules);

  /// Traverses the previously collected direct modular dependencies to discover
  /// transitive modular dependencies and fills the parent \c ModuleDepCollector
  /// with both.
  /// Returns the ID or nothing if the dependency is spurious and is ignored.
  std::optional<ModuleID> handleTopLevelModule(const Module *M);
  void addAllSubmoduleDeps(const Module *M, ModuleDeps &MD,
                           llvm::DenseSet<const Module *> &AddedModules);
  void addModuleDep(const Module *M, ModuleDeps &MD,
                    llvm::DenseSet<const Module *> &AddedModules);

  /// Traverses the affecting modules and updates \c MD with references to the
  /// parent \c ModuleDepCollector info.
  void addAllAffectingClangModules(const Module *M, ModuleDeps &MD,
                              llvm::DenseSet<const Module *> &AddedModules);
  void addAffectingClangModule(const Module *M, ModuleDeps &MD,
                          llvm::DenseSet<const Module *> &AddedModules);
};

/// Collects modular and non-modular dependencies of the main file by attaching
/// \c ModuleDepCollectorPP to the preprocessor.
class ModuleDepCollector final : public DependencyCollector {
public:
  ModuleDepCollector(std::unique_ptr<DependencyOutputOptions> Opts,
                     CompilerInstance &ScanInstance, DependencyConsumer &C,
                     DependencyActionController &Controller,
                     CompilerInvocation OriginalCI,
                     PrebuiltModuleVFSMapT PrebuiltModuleVFSMap,
                     ScanningOptimizations OptimizeArgs, bool EagerLoadModules,
                     bool IsStdModuleP1689Format);

  void attachToPreprocessor(Preprocessor &PP) override;
  void attachToASTReader(ASTReader &R) override;

  /// Apply any changes implied by the discovered dependencies to the given
  /// invocation, (e.g. disable implicit modules, add explicit module paths).
  void applyDiscoveredDependencies(CompilerInvocation &CI);

private:
  friend ModuleDepCollectorPP;

  /// The compiler instance for scanning the current translation unit.
  CompilerInstance &ScanInstance;
  /// The consumer of collected dependency information.
  DependencyConsumer &Consumer;
  /// Callbacks for computing dependency information.
  DependencyActionController &Controller;
  /// Mapping from prebuilt AST files to their sorted list of VFS overlay files.
  PrebuiltModuleVFSMapT PrebuiltModuleVFSMap;
  /// Path to the main source file.
  std::string MainFile;
  /// Hash identifying the compilation conditions of the current TU.
  std::string ContextHash;
  /// Non-modular file dependencies. This includes the main source file and
  /// textually included header files.
  std::vector<std::string> FileDeps;
  /// Direct and transitive modular dependencies of the main source file.
  llvm::MapVector<const Module *, std::unique_ptr<ModuleDeps>> ModularDeps;
  /// Secondary mapping for \c ModularDeps allowing lookup by ModuleID without
  /// a preprocessor. Storage owned by \c ModularDeps.
  llvm::DenseMap<ModuleID, ModuleDeps *> ModuleDepsByID;
  /// Direct modular dependencies that have already been built.
  llvm::MapVector<const Module *, PrebuiltModuleDep> DirectPrebuiltModularDeps;
  /// Working set of direct modular dependencies.
  llvm::SetVector<const Module *> DirectModularDeps;
  /// Options that control the dependency output generation.
  std::unique_ptr<DependencyOutputOptions> Opts;
  /// A Clang invocation that's based on the original TU invocation and that has
  /// been partially transformed into one that can perform explicit build of
  /// a discovered modular dependency. Note that this still needs to be adjusted
  /// for each individual module.
  CowCompilerInvocation CommonInvocation;
  /// Whether to optimize the modules' command-line arguments.
  ScanningOptimizations OptimizeArgs;
  /// Whether to set up command-lines to load PCM files eagerly.
  bool EagerLoadModules;
  /// If we're generating dependency output in P1689 format
  /// for standard C++ modules.
  bool IsStdModuleP1689Format;

  std::optional<P1689ModuleInfo> ProvidedStdCXXModule;
  std::vector<P1689ModuleInfo> RequiredStdCXXModules;

  /// Checks whether the module is known as being prebuilt.
  bool isPrebuiltModule(const Module *M);

  /// Adds \p Path to \c FileDeps, making it absolute if necessary.
  void addFileDep(StringRef Path);
  /// Adds \p Path to \c MD.FileDeps, making it absolute if necessary.
  void addFileDep(ModuleDeps &MD, StringRef Path);

  /// Get a Clang invocation adjusted to build the given modular dependency.
  /// This excludes paths that are yet-to-be-provided by the build system.
  CowCompilerInvocation getInvocationAdjustedForModuleBuildWithoutOutputs(
      const ModuleDeps &Deps,
      llvm::function_ref<void(CowCompilerInvocation &)> Optimize) const;

  /// Collect module map files for given modules.
  llvm::DenseSet<const FileEntry *>
  collectModuleMapFiles(ArrayRef<ModuleID> ClangModuleDeps) const;

  /// Add module map files to the invocation, if needed.
  void addModuleMapFiles(CompilerInvocation &CI,
                         ArrayRef<ModuleID> ClangModuleDeps) const;
  /// Add module files (pcm) to the invocation, if needed.
  void addModuleFiles(CompilerInvocation &CI,
                      ArrayRef<ModuleID> ClangModuleDeps) const;
  void addModuleFiles(CowCompilerInvocation &CI,
                      ArrayRef<ModuleID> ClangModuleDeps) const;

  /// Add paths that require looking up outputs to the given dependencies.
  void addOutputPaths(CowCompilerInvocation &CI, ModuleDeps &Deps);

  /// Compute the context hash for \p Deps, and create the mapping
  /// \c ModuleDepsByID[Deps.ID] = &Deps.
  void associateWithContextHash(const CowCompilerInvocation &CI,
                                ModuleDeps &Deps);
};

/// Resets codegen options that don't affect modules/PCH.
void resetBenignCodeGenOptions(frontend::ActionKind ProgramAction,
                               const LangOptions &LangOpts,
                               CodeGenOptions &CGOpts);

} // end namespace dependencies
} // end namespace tooling
} // end namespace clang

namespace llvm {
inline hash_code hash_value(const clang::tooling::dependencies::ModuleID &ID) {
  return hash_combine(ID.ModuleName, ID.ContextHash);
}

template <> struct DenseMapInfo<clang::tooling::dependencies::ModuleID> {
  using ModuleID = clang::tooling::dependencies::ModuleID;
  static inline ModuleID getEmptyKey() { return ModuleID{"", ""}; }
  static inline ModuleID getTombstoneKey() {
    return ModuleID{"~", "~"}; // ~ is not a valid module name or context hash
  }
  static unsigned getHashValue(const ModuleID &ID) { return hash_value(ID); }
  static bool isEqual(const ModuleID &LHS, const ModuleID &RHS) {
    return LHS == RHS;
  }
};
} // namespace llvm

#endif // LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_MODULEDEPCOLLECTOR_H
