//=======- PtrTypesSemantics.cpp ---------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PtrTypesSemantics.h"
#include "ASTUtils.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtVisitor.h"
#include <optional>

using namespace clang;

namespace {

bool hasPublicMethodInBaseClass(const CXXRecordDecl *R,
                                const char *NameToMatch) {
  assert(R);
  assert(R->hasDefinition());

  for (const CXXMethodDecl *MD : R->methods()) {
    const auto MethodName = safeGetName(MD);
    if (MethodName == NameToMatch && MD->getAccess() == AS_public)
      return true;
  }
  return false;
}

} // namespace

namespace clang {

std::optional<const clang::CXXRecordDecl *>
hasPublicMethodInBase(const CXXBaseSpecifier *Base, const char *NameToMatch) {
  assert(Base);

  const Type *T = Base->getType().getTypePtrOrNull();
  if (!T)
    return std::nullopt;

  const CXXRecordDecl *R = T->getAsCXXRecordDecl();
  if (!R)
    return std::nullopt;
  if (!R->hasDefinition())
    return std::nullopt;

  return hasPublicMethodInBaseClass(R, NameToMatch) ? R : nullptr;
}

std::optional<bool> isRefCountable(const CXXRecordDecl* R)
{
  assert(R);

  R = R->getDefinition();
  if (!R)
    return std::nullopt;

  bool hasRef = hasPublicMethodInBaseClass(R, "ref");
  bool hasDeref = hasPublicMethodInBaseClass(R, "deref");
  if (hasRef && hasDeref)
    return true;

  CXXBasePaths Paths;
  Paths.setOrigin(const_cast<CXXRecordDecl *>(R));

  bool AnyInconclusiveBase = false;
  const auto hasPublicRefInBase =
      [&AnyInconclusiveBase](const CXXBaseSpecifier *Base, CXXBasePath &) {
        auto hasRefInBase = clang::hasPublicMethodInBase(Base, "ref");
        if (!hasRefInBase) {
          AnyInconclusiveBase = true;
          return false;
        }
        return (*hasRefInBase) != nullptr;
      };

  hasRef = hasRef || R->lookupInBases(hasPublicRefInBase, Paths,
                                      /*LookupInDependent =*/true);
  if (AnyInconclusiveBase)
    return std::nullopt;

  Paths.clear();
  const auto hasPublicDerefInBase =
      [&AnyInconclusiveBase](const CXXBaseSpecifier *Base, CXXBasePath &) {
        auto hasDerefInBase = clang::hasPublicMethodInBase(Base, "deref");
        if (!hasDerefInBase) {
          AnyInconclusiveBase = true;
          return false;
        }
        return (*hasDerefInBase) != nullptr;
      };
  hasDeref = hasDeref || R->lookupInBases(hasPublicDerefInBase, Paths,
                                          /*LookupInDependent =*/true);
  if (AnyInconclusiveBase)
    return std::nullopt;

  return hasRef && hasDeref;
}

bool isRefType(const std::string &Name) {
  return Name == "Ref" || Name == "RefAllowingPartiallyDestroyed" ||
         Name == "RefPtr" || Name == "RefPtrAllowingPartiallyDestroyed";
}

bool isCtorOfRefCounted(const clang::FunctionDecl *F) {
  assert(F);
  const std::string &FunctionName = safeGetName(F);

  return isRefType(FunctionName) || FunctionName == "makeRef" ||
         FunctionName == "makeRefPtr" || FunctionName == "UniqueRef" ||
         FunctionName == "makeUniqueRef" ||
         FunctionName == "makeUniqueRefWithoutFastMallocCheck"

         || FunctionName == "String" || FunctionName == "AtomString" ||
         FunctionName == "UniqueString"
         // FIXME: Implement as attribute.
         || FunctionName == "Identifier";
}

bool isReturnValueRefCounted(const clang::FunctionDecl *F) {
  assert(F);
  QualType type = F->getReturnType();
  while (!type.isNull()) {
    if (auto *elaboratedT = type->getAs<ElaboratedType>()) {
      type = elaboratedT->desugar();
      continue;
    }
    if (auto *specialT = type->getAs<TemplateSpecializationType>()) {
      if (auto *decl = specialT->getTemplateName().getAsTemplateDecl()) {
        auto name = decl->getNameAsString();
        return isRefType(name);
      }
      return false;
    }
    return false;
  }
  return false;
}

std::optional<bool> isUncounted(const CXXRecordDecl* Class)
{
  // Keep isRefCounted first as it's cheaper.
  if (isRefCounted(Class))
    return false;

  std::optional<bool> IsRefCountable = isRefCountable(Class);
  if (!IsRefCountable)
    return std::nullopt;

  return (*IsRefCountable);
}

std::optional<bool> isUncountedPtr(const Type* T)
{
  assert(T);

  if (T->isPointerType() || T->isReferenceType()) {
    if (auto *CXXRD = T->getPointeeCXXRecordDecl()) {
      return isUncounted(CXXRD);
    }
  }
  return false;
}

std::optional<bool> isGetterOfRefCounted(const CXXMethodDecl* M)
{
  assert(M);

  if (isa<CXXMethodDecl>(M)) {
    const CXXRecordDecl *calleeMethodsClass = M->getParent();
    auto className = safeGetName(calleeMethodsClass);
    auto method = safeGetName(M);

    if ((isRefType(className) && (method == "get" || method == "ptr")) ||
        ((className == "String" || className == "AtomString" ||
          className == "AtomStringImpl" || className == "UniqueString" ||
          className == "UniqueStringImpl" || className == "Identifier") &&
         method == "impl"))
      return true;

    // Ref<T> -> T conversion
    // FIXME: Currently allowing any Ref<T> -> whatever cast.
    if (isRefType(className)) {
      if (auto *maybeRefToRawOperator = dyn_cast<CXXConversionDecl>(M)) {
        if (auto *targetConversionType =
                maybeRefToRawOperator->getConversionType().getTypePtrOrNull()) {
          return isUncountedPtr(targetConversionType);
        }
      }
    }
  }
  return false;
}

bool isRefCounted(const CXXRecordDecl *R) {
  assert(R);
  if (auto *TmplR = R->getTemplateInstantiationPattern()) {
    // FIXME: String/AtomString/UniqueString
    const auto &ClassName = safeGetName(TmplR);
    return isRefType(ClassName);
  }
  return false;
}

bool isPtrConversion(const FunctionDecl *F) {
  assert(F);
  if (isCtorOfRefCounted(F))
    return true;

  // FIXME: check # of params == 1
  const auto FunctionName = safeGetName(F);
  if (FunctionName == "getPtr" || FunctionName == "WeakPtr" ||
      FunctionName == "dynamicDowncast" || FunctionName == "downcast" ||
      FunctionName == "checkedDowncast" ||
      FunctionName == "uncheckedDowncast" || FunctionName == "bitwise_cast")
    return true;

  return false;
}

bool isSingleton(const FunctionDecl *F) {
  assert(F);
  // FIXME: check # of params == 1
  if (auto *MethodDecl = dyn_cast<CXXMethodDecl>(F)) {
    if (!MethodDecl->isStatic())
      return false;
  }
  const auto &Name = safeGetName(F);
  std::string SingletonStr = "singleton";
  auto index = Name.find(SingletonStr);
  return index != std::string::npos &&
         index == Name.size() - SingletonStr.size();
}

// We only care about statements so let's use the simple
// (non-recursive) visitor.
class TrivialFunctionAnalysisVisitor
    : public ConstStmtVisitor<TrivialFunctionAnalysisVisitor, bool> {

  // Returns false if at least one child is non-trivial.
  bool VisitChildren(const Stmt *S) {
    for (const Stmt *Child : S->children()) {
      if (Child && !Visit(Child))
        return false;
    }

    return true;
  }

  template <typename CheckFunction>
  bool WithCachedResult(const Stmt *S, CheckFunction Function) {
    // If the statement isn't in the cache, conservatively assume that
    // it's not trivial until analysis completes. Insert false to the cache
    // first to avoid infinite recursion.
    auto [It, IsNew] = Cache.insert(std::make_pair(S, false));
    if (!IsNew)
      return It->second;
    bool Result = Function();
    Cache[S] = Result;
    return Result;
  }

public:
  using CacheTy = TrivialFunctionAnalysis::CacheTy;

  TrivialFunctionAnalysisVisitor(CacheTy &Cache) : Cache(Cache) {}

  bool IsFunctionTrivial(const Decl *D) {
    auto CacheIt = Cache.find(D);
    if (CacheIt != Cache.end())
      return CacheIt->second;

    // Treat a recursive function call to be trivial until proven otherwise.
    auto [RecursiveIt, IsNew] = RecursiveFn.insert(std::make_pair(D, true));
    if (!IsNew)
      return RecursiveIt->second;

    bool Result = [&]() {
      if (auto *CtorDecl = dyn_cast<CXXConstructorDecl>(D)) {
        for (auto *CtorInit : CtorDecl->inits()) {
          if (!Visit(CtorInit->getInit()))
            return false;
        }
      }
      const Stmt *Body = D->getBody();
      if (!Body)
        return false;
      return Visit(Body);
    }();

    if (!Result) {
      // D and its mutually recursive callers are all non-trivial.
      for (auto &It : RecursiveFn)
        It.second = false;
    }
    RecursiveIt = RecursiveFn.find(D);
    assert(RecursiveIt != RecursiveFn.end());
    Result = RecursiveIt->second;
    RecursiveFn.erase(RecursiveIt);
    Cache[D] = Result;

    return Result;
  }

  bool VisitStmt(const Stmt *S) {
    // All statements are non-trivial unless overriden later.
    // Don't even recurse into children by default.
    return false;
  }

  bool VisitCompoundStmt(const CompoundStmt *CS) {
    // A compound statement is allowed as long each individual sub-statement
    // is trivial.
    return WithCachedResult(CS, [&]() { return VisitChildren(CS); });
  }

  bool VisitReturnStmt(const ReturnStmt *RS) {
    // A return statement is allowed as long as the return value is trivial.
    if (auto *RV = RS->getRetValue())
      return Visit(RV);
    return true;
  }

  bool VisitDeclStmt(const DeclStmt *DS) { return VisitChildren(DS); }
  bool VisitDoStmt(const DoStmt *DS) { return VisitChildren(DS); }
  bool VisitIfStmt(const IfStmt *IS) {
    return WithCachedResult(IS, [&]() { return VisitChildren(IS); });
  }
  bool VisitForStmt(const ForStmt *FS) {
    return WithCachedResult(FS, [&]() { return VisitChildren(FS); });
  }
  bool VisitCXXForRangeStmt(const CXXForRangeStmt *FS) {
    return WithCachedResult(FS, [&]() { return VisitChildren(FS); });
  }
  bool VisitWhileStmt(const WhileStmt *WS) {
    return WithCachedResult(WS, [&]() { return VisitChildren(WS); });
  }
  bool VisitSwitchStmt(const SwitchStmt *SS) { return VisitChildren(SS); }
  bool VisitCaseStmt(const CaseStmt *CS) { return VisitChildren(CS); }
  bool VisitDefaultStmt(const DefaultStmt *DS) { return VisitChildren(DS); }

  // break, continue, goto, and label statements are always trivial.
  bool VisitBreakStmt(const BreakStmt *) { return true; }
  bool VisitContinueStmt(const ContinueStmt *) { return true; }
  bool VisitGotoStmt(const GotoStmt *) { return true; }
  bool VisitLabelStmt(const LabelStmt *) { return true; }

  bool VisitUnaryOperator(const UnaryOperator *UO) {
    // Unary operators are trivial if its operand is trivial except co_await.
    return UO->getOpcode() != UO_Coawait && Visit(UO->getSubExpr());
  }

  bool VisitBinaryOperator(const BinaryOperator *BO) {
    // Binary operators are trivial if their operands are trivial.
    return Visit(BO->getLHS()) && Visit(BO->getRHS());
  }

  bool VisitCompoundAssignOperator(const CompoundAssignOperator *CAO) {
    // Compound assignment operator such as |= is trivial if its
    // subexpresssions are trivial.
    return VisitChildren(CAO);
  }

  bool VisitArraySubscriptExpr(const ArraySubscriptExpr *ASE) {
    return VisitChildren(ASE);
  }

  bool VisitConditionalOperator(const ConditionalOperator *CO) {
    // Ternary operators are trivial if their conditions & values are trivial.
    return VisitChildren(CO);
  }

  bool VisitAtomicExpr(const AtomicExpr *E) { return VisitChildren(E); }

  bool VisitStaticAssertDecl(const StaticAssertDecl *SAD) {
    // Any static_assert is considered trivial.
    return true;
  }

  bool VisitCallExpr(const CallExpr *CE) {
    if (!checkArguments(CE))
      return false;

    auto *Callee = CE->getDirectCallee();
    if (!Callee)
      return false;
    const auto &Name = safeGetName(Callee);

    if (Callee->isInStdNamespace() &&
        (Name == "addressof" || Name == "forward" || Name == "move"))
      return true;

    if (Name == "WTFCrashWithInfo" || Name == "WTFBreakpointTrap" ||
        Name == "WTFCrashWithSecurityImplication" || Name == "WTFCrash" ||
        Name == "WTFReportAssertionFailure" || Name == "isMainThread" ||
        Name == "isMainThreadOrGCThread" || Name == "isMainRunLoop" ||
        Name == "isWebThread" || Name == "isUIThread" ||
        Name == "mayBeGCThread" || Name == "compilerFenceForCrash" ||
        Name == "bitwise_cast" || Name.find("__builtin") == 0)
      return true;

    return IsFunctionTrivial(Callee);
  }

  bool
  VisitSubstNonTypeTemplateParmExpr(const SubstNonTypeTemplateParmExpr *E) {
    // Non-type template paramter is compile time constant and trivial.
    return true;
  }

  bool VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *E) {
    return VisitChildren(E);
  }

