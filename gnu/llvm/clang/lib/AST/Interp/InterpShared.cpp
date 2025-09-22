//===--- InterpShared.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InterpShared.h"
#include "clang/AST/Attr.h"
#include "llvm/ADT/BitVector.h"

namespace clang {
namespace interp {

llvm::BitVector collectNonNullArgs(const FunctionDecl *F,
                                   const llvm::ArrayRef<const Expr *> &Args) {
  llvm::BitVector NonNullArgs;
  if (!F)
    return NonNullArgs;

  assert(F);
  NonNullArgs.resize(Args.size());

  for (const auto *Attr : F->specific_attrs<NonNullAttr>()) {
    if (!Attr->args_size()) {
      NonNullArgs.set();
      break;
    } else
      for (auto Idx : Attr->args()) {
        unsigned ASTIdx = Idx.getASTIndex();
        if (ASTIdx >= Args.size())
          continue;
        NonNullArgs[ASTIdx] = true;
      }
  }

  return NonNullArgs;
}

} // namespace interp
} // namespace clang
