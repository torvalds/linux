//===--- TransAutoreleasePool.cpp - Transformations to ARC mode -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// rewriteAutoreleasePool:
//
// Calls to NSAutoreleasePools will be rewritten as an @autorelease scope.
//
//  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
//  ...
//  [pool release];
// ---->
//  @autorelease {
//  ...
//  }
//
// An NSAutoreleasePool will not be touched if:
// - There is not a corresponding -release/-drain in the same scope
// - Not all references of the NSAutoreleasePool variable can be removed
// - There is a variable that is declared inside the intended @autorelease scope
//   which is also used outside it.
//
//===----------------------------------------------------------------------===//

#include "Transforms.h"
#include "Internals.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Sema/SemaDiagnostic.h"
#include <map>

using namespace clang;
using namespace arcmt;
using namespace trans;

namespace {

class ReleaseCollector : public RecursiveASTVisitor<ReleaseCollector> {
  Decl *Dcl;
  SmallVectorImpl<ObjCMessageExpr *> &Releases;

public:
  ReleaseCollector(Decl *D, SmallVectorImpl<ObjCMessageExpr *> &releases)
    : Dcl(D), Releases(releases) { }

  bool VisitObjCMessageExpr(ObjCMessageExpr *E) {
    if (!E->isInstanceMessage())
      return true;
    if (E->getMethodFamily() != OMF_release)
      return true;
    Expr *instance = E->getInstanceReceiver()->IgnoreParenCasts();
    if (DeclRefExpr *DE = dyn_cast<DeclRefExpr>(instance)) {
      if (DE->getDecl() == Dcl)
        Releases.push_back(E);
    }
    return true;
  }
};

}

