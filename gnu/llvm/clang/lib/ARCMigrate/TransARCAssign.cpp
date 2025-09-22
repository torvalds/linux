//===--- TransARCAssign.cpp - Transformations to ARC mode -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// makeAssignARCSafe:
//
// Add '__strong' where appropriate.
//
//  for (id x in collection) {
//    x = 0;
//  }
// ---->
//  for (__strong id x in collection) {
//    x = 0;
//  }
//
//===----------------------------------------------------------------------===//

#include "Transforms.h"
#include "Internals.h"
#include "clang/AST/ASTContext.h"
#include "clang/Sema/SemaDiagnostic.h"

using namespace clang;
using namespace arcmt;
using namespace trans;

namespace {

class ARCAssignChecker : public RecursiveASTVisitor<ARCAssignChecker> {
  MigrationPass &Pass;
  llvm::DenseSet<VarDecl *> ModifiedVars;

public:
  ARCAssignChecker(MigrationPass &pass) : Pass(pass) { }

  bool VisitBinaryOperator(BinaryOperator *Exp) {
    if (Exp->getType()->isDependentType())
      return true;

    Expr *E = Exp->getLHS();
    SourceLocation OrigLoc = E->getExprLoc();
    SourceLocation Loc = OrigLoc;
    DeclRefExpr *declRef = dyn_cast<DeclRefExpr>(E->IgnoreParenCasts());
    if (declRef && isa<VarDecl>(declRef->getDecl())) {
      ASTContext &Ctx = Pass.Ctx;
      Expr::isModifiableLvalueResult IsLV = E->isModifiableLvalue(Ctx, &Loc);
      if (IsLV != Expr::MLV_ConstQualified)
        return true;
      VarDecl *var = cast<VarDecl>(declRef->getDecl());
      if (var->isARCPseudoStrong()) {
        Transaction Trans(Pass.TA);
        if (Pass.TA.clearDiagnostic(diag::err_typecheck_arr_assign_enumeration,
                                    Exp->getOperatorLoc())) {
          if (!ModifiedVars.count(var)) {
            TypeLoc TLoc = var->getTypeSourceInfo()->getTypeLoc();
            Pass.TA.insert(TLoc.getBeginLoc(), "__strong ");
            ModifiedVars.insert(var);
          }
        }
      }
    }

    return true;
  }
};

} // anonymous namespace

void trans::makeAssignARCSafe(MigrationPass &pass) {
  ARCAssignChecker assignCheck(pass);
  assignCheck.TraverseDecl(pass.Ctx.getTranslationUnitDecl());
}
