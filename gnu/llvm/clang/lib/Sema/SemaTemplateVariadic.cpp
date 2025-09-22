//===------- SemaTemplateVariadic.cpp - C++ Variadic Templates ------------===/
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===/
//
//  This file implements semantic analysis for C++0x variadic templates.
//===----------------------------------------------------------------------===/

#include "clang/Sema/Sema.h"
#include "TypeLocBuilder.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/Template.h"
#include <optional>

using namespace clang;

//----------------------------------------------------------------------------
// Visitor that collects unexpanded parameter packs
//----------------------------------------------------------------------------

namespace {
  /// A class that collects unexpanded parameter packs.
  class CollectUnexpandedParameterPacksVisitor :
    public RecursiveASTVisitor<CollectUnexpandedParameterPacksVisitor>
  {
    typedef RecursiveASTVisitor<CollectUnexpandedParameterPacksVisitor>
      inherited;

    SmallVectorImpl<UnexpandedParameterPack> &Unexpanded;

    bool InLambda = false;
    unsigned DepthLimit = (unsigned)-1;

    void addUnexpanded(NamedDecl *ND, SourceLocation Loc = SourceLocation()) {
      if (auto *VD = dyn_cast<VarDecl>(ND)) {
        // For now, the only problematic case is a generic lambda's templated
        // call operator, so we don't need to look for all the other ways we
        // could have reached a dependent parameter pack.
        auto *FD = dyn_cast<FunctionDecl>(VD->getDeclContext());
        auto *FTD = FD ? FD->getDescribedFunctionTemplate() : nullptr;
        if (FTD && FTD->getTemplateParameters()->getDepth() >= DepthLimit)
          return;
      } else if (getDepthAndIndex(ND).first >= DepthLimit)
        return;

      Unexpanded.push_back({ND, Loc});
    }
    void addUnexpanded(const TemplateTypeParmType *T,
                       SourceLocation Loc = SourceLocation()) {
      if (T->getDepth() < DepthLimit)
        Unexpanded.push_back({T, Loc});
    }

  public:
    explicit CollectUnexpandedParameterPacksVisitor(
        SmallVectorImpl<UnexpandedParameterPack> &Unexpanded)
        : Unexpanded(Unexpanded) {}

    bool shouldWalkTypesOfTypeLocs() const { return false; }

    // We need this so we can find e.g. attributes on lambdas.
    bool shouldVisitImplicitCode() const { return true; }

    //------------------------------------------------------------------------
    // Recording occurrences of (unexpanded) parameter packs.
    //------------------------------------------------------------------------

    /// Record occurrences of template type parameter packs.
    bool VisitTemplateTypeParmTypeLoc(TemplateTypeParmTypeLoc TL) {
      if (TL.getTypePtr()->isParameterPack())
        addUnexpanded(TL.getTypePtr(), TL.getNameLoc());
      return true;
    }

    /// Record occurrences of template type parameter packs
    /// when we don't have proper source-location information for
    /// them.
    ///
    /// Ideally, this routine would never be used.
    bool VisitTemplateTypeParmType(TemplateTypeParmType *T) {
      if (T->isParameterPack())
        addUnexpanded(T);

      return true;
    }

    /// Record occurrences of function and non-type template
    /// parameter packs in an expression.
    bool VisitDeclRefExpr(DeclRefExpr *E) {
      if (E->getDecl()->isParameterPack())
        addUnexpanded(E->getDecl(), E->getLocation());

      return true;
    }

    /// Record occurrences of template template parameter packs.
    bool TraverseTemplateName(TemplateName Template) {
      if (auto *TTP = dyn_cast_or_null<TemplateTemplateParmDecl>(
              Template.getAsTemplateDecl())) {
        if (TTP->isParameterPack())
          addUnexpanded(TTP);
      }

      return inherited::TraverseTemplateName(Template);
    }

    /// Suppress traversal into Objective-C container literal
    /// elements that are pack expansions.
    bool TraverseObjCDictionaryLiteral(ObjCDictionaryLiteral *E) {
      if (!E->containsUnexpandedParameterPack())
        return true;

      for (unsigned I = 0, N = E->getNumElements(); I != N; ++I) {
        ObjCDictionaryElement Element = E->getKeyValueElement(I);
        if (Element.isPackExpansion())
          continue;

        TraverseStmt(Element.Key);
        TraverseStmt(Element.Value);
      }
      return true;
    }
    //------------------------------------------------------------------------
    // Pruning the search for unexpanded parameter packs.
    //------------------------------------------------------------------------

    /// Suppress traversal into statements and expressions that
    /// do not contain unexpanded parameter packs.
    bool TraverseStmt(Stmt *S) {
      Expr *E = dyn_cast_or_null<Expr>(S);
      if ((E && E->containsUnexpandedParameterPack()) || InLambda)
        return inherited::TraverseStmt(S);

      return true;
    }

    /// Suppress traversal into types that do not contain
    /// unexpanded parameter packs.
    bool TraverseType(QualType T) {
      if ((!T.isNull() && T->containsUnexpandedParameterPack()) || InLambda)
        return inherited::TraverseType(T);

      return true;
    }

    /// Suppress traversal into types with location information
    /// that do not contain unexpanded parameter packs.
    bool TraverseTypeLoc(TypeLoc TL) {
      if ((!TL.getType().isNull() &&
           TL.getType()->containsUnexpandedParameterPack()) ||
          InLambda)
        return inherited::TraverseTypeLoc(TL);

      return true;
    }

    /// Suppress traversal of parameter packs.
    bool TraverseDecl(Decl *D) {
      // A function parameter pack is a pack expansion, so cannot contain
      // an unexpanded parameter pack. Likewise for a template parameter
      // pack that contains any references to other packs.
      if (D && D->isParameterPack())
        return true;

      return inherited::TraverseDecl(D);
    }

    /// Suppress traversal of pack-expanded attributes.
    bool TraverseAttr(Attr *A) {
      if (A->isPackExpansion())
        return true;

      return inherited::TraverseAttr(A);
    }

    /// Suppress traversal of pack expansion expressions and types.
    ///@{
    bool TraversePackExpansionType(PackExpansionType *T) { return true; }
    bool TraversePackExpansionTypeLoc(PackExpansionTypeLoc TL) { return true; }
    bool TraversePackExpansionExpr(PackExpansionExpr *E) { return true; }
    bool TraverseCXXFoldExpr(CXXFoldExpr *E) { return true; }
    bool TraversePackIndexingExpr(PackIndexingExpr *E) {
      return inherited::TraverseStmt(E->getIndexExpr());
    }
    bool TraversePackIndexingType(PackIndexingType *E) {
      return inherited::TraverseStmt(E->getIndexExpr());
    }
    bool TraversePackIndexingTypeLoc(PackIndexingTypeLoc TL) {
      return inherited::TraverseStmt(TL.getIndexExpr());
    }

    ///@}

    /// Suppress traversal of using-declaration pack expansion.
    bool TraverseUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D) {
      if (D->isPackExpansion())
        return true;

      return inherited::TraverseUnresolvedUsingValueDecl(D);
    }

