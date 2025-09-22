//===-- BenchmarkCode.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_BENCHMARKCODE_H
#define LLVM_TOOLS_LLVM_EXEGESIS_BENCHMARKCODE_H

#include "BenchmarkResult.h"
#include "llvm/MC/MCInst.h"
#include <string>
#include <vector>

namespace llvm {
namespace exegesis {

// A collection of instructions that are to be assembled, executed and measured.
struct BenchmarkCode {
  BenchmarkKey Key;

  // We also need to provide the registers that are live on entry for the
  // assembler to generate proper prologue/epilogue.
  std::vector<unsigned> LiveIns;

  // Informations about how this configuration was built.
  std::string Info;
};

} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_BENCHMARKCODE_H
