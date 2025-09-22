//===- FuzzerFork.h - run fuzzing in sub-processes --------------*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_FORK_H
#define LLVM_FUZZER_FORK_H

#include "FuzzerDefs.h"
#include "FuzzerOptions.h"
#include "FuzzerRandom.h"

#include <string>

namespace fuzzer {
void FuzzWithFork(Random &Rand, const FuzzingOptions &Options,
                  const std::vector<std::string> &Args,
                  const std::vector<std::string> &CorpusDirs, int NumJobs);
} // namespace fuzzer

#endif // LLVM_FUZZER_FORK_H