    /// Suppress traversal of using-declaration pack expansion.
    bool TraverseUnresolvedUsingTypenameDecl(UnresolvedUsingTypenameDecl *D) {
      if (D->isPackExpansion())
        return true;

      return inherited::TraverseUnresolvedUsingTypenameDecl(D);
    }

    /// Suppress traversal of template argument pack expansions.
    bool TraverseTemplateArgument(const TemplateArgument &Arg) {
      if (Arg.isPackExpansion())
        return true;

      return inherited::TraverseTemplateArgument(Arg);
    }

    /// Suppress traversal of template argument pack expansions.
    bool TraverseTemplateArgumentLoc(const TemplateArgumentLoc &ArgLoc) {
      if (ArgLoc.getArgument().isPackExpansion())
        return true;

      return inherited::TraverseTemplateArgumentLoc(ArgLoc);
    }

    /// Suppress traversal of base specifier pack expansions.
    bool TraverseCXXBaseSpecifier(const CXXBaseSpecifier &Base) {
      if (Base.isPackExpansion())
        return true;

      return inherited::TraverseCXXBaseSpecifier(Base);
    }

    /// Suppress traversal of mem-initializer pack expansions.
    bool TraverseConstructorInitializer(CXXCtorInitializer *Init) {
      if (Init->isPackExpansion())
        return true;

      return inherited::TraverseConstructorInitializer(Init);
    }

    /// Note whether we're traversing a lambda containing an unexpanded
    /// parameter pack. In this case, the unexpanded pack can occur anywhere,
    /// including all the places where we normally wouldn't look. Within a
    /// lambda, we don't propagate the 'contains unexpanded parameter pack' bit
    /// outside an expression.
    bool TraverseLambdaExpr(LambdaExpr *Lambda) {
      // The ContainsUnexpandedParameterPack bit on a lambda is always correct,
      // even if it's contained within another lambda.
      if (!Lambda->containsUnexpandedParameterPack())
        return true;

      bool WasInLambda = InLambda;
      unsigned OldDepthLimit = DepthLimit;

      InLambda = true;
      if (auto *TPL = Lambda->getTemplateParameterList())
        DepthLimit = TPL->getDepth();

      inherited::TraverseLambdaExpr(Lambda);

      InLambda = WasInLambda;
      DepthLimit = OldDepthLimit;
      return true;
    }

    /// Suppress traversal within pack expansions in lambda captures.
    bool TraverseLambdaCapture(LambdaExpr *Lambda, const LambdaCapture *C,
                               Expr *Init) {
      if (C->isPackExpansion())
        return true;

      return inherited::TraverseLambdaCapture(Lambda, C, Init);
    }
  };
}

/// Determine whether it's possible for an unexpanded parameter pack to
/// be valid in this location. This only happens when we're in a declaration
/// that is nested within an expression that could be expanded, such as a
/// lambda-expression within a function call.
///
/// This is conservatively correct, but may claim that some unexpanded packs are
/// permitted when they are not.
bool Sema::isUnexpandedParameterPackPermitted() {
  for (auto *SI : FunctionScopes)
    if (isa<sema::LambdaScopeInfo>(SI))
      return true;
  return false;
}

/// Diagnose all of the unexpanded parameter packs in the given
/// vector.
bool
Sema::DiagnoseUnexpandedParameterPacks(SourceLocation Loc,
                                       UnexpandedParameterPackContext UPPC,
                                 ArrayRef<UnexpandedParameterPack> Unexpanded) {
  if (Unexpanded.empty())
    return false;

  // If we are within a lambda expression and referencing a pack that is not
  // declared within the lambda itself, that lambda contains an unexpanded
  // parameter pack, and we are done.
  // FIXME: Store 'Unexpanded' on the lambda so we don't need to recompute it
  // later.
  SmallVector<UnexpandedParameterPack, 4> LambdaParamPackReferences;
  if (auto *LSI = getEnclosingLambda()) {
    for (auto &Pack : Unexpanded) {
      auto DeclaresThisPack = [&](NamedDecl *LocalPack) {
        if (auto *TTPT = Pack.first.dyn_cast<const TemplateTypeParmType *>()) {
          auto *TTPD = dyn_cast<TemplateTypeParmDecl>(LocalPack);
          return TTPD && TTPD->getTypeForDecl() == TTPT;
        }
        return declaresSameEntity(Pack.first.get<NamedDecl *>(), LocalPack);
      };
      if (llvm::any_of(LSI->LocalPacks, DeclaresThisPack))
        LambdaParamPackReferences.push_back(Pack);
    }

    if (LambdaParamPackReferences.empty()) {
      // Construct in lambda only references packs declared outside the lambda.
      // That's OK for now, but the lambda itself is considered to contain an
      // unexpanded pack in this case, which will require expansion outside the
      // lambda.

      // We do not permit pack expansion that would duplicate a statement
      // expression, not even within a lambda.
      // FIXME: We could probably support this for statement expressions that
      // do not contain labels.
      // FIXME: This is insufficient to detect this problem; consider
      //   f( ({ bad: 0; }) + pack ... );
      bool EnclosingStmtExpr = false;
      for (unsigned N = FunctionScopes.size(); N; --N) {
        sema::FunctionScopeInfo *Func = FunctionScopes[N-1];
        if (llvm::any_of(
                Func->CompoundScopes,
                [](sema::CompoundScopeInfo &CSI) { return CSI.IsStmtExpr; })) {
          EnclosingStmtExpr = true;
          break;
        }
        // Coumpound-statements outside the lambda are OK for now; we'll check
        // for those when we finish handling the lambda.
        if (Func == LSI)
          break;
      }

      if (!EnclosingStmtExpr) {
        LSI->ContainsUnexpandedParameterPack = true;
        return false;
      }
    } else {
      Unexpanded = LambdaParamPackReferences;
    }
  }

  SmallVector<SourceLocation, 4> Locations;
  SmallVector<IdentifierInfo *, 4> Names;
  llvm::SmallPtrSet<IdentifierInfo *, 4> NamesKnown;

  for (unsigned I = 0, N = Unexpanded.size(); I != N; ++I) {
    IdentifierInfo *Name = nullptr;
    if (const TemplateTypeParmType *TTP
          = Unexpanded[I].first.dyn_cast<const TemplateTypeParmType *>())
      Name = TTP->getIdentifier();
    else
      Name = Unexpanded[I].first.get<NamedDecl *>()->getIdentifier();

    if (Name && NamesKnown.insert(Name).second)
      Names.push_back(Name);

    if (Unexpanded[I].second.isValid())
      Locations.push_back(Unexpanded[I].second);
  }

  auto DB = Diag(Loc, diag::err_unexpanded_parameter_pack)
            << (int)UPPC << (int)Names.size();
  for (size_t I = 0, E = std::min(Names.size(), (size_t)2); I != E; ++I)
    DB << Names[I];

  for (unsigned I = 0, N = Locations.size(); I != N; ++I)
    DB << SourceRange(Locations[I]);
  return true;
}

