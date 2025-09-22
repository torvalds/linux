//===- Utils.h - Misc utilities for the front-end ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This header contains miscellaneous utilities for various front-end actions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_UTILS_H
#define LLVM_CLANG_FRONTEND_UTILS_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Driver/OptionUtils.h"
#include "clang/Frontend/DependencyOutputOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FileCollector.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace clang {

class ASTReader;
class CompilerInstance;
class CompilerInvocation;
class DiagnosticsEngine;
class ExternalSemaSource;
class FrontendOptions;
class PCHContainerReader;
class Preprocessor;
class PreprocessorOptions;
class PreprocessorOutputOptions;
class CodeGenOptions;

/// InitializePreprocessor - Initialize the preprocessor getting it and the
/// environment ready to process a single file.
void InitializePreprocessor(Preprocessor &PP, const PreprocessorOptions &PPOpts,
                            const PCHContainerReader &PCHContainerRdr,
                            const FrontendOptions &FEOpts,
                            const CodeGenOptions &CodeGenOpts);

/// DoPrintPreprocessedInput - Implement -E mode.
void DoPrintPreprocessedInput(Preprocessor &PP, raw_ostream *OS,
                              const PreprocessorOutputOptions &Opts);

/// An interface for collecting the dependencies of a compilation. Users should
/// use \c attachToPreprocessor and \c attachToASTReader to get all of the
/// dependencies.
/// FIXME: Migrate DependencyGraphGen to use this interface.
class DependencyCollector {
public:
  virtual ~DependencyCollector();

  virtual void attachToPreprocessor(Preprocessor &PP);
  virtual void attachToASTReader(ASTReader &R);
  ArrayRef<std::string> getDependencies() const { return Dependencies; }

  /// Called when a new file is seen. Return true if \p Filename should be added
  /// to the list of dependencies.
  ///
  /// The default implementation ignores <built-in> and system files.
  virtual bool sawDependency(StringRef Filename, bool FromModule,
                             bool IsSystem, bool IsModuleFile, bool IsMissing);

  /// Called when the end of the main file is reached.
  virtual void finishedMainFile(DiagnosticsEngine &Diags) {}

  /// Return true if system files should be passed to sawDependency().
  virtual bool needSystemDependencies() { return false; }

  /// Add a dependency \p Filename if it has not been seen before and
  /// sawDependency() returns true.
  virtual void maybeAddDependency(StringRef Filename, bool FromModule,
                                  bool IsSystem, bool IsModuleFile,
                                  bool IsMissing);

protected:
  /// Return true if the filename was added to the list of dependencies, false
  /// otherwise.
  bool addDependency(StringRef Filename);

private:
  llvm::StringSet<> Seen;
  std::vector<std::string> Dependencies;
};

/// Builds a dependency file when attached to a Preprocessor (for includes) and
/// ASTReader (for module imports), and writes it out at the end of processing
/// a source file.  Users should attach to the ast reader whenever a module is
/// loaded.
class DependencyFileGenerator : public DependencyCollector {
public:
  DependencyFileGenerator(const DependencyOutputOptions &Opts);

  void attachToPreprocessor(Preprocessor &PP) override;

  void finishedMainFile(DiagnosticsEngine &Diags) override;

  bool needSystemDependencies() final { return IncludeSystemHeaders; }

  bool sawDependency(StringRef Filename, bool FromModule, bool IsSystem,
                     bool IsModuleFile, bool IsMissing) final;

protected:
  void outputDependencyFile(llvm::raw_ostream &OS);

private:
  void outputDependencyFile(DiagnosticsEngine &Diags);

  std::string OutputFile;
  std::vector<std::string> Targets;
  bool IncludeSystemHeaders;
  bool PhonyTarget;
  bool AddMissingHeaderDeps;
  bool SeenMissingHeader;
  bool IncludeModuleFiles;
  DependencyOutputFormat OutputFormat;
  unsigned InputFileIndex;
};

/// Collects the dependencies for imported modules into a directory.  Users
/// should attach to the AST reader whenever a module is loaded.
class ModuleDependencyCollector : public DependencyCollector {
  std::string DestDir;
  bool HasErrors = false;
  llvm::StringSet<> Seen;
  llvm::vfs::YAMLVFSWriter VFSWriter;
  llvm::FileCollector::PathCanonicalizer Canonicalizer;

