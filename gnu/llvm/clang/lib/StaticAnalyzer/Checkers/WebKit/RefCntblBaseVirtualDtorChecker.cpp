//=======- RefCntblBaseVirtualDtor.cpp ---------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ASTUtils.h"
#include "DiagOutputUtils.h"
#include "PtrTypesSemantics.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include <optional>

using namespace clang;
using namespace ento;

namespace {

class DerefFuncDeleteExprVisitor
    : public ConstStmtVisitor<DerefFuncDeleteExprVisitor, bool> {
  // Returns true if any of child statements return true.
  bool VisitChildren(const Stmt *S) {
    for (const Stmt *Child : S->children()) {
      if (Child && Visit(Child))
        return true;
    }
    return false;
  }

  bool VisitBody(const Stmt *Body) {
    if (!Body)
      return false;

    auto [It, IsNew] = VisitedBody.insert(Body);
    if (!IsNew) // This body is recursive
      return false;

    return Visit(Body);
  }

public:
  DerefFuncDeleteExprVisitor(const TemplateArgumentList &ArgList,
                             const CXXRecordDecl *ClassDecl)
      : ArgList(&ArgList), ClassDecl(ClassDecl) {}

  DerefFuncDeleteExprVisitor(const CXXRecordDecl *ClassDecl)
      : ClassDecl(ClassDecl) {}

  std::optional<bool> HasSpecializedDelete(CXXMethodDecl *Decl) {
    if (auto *Body = Decl->getBody())
      return VisitBody(Body);
    if (Decl->getTemplateInstantiationPattern())
      return std::nullopt; // Indeterminate. There was no concrete instance.
    return false;
  }

  bool VisitCallExpr(const CallExpr *CE) {
    const Decl *D = CE->getCalleeDecl();
    if (D && D->hasBody())
      return VisitBody(D->getBody());
    return false;
  }

  bool VisitCXXDeleteExpr(const CXXDeleteExpr *E) {
    auto *Arg = E->getArgument();
    while (Arg) {
      if (auto *Paren = dyn_cast<ParenExpr>(Arg))
        Arg = Paren->getSubExpr();
      else if (auto *Cast = dyn_cast<CastExpr>(Arg)) {
        Arg = Cast->getSubExpr();
        auto CastType = Cast->getType();
        if (auto *PtrType = dyn_cast<PointerType>(CastType)) {
          auto PointeeType = PtrType->getPointeeType();
          while (auto *ET = dyn_cast<ElaboratedType>(PointeeType)) {
            if (ET->isSugared())
              PointeeType = ET->desugar();
          }
          if (auto *ParmType = dyn_cast<TemplateTypeParmType>(PointeeType)) {
            if (ArgList) {
              auto ParmIndex = ParmType->getIndex();
              auto Type = ArgList->get(ParmIndex).getAsType();
              if (Type->getAsCXXRecordDecl() == ClassDecl)
                return true;
            }
          } else if (auto *RD = dyn_cast<RecordType>(PointeeType)) {
            if (RD->getDecl() == ClassDecl)
              return true;
          } else if (auto *ST =
                         dyn_cast<SubstTemplateTypeParmType>(PointeeType)) {
            auto Type = ST->getReplacementType();
            if (auto *RD = dyn_cast<RecordType>(Type)) {
              if (RD->getDecl() == ClassDecl)
                return true;
            }
          }
        }
      } else
        break;
    }
    return false;
  }

  bool VisitStmt(const Stmt *S) { return VisitChildren(S); }

  // Return false since the contents of lambda isn't necessarily executed.
  // If it is executed, VisitCallExpr above will visit its body.
  bool VisitLambdaExpr(const LambdaExpr *) { return false; }

private:
  const TemplateArgumentList *ArgList{nullptr};
  const CXXRecordDecl *ClassDecl;
  llvm::DenseSet<const Stmt *> VisitedBody;
};

class RefCntblBaseVirtualDtorChecker
    : public Checker<check::ASTDecl<TranslationUnitDecl>> {
private:
  BugType Bug;
  mutable BugReporter *BR;

public:
  RefCntblBaseVirtualDtorChecker()
      : Bug(this,
            "Reference-countable base class doesn't have virtual destructor",
            "WebKit coding guidelines") {}

  void checkASTDecl(const TranslationUnitDecl *TUD, AnalysisManager &MGR,
                    BugReporter &BRArg) const {
    BR = &BRArg;

    // The calls to checkAST* from AnalysisConsumer don't
    // visit template instantiations or lambda classes. We
    // want to visit those, so we make our own RecursiveASTVisitor.
    struct LocalVisitor : public RecursiveASTVisitor<LocalVisitor> {
      const RefCntblBaseVirtualDtorChecker *Checker;
      explicit LocalVisitor(const RefCntblBaseVirtualDtorChecker *Checker)
          : Checker(Checker) {
        assert(Checker);
      }

      bool shouldVisitTemplateInstantiations() const { return true; }
      bool shouldVisitImplicitCode() const { return false; }

      bool VisitCXXRecordDecl(const CXXRecordDecl *RD) {
        if (!RD->hasDefinition())
          return true;

        Decls.insert(RD);

        for (auto &Base : RD->bases()) {
          const auto AccSpec = Base.getAccessSpecifier();
          if (AccSpec == AS_protected || AccSpec == AS_private ||
              (AccSpec == AS_none && RD->isClass()))
            continue;

          QualType T = Base.getType();
          if (T.isNull())
            continue;

          const CXXRecordDecl *C = T->getAsCXXRecordDecl();
          if (!C)
            continue;

          if (auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(C)) {
            for (auto &Arg : CTSD->getTemplateArgs().asArray()) {
              if (Arg.getKind() != TemplateArgument::Type)
                continue;
              auto TemplT = Arg.getAsType();
              if (TemplT.isNull())
                continue;

              bool IsCRTP = TemplT->getAsCXXRecordDecl() == RD;
              if (!IsCRTP)
                continue;
              CRTPs.insert(C);
            }
          }
        }

        return true;
      }

      llvm::SetVector<const CXXRecordDecl *> Decls;
      llvm::DenseSet<const CXXRecordDecl *> CRTPs;
    };

    LocalVisitor visitor(this);
    visitor.TraverseDecl(const_cast<TranslationUnitDecl *>(TUD));
    for (auto *RD : visitor.Decls) {
      if (visitor.CRTPs.contains(RD))
        continue;
      visitCXXRecordDecl(RD);
    }
  }

  void visitCXXRecordDecl(const CXXRecordDecl *RD) const {
    if (shouldSkipDecl(RD))
      return;

    for (auto &Base : RD->bases()) {
      const auto AccSpec = Base.getAccessSpecifier();
      if (AccSpec == AS_protected || AccSpec == AS_private ||
          (AccSpec == AS_none && RD->isClass()))
        continue;

      auto hasRefInBase = clang::hasPublicMethodInBase(&Base, "ref");
      auto hasDerefInBase = clang::hasPublicMethodInBase(&Base, "deref");

      bool hasRef = hasRefInBase && *hasRefInBase != nullptr;
      bool hasDeref = hasDerefInBase && *hasDerefInBase != nullptr;

      QualType T = Base.getType();
      if (T.isNull())
        continue;

      const CXXRecordDecl *C = T->getAsCXXRecordDecl();
      if (!C)
        continue;

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
      const auto hasPublicDerefInBase =
          [&AnyInconclusiveBase](const CXXBaseSpecifier *Base, CXXBasePath &) {
            auto hasDerefInBase = clang::hasPublicMethodInBase(Base, "deref");
            if (!hasDerefInBase) {
              AnyInconclusiveBase = true;
              return false;
            }
            return (*hasDerefInBase) != nullptr;
          };
      CXXBasePaths Paths;
      Paths.setOrigin(C);
      hasRef = hasRef || C->lookupInBases(hasPublicRefInBase, Paths,
                                          /*LookupInDependent =*/true);
      hasDeref = hasDeref || C->lookupInBases(hasPublicDerefInBase, Paths,
                                              /*LookupInDependent =*/true);
      if (AnyInconclusiveBase || !hasRef || !hasDeref)
        continue;

      auto HasSpecializedDelete = isClassWithSpecializedDelete(C, RD);
      if (!HasSpecializedDelete || *HasSpecializedDelete)
        continue;
      if (C->lookupInBases(
              [&](const CXXBaseSpecifier *Base, CXXBasePath &) {
                auto *T = Base->getType().getTypePtrOrNull();
                if (!T)
                  return false;
                auto *R = T->getAsCXXRecordDecl();
                if (!R)
                  return false;
                auto Result = isClassWithSpecializedDelete(R, RD);
                if (!Result)
                  AnyInconclusiveBase = true;
                return Result && *Result;
              },
              Paths, /*LookupInDependent =*/true))
        continue;
      if (AnyInconclusiveBase)
        continue;

      const auto *Dtor = C->getDestructor();
      if (!Dtor || !Dtor->isVirtual()) {
        auto *ProblematicBaseSpecifier = &Base;
        auto *ProblematicBaseClass = C;
        reportBug(RD, ProblematicBaseSpecifier, ProblematicBaseClass);
      }
    }
  }

  bool shouldSkipDecl(const CXXRecordDecl *RD) const {
    if (!RD->isThisDeclarationADefinition())
      return true;

    if (RD->isImplicit())
      return true;

    if (RD->isLambda())
      return true;

    // If the construct doesn't have a source file, then it's not something
    // we want to diagnose.
    const auto RDLocation = RD->getLocation();
    if (!RDLocation.isValid())
      return true;

    const auto Kind = RD->getTagKind();
    if (Kind != TagTypeKind::Struct && Kind != TagTypeKind::Class)
      return true;

    // Ignore CXXRecords that come from system headers.
    if (BR->getSourceManager().getFileCharacteristic(RDLocation) !=
        SrcMgr::C_User)
      return true;

    return false;
  }

  static bool isRefCountedClass(const CXXRecordDecl *D) {
    if (!D->getTemplateInstantiationPattern())
      return false;
    auto *NsDecl = D->getParent();
    if (!NsDecl || !isa<NamespaceDecl>(NsDecl))
      return false;
    auto NamespaceName = safeGetName(NsDecl);
    auto ClsNameStr = safeGetName(D);
    StringRef ClsName = ClsNameStr; // FIXME: Make safeGetName return StringRef.
    return NamespaceName == "WTF" &&
           (ClsName.ends_with("RefCounted") ||
            ClsName == "ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr");
  }

  static std::optional<bool>
  isClassWithSpecializedDelete(const CXXRecordDecl *C,
                               const CXXRecordDecl *DerivedClass) {
    if (auto *ClsTmplSpDecl = dyn_cast<ClassTemplateSpecializationDecl>(C)) {
      for (auto *MethodDecl : C->methods()) {
        if (safeGetName(MethodDecl) == "deref") {
          DerefFuncDeleteExprVisitor Visitor(ClsTmplSpDecl->getTemplateArgs(),
                                             DerivedClass);
          auto Result = Visitor.HasSpecializedDelete(MethodDecl);
          if (!Result || *Result)
            return Result;
        }
      }
      return false;
    }
    for (auto *MethodDecl : C->methods()) {
      if (safeGetName(MethodDecl) == "deref") {
        DerefFuncDeleteExprVisitor Visitor(DerivedClass);
        auto Result = Visitor.HasSpecializedDelete(MethodDecl);
        if (!Result || *Result)
          return Result;
      }
    }
    return false;
  }

  void reportBug(const CXXRecordDecl *DerivedClass,
                 const CXXBaseSpecifier *BaseSpec,
                 const CXXRecordDecl *ProblematicBaseClass) const {
    assert(DerivedClass);
    assert(BaseSpec);
    assert(ProblematicBaseClass);

    SmallString<100> Buf;
    llvm::raw_svector_ostream Os(Buf);

    Os << (ProblematicBaseClass->isClass() ? "Class" : "Struct") << " ";
    printQuotedQualifiedName(Os, ProblematicBaseClass);

    Os << " is used as a base of "
       << (DerivedClass->isClass() ? "class" : "struct") << " ";
    printQuotedQualifiedName(Os, DerivedClass);

    Os << " but doesn't have virtual destructor";

    PathDiagnosticLocation BSLoc(BaseSpec->getSourceRange().getBegin(),
                                 BR->getSourceManager());
    auto Report = std::make_unique<BasicBugReport>(Bug, Os.str(), BSLoc);
    Report->addRange(BaseSpec->getSourceRange());
    BR->emitReport(std::move(Report));
  }
};
} // namespace

void ento::registerRefCntblBaseVirtualDtorChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<RefCntblBaseVirtualDtorChecker>();
}

bool ento::shouldRegisterRefCntblBaseVirtualDtorChecker(
    const CheckerManager &mgr) {
  return true;
}
