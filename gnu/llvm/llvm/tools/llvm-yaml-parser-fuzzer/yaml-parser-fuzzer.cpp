//===-- yaml-parser-fuzzer.cpp - Fuzzer for YAML parser -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/YAMLParser.h"

using namespace llvm;

static bool isValidYaml(const uint8_t *Data, size_t Size) {
  SourceMgr SM;
  yaml::Stream Stream(StringRef(reinterpret_cast<const char *>(Data), Size),
                      SM);
  return Stream.validate();
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  std::vector<uint8_t> Input(Data, Data + Size);

  // Ensure we don't crash on any arbitrary byte string.
  isValidYaml(Input.data(), Input.size());

  // Ensure we don't crash on byte strings with no null characters.
  llvm::erase(Input, 0);
  Input.shrink_to_fit();
  bool IsValidWithout0s = isValidYaml(Input.data(), Input.size());

  // Ensure we don't crash on byte strings where the only null character is
  // one-past-the-end of the actual input to the parser.
  Input.push_back(0);
  Input.shrink_to_fit();
  bool IsValidWhen0Terminated = isValidYaml(Input.data(), Input.size() - 1);

  // Ensure we don't crash on byte strings with no null characters, but with
  // an invalid character one-past-the-end of the actual input to the parser.
  Input.back() = 1;
  bool IsValidWhen1Terminated = isValidYaml(Input.data(), Input.size() - 1);

  // The parser should either accept all of these inputs, or reject all of
  // them, because the parser sees an identical byte string in each case. This
  // should hopefully catch some cases where the parser is sensitive to what is
  // present one-past-the-end of the actual input.
  if (IsValidWithout0s != IsValidWhen0Terminated ||
      IsValidWhen0Terminated != IsValidWhen1Terminated)
    LLVM_BUILTIN_TRAP;

  return 0;
}