  std::error_code copyToRoot(StringRef Src, StringRef Dst = {});

public:
  ModuleDependencyCollector(std::string DestDir)
      : DestDir(std::move(DestDir)) {}
  ~ModuleDependencyCollector() override { writeFileMap(); }

  StringRef getDest() { return DestDir; }
  virtual bool insertSeen(StringRef Filename) { return Seen.insert(Filename).second; }
  virtual void addFile(StringRef Filename, StringRef FileDst = {});

  virtual void addFileMapping(StringRef VPath, StringRef RPath) {
    VFSWriter.addFileMapping(VPath, RPath);
  }

  void attachToPreprocessor(Preprocessor &PP) override;
  void attachToASTReader(ASTReader &R) override;

  virtual void writeFileMap();
  virtual bool hasErrors() { return HasErrors; }
};

/// AttachDependencyGraphGen - Create a dependency graph generator, and attach
/// it to the given preprocessor.
void AttachDependencyGraphGen(Preprocessor &PP, StringRef OutputFile,
                              StringRef SysRoot);

/// AttachHeaderIncludeGen - Create a header include list generator, and attach
/// it to the given preprocessor.
///
/// \param DepOpts - Options controlling the output.
/// \param ShowAllHeaders - If true, show all header information instead of just
/// headers following the predefines buffer. This is useful for making sure
/// includes mentioned on the command line are also reported, but differs from
/// the default behavior used by -H.
/// \param OutputPath - If non-empty, a path to write the header include
/// information to, instead of writing to stderr.
/// \param ShowDepth - Whether to indent to show the nesting of the includes.
/// \param MSStyle - Whether to print in cl.exe /showIncludes style.
void AttachHeaderIncludeGen(Preprocessor &PP,
                            const DependencyOutputOptions &DepOpts,
                            bool ShowAllHeaders = false,
                            StringRef OutputPath = {},
                            bool ShowDepth = true, bool MSStyle = false);

/// The ChainedIncludesSource class converts headers to chained PCHs in
/// memory, mainly for testing.
IntrusiveRefCntPtr<ExternalSemaSource>
createChainedIncludesSource(CompilerInstance &CI,
                            IntrusiveRefCntPtr<ExternalSemaSource> &Reader);

/// Optional inputs to createInvocation.
struct CreateInvocationOptions {
  /// Receives diagnostics encountered while parsing command-line flags.
  /// If not provided, these are printed to stderr.
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags = nullptr;
  /// Used e.g. to probe for system headers locations.
  /// If not provided, the real filesystem is used.
  /// FIXME: the driver does perform some non-virtualized IO.
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS = nullptr;
  /// Whether to attempt to produce a non-null (possibly incorrect) invocation
  /// if any errors were encountered.
  /// By default, always return null on errors.
  bool RecoverOnError = false;
  /// Allow the driver to probe the filesystem for PCH files.
  /// This is used to replace -include with -include-pch in the cc1 args.
  /// FIXME: ProbePrecompiled=true is a poor, historical default.
  /// It misbehaves if the PCH file is from GCC, has the wrong version, etc.
  bool ProbePrecompiled = false;
  /// If set, the target is populated with the cc1 args produced by the driver.
  /// This may be populated even if createInvocation returns nullptr.
  std::vector<std::string> *CC1Args = nullptr;
};

/// Interpret clang arguments in preparation to parse a file.
///
/// This simulates a number of steps Clang takes when its driver is invoked:
/// - choosing actions (e.g compile + link) to run
/// - probing the system for settings like standard library locations
/// - spawning a cc1 subprocess to compile code, with more explicit arguments
/// - in the cc1 process, assembling those arguments into a CompilerInvocation
///   which is used to configure the parser
///
/// This simulation is lossy, e.g. in some situations one driver run would
/// result in multiple parses. (Multi-arch, CUDA, ...).
/// This function tries to select a reasonable invocation that tools should use.
///
/// Args[0] should be the driver name, such as "clang" or "/usr/bin/g++".
/// Absolute path is preferred - this affects searching for system headers.
///
/// May return nullptr if an invocation could not be determined.
/// See CreateInvocationOptions::ShouldRecoverOnErrors to try harder!
std::unique_ptr<CompilerInvocation>
createInvocation(ArrayRef<const char *> Args,
                 CreateInvocationOptions Opts = {});

} // namespace clang

#endif // LLVM_CLANG_FRONTEND_UTILS_H
