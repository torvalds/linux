//===--- USRFinder.h - Clang refactoring library --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Methods for determining the USR of a symbol at a location in source
/// code.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTORING_RENAME_USRFINDER_H
#define LLVM_CLANG_TOOLING_REFACTORING_RENAME_USRFINDER_H

#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include <string>
#include <vector>

namespace clang {

class ASTContext;
class Decl;
class SourceLocation;
class NamedDecl;

namespace tooling {

// Given an AST context and a point, returns a NamedDecl identifying the symbol
// at the point. Returns null if nothing is found at the point.
const NamedDecl *getNamedDeclAt(const ASTContext &Context,
                                const SourceLocation Point);

// Given an AST context and a fully qualified name, returns a NamedDecl
// identifying the symbol with a matching name. Returns null if nothing is
// found for the name.
const NamedDecl *getNamedDeclFor(const ASTContext &Context,
                                 const std::string &Name);

// Converts a Decl into a USR.
std::string getUSRForDecl(const Decl *Decl);

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTORING_RENAME_USRFINDER_H
