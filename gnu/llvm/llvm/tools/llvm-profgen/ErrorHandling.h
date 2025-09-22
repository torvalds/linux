//===-- ErrorHandling.h - Error handler -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_PROFGEN_ERRORHANDLING_H
#define LLVM_TOOLS_LLVM_PROFGEN_ERRORHANDLING_H

#include "llvm/ADT/Twine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/WithColor.h"
#include <system_error>

using namespace llvm;

[[noreturn]] inline void exitWithError(const Twine &Message,
                                       StringRef Whence = StringRef(),
                                       StringRef Hint = StringRef()) {
  WithColor::error(errs(), "llvm-profgen");
  if (!Whence.empty())
    errs() << Whence.str() << ": ";
  errs() << Message << "\n";
  if (!Hint.empty())
    WithColor::note() << Hint.str() << "\n";
  ::exit(EXIT_FAILURE);
}

[[noreturn]] inline void exitWithError(std::error_code EC,
                                       StringRef Whence = StringRef()) {
  exitWithError(EC.message(), Whence);
}

[[noreturn]] inline void exitWithError(Error E, StringRef Whence) {
  exitWithError(errorToErrorCode(std::move(E)), Whence);
}

template <typename T, typename... Ts>
T unwrapOrError(Expected<T> EO, Ts &&... Args) {
  if (EO)
    return std::move(*EO);
  exitWithError(EO.takeError(), std::forward<Ts>(Args)...);
}

inline void emitWarningSummary(uint64_t Num, uint64_t Total, StringRef Msg) {
  if (!Total || !Num)
    return;
  WithColor::warning() << format("%.2f", static_cast<double>(Num) * 100 / Total)
                       << "%(" << Num << "/" << Total << ") " << Msg << "\n";
}

#endif
