//===---- CheckerHelpers.cpp - Helper functions for checkers ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines several static functions for use in checkers.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerHelpers.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include <optional>

namespace clang {

namespace ento {

// Recursively find any substatements containing macros
bool containsMacro(const Stmt *S) {
  if (S->getBeginLoc().isMacroID())
    return true;

  if (S->getEndLoc().isMacroID())
    return true;

  for (const Stmt *Child : S->children())
    if (Child && containsMacro(Child))
      return true;

  return false;
}

// Recursively find any substatements containing enum constants
bool containsEnum(const Stmt *S) {
  const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(S);

  if (DR && isa<EnumConstantDecl>(DR->getDecl()))
    return true;

  for (const Stmt *Child : S->children())
    if (Child && containsEnum(Child))
      return true;

  return false;
}

// Recursively find any substatements containing static vars
bool containsStaticLocal(const Stmt *S) {
  const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(S);

  if (DR)
    if (const VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl()))
      if (VD->isStaticLocal())
        return true;

  for (const Stmt *Child : S->children())
    if (Child && containsStaticLocal(Child))
      return true;

  return false;
}

// Recursively find any substatements containing __builtin_offsetof
bool containsBuiltinOffsetOf(const Stmt *S) {
  if (isa<OffsetOfExpr>(S))
    return true;

  for (const Stmt *Child : S->children())
    if (Child && containsBuiltinOffsetOf(Child))
      return true;

  return false;
}

// Extract lhs and rhs from assignment statement
std::pair<const clang::VarDecl *, const clang::Expr *>
parseAssignment(const Stmt *S) {
  const VarDecl *VD = nullptr;
  const Expr *RHS = nullptr;

  if (auto Assign = dyn_cast_or_null<BinaryOperator>(S)) {
    if (Assign->isAssignmentOp()) {
      // Ordinary assignment
      RHS = Assign->getRHS();
      if (auto DE = dyn_cast_or_null<DeclRefExpr>(Assign->getLHS()))
        VD = dyn_cast_or_null<VarDecl>(DE->getDecl());
    }
  } else if (auto PD = dyn_cast_or_null<DeclStmt>(S)) {
    // Initialization
    assert(PD->isSingleDecl() && "We process decls one by one");
    VD = cast<VarDecl>(PD->getSingleDecl());
    RHS = VD->getAnyInitializer();
  }

  return std::make_pair(VD, RHS);
}

Nullability getNullabilityAnnotation(QualType Type) {
  const auto *AttrType = Type->getAs<AttributedType>();
  if (!AttrType)
    return Nullability::Unspecified;
  if (AttrType->getAttrKind() == attr::TypeNullable)
    return Nullability::Nullable;
  else if (AttrType->getAttrKind() == attr::TypeNonNull)
    return Nullability::Nonnull;
  return Nullability::Unspecified;
}

std::optional<int> tryExpandAsInteger(StringRef Macro, const Preprocessor &PP) {
  const auto *MacroII = PP.getIdentifierInfo(Macro);
  if (!MacroII)
    return std::nullopt;
  const MacroInfo *MI = PP.getMacroInfo(MacroII);
  if (!MI)
    return std::nullopt;

  // Filter out parens.
  std::vector<Token> FilteredTokens;
  FilteredTokens.reserve(MI->tokens().size());
  for (auto &T : MI->tokens())
    if (!T.isOneOf(tok::l_paren, tok::r_paren))
      FilteredTokens.push_back(T);

  // Parse an integer at the end of the macro definition.
  const Token &T = FilteredTokens.back();
  // FIXME: EOF macro token coming from a PCH file on macOS while marked as
  //        literal, doesn't contain any literal data
  if (!T.isLiteral() || !T.getLiteralData())
    return std::nullopt;
  StringRef ValueStr = StringRef(T.getLiteralData(), T.getLength());
  llvm::APInt IntValue;
  constexpr unsigned AutoSenseRadix = 0;
  if (ValueStr.getAsInteger(AutoSenseRadix, IntValue))
    return std::nullopt;

  // Parse an optional minus sign.
  size_t Size = FilteredTokens.size();
  if (Size >= 2) {
    if (FilteredTokens[Size - 2].is(tok::minus))
      IntValue = -IntValue;
  }

  return IntValue.getSExtValue();
}

OperatorKind operationKindFromOverloadedOperator(OverloadedOperatorKind OOK,
                                                 bool IsBinary) {
  llvm::StringMap<BinaryOperatorKind> BinOps{
#define BINARY_OPERATION(Name, Spelling) {Spelling, BO_##Name},
#include "clang/AST/OperationKinds.def"
  };
  llvm::StringMap<UnaryOperatorKind> UnOps{
#define UNARY_OPERATION(Name, Spelling) {Spelling, UO_##Name},
#include "clang/AST/OperationKinds.def"
  };

  switch (OOK) {
#define OVERLOADED_OPERATOR(Name, Spelling, Token, Unary, Binary, MemberOnly)  \
  case OO_##Name:                                                              \
    if (IsBinary) {                                                            \
      auto BinOpIt = BinOps.find(Spelling);                                    \
      if (BinOpIt != BinOps.end())                                             \
        return OperatorKind(BinOpIt->second);                                  \
      else                                                                     \
        llvm_unreachable("operator was expected to be binary but is not");     \
    } else {                                                                   \
      auto UnOpIt = UnOps.find(Spelling);                                      \
      if (UnOpIt != UnOps.end())                                               \
        return OperatorKind(UnOpIt->second);                                   \
      else                                                                     \
        llvm_unreachable("operator was expected to be unary but is not");      \
    }                                                                          \
    break;
#include "clang/Basic/OperatorKinds.def"
  default:
    llvm_unreachable("unexpected operator kind");
  }
}

std::optional<SVal> getPointeeVal(SVal PtrSVal, ProgramStateRef State) {
  if (const auto *Ptr = PtrSVal.getAsRegion()) {
    return State->getSVal(Ptr);
  }
  return std::nullopt;
}

} // namespace ento
} // namespace clang
