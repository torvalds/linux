//===-- ASTMerge.cpp - AST Merging Frontend Action --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/Frontend/ASTUnit.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/ASTImporter.h"
#include "clang/AST/ASTImporterSharedState.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"

using namespace clang;

std::unique_ptr<ASTConsumer>
ASTMergeAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  return AdaptedAction->CreateASTConsumer(CI, InFile);
}

bool ASTMergeAction::BeginSourceFileAction(CompilerInstance &CI) {
  // FIXME: This is a hack. We need a better way to communicate the
  // AST file, compiler instance, and file name than member variables
  // of FrontendAction.
  AdaptedAction->setCurrentInput(getCurrentInput(), takeCurrentASTUnit());
  AdaptedAction->setCompilerInstance(&CI);
  return AdaptedAction->BeginSourceFileAction(CI);
}

void ASTMergeAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  CI.getDiagnostics().getClient()->BeginSourceFile(
                                             CI.getASTContext().getLangOpts());
  CI.getDiagnostics().SetArgToStringFn(&FormatASTNodeDiagnosticArgument,
                                       &CI.getASTContext());
  IntrusiveRefCntPtr<DiagnosticIDs>
      DiagIDs(CI.getDiagnostics().getDiagnosticIDs());
  auto SharedState = std::make_shared<ASTImporterSharedState>(
      *CI.getASTContext().getTranslationUnitDecl());
  for (unsigned I = 0, N = ASTFiles.size(); I != N; ++I) {
    IntrusiveRefCntPtr<DiagnosticsEngine>
        Diags(new DiagnosticsEngine(DiagIDs, &CI.getDiagnosticOpts(),
                                    new ForwardingDiagnosticConsumer(
                                          *CI.getDiagnostics().getClient()),
                                    /*ShouldOwnClient=*/true));
    std::unique_ptr<ASTUnit> Unit = ASTUnit::LoadFromASTFile(
        ASTFiles[I], CI.getPCHContainerReader(), ASTUnit::LoadEverything, Diags,
        CI.getFileSystemOpts(), CI.getHeaderSearchOptsPtr());

    if (!Unit)
      continue;

    ASTImporter Importer(CI.getASTContext(), CI.getFileManager(),
                         Unit->getASTContext(), Unit->getFileManager(),
                         /*MinimalImport=*/false, SharedState);

    TranslationUnitDecl *TU = Unit->getASTContext().getTranslationUnitDecl();
    for (auto *D : TU->decls()) {
      // Don't re-import __va_list_tag, __builtin_va_list.
      if (const auto *ND = dyn_cast<NamedDecl>(D))
        if (IdentifierInfo *II = ND->getIdentifier())
          if (II->isStr("__va_list_tag") || II->isStr("__builtin_va_list"))
            continue;

      llvm::Expected<Decl *> ToDOrError = Importer.Import(D);

      if (ToDOrError) {
        DeclGroupRef DGR(*ToDOrError);
        CI.getASTConsumer().HandleTopLevelDecl(DGR);
      } else {
        llvm::consumeError(ToDOrError.takeError());
      }
    }
  }

  AdaptedAction->ExecuteAction();
  CI.getDiagnostics().getClient()->EndSourceFile();
}

void ASTMergeAction::EndSourceFileAction() {
  return AdaptedAction->EndSourceFileAction();
}

ASTMergeAction::ASTMergeAction(std::unique_ptr<FrontendAction> adaptedAction,
                               ArrayRef<std::string> ASTFiles)
: AdaptedAction(std::move(adaptedAction)), ASTFiles(ASTFiles.begin(), ASTFiles.end()) {
  assert(AdaptedAction && "ASTMergeAction needs an action to adapt");
}

ASTMergeAction::~ASTMergeAction() {
}

bool ASTMergeAction::usesPreprocessorOnly() const {
  return AdaptedAction->usesPreprocessorOnly();
}

TranslationUnitKind ASTMergeAction::getTranslationUnitKind() {
  return AdaptedAction->getTranslationUnitKind();
}

bool ASTMergeAction::hasPCHSupport() const {
  return AdaptedAction->hasPCHSupport();
}

bool ASTMergeAction::hasASTFileSupport() const {
  return AdaptedAction->hasASTFileSupport();
}

bool ASTMergeAction::hasCodeCompletionSupport() const {
  return AdaptedAction->hasCodeCompletionSupport();
}
