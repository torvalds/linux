//===--- Stencil.h - Stencil class ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the *Stencil* abstraction: a code-generating object,
/// parameterized by named references to (bound) AST nodes.  Given a match
/// result, a stencil can be evaluated to a string of source code.
///
/// A stencil is similar in spirit to a format string: it is composed of a
/// series of raw text strings, references to nodes (the parameters) and helper
/// code-generation operations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_TRANSFORMER_STENCIL_H_
#define LLVM_CLANG_TOOLING_TRANSFORMER_STENCIL_H_

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Transformer/MatchConsumer.h"
#include "clang/Tooling/Transformer/RangeSelector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace clang {
namespace transformer {

using StencilInterface = MatchComputation<std::string>;

/// A sequence of code fragments, references to parameters and code-generation
/// operations that together can be evaluated to (a fragment of) source code or
/// a diagnostic message, given a match result.
///
/// We use a `shared_ptr` to allow for easy and cheap copying of stencils.
/// Since `StencilInterface` is an immutable interface, the sharing doesn't
/// impose any risks. Otherwise, we would have to add a virtual `copy` method to
/// the API and implement it for all derived classes.
using Stencil = std::shared_ptr<StencilInterface>;

namespace detail {
/// Convenience function to construct a \c Stencil. Overloaded for common cases
/// so that user doesn't need to specify which factory function to use. This
/// pattern gives benefits similar to implicit constructors, while maintaing a
/// higher degree of explicitness.
Stencil makeStencil(llvm::StringRef Text);
Stencil makeStencil(RangeSelector Selector);
inline Stencil makeStencil(Stencil S) { return S; }
} // namespace detail

/// Constructs the string representing the concatenation of the given \p
/// Parts. If only one element is passed in \p Parts, returns that element.
Stencil catVector(std::vector<Stencil> Parts);

/// Concatenates 0+ stencil pieces into a single stencil. Arguments can be raw
/// text, ranges in the matched code (\p RangeSelector) or other `Stencil`s.
template <typename... Ts> Stencil cat(Ts &&... Parts) {
  return catVector({detail::makeStencil(std::forward<Ts>(Parts))...});
}

//
// Functions for conveniently building stencils.
//

/// Generates the source of the expression bound to \p Id, wrapping it in
/// parentheses if it may parse differently depending on context. For example, a
/// binary operation is always wrapped, while a variable reference is never
/// wrapped.
Stencil expression(llvm::StringRef Id);

/// Constructs an idiomatic dereferencing of the expression bound to \p ExprId.
/// \p ExprId is wrapped in parentheses, if needed.
Stencil deref(llvm::StringRef ExprId);

/// If \p ExprId is of pointer type, constructs an idiomatic dereferencing of
/// the expression bound to \p ExprId, including wrapping it in parentheses, if
/// needed. Otherwise, generates the original expression source.
Stencil maybeDeref(llvm::StringRef ExprId);

/// Constructs an expression that idiomatically takes the address of the
/// expression bound to \p ExprId. \p ExprId is wrapped in parentheses, if
/// needed.
Stencil addressOf(llvm::StringRef ExprId);

/// If \p ExprId is not a pointer type, constructs an expression that
/// idiomatically takes the address of the expression bound to \p ExprId,
/// including wrapping \p ExprId in parentheses, if needed. Otherwise, generates
/// the original expression source.
Stencil maybeAddressOf(llvm::StringRef ExprId);

/// Constructs a `MemberExpr` that accesses the named member (\p Member) of the
/// object bound to \p BaseId. The access is constructed idiomatically: if \p
/// BaseId is bound to `e` and \p Member identifies member `m`, then returns
/// `e->m`, when e is a pointer, `e2->m` when e = `*e2` and `e.m` otherwise.
/// Additionally, `e` is wrapped in parentheses, if needed.
Stencil access(llvm::StringRef BaseId, Stencil Member);
inline Stencil access(llvm::StringRef BaseId, llvm::StringRef Member) {
  return access(BaseId, detail::makeStencil(Member));
}

/// Chooses between the two stencil parts, based on whether \p ID is bound in
/// the match.
Stencil ifBound(llvm::StringRef Id, Stencil TrueStencil, Stencil FalseStencil);

/// Chooses between the two strings, based on whether \p ID is bound in the
/// match.
inline Stencil ifBound(llvm::StringRef Id, llvm::StringRef TrueText,
                       llvm::StringRef FalseText) {
  return ifBound(Id, detail::makeStencil(TrueText),
                 detail::makeStencil(FalseText));
}

/// Chooses between multiple stencils, based on the presence of bound nodes. \p
/// CaseStencils takes a vector of (ID, \c Stencil) pairs and checks each ID in
/// order to see if it's bound to a node.  If so, the associated \c Stencil is
/// run and all other cases are ignored.  An optional \p DefaultStencil can be
/// provided to be run if all cases are exhausted beacause none of the provided
/// IDs are bound.  If no default case is provided and all cases are exhausted,
/// the stencil will fail with error `llvm::errc::result_out_of_range`.
///
/// For example, say one matches a statement's type with:
///     anyOf(
///       qualType(isInteger()).bind("int"),
///       qualType(realFloatingPointType()).bind("float"),
///       qualType(isAnyCharacter()).bind("char"),
///       booleanType().bind("bool"))
///
/// Then, one can decide in a stencil how to construct a literal.
///     cat("a = ",
///         selectBound(
///             {{"int", cat("0")},
///              {"float", cat("0.0")},
///              {"char", cat("'\\0'")},
///              {"bool", cat("false")}}))
///
/// In addition, one could supply a default case for all other types:
///     selectBound(
///         {{"int", cat("0")},
///          ...
///          {"bool", cat("false")}},
///         cat("{}"))
Stencil selectBound(std::vector<std::pair<std::string, Stencil>> CaseStencils,
                    Stencil DefaultStencil = nullptr);

/// Wraps a \c MatchConsumer in a \c Stencil, so that it can be used in a \c
/// Stencil.  This supports user-defined extensions to the \c Stencil language.
Stencil run(MatchConsumer<std::string> C);

/// Produces a human-readable rendering of the node bound to `Id`, suitable for
/// diagnostics and debugging. This operator can be applied to any node, but is
/// targeted at those whose source cannot be printed directly, including:
///
/// * Types. represented based on their structure. Note that namespace
///   qualifiers are always printed, with the anonymous namespace represented
///   explicitly. No desugaring or canonicalization is applied.
Stencil describe(llvm::StringRef Id);

/// For debug use only; semantics are not guaranteed.
///
/// \returns the string resulting from calling the node's print() method.
Stencil dPrint(llvm::StringRef Id);
} // namespace transformer
} // namespace clang
#endif // LLVM_CLANG_TOOLING_TRANSFORMER_STENCIL_H_
