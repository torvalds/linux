//===-- yaml-numeric-parser-fuzzer.cpp - Fuzzer for YAML numeric parser ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/YAMLTraits.h"
#include <string>

inline bool isNumericRegex(llvm::StringRef S) {
  static llvm::Regex Infinity("^[-+]?(\\.inf|\\.Inf|\\.INF)$");
  static llvm::Regex Base8("^0o[0-7]+$");
  static llvm::Regex Base16("^0x[0-9a-fA-F]+$");
  static llvm::Regex Float(
      "^[-+]?(\\.[0-9]+|[0-9]+(\\.[0-9]*)?)([eE][-+]?[0-9]+)?$");

  if (S == ".nan" || S == ".NaN" || S == ".NAN")
    return true;

  if (Infinity.match(S))
    return true;

  if (Base8.match(S))
    return true;

  if (Base16.match(S))
    return true;

  if (Float.match(S))
    return true;

  return false;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  std::string Input(reinterpret_cast<const char *>(Data), Size);
  llvm::erase(Input, 0);
  if (!Input.empty() && llvm::yaml::isNumeric(Input) != isNumericRegex(Input))
    LLVM_BUILTIN_TRAP;
  return 0;
}