  bool VisitPredefinedExpr(const PredefinedExpr *E) {
    // A predefined identifier such as "func" is considered trivial.
    return true;
  }

  bool VisitCXXMemberCallExpr(const CXXMemberCallExpr *MCE) {
    if (!checkArguments(MCE))
      return false;

    bool TrivialThis = Visit(MCE->getImplicitObjectArgument());
    if (!TrivialThis)
      return false;

    auto *Callee = MCE->getMethodDecl();
    if (!Callee)
      return false;

    std::optional<bool> IsGetterOfRefCounted = isGetterOfRefCounted(Callee);
    if (IsGetterOfRefCounted && *IsGetterOfRefCounted)
      return true;

    // Recursively descend into the callee to confirm that it's trivial as well.
    return IsFunctionTrivial(Callee);
  }

  bool VisitCXXOperatorCallExpr(const CXXOperatorCallExpr *OCE) {
    if (!checkArguments(OCE))
      return false;
    auto *Callee = OCE->getCalleeDecl();
    if (!Callee)
      return false;
    // Recursively descend into the callee to confirm that it's trivial as well.
    return IsFunctionTrivial(Callee);
  }

  bool VisitCXXDefaultArgExpr(const CXXDefaultArgExpr *E) {
    if (auto *Expr = E->getExpr()) {
      if (!Visit(Expr))
        return false;
    }
    return true;
  }

