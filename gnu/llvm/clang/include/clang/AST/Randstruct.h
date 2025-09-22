//===- Randstruct.h - Interfact for structure randomization -------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the interface for Clang's structure field layout
// randomization.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_RANDSTRUCT_H
#define LLVM_CLANG_AST_RANDSTRUCT_H

namespace llvm {
template <typename T> class SmallVectorImpl;
} // end namespace llvm

namespace clang {

class ASTContext;
class Decl;
class RecordDecl;

namespace randstruct {

bool randomizeStructureLayout(const ASTContext &Context, RecordDecl *RD,
                              llvm::SmallVectorImpl<Decl *> &FinalOrdering);

} // namespace randstruct
} // namespace clang

#endif // LLVM_CLANG_AST_RANDSTRUCT_H
