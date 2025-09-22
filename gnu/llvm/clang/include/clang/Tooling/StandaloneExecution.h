//===--- StandaloneExecution.h - Standalone execution. -*- C++ ----------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines standalone execution of clang tools.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_STANDALONEEXECUTION_H
#define LLVM_CLANG_TOOLING_STANDALONEEXECUTION_H

#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/Execution.h"
#include <optional>

namespace clang {
namespace tooling {

/// A standalone executor that runs FrontendActions on a given set of
/// TUs in sequence.
///
/// By default, this executor uses the following arguments adjusters (as defined
/// in `clang/Tooling/ArgumentsAdjusters.h`):
///   - `getClangStripOutputAdjuster()`
///   - `getClangSyntaxOnlyAdjuster()`
///   - `getClangStripDependencyFileAdjuster()`
class StandaloneToolExecutor : public ToolExecutor {
public:
  static const char *ExecutorName;

  /// Init with \p CompilationDatabase and the paths of all files to be
  /// proccessed.
  StandaloneToolExecutor(
      const CompilationDatabase &Compilations,
      llvm::ArrayRef<std::string> SourcePaths,
      IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS =
          llvm::vfs::getRealFileSystem(),
      std::shared_ptr<PCHContainerOperations> PCHContainerOps =
          std::make_shared<PCHContainerOperations>());

  /// Init with \p CommonOptionsParser. This is expected to be used by
  /// `createExecutorFromCommandLineArgs` based on commandline options.
  ///
  /// The executor takes ownership of \p Options.
  StandaloneToolExecutor(
      CommonOptionsParser Options,
      std::shared_ptr<PCHContainerOperations> PCHContainerOps =
          std::make_shared<PCHContainerOperations>());

  StringRef getExecutorName() const override { return ExecutorName; }

  using ToolExecutor::execute;

  llvm::Error
  execute(llvm::ArrayRef<
          std::pair<std::unique_ptr<FrontendActionFactory>, ArgumentsAdjuster>>
              Actions) override;

  /// Set a \c DiagnosticConsumer to use during parsing.
  void setDiagnosticConsumer(DiagnosticConsumer *DiagConsumer) {
    Tool.setDiagnosticConsumer(DiagConsumer);
  }

  ExecutionContext *getExecutionContext() override { return &Context; };

  ToolResults *getToolResults() override { return &Results; }

  llvm::ArrayRef<std::string> getSourcePaths() const {
    return Tool.getSourcePaths();
  }

  void mapVirtualFile(StringRef FilePath, StringRef Content) override {
    Tool.mapVirtualFile(FilePath, Content);
  }

  /// Returns the file manager used in the tool.
  ///
  /// The file manager is shared between all translation units.
  FileManager &getFiles() { return Tool.getFiles(); }

private:
  // Used to store the parser when the executor is initialized with parser.
  std::optional<CommonOptionsParser> OptionsParser;
  // FIXME: The standalone executor is currently just a wrapper of `ClangTool`.
  // Merge `ClangTool` implementation into the this.
  ClangTool Tool;
  ExecutionContext Context;
  InMemoryToolResults Results;
  ArgumentsAdjuster ArgsAdjuster;
};

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_STANDALONEEXECUTION_H