  bool checkArguments(const CallExpr *CE) {
    for (const Expr *Arg : CE->arguments()) {
      if (Arg && !Visit(Arg))
        return false;
    }
    return true;
  }

  bool VisitCXXConstructExpr(const CXXConstructExpr *CE) {
    for (const Expr *Arg : CE->arguments()) {
      if (Arg && !Visit(Arg))
        return false;
    }

    // Recursively descend into the callee to confirm that it's trivial.
    return IsFunctionTrivial(CE->getConstructor());
  }

  bool VisitCXXNewExpr(const CXXNewExpr *NE) { return VisitChildren(NE); }

  bool VisitImplicitCastExpr(const ImplicitCastExpr *ICE) {
    return Visit(ICE->getSubExpr());
  }

  bool VisitExplicitCastExpr(const ExplicitCastExpr *ECE) {
    return Visit(ECE->getSubExpr());
  }

  bool VisitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *VMT) {
    return Visit(VMT->getSubExpr());
  }

  bool VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *BTE) {
    if (auto *Temp = BTE->getTemporary()) {
      if (!TrivialFunctionAnalysis::isTrivialImpl(Temp->getDestructor(), Cache))
        return false;
    }
    return Visit(BTE->getSubExpr());
  }

  bool VisitExprWithCleanups(const ExprWithCleanups *EWC) {
    return Visit(EWC->getSubExpr());
  }

  bool VisitParenExpr(const ParenExpr *PE) { return Visit(PE->getSubExpr()); }

  bool VisitInitListExpr(const InitListExpr *ILE) {
    for (const Expr *Child : ILE->inits()) {
      if (Child && !Visit(Child))
        return false;
    }
    return true;
  }

  bool VisitMemberExpr(const MemberExpr *ME) {
    // Field access is allowed but the base pointer may itself be non-trivial.
    return Visit(ME->getBase());
  }

  bool VisitCXXThisExpr(const CXXThisExpr *CTE) {
    // The expression 'this' is always trivial, be it explicit or implicit.
    return true;
  }

  bool VisitCXXNullPtrLiteralExpr(const CXXNullPtrLiteralExpr *E) {
    // nullptr is trivial.
    return true;
  }

  bool VisitDeclRefExpr(const DeclRefExpr *DRE) {
    // The use of a variable is trivial.
    return true;
  }

  // Constant literal expressions are always trivial
  bool VisitIntegerLiteral(const IntegerLiteral *E) { return true; }
  bool VisitFloatingLiteral(const FloatingLiteral *E) { return true; }
  bool VisitFixedPointLiteral(const FixedPointLiteral *E) { return true; }
  bool VisitCharacterLiteral(const CharacterLiteral *E) { return true; }
  bool VisitStringLiteral(const StringLiteral *E) { return true; }
  bool VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *E) { return true; }

  bool VisitConstantExpr(const ConstantExpr *CE) {
    // Constant expressions are trivial.
    return true;
  }

private:
  CacheTy &Cache;
  CacheTy RecursiveFn;
};

bool TrivialFunctionAnalysis::isTrivialImpl(
    const Decl *D, TrivialFunctionAnalysis::CacheTy &Cache) {
  TrivialFunctionAnalysisVisitor V(Cache);
  return V.IsFunctionTrivial(D);
}

bool TrivialFunctionAnalysis::isTrivialImpl(
    const Stmt *S, TrivialFunctionAnalysis::CacheTy &Cache) {
  // If the statement isn't in the cache, conservatively assume that
  // it's not trivial until analysis completes. Unlike a function case,
  // we don't insert an entry into the cache until Visit returns
  // since Visit* functions themselves make use of the cache.

  TrivialFunctionAnalysisVisitor V(Cache);
  bool Result = V.Visit(S);
  assert(Cache.contains(S) && "Top-level statement not properly cached!");
  return Result;
}

} // namespace clang
