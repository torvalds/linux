//===- Dominators.cpp - Implementation of dominators tree for Clang CFG ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/Dominators.h"

namespace clang {

template <>
void CFGDominatorTreeImpl</*IsPostDom=*/true>::anchor() {}

template <>
void CFGDominatorTreeImpl</*IsPostDom=*/false>::anchor() {}

} // end of namespace clang
