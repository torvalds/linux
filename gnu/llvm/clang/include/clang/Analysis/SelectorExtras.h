//=== SelectorExtras.h - Helpers for checkers using selectors -----*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_SELECTOREXTRAS_H
#define LLVM_CLANG_ANALYSIS_SELECTOREXTRAS_H

#include "clang/AST/ASTContext.h"

namespace clang {

template <typename... IdentifierInfos>
static inline Selector getKeywordSelector(ASTContext &Ctx,
                                          const IdentifierInfos *...IIs) {
  static_assert(sizeof...(IdentifierInfos) > 0,
                "keyword selectors must have at least one argument");
  SmallVector<const IdentifierInfo *, 10> II({&Ctx.Idents.get(IIs)...});

  return Ctx.Selectors.getSelector(II.size(), &II[0]);
}

template <typename... IdentifierInfos>
static inline void lazyInitKeywordSelector(Selector &Sel, ASTContext &Ctx,
                                           IdentifierInfos *... IIs) {
  if (!Sel.isNull())
    return;
  Sel = getKeywordSelector(Ctx, IIs...);
}

} // end namespace clang

#endif
