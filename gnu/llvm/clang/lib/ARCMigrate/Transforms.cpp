//===--- Transforms.cpp - Transformations to ARC mode ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Transforms.h"
#include "Internals.h"
#include "clang/ARCMigrate/ARCMT.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/DomainSpecific/CocoaConventions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaObjC.h"

using namespace clang;
using namespace arcmt;
using namespace trans;

ASTTraverser::~ASTTraverser() { }

bool MigrationPass::CFBridgingFunctionsDefined() {
  if (!EnableCFBridgeFns)
    EnableCFBridgeFns = SemaRef.ObjC().isKnownName("CFBridgingRetain") &&
                        SemaRef.ObjC().isKnownName("CFBridgingRelease");
  return *EnableCFBridgeFns;
}

//===----------------------------------------------------------------------===//
// Helpers.
//===----------------------------------------------------------------------===//

bool trans::canApplyWeak(ASTContext &Ctx, QualType type,
                         bool AllowOnUnknownClass) {
  if (!Ctx.getLangOpts().ObjCWeakRuntime)
    return false;

  QualType T = type;
  if (T.isNull())
    return false;

  // iOS is always safe to use 'weak'.
  if (Ctx.getTargetInfo().getTriple().isiOS() ||
      Ctx.getTargetInfo().getTriple().isWatchOS())
    AllowOnUnknownClass = true;

  while (const PointerType *ptr = T->getAs<PointerType>())
    T = ptr->getPointeeType();
  if (const ObjCObjectPointerType *ObjT = T->getAs<ObjCObjectPointerType>()) {
    ObjCInterfaceDecl *Class = ObjT->getInterfaceDecl();
    if (!AllowOnUnknownClass && (!Class || Class->getName() == "NSObject"))
      return false; // id/NSObject is not safe for weak.
    if (!AllowOnUnknownClass && !Class->hasDefinition())
      return false; // forward classes are not verifiable, therefore not safe.
    if (Class && Class->isArcWeakrefUnavailable())
      return false;
  }

  return true;
}

bool trans::isPlusOneAssign(const BinaryOperator *E) {
  if (E->getOpcode() != BO_Assign)
    return false;

  return isPlusOne(E->getRHS());
}

bool trans::isPlusOne(const Expr *E) {
  if (!E)
    return false;
  if (const FullExpr *FE = dyn_cast<FullExpr>(E))
    E = FE->getSubExpr();

  if (const ObjCMessageExpr *
        ME = dyn_cast<ObjCMessageExpr>(E->IgnoreParenCasts()))
    if (ME->getMethodFamily() == OMF_retain)
      return true;

  if (const CallExpr *
        callE = dyn_cast<CallExpr>(E->IgnoreParenCasts())) {
    if (const FunctionDecl *FD = callE->getDirectCallee()) {
      if (FD->hasAttr<CFReturnsRetainedAttr>())
        return true;

      if (FD->isGlobal() &&
          FD->getIdentifier() &&
          FD->getParent()->isTranslationUnit() &&
          FD->isExternallyVisible() &&
          ento::cocoa::isRefType(callE->getType(), "CF",
                                 FD->getIdentifier()->getName())) {
        StringRef fname = FD->getIdentifier()->getName();
        if (fname.ends_with("Retain") || fname.contains("Create") ||
            fname.contains("Copy"))
          return true;
      }
    }
  }

  const ImplicitCastExpr *implCE = dyn_cast<ImplicitCastExpr>(E);
  while (implCE && implCE->getCastKind() ==  CK_BitCast)
    implCE = dyn_cast<ImplicitCastExpr>(implCE->getSubExpr());

  return implCE && implCE->getCastKind() == CK_ARCConsumeObject;
}

/// 'Loc' is the end of a statement range. This returns the location
/// immediately after the semicolon following the statement.
/// If no semicolon is found or the location is inside a macro, the returned
/// source location will be invalid.
SourceLocation trans::findLocationAfterSemi(SourceLocation loc,
                                            ASTContext &Ctx, bool IsDecl) {
  SourceLocation SemiLoc = findSemiAfterLocation(loc, Ctx, IsDecl);
  if (SemiLoc.isInvalid())
    return SourceLocation();
  return SemiLoc.getLocWithOffset(1);
}

