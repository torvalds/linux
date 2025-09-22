//===--- llvm-dlang-demangle-fuzzer.cpp - Fuzzer for the DLang Demangler --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Demangle/Demangle.h"
#include <cstdint>
#include <cstdlib>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  std::string NullTerminatedString((const char *)Data, Size);
  char *Demangled = llvm::dlangDemangle(NullTerminatedString);
  std::free(Demangled);
  return 0;
}