namespace {

class AutoreleasePoolRewriter
                         : public RecursiveASTVisitor<AutoreleasePoolRewriter> {
public:
  AutoreleasePoolRewriter(MigrationPass &pass)
    : Body(nullptr), Pass(pass) {
    PoolII = &pass.Ctx.Idents.get("NSAutoreleasePool");
    DrainSel = pass.Ctx.Selectors.getNullarySelector(
                                                 &pass.Ctx.Idents.get("drain"));
  }

  void transformBody(Stmt *body, Decl *ParentD) {
    Body = body;
    TraverseStmt(body);
  }

  ~AutoreleasePoolRewriter() {
    SmallVector<VarDecl *, 8> VarsToHandle;

    for (std::map<VarDecl *, PoolVarInfo>::iterator
           I = PoolVars.begin(), E = PoolVars.end(); I != E; ++I) {
      VarDecl *var = I->first;
      PoolVarInfo &info = I->second;

      // Check that we can handle/rewrite all references of the pool.

      clearRefsIn(info.Dcl, info.Refs);
      for (SmallVectorImpl<PoolScope>::iterator
             scpI = info.Scopes.begin(),
             scpE = info.Scopes.end(); scpI != scpE; ++scpI) {
        PoolScope &scope = *scpI;
        clearRefsIn(*scope.Begin, info.Refs);
        clearRefsIn(*scope.End, info.Refs);
        clearRefsIn(scope.Releases.begin(), scope.Releases.end(), info.Refs);
      }

      // Even if one reference is not handled we will not do anything about that
      // pool variable.
      if (info.Refs.empty())
        VarsToHandle.push_back(var);
    }

    for (unsigned i = 0, e = VarsToHandle.size(); i != e; ++i) {
      PoolVarInfo &info = PoolVars[VarsToHandle[i]];

      Transaction Trans(Pass.TA);

      clearUnavailableDiags(info.Dcl);
      Pass.TA.removeStmt(info.Dcl);

      // Add "@autoreleasepool { }"
      for (SmallVectorImpl<PoolScope>::iterator
             scpI = info.Scopes.begin(),
             scpE = info.Scopes.end(); scpI != scpE; ++scpI) {
        PoolScope &scope = *scpI;
        clearUnavailableDiags(*scope.Begin);
        clearUnavailableDiags(*scope.End);
        if (scope.IsFollowedBySimpleReturnStmt) {
          // Include the return in the scope.
          Pass.TA.replaceStmt(*scope.Begin, "@autoreleasepool {");
          Pass.TA.removeStmt(*scope.End);
          Stmt::child_iterator retI = scope.End;
          ++retI;
          SourceLocation afterSemi =
              findLocationAfterSemi((*retI)->getEndLoc(), Pass.Ctx);
          assert(afterSemi.isValid() &&
                 "Didn't we check before setting IsFollowedBySimpleReturnStmt "
                 "to true?");
          Pass.TA.insertAfterToken(afterSemi, "\n}");
          Pass.TA.increaseIndentation(
              SourceRange(scope.getIndentedRange().getBegin(),
                          (*retI)->getEndLoc()),
              scope.CompoundParent->getBeginLoc());
        } else {
          Pass.TA.replaceStmt(*scope.Begin, "@autoreleasepool {");
          Pass.TA.replaceStmt(*scope.End, "}");
          Pass.TA.increaseIndentation(scope.getIndentedRange(),
                                      scope.CompoundParent->getBeginLoc());
        }
      }

      // Remove rest of pool var references.
      for (SmallVectorImpl<PoolScope>::iterator
             scpI = info.Scopes.begin(),
             scpE = info.Scopes.end(); scpI != scpE; ++scpI) {
        PoolScope &scope = *scpI;
        for (SmallVectorImpl<ObjCMessageExpr *>::iterator
               relI = scope.Releases.begin(),
               relE = scope.Releases.end(); relI != relE; ++relI) {
          clearUnavailableDiags(*relI);
          Pass.TA.removeStmt(*relI);
        }
      }
    }
  }

  bool VisitCompoundStmt(CompoundStmt *S) {
    SmallVector<PoolScope, 4> Scopes;

    for (Stmt::child_iterator
           I = S->body_begin(), E = S->body_end(); I != E; ++I) {
      Stmt *child = getEssential(*I);
      if (DeclStmt *DclS = dyn_cast<DeclStmt>(child)) {
        if (DclS->isSingleDecl()) {
          if (VarDecl *VD = dyn_cast<VarDecl>(DclS->getSingleDecl())) {
            if (isNSAutoreleasePool(VD->getType())) {
              PoolVarInfo &info = PoolVars[VD];
              info.Dcl = DclS;
              collectRefs(VD, S, info.Refs);
              // Does this statement follow the pattern:
              // NSAutoreleasePool * pool = [NSAutoreleasePool  new];
              if (isPoolCreation(VD->getInit())) {
                Scopes.push_back(PoolScope());
                Scopes.back().PoolVar = VD;
                Scopes.back().CompoundParent = S;
                Scopes.back().Begin = I;
              }
            }
          }
        }
      } else if (BinaryOperator *bop = dyn_cast<BinaryOperator>(child)) {
        if (DeclRefExpr *dref = dyn_cast<DeclRefExpr>(bop->getLHS())) {
          if (VarDecl *VD = dyn_cast<VarDecl>(dref->getDecl())) {
            // Does this statement follow the pattern:
            // pool = [NSAutoreleasePool  new];
            if (isNSAutoreleasePool(VD->getType()) &&
                isPoolCreation(bop->getRHS())) {
              Scopes.push_back(PoolScope());
              Scopes.back().PoolVar = VD;
              Scopes.back().CompoundParent = S;
              Scopes.back().Begin = I;
            }
          }
        }
      }

      if (Scopes.empty())
        continue;

      if (isPoolDrain(Scopes.back().PoolVar, child)) {
        PoolScope &scope = Scopes.back();
        scope.End = I;
        handlePoolScope(scope, S);
        Scopes.pop_back();
      }
    }
    return true;
  }

private:
  void clearUnavailableDiags(Stmt *S) {
    if (S)
      Pass.TA.clearDiagnostic(diag::err_unavailable,
                              diag::err_unavailable_message,
                              S->getSourceRange());
  }

  struct PoolScope {
    VarDecl *PoolVar;
    CompoundStmt *CompoundParent;
    Stmt::child_iterator Begin;
    Stmt::child_iterator End;
    bool IsFollowedBySimpleReturnStmt;
    SmallVector<ObjCMessageExpr *, 4> Releases;

    PoolScope()
        : PoolVar(nullptr), CompoundParent(nullptr),
          IsFollowedBySimpleReturnStmt(false) {}

    SourceRange getIndentedRange() const {
      Stmt::child_iterator rangeS = Begin;
      ++rangeS;
      if (rangeS == End)
        return SourceRange();
      Stmt::child_iterator rangeE = Begin;
      for (Stmt::child_iterator I = rangeS; I != End; ++I)
        ++rangeE;
      return SourceRange((*rangeS)->getBeginLoc(), (*rangeE)->getEndLoc());
    }
  };

  class NameReferenceChecker : public RecursiveASTVisitor<NameReferenceChecker>{
    ASTContext &Ctx;
    SourceRange ScopeRange;
    SourceLocation &referenceLoc, &declarationLoc;

  public:
    NameReferenceChecker(ASTContext &ctx, PoolScope &scope,
                         SourceLocation &referenceLoc,
                         SourceLocation &declarationLoc)
      : Ctx(ctx), referenceLoc(referenceLoc),
        declarationLoc(declarationLoc) {
      ScopeRange = SourceRange((*scope.Begin)->getBeginLoc(),
                               (*scope.End)->getBeginLoc());
    }

    bool VisitDeclRefExpr(DeclRefExpr *E) {
      return checkRef(E->getLocation(), E->getDecl()->getLocation());
    }

    bool VisitTypedefTypeLoc(TypedefTypeLoc TL) {
      return checkRef(TL.getBeginLoc(), TL.getTypedefNameDecl()->getLocation());
    }

    bool VisitTagTypeLoc(TagTypeLoc TL) {
      return checkRef(TL.getBeginLoc(), TL.getDecl()->getLocation());
    }

  private:
    bool checkRef(SourceLocation refLoc, SourceLocation declLoc) {
      if (isInScope(declLoc)) {
        referenceLoc = refLoc;
        declarationLoc = declLoc;
        return false;
      }
      return true;
    }

    bool isInScope(SourceLocation loc) {
      if (loc.isInvalid())
        return false;

      SourceManager &SM = Ctx.getSourceManager();
      if (SM.isBeforeInTranslationUnit(loc, ScopeRange.getBegin()))
        return false;
      return SM.isBeforeInTranslationUnit(loc, ScopeRange.getEnd());
    }
  };

  void handlePoolScope(PoolScope &scope, CompoundStmt *compoundS) {
    // Check that all names declared inside the scope are not used
    // outside the scope.
    {
      bool nameUsedOutsideScope = false;
      SourceLocation referenceLoc, declarationLoc;
      Stmt::child_iterator SI = scope.End, SE = compoundS->body_end();
      ++SI;
      // Check if the autoreleasepool scope is followed by a simple return
      // statement, in which case we will include the return in the scope.
      if (SI != SE)
        if (ReturnStmt *retS = dyn_cast<ReturnStmt>(*SI))
          if ((retS->getRetValue() == nullptr ||
               isa<DeclRefExpr>(retS->getRetValue()->IgnoreParenCasts())) &&
              findLocationAfterSemi(retS->getEndLoc(), Pass.Ctx).isValid()) {
            scope.IsFollowedBySimpleReturnStmt = true;
            ++SI; // the return will be included in scope, don't check it.
          }

      for (; SI != SE; ++SI) {
        nameUsedOutsideScope = !NameReferenceChecker(Pass.Ctx, scope,
                                                     referenceLoc,
                                              declarationLoc).TraverseStmt(*SI);
        if (nameUsedOutsideScope)
          break;
      }

      // If not all references were cleared it means some variables/typenames/etc
      // declared inside the pool scope are used outside of it.
      // We won't try to rewrite the pool.
      if (nameUsedOutsideScope) {
        Pass.TA.reportError("a name is referenced outside the "
            "NSAutoreleasePool scope that it was declared in", referenceLoc);
        Pass.TA.reportNote("name declared here", declarationLoc);
        Pass.TA.reportNote("intended @autoreleasepool scope begins here",
                           (*scope.Begin)->getBeginLoc());
        Pass.TA.reportNote("intended @autoreleasepool scope ends here",
                           (*scope.End)->getBeginLoc());
        return;
      }
    }

    // Collect all releases of the pool; they will be removed.
    {
      ReleaseCollector releaseColl(scope.PoolVar, scope.Releases);
      Stmt::child_iterator I = scope.Begin;
      ++I;
      for (; I != scope.End; ++I)
        releaseColl.TraverseStmt(*I);
    }

    PoolVars[scope.PoolVar].Scopes.push_back(scope);
  }

  bool isPoolCreation(Expr *E) {
    if (!E) return false;
    E = getEssential(E);
    ObjCMessageExpr *ME = dyn_cast<ObjCMessageExpr>(E);
    if (!ME) return false;
    if (ME->getMethodFamily() == OMF_new &&
        ME->getReceiverKind() == ObjCMessageExpr::Class &&
        isNSAutoreleasePool(ME->getReceiverInterface()))
      return true;
    if (ME->getReceiverKind() == ObjCMessageExpr::Instance &&
        ME->getMethodFamily() == OMF_init) {
      Expr *rec = getEssential(ME->getInstanceReceiver());
      if (ObjCMessageExpr *recME = dyn_cast_or_null<ObjCMessageExpr>(rec)) {
        if (recME->getMethodFamily() == OMF_alloc &&
            recME->getReceiverKind() == ObjCMessageExpr::Class &&
            isNSAutoreleasePool(recME->getReceiverInterface()))
          return true;
      }
    }

    return false;
  }

  bool isPoolDrain(VarDecl *poolVar, Stmt *S) {
    if (!S) return false;
    S = getEssential(S);
    ObjCMessageExpr *ME = dyn_cast<ObjCMessageExpr>(S);
    if (!ME) return false;
    if (ME->getReceiverKind() == ObjCMessageExpr::Instance) {
      Expr *rec = getEssential(ME->getInstanceReceiver());
      if (DeclRefExpr *dref = dyn_cast<DeclRefExpr>(rec))
        if (dref->getDecl() == poolVar)
          return ME->getMethodFamily() == OMF_release ||
                 ME->getSelector() == DrainSel;
    }

    return false;
  }

  bool isNSAutoreleasePool(ObjCInterfaceDecl *IDecl) {
    return IDecl && IDecl->getIdentifier() == PoolII;
  }

  bool isNSAutoreleasePool(QualType Ty) {
    QualType pointee = Ty->getPointeeType();
    if (pointee.isNull())
      return false;
    if (const ObjCInterfaceType *interT = pointee->getAs<ObjCInterfaceType>())
      return isNSAutoreleasePool(interT->getDecl());
    return false;
  }

  static Expr *getEssential(Expr *E) {
    return cast<Expr>(getEssential((Stmt*)E));
  }
  static Stmt *getEssential(Stmt *S) {
    if (FullExpr *FE = dyn_cast<FullExpr>(S))
      S = FE->getSubExpr();
    if (Expr *E = dyn_cast<Expr>(S))
      S = E->IgnoreParenCasts();
    return S;
  }

  Stmt *Body;
  MigrationPass &Pass;

  IdentifierInfo *PoolII;
  Selector DrainSel;

  struct PoolVarInfo {
    DeclStmt *Dcl = nullptr;
    ExprSet Refs;
    SmallVector<PoolScope, 2> Scopes;

    PoolVarInfo() = default;
  };

  std::map<VarDecl *, PoolVarInfo> PoolVars;
};

} // anonymous namespace

void trans::rewriteAutoreleasePool(MigrationPass &pass) {
  BodyTransform<AutoreleasePoolRewriter> trans(pass);
  trans.TraverseDecl(pass.Ctx.getTranslationUnitDecl());
}
