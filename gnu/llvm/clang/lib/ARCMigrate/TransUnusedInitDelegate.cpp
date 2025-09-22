//===--- TransUnusedInitDelegate.cpp - Transformations to ARC mode --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Transformations:
//===----------------------------------------------------------------------===//
//
// rewriteUnusedInitDelegate:
//
// Rewrites an unused result of calling a delegate initialization, to assigning
// the result to self.
// e.g
//  [self init];
// ---->
//  self = [self init];
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

class UnusedInitRewriter : public RecursiveASTVisitor<UnusedInitRewriter> {
  Stmt *Body;
  MigrationPass &Pass;

  ExprSet Removables;

public:
  UnusedInitRewriter(MigrationPass &pass)
    : Body(nullptr), Pass(pass) { }

  void transformBody(Stmt *body, Decl *ParentD) {
    Body = body;
    collectRemovables(body, Removables);
    TraverseStmt(body);
  }

  bool VisitObjCMessageExpr(ObjCMessageExpr *ME) {
    if (ME->isDelegateInitCall() &&
        isRemovable(ME) &&
        Pass.TA.hasDiagnostic(diag::err_arc_unused_init_message,
                              ME->getExprLoc())) {
      Transaction Trans(Pass.TA);
      Pass.TA.clearDiagnostic(diag::err_arc_unused_init_message,
                              ME->getExprLoc());
      SourceRange ExprRange = ME->getSourceRange();
      Pass.TA.insert(ExprRange.getBegin(), "if (!(self = ");
      std::string retStr = ")) return ";
      retStr += getNilString(Pass);
      Pass.TA.insertAfterToken(ExprRange.getEnd(), retStr);
    }
    return true;
  }

private:
  bool isRemovable(Expr *E) const {
    return Removables.count(E);
  }
};

} // anonymous namespace

void trans::rewriteUnusedInitDelegate(MigrationPass &pass) {
  BodyTransform<UnusedInitRewriter> trans(pass);
  trans.TraverseDecl(pass.Ctx.getTranslationUnitDecl());
}
