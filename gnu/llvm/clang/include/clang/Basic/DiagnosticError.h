//===--- DiagnosticError.h - Diagnostic payload for llvm::Error -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_DIAGNOSTICERROR_H
#define LLVM_CLANG_BASIC_DIAGNOSTICERROR_H

#include "clang/Basic/PartialDiagnostic.h"
#include "llvm/Support/Error.h"
#include <optional>

namespace clang {

/// Carries a Clang diagnostic in an llvm::Error.
///
/// Users should emit the stored diagnostic using the DiagnosticsEngine.
class DiagnosticError : public llvm::ErrorInfo<DiagnosticError> {
public:
  DiagnosticError(PartialDiagnosticAt Diag) : Diag(std::move(Diag)) {}

  void log(raw_ostream &OS) const override { OS << "clang diagnostic"; }

  PartialDiagnosticAt &getDiagnostic() { return Diag; }
  const PartialDiagnosticAt &getDiagnostic() const { return Diag; }

  /// Creates a new \c DiagnosticError that contains the given diagnostic at
  /// the given location.
  static llvm::Error create(SourceLocation Loc, PartialDiagnostic Diag) {
    return llvm::make_error<DiagnosticError>(
        PartialDiagnosticAt(Loc, std::move(Diag)));
  }

  /// Extracts and returns the diagnostic payload from the given \c Error if
  /// the error is a \c DiagnosticError. Returns std::nullopt if the given error
  /// is not a \c DiagnosticError.
  static std::optional<PartialDiagnosticAt> take(llvm::Error &Err) {
    std::optional<PartialDiagnosticAt> Result;
    Err = llvm::handleErrors(std::move(Err), [&](DiagnosticError &E) {
      Result = std::move(E.getDiagnostic());
    });
    return Result;
  }

  static char ID;

private:
  // Users are not expected to use error_code.
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }

  PartialDiagnosticAt Diag;
};

} // end namespace clang

#endif // LLVM_CLANG_BASIC_DIAGNOSTICERROR_H