bool Sema::DiagnoseUnexpandedParameterPack(SourceLocation Loc,
                                           TypeSourceInfo *T,
                                         UnexpandedParameterPackContext UPPC) {
  // C++0x [temp.variadic]p5:
  //   An appearance of a name of a parameter pack that is not expanded is
  //   ill-formed.
  if (!T->getType()->containsUnexpandedParameterPack())
    return false;

  SmallVector<UnexpandedParameterPack, 2> Unexpanded;
  CollectUnexpandedParameterPacksVisitor(Unexpanded).TraverseTypeLoc(
                                                              T->getTypeLoc());
  assert(!Unexpanded.empty() && "Unable to find unexpanded parameter packs");
  return DiagnoseUnexpandedParameterPacks(Loc, UPPC, Unexpanded);
}

bool Sema::DiagnoseUnexpandedParameterPack(Expr *E,
                                        UnexpandedParameterPackContext UPPC) {
  // C++0x [temp.variadic]p5:
  //   An appearance of a name of a parameter pack that is not expanded is
  //   ill-formed.
  if (!E->containsUnexpandedParameterPack())
    return false;

  // CollectUnexpandedParameterPacksVisitor does not expect to see a
  // FunctionParmPackExpr, but diagnosing unexpected parameter packs may still
  // see such an expression in a lambda body.
  // We'll bail out early in this case to avoid triggering an assertion.
  if (isa<FunctionParmPackExpr>(E) && getEnclosingLambda())
    return false;

  SmallVector<UnexpandedParameterPack, 2> Unexpanded;
  CollectUnexpandedParameterPacksVisitor(Unexpanded).TraverseStmt(E);
  assert(!Unexpanded.empty() && "Unable to find unexpanded parameter packs");
  return DiagnoseUnexpandedParameterPacks(E->getBeginLoc(), UPPC, Unexpanded);
}

bool Sema::DiagnoseUnexpandedParameterPackInRequiresExpr(RequiresExpr *RE) {
  if (!RE->containsUnexpandedParameterPack())
    return false;

  SmallVector<UnexpandedParameterPack, 2> Unexpanded;
  CollectUnexpandedParameterPacksVisitor(Unexpanded).TraverseStmt(RE);
  assert(!Unexpanded.empty() && "Unable to find unexpanded parameter packs");

  // We only care about unexpanded references to the RequiresExpr's own
  // parameter packs.
  auto Parms = RE->getLocalParameters();
  llvm::SmallPtrSet<NamedDecl*, 8> ParmSet(Parms.begin(), Parms.end());
  SmallVector<UnexpandedParameterPack, 2> UnexpandedParms;
  for (auto Parm : Unexpanded)
    if (ParmSet.contains(Parm.first.dyn_cast<NamedDecl *>()))
      UnexpandedParms.push_back(Parm);
  if (UnexpandedParms.empty())
    return false;

  return DiagnoseUnexpandedParameterPacks(RE->getBeginLoc(), UPPC_Requirement,
                                          UnexpandedParms);
}

bool Sema::DiagnoseUnexpandedParameterPack(const CXXScopeSpec &SS,
                                        UnexpandedParameterPackContext UPPC) {
  // C++0x [temp.variadic]p5:
  //   An appearance of a name of a parameter pack that is not expanded is
  //   ill-formed.
  if (!SS.getScopeRep() ||
      !SS.getScopeRep()->containsUnexpandedParameterPack())
    return false;

  SmallVector<UnexpandedParameterPack, 2> Unexpanded;
  CollectUnexpandedParameterPacksVisitor(Unexpanded)
    .TraverseNestedNameSpecifier(SS.getScopeRep());
  assert(!Unexpanded.empty() && "Unable to find unexpanded parameter packs");
  return DiagnoseUnexpandedParameterPacks(SS.getRange().getBegin(),
                                          UPPC, Unexpanded);
}

bool Sema::DiagnoseUnexpandedParameterPack(const DeclarationNameInfo &NameInfo,
                                         UnexpandedParameterPackContext UPPC) {
  // C++0x [temp.variadic]p5:
  //   An appearance of a name of a parameter pack that is not expanded is
  //   ill-formed.
  switch (NameInfo.getName().getNameKind()) {
  case DeclarationName::Identifier:
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
  case DeclarationName::CXXOperatorName:
  case DeclarationName::CXXLiteralOperatorName:
  case DeclarationName::CXXUsingDirective:
  case DeclarationName::CXXDeductionGuideName:
    return false;

  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    // FIXME: We shouldn't need this null check!
    if (TypeSourceInfo *TSInfo = NameInfo.getNamedTypeInfo())
      return DiagnoseUnexpandedParameterPack(NameInfo.getLoc(), TSInfo, UPPC);

    if (!NameInfo.getName().getCXXNameType()->containsUnexpandedParameterPack())
      return false;

    break;
  }

  SmallVector<UnexpandedParameterPack, 2> Unexpanded;
  CollectUnexpandedParameterPacksVisitor(Unexpanded)
    .TraverseType(NameInfo.getName().getCXXNameType());
  assert(!Unexpanded.empty() && "Unable to find unexpanded parameter packs");
  return DiagnoseUnexpandedParameterPacks(NameInfo.getLoc(), UPPC, Unexpanded);
}

bool Sema::DiagnoseUnexpandedParameterPack(SourceLocation Loc,
                                           TemplateName Template,
                                       UnexpandedParameterPackContext UPPC) {

  if (Template.isNull() || !Template.containsUnexpandedParameterPack())
    return false;

  SmallVector<UnexpandedParameterPack, 2> Unexpanded;
  CollectUnexpandedParameterPacksVisitor(Unexpanded)
    .TraverseTemplateName(Template);
  assert(!Unexpanded.empty() && "Unable to find unexpanded parameter packs");
  return DiagnoseUnexpandedParameterPacks(Loc, UPPC, Unexpanded);
}

bool Sema::DiagnoseUnexpandedParameterPack(TemplateArgumentLoc Arg,
                                         UnexpandedParameterPackContext UPPC) {
  if (Arg.getArgument().isNull() ||
      !Arg.getArgument().containsUnexpandedParameterPack())
    return false;

  SmallVector<UnexpandedParameterPack, 2> Unexpanded;
  CollectUnexpandedParameterPacksVisitor(Unexpanded)
    .TraverseTemplateArgumentLoc(Arg);
  assert(!Unexpanded.empty() && "Unable to find unexpanded parameter packs");
  return DiagnoseUnexpandedParameterPacks(Arg.getLocation(), UPPC, Unexpanded);
}

