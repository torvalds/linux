//===--- DummyYAMLParserFuzzer.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of main so we can build and test without linking libFuzzer.
//
//===----------------------------------------------------------------------===//

#include "llvm/FuzzMutate/FuzzerCLI.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);
int main(int argc, char *argv[]) {
  return llvm::runFuzzerOnInputs(argc, argv, LLVMFuzzerTestOneInput);
}
