//===-- SnippetFile.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Utilities to read a snippet file.
/// Snippet files are just asm files with additional comments to specify which
/// registers should be defined or are live on entry.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_SNIPPETFILE_H
#define LLVM_TOOLS_LLVM_EXEGESIS_SNIPPETFILE_H

#include "BenchmarkCode.h"
#include "BenchmarkRunner.h"
#include "LlvmState.h"
#include "llvm/Support/Error.h"

#include <vector>

namespace llvm {
namespace exegesis {

// Reads code snippets from file `Filename`.
Expected<std::vector<BenchmarkCode>> readSnippets(const LLVMState &State,
                                                  StringRef Filename);

} // namespace exegesis
} // namespace llvm

#endif
