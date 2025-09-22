//===--- InterpShared.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_AST_INTERP_SHARED_H
#define LLVM_CLANG_LIB_AST_INTERP_SHARED_H

#include "llvm/ADT/BitVector.h"

namespace clang {
class FunctionDecl;
class Expr;

namespace interp {

llvm::BitVector collectNonNullArgs(const FunctionDecl *F,
                                   const llvm::ArrayRef<const Expr *> &Args);

} // namespace interp
} // namespace clang

#endif
