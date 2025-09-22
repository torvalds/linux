//=======- ASTUtis.h ---------------------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYZER_WEBKIT_ASTUTILS_H
#define LLVM_CLANG_ANALYZER_WEBKIT_ASTUTILS_H

#include "clang/AST/Decl.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Support/Casting.h"

#include <functional>
#include <string>
#include <utility>

namespace clang {
class Expr;

/// This function de-facto defines a set of transformations that we consider
/// safe (in heuristical sense). These transformation if passed a safe value as
/// an input should provide a safe value (or an object that provides safe
/// values).
///
/// For more context see Static Analyzer checkers documentation - specifically
/// webkit.UncountedCallArgsChecker checker. Allowed list of transformations:
/// - constructors of ref-counted types (including factory methods)
/// - getters of ref-counted types
/// - member overloaded operators
/// - casts
/// - unary operators like ``&`` or ``*``
///
/// If passed expression is of type uncounted pointer/reference we try to find
/// the "origin" of the pointer value.
/// Origin can be for example a local variable, nullptr, constant or
/// this-pointer.
///
/// Certain subexpression nodes represent transformations that don't affect
/// where the memory address originates from. We try to traverse such
/// subexpressions to get to the relevant child nodes. Whenever we encounter a
/// subexpression that either can't be ignored, we don't model its semantics or
/// that has multiple children we stop.
///
/// \p E is an expression of uncounted pointer/reference type.
/// If \p StopAtFirstRefCountedObj is true and we encounter a subexpression that
/// represents ref-counted object during the traversal we return relevant
/// sub-expression and true.
///
/// Calls \p callback with the subexpression that we traversed to and if \p
/// StopAtFirstRefCountedObj is true we also specify whether we stopped early.
/// Returns false if any of calls to callbacks returned false. Otherwise true.
bool tryToFindPtrOrigin(
    const clang::Expr *E, bool StopAtFirstRefCountedObj,
    std::function<bool(const clang::Expr *, bool)> callback);

/// For \p E referring to a ref-countable/-counted pointer/reference we return
/// whether it's a safe call argument. Examples: function parameter or
/// this-pointer. The logic relies on the set of recursive rules we enforce for
/// WebKit codebase.
///
/// \returns Whether \p E is a safe call arugment.
bool isASafeCallArg(const clang::Expr *E);

/// \returns name of AST node or empty string.
template <typename T> std::string safeGetName(const T *ASTNode) {
  const auto *const ND = llvm::dyn_cast_or_null<clang::NamedDecl>(ASTNode);
  if (!ND)
    return "";

  // In case F is for example "operator|" the getName() method below would
  // assert.
  if (!ND->getDeclName().isIdentifier())
    return "";

  return ND->getName().str();
}

} // namespace clang

#endif
