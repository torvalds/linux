//===-- AnalyzerHelpFlags.h - Query functions for --help flags --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_FRONTEND_ANALYZERHELPFLAGS_H
#define LLVM_CLANG_STATICANALYZER_FRONTEND_ANALYZERHELPFLAGS_H

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace clang {

class CompilerInstance;

namespace ento {

void printCheckerHelp(llvm::raw_ostream &OS, CompilerInstance &CI);
void printEnabledCheckerList(llvm::raw_ostream &OS, CompilerInstance &CI);
void printAnalyzerConfigList(llvm::raw_ostream &OS);
void printCheckerConfigList(llvm::raw_ostream &OS, CompilerInstance &CI);

} // namespace ento
} // namespace clang

#endif