/// \arg Loc is the end of a statement range. This returns the location
/// of the semicolon following the statement.
/// If no semicolon is found or the location is inside a macro, the returned
/// source location will be invalid.
SourceLocation trans::findSemiAfterLocation(SourceLocation loc,
                                            ASTContext &Ctx,
                                            bool IsDecl) {
  SourceManager &SM = Ctx.getSourceManager();
  if (loc.isMacroID()) {
    if (!Lexer::isAtEndOfMacroExpansion(loc, SM, Ctx.getLangOpts(), &loc))
      return SourceLocation();
  }
  loc = Lexer::getLocForEndOfToken(loc, /*Offset=*/0, SM, Ctx.getLangOpts());

  // Break down the source location.
  std::pair<FileID, unsigned> locInfo = SM.getDecomposedLoc(loc);

  // Try to load the file buffer.
  bool invalidTemp = false;
  StringRef file = SM.getBufferData(locInfo.first, &invalidTemp);
  if (invalidTemp)
    return SourceLocation();

  const char *tokenBegin = file.data() + locInfo.second;

  // Lex from the start of the given location.
  Lexer lexer(SM.getLocForStartOfFile(locInfo.first),
              Ctx.getLangOpts(),
              file.begin(), tokenBegin, file.end());
  Token tok;
  lexer.LexFromRawLexer(tok);
  if (tok.isNot(tok::semi)) {
    if (!IsDecl)
      return SourceLocation();
    // Declaration may be followed with other tokens; such as an __attribute,
    // before ending with a semicolon.
    return findSemiAfterLocation(tok.getLocation(), Ctx, /*IsDecl*/true);
  }

  return tok.getLocation();
}

bool trans::hasSideEffects(Expr *E, ASTContext &Ctx) {
  if (!E || !E->HasSideEffects(Ctx))
    return false;

  E = E->IgnoreParenCasts();
  ObjCMessageExpr *ME = dyn_cast<ObjCMessageExpr>(E);
  if (!ME)
    return true;
  switch (ME->getMethodFamily()) {
  case OMF_autorelease:
  case OMF_dealloc:
  case OMF_release:
  case OMF_retain:
    switch (ME->getReceiverKind()) {
    case ObjCMessageExpr::SuperInstance:
      return false;
    case ObjCMessageExpr::Instance:
      return hasSideEffects(ME->getInstanceReceiver(), Ctx);
    default:
      break;
    }
    break;
  default:
    break;
  }

  return true;
}

bool trans::isGlobalVar(Expr *E) {
  E = E->IgnoreParenCasts();
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    return DRE->getDecl()->getDeclContext()->isFileContext() &&
           DRE->getDecl()->isExternallyVisible();
  if (ConditionalOperator *condOp = dyn_cast<ConditionalOperator>(E))
    return isGlobalVar(condOp->getTrueExpr()) &&
           isGlobalVar(condOp->getFalseExpr());

  return false;
}

StringRef trans::getNilString(MigrationPass &Pass) {
  return Pass.SemaRef.PP.isMacroDefined("nil") ? "nil" : "0";
}

namespace {

class ReferenceClear : public RecursiveASTVisitor<ReferenceClear> {
  ExprSet &Refs;
public:
  ReferenceClear(ExprSet &refs) : Refs(refs) { }
  bool VisitDeclRefExpr(DeclRefExpr *E) { Refs.erase(E); return true; }
};

class ReferenceCollector : public RecursiveASTVisitor<ReferenceCollector> {
  ValueDecl *Dcl;
  ExprSet &Refs;

public:
  ReferenceCollector(ValueDecl *D, ExprSet &refs)
    : Dcl(D), Refs(refs) { }

  bool VisitDeclRefExpr(DeclRefExpr *E) {
    if (E->getDecl() == Dcl)
      Refs.insert(E);
    return true;
  }
};

class RemovablesCollector : public RecursiveASTVisitor<RemovablesCollector> {
  ExprSet &Removables;

public:
  RemovablesCollector(ExprSet &removables)
  : Removables(removables) { }

