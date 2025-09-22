//==-- handle_llvm.h - Helper function for Clang fuzzers -------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines HandleLLVM for use by the Clang fuzzers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_CLANG_FUZZER_HANDLE_LLVM_HANDLELLVM_H
#define LLVM_CLANG_TOOLS_CLANG_FUZZER_HANDLE_LLVM_HANDLELLVM_H

#include <string>
#include <vector>

namespace clang_fuzzer {
void HandleLLVM(const std::string &S,
                const std::vector<const char *> &ExtraArgs);
} // namespace clang_fuzzer

#endif
