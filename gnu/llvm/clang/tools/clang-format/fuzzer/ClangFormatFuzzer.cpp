//===-- ClangFormatFuzzer.cpp - Fuzz the Clang format tool ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a function that runs Clang format on a single
///  input. This function is then linked into the Fuzzer library.
///
//===----------------------------------------------------------------------===//

#include "clang/Format/Format.h"

extern "C" int LLVMFuzzerTestOneInput(uint8_t *data, size_t size) {
  // FIXME: fuzz more things: different styles, different style features.
  std::string s((const char *)data, size);
  auto Style = getGoogleStyle(clang::format::FormatStyle::LK_Cpp);
  Style.ColumnLimit = 60;
  Style.Macros.push_back("ASSIGN_OR_RETURN(a, b)=a = (b)");
  Style.Macros.push_back("ASSIGN_OR_RETURN(a, b, c)=a = (b); if (x) return c");
  Style.Macros.push_back("MOCK_METHOD(r, n, a, s)=r n a s");
  auto Replaces = reformat(Style, s, clang::tooling::Range(0, s.size()));
  auto Result = applyAllReplacements(s, Replaces);

  // Output must be checked, as otherwise we crash.
  if (!Result) {
  }
  return 0;
}
