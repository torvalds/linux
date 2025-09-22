//===- DependencyScanningTool.h - clang-scan-deps service -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNINGTOOL_H
#define LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNINGTOOL_H

#include "clang/Tooling/DependencyScanning/DependencyScanningService.h"
#include "clang/Tooling/DependencyScanning/DependencyScanningWorker.h"
#include "clang/Tooling/DependencyScanning/ModuleDepCollector.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include <optional>
#include <string>
#include <vector>

namespace clang {
namespace tooling {
namespace dependencies {

/// A callback to lookup module outputs for "-fmodule-file=", "-o" etc.
using LookupModuleOutputCallback =
    llvm::function_ref<std::string(const ModuleID &, ModuleOutputKind)>;

/// Graph of modular dependencies.
using ModuleDepsGraph = std::vector<ModuleDeps>;

/// The full dependencies and module graph for a specific input.
struct TranslationUnitDeps {
  /// The graph of direct and transitive modular dependencies.
  ModuleDepsGraph ModuleGraph;

  /// The identifier of the C++20 module this translation unit exports.
  ///
  /// If the translation unit is not a module then \c ID.ModuleName is empty.
  ModuleID ID;

  /// A collection of absolute paths to files that this translation unit
  /// directly depends on, not including transitive dependencies.
  std::vector<std::string> FileDeps;

  /// A collection of prebuilt modules this translation unit directly depends
  /// on, not including transitive dependencies.
  std::vector<PrebuiltModuleDep> PrebuiltModuleDeps;

  /// A list of modules this translation unit directly depends on, not including
  /// transitive dependencies.
  ///
  /// This may include modules with a different context hash when it can be
  /// determined that the differences are benign for this compilation.
  std::vector<ModuleID> ClangModuleDeps;

  /// The sequence of commands required to build the translation unit. Commands
  /// should be executed in order.
  ///
  /// FIXME: If we add support for multi-arch builds in clang-scan-deps, we
  /// should make the dependencies between commands explicit to enable parallel
  /// builds of each architecture.
  std::vector<Command> Commands;

  /// Deprecated driver command-line. This will be removed in a future version.
  std::vector<std::string> DriverCommandLine;
};

struct P1689Rule {
  std::string PrimaryOutput;
  std::optional<P1689ModuleInfo> Provides;
  std::vector<P1689ModuleInfo> Requires;
};

/// The high-level implementation of the dependency discovery tool that runs on
/// an individual worker thread.
class DependencyScanningTool {
public:
  /// Construct a dependency scanning tool.
  DependencyScanningTool(DependencyScanningService &Service,
                         llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS =
                             llvm::vfs::createPhysicalFileSystem());

  /// Print out the dependency information into a string using the dependency
  /// file format that is specified in the options (-MD is the default) and
  /// return it.
  ///
  /// \returns A \c StringError with the diagnostic output if clang errors
  /// occurred, dependency file contents otherwise.
  llvm::Expected<std::string>
  getDependencyFile(const std::vector<std::string> &CommandLine, StringRef CWD);

  /// Collect the module dependency in P1689 format for C++20 named modules.
  ///
  /// \param MakeformatOutput The output parameter for dependency information
  /// in make format if the command line requires to generate make-format
  /// dependency information by `-MD -MF <dep_file>`.
  ///
  /// \param MakeformatOutputPath The output parameter for the path to
  /// \param MakeformatOutput.
  ///
  /// \returns A \c StringError with the diagnostic output if clang errors
  /// occurred, P1689 dependency format rules otherwise.
  llvm::Expected<P1689Rule>
  getP1689ModuleDependencyFile(const clang::tooling::CompileCommand &Command,
                               StringRef CWD, std::string &MakeformatOutput,
                               std::string &MakeformatOutputPath);
  llvm::Expected<P1689Rule>
  getP1689ModuleDependencyFile(const clang::tooling::CompileCommand &Command,
                               StringRef CWD) {
    std::string MakeformatOutput;
    std::string MakeformatOutputPath;

    return getP1689ModuleDependencyFile(Command, CWD, MakeformatOutput,
                                        MakeformatOutputPath);
  }

