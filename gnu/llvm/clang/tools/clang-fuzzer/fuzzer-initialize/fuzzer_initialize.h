//==-- fuzzer_initialize.h - Fuzz Clang ------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines a function that returns the command line arguments for a specific
// call to the fuzz target.
//
//===----------------------------------------------------------------------===//

#include <vector>

namespace clang_fuzzer {
const std::vector<const char *>& GetCLArgs();
}
