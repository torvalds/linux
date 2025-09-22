//===--- AllTUsExecution.h - Execute actions on all TUs. -*- C++ --------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a tool executor that runs given actions on all TUs in the
//  compilation database. Tool results are deuplicated by the result key.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_ALLTUSEXECUTION_H
#define LLVM_CLANG_TOOLING_ALLTUSEXECUTION_H

#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/Execution.h"
#include <optional>

namespace clang {
namespace tooling {

/// Executes given frontend actions on all files/TUs in the compilation
/// database.
class AllTUsToolExecutor : public ToolExecutor {
public:
  static const char *ExecutorName;

  /// Init with \p CompilationDatabase.
  /// This uses \p ThreadCount threads to exececute the actions on all files in
  /// parallel. If \p ThreadCount is 0, this uses `llvm::hardware_concurrency`.
  AllTUsToolExecutor(const CompilationDatabase &Compilations,
                     unsigned ThreadCount,
                     std::shared_ptr<PCHContainerOperations> PCHContainerOps =
                         std::make_shared<PCHContainerOperations>());

  /// Init with \p CommonOptionsParser. This is expected to be used by
  /// `createExecutorFromCommandLineArgs` based on commandline options.
  ///
  /// The executor takes ownership of \p Options.
  AllTUsToolExecutor(CommonOptionsParser Options, unsigned ThreadCount,
                     std::shared_ptr<PCHContainerOperations> PCHContainerOps =
                         std::make_shared<PCHContainerOperations>());

  StringRef getExecutorName() const override { return ExecutorName; }

  using ToolExecutor::execute;

  llvm::Error
  execute(llvm::ArrayRef<
          std::pair<std::unique_ptr<FrontendActionFactory>, ArgumentsAdjuster>>
              Actions) override;

  ExecutionContext *getExecutionContext() override { return &Context; };

  ToolResults *getToolResults() override { return Results.get(); }

  void mapVirtualFile(StringRef FilePath, StringRef Content) override {
    OverlayFiles[FilePath] = std::string(Content);
  }

private:
  // Used to store the parser when the executor is initialized with parser.
  std::optional<CommonOptionsParser> OptionsParser;
  const CompilationDatabase &Compilations;
  std::unique_ptr<ToolResults> Results;
  ExecutionContext Context;
  llvm::StringMap<std::string> OverlayFiles;
  unsigned ThreadCount;
};

extern llvm::cl::opt<unsigned> ExecutorConcurrency;
extern llvm::cl::opt<std::string> Filter;

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_ALLTUSEXECUTION_H