  bool shouldWalkTypesOfTypeLocs() const { return false; }

  bool TraverseStmtExpr(StmtExpr *E) {
    CompoundStmt *S = E->getSubStmt();
    for (CompoundStmt::body_iterator
        I = S->body_begin(), E = S->body_end(); I != E; ++I) {
      if (I != E - 1)
        mark(*I);
      TraverseStmt(*I);
    }
    return true;
  }

  bool VisitCompoundStmt(CompoundStmt *S) {
    for (auto *I : S->body())
      mark(I);
    return true;
  }

  bool VisitIfStmt(IfStmt *S) {
    mark(S->getThen());
    mark(S->getElse());
    return true;
  }

  bool VisitWhileStmt(WhileStmt *S) {
    mark(S->getBody());
    return true;
  }

  bool VisitDoStmt(DoStmt *S) {
    mark(S->getBody());
    return true;
  }

  bool VisitForStmt(ForStmt *S) {
    mark(S->getInit());
    mark(S->getInc());
    mark(S->getBody());
    return true;
  }

private:
  void mark(Stmt *S) {
    if (!S) return;

    while (auto *Label = dyn_cast<LabelStmt>(S))
      S = Label->getSubStmt();
    if (auto *E = dyn_cast<Expr>(S))
      S = E->IgnoreImplicit();
    if (auto *E = dyn_cast<Expr>(S))
      Removables.insert(E);
  }
};

} // end anonymous namespace

void trans::clearRefsIn(Stmt *S, ExprSet &refs) {
  ReferenceClear(refs).TraverseStmt(S);
}

void trans::collectRefs(ValueDecl *D, Stmt *S, ExprSet &refs) {
  ReferenceCollector(D, refs).TraverseStmt(S);
}

void trans::collectRemovables(Stmt *S, ExprSet &exprs) {
  RemovablesCollector(exprs).TraverseStmt(S);
}

//===----------------------------------------------------------------------===//
// MigrationContext
//===----------------------------------------------------------------------===//

namespace {

class ASTTransform : public RecursiveASTVisitor<ASTTransform> {
  MigrationContext &MigrateCtx;
  typedef RecursiveASTVisitor<ASTTransform> base;

public:
  ASTTransform(MigrationContext &MigrateCtx) : MigrateCtx(MigrateCtx) { }

  bool shouldWalkTypesOfTypeLocs() const { return false; }

  bool TraverseObjCImplementationDecl(ObjCImplementationDecl *D) {
    ObjCImplementationContext ImplCtx(MigrateCtx, D);
    for (MigrationContext::traverser_iterator
           I = MigrateCtx.traversers_begin(),
           E = MigrateCtx.traversers_end(); I != E; ++I)
      (*I)->traverseObjCImplementation(ImplCtx);

    return base::TraverseObjCImplementationDecl(D);
  }

  bool TraverseStmt(Stmt *rootS) {
    if (!rootS)
      return true;

    BodyContext BodyCtx(MigrateCtx, rootS);
    for (MigrationContext::traverser_iterator
           I = MigrateCtx.traversers_begin(),
           E = MigrateCtx.traversers_end(); I != E; ++I)
      (*I)->traverseBody(BodyCtx);

    return true;
  }
};

}

MigrationContext::~MigrationContext() {
  for (traverser_iterator
         I = traversers_begin(), E = traversers_end(); I != E; ++I)
    delete *I;
}

bool MigrationContext::isGCOwnedNonObjC(QualType T) {
  while (!T.isNull()) {
    if (const AttributedType *AttrT = T->getAs<AttributedType>()) {
      if (AttrT->getAttrKind() == attr::ObjCOwnership)
        return !AttrT->getModifiedType()->isObjCRetainableType();
    }

    if (T->isArrayType())
      T = Pass.Ctx.getBaseElementType(T);
    else if (const PointerType *PT = T->getAs<PointerType>())
      T = PT->getPointeeType();
    else if (const ReferenceType *RT = T->getAs<ReferenceType>())
      T = RT->getPointeeType();
    else
      break;
  }

  return false;
}

