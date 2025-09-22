//===--- IntegerLiteralSeparatorFixer.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares IntegerLiteralSeparatorFixer that fixes C++ integer
/// literal separators.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_INTEGERLITERALSEPARATORFIXER_H
#define LLVM_CLANG_LIB_FORMAT_INTEGERLITERALSEPARATORFIXER_H

#include "TokenAnalyzer.h"

namespace clang {
namespace format {

class IntegerLiteralSeparatorFixer {
public:
  std::pair<tooling::Replacements, unsigned> process(const Environment &Env,
                                                     const FormatStyle &Style);

private:
  bool checkSeparator(const StringRef IntegerLiteral, int DigitsPerGroup) const;
  std::string format(const StringRef IntegerLiteral, int DigitsPerGroup,
                     int DigitCount, bool RemoveSeparator) const;

  char Separator;
};

} // end namespace format
} // end namespace clang

#endif