  /// Given a Clang driver command-line for a translation unit, gather the
  /// modular dependencies and return the information needed for explicit build.
  ///
  /// \param AlreadySeen This stores modules which have previously been
  ///                    reported. Use the same instance for all calls to this
  ///                    function for a single \c DependencyScanningTool in a
  ///                    single build. Use a different one for different tools,
  ///                    and clear it between builds.
  /// \param LookupModuleOutput This function is called to fill in
  ///                           "-fmodule-file=", "-o" and other output
  ///                           arguments for dependencies.
  ///
  /// \returns a \c StringError with the diagnostic output if clang errors
  /// occurred, \c TranslationUnitDeps otherwise.
  llvm::Expected<TranslationUnitDeps>
  getTranslationUnitDependencies(const std::vector<std::string> &CommandLine,
                                 StringRef CWD,
                                 const llvm::DenseSet<ModuleID> &AlreadySeen,
                                 LookupModuleOutputCallback LookupModuleOutput);

  /// Given a compilation context specified via the Clang driver command-line,
  /// gather modular dependencies of module with the given name, and return the
  /// information needed for explicit build.
  llvm::Expected<ModuleDepsGraph> getModuleDependencies(
      StringRef ModuleName, const std::vector<std::string> &CommandLine,
      StringRef CWD, const llvm::DenseSet<ModuleID> &AlreadySeen,
      LookupModuleOutputCallback LookupModuleOutput);

private:
  DependencyScanningWorker Worker;
};

class FullDependencyConsumer : public DependencyConsumer {
public:
  FullDependencyConsumer(const llvm::DenseSet<ModuleID> &AlreadySeen)
      : AlreadySeen(AlreadySeen) {}

  void handleBuildCommand(Command Cmd) override {
    Commands.push_back(std::move(Cmd));
  }

  void handleDependencyOutputOpts(const DependencyOutputOptions &) override {}

  void handleFileDependency(StringRef File) override {
    Dependencies.push_back(std::string(File));
  }

  void handlePrebuiltModuleDependency(PrebuiltModuleDep PMD) override {
    PrebuiltModuleDeps.emplace_back(std::move(PMD));
  }

  void handleModuleDependency(ModuleDeps MD) override {
    ClangModuleDeps[MD.ID] = std::move(MD);
  }

  void handleDirectModuleDependency(ModuleID ID) override {
    DirectModuleDeps.push_back(ID);
  }

  void handleContextHash(std::string Hash) override {
    ContextHash = std::move(Hash);
  }

  TranslationUnitDeps takeTranslationUnitDeps();
  ModuleDepsGraph takeModuleGraphDeps();

private:
  std::vector<std::string> Dependencies;
  std::vector<PrebuiltModuleDep> PrebuiltModuleDeps;
  llvm::MapVector<ModuleID, ModuleDeps> ClangModuleDeps;
  std::vector<ModuleID> DirectModuleDeps;
  std::vector<Command> Commands;
  std::string ContextHash;
  std::vector<std::string> OutputPaths;
  const llvm::DenseSet<ModuleID> &AlreadySeen;
};

/// A simple dependency action controller that uses a callback. If no callback
/// is provided, it is assumed that looking up module outputs is unreachable.
class CallbackActionController : public DependencyActionController {
public:
  virtual ~CallbackActionController();

  CallbackActionController(LookupModuleOutputCallback LMO)
      : LookupModuleOutput(std::move(LMO)) {
    if (!LookupModuleOutput) {
      LookupModuleOutput = [](const ModuleID &,
                              ModuleOutputKind) -> std::string {
        llvm::report_fatal_error("unexpected call to lookupModuleOutput");
      };
    }
  }

  std::string lookupModuleOutput(const ModuleID &ID,
                                 ModuleOutputKind Kind) override {
    return LookupModuleOutput(ID, Kind);
  }

private:
  LookupModuleOutputCallback LookupModuleOutput;
};

} // end namespace dependencies
} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNINGTOOL_H
