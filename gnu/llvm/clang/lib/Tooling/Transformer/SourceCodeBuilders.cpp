//===--- SourceCodeBuilder.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Transformer/SourceCodeBuilders.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Tooling/Transformer/SourceCode.h"
#include "llvm/ADT/Twine.h"
#include <string>

using namespace clang;
using namespace tooling;

const Expr *tooling::reallyIgnoreImplicit(const Expr &E) {
  const Expr *Expr = E.IgnoreImplicit();
  if (const auto *CE = dyn_cast<CXXConstructExpr>(Expr)) {
    if (CE->getNumArgs() > 0 &&
        CE->getArg(0)->getSourceRange() == Expr->getSourceRange())
      return CE->getArg(0)->IgnoreImplicit();
  }
  return Expr;
}

bool tooling::mayEverNeedParens(const Expr &E) {
  const Expr *Expr = reallyIgnoreImplicit(E);
  // We always want parens around unary, binary, and ternary operators, because
  // they are lower precedence.
  if (isa<UnaryOperator>(Expr) || isa<BinaryOperator>(Expr) ||
      isa<AbstractConditionalOperator>(Expr))
    return true;

  // We need parens around calls to all overloaded operators except: function
  // calls, subscripts, and expressions that are already part of an (implicit)
  // call to operator->. These latter are all in the same precedence level as
  // dot/arrow and that level is left associative, so they don't need parens
  // when appearing on the left.
  if (const auto *Op = dyn_cast<CXXOperatorCallExpr>(Expr))
    return Op->getOperator() != OO_Call && Op->getOperator() != OO_Subscript &&
           Op->getOperator() != OO_Arrow;

  return false;
}

bool tooling::needParensAfterUnaryOperator(const Expr &E) {
  const Expr *Expr = reallyIgnoreImplicit(E);
  if (isa<BinaryOperator>(Expr) || isa<AbstractConditionalOperator>(Expr))
    return true;

  if (const auto *Op = dyn_cast<CXXOperatorCallExpr>(Expr))
    return Op->getNumArgs() == 2 && Op->getOperator() != OO_PlusPlus &&
           Op->getOperator() != OO_MinusMinus && Op->getOperator() != OO_Call &&
           Op->getOperator() != OO_Subscript;

  return false;
}

bool tooling::isKnownPointerLikeType(QualType Ty, ASTContext &Context) {
  using namespace ast_matchers;
  const auto PointerLikeTy = type(hasUnqualifiedDesugaredType(
      recordType(hasDeclaration(cxxRecordDecl(hasAnyName(
          "::std::unique_ptr", "::std::shared_ptr", "::std::weak_ptr",
          "::std::optional", "::absl::optional", "::llvm::Optional",
          "absl::StatusOr", "::llvm::Expected"))))));
  return match(PointerLikeTy, Ty, Context).size() > 0;
}

std::optional<std::string> tooling::buildParens(const Expr &E,
                                                const ASTContext &Context) {
  StringRef Text = getText(E, Context);
  if (Text.empty())
    return std::nullopt;
  if (mayEverNeedParens(E))
    return ("(" + Text + ")").str();
  return Text.str();
}

std::optional<std::string>
tooling::buildDereference(const Expr &E, const ASTContext &Context) {
  if (const auto *Op = dyn_cast<UnaryOperator>(&E))
    if (Op->getOpcode() == UO_AddrOf) {
      // Strip leading '&'.
      StringRef Text =
          getText(*Op->getSubExpr()->IgnoreParenImpCasts(), Context);
      if (Text.empty())
        return std::nullopt;
      return Text.str();
    }

  StringRef Text = getText(E, Context);
  if (Text.empty())
    return std::nullopt;
  // Add leading '*'.
  if (needParensAfterUnaryOperator(E))
    return ("*(" + Text + ")").str();
  return ("*" + Text).str();
}

std::optional<std::string> tooling::buildAddressOf(const Expr &E,
                                                   const ASTContext &Context) {
  if (E.isImplicitCXXThis())
    return std::string("this");
  if (const auto *Op = dyn_cast<UnaryOperator>(&E))
    if (Op->getOpcode() == UO_Deref) {
      // Strip leading '*'.
      StringRef Text =
          getText(*Op->getSubExpr()->IgnoreParenImpCasts(), Context);
      if (Text.empty())
        return std::nullopt;
      return Text.str();
    }
  // Add leading '&'.
  StringRef Text = getText(E, Context);
  if (Text.empty())
    return std::nullopt;
  if (needParensAfterUnaryOperator(E)) {
    return ("&(" + Text + ")").str();
  }
  return ("&" + Text).str();
}

