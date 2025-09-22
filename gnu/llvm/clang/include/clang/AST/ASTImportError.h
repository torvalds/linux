//===- ASTImportError.h - Define errors while importing AST -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTImportError class which basically defines the kind
//  of error while importing AST .
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTIMPORTERROR_H
#define LLVM_CLANG_AST_ASTIMPORTERROR_H

#include "llvm/Support/Error.h"

namespace clang {

class ASTImportError : public llvm::ErrorInfo<ASTImportError> {
public:
  /// \brief Kind of error when importing an AST component.
  enum ErrorKind {
    NameConflict,         /// Naming ambiguity (likely ODR violation).
    UnsupportedConstruct, /// Not supported node or case.
    Unknown               /// Other error.
  };

  ErrorKind Error;

  static char ID;

  ASTImportError() : Error(Unknown) {}
  ASTImportError(const ASTImportError &Other) : Error(Other.Error) {}
  ASTImportError &operator=(const ASTImportError &Other) {
    Error = Other.Error;
    return *this;
  }
  ASTImportError(ErrorKind Error) : Error(Error) {}

  std::string toString() const;

  void log(llvm::raw_ostream &OS) const override;
  std::error_code convertToErrorCode() const override;
};

} // namespace clang

#endif // LLVM_CLANG_AST_ASTIMPORTERROR_H
