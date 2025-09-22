//=======- DiagOutputUtils.h -------------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYZER_WEBKIT_DIAGPRINTUTILS_H
#define LLVM_CLANG_ANALYZER_WEBKIT_DIAGPRINTUTILS_H

#include "clang/AST/Decl.h"
#include "llvm/Support/raw_ostream.h"

namespace clang {

template <typename NamedDeclDerivedT>
void printQuotedQualifiedName(llvm::raw_ostream &Os,
                              const NamedDeclDerivedT &D) {
  Os << "'";
  D->getNameForDiagnostic(Os, D->getASTContext().getPrintingPolicy(),
                          /*Qualified=*/true);
  Os << "'";
}

template <typename NamedDeclDerivedT>
void printQuotedName(llvm::raw_ostream &Os, const NamedDeclDerivedT &D) {
  Os << "'";
  D->getNameForDiagnostic(Os, D->getASTContext().getPrintingPolicy(),
                          /*Qualified=*/false);
  Os << "'";
}

} // namespace clang

#endif