// Append the appropriate access operation (syntactically) to `E`, assuming `E`
// is a non-pointer value.
static std::optional<std::string>
buildAccessForValue(const Expr &E, const ASTContext &Context) {
  if (const auto *Op = llvm::dyn_cast<UnaryOperator>(&E))
    if (Op->getOpcode() == UO_Deref) {
      // Strip leading '*', add following '->'.
      const Expr *SubExpr = Op->getSubExpr()->IgnoreParenImpCasts();
      StringRef DerefText = getText(*SubExpr, Context);
      if (DerefText.empty())
        return std::nullopt;
      if (needParensBeforeDotOrArrow(*SubExpr))
        return ("(" + DerefText + ")->").str();
      return (DerefText + "->").str();
    }

  // Add following '.'.
  StringRef Text = getText(E, Context);
  if (Text.empty())
    return std::nullopt;
  if (needParensBeforeDotOrArrow(E)) {
    return ("(" + Text + ").").str();
  }
  return (Text + ".").str();
}

// Append the appropriate access operation (syntactically) to `E`, assuming `E`
// is a pointer value.
static std::optional<std::string>
buildAccessForPointer(const Expr &E, const ASTContext &Context) {
  if (const auto *Op = llvm::dyn_cast<UnaryOperator>(&E))
    if (Op->getOpcode() == UO_AddrOf) {
      // Strip leading '&', add following '.'.
      const Expr *SubExpr = Op->getSubExpr()->IgnoreParenImpCasts();
      StringRef DerefText = getText(*SubExpr, Context);
      if (DerefText.empty())
        return std::nullopt;
      if (needParensBeforeDotOrArrow(*SubExpr))
        return ("(" + DerefText + ").").str();
      return (DerefText + ".").str();
    }

  // Add following '->'.
  StringRef Text = getText(E, Context);
  if (Text.empty())
    return std::nullopt;
  if (needParensBeforeDotOrArrow(E))
    return ("(" + Text + ")->").str();
  return (Text + "->").str();
}

std::optional<std::string> tooling::buildDot(const Expr &E,
                                             const ASTContext &Context) {
  return buildAccessForValue(E, Context);
}

std::optional<std::string> tooling::buildArrow(const Expr &E,
                                               const ASTContext &Context) {
  return buildAccessForPointer(E, Context);
}

// If `E` is an overloaded-operator call of kind `K` on an object `O`, returns
// `O`. Otherwise, returns `nullptr`.
static const Expr *maybeGetOperatorObjectArg(const Expr &E,
                                             OverloadedOperatorKind K) {
  if (const auto *OpCall = dyn_cast<clang::CXXOperatorCallExpr>(&E)) {
    if (OpCall->getOperator() == K && OpCall->getNumArgs() == 1)
      return OpCall->getArg(0);
  }
  return nullptr;
}

static bool treatLikePointer(QualType Ty, PLTClass C, ASTContext &Context) {
  switch (C) {
  case PLTClass::Value:
    return false;
  case PLTClass::Pointer:
    return isKnownPointerLikeType(Ty, Context);
  }
  llvm_unreachable("Unknown PLTClass enum");
}

// FIXME: move over the other `maybe` functionality from Stencil. Should all be
// in one place.
std::optional<std::string> tooling::buildAccess(const Expr &RawExpression,
                                                ASTContext &Context,
                                                PLTClass Classification) {
  if (RawExpression.isImplicitCXXThis())
    // Return the empty string, because `std::nullopt` signifies some sort of
    // failure.
    return std::string();

  const Expr *E = RawExpression.IgnoreImplicitAsWritten();

  if (E->getType()->isAnyPointerType() ||
      treatLikePointer(E->getType(), Classification, Context)) {
    // Strip off operator-> calls. They can only occur inside an actual arrow
    // member access, so we treat them as equivalent to an actual object
    // expression.
    if (const auto *Obj = maybeGetOperatorObjectArg(*E, clang::OO_Arrow))
      E = Obj;
    return buildAccessForPointer(*E, Context);
  }

  if (const auto *Obj = maybeGetOperatorObjectArg(*E, clang::OO_Star)) {
    if (treatLikePointer(Obj->getType(), Classification, Context))
      return buildAccessForPointer(*Obj, Context);
  };

  return buildAccessForValue(*E, Context);
}
