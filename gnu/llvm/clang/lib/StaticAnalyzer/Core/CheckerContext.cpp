//== CheckerContext.cpp - Context info for path-sensitive checkers-----------=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines CheckerContext that provides contextual info for
//  path-sensitive checkers.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/Basic/Builtins.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringExtras.h"

using namespace clang;
using namespace ento;

const FunctionDecl *CheckerContext::getCalleeDecl(const CallExpr *CE) const {
  const FunctionDecl *D = CE->getDirectCallee();
  if (D)
    return D;

  const Expr *Callee = CE->getCallee();
  SVal L = Pred->getSVal(Callee);
  return L.getAsFunctionDecl();
}

StringRef CheckerContext::getCalleeName(const FunctionDecl *FunDecl) const {
  if (!FunDecl)
    return StringRef();
  IdentifierInfo *funI = FunDecl->getIdentifier();
  if (!funI)
    return StringRef();
  return funI->getName();
}

StringRef CheckerContext::getDeclDescription(const Decl *D) {
  if (isa<ObjCMethodDecl, CXXMethodDecl>(D))
    return "method";
  if (isa<BlockDecl>(D))
    return "anonymous block";
  return "function";
}

bool CheckerContext::isCLibraryFunction(const FunctionDecl *FD,
                                        StringRef Name) {
  // To avoid false positives (Ex: finding user defined functions with
  // similar names), only perform fuzzy name matching when it's a builtin.
  // Using a string compare is slow, we might want to switch on BuiltinID here.
  unsigned BId = FD->getBuiltinID();
  if (BId != 0) {
    if (Name.empty())
      return true;
    StringRef BName = FD->getASTContext().BuiltinInfo.getName(BId);
    size_t start = BName.find(Name);
    if (start != StringRef::npos) {
      // Accept exact match.
      if (BName.size() == Name.size())
        return true;

      //    v-- match starts here
      // ...xxxxx...
      //   _xxxxx_
      //   ^     ^ lookbehind and lookahead characters

      const auto MatchPredecessor = [=]() -> bool {
        return start <= 0 || !llvm::isAlpha(BName[start - 1]);
      };
      const auto MatchSuccessor = [=]() -> bool {
        std::size_t LookbehindPlace = start + Name.size();
        return LookbehindPlace >= BName.size() ||
               !llvm::isAlpha(BName[LookbehindPlace]);
      };

      if (MatchPredecessor() && MatchSuccessor())
        return true;
    }
  }

  const IdentifierInfo *II = FD->getIdentifier();
  // If this is a special C++ name without IdentifierInfo, it can't be a
  // C library function.
  if (!II)
    return false;

  // C library functions are either declared directly within a TU (the common
  // case) or they are accessed through the namespace `std` (when they are used
  // in C++ via headers like <cstdlib>).
  const DeclContext *DC = FD->getDeclContext()->getRedeclContext();
  if (!(DC->isTranslationUnit() || DC->isStdNamespace()))
    return false;

  // If this function is not externally visible, it is not a C library function.
  // Note that we make an exception for inline functions, which may be
  // declared in header files without external linkage.
  if (!FD->isInlined() && !FD->isExternallyVisible())
    return false;

  if (Name.empty())
    return true;

  StringRef FName = II->getName();
  if (FName == Name)
    return true;

  if (FName.starts_with("__inline") && FName.contains(Name))
    return true;

  return false;
}

bool CheckerContext::isHardenedVariantOf(const FunctionDecl *FD,
                                         StringRef Name) {
  const IdentifierInfo *II = FD->getIdentifier();
  if (!II)
    return false;

  auto CompletelyMatchesParts = [II](auto... Parts) -> bool {
    StringRef FName = II->getName();
    return (FName.consume_front(Parts) && ...) && FName.empty();
  };

  return CompletelyMatchesParts("__", Name, "_chk") ||
         CompletelyMatchesParts("__builtin_", "__", Name, "_chk");
}

StringRef CheckerContext::getMacroNameOrSpelling(SourceLocation &Loc) {
  if (Loc.isMacroID())
    return Lexer::getImmediateMacroName(Loc, getSourceManager(),
                                             getLangOpts());
  SmallString<16> buf;
  return Lexer::getSpelling(Loc, buf, getSourceManager(), getLangOpts());
}

/// Evaluate comparison and return true if it's known that condition is true
static bool evalComparison(SVal LHSVal, BinaryOperatorKind ComparisonOp,
                           SVal RHSVal, ProgramStateRef State) {
  if (LHSVal.isUnknownOrUndef())
    return false;
  ProgramStateManager &Mgr = State->getStateManager();
  if (!isa<NonLoc>(LHSVal)) {
    LHSVal = Mgr.getStoreManager().getBinding(State->getStore(),
                                              LHSVal.castAs<Loc>());
    if (LHSVal.isUnknownOrUndef() || !isa<NonLoc>(LHSVal))
      return false;
  }

  SValBuilder &Bldr = Mgr.getSValBuilder();
  SVal Eval = Bldr.evalBinOp(State, ComparisonOp, LHSVal, RHSVal,
                             Bldr.getConditionType());
  if (Eval.isUnknownOrUndef())
    return false;
  ProgramStateRef StTrue, StFalse;
  std::tie(StTrue, StFalse) = State->assume(Eval.castAs<DefinedSVal>());
  return StTrue && !StFalse;
}

bool CheckerContext::isGreaterOrEqual(const Expr *E, unsigned long long Val) {
  DefinedSVal V = getSValBuilder().makeIntVal(Val, getASTContext().LongLongTy);
  return evalComparison(getSVal(E), BO_GE, V, getState());
}

bool CheckerContext::isNegative(const Expr *E) {
  DefinedSVal V = getSValBuilder().makeIntVal(0, false);
  return evalComparison(getSVal(E), BO_LT, V, getState());
}
