//===-- ClangObjectiveCFuzzer.cpp - Fuzz Clang ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a function that runs Clang on a single Objective-C
///   input. This function is then linked into the Fuzzer library.
///
//===----------------------------------------------------------------------===//

#include "handle-cxx/handle_cxx.h"

using namespace clang_fuzzer;

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) { return 0; }

extern "C" int LLVMFuzzerTestOneInput(uint8_t *data, size_t size) {
  std::string s(reinterpret_cast<const char *>(data), size);
  HandleCXX(s, "./test.m", {"-O2"});
  return 0;
}

