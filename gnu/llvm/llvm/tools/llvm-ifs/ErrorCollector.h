//===- ErrorCollector.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===/
///
/// This class collects errors that should be reported or ignored in aggregate.
///
/// Like llvm::Error, an ErrorCollector cannot be copied. Unlike llvm::Error,
/// an ErrorCollector may be destroyed if it was originally constructed to treat
/// errors as non-fatal. In this case, all Errors are consumed upon destruction.
/// An ErrorCollector may be initially constructed (or escalated) such that
/// errors are treated as fatal. This causes a crash if an attempt is made to
/// delete the ErrorCollector when some Errors have not been retrieved via
/// makeError().
///
//===-----------------------------------------------------------------------===/

#ifndef LLVM_TOOLS_LLVM_IFS_ERRORCOLLECTOR_H
#define LLVM_TOOLS_LLVM_IFS_ERRORCOLLECTOR_H

#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

namespace llvm {

namespace ifs {

class ErrorCollector {
public:
  /// Upon destruction, an ErrorCollector will crash if UseFatalErrors=true and
  /// there are remaining errors that haven't been fetched by makeError().
  ErrorCollector(bool UseFatalErrors = true) : ErrorsAreFatal(UseFatalErrors) {}
  // Don't allow copying.
  ErrorCollector(const ErrorCollector &Stub) = delete;
  ErrorCollector &operator=(const ErrorCollector &Other) = delete;
  ~ErrorCollector();

  // TODO: Add move constructor and operator= when a testable situation arises.

  /// Returns a single error that contains messages for all stored Errors.
  Error makeError();

  /// Adds an error with a descriptive tag that helps with identification.
  /// If the error is an Error::success(), it is checked and discarded.
  void addError(Error &&E, StringRef Tag);

  /// This ensures an ErrorCollector will treat unhandled errors as fatal.
  /// This function should be called if errors that usually can be ignored
  /// are suddenly of concern (i.e. attempt multiple things that return Error,
  /// but only care about the Errors if no attempt succeeds).
  void escalateToFatal();

private:
  /// Logs all errors to a raw_ostream.
  void log(raw_ostream &OS);

  /// Returns true if all errors have been retrieved through makeError(), or
  /// false if errors have been added since the last makeError() call.
  bool allErrorsHandled() const;

  /// Dump output and crash.
  [[noreturn]] void fatalUnhandledError();

  bool ErrorsAreFatal;
  std::vector<Error> Errors;
  std::vector<std::string> Tags;
};

} // end namespace ifs
} // end namespace llvm

#endif // LLVM_TOOLS_LLVM_IFS_ERRORCOLLECTOR_H