bool MigrationContext::rewritePropertyAttribute(StringRef fromAttr,
                                                StringRef toAttr,
                                                SourceLocation atLoc) {
  if (atLoc.isMacroID())
    return false;

  SourceManager &SM = Pass.Ctx.getSourceManager();

  // Break down the source location.
  std::pair<FileID, unsigned> locInfo = SM.getDecomposedLoc(atLoc);

  // Try to load the file buffer.
  bool invalidTemp = false;
  StringRef file = SM.getBufferData(locInfo.first, &invalidTemp);
  if (invalidTemp)
    return false;

  const char *tokenBegin = file.data() + locInfo.second;

  // Lex from the start of the given location.
  Lexer lexer(SM.getLocForStartOfFile(locInfo.first),
              Pass.Ctx.getLangOpts(),
              file.begin(), tokenBegin, file.end());
  Token tok;
  lexer.LexFromRawLexer(tok);
  if (tok.isNot(tok::at)) return false;
  lexer.LexFromRawLexer(tok);
  if (tok.isNot(tok::raw_identifier)) return false;
  if (tok.getRawIdentifier() != "property")
    return false;
  lexer.LexFromRawLexer(tok);
  if (tok.isNot(tok::l_paren)) return false;

  Token BeforeTok = tok;
  Token AfterTok;
  AfterTok.startToken();
  SourceLocation AttrLoc;

  lexer.LexFromRawLexer(tok);
  if (tok.is(tok::r_paren))
    return false;

  while (true) {
    if (tok.isNot(tok::raw_identifier)) return false;
    if (tok.getRawIdentifier() == fromAttr) {
      if (!toAttr.empty()) {
        Pass.TA.replaceText(tok.getLocation(), fromAttr, toAttr);
        return true;
      }
      // We want to remove the attribute.
      AttrLoc = tok.getLocation();
    }

    do {
      lexer.LexFromRawLexer(tok);
      if (AttrLoc.isValid() && AfterTok.is(tok::unknown))
        AfterTok = tok;
    } while (tok.isNot(tok::comma) && tok.isNot(tok::r_paren));
    if (tok.is(tok::r_paren))
      break;
    if (AttrLoc.isInvalid())
      BeforeTok = tok;
    lexer.LexFromRawLexer(tok);
  }

  if (toAttr.empty() && AttrLoc.isValid() && AfterTok.isNot(tok::unknown)) {
    // We want to remove the attribute.
    if (BeforeTok.is(tok::l_paren) && AfterTok.is(tok::r_paren)) {
      Pass.TA.remove(SourceRange(BeforeTok.getLocation(),
                                 AfterTok.getLocation()));
    } else if (BeforeTok.is(tok::l_paren) && AfterTok.is(tok::comma)) {
      Pass.TA.remove(SourceRange(AttrLoc, AfterTok.getLocation()));
    } else {
      Pass.TA.remove(SourceRange(BeforeTok.getLocation(), AttrLoc));
    }

    return true;
  }

  return false;
}

bool MigrationContext::addPropertyAttribute(StringRef attr,
                                            SourceLocation atLoc) {
  if (atLoc.isMacroID())
    return false;

  SourceManager &SM = Pass.Ctx.getSourceManager();

  // Break down the source location.
  std::pair<FileID, unsigned> locInfo = SM.getDecomposedLoc(atLoc);

  // Try to load the file buffer.
  bool invalidTemp = false;
  StringRef file = SM.getBufferData(locInfo.first, &invalidTemp);
  if (invalidTemp)
    return false;

  const char *tokenBegin = file.data() + locInfo.second;

  // Lex from the start of the given location.
  Lexer lexer(SM.getLocForStartOfFile(locInfo.first),
              Pass.Ctx.getLangOpts(),
              file.begin(), tokenBegin, file.end());
  Token tok;
  lexer.LexFromRawLexer(tok);
  if (tok.isNot(tok::at)) return false;
  lexer.LexFromRawLexer(tok);
  if (tok.isNot(tok::raw_identifier)) return false;
  if (tok.getRawIdentifier() != "property")
    return false;
  lexer.LexFromRawLexer(tok);

  if (tok.isNot(tok::l_paren)) {
    Pass.TA.insert(tok.getLocation(), std::string("(") + attr.str() + ") ");
    return true;
  }

  lexer.LexFromRawLexer(tok);
  if (tok.is(tok::r_paren)) {
    Pass.TA.insert(tok.getLocation(), attr);
    return true;
  }

  if (tok.isNot(tok::raw_identifier)) return false;

  Pass.TA.insert(tok.getLocation(), std::string(attr) + ", ");
  return true;
}

