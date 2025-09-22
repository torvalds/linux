//===--- Transaction.h - Incremental Compilation and Execution---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines utilities tracking the incrementally processed pieces of
// code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INTERPRETER_PARTIALTRANSLATIONUNIT_H
#define LLVM_CLANG_INTERPRETER_PARTIALTRANSLATIONUNIT_H

#include <memory>

namespace llvm {
class Module;
}

namespace clang {

class TranslationUnitDecl;

/// The class keeps track of various objects created as part of processing
/// incremental inputs.
struct PartialTranslationUnit {
  TranslationUnitDecl *TUPart = nullptr;

  /// The llvm IR produced for the input.
  std::unique_ptr<llvm::Module> TheModule;
};
} // namespace clang

#endif // LLVM_CLANG_INTERPRETER_PARTIALTRANSLATIONUNIT_H
