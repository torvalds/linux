//===-- SerialSnippetGenerator.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// A SnippetGenerator implementation to create serial instruction snippets.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_SERIALSNIPPETGENERATOR_H
#define LLVM_TOOLS_LLVM_EXEGESIS_SERIALSNIPPETGENERATOR_H

#include "Error.h"
#include "MCInstrDescView.h"
#include "SnippetGenerator.h"

namespace llvm {
namespace exegesis {

class SerialSnippetGenerator : public SnippetGenerator {
public:
  using SnippetGenerator::SnippetGenerator;
  ~SerialSnippetGenerator() override;

  Expected<std::vector<CodeTemplate>>
  generateCodeTemplates(InstructionTemplate Variant,
                        const BitVector &ForbiddenRegisters) const override;
};

} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_SERIALSNIPPETGENERATOR_H