void Sema::collectUnexpandedParameterPacks(TemplateArgument Arg,
                   SmallVectorImpl<UnexpandedParameterPack> &Unexpanded) {
  CollectUnexpandedParameterPacksVisitor(Unexpanded)
    .TraverseTemplateArgument(Arg);
}

void Sema::collectUnexpandedParameterPacks(TemplateArgumentLoc Arg,
                   SmallVectorImpl<UnexpandedParameterPack> &Unexpanded) {
  CollectUnexpandedParameterPacksVisitor(Unexpanded)
    .TraverseTemplateArgumentLoc(Arg);
}

void Sema::collectUnexpandedParameterPacks(QualType T,
                   SmallVectorImpl<UnexpandedParameterPack> &Unexpanded) {
  CollectUnexpandedParameterPacksVisitor(Unexpanded).TraverseType(T);
}

void Sema::collectUnexpandedParameterPacks(TypeLoc TL,
                   SmallVectorImpl<UnexpandedParameterPack> &Unexpanded) {
  CollectUnexpandedParameterPacksVisitor(Unexpanded).TraverseTypeLoc(TL);
}

void Sema::collectUnexpandedParameterPacks(
    NestedNameSpecifierLoc NNS,
    SmallVectorImpl<UnexpandedParameterPack> &Unexpanded) {
  CollectUnexpandedParameterPacksVisitor(Unexpanded)
      .TraverseNestedNameSpecifierLoc(NNS);
}

void Sema::collectUnexpandedParameterPacks(
    const DeclarationNameInfo &NameInfo,
    SmallVectorImpl<UnexpandedParameterPack> &Unexpanded) {
  CollectUnexpandedParameterPacksVisitor(Unexpanded)
    .TraverseDeclarationNameInfo(NameInfo);
}

void Sema::collectUnexpandedParameterPacks(
    Expr *E, SmallVectorImpl<UnexpandedParameterPack> &Unexpanded) {
  CollectUnexpandedParameterPacksVisitor(Unexpanded).TraverseStmt(E);
}

