//===- InstallAPI/Frontend.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// Top level wrappers for InstallAPI frontend operations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INSTALLAPI_FRONTEND_H
#define LLVM_CLANG_INSTALLAPI_FRONTEND_H

#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/InstallAPI/Context.h"
#include "clang/InstallAPI/DylibVerifier.h"
#include "clang/InstallAPI/Visitor.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/MemoryBuffer.h"

namespace clang {
namespace installapi {

/// Create a buffer that contains all headers to scan
/// for global symbols with.
std::unique_ptr<llvm::MemoryBuffer> createInputBuffer(InstallAPIContext &Ctx);

class InstallAPIAction : public ASTFrontendAction {
public:
  explicit InstallAPIAction(InstallAPIContext &Ctx) : Ctx(Ctx) {}

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override {
    Ctx.Diags->getClient()->BeginSourceFile(CI.getLangOpts());
    Ctx.Verifier->setSourceManager(CI.getSourceManagerPtr());
    return std::make_unique<InstallAPIVisitor>(
        CI.getASTContext(), Ctx, CI.getSourceManager(), CI.getPreprocessor());
  }

private:
  InstallAPIContext &Ctx;
};
} // namespace installapi
} // namespace clang

#endif // LLVM_CLANG_INSTALLAPI_FRONTEND_H