void MigrationContext::traverse(TranslationUnitDecl *TU) {
  for (traverser_iterator
         I = traversers_begin(), E = traversers_end(); I != E; ++I)
    (*I)->traverseTU(*this);

  ASTTransform(*this).TraverseDecl(TU);
}

static void GCRewriteFinalize(MigrationPass &pass) {
  ASTContext &Ctx = pass.Ctx;
  TransformActions &TA = pass.TA;
  DeclContext *DC = Ctx.getTranslationUnitDecl();
  Selector FinalizeSel =
   Ctx.Selectors.getNullarySelector(&pass.Ctx.Idents.get("finalize"));

  typedef DeclContext::specific_decl_iterator<ObjCImplementationDecl>
  impl_iterator;
  for (impl_iterator I = impl_iterator(DC->decls_begin()),
       E = impl_iterator(DC->decls_end()); I != E; ++I) {
    for (const auto *MD : I->instance_methods()) {
      if (!MD->hasBody())
        continue;

      if (MD->isInstanceMethod() && MD->getSelector() == FinalizeSel) {
        const ObjCMethodDecl *FinalizeM = MD;
        Transaction Trans(TA);
        TA.insert(FinalizeM->getSourceRange().getBegin(),
                  "#if !__has_feature(objc_arc)\n");
        CharSourceRange::getTokenRange(FinalizeM->getSourceRange());
        const SourceManager &SM = pass.Ctx.getSourceManager();
        const LangOptions &LangOpts = pass.Ctx.getLangOpts();
        bool Invalid;
        std::string str = "\n#endif\n";
        str += Lexer::getSourceText(
                  CharSourceRange::getTokenRange(FinalizeM->getSourceRange()),
                                    SM, LangOpts, &Invalid);
        TA.insertAfterToken(FinalizeM->getSourceRange().getEnd(), str);

        break;
      }
    }
  }
}

//===----------------------------------------------------------------------===//
// getAllTransformations.
//===----------------------------------------------------------------------===//

static void traverseAST(MigrationPass &pass) {
  MigrationContext MigrateCtx(pass);

  if (pass.isGCMigration()) {
    MigrateCtx.addTraverser(new GCCollectableCallsTraverser);
    MigrateCtx.addTraverser(new GCAttrsTraverser());
  }
  MigrateCtx.addTraverser(new PropertyRewriteTraverser());
  MigrateCtx.addTraverser(new BlockObjCVariableTraverser());
  MigrateCtx.addTraverser(new ProtectedScopeTraverser());

  MigrateCtx.traverse(pass.Ctx.getTranslationUnitDecl());
}

static void independentTransforms(MigrationPass &pass) {
  rewriteAutoreleasePool(pass);
  removeRetainReleaseDeallocFinalize(pass);
  rewriteUnusedInitDelegate(pass);
  removeZeroOutPropsInDeallocFinalize(pass);
  makeAssignARCSafe(pass);
  rewriteUnbridgedCasts(pass);
  checkAPIUses(pass);
  traverseAST(pass);
}

std::vector<TransformFn> arcmt::getAllTransformations(
                                               LangOptions::GCMode OrigGCMode,
                                               bool NoFinalizeRemoval) {
  std::vector<TransformFn> transforms;

  if (OrigGCMode ==  LangOptions::GCOnly && NoFinalizeRemoval)
    transforms.push_back(GCRewriteFinalize);
  transforms.push_back(independentTransforms);
  // This depends on previous transformations removing various expressions.
  transforms.push_back(removeEmptyStatementsAndDeallocFinalize);

  return transforms;
}
