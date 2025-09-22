//===- HotnessThresholdParser.h - Parser for hotness threshold --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a simple parser to decode commandline option for
/// remarks hotness threshold that supports both int and a special 'auto' value.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_HOTNESSTHRESHOLDPARSER_H
#define LLVM_REMARKS_HOTNESSTHRESHOLDPARSER_H

#include "llvm/Support/CommandLine.h"
#include <optional>

namespace llvm {
namespace remarks {

// Parse remarks hotness threshold argument value.
// Valid option values are
// 1. integer: manually specified threshold; or
// 2. string 'auto': automatically get threshold from profile summary.
//
// Return std::nullopt Optional if 'auto' is specified, indicating the value
// will be filled later during PSI.
inline Expected<std::optional<uint64_t>> parseHotnessThresholdOption(StringRef Arg) {
  if (Arg == "auto")
    return std::nullopt;

  int64_t Val;
  if (Arg.getAsInteger(10, Val))
    return createStringError(llvm::inconvertibleErrorCode(),
                             "Not an integer: %s", Arg.data());

  // Negative integer effectively means no threshold
  return Val < 0 ? 0 : Val;
}

// A simple CL parser for '*-remarks-hotness-threshold='
class HotnessThresholdParser : public cl::parser<std::optional<uint64_t>> {
public:
  HotnessThresholdParser(cl::Option &O) : cl::parser<std::optional<uint64_t>>(O) {}

  bool parse(cl::Option &O, StringRef ArgName, StringRef Arg,
             std::optional<uint64_t> &V) {
    auto ResultOrErr = parseHotnessThresholdOption(Arg);
    if (!ResultOrErr)
      return O.error("Invalid argument '" + Arg +
                     "', only integer or 'auto' is supported.");

    V = *ResultOrErr;
    return false;
  }
};

} // namespace remarks
} // namespace llvm
#endif // LLVM_REMARKS_HOTNESSTHRESHOLDPARSER_H
