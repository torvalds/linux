//===--- RangeSelector.h - Source-selection library ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
///  \file
///  Defines a combinator library supporting the definition of _selectors_,
///  which select source ranges based on (bound) AST nodes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_TRANSFORMER_RANGESELECTOR_H
#define LLVM_CLANG_TOOLING_TRANSFORMER_RANGESELECTOR_H

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Tooling/Transformer/MatchConsumer.h"
#include "llvm/Support/Error.h"
#include <functional>
#include <string>

namespace clang {
namespace transformer {
using RangeSelector = MatchConsumer<CharSourceRange>;

inline RangeSelector charRange(CharSourceRange R) {
  return [R](const ast_matchers::MatchFinder::MatchResult &)
             -> Expected<CharSourceRange> { return R; };
}

/// Selects from the start of \p Begin and to the end of \p End.
RangeSelector enclose(RangeSelector Begin, RangeSelector End);

/// Convenience version of \c range where end-points are bound nodes.
RangeSelector encloseNodes(std::string BeginID, std::string EndID);

/// DEPRECATED. Use `enclose`.
inline RangeSelector range(RangeSelector Begin, RangeSelector End) {
  return enclose(std::move(Begin), std::move(End));
}

/// DEPRECATED. Use `encloseNodes`.
inline RangeSelector range(std::string BeginID, std::string EndID) {
  return encloseNodes(std::move(BeginID), std::move(EndID));
}

/// Selects the (empty) range [B,B) when \p Selector selects the range [B,E).
RangeSelector before(RangeSelector Selector);

/// Selects the point immediately following \p Selector. That is, the
/// (empty) range [E,E), when \p Selector selects either
/// * the CharRange [B,E) or
/// * the TokenRange [B,E'] where the token at E' spans the range [E',E).
RangeSelector after(RangeSelector Selector);

/// Selects the range between `R1` and `R2.
inline RangeSelector between(RangeSelector R1, RangeSelector R2) {
  return enclose(after(std::move(R1)), before(std::move(R2)));
}

/// Selects a node, including trailing semicolon, if any (for declarations and
/// non-expression statements). \p ID is the node's binding in the match result.
RangeSelector node(std::string ID);

/// Selects a node, including trailing semicolon (always). Useful for selecting
/// expression statements. \p ID is the node's binding in the match result.
RangeSelector statement(std::string ID);

/// Given a \c MemberExpr, selects the member token. \p ID is the node's
/// binding in the match result.
RangeSelector member(std::string ID);

/// Given a node with a "name", (like \c NamedDecl, \c DeclRefExpr, \c
/// CxxCtorInitializer, and \c TypeLoc) selects the name's token.  Only selects
/// the final identifier of a qualified name, but not any qualifiers or template
/// arguments.  For example, for `::foo::bar::baz` and `::foo::bar::baz<int>`,
/// it selects only `baz`.
///
/// \param ID is the node's binding in the match result.
RangeSelector name(std::string ID);

// Given a \c CallExpr (bound to \p ID), selects the arguments' source text (all
// source between the call's parentheses).
RangeSelector callArgs(std::string ID);

// Given a \c CXXConstructExpr (bound to \p ID), selects the
// arguments' source text. Depending on the syntactic form of the construct,
// this is the range between parentheses or braces.
RangeSelector constructExprArgs(std::string ID);

// Given a \c CompoundStmt (bound to \p ID), selects the source of the
// statements (all source between the braces).
RangeSelector statements(std::string ID);

// Given a \c InitListExpr (bound to \p ID), selects the range of the elements
// (all source between the braces).
RangeSelector initListElements(std::string ID);

/// Given an \IfStmt (bound to \p ID), selects the range of the else branch,
/// starting from the \c else keyword.
RangeSelector elseBranch(std::string ID);

/// Selects the range from which `S` was expanded (possibly along with other
/// source), if `S` is an expansion, and `S` itself, otherwise.  Corresponds to
/// `SourceManager::getExpansionRange`.
RangeSelector expansion(RangeSelector S);
} // namespace transformer
} // namespace clang

#endif // LLVM_CLANG_TOOLING_TRANSFORMER_RANGESELECTOR_H
