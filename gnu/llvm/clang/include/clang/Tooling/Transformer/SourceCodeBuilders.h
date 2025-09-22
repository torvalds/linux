//===--- SourceCodeBuilders.h - Source-code building facilities -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file collects facilities for generating source code strings.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_TRANSFORMER_SOURCECODEBUILDERS_H
#define LLVM_CLANG_TOOLING_TRANSFORMER_SOURCECODEBUILDERS_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include <string>

namespace clang {
namespace tooling {

/// \name Code analysis utilities.
/// @{
/// Ignores implicit object-construction expressions in addition to the normal
/// implicit expressions that are ignored.
const Expr *reallyIgnoreImplicit(const Expr &E);

/// Determines whether printing this expression in *any* expression requires
/// parentheses to preserve its meaning. This analyses is necessarily
/// conservative because it lacks information about the target context.
bool mayEverNeedParens(const Expr &E);

/// Determines whether printing this expression to the left of a dot or arrow
/// operator requires a parentheses to preserve its meaning. Given that
/// dot/arrow are (effectively) the highest precedence, this is equivalent to
/// asking whether it ever needs parens.
inline bool needParensBeforeDotOrArrow(const Expr &E) {
  return mayEverNeedParens(E);
}

/// Determines whether printing this expression to the right of a unary operator
/// requires a parentheses to preserve its meaning.
bool needParensAfterUnaryOperator(const Expr &E);

// Recognizes known types (and sugared versions thereof) that overload the `*`
// and `->` operator. Below is the list of currently included types, but it is
// subject to change:
//
// * std::unique_ptr, std::shared_ptr, std::weak_ptr,
// * std::optional, absl::optional, llvm::Optional,
// * absl::StatusOr, llvm::Expected.
bool isKnownPointerLikeType(QualType Ty, ASTContext &Context);
/// @}

/// \name Basic code-string generation utilities.
/// @{

/// Builds source for an expression, adding parens if needed for unambiguous
/// parsing.
std::optional<std::string> buildParens(const Expr &E,
                                       const ASTContext &Context);

/// Builds idiomatic source for the dereferencing of `E`: prefix with `*` but
/// simplify when it already begins with `&`.  \returns empty string on failure.
std::optional<std::string> buildDereference(const Expr &E,
                                            const ASTContext &Context);

/// Builds idiomatic source for taking the address of `E`: prefix with `&` but
/// simplify when it already begins with `*`.  \returns empty string on failure.
std::optional<std::string> buildAddressOf(const Expr &E,
                                          const ASTContext &Context);

/// Adds a dot to the end of the given expression, but adds parentheses when
/// needed by the syntax, and simplifies to `->` when possible, e.g.:
///
///  `x` becomes `x.`
///  `*a` becomes `a->`
///  `a+b` becomes `(a+b).`
///
/// DEPRECATED. Use `buildAccess`.
std::optional<std::string> buildDot(const Expr &E, const ASTContext &Context);

/// Adds an arrow to the end of the given expression, but adds parentheses
/// when needed by the syntax, and simplifies to `.` when possible, e.g.:
///
///  `x` becomes `x->`
///  `&a` becomes `a.`
///  `a+b` becomes `(a+b)->`
///
/// DEPRECATED. Use `buildAccess`.
std::optional<std::string> buildArrow(const Expr &E, const ASTContext &Context);

/// Specifies how to classify pointer-like types -- like values or like pointers
/// -- with regard to generating member-access syntax.
enum class PLTClass : bool {
  Value,
  Pointer,
};

/// Adds an appropriate access operator (`.`, `->` or nothing, in the case of
/// implicit `this`) to the end of the given expression. Adds parentheses when
/// needed by the syntax and simplifies when possible. If `PLTypeClass` is
/// `Pointer`, for known pointer-like types (see `isKnownPointerLikeType`),
/// treats `operator->` and `operator*` like the built-in `->` and `*`
/// operators.
///
///  `x` becomes `x->` or `x.`, depending on `E`'s type
///  `a+b` becomes `(a+b)->` or `(a+b).`, depending on `E`'s type
///  `&a` becomes `a.`
///  `*a` becomes `a->`
std::optional<std::string>
buildAccess(const Expr &E, ASTContext &Context,
            PLTClass Classification = PLTClass::Pointer);
/// @}

} // namespace tooling
} // namespace clang
#endif // LLVM_CLANG_TOOLING_TRANSFORMER_SOURCECODEBUILDERS_H
