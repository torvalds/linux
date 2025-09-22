//===- Rewrite.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang-c/Rewrite.h"
#include "CXSourceLocation.h"
#include "CXTranslationUnit.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Rewrite/Core/Rewriter.h"

CXRewriter clang_CXRewriter_create(CXTranslationUnit TU) {
  if (clang::cxtu::isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return {};
  }
  clang::ASTUnit *AU = clang::cxtu::getASTUnit(TU);
  assert(AU);
  return reinterpret_cast<CXRewriter>(
      new clang::Rewriter(AU->getSourceManager(), AU->getLangOpts()));
}

void clang_CXRewriter_insertTextBefore(CXRewriter Rew, CXSourceLocation Loc,
                            const char *Insert) {
  assert(Rew);
  clang::Rewriter &R = *reinterpret_cast<clang::Rewriter *>(Rew);
  R.InsertTextBefore(clang::cxloc::translateSourceLocation(Loc), Insert);
}

void clang_CXRewriter_replaceText(CXRewriter Rew, CXSourceRange ToBeReplaced,
                       const char *Replacement) {
  assert(Rew);
  clang::Rewriter &R = *reinterpret_cast<clang::Rewriter *>(Rew);
  R.ReplaceText(clang::cxloc::translateCXRangeToCharRange(ToBeReplaced),
                Replacement);
}

void clang_CXRewriter_removeText(CXRewriter Rew, CXSourceRange ToBeRemoved) {
  assert(Rew);
  clang::Rewriter &R = *reinterpret_cast<clang::Rewriter *>(Rew);
  R.RemoveText(clang::cxloc::translateCXRangeToCharRange(ToBeRemoved));
}

int clang_CXRewriter_overwriteChangedFiles(CXRewriter Rew) {
  assert(Rew);
  clang::Rewriter &R = *reinterpret_cast<clang::Rewriter *>(Rew);
  return R.overwriteChangedFiles();
}

void clang_CXRewriter_writeMainFileToStdOut(CXRewriter Rew) {
  assert(Rew);
  clang::Rewriter &R = *reinterpret_cast<clang::Rewriter *>(Rew);
  R.getEditBuffer(R.getSourceMgr().getMainFileID()).write(llvm::outs());
}

void clang_CXRewriter_dispose(CXRewriter Rew) {
  if (Rew)
    delete reinterpret_cast<clang::Rewriter *>(Rew);
}