ParsedTemplateArgument
Sema::ActOnPackExpansion(const ParsedTemplateArgument &Arg,
                         SourceLocation EllipsisLoc) {
  if (Arg.isInvalid())
    return Arg;

  switch (Arg.getKind()) {
  case ParsedTemplateArgument::Type: {
    TypeResult Result = ActOnPackExpansion(Arg.getAsType(), EllipsisLoc);
    if (Result.isInvalid())
      return ParsedTemplateArgument();

    return ParsedTemplateArgument(Arg.getKind(), Result.get().getAsOpaquePtr(),
                                  Arg.getLocation());
  }

  case ParsedTemplateArgument::NonType: {
    ExprResult Result = ActOnPackExpansion(Arg.getAsExpr(), EllipsisLoc);
    if (Result.isInvalid())
      return ParsedTemplateArgument();

    return ParsedTemplateArgument(Arg.getKind(), Result.get(),
                                  Arg.getLocation());
  }

  case ParsedTemplateArgument::Template:
    if (!Arg.getAsTemplate().get().containsUnexpandedParameterPack()) {
      SourceRange R(Arg.getLocation());
      if (Arg.getScopeSpec().isValid())
        R.setBegin(Arg.getScopeSpec().getBeginLoc());
      Diag(EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
        << R;
      return ParsedTemplateArgument();
    }

    return Arg.getTemplatePackExpansion(EllipsisLoc);
  }
  llvm_unreachable("Unhandled template argument kind?");
}

TypeResult Sema::ActOnPackExpansion(ParsedType Type,
                                    SourceLocation EllipsisLoc) {
  TypeSourceInfo *TSInfo;
  GetTypeFromParser(Type, &TSInfo);
  if (!TSInfo)
    return true;

  TypeSourceInfo *TSResult =
      CheckPackExpansion(TSInfo, EllipsisLoc, std::nullopt);
  if (!TSResult)
    return true;

  return CreateParsedType(TSResult->getType(), TSResult);
}

TypeSourceInfo *
Sema::CheckPackExpansion(TypeSourceInfo *Pattern, SourceLocation EllipsisLoc,
                         std::optional<unsigned> NumExpansions) {
  // Create the pack expansion type and source-location information.
  QualType Result = CheckPackExpansion(Pattern->getType(),
                                       Pattern->getTypeLoc().getSourceRange(),
                                       EllipsisLoc, NumExpansions);
  if (Result.isNull())
    return nullptr;

  TypeLocBuilder TLB;
  TLB.pushFullCopy(Pattern->getTypeLoc());
  PackExpansionTypeLoc TL = TLB.push<PackExpansionTypeLoc>(Result);
  TL.setEllipsisLoc(EllipsisLoc);

  return TLB.getTypeSourceInfo(Context, Result);
}

QualType Sema::CheckPackExpansion(QualType Pattern, SourceRange PatternRange,
                                  SourceLocation EllipsisLoc,
                                  std::optional<unsigned> NumExpansions) {
  // C++11 [temp.variadic]p5:
  //   The pattern of a pack expansion shall name one or more
  //   parameter packs that are not expanded by a nested pack
  //   expansion.
  //
  // A pattern containing a deduced type can't occur "naturally" but arises in
  // the desugaring of an init-capture pack.
  if (!Pattern->containsUnexpandedParameterPack() &&
      !Pattern->getContainedDeducedType()) {
    Diag(EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
      << PatternRange;
    return QualType();
  }

  return Context.getPackExpansionType(Pattern, NumExpansions,
                                      /*ExpectPackInType=*/false);
}

ExprResult Sema::ActOnPackExpansion(Expr *Pattern, SourceLocation EllipsisLoc) {
  return CheckPackExpansion(Pattern, EllipsisLoc, std::nullopt);
}

ExprResult Sema::CheckPackExpansion(Expr *Pattern, SourceLocation EllipsisLoc,
                                    std::optional<unsigned> NumExpansions) {
  if (!Pattern)
    return ExprError();

  // C++0x [temp.variadic]p5:
  //   The pattern of a pack expansion shall name one or more
  //   parameter packs that are not expanded by a nested pack
  //   expansion.
  if (!Pattern->containsUnexpandedParameterPack()) {
    Diag(EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
    << Pattern->getSourceRange();
    CorrectDelayedTyposInExpr(Pattern);
    return ExprError();
  }

  // Create the pack expansion expression and source-location information.
  return new (Context)
    PackExpansionExpr(Context.DependentTy, Pattern, EllipsisLoc, NumExpansions);
}

bool Sema::CheckParameterPacksForExpansion(
    SourceLocation EllipsisLoc, SourceRange PatternRange,
    ArrayRef<UnexpandedParameterPack> Unexpanded,
    const MultiLevelTemplateArgumentList &TemplateArgs, bool &ShouldExpand,
    bool &RetainExpansion, std::optional<unsigned> &NumExpansions) {
  ShouldExpand = true;
  RetainExpansion = false;
  std::pair<IdentifierInfo *, SourceLocation> FirstPack;
  bool HaveFirstPack = false;
  std::optional<unsigned> NumPartialExpansions;
  SourceLocation PartiallySubstitutedPackLoc;

  for (UnexpandedParameterPack ParmPack : Unexpanded) {
    // Compute the depth and index for this parameter pack.
    unsigned Depth = 0, Index = 0;
    IdentifierInfo *Name;
    bool IsVarDeclPack = false;

    if (const TemplateTypeParmType *TTP =
            ParmPack.first.dyn_cast<const TemplateTypeParmType *>()) {
      Depth = TTP->getDepth();
      Index = TTP->getIndex();
      Name = TTP->getIdentifier();
    } else {
      NamedDecl *ND = ParmPack.first.get<NamedDecl *>();
      if (isa<VarDecl>(ND))
        IsVarDeclPack = true;
      else
        std::tie(Depth, Index) = getDepthAndIndex(ND);

      Name = ND->getIdentifier();
    }

    // Determine the size of this argument pack.
    unsigned NewPackSize;
    if (IsVarDeclPack) {
      // Figure out whether we're instantiating to an argument pack or not.
      typedef LocalInstantiationScope::DeclArgumentPack DeclArgumentPack;

      llvm::PointerUnion<Decl *, DeclArgumentPack *> *Instantiation =
          CurrentInstantiationScope->findInstantiationOf(
              ParmPack.first.get<NamedDecl *>());
      if (Instantiation->is<DeclArgumentPack *>()) {
        // We could expand this function parameter pack.
        NewPackSize = Instantiation->get<DeclArgumentPack *>()->size();
      } else {
        // We can't expand this function parameter pack, so we can't expand
        // the pack expansion.
        ShouldExpand = false;
        continue;
      }
    } else {
      // If we don't have a template argument at this depth/index, then we
      // cannot expand the pack expansion. Make a note of this, but we still
      // want to check any parameter packs we *do* have arguments for.
      if (Depth >= TemplateArgs.getNumLevels() ||
          !TemplateArgs.hasTemplateArgument(Depth, Index)) {
        ShouldExpand = false;
        continue;
      }

      // Determine the size of the argument pack.
      NewPackSize = TemplateArgs(Depth, Index).pack_size();
    }

    // C++0x [temp.arg.explicit]p9:
    //   Template argument deduction can extend the sequence of template
    //   arguments corresponding to a template parameter pack, even when the
    //   sequence contains explicitly specified template arguments.
    if (!IsVarDeclPack && CurrentInstantiationScope) {
      if (NamedDecl *PartialPack =
              CurrentInstantiationScope->getPartiallySubstitutedPack()) {
        unsigned PartialDepth, PartialIndex;
        std::tie(PartialDepth, PartialIndex) = getDepthAndIndex(PartialPack);
        if (PartialDepth == Depth && PartialIndex == Index) {
          RetainExpansion = true;
          // We don't actually know the new pack size yet.
          NumPartialExpansions = NewPackSize;
          PartiallySubstitutedPackLoc = ParmPack.second;
          continue;
        }
      }
    }

    if (!NumExpansions) {
      // The is the first pack we've seen for which we have an argument.
      // Record it.
      NumExpansions = NewPackSize;
      FirstPack.first = Name;
      FirstPack.second = ParmPack.second;
      HaveFirstPack = true;
      continue;
    }

    if (NewPackSize != *NumExpansions) {
      // C++0x [temp.variadic]p5:
      //   All of the parameter packs expanded by a pack expansion shall have
      //   the same number of arguments specified.
      if (HaveFirstPack)
        Diag(EllipsisLoc, diag::err_pack_expansion_length_conflict)
            << FirstPack.first << Name << *NumExpansions << NewPackSize
            << SourceRange(FirstPack.second) << SourceRange(ParmPack.second);
      else
        Diag(EllipsisLoc, diag::err_pack_expansion_length_conflict_multilevel)
            << Name << *NumExpansions << NewPackSize
            << SourceRange(ParmPack.second);
      return true;
    }
  }

  // If we're performing a partial expansion but we also have a full expansion,
  // expand to the number of common arguments. For example, given:
  //
  //   template<typename ...T> struct A {
  //     template<typename ...U> void f(pair<T, U>...);
  //   };
  //
  // ... a call to 'A<int, int>().f<int>' should expand the pack once and
  // retain an expansion.
  if (NumPartialExpansions) {
    if (NumExpansions && *NumExpansions < *NumPartialExpansions) {
      NamedDecl *PartialPack =
          CurrentInstantiationScope->getPartiallySubstitutedPack();
      Diag(EllipsisLoc, diag::err_pack_expansion_length_conflict_partial)
          << PartialPack << *NumPartialExpansions << *NumExpansions
          << SourceRange(PartiallySubstitutedPackLoc);
      return true;
    }

    NumExpansions = NumPartialExpansions;
  }

  return false;
}

std::optional<unsigned> Sema::getNumArgumentsInExpansion(
    QualType T, const MultiLevelTemplateArgumentList &TemplateArgs) {
  QualType Pattern = cast<PackExpansionType>(T)->getPattern();
  SmallVector<UnexpandedParameterPack, 2> Unexpanded;
  CollectUnexpandedParameterPacksVisitor(Unexpanded).TraverseType(Pattern);

  std::optional<unsigned> Result;
  for (unsigned I = 0, N = Unexpanded.size(); I != N; ++I) {
    // Compute the depth and index for this parameter pack.
    unsigned Depth;
    unsigned Index;

    if (const TemplateTypeParmType *TTP =
            Unexpanded[I].first.dyn_cast<const TemplateTypeParmType *>()) {
      Depth = TTP->getDepth();
      Index = TTP->getIndex();
    } else {
      NamedDecl *ND = Unexpanded[I].first.get<NamedDecl *>();
      if (isa<VarDecl>(ND)) {
        // Function parameter pack or init-capture pack.
        typedef LocalInstantiationScope::DeclArgumentPack DeclArgumentPack;

        llvm::PointerUnion<Decl *, DeclArgumentPack *> *Instantiation =
            CurrentInstantiationScope->findInstantiationOf(
                Unexpanded[I].first.get<NamedDecl *>());
        if (Instantiation->is<Decl *>())
          // The pattern refers to an unexpanded pack. We're not ready to expand
          // this pack yet.
          return std::nullopt;

        unsigned Size = Instantiation->get<DeclArgumentPack *>()->size();
        assert((!Result || *Result == Size) && "inconsistent pack sizes");
        Result = Size;
        continue;
      }

      std::tie(Depth, Index) = getDepthAndIndex(ND);
    }
    if (Depth >= TemplateArgs.getNumLevels() ||
        !TemplateArgs.hasTemplateArgument(Depth, Index))
      // The pattern refers to an unknown template argument. We're not ready to
      // expand this pack yet.
      return std::nullopt;

    // Determine the size of the argument pack.
    unsigned Size = TemplateArgs(Depth, Index).pack_size();
    assert((!Result || *Result == Size) && "inconsistent pack sizes");
    Result = Size;
  }

  return Result;
}

bool Sema::containsUnexpandedParameterPacks(Declarator &D) {
  const DeclSpec &DS = D.getDeclSpec();
  switch (DS.getTypeSpecType()) {
  case TST_typename_pack_indexing:
  case TST_typename:
  case TST_typeof_unqualType:
  case TST_typeofType:
#define TRANSFORM_TYPE_TRAIT_DEF(_, Trait) case TST_##Trait:
#include "clang/Basic/TransformTypeTraits.def"
  case TST_atomic: {
    QualType T = DS.getRepAsType().get();
    if (!T.isNull() && T->containsUnexpandedParameterPack())
      return true;
    break;
  }

  case TST_typeof_unqualExpr:
  case TST_typeofExpr:
  case TST_decltype:
  case TST_bitint:
    if (DS.getRepAsExpr() &&
        DS.getRepAsExpr()->containsUnexpandedParameterPack())
      return true;
    break;

  case TST_unspecified:
  case TST_void:
  case TST_char:
  case TST_wchar:
  case TST_char8:
  case TST_char16:
  case TST_char32:
  case TST_int:
  case TST_int128:
  case TST_half:
  case TST_float:
  case TST_double:
  case TST_Accum:
  case TST_Fract:
  case TST_Float16:
  case TST_float128:
  case TST_ibm128:
  case TST_bool:
  case TST_decimal32:
  case TST_decimal64:
  case TST_decimal128:
  case TST_enum:
  case TST_union:
  case TST_struct:
  case TST_interface:
  case TST_class:
  case TST_auto:
  case TST_auto_type:
  case TST_decltype_auto:
  case TST_BFloat16:
#define GENERIC_IMAGE_TYPE(ImgType, Id) case TST_##ImgType##_t:
#include "clang/Basic/OpenCLImageTypes.def"
  case TST_unknown_anytype:
  case TST_error:
    break;
  }

  for (unsigned I = 0, N = D.getNumTypeObjects(); I != N; ++I) {
    const DeclaratorChunk &Chunk = D.getTypeObject(I);
    switch (Chunk.Kind) {
    case DeclaratorChunk::Pointer:
    case DeclaratorChunk::Reference:
    case DeclaratorChunk::Paren:
    case DeclaratorChunk::Pipe:
    case DeclaratorChunk::BlockPointer:
      // These declarator chunks cannot contain any parameter packs.
      break;

    case DeclaratorChunk::Array:
      if (Chunk.Arr.NumElts &&
          Chunk.Arr.NumElts->containsUnexpandedParameterPack())
        return true;
      break;
    case DeclaratorChunk::Function:
      for (unsigned i = 0, e = Chunk.Fun.NumParams; i != e; ++i) {
        ParmVarDecl *Param = cast<ParmVarDecl>(Chunk.Fun.Params[i].Param);
        QualType ParamTy = Param->getType();
        assert(!ParamTy.isNull() && "Couldn't parse type?");
        if (ParamTy->containsUnexpandedParameterPack()) return true;
      }

      if (Chunk.Fun.getExceptionSpecType() == EST_Dynamic) {
        for (unsigned i = 0; i != Chunk.Fun.getNumExceptions(); ++i) {
          if (Chunk.Fun.Exceptions[i]
                  .Ty.get()
                  ->containsUnexpandedParameterPack())
            return true;
        }
      } else if (isComputedNoexcept(Chunk.Fun.getExceptionSpecType()) &&
                 Chunk.Fun.NoexceptExpr->containsUnexpandedParameterPack())
        return true;

      if (Chunk.Fun.hasTrailingReturnType()) {
        QualType T = Chunk.Fun.getTrailingReturnType().get();
        if (!T.isNull() && T->containsUnexpandedParameterPack())
          return true;
      }
      break;

    case DeclaratorChunk::MemberPointer:
      if (Chunk.Mem.Scope().getScopeRep() &&
          Chunk.Mem.Scope().getScopeRep()->containsUnexpandedParameterPack())
        return true;
      break;
    }
  }

  if (Expr *TRC = D.getTrailingRequiresClause())
    if (TRC->containsUnexpandedParameterPack())
      return true;

  return false;
}

namespace {

// Callback to only accept typo corrections that refer to parameter packs.
class ParameterPackValidatorCCC final : public CorrectionCandidateCallback {
 public:
  bool ValidateCandidate(const TypoCorrection &candidate) override {
    NamedDecl *ND = candidate.getCorrectionDecl();
    return ND && ND->isParameterPack();
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<ParameterPackValidatorCCC>(*this);
  }
};

}

ExprResult Sema::ActOnSizeofParameterPackExpr(Scope *S,
                                              SourceLocation OpLoc,
                                              IdentifierInfo &Name,
                                              SourceLocation NameLoc,
                                              SourceLocation RParenLoc) {
  // C++0x [expr.sizeof]p5:
  //   The identifier in a sizeof... expression shall name a parameter pack.
  LookupResult R(*this, &Name, NameLoc, LookupOrdinaryName);
  LookupName(R, S);

  NamedDecl *ParameterPack = nullptr;
  switch (R.getResultKind()) {
  case LookupResult::Found:
    ParameterPack = R.getFoundDecl();
    break;

  case LookupResult::NotFound:
  case LookupResult::NotFoundInCurrentInstantiation: {
    ParameterPackValidatorCCC CCC{};
    if (TypoCorrection Corrected =
            CorrectTypo(R.getLookupNameInfo(), R.getLookupKind(), S, nullptr,
                        CCC, CTK_ErrorRecovery)) {
      diagnoseTypo(Corrected,
                   PDiag(diag::err_sizeof_pack_no_pack_name_suggest) << &Name,
                   PDiag(diag::note_parameter_pack_here));
      ParameterPack = Corrected.getCorrectionDecl();
    }
    break;
  }
  case LookupResult::FoundOverloaded:
  case LookupResult::FoundUnresolvedValue:
    break;

  case LookupResult::Ambiguous:
    DiagnoseAmbiguousLookup(R);
    return ExprError();
  }

  if (!ParameterPack || !ParameterPack->isParameterPack()) {
    Diag(NameLoc, diag::err_expected_name_of_pack) << &Name;
    return ExprError();
  }

  MarkAnyDeclReferenced(OpLoc, ParameterPack, true);

  return SizeOfPackExpr::Create(Context, OpLoc, ParameterPack, NameLoc,
                                RParenLoc);
}

static bool isParameterPack(Expr *PackExpression) {
  if (auto *D = dyn_cast<DeclRefExpr>(PackExpression); D) {
    ValueDecl *VD = D->getDecl();
    return VD->isParameterPack();
  }
  return false;
}

ExprResult Sema::ActOnPackIndexingExpr(Scope *S, Expr *PackExpression,
                                       SourceLocation EllipsisLoc,
                                       SourceLocation LSquareLoc,
                                       Expr *IndexExpr,
                                       SourceLocation RSquareLoc) {
  bool isParameterPack = ::isParameterPack(PackExpression);
  if (!isParameterPack) {
    if (!PackExpression->containsErrors()) {
      CorrectDelayedTyposInExpr(IndexExpr);
      Diag(PackExpression->getBeginLoc(), diag::err_expected_name_of_pack)
          << PackExpression;
    }
    return ExprError();
  }
  ExprResult Res =
      BuildPackIndexingExpr(PackExpression, EllipsisLoc, IndexExpr, RSquareLoc);
  if (!Res.isInvalid())
    Diag(Res.get()->getBeginLoc(), getLangOpts().CPlusPlus26
                                       ? diag::warn_cxx23_pack_indexing
                                       : diag::ext_pack_indexing);
  return Res;
}

ExprResult
Sema::BuildPackIndexingExpr(Expr *PackExpression, SourceLocation EllipsisLoc,
                            Expr *IndexExpr, SourceLocation RSquareLoc,
                            ArrayRef<Expr *> ExpandedExprs, bool EmptyPack) {

  std::optional<int64_t> Index;
  if (!IndexExpr->isInstantiationDependent()) {
    llvm::APSInt Value(Context.getIntWidth(Context.getSizeType()));

    ExprResult Res = CheckConvertedConstantExpression(
        IndexExpr, Context.getSizeType(), Value, CCEK_ArrayBound);
    if (!Res.isUsable())
      return ExprError();
    Index = Value.getExtValue();
    IndexExpr = Res.get();
  }

  if (Index && (!ExpandedExprs.empty() || EmptyPack)) {
    if (*Index < 0 || EmptyPack || *Index >= int64_t(ExpandedExprs.size())) {
      Diag(PackExpression->getBeginLoc(), diag::err_pack_index_out_of_bound)
          << *Index << PackExpression << ExpandedExprs.size();
      return ExprError();
    }
  }

  return PackIndexingExpr::Create(getASTContext(), EllipsisLoc, RSquareLoc,
                                  PackExpression, IndexExpr, Index,
                                  ExpandedExprs, EmptyPack);
}

TemplateArgumentLoc Sema::getTemplateArgumentPackExpansionPattern(
    TemplateArgumentLoc OrigLoc, SourceLocation &Ellipsis,
    std::optional<unsigned> &NumExpansions) const {
  const TemplateArgument &Argument = OrigLoc.getArgument();
  assert(Argument.isPackExpansion());
  switch (Argument.getKind()) {
  case TemplateArgument::Type: {
    // FIXME: We shouldn't ever have to worry about missing
    // type-source info!
    TypeSourceInfo *ExpansionTSInfo = OrigLoc.getTypeSourceInfo();
    if (!ExpansionTSInfo)
      ExpansionTSInfo = Context.getTrivialTypeSourceInfo(Argument.getAsType(),
                                                         Ellipsis);
    PackExpansionTypeLoc Expansion =
        ExpansionTSInfo->getTypeLoc().castAs<PackExpansionTypeLoc>();
    Ellipsis = Expansion.getEllipsisLoc();

    TypeLoc Pattern = Expansion.getPatternLoc();
    NumExpansions = Expansion.getTypePtr()->getNumExpansions();

    // We need to copy the TypeLoc because TemplateArgumentLocs store a
    // TypeSourceInfo.
    // FIXME: Find some way to avoid the copy?
    TypeLocBuilder TLB;
    TLB.pushFullCopy(Pattern);
    TypeSourceInfo *PatternTSInfo =
        TLB.getTypeSourceInfo(Context, Pattern.getType());
    return TemplateArgumentLoc(TemplateArgument(Pattern.getType()),
                               PatternTSInfo);
  }

  case TemplateArgument::Expression: {
    PackExpansionExpr *Expansion
      = cast<PackExpansionExpr>(Argument.getAsExpr());
    Expr *Pattern = Expansion->getPattern();
    Ellipsis = Expansion->getEllipsisLoc();
    NumExpansions = Expansion->getNumExpansions();
    return TemplateArgumentLoc(Pattern, Pattern);
  }

  case TemplateArgument::TemplateExpansion:
    Ellipsis = OrigLoc.getTemplateEllipsisLoc();
    NumExpansions = Argument.getNumTemplateExpansions();
    return TemplateArgumentLoc(Context, Argument.getPackExpansionPattern(),
                               OrigLoc.getTemplateQualifierLoc(),
                               OrigLoc.getTemplateNameLoc());

  case TemplateArgument::Declaration:
  case TemplateArgument::NullPtr:
  case TemplateArgument::Template:
  case TemplateArgument::Integral:
  case TemplateArgument::StructuralValue:
  case TemplateArgument::Pack:
  case TemplateArgument::Null:
    return TemplateArgumentLoc();
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

std::optional<unsigned> Sema::getFullyPackExpandedSize(TemplateArgument Arg) {
  assert(Arg.containsUnexpandedParameterPack());

  // If this is a substituted pack, grab that pack. If not, we don't know
  // the size yet.
  // FIXME: We could find a size in more cases by looking for a substituted
  // pack anywhere within this argument, but that's not necessary in the common
  // case for 'sizeof...(A)' handling.
  TemplateArgument Pack;
  switch (Arg.getKind()) {
  case TemplateArgument::Type:
    if (auto *Subst = Arg.getAsType()->getAs<SubstTemplateTypeParmPackType>())
      Pack = Subst->getArgumentPack();
    else
      return std::nullopt;
    break;

  case TemplateArgument::Expression:
    if (auto *Subst =
            dyn_cast<SubstNonTypeTemplateParmPackExpr>(Arg.getAsExpr()))
      Pack = Subst->getArgumentPack();
    else if (auto *Subst = dyn_cast<FunctionParmPackExpr>(Arg.getAsExpr()))  {
      for (VarDecl *PD : *Subst)
        if (PD->isParameterPack())
          return std::nullopt;
      return Subst->getNumExpansions();
    } else
      return std::nullopt;
    break;

  case TemplateArgument::Template:
    if (SubstTemplateTemplateParmPackStorage *Subst =
            Arg.getAsTemplate().getAsSubstTemplateTemplateParmPack())
      Pack = Subst->getArgumentPack();
    else
      return std::nullopt;
    break;

  case TemplateArgument::Declaration:
  case TemplateArgument::NullPtr:
  case TemplateArgument::TemplateExpansion:
  case TemplateArgument::Integral:
  case TemplateArgument::StructuralValue:
  case TemplateArgument::Pack:
  case TemplateArgument::Null:
    return std::nullopt;
  }

  // Check that no argument in the pack is itself a pack expansion.
  for (TemplateArgument Elem : Pack.pack_elements()) {
    // There's no point recursing in this case; we would have already
    // expanded this pack expansion into the enclosing pack if we could.
    if (Elem.isPackExpansion())
      return std::nullopt;
    // Don't guess the size of unexpanded packs. The pack within a template
    // argument may have yet to be of a PackExpansion type before we see the
    // ellipsis in the annotation stage.
    //
    // This doesn't mean we would invalidate the optimization: Arg can be an
    // unexpanded pack regardless of Elem's dependence. For instance,
    // A TemplateArgument that contains either a SubstTemplateTypeParmPackType
    // or SubstNonTypeTemplateParmPackExpr is always considered Unexpanded, but
    // the underlying TemplateArgument thereof may not.
    if (Elem.containsUnexpandedParameterPack())
      return std::nullopt;
  }
  return Pack.pack_size();
}

static void CheckFoldOperand(Sema &S, Expr *E) {
  if (!E)
    return;

  E = E->IgnoreImpCasts();
  auto *OCE = dyn_cast<CXXOperatorCallExpr>(E);
  if ((OCE && OCE->isInfixBinaryOp()) || isa<BinaryOperator>(E) ||
      isa<AbstractConditionalOperator>(E)) {
    S.Diag(E->getExprLoc(), diag::err_fold_expression_bad_operand)
        << E->getSourceRange()
        << FixItHint::CreateInsertion(E->getBeginLoc(), "(")
        << FixItHint::CreateInsertion(E->getEndLoc(), ")");
  }
}

ExprResult Sema::ActOnCXXFoldExpr(Scope *S, SourceLocation LParenLoc, Expr *LHS,
                                  tok::TokenKind Operator,
                                  SourceLocation EllipsisLoc, Expr *RHS,
                                  SourceLocation RParenLoc) {
  // LHS and RHS must be cast-expressions. We allow an arbitrary expression
  // in the parser and reduce down to just cast-expressions here.
  CheckFoldOperand(*this, LHS);
  CheckFoldOperand(*this, RHS);

  auto DiscardOperands = [&] {
    CorrectDelayedTyposInExpr(LHS);
    CorrectDelayedTyposInExpr(RHS);
  };

  // [expr.prim.fold]p3:
  //   In a binary fold, op1 and op2 shall be the same fold-operator, and
  //   either e1 shall contain an unexpanded parameter pack or e2 shall contain
  //   an unexpanded parameter pack, but not both.
  if (LHS && RHS &&
      LHS->containsUnexpandedParameterPack() ==
          RHS->containsUnexpandedParameterPack()) {
    DiscardOperands();
    return Diag(EllipsisLoc,
                LHS->containsUnexpandedParameterPack()
                    ? diag::err_fold_expression_packs_both_sides
                    : diag::err_pack_expansion_without_parameter_packs)
        << LHS->getSourceRange() << RHS->getSourceRange();
  }

  // [expr.prim.fold]p2:
  //   In a unary fold, the cast-expression shall contain an unexpanded
  //   parameter pack.
  if (!LHS || !RHS) {
    Expr *Pack = LHS ? LHS : RHS;
    assert(Pack && "fold expression with neither LHS nor RHS");
    if (!Pack->containsUnexpandedParameterPack()) {
      DiscardOperands();
      return Diag(EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
             << Pack->getSourceRange();
    }
  }

  BinaryOperatorKind Opc = ConvertTokenKindToBinaryOpcode(Operator);

  // Perform first-phase name lookup now.
  UnresolvedLookupExpr *ULE = nullptr;
  {
    UnresolvedSet<16> Functions;
    LookupBinOp(S, EllipsisLoc, Opc, Functions);
    if (!Functions.empty()) {
      DeclarationName OpName = Context.DeclarationNames.getCXXOperatorName(
          BinaryOperator::getOverloadedOperator(Opc));
      ExprResult Callee = CreateUnresolvedLookupExpr(
          /*NamingClass*/ nullptr, NestedNameSpecifierLoc(),
          DeclarationNameInfo(OpName, EllipsisLoc), Functions);
      if (Callee.isInvalid())
        return ExprError();
      ULE = cast<UnresolvedLookupExpr>(Callee.get());
    }
  }

  return BuildCXXFoldExpr(ULE, LParenLoc, LHS, Opc, EllipsisLoc, RHS, RParenLoc,
                          std::nullopt);
}

ExprResult Sema::BuildCXXFoldExpr(UnresolvedLookupExpr *Callee,
                                  SourceLocation LParenLoc, Expr *LHS,
                                  BinaryOperatorKind Operator,
                                  SourceLocation EllipsisLoc, Expr *RHS,
                                  SourceLocation RParenLoc,
                                  std::optional<unsigned> NumExpansions) {
  return new (Context)
      CXXFoldExpr(Context.DependentTy, Callee, LParenLoc, LHS, Operator,
                  EllipsisLoc, RHS, RParenLoc, NumExpansions);
}

ExprResult Sema::BuildEmptyCXXFoldExpr(SourceLocation EllipsisLoc,
                                       BinaryOperatorKind Operator) {
  // [temp.variadic]p9:
  //   If N is zero for a unary fold-expression, the value of the expression is
  //       &&  ->  true
  //       ||  ->  false
  //       ,   ->  void()
  //   if the operator is not listed [above], the instantiation is ill-formed.
  //
  // Note that we need to use something like int() here, not merely 0, to
  // prevent the result from being a null pointer constant.
  QualType ScalarType;
  switch (Operator) {
  case BO_LOr:
    return ActOnCXXBoolLiteral(EllipsisLoc, tok::kw_false);
  case BO_LAnd:
    return ActOnCXXBoolLiteral(EllipsisLoc, tok::kw_true);
  case BO_Comma:
    ScalarType = Context.VoidTy;
    break;

  default:
    return Diag(EllipsisLoc, diag::err_fold_expression_empty)
        << BinaryOperator::getOpcodeStr(Operator);
  }

  return new (Context) CXXScalarValueInitExpr(
      ScalarType, Context.getTrivialTypeSourceInfo(ScalarType, EllipsisLoc),
      EllipsisLoc);
}
