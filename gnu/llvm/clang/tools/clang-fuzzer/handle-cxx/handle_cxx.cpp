//==-- handle_cxx.cpp - Helper function for Clang fuzzers ------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements HandleCXX for use by the Clang fuzzers.
//
//===----------------------------------------------------------------------===//

#include "handle_cxx.h"

#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Option/Option.h"

using namespace clang;

void clang_fuzzer::HandleCXX(const std::string &S,
                             const char *FileName,
                             const std::vector<const char *> &ExtraArgs) {
  llvm::opt::ArgStringList CC1Args;
  CC1Args.push_back("-cc1");
  for (auto &A : ExtraArgs)
    CC1Args.push_back(A);
  CC1Args.push_back(FileName);

  llvm::IntrusiveRefCntPtr<FileManager> Files(
      new FileManager(FileSystemOptions()));
  IgnoringDiagConsumer Diags;
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
  DiagnosticsEngine Diagnostics(
      IntrusiveRefCntPtr<clang::DiagnosticIDs>(new DiagnosticIDs()), &*DiagOpts,
      &Diags, false);
  std::unique_ptr<clang::CompilerInvocation> Invocation(
      tooling::newInvocation(&Diagnostics, CC1Args, /*BinaryName=*/nullptr));
  std::unique_ptr<llvm::MemoryBuffer> Input =
      llvm::MemoryBuffer::getMemBuffer(S);
  Invocation->getPreprocessorOpts().addRemappedFile(FileName,
                                                    Input.release());
  std::unique_ptr<tooling::ToolAction> action(
      tooling::newFrontendActionFactory<clang::EmitObjAction>());
  std::shared_ptr<PCHContainerOperations> PCHContainerOps =
      std::make_shared<PCHContainerOperations>();
  action->runInvocation(std::move(Invocation), Files.get(), PCHContainerOps,
                        &Diags);
}

