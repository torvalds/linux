//===- DeclGroup.cpp - Classes for representing groups of Decls -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the DeclGroup and DeclGroupRef classes.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclGroup.h"
#include "clang/AST/ASTContext.h"
#include <cassert>
#include <memory>

using namespace clang;

DeclGroup* DeclGroup::Create(ASTContext &C, Decl **Decls, unsigned NumDecls) {
  assert(NumDecls > 1 && "Invalid DeclGroup");
  unsigned Size = totalSizeToAlloc<Decl *>(NumDecls);
  void *Mem = C.Allocate(Size, alignof(DeclGroup));
  new (Mem) DeclGroup(NumDecls, Decls);
  return static_cast<DeclGroup*>(Mem);
}

DeclGroup::DeclGroup(unsigned numdecls, Decl** decls) : NumDecls(numdecls) {
  assert(numdecls > 0);
  assert(decls);
  std::uninitialized_copy(decls, decls + numdecls,
                          getTrailingObjects<Decl *>());
}
