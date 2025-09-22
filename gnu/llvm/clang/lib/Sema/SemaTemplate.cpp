//===------- SemaTemplate.cpp - Semantic Analysis for C++ Templates -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for C++ templates.
//===----------------------------------------------------------------------===//

#include "TreeTransform.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/TypeVisitor.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Stack.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/Template.h"
#include "clang/Sema/TemplateDeduction.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"

#include <iterator>
#include <optional>
using namespace clang;
using namespace sema;

// Exported for use by Parser.
SourceRange
clang::getTemplateParamsRange(TemplateParameterList const * const *Ps,
                              unsigned N) {
  if (!N) return SourceRange();
  return SourceRange(Ps[0]->getTemplateLoc(), Ps[N-1]->getRAngleLoc());
}

unsigned Sema::getTemplateDepth(Scope *S) const {
  unsigned Depth = 0;

  // Each template parameter scope represents one level of template parameter
  // depth.
  for (Scope *TempParamScope = S->getTemplateParamParent(); TempParamScope;
       TempParamScope = TempParamScope->getParent()->getTemplateParamParent()) {
    ++Depth;
  }

  // Note that there are template parameters with the given depth.
  auto ParamsAtDepth = [&](unsigned D) { Depth = std::max(Depth, D + 1); };

  // Look for parameters of an enclosing generic lambda. We don't create a
  // template parameter scope for these.
  for (FunctionScopeInfo *FSI : getFunctionScopes()) {
    if (auto *LSI = dyn_cast<LambdaScopeInfo>(FSI)) {
      if (!LSI->TemplateParams.empty()) {
        ParamsAtDepth(LSI->AutoTemplateParameterDepth);
        break;
      }
      if (LSI->GLTemplateParameterList) {
        ParamsAtDepth(LSI->GLTemplateParameterList->getDepth());
        break;
      }
    }
  }

  // Look for parameters of an enclosing terse function template. We don't
  // create a template parameter scope for these either.
  for (const InventedTemplateParameterInfo &Info :
       getInventedParameterInfos()) {
    if (!Info.TemplateParams.empty()) {
      ParamsAtDepth(Info.AutoTemplateParameterDepth);
      break;
    }
  }

  return Depth;
}

/// \brief Determine whether the declaration found is acceptable as the name
/// of a template and, if so, return that template declaration. Otherwise,
/// returns null.
///
/// Note that this may return an UnresolvedUsingValueDecl if AllowDependent
/// is true. In all other cases it will return a TemplateDecl (or null).
NamedDecl *Sema::getAsTemplateNameDecl(NamedDecl *D,
                                       bool AllowFunctionTemplates,
                                       bool AllowDependent) {
  D = D->getUnderlyingDecl();

  if (isa<TemplateDecl>(D)) {
    if (!AllowFunctionTemplates && isa<FunctionTemplateDecl>(D))
      return nullptr;

    return D;
  }

  if (const auto *Record = dyn_cast<CXXRecordDecl>(D)) {
    // C++ [temp.local]p1:
    //   Like normal (non-template) classes, class templates have an
    //   injected-class-name (Clause 9). The injected-class-name
    //   can be used with or without a template-argument-list. When
    //   it is used without a template-argument-list, it is
    //   equivalent to the injected-class-name followed by the
    //   template-parameters of the class template enclosed in
    //   <>. When it is used with a template-argument-list, it
    //   refers to the specified class template specialization,
    //   which could be the current specialization or another
    //   specialization.
    if (Record->isInjectedClassName()) {
      Record = cast<CXXRecordDecl>(Record->getDeclContext());
      if (Record->getDescribedClassTemplate())
        return Record->getDescribedClassTemplate();

      if (const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(Record))
        return Spec->getSpecializedTemplate();
    }

    return nullptr;
  }

  // 'using Dependent::foo;' can resolve to a template name.
  // 'using typename Dependent::foo;' cannot (not even if 'foo' is an
  // injected-class-name).
  if (AllowDependent && isa<UnresolvedUsingValueDecl>(D))
    return D;

  return nullptr;
}

void Sema::FilterAcceptableTemplateNames(LookupResult &R,
                                         bool AllowFunctionTemplates,
                                         bool AllowDependent) {
  LookupResult::Filter filter = R.makeFilter();
  while (filter.hasNext()) {
    NamedDecl *Orig = filter.next();
    if (!getAsTemplateNameDecl(Orig, AllowFunctionTemplates, AllowDependent))
      filter.erase();
  }
  filter.done();
}

bool Sema::hasAnyAcceptableTemplateNames(LookupResult &R,
                                         bool AllowFunctionTemplates,
                                         bool AllowDependent,
                                         bool AllowNonTemplateFunctions) {
  for (LookupResult::iterator I = R.begin(), IEnd = R.end(); I != IEnd; ++I) {
    if (getAsTemplateNameDecl(*I, AllowFunctionTemplates, AllowDependent))
      return true;
    if (AllowNonTemplateFunctions &&
        isa<FunctionDecl>((*I)->getUnderlyingDecl()))
      return true;
  }

  return false;
}

TemplateNameKind Sema::isTemplateName(Scope *S,
                                      CXXScopeSpec &SS,
                                      bool hasTemplateKeyword,
                                      const UnqualifiedId &Name,
                                      ParsedType ObjectTypePtr,
                                      bool EnteringContext,
                                      TemplateTy &TemplateResult,
                                      bool &MemberOfUnknownSpecialization,
                                      bool Disambiguation) {
  assert(getLangOpts().CPlusPlus && "No template names in C!");

  DeclarationName TName;
  MemberOfUnknownSpecialization = false;

  switch (Name.getKind()) {
  case UnqualifiedIdKind::IK_Identifier:
    TName = DeclarationName(Name.Identifier);
    break;

  case UnqualifiedIdKind::IK_OperatorFunctionId:
    TName = Context.DeclarationNames.getCXXOperatorName(
                                              Name.OperatorFunctionId.Operator);
    break;

  case UnqualifiedIdKind::IK_LiteralOperatorId:
    TName = Context.DeclarationNames.getCXXLiteralOperatorName(Name.Identifier);
    break;

  default:
    return TNK_Non_template;
  }

  QualType ObjectType = ObjectTypePtr.get();

  AssumedTemplateKind AssumedTemplate;
  LookupResult R(*this, TName, Name.getBeginLoc(), LookupOrdinaryName);
  if (LookupTemplateName(R, S, SS, ObjectType, EnteringContext,
                         /*RequiredTemplate=*/SourceLocation(),
                         &AssumedTemplate,
                         /*AllowTypoCorrection=*/!Disambiguation))
    return TNK_Non_template;
  MemberOfUnknownSpecialization = R.wasNotFoundInCurrentInstantiation();

  if (AssumedTemplate != AssumedTemplateKind::None) {
    TemplateResult = TemplateTy::make(Context.getAssumedTemplateName(TName));
    // Let the parser know whether we found nothing or found functions; if we
    // found nothing, we want to more carefully check whether this is actually
    // a function template name versus some other kind of undeclared identifier.
    return AssumedTemplate == AssumedTemplateKind::FoundNothing
               ? TNK_Undeclared_template
               : TNK_Function_template;
  }

  if (R.empty())
    return TNK_Non_template;

  NamedDecl *D = nullptr;
  UsingShadowDecl *FoundUsingShadow = dyn_cast<UsingShadowDecl>(*R.begin());
  if (R.isAmbiguous()) {
    // If we got an ambiguity involving a non-function template, treat this
    // as a template name, and pick an arbitrary template for error recovery.
    bool AnyFunctionTemplates = false;
    for (NamedDecl *FoundD : R) {
      if (NamedDecl *FoundTemplate = getAsTemplateNameDecl(FoundD)) {
        if (isa<FunctionTemplateDecl>(FoundTemplate))
          AnyFunctionTemplates = true;
        else {
          D = FoundTemplate;
          FoundUsingShadow = dyn_cast<UsingShadowDecl>(FoundD);
          break;
        }
      }
    }

    // If we didn't find any templates at all, this isn't a template name.
    // Leave the ambiguity for a later lookup to diagnose.
    if (!D && !AnyFunctionTemplates) {
      R.suppressDiagnostics();
      return TNK_Non_template;
    }

    // If the only templates were function templates, filter out the rest.
    // We'll diagnose the ambiguity later.
    if (!D)
      FilterAcceptableTemplateNames(R);
  }

  // At this point, we have either picked a single template name declaration D
  // or we have a non-empty set of results R containing either one template name
  // declaration or a set of function templates.

  TemplateName Template;
  TemplateNameKind TemplateKind;

  unsigned ResultCount = R.end() - R.begin();
  if (!D && ResultCount > 1) {
    // We assume that we'll preserve the qualifier from a function
    // template name in other ways.
    Template = Context.getOverloadedTemplateName(R.begin(), R.end());
    TemplateKind = TNK_Function_template;

    // We'll do this lookup again later.
    R.suppressDiagnostics();
  } else {
    if (!D) {
      D = getAsTemplateNameDecl(*R.begin());
      assert(D && "unambiguous result is not a template name");
    }

    if (isa<UnresolvedUsingValueDecl>(D)) {
      // We don't yet know whether this is a template-name or not.
      MemberOfUnknownSpecialization = true;
      return TNK_Non_template;
    }

    TemplateDecl *TD = cast<TemplateDecl>(D);
    Template =
        FoundUsingShadow ? TemplateName(FoundUsingShadow) : TemplateName(TD);
    assert(!FoundUsingShadow || FoundUsingShadow->getTargetDecl() == TD);
    if (!SS.isInvalid()) {
      NestedNameSpecifier *Qualifier = SS.getScopeRep();
      Template = Context.getQualifiedTemplateName(Qualifier, hasTemplateKeyword,
                                                  Template);
    }

    if (isa<FunctionTemplateDecl>(TD)) {
      TemplateKind = TNK_Function_template;

      // We'll do this lookup again later.
      R.suppressDiagnostics();
    } else {
      assert(isa<ClassTemplateDecl>(TD) || isa<TemplateTemplateParmDecl>(TD) ||
             isa<TypeAliasTemplateDecl>(TD) || isa<VarTemplateDecl>(TD) ||
             isa<BuiltinTemplateDecl>(TD) || isa<ConceptDecl>(TD));
      TemplateKind =
          isa<VarTemplateDecl>(TD) ? TNK_Var_template :
          isa<ConceptDecl>(TD) ? TNK_Concept_template :
          TNK_Type_template;
    }
  }

  TemplateResult = TemplateTy::make(Template);
  return TemplateKind;
}

bool Sema::isDeductionGuideName(Scope *S, const IdentifierInfo &Name,
                                SourceLocation NameLoc, CXXScopeSpec &SS,
                                ParsedTemplateTy *Template /*=nullptr*/) {
  // We could use redeclaration lookup here, but we don't need to: the
  // syntactic form of a deduction guide is enough to identify it even
  // if we can't look up the template name at all.
  LookupResult R(*this, DeclarationName(&Name), NameLoc, LookupOrdinaryName);
  if (LookupTemplateName(R, S, SS, /*ObjectType*/ QualType(),
                         /*EnteringContext*/ false))
    return false;

  if (R.empty()) return false;
  if (R.isAmbiguous()) {
    // FIXME: Diagnose an ambiguity if we find at least one template.
    R.suppressDiagnostics();
    return false;
  }

  // We only treat template-names that name type templates as valid deduction
  // guide names.
  TemplateDecl *TD = R.getAsSingle<TemplateDecl>();
  if (!TD || !getAsTypeTemplateDecl(TD))
    return false;

  if (Template) {
    TemplateName Name = Context.getQualifiedTemplateName(
        SS.getScopeRep(), /*TemplateKeyword=*/false, TemplateName(TD));
    *Template = TemplateTy::make(Name);
  }
  return true;
}

bool Sema::DiagnoseUnknownTemplateName(const IdentifierInfo &II,
                                       SourceLocation IILoc,
                                       Scope *S,
                                       const CXXScopeSpec *SS,
                                       TemplateTy &SuggestedTemplate,
                                       TemplateNameKind &SuggestedKind) {
  // We can't recover unless there's a dependent scope specifier preceding the
  // template name.
  // FIXME: Typo correction?
  if (!SS || !SS->isSet() || !isDependentScopeSpecifier(*SS) ||
      computeDeclContext(*SS))
    return false;

  // The code is missing a 'template' keyword prior to the dependent template
  // name.
  NestedNameSpecifier *Qualifier = (NestedNameSpecifier*)SS->getScopeRep();
  Diag(IILoc, diag::err_template_kw_missing)
    << Qualifier << II.getName()
    << FixItHint::CreateInsertion(IILoc, "template ");
  SuggestedTemplate
    = TemplateTy::make(Context.getDependentTemplateName(Qualifier, &II));
  SuggestedKind = TNK_Dependent_template_name;
  return true;
}

bool Sema::LookupTemplateName(LookupResult &Found, Scope *S, CXXScopeSpec &SS,
                              QualType ObjectType, bool EnteringContext,
                              RequiredTemplateKind RequiredTemplate,
                              AssumedTemplateKind *ATK,
                              bool AllowTypoCorrection) {
  if (ATK)
    *ATK = AssumedTemplateKind::None;

  if (SS.isInvalid())
    return true;

  Found.setTemplateNameLookup(true);

  // Determine where to perform name lookup
  DeclContext *LookupCtx = nullptr;
  bool IsDependent = false;
  if (!ObjectType.isNull()) {
    // This nested-name-specifier occurs in a member access expression, e.g.,
    // x->B::f, and we are looking into the type of the object.
    assert(SS.isEmpty() && "ObjectType and scope specifier cannot coexist");
    LookupCtx = computeDeclContext(ObjectType);
    IsDependent = !LookupCtx && ObjectType->isDependentType();
    assert((IsDependent || !ObjectType->isIncompleteType() ||
            !ObjectType->getAs<TagType>() ||
            ObjectType->castAs<TagType>()->isBeingDefined()) &&
           "Caller should have completed object type");

    // Template names cannot appear inside an Objective-C class or object type
    // or a vector type.
    //
    // FIXME: This is wrong. For example:
    //
    //   template<typename T> using Vec = T __attribute__((ext_vector_type(4)));
    //   Vec<int> vi;
    //   vi.Vec<int>::~Vec<int>();
    //
    // ... should be accepted but we will not treat 'Vec' as a template name
    // here. The right thing to do would be to check if the name is a valid
    // vector component name, and look up a template name if not. And similarly
    // for lookups into Objective-C class and object types, where the same
    // problem can arise.
    if (ObjectType->isObjCObjectOrInterfaceType() ||
        ObjectType->isVectorType()) {
      Found.clear();
      return false;
    }
  } else if (SS.isNotEmpty()) {
    // This nested-name-specifier occurs after another nested-name-specifier,
    // so long into the context associated with the prior nested-name-specifier.
    LookupCtx = computeDeclContext(SS, EnteringContext);
    IsDependent = !LookupCtx && isDependentScopeSpecifier(SS);

    // The declaration context must be complete.
    if (LookupCtx && RequireCompleteDeclContext(SS, LookupCtx))
      return true;
  }

  bool ObjectTypeSearchedInScope = false;
  bool AllowFunctionTemplatesInLookup = true;
  if (LookupCtx) {
    // Perform "qualified" name lookup into the declaration context we
    // computed, which is either the type of the base of a member access
    // expression or the declaration context associated with a prior
    // nested-name-specifier.
    LookupQualifiedName(Found, LookupCtx);

    // FIXME: The C++ standard does not clearly specify what happens in the
    // case where the object type is dependent, and implementations vary. In
    // Clang, we treat a name after a . or -> as a template-name if lookup
    // finds a non-dependent member or member of the current instantiation that
    // is a type template, or finds no such members and lookup in the context
    // of the postfix-expression finds a type template. In the latter case, the
    // name is nonetheless dependent, and we may resolve it to a member of an
    // unknown specialization when we come to instantiate the template.
    IsDependent |= Found.wasNotFoundInCurrentInstantiation();
  }

  if (SS.isEmpty() && (ObjectType.isNull() || Found.empty())) {
    // C++ [basic.lookup.classref]p1:
    //   In a class member access expression (5.2.5), if the . or -> token is
    //   immediately followed by an identifier followed by a <, the
    //   identifier must be looked up to determine whether the < is the
    //   beginning of a template argument list (14.2) or a less-than operator.
    //   The identifier is first looked up in the class of the object
    //   expression. If the identifier is not found, it is then looked up in
    //   the context of the entire postfix-expression and shall name a class
    //   template.
    if (S)
      LookupName(Found, S);

    if (!ObjectType.isNull()) {
      //  FIXME: We should filter out all non-type templates here, particularly
      //  variable templates and concepts. But the exclusion of alias templates
      //  and template template parameters is a wording defect.
      AllowFunctionTemplatesInLookup = false;
      ObjectTypeSearchedInScope = true;
    }

    IsDependent |= Found.wasNotFoundInCurrentInstantiation();
  }

  if (Found.isAmbiguous())
    return false;

  if (ATK && SS.isEmpty() && ObjectType.isNull() &&
      !RequiredTemplate.hasTemplateKeyword()) {
    // C++2a [temp.names]p2:
    //   A name is also considered to refer to a template if it is an
    //   unqualified-id followed by a < and name lookup finds either one or more
    //   functions or finds nothing.
    //
    // To keep our behavior consistent, we apply the "finds nothing" part in
    // all language modes, and diagnose the empty lookup in ActOnCallExpr if we
    // successfully form a call to an undeclared template-id.
    bool AllFunctions =
        getLangOpts().CPlusPlus20 && llvm::all_of(Found, [](NamedDecl *ND) {
          return isa<FunctionDecl>(ND->getUnderlyingDecl());
        });
    if (AllFunctions || (Found.empty() && !IsDependent)) {
      // If lookup found any functions, or if this is a name that can only be
      // used for a function, then strongly assume this is a function
      // template-id.
      *ATK = (Found.empty() && Found.getLookupName().isIdentifier())
                 ? AssumedTemplateKind::FoundNothing
                 : AssumedTemplateKind::FoundFunctions;
      Found.clear();
      return false;
    }
  }

  if (Found.empty() && !IsDependent && AllowTypoCorrection) {
    // If we did not find any names, and this is not a disambiguation, attempt
    // to correct any typos.
    DeclarationName Name = Found.getLookupName();
    Found.clear();
    // Simple filter callback that, for keywords, only accepts the C++ *_cast
    DefaultFilterCCC FilterCCC{};
    FilterCCC.WantTypeSpecifiers = false;
    FilterCCC.WantExpressionKeywords = false;
    FilterCCC.WantRemainingKeywords = false;
    FilterCCC.WantCXXNamedCasts = true;
    if (TypoCorrection Corrected =
            CorrectTypo(Found.getLookupNameInfo(), Found.getLookupKind(), S,
                        &SS, FilterCCC, CTK_ErrorRecovery, LookupCtx)) {
      if (auto *ND = Corrected.getFoundDecl())
        Found.addDecl(ND);
      FilterAcceptableTemplateNames(Found);
      if (Found.isAmbiguous()) {
        Found.clear();
      } else if (!Found.empty()) {
        Found.setLookupName(Corrected.getCorrection());
        if (LookupCtx) {
          std::string CorrectedStr(Corrected.getAsString(getLangOpts()));
          bool DroppedSpecifier = Corrected.WillReplaceSpecifier() &&
                                  Name.getAsString() == CorrectedStr;
          diagnoseTypo(Corrected, PDiag(diag::err_no_member_template_suggest)
                                    << Name << LookupCtx << DroppedSpecifier
                                    << SS.getRange());
        } else {
          diagnoseTypo(Corrected, PDiag(diag::err_no_template_suggest) << Name);
        }
      }
    }
  }

  NamedDecl *ExampleLookupResult =
      Found.empty() ? nullptr : Found.getRepresentativeDecl();
  FilterAcceptableTemplateNames(Found, AllowFunctionTemplatesInLookup);
  if (Found.empty()) {
    if (IsDependent) {
      Found.setNotFoundInCurrentInstantiation();
      return false;
    }

    // If a 'template' keyword was used, a lookup that finds only non-template
    // names is an error.
    if (ExampleLookupResult && RequiredTemplate) {
      Diag(Found.getNameLoc(), diag::err_template_kw_refers_to_non_template)
          << Found.getLookupName() << SS.getRange()
          << RequiredTemplate.hasTemplateKeyword()
          << RequiredTemplate.getTemplateKeywordLoc();
      Diag(ExampleLookupResult->getUnderlyingDecl()->getLocation(),
           diag::note_template_kw_refers_to_non_template)
          << Found.getLookupName();
      return true;
    }

    return false;
  }

  if (S && !ObjectType.isNull() && !ObjectTypeSearchedInScope &&
      !getLangOpts().CPlusPlus11) {
    // C++03 [basic.lookup.classref]p1:
    //   [...] If the lookup in the class of the object expression finds a
    //   template, the name is also looked up in the context of the entire
    //   postfix-expression and [...]
    //
    // Note: C++11 does not perform this second lookup.
    LookupResult FoundOuter(*this, Found.getLookupName(), Found.getNameLoc(),
                            LookupOrdinaryName);
    FoundOuter.setTemplateNameLookup(true);
    LookupName(FoundOuter, S);
    // FIXME: We silently accept an ambiguous lookup here, in violation of
    // [basic.lookup]/1.
    FilterAcceptableTemplateNames(FoundOuter, /*AllowFunctionTemplates=*/false);

    NamedDecl *OuterTemplate;
    if (FoundOuter.empty()) {
      //   - if the name is not found, the name found in the class of the
      //     object expression is used, otherwise
    } else if (FoundOuter.isAmbiguous() || !FoundOuter.isSingleResult() ||
               !(OuterTemplate =
                     getAsTemplateNameDecl(FoundOuter.getFoundDecl()))) {
      //   - if the name is found in the context of the entire
      //     postfix-expression and does not name a class template, the name
      //     found in the class of the object expression is used, otherwise
      FoundOuter.clear();
    } else if (!Found.isSuppressingAmbiguousDiagnostics()) {
      //   - if the name found is a class template, it must refer to the same
      //     entity as the one found in the class of the object expression,
      //     otherwise the program is ill-formed.
      if (!Found.isSingleResult() ||
          getAsTemplateNameDecl(Found.getFoundDecl())->getCanonicalDecl() !=
              OuterTemplate->getCanonicalDecl()) {
        Diag(Found.getNameLoc(),
             diag::ext_nested_name_member_ref_lookup_ambiguous)
          << Found.getLookupName()
          << ObjectType;
        Diag(Found.getRepresentativeDecl()->getLocation(),
             diag::note_ambig_member_ref_object_type)
          << ObjectType;
        Diag(FoundOuter.getFoundDecl()->getLocation(),
             diag::note_ambig_member_ref_scope);

        // Recover by taking the template that we found in the object
        // expression's type.
      }
    }
  }

  return false;
}

void Sema::diagnoseExprIntendedAsTemplateName(Scope *S, ExprResult TemplateName,
                                              SourceLocation Less,
                                              SourceLocation Greater) {
  if (TemplateName.isInvalid())
    return;

  DeclarationNameInfo NameInfo;
  CXXScopeSpec SS;
  LookupNameKind LookupKind;

  DeclContext *LookupCtx = nullptr;
  NamedDecl *Found = nullptr;
  bool MissingTemplateKeyword = false;

  // Figure out what name we looked up.
  if (auto *DRE = dyn_cast<DeclRefExpr>(TemplateName.get())) {
    NameInfo = DRE->getNameInfo();
    SS.Adopt(DRE->getQualifierLoc());
    LookupKind = LookupOrdinaryName;
    Found = DRE->getFoundDecl();
  } else if (auto *ME = dyn_cast<MemberExpr>(TemplateName.get())) {
    NameInfo = ME->getMemberNameInfo();
    SS.Adopt(ME->getQualifierLoc());
    LookupKind = LookupMemberName;
    LookupCtx = ME->getBase()->getType()->getAsCXXRecordDecl();
    Found = ME->getMemberDecl();
  } else if (auto *DSDRE =
                 dyn_cast<DependentScopeDeclRefExpr>(TemplateName.get())) {
    NameInfo = DSDRE->getNameInfo();
    SS.Adopt(DSDRE->getQualifierLoc());
    MissingTemplateKeyword = true;
  } else if (auto *DSME =
                 dyn_cast<CXXDependentScopeMemberExpr>(TemplateName.get())) {
    NameInfo = DSME->getMemberNameInfo();
    SS.Adopt(DSME->getQualifierLoc());
    MissingTemplateKeyword = true;
  } else {
    llvm_unreachable("unexpected kind of potential template name");
  }

  // If this is a dependent-scope lookup, diagnose that the 'template' keyword
  // was missing.
  if (MissingTemplateKeyword) {
    Diag(NameInfo.getBeginLoc(), diag::err_template_kw_missing)
        << "" << NameInfo.getName().getAsString() << SourceRange(Less, Greater);
    return;
  }

  // Try to correct the name by looking for templates and C++ named casts.
  struct TemplateCandidateFilter : CorrectionCandidateCallback {
    Sema &S;
    TemplateCandidateFilter(Sema &S) : S(S) {
      WantTypeSpecifiers = false;
      WantExpressionKeywords = false;
      WantRemainingKeywords = false;
      WantCXXNamedCasts = true;
    };
    bool ValidateCandidate(const TypoCorrection &Candidate) override {
      if (auto *ND = Candidate.getCorrectionDecl())
        return S.getAsTemplateNameDecl(ND);
      return Candidate.isKeyword();
    }

    std::unique_ptr<CorrectionCandidateCallback> clone() override {
      return std::make_unique<TemplateCandidateFilter>(*this);
    }
  };

  DeclarationName Name = NameInfo.getName();
  TemplateCandidateFilter CCC(*this);
  if (TypoCorrection Corrected = CorrectTypo(NameInfo, LookupKind, S, &SS, CCC,
                                             CTK_ErrorRecovery, LookupCtx)) {
    auto *ND = Corrected.getFoundDecl();
    if (ND)
      ND = getAsTemplateNameDecl(ND);
    if (ND || Corrected.isKeyword()) {
      if (LookupCtx) {
        std::string CorrectedStr(Corrected.getAsString(getLangOpts()));
        bool DroppedSpecifier = Corrected.WillReplaceSpecifier() &&
                                Name.getAsString() == CorrectedStr;
        diagnoseTypo(Corrected,
                     PDiag(diag::err_non_template_in_member_template_id_suggest)
                         << Name << LookupCtx << DroppedSpecifier
                         << SS.getRange(), false);
      } else {
        diagnoseTypo(Corrected,
                     PDiag(diag::err_non_template_in_template_id_suggest)
                         << Name, false);
      }
      if (Found)
        Diag(Found->getLocation(),
             diag::note_non_template_in_template_id_found);
      return;
    }
  }

  Diag(NameInfo.getLoc(), diag::err_non_template_in_template_id)
    << Name << SourceRange(Less, Greater);
  if (Found)
    Diag(Found->getLocation(), diag::note_non_template_in_template_id_found);
}

ExprResult
Sema::ActOnDependentIdExpression(const CXXScopeSpec &SS,
                                 SourceLocation TemplateKWLoc,
                                 const DeclarationNameInfo &NameInfo,
                                 bool isAddressOfOperand,
                           const TemplateArgumentListInfo *TemplateArgs) {
  if (SS.isEmpty()) {
    // FIXME: This codepath is only used by dependent unqualified names
    // (e.g. a dependent conversion-function-id, or operator= once we support
    // it). It doesn't quite do the right thing, and it will silently fail if
    // getCurrentThisType() returns null.
    QualType ThisType = getCurrentThisType();
    if (ThisType.isNull())
      return ExprError();

    return CXXDependentScopeMemberExpr::Create(
        Context, /*Base=*/nullptr, ThisType,
        /*IsArrow=*/!Context.getLangOpts().HLSL,
        /*OperatorLoc=*/SourceLocation(),
        /*QualifierLoc=*/NestedNameSpecifierLoc(), TemplateKWLoc,
        /*FirstQualifierFoundInScope=*/nullptr, NameInfo, TemplateArgs);
  }
  return BuildDependentDeclRefExpr(SS, TemplateKWLoc, NameInfo, TemplateArgs);
}

ExprResult
Sema::BuildDependentDeclRefExpr(const CXXScopeSpec &SS,
                                SourceLocation TemplateKWLoc,
                                const DeclarationNameInfo &NameInfo,
                                const TemplateArgumentListInfo *TemplateArgs) {
  // DependentScopeDeclRefExpr::Create requires a valid NestedNameSpecifierLoc
  if (!SS.isValid())
    return CreateRecoveryExpr(
        SS.getBeginLoc(),
        TemplateArgs ? TemplateArgs->getRAngleLoc() : NameInfo.getEndLoc(), {});

  return DependentScopeDeclRefExpr::Create(
      Context, SS.getWithLocInContext(Context), TemplateKWLoc, NameInfo,
      TemplateArgs);
}

bool Sema::DiagnoseUninstantiableTemplate(SourceLocation PointOfInstantiation,
                                          NamedDecl *Instantiation,
                                          bool InstantiatedFromMember,
                                          const NamedDecl *Pattern,
                                          const NamedDecl *PatternDef,
                                          TemplateSpecializationKind TSK,
                                          bool Complain /*= true*/) {
  assert(isa<TagDecl>(Instantiation) || isa<FunctionDecl>(Instantiation) ||
         isa<VarDecl>(Instantiation));

  bool IsEntityBeingDefined = false;
  if (const TagDecl *TD = dyn_cast_or_null<TagDecl>(PatternDef))
    IsEntityBeingDefined = TD->isBeingDefined();

  if (PatternDef && !IsEntityBeingDefined) {
    NamedDecl *SuggestedDef = nullptr;
    if (!hasReachableDefinition(const_cast<NamedDecl *>(PatternDef),
                                &SuggestedDef,
                                /*OnlyNeedComplete*/ false)) {
      // If we're allowed to diagnose this and recover, do so.
      bool Recover = Complain && !isSFINAEContext();
      if (Complain)
        diagnoseMissingImport(PointOfInstantiation, SuggestedDef,
                              Sema::MissingImportKind::Definition, Recover);
      return !Recover;
    }
    return false;
  }

  if (!Complain || (PatternDef && PatternDef->isInvalidDecl()))
    return true;

  QualType InstantiationTy;
  if (TagDecl *TD = dyn_cast<TagDecl>(Instantiation))
    InstantiationTy = Context.getTypeDeclType(TD);
  if (PatternDef) {
    Diag(PointOfInstantiation,
         diag::err_template_instantiate_within_definition)
      << /*implicit|explicit*/(TSK != TSK_ImplicitInstantiation)
      << InstantiationTy;
    // Not much point in noting the template declaration here, since
    // we're lexically inside it.
    Instantiation->setInvalidDecl();
  } else if (InstantiatedFromMember) {
    if (isa<FunctionDecl>(Instantiation)) {
      Diag(PointOfInstantiation,
           diag::err_explicit_instantiation_undefined_member)
        << /*member function*/ 1 << Instantiation->getDeclName()
        << Instantiation->getDeclContext();
      Diag(Pattern->getLocation(), diag::note_explicit_instantiation_here);
    } else {
      assert(isa<TagDecl>(Instantiation) && "Must be a TagDecl!");
      Diag(PointOfInstantiation,
           diag::err_implicit_instantiate_member_undefined)
        << InstantiationTy;
      Diag(Pattern->getLocation(), diag::note_member_declared_at);
    }
  } else {
    if (isa<FunctionDecl>(Instantiation)) {
      Diag(PointOfInstantiation,
           diag::err_explicit_instantiation_undefined_func_template)
        << Pattern;
      Diag(Pattern->getLocation(), diag::note_explicit_instantiation_here);
    } else if (isa<TagDecl>(Instantiation)) {
      Diag(PointOfInstantiation, diag::err_template_instantiate_undefined)
        << (TSK != TSK_ImplicitInstantiation)
        << InstantiationTy;
      NoteTemplateLocation(*Pattern);
    } else {
      assert(isa<VarDecl>(Instantiation) && "Must be a VarDecl!");
      if (isa<VarTemplateSpecializationDecl>(Instantiation)) {
        Diag(PointOfInstantiation,
             diag::err_explicit_instantiation_undefined_var_template)
          << Instantiation;
        Instantiation->setInvalidDecl();
      } else
        Diag(PointOfInstantiation,
             diag::err_explicit_instantiation_undefined_member)
          << /*static data member*/ 2 << Instantiation->getDeclName()
          << Instantiation->getDeclContext();
      Diag(Pattern->getLocation(), diag::note_explicit_instantiation_here);
    }
  }

  // In general, Instantiation isn't marked invalid to get more than one
  // error for multiple undefined instantiations. But the code that does
  // explicit declaration -> explicit definition conversion can't handle
  // invalid declarations, so mark as invalid in that case.
  if (TSK == TSK_ExplicitInstantiationDeclaration)
    Instantiation->setInvalidDecl();
  return true;
}

void Sema::DiagnoseTemplateParameterShadow(SourceLocation Loc, Decl *PrevDecl,
                                           bool SupportedForCompatibility) {
  assert(PrevDecl->isTemplateParameter() && "Not a template parameter");

  // C++23 [temp.local]p6:
  //   The name of a template-parameter shall not be bound to any following.
  //   declaration whose locus is contained by the scope to which the
  //   template-parameter belongs.
  //
  // When MSVC compatibility is enabled, the diagnostic is always a warning
  // by default. Otherwise, it an error unless SupportedForCompatibility is
  // true, in which case it is a default-to-error warning.
  unsigned DiagId =
      getLangOpts().MSVCCompat
          ? diag::ext_template_param_shadow
          : (SupportedForCompatibility ? diag::ext_compat_template_param_shadow
                                       : diag::err_template_param_shadow);
  const auto *ND = cast<NamedDecl>(PrevDecl);
  Diag(Loc, DiagId) << ND->getDeclName();
  NoteTemplateParameterLocation(*ND);
}

TemplateDecl *Sema::AdjustDeclIfTemplate(Decl *&D) {
  if (TemplateDecl *Temp = dyn_cast_or_null<TemplateDecl>(D)) {
    D = Temp->getTemplatedDecl();
    return Temp;
  }
  return nullptr;
}

ParsedTemplateArgument ParsedTemplateArgument::getTemplatePackExpansion(
                                             SourceLocation EllipsisLoc) const {
  assert(Kind == Template &&
         "Only template template arguments can be pack expansions here");
  assert(getAsTemplate().get().containsUnexpandedParameterPack() &&
         "Template template argument pack expansion without packs");
  ParsedTemplateArgument Result(*this);
  Result.EllipsisLoc = EllipsisLoc;
  return Result;
}

static TemplateArgumentLoc translateTemplateArgument(Sema &SemaRef,
                                            const ParsedTemplateArgument &Arg) {

  switch (Arg.getKind()) {
  case ParsedTemplateArgument::Type: {
    TypeSourceInfo *DI;
    QualType T = SemaRef.GetTypeFromParser(Arg.getAsType(), &DI);
    if (!DI)
      DI = SemaRef.Context.getTrivialTypeSourceInfo(T, Arg.getLocation());
    return TemplateArgumentLoc(TemplateArgument(T), DI);
  }

  case ParsedTemplateArgument::NonType: {
    Expr *E = static_cast<Expr *>(Arg.getAsExpr());
    return TemplateArgumentLoc(TemplateArgument(E), E);
  }

  case ParsedTemplateArgument::Template: {
    TemplateName Template = Arg.getAsTemplate().get();
    TemplateArgument TArg;
    if (Arg.getEllipsisLoc().isValid())
      TArg = TemplateArgument(Template, std::optional<unsigned int>());
    else
      TArg = Template;
    return TemplateArgumentLoc(
        SemaRef.Context, TArg,
        Arg.getScopeSpec().getWithLocInContext(SemaRef.Context),
        Arg.getLocation(), Arg.getEllipsisLoc());
  }
  }

  llvm_unreachable("Unhandled parsed template argument");
}

void Sema::translateTemplateArguments(const ASTTemplateArgsPtr &TemplateArgsIn,
                                      TemplateArgumentListInfo &TemplateArgs) {
 for (unsigned I = 0, Last = TemplateArgsIn.size(); I != Last; ++I)
   TemplateArgs.addArgument(translateTemplateArgument(*this,
                                                      TemplateArgsIn[I]));
}

static void maybeDiagnoseTemplateParameterShadow(Sema &SemaRef, Scope *S,
                                                 SourceLocation Loc,
                                                 const IdentifierInfo *Name) {
  NamedDecl *PrevDecl =
      SemaRef.LookupSingleName(S, Name, Loc, Sema::LookupOrdinaryName,
                               RedeclarationKind::ForVisibleRedeclaration);
  if (PrevDecl && PrevDecl->isTemplateParameter())
    SemaRef.DiagnoseTemplateParameterShadow(Loc, PrevDecl);
}

ParsedTemplateArgument Sema::ActOnTemplateTypeArgument(TypeResult ParsedType) {
  TypeSourceInfo *TInfo;
  QualType T = GetTypeFromParser(ParsedType.get(), &TInfo);
  if (T.isNull())
    return ParsedTemplateArgument();
  assert(TInfo && "template argument with no location");

  // If we might have formed a deduced template specialization type, convert
  // it to a template template argument.
  if (getLangOpts().CPlusPlus17) {
    TypeLoc TL = TInfo->getTypeLoc();
    SourceLocation EllipsisLoc;
    if (auto PET = TL.getAs<PackExpansionTypeLoc>()) {
      EllipsisLoc = PET.getEllipsisLoc();
      TL = PET.getPatternLoc();
    }

    CXXScopeSpec SS;
    if (auto ET = TL.getAs<ElaboratedTypeLoc>()) {
      SS.Adopt(ET.getQualifierLoc());
      TL = ET.getNamedTypeLoc();
    }

    if (auto DTST = TL.getAs<DeducedTemplateSpecializationTypeLoc>()) {
      TemplateName Name = DTST.getTypePtr()->getTemplateName();
      ParsedTemplateArgument Result(SS, TemplateTy::make(Name),
                                    DTST.getTemplateNameLoc());
      if (EllipsisLoc.isValid())
        Result = Result.getTemplatePackExpansion(EllipsisLoc);
      return Result;
    }
  }

  // This is a normal type template argument. Note, if the type template
  // argument is an injected-class-name for a template, it has a dual nature
  // and can be used as either a type or a template. We handle that in
  // convertTypeTemplateArgumentToTemplate.
  return ParsedTemplateArgument(ParsedTemplateArgument::Type,
                                ParsedType.get().getAsOpaquePtr(),
                                TInfo->getTypeLoc().getBeginLoc());
}

NamedDecl *Sema::ActOnTypeParameter(Scope *S, bool Typename,
                                    SourceLocation EllipsisLoc,
                                    SourceLocation KeyLoc,
                                    IdentifierInfo *ParamName,
                                    SourceLocation ParamNameLoc,
                                    unsigned Depth, unsigned Position,
                                    SourceLocation EqualLoc,
                                    ParsedType DefaultArg,
                                    bool HasTypeConstraint) {
  assert(S->isTemplateParamScope() &&
         "Template type parameter not in template parameter scope!");

  bool IsParameterPack = EllipsisLoc.isValid();
  TemplateTypeParmDecl *Param
    = TemplateTypeParmDecl::Create(Context, Context.getTranslationUnitDecl(),
                                   KeyLoc, ParamNameLoc, Depth, Position,
                                   ParamName, Typename, IsParameterPack,
                                   HasTypeConstraint);
  Param->setAccess(AS_public);

  if (Param->isParameterPack())
    if (auto *LSI = getEnclosingLambda())
      LSI->LocalPacks.push_back(Param);

  if (ParamName) {
    maybeDiagnoseTemplateParameterShadow(*this, S, ParamNameLoc, ParamName);

    // Add the template parameter into the current scope.
    S->AddDecl(Param);
    IdResolver.AddDecl(Param);
  }

  // C++0x [temp.param]p9:
  //   A default template-argument may be specified for any kind of
  //   template-parameter that is not a template parameter pack.
  if (DefaultArg && IsParameterPack) {
    Diag(EqualLoc, diag::err_template_param_pack_default_arg);
    DefaultArg = nullptr;
  }

  // Handle the default argument, if provided.
  if (DefaultArg) {
    TypeSourceInfo *DefaultTInfo;
    GetTypeFromParser(DefaultArg, &DefaultTInfo);

    assert(DefaultTInfo && "expected source information for type");

    // Check for unexpanded parameter packs.
    if (DiagnoseUnexpandedParameterPack(ParamNameLoc, DefaultTInfo,
                                        UPPC_DefaultArgument))
      return Param;

    // Check the template argument itself.
    if (CheckTemplateArgument(DefaultTInfo)) {
      Param->setInvalidDecl();
      return Param;
    }

    Param->setDefaultArgument(
        Context, TemplateArgumentLoc(DefaultTInfo->getType(), DefaultTInfo));
  }

  return Param;
}

/// Convert the parser's template argument list representation into our form.
static TemplateArgumentListInfo
makeTemplateArgumentListInfo(Sema &S, TemplateIdAnnotation &TemplateId) {
  TemplateArgumentListInfo TemplateArgs(TemplateId.LAngleLoc,
                                        TemplateId.RAngleLoc);
  ASTTemplateArgsPtr TemplateArgsPtr(TemplateId.getTemplateArgs(),
                                     TemplateId.NumArgs);
  S.translateTemplateArguments(TemplateArgsPtr, TemplateArgs);
  return TemplateArgs;
}

bool Sema::CheckTypeConstraint(TemplateIdAnnotation *TypeConstr) {

  TemplateName TN = TypeConstr->Template.get();
  ConceptDecl *CD = cast<ConceptDecl>(TN.getAsTemplateDecl());

  // C++2a [temp.param]p4:
  //     [...] The concept designated by a type-constraint shall be a type
  //     concept ([temp.concept]).
  if (!CD->isTypeConcept()) {
    Diag(TypeConstr->TemplateNameLoc,
         diag::err_type_constraint_non_type_concept);
    return true;
  }

  bool WereArgsSpecified = TypeConstr->LAngleLoc.isValid();

  if (!WereArgsSpecified &&
      CD->getTemplateParameters()->getMinRequiredArguments() > 1) {
    Diag(TypeConstr->TemplateNameLoc,
         diag::err_type_constraint_missing_arguments)
        << CD;
    return true;
  }
  return false;
}

bool Sema::ActOnTypeConstraint(const CXXScopeSpec &SS,
                               TemplateIdAnnotation *TypeConstr,
                               TemplateTypeParmDecl *ConstrainedParameter,
                               SourceLocation EllipsisLoc) {
  return BuildTypeConstraint(SS, TypeConstr, ConstrainedParameter, EllipsisLoc,
                             false);
}

bool Sema::BuildTypeConstraint(const CXXScopeSpec &SS,
                               TemplateIdAnnotation *TypeConstr,
                               TemplateTypeParmDecl *ConstrainedParameter,
                               SourceLocation EllipsisLoc,
                               bool AllowUnexpandedPack) {

  if (CheckTypeConstraint(TypeConstr))
    return true;

  TemplateName TN = TypeConstr->Template.get();
  ConceptDecl *CD = cast<ConceptDecl>(TN.getAsTemplateDecl());
  UsingShadowDecl *USD = TN.getAsUsingShadowDecl();

  DeclarationNameInfo ConceptName(DeclarationName(TypeConstr->Name),
                                  TypeConstr->TemplateNameLoc);

  TemplateArgumentListInfo TemplateArgs;
  if (TypeConstr->LAngleLoc.isValid()) {
    TemplateArgs =
        makeTemplateArgumentListInfo(*this, *TypeConstr);

    if (EllipsisLoc.isInvalid() && !AllowUnexpandedPack) {
      for (TemplateArgumentLoc Arg : TemplateArgs.arguments()) {
        if (DiagnoseUnexpandedParameterPack(Arg, UPPC_TypeConstraint))
          return true;
      }
    }
  }
  return AttachTypeConstraint(
      SS.isSet() ? SS.getWithLocInContext(Context) : NestedNameSpecifierLoc(),
      ConceptName, CD, /*FoundDecl=*/USD ? cast<NamedDecl>(USD) : CD,
      TypeConstr->LAngleLoc.isValid() ? &TemplateArgs : nullptr,
      ConstrainedParameter, EllipsisLoc);
}

template <typename ArgumentLocAppender>
static ExprResult formImmediatelyDeclaredConstraint(
    Sema &S, NestedNameSpecifierLoc NS, DeclarationNameInfo NameInfo,
    ConceptDecl *NamedConcept, NamedDecl *FoundDecl, SourceLocation LAngleLoc,
    SourceLocation RAngleLoc, QualType ConstrainedType,
    SourceLocation ParamNameLoc, ArgumentLocAppender Appender,
    SourceLocation EllipsisLoc) {

  TemplateArgumentListInfo ConstraintArgs;
  ConstraintArgs.addArgument(
    S.getTrivialTemplateArgumentLoc(TemplateArgument(ConstrainedType),
                                    /*NTTPType=*/QualType(), ParamNameLoc));

  ConstraintArgs.setRAngleLoc(RAngleLoc);
  ConstraintArgs.setLAngleLoc(LAngleLoc);
  Appender(ConstraintArgs);

  // C++2a [temp.param]p4:
  //     [...] This constraint-expression E is called the immediately-declared
  //     constraint of T. [...]
  CXXScopeSpec SS;
  SS.Adopt(NS);
  ExprResult ImmediatelyDeclaredConstraint = S.CheckConceptTemplateId(
      SS, /*TemplateKWLoc=*/SourceLocation(), NameInfo,
      /*FoundDecl=*/FoundDecl ? FoundDecl : NamedConcept, NamedConcept,
      &ConstraintArgs);
  if (ImmediatelyDeclaredConstraint.isInvalid() || !EllipsisLoc.isValid())
    return ImmediatelyDeclaredConstraint;

  // C++2a [temp.param]p4:
  //     [...] If T is not a pack, then E is E', otherwise E is (E' && ...).
  //
  // We have the following case:
  //
  // template<typename T> concept C1 = true;
  // template<C1... T> struct s1;
  //
  // The constraint: (C1<T> && ...)
  //
  // Note that the type of C1<T> is known to be 'bool', so we don't need to do
  // any unqualified lookups for 'operator&&' here.
  return S.BuildCXXFoldExpr(/*UnqualifiedLookup=*/nullptr,
                            /*LParenLoc=*/SourceLocation(),
                            ImmediatelyDeclaredConstraint.get(), BO_LAnd,
                            EllipsisLoc, /*RHS=*/nullptr,
                            /*RParenLoc=*/SourceLocation(),
                            /*NumExpansions=*/std::nullopt);
}

bool Sema::AttachTypeConstraint(NestedNameSpecifierLoc NS,
                                DeclarationNameInfo NameInfo,
                                ConceptDecl *NamedConcept, NamedDecl *FoundDecl,
                                const TemplateArgumentListInfo *TemplateArgs,
                                TemplateTypeParmDecl *ConstrainedParameter,
                                SourceLocation EllipsisLoc) {
  // C++2a [temp.param]p4:
  //     [...] If Q is of the form C<A1, ..., An>, then let E' be
  //     C<T, A1, ..., An>. Otherwise, let E' be C<T>. [...]
  const ASTTemplateArgumentListInfo *ArgsAsWritten =
    TemplateArgs ? ASTTemplateArgumentListInfo::Create(Context,
                                                       *TemplateArgs) : nullptr;

  QualType ParamAsArgument(ConstrainedParameter->getTypeForDecl(), 0);

  ExprResult ImmediatelyDeclaredConstraint = formImmediatelyDeclaredConstraint(
      *this, NS, NameInfo, NamedConcept, FoundDecl,
      TemplateArgs ? TemplateArgs->getLAngleLoc() : SourceLocation(),
      TemplateArgs ? TemplateArgs->getRAngleLoc() : SourceLocation(),
      ParamAsArgument, ConstrainedParameter->getLocation(),
      [&](TemplateArgumentListInfo &ConstraintArgs) {
        if (TemplateArgs)
          for (const auto &ArgLoc : TemplateArgs->arguments())
            ConstraintArgs.addArgument(ArgLoc);
      },
      EllipsisLoc);
  if (ImmediatelyDeclaredConstraint.isInvalid())
    return true;

  auto *CL = ConceptReference::Create(Context, /*NNS=*/NS,
                                      /*TemplateKWLoc=*/SourceLocation{},
                                      /*ConceptNameInfo=*/NameInfo,
                                      /*FoundDecl=*/FoundDecl,
                                      /*NamedConcept=*/NamedConcept,
                                      /*ArgsWritten=*/ArgsAsWritten);
  ConstrainedParameter->setTypeConstraint(CL,
                                          ImmediatelyDeclaredConstraint.get());
  return false;
}

bool Sema::AttachTypeConstraint(AutoTypeLoc TL,
                                NonTypeTemplateParmDecl *NewConstrainedParm,
                                NonTypeTemplateParmDecl *OrigConstrainedParm,
                                SourceLocation EllipsisLoc) {
  if (NewConstrainedParm->getType() != TL.getType() ||
      TL.getAutoKeyword() != AutoTypeKeyword::Auto) {
    Diag(NewConstrainedParm->getTypeSourceInfo()->getTypeLoc().getBeginLoc(),
         diag::err_unsupported_placeholder_constraint)
        << NewConstrainedParm->getTypeSourceInfo()
               ->getTypeLoc()
               .getSourceRange();
    return true;
  }
  // FIXME: Concepts: This should be the type of the placeholder, but this is
  // unclear in the wording right now.
  DeclRefExpr *Ref =
      BuildDeclRefExpr(OrigConstrainedParm, OrigConstrainedParm->getType(),
                       VK_PRValue, OrigConstrainedParm->getLocation());
  if (!Ref)
    return true;
  ExprResult ImmediatelyDeclaredConstraint = formImmediatelyDeclaredConstraint(
      *this, TL.getNestedNameSpecifierLoc(), TL.getConceptNameInfo(),
      TL.getNamedConcept(), /*FoundDecl=*/TL.getFoundDecl(), TL.getLAngleLoc(),
      TL.getRAngleLoc(), BuildDecltypeType(Ref),
      OrigConstrainedParm->getLocation(),
      [&](TemplateArgumentListInfo &ConstraintArgs) {
        for (unsigned I = 0, C = TL.getNumArgs(); I != C; ++I)
          ConstraintArgs.addArgument(TL.getArgLoc(I));
      },
      EllipsisLoc);
  if (ImmediatelyDeclaredConstraint.isInvalid() ||
      !ImmediatelyDeclaredConstraint.isUsable())
    return true;

  NewConstrainedParm->setPlaceholderTypeConstraint(
      ImmediatelyDeclaredConstraint.get());
  return false;
}

QualType Sema::CheckNonTypeTemplateParameterType(TypeSourceInfo *&TSI,
                                                 SourceLocation Loc) {
  if (TSI->getType()->isUndeducedType()) {
    // C++17 [temp.dep.expr]p3:
    //   An id-expression is type-dependent if it contains
    //    - an identifier associated by name lookup with a non-type
    //      template-parameter declared with a type that contains a
    //      placeholder type (7.1.7.4),
    TSI = SubstAutoTypeSourceInfoDependent(TSI);
  }

  return CheckNonTypeTemplateParameterType(TSI->getType(), Loc);
}

bool Sema::RequireStructuralType(QualType T, SourceLocation Loc) {
  if (T->isDependentType())
    return false;

  if (RequireCompleteType(Loc, T, diag::err_template_nontype_parm_incomplete))
    return true;

  if (T->isStructuralType())
    return false;

  // Structural types are required to be object types or lvalue references.
  if (T->isRValueReferenceType()) {
    Diag(Loc, diag::err_template_nontype_parm_rvalue_ref) << T;
    return true;
  }

  // Don't mention structural types in our diagnostic prior to C++20. Also,
  // there's not much more we can say about non-scalar non-class types --
  // because we can't see functions or arrays here, those can only be language
  // extensions.
  if (!getLangOpts().CPlusPlus20 ||
      (!T->isScalarType() && !T->isRecordType())) {
    Diag(Loc, diag::err_template_nontype_parm_bad_type) << T;
    return true;
  }

  // Structural types are required to be literal types.
  if (RequireLiteralType(Loc, T, diag::err_template_nontype_parm_not_literal))
    return true;

  Diag(Loc, diag::err_template_nontype_parm_not_structural) << T;

  // Drill down into the reason why the class is non-structural.
  while (const CXXRecordDecl *RD = T->getAsCXXRecordDecl()) {
    // All members are required to be public and non-mutable, and can't be of
    // rvalue reference type. Check these conditions first to prefer a "local"
    // reason over a more distant one.
    for (const FieldDecl *FD : RD->fields()) {
      if (FD->getAccess() != AS_public) {
        Diag(FD->getLocation(), diag::note_not_structural_non_public) << T << 0;
        return true;
      }
      if (FD->isMutable()) {
        Diag(FD->getLocation(), diag::note_not_structural_mutable_field) << T;
        return true;
      }
      if (FD->getType()->isRValueReferenceType()) {
        Diag(FD->getLocation(), diag::note_not_structural_rvalue_ref_field)
            << T;
        return true;
      }
    }

    // All bases are required to be public.
    for (const auto &BaseSpec : RD->bases()) {
      if (BaseSpec.getAccessSpecifier() != AS_public) {
        Diag(BaseSpec.getBaseTypeLoc(), diag::note_not_structural_non_public)
            << T << 1;
        return true;
      }
    }

    // All subobjects are required to be of structural types.
    SourceLocation SubLoc;
    QualType SubType;
    int Kind = -1;

    for (const FieldDecl *FD : RD->fields()) {
      QualType T = Context.getBaseElementType(FD->getType());
      if (!T->isStructuralType()) {
        SubLoc = FD->getLocation();
        SubType = T;
        Kind = 0;
        break;
      }
    }

    if (Kind == -1) {
      for (const auto &BaseSpec : RD->bases()) {
        QualType T = BaseSpec.getType();
        if (!T->isStructuralType()) {
          SubLoc = BaseSpec.getBaseTypeLoc();
          SubType = T;
          Kind = 1;
          break;
        }
      }
    }

    assert(Kind != -1 && "couldn't find reason why type is not structural");
    Diag(SubLoc, diag::note_not_structural_subobject)
        << T << Kind << SubType;
    T = SubType;
    RD = T->getAsCXXRecordDecl();
  }

  return true;
}

QualType Sema::CheckNonTypeTemplateParameterType(QualType T,
                                                 SourceLocation Loc) {
  // We don't allow variably-modified types as the type of non-type template
  // parameters.
  if (T->isVariablyModifiedType()) {
    Diag(Loc, diag::err_variably_modified_nontype_template_param)
      << T;
    return QualType();
  }

  // C++ [temp.param]p4:
  //
  // A non-type template-parameter shall have one of the following
  // (optionally cv-qualified) types:
  //
  //       -- integral or enumeration type,
  if (T->isIntegralOrEnumerationType() ||
      //   -- pointer to object or pointer to function,
      T->isPointerType() ||
      //   -- lvalue reference to object or lvalue reference to function,
      T->isLValueReferenceType() ||
      //   -- pointer to member,
      T->isMemberPointerType() ||
      //   -- std::nullptr_t, or
      T->isNullPtrType() ||
      //   -- a type that contains a placeholder type.
      T->isUndeducedType()) {
    // C++ [temp.param]p5: The top-level cv-qualifiers on the template-parameter
    // are ignored when determining its type.
    return T.getUnqualifiedType();
  }

  // C++ [temp.param]p8:
  //
  //   A non-type template-parameter of type "array of T" or
  //   "function returning T" is adjusted to be of type "pointer to
  //   T" or "pointer to function returning T", respectively.
  if (T->isArrayType() || T->isFunctionType())
    return Context.getDecayedType(T);

  // If T is a dependent type, we can't do the check now, so we
  // assume that it is well-formed. Note that stripping off the
  // qualifiers here is not really correct if T turns out to be
  // an array type, but we'll recompute the type everywhere it's
  // used during instantiation, so that should be OK. (Using the
  // qualified type is equally wrong.)
  if (T->isDependentType())
    return T.getUnqualifiedType();

  // C++20 [temp.param]p6:
  //   -- a structural type
  if (RequireStructuralType(T, Loc))
    return QualType();

  if (!getLangOpts().CPlusPlus20) {
    // FIXME: Consider allowing structural types as an extension in C++17. (In
    // earlier language modes, the template argument evaluation rules are too
    // inflexible.)
    Diag(Loc, diag::err_template_nontype_parm_bad_structural_type) << T;
    return QualType();
  }

  Diag(Loc, diag::warn_cxx17_compat_template_nontype_parm_type) << T;
  return T.getUnqualifiedType();
}

NamedDecl *Sema::ActOnNonTypeTemplateParameter(Scope *S, Declarator &D,
                                          unsigned Depth,
                                          unsigned Position,
                                          SourceLocation EqualLoc,
                                          Expr *Default) {
  TypeSourceInfo *TInfo = GetTypeForDeclarator(D);

  // Check that we have valid decl-specifiers specified.
  auto CheckValidDeclSpecifiers = [this, &D] {
    // C++ [temp.param]
    // p1
    //   template-parameter:
    //     ...
    //     parameter-declaration
    // p2
    //   ... A storage class shall not be specified in a template-parameter
    //   declaration.
    // [dcl.typedef]p1:
    //   The typedef specifier [...] shall not be used in the decl-specifier-seq
    //   of a parameter-declaration
    const DeclSpec &DS = D.getDeclSpec();
    auto EmitDiag = [this](SourceLocation Loc) {
      Diag(Loc, diag::err_invalid_decl_specifier_in_nontype_parm)
          << FixItHint::CreateRemoval(Loc);
    };
    if (DS.getStorageClassSpec() != DeclSpec::SCS_unspecified)
      EmitDiag(DS.getStorageClassSpecLoc());

    if (DS.getThreadStorageClassSpec() != TSCS_unspecified)
      EmitDiag(DS.getThreadStorageClassSpecLoc());

    // [dcl.inline]p1:
    //   The inline specifier can be applied only to the declaration or
    //   definition of a variable or function.

    if (DS.isInlineSpecified())
      EmitDiag(DS.getInlineSpecLoc());

    // [dcl.constexpr]p1:
    //   The constexpr specifier shall be applied only to the definition of a
    //   variable or variable template or the declaration of a function or
    //   function template.

    if (DS.hasConstexprSpecifier())
      EmitDiag(DS.getConstexprSpecLoc());

    // [dcl.fct.spec]p1:
    //   Function-specifiers can be used only in function declarations.

    if (DS.isVirtualSpecified())
      EmitDiag(DS.getVirtualSpecLoc());

    if (DS.hasExplicitSpecifier())
      EmitDiag(DS.getExplicitSpecLoc());

    if (DS.isNoreturnSpecified())
      EmitDiag(DS.getNoreturnSpecLoc());
  };

  CheckValidDeclSpecifiers();

  if (const auto *T = TInfo->getType()->getContainedDeducedType())
    if (isa<AutoType>(T))
      Diag(D.getIdentifierLoc(),
           diag::warn_cxx14_compat_template_nontype_parm_auto_type)
          << QualType(TInfo->getType()->getContainedAutoType(), 0);

  assert(S->isTemplateParamScope() &&
         "Non-type template parameter not in template parameter scope!");
  bool Invalid = false;

  QualType T = CheckNonTypeTemplateParameterType(TInfo, D.getIdentifierLoc());
  if (T.isNull()) {
    T = Context.IntTy; // Recover with an 'int' type.
    Invalid = true;
  }

  CheckFunctionOrTemplateParamDeclarator(S, D);

  const IdentifierInfo *ParamName = D.getIdentifier();
  bool IsParameterPack = D.hasEllipsis();
  NonTypeTemplateParmDecl *Param = NonTypeTemplateParmDecl::Create(
      Context, Context.getTranslationUnitDecl(), D.getBeginLoc(),
      D.getIdentifierLoc(), Depth, Position, ParamName, T, IsParameterPack,
      TInfo);
  Param->setAccess(AS_public);

  if (AutoTypeLoc TL = TInfo->getTypeLoc().getContainedAutoTypeLoc())
    if (TL.isConstrained())
      if (AttachTypeConstraint(TL, Param, Param, D.getEllipsisLoc()))
        Invalid = true;

  if (Invalid)
    Param->setInvalidDecl();

  if (Param->isParameterPack())
    if (auto *LSI = getEnclosingLambda())
      LSI->LocalPacks.push_back(Param);

  if (ParamName) {
    maybeDiagnoseTemplateParameterShadow(*this, S, D.getIdentifierLoc(),
                                         ParamName);

    // Add the template parameter into the current scope.
    S->AddDecl(Param);
    IdResolver.AddDecl(Param);
  }

  // C++0x [temp.param]p9:
  //   A default template-argument may be specified for any kind of
  //   template-parameter that is not a template parameter pack.
  if (Default && IsParameterPack) {
    Diag(EqualLoc, diag::err_template_param_pack_default_arg);
    Default = nullptr;
  }

  // Check the well-formedness of the default template argument, if provided.
  if (Default) {
    // Check for unexpanded parameter packs.
    if (DiagnoseUnexpandedParameterPack(Default, UPPC_DefaultArgument))
      return Param;

    Param->setDefaultArgument(
        Context, getTrivialTemplateArgumentLoc(TemplateArgument(Default),
                                               QualType(), SourceLocation()));
  }

  return Param;
}

NamedDecl *Sema::ActOnTemplateTemplateParameter(
    Scope *S, SourceLocation TmpLoc, TemplateParameterList *Params,
    bool Typename, SourceLocation EllipsisLoc, IdentifierInfo *Name,
    SourceLocation NameLoc, unsigned Depth, unsigned Position,
    SourceLocation EqualLoc, ParsedTemplateArgument Default) {
  assert(S->isTemplateParamScope() &&
         "Template template parameter not in template parameter scope!");

  // Construct the parameter object.
  bool IsParameterPack = EllipsisLoc.isValid();
  TemplateTemplateParmDecl *Param = TemplateTemplateParmDecl::Create(
      Context, Context.getTranslationUnitDecl(),
      NameLoc.isInvalid() ? TmpLoc : NameLoc, Depth, Position, IsParameterPack,
      Name, Typename, Params);
  Param->setAccess(AS_public);

  if (Param->isParameterPack())
    if (auto *LSI = getEnclosingLambda())
      LSI->LocalPacks.push_back(Param);

  // If the template template parameter has a name, then link the identifier
  // into the scope and lookup mechanisms.
  if (Name) {
    maybeDiagnoseTemplateParameterShadow(*this, S, NameLoc, Name);

    S->AddDecl(Param);
    IdResolver.AddDecl(Param);
  }

  if (Params->size() == 0) {
    Diag(Param->getLocation(), diag::err_template_template_parm_no_parms)
    << SourceRange(Params->getLAngleLoc(), Params->getRAngleLoc());
    Param->setInvalidDecl();
  }

  // C++0x [temp.param]p9:
  //   A default template-argument may be specified for any kind of
  //   template-parameter that is not a template parameter pack.
  if (IsParameterPack && !Default.isInvalid()) {
    Diag(EqualLoc, diag::err_template_param_pack_default_arg);
    Default = ParsedTemplateArgument();
  }

  if (!Default.isInvalid()) {
    // Check only that we have a template template argument. We don't want to
    // try to check well-formedness now, because our template template parameter
    // might have dependent types in its template parameters, which we wouldn't
    // be able to match now.
    //
    // If none of the template template parameter's template arguments mention
    // other template parameters, we could actually perform more checking here.
    // However, it isn't worth doing.
    TemplateArgumentLoc DefaultArg = translateTemplateArgument(*this, Default);
    if (DefaultArg.getArgument().getAsTemplate().isNull()) {
      Diag(DefaultArg.getLocation(), diag::err_template_arg_not_valid_template)
        << DefaultArg.getSourceRange();
      return Param;
    }

    // Check for unexpanded parameter packs.
    if (DiagnoseUnexpandedParameterPack(DefaultArg.getLocation(),
                                        DefaultArg.getArgument().getAsTemplate(),
                                        UPPC_DefaultArgument))
      return Param;

    Param->setDefaultArgument(Context, DefaultArg);
  }

  return Param;
}

namespace {
class ConstraintRefersToContainingTemplateChecker
    : public TreeTransform<ConstraintRefersToContainingTemplateChecker> {
  bool Result = false;
  const FunctionDecl *Friend = nullptr;
  unsigned TemplateDepth = 0;

  // Check a record-decl that we've seen to see if it is a lexical parent of the
  // Friend, likely because it was referred to without its template arguments.
  void CheckIfContainingRecord(const CXXRecordDecl *CheckingRD) {
    CheckingRD = CheckingRD->getMostRecentDecl();
    if (!CheckingRD->isTemplated())
      return;

    for (const DeclContext *DC = Friend->getLexicalDeclContext();
         DC && !DC->isFileContext(); DC = DC->getParent())
      if (const auto *RD = dyn_cast<CXXRecordDecl>(DC))
        if (CheckingRD == RD->getMostRecentDecl())
          Result = true;
  }

  void CheckNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D) {
    assert(D->getDepth() <= TemplateDepth &&
           "Nothing should reference a value below the actual template depth, "
           "depth is likely wrong");
    if (D->getDepth() != TemplateDepth)
      Result = true;

    // Necessary because the type of the NTTP might be what refers to the parent
    // constriant.
    TransformType(D->getType());
  }

public:
  using inherited = TreeTransform<ConstraintRefersToContainingTemplateChecker>;

  ConstraintRefersToContainingTemplateChecker(Sema &SemaRef,
                                              const FunctionDecl *Friend,
                                              unsigned TemplateDepth)
      : inherited(SemaRef), Friend(Friend), TemplateDepth(TemplateDepth) {}
  bool getResult() const { return Result; }

  // This should be the only template parm type that we have to deal with.
  // SubstTempalteTypeParmPack, SubstNonTypeTemplateParmPack, and
  // FunctionParmPackExpr are all partially substituted, which cannot happen
  // with concepts at this point in translation.
  using inherited::TransformTemplateTypeParmType;
  QualType TransformTemplateTypeParmType(TypeLocBuilder &TLB,
                                         TemplateTypeParmTypeLoc TL, bool) {
    assert(TL.getDecl()->getDepth() <= TemplateDepth &&
           "Nothing should reference a value below the actual template depth, "
           "depth is likely wrong");
    if (TL.getDecl()->getDepth() != TemplateDepth)
      Result = true;
    return inherited::TransformTemplateTypeParmType(
        TLB, TL,
        /*SuppressObjCLifetime=*/false);
  }

  Decl *TransformDecl(SourceLocation Loc, Decl *D) {
    if (!D)
      return D;
    // FIXME : This is possibly an incomplete list, but it is unclear what other
    // Decl kinds could be used to refer to the template parameters.  This is a
    // best guess so far based on examples currently available, but the
    // unreachable should catch future instances/cases.
    if (auto *TD = dyn_cast<TypedefNameDecl>(D))
      TransformType(TD->getUnderlyingType());
    else if (auto *NTTPD = dyn_cast<NonTypeTemplateParmDecl>(D))
      CheckNonTypeTemplateParmDecl(NTTPD);
    else if (auto *VD = dyn_cast<ValueDecl>(D))
      TransformType(VD->getType());
    else if (auto *TD = dyn_cast<TemplateDecl>(D))
      TransformTemplateParameterList(TD->getTemplateParameters());
    else if (auto *RD = dyn_cast<CXXRecordDecl>(D))
      CheckIfContainingRecord(RD);
    else if (isa<NamedDecl>(D)) {
      // No direct types to visit here I believe.
    } else
      llvm_unreachable("Don't know how to handle this declaration type yet");
    return D;
  }
};
} // namespace

bool Sema::ConstraintExpressionDependsOnEnclosingTemplate(
    const FunctionDecl *Friend, unsigned TemplateDepth,
    const Expr *Constraint) {
  assert(Friend->getFriendObjectKind() && "Only works on a friend");
  ConstraintRefersToContainingTemplateChecker Checker(*this, Friend,
                                                      TemplateDepth);
  Checker.TransformExpr(const_cast<Expr *>(Constraint));
  return Checker.getResult();
}

TemplateParameterList *
Sema::ActOnTemplateParameterList(unsigned Depth,
                                 SourceLocation ExportLoc,
                                 SourceLocation TemplateLoc,
                                 SourceLocation LAngleLoc,
                                 ArrayRef<NamedDecl *> Params,
                                 SourceLocation RAngleLoc,
                                 Expr *RequiresClause) {
  if (ExportLoc.isValid())
    Diag(ExportLoc, diag::warn_template_export_unsupported);

  for (NamedDecl *P : Params)
    warnOnReservedIdentifier(P);

  return TemplateParameterList::Create(
      Context, TemplateLoc, LAngleLoc,
      llvm::ArrayRef(Params.data(), Params.size()), RAngleLoc, RequiresClause);
}

static void SetNestedNameSpecifier(Sema &S, TagDecl *T,
                                   const CXXScopeSpec &SS) {
  if (SS.isSet())
    T->setQualifierInfo(SS.getWithLocInContext(S.Context));
}

// Returns the template parameter list with all default template argument
// information.
TemplateParameterList *Sema::GetTemplateParameterList(TemplateDecl *TD) {
  // Make sure we get the template parameter list from the most
  // recent declaration, since that is the only one that is guaranteed to
  // have all the default template argument information.
  Decl *D = TD->getMostRecentDecl();
  // C++11 N3337 [temp.param]p12:
  // A default template argument shall not be specified in a friend class
  // template declaration.
  //
  // Skip past friend *declarations* because they are not supposed to contain
  // default template arguments. Moreover, these declarations may introduce
  // template parameters living in different template depths than the
  // corresponding template parameters in TD, causing unmatched constraint
  // substitution.
  //
  // FIXME: Diagnose such cases within a class template:
  //  template <class T>
  //  struct S {
  //    template <class = void> friend struct C;
  //  };
  //  template struct S<int>;
  while (D->getFriendObjectKind() != Decl::FriendObjectKind::FOK_None &&
         D->getPreviousDecl())
    D = D->getPreviousDecl();
  return cast<TemplateDecl>(D)->getTemplateParameters();
}

DeclResult Sema::CheckClassTemplate(
    Scope *S, unsigned TagSpec, TagUseKind TUK, SourceLocation KWLoc,
    CXXScopeSpec &SS, IdentifierInfo *Name, SourceLocation NameLoc,
    const ParsedAttributesView &Attr, TemplateParameterList *TemplateParams,
    AccessSpecifier AS, SourceLocation ModulePrivateLoc,
    SourceLocation FriendLoc, unsigned NumOuterTemplateParamLists,
    TemplateParameterList **OuterTemplateParamLists, SkipBodyInfo *SkipBody) {
  assert(TemplateParams && TemplateParams->size() > 0 &&
         "No template parameters");
  assert(TUK != TagUseKind::Reference &&
         "Can only declare or define class templates");
  bool Invalid = false;

  // Check that we can declare a template here.
  if (CheckTemplateDeclScope(S, TemplateParams))
    return true;

  TagTypeKind Kind = TypeWithKeyword::getTagTypeKindForTypeSpec(TagSpec);
  assert(Kind != TagTypeKind::Enum &&
         "can't build template of enumerated type");

  // There is no such thing as an unnamed class template.
  if (!Name) {
    Diag(KWLoc, diag::err_template_unnamed_class);
    return true;
  }

  // Find any previous declaration with this name. For a friend with no
  // scope explicitly specified, we only look for tag declarations (per
  // C++11 [basic.lookup.elab]p2).
  DeclContext *SemanticContext;
  LookupResult Previous(*this, Name, NameLoc,
                        (SS.isEmpty() && TUK == TagUseKind::Friend)
                            ? LookupTagName
                            : LookupOrdinaryName,
                        forRedeclarationInCurContext());
  if (SS.isNotEmpty() && !SS.isInvalid()) {
    SemanticContext = computeDeclContext(SS, true);
    if (!SemanticContext) {
      // FIXME: Horrible, horrible hack! We can't currently represent this
      // in the AST, and historically we have just ignored such friend
      // class templates, so don't complain here.
      Diag(NameLoc, TUK == TagUseKind::Friend
                        ? diag::warn_template_qualified_friend_ignored
                        : diag::err_template_qualified_declarator_no_match)
          << SS.getScopeRep() << SS.getRange();
      return TUK != TagUseKind::Friend;
    }

    if (RequireCompleteDeclContext(SS, SemanticContext))
      return true;

    // If we're adding a template to a dependent context, we may need to
    // rebuilding some of the types used within the template parameter list,
    // now that we know what the current instantiation is.
    if (SemanticContext->isDependentContext()) {
      ContextRAII SavedContext(*this, SemanticContext);
      if (RebuildTemplateParamsInCurrentInstantiation(TemplateParams))
        Invalid = true;
    }

    if (TUK != TagUseKind::Friend && TUK != TagUseKind::Reference)
      diagnoseQualifiedDeclaration(SS, SemanticContext, Name, NameLoc,
                                   /*TemplateId-*/ nullptr,
                                   /*IsMemberSpecialization*/ false);

    LookupQualifiedName(Previous, SemanticContext);
  } else {
    SemanticContext = CurContext;

    // C++14 [class.mem]p14:
    //   If T is the name of a class, then each of the following shall have a
    //   name different from T:
    //    -- every member template of class T
    if (TUK != TagUseKind::Friend &&
        DiagnoseClassNameShadow(SemanticContext,
                                DeclarationNameInfo(Name, NameLoc)))
      return true;

    LookupName(Previous, S);
  }

  if (Previous.isAmbiguous())
    return true;

  NamedDecl *PrevDecl = nullptr;
  if (Previous.begin() != Previous.end())
    PrevDecl = (*Previous.begin())->getUnderlyingDecl();

  if (PrevDecl && PrevDecl->isTemplateParameter()) {
    // Maybe we will complain about the shadowed template parameter.
    DiagnoseTemplateParameterShadow(NameLoc, PrevDecl);
    // Just pretend that we didn't see the previous declaration.
    PrevDecl = nullptr;
  }

  // If there is a previous declaration with the same name, check
  // whether this is a valid redeclaration.
  ClassTemplateDecl *PrevClassTemplate =
      dyn_cast_or_null<ClassTemplateDecl>(PrevDecl);

  // We may have found the injected-class-name of a class template,
  // class template partial specialization, or class template specialization.
  // In these cases, grab the template that is being defined or specialized.
  if (!PrevClassTemplate && isa_and_nonnull<CXXRecordDecl>(PrevDecl) &&
      cast<CXXRecordDecl>(PrevDecl)->isInjectedClassName()) {
    PrevDecl = cast<CXXRecordDecl>(PrevDecl->getDeclContext());
    PrevClassTemplate
      = cast<CXXRecordDecl>(PrevDecl)->getDescribedClassTemplate();
    if (!PrevClassTemplate && isa<ClassTemplateSpecializationDecl>(PrevDecl)) {
      PrevClassTemplate
        = cast<ClassTemplateSpecializationDecl>(PrevDecl)
            ->getSpecializedTemplate();
    }
  }

  if (TUK == TagUseKind::Friend) {
    // C++ [namespace.memdef]p3:
    //   [...] When looking for a prior declaration of a class or a function
    //   declared as a friend, and when the name of the friend class or
    //   function is neither a qualified name nor a template-id, scopes outside
    //   the innermost enclosing namespace scope are not considered.
    if (!SS.isSet()) {
      DeclContext *OutermostContext = CurContext;
      while (!OutermostContext->isFileContext())
        OutermostContext = OutermostContext->getLookupParent();

      if (PrevDecl &&
          (OutermostContext->Equals(PrevDecl->getDeclContext()) ||
           OutermostContext->Encloses(PrevDecl->getDeclContext()))) {
        SemanticContext = PrevDecl->getDeclContext();
      } else {
        // Declarations in outer scopes don't matter. However, the outermost
        // context we computed is the semantic context for our new
        // declaration.
        PrevDecl = PrevClassTemplate = nullptr;
        SemanticContext = OutermostContext;

        // Check that the chosen semantic context doesn't already contain a
        // declaration of this name as a non-tag type.
        Previous.clear(LookupOrdinaryName);
        DeclContext *LookupContext = SemanticContext;
        while (LookupContext->isTransparentContext())
          LookupContext = LookupContext->getLookupParent();
        LookupQualifiedName(Previous, LookupContext);

        if (Previous.isAmbiguous())
          return true;

        if (Previous.begin() != Previous.end())
          PrevDecl = (*Previous.begin())->getUnderlyingDecl();
      }
    }
  } else if (PrevDecl && !isDeclInScope(Previous.getRepresentativeDecl(),
                                        SemanticContext, S, SS.isValid()))
    PrevDecl = PrevClassTemplate = nullptr;

  if (auto *Shadow = dyn_cast_or_null<UsingShadowDecl>(
          PrevDecl ? Previous.getRepresentativeDecl() : nullptr)) {
    if (SS.isEmpty() &&
        !(PrevClassTemplate &&
          PrevClassTemplate->getDeclContext()->getRedeclContext()->Equals(
              SemanticContext->getRedeclContext()))) {
      Diag(KWLoc, diag::err_using_decl_conflict_reverse);
      Diag(Shadow->getTargetDecl()->getLocation(),
           diag::note_using_decl_target);
      Diag(Shadow->getIntroducer()->getLocation(), diag::note_using_decl) << 0;
      // Recover by ignoring the old declaration.
      PrevDecl = PrevClassTemplate = nullptr;
    }
  }

  if (PrevClassTemplate) {
    // Ensure that the template parameter lists are compatible. Skip this check
    // for a friend in a dependent context: the template parameter list itself
    // could be dependent.
    if (!(TUK == TagUseKind::Friend && CurContext->isDependentContext()) &&
        !TemplateParameterListsAreEqual(
            TemplateCompareNewDeclInfo(SemanticContext ? SemanticContext
                                                       : CurContext,
                                       CurContext, KWLoc),
            TemplateParams, PrevClassTemplate,
            PrevClassTemplate->getTemplateParameters(), /*Complain=*/true,
            TPL_TemplateMatch))
      return true;

    // C++ [temp.class]p4:
    //   In a redeclaration, partial specialization, explicit
    //   specialization or explicit instantiation of a class template,
    //   the class-key shall agree in kind with the original class
    //   template declaration (7.1.5.3).
    RecordDecl *PrevRecordDecl = PrevClassTemplate->getTemplatedDecl();
    if (!isAcceptableTagRedeclaration(
            PrevRecordDecl, Kind, TUK == TagUseKind::Definition, KWLoc, Name)) {
      Diag(KWLoc, diag::err_use_with_wrong_tag)
        << Name
        << FixItHint::CreateReplacement(KWLoc, PrevRecordDecl->getKindName());
      Diag(PrevRecordDecl->getLocation(), diag::note_previous_use);
      Kind = PrevRecordDecl->getTagKind();
    }

    // Check for redefinition of this class template.
    if (TUK == TagUseKind::Definition) {
      if (TagDecl *Def = PrevRecordDecl->getDefinition()) {
        // If we have a prior definition that is not visible, treat this as
        // simply making that previous definition visible.
        NamedDecl *Hidden = nullptr;
        if (SkipBody && !hasVisibleDefinition(Def, &Hidden)) {
          SkipBody->ShouldSkip = true;
          SkipBody->Previous = Def;
          auto *Tmpl = cast<CXXRecordDecl>(Hidden)->getDescribedClassTemplate();
          assert(Tmpl && "original definition of a class template is not a "
                         "class template?");
          makeMergedDefinitionVisible(Hidden);
          makeMergedDefinitionVisible(Tmpl);
        } else {
          Diag(NameLoc, diag::err_redefinition) << Name;
          Diag(Def->getLocation(), diag::note_previous_definition);
          // FIXME: Would it make sense to try to "forget" the previous
          // definition, as part of error recovery?
          return true;
        }
      }
    }
  } else if (PrevDecl) {
    // C++ [temp]p5:
    //   A class template shall not have the same name as any other
    //   template, class, function, object, enumeration, enumerator,
    //   namespace, or type in the same scope (3.3), except as specified
    //   in (14.5.4).
    Diag(NameLoc, diag::err_redefinition_different_kind) << Name;
    Diag(PrevDecl->getLocation(), diag::note_previous_definition);
    return true;
  }

  // Check the template parameter list of this declaration, possibly
  // merging in the template parameter list from the previous class
  // template declaration. Skip this check for a friend in a dependent
  // context, because the template parameter list might be dependent.
  if (!(TUK == TagUseKind::Friend && CurContext->isDependentContext()) &&
      CheckTemplateParameterList(
          TemplateParams,
          PrevClassTemplate ? GetTemplateParameterList(PrevClassTemplate)
                            : nullptr,
          (SS.isSet() && SemanticContext && SemanticContext->isRecord() &&
           SemanticContext->isDependentContext())
              ? TPC_ClassTemplateMember
          : TUK == TagUseKind::Friend ? TPC_FriendClassTemplate
                                      : TPC_ClassTemplate,
          SkipBody))
    Invalid = true;

  if (SS.isSet()) {
    // If the name of the template was qualified, we must be defining the
    // template out-of-line.
    if (!SS.isInvalid() && !Invalid && !PrevClassTemplate) {
      Diag(NameLoc, TUK == TagUseKind::Friend
                        ? diag::err_friend_decl_does_not_match
                        : diag::err_member_decl_does_not_match)
          << Name << SemanticContext << /*IsDefinition*/ true << SS.getRange();
      Invalid = true;
    }
  }

  // If this is a templated friend in a dependent context we should not put it
  // on the redecl chain. In some cases, the templated friend can be the most
  // recent declaration tricking the template instantiator to make substitutions
  // there.
  // FIXME: Figure out how to combine with shouldLinkDependentDeclWithPrevious
  bool ShouldAddRedecl =
      !(TUK == TagUseKind::Friend && CurContext->isDependentContext());

  CXXRecordDecl *NewClass =
    CXXRecordDecl::Create(Context, Kind, SemanticContext, KWLoc, NameLoc, Name,
                          PrevClassTemplate && ShouldAddRedecl ?
                            PrevClassTemplate->getTemplatedDecl() : nullptr,
                          /*DelayTypeCreation=*/true);
  SetNestedNameSpecifier(*this, NewClass, SS);
  if (NumOuterTemplateParamLists > 0)
    NewClass->setTemplateParameterListsInfo(
        Context,
        llvm::ArrayRef(OuterTemplateParamLists, NumOuterTemplateParamLists));

  // Add alignment attributes if necessary; these attributes are checked when
  // the ASTContext lays out the structure.
  if (TUK == TagUseKind::Definition && (!SkipBody || !SkipBody->ShouldSkip)) {
    AddAlignmentAttributesForRecord(NewClass);
    AddMsStructLayoutForRecord(NewClass);
  }

  ClassTemplateDecl *NewTemplate
    = ClassTemplateDecl::Create(Context, SemanticContext, NameLoc,
                                DeclarationName(Name), TemplateParams,
                                NewClass);

  if (ShouldAddRedecl)
    NewTemplate->setPreviousDecl(PrevClassTemplate);

  NewClass->setDescribedClassTemplate(NewTemplate);

  if (ModulePrivateLoc.isValid())
    NewTemplate->setModulePrivate();

  // Build the type for the class template declaration now.
  QualType T = NewTemplate->getInjectedClassNameSpecialization();
  T = Context.getInjectedClassNameType(NewClass, T);
  assert(T->isDependentType() && "Class template type is not dependent?");
  (void)T;

  // If we are providing an explicit specialization of a member that is a
  // class template, make a note of that.
  if (PrevClassTemplate &&
      PrevClassTemplate->getInstantiatedFromMemberTemplate())
    PrevClassTemplate->setMemberSpecialization();

  // Set the access specifier.
  if (!Invalid && TUK != TagUseKind::Friend &&
      NewTemplate->getDeclContext()->isRecord())
    SetMemberAccessSpecifier(NewTemplate, PrevClassTemplate, AS);

  // Set the lexical context of these templates
  NewClass->setLexicalDeclContext(CurContext);
  NewTemplate->setLexicalDeclContext(CurContext);

  if (TUK == TagUseKind::Definition && (!SkipBody || !SkipBody->ShouldSkip))
    NewClass->startDefinition();

  ProcessDeclAttributeList(S, NewClass, Attr);
  ProcessAPINotes(NewClass);

  if (PrevClassTemplate)
    mergeDeclAttributes(NewClass, PrevClassTemplate->getTemplatedDecl());

  AddPushedVisibilityAttribute(NewClass);
  inferGslOwnerPointerAttribute(NewClass);
  inferNullableClassAttribute(NewClass);

  if (TUK != TagUseKind::Friend) {
    // Per C++ [basic.scope.temp]p2, skip the template parameter scopes.
    Scope *Outer = S;
    while ((Outer->getFlags() & Scope::TemplateParamScope) != 0)
      Outer = Outer->getParent();
    PushOnScopeChains(NewTemplate, Outer);
  } else {
    if (PrevClassTemplate && PrevClassTemplate->getAccess() != AS_none) {
      NewTemplate->setAccess(PrevClassTemplate->getAccess());
      NewClass->setAccess(PrevClassTemplate->getAccess());
    }

    NewTemplate->setObjectOfFriendDecl();

    // Friend templates are visible in fairly strange ways.
    if (!CurContext->isDependentContext()) {
      DeclContext *DC = SemanticContext->getRedeclContext();
      DC->makeDeclVisibleInContext(NewTemplate);
      if (Scope *EnclosingScope = getScopeForDeclContext(S, DC))
        PushOnScopeChains(NewTemplate, EnclosingScope,
                          /* AddToContext = */ false);
    }

    FriendDecl *Friend = FriendDecl::Create(
        Context, CurContext, NewClass->getLocation(), NewTemplate, FriendLoc);
    Friend->setAccess(AS_public);
    CurContext->addDecl(Friend);
  }

  if (PrevClassTemplate)
    CheckRedeclarationInModule(NewTemplate, PrevClassTemplate);

  if (Invalid) {
    NewTemplate->setInvalidDecl();
    NewClass->setInvalidDecl();
  }

  ActOnDocumentableDecl(NewTemplate);

  if (SkipBody && SkipBody->ShouldSkip)
    return SkipBody->Previous;

  return NewTemplate;
}

/// Diagnose the presence of a default template argument on a
/// template parameter, which is ill-formed in certain contexts.
///
/// \returns true if the default template argument should be dropped.
static bool DiagnoseDefaultTemplateArgument(Sema &S,
                                            Sema::TemplateParamListContext TPC,
                                            SourceLocation ParamLoc,
                                            SourceRange DefArgRange) {
  switch (TPC) {
  case Sema::TPC_ClassTemplate:
  case Sema::TPC_VarTemplate:
  case Sema::TPC_TypeAliasTemplate:
    return false;

  case Sema::TPC_FunctionTemplate:
  case Sema::TPC_FriendFunctionTemplateDefinition:
    // C++ [temp.param]p9:
    //   A default template-argument shall not be specified in a
    //   function template declaration or a function template
    //   definition [...]
    //   If a friend function template declaration specifies a default
    //   template-argument, that declaration shall be a definition and shall be
    //   the only declaration of the function template in the translation unit.
    // (C++98/03 doesn't have this wording; see DR226).
    S.Diag(ParamLoc, S.getLangOpts().CPlusPlus11 ?
         diag::warn_cxx98_compat_template_parameter_default_in_function_template
           : diag::ext_template_parameter_default_in_function_template)
      << DefArgRange;
    return false;

  case Sema::TPC_ClassTemplateMember:
    // C++0x [temp.param]p9:
    //   A default template-argument shall not be specified in the
    //   template-parameter-lists of the definition of a member of a
    //   class template that appears outside of the member's class.
    S.Diag(ParamLoc, diag::err_template_parameter_default_template_member)
      << DefArgRange;
    return true;

  case Sema::TPC_FriendClassTemplate:
  case Sema::TPC_FriendFunctionTemplate:
    // C++ [temp.param]p9:
    //   A default template-argument shall not be specified in a
    //   friend template declaration.
    S.Diag(ParamLoc, diag::err_template_parameter_default_friend_template)
      << DefArgRange;
    return true;

    // FIXME: C++0x [temp.param]p9 allows default template-arguments
    // for friend function templates if there is only a single
    // declaration (and it is a definition). Strange!
  }

  llvm_unreachable("Invalid TemplateParamListContext!");
}

/// Check for unexpanded parameter packs within the template parameters
/// of a template template parameter, recursively.
static bool DiagnoseUnexpandedParameterPacks(Sema &S,
                                             TemplateTemplateParmDecl *TTP) {
  // A template template parameter which is a parameter pack is also a pack
  // expansion.
  if (TTP->isParameterPack())
    return false;

  TemplateParameterList *Params = TTP->getTemplateParameters();
  for (unsigned I = 0, N = Params->size(); I != N; ++I) {
    NamedDecl *P = Params->getParam(I);
    if (TemplateTypeParmDecl *TTP = dyn_cast<TemplateTypeParmDecl>(P)) {
      if (!TTP->isParameterPack())
        if (const TypeConstraint *TC = TTP->getTypeConstraint())
          if (TC->hasExplicitTemplateArgs())
            for (auto &ArgLoc : TC->getTemplateArgsAsWritten()->arguments())
              if (S.DiagnoseUnexpandedParameterPack(ArgLoc,
                                                    Sema::UPPC_TypeConstraint))
                return true;
      continue;
    }

    if (NonTypeTemplateParmDecl *NTTP = dyn_cast<NonTypeTemplateParmDecl>(P)) {
      if (!NTTP->isParameterPack() &&
          S.DiagnoseUnexpandedParameterPack(NTTP->getLocation(),
                                            NTTP->getTypeSourceInfo(),
                                      Sema::UPPC_NonTypeTemplateParameterType))
        return true;

      continue;
    }

    if (TemplateTemplateParmDecl *InnerTTP
                                        = dyn_cast<TemplateTemplateParmDecl>(P))
      if (DiagnoseUnexpandedParameterPacks(S, InnerTTP))
        return true;
  }

  return false;
}

bool Sema::CheckTemplateParameterList(TemplateParameterList *NewParams,
                                      TemplateParameterList *OldParams,
                                      TemplateParamListContext TPC,
                                      SkipBodyInfo *SkipBody) {
  bool Invalid = false;

  // C++ [temp.param]p10:
  //   The set of default template-arguments available for use with a
  //   template declaration or definition is obtained by merging the
  //   default arguments from the definition (if in scope) and all
  //   declarations in scope in the same way default function
  //   arguments are (8.3.6).
  bool SawDefaultArgument = false;
  SourceLocation PreviousDefaultArgLoc;

  // Dummy initialization to avoid warnings.
  TemplateParameterList::iterator OldParam = NewParams->end();
  if (OldParams)
    OldParam = OldParams->begin();

  bool RemoveDefaultArguments = false;
  for (TemplateParameterList::iterator NewParam = NewParams->begin(),
                                    NewParamEnd = NewParams->end();
       NewParam != NewParamEnd; ++NewParam) {
    // Whether we've seen a duplicate default argument in the same translation
    // unit.
    bool RedundantDefaultArg = false;
    // Whether we've found inconsis inconsitent default arguments in different
    // translation unit.
    bool InconsistentDefaultArg = false;
    // The name of the module which contains the inconsistent default argument.
    std::string PrevModuleName;

    SourceLocation OldDefaultLoc;
    SourceLocation NewDefaultLoc;

    // Variable used to diagnose missing default arguments
    bool MissingDefaultArg = false;

    // Variable used to diagnose non-final parameter packs
    bool SawParameterPack = false;

    if (TemplateTypeParmDecl *NewTypeParm
          = dyn_cast<TemplateTypeParmDecl>(*NewParam)) {
      // Check the presence of a default argument here.
      if (NewTypeParm->hasDefaultArgument() &&
          DiagnoseDefaultTemplateArgument(
              *this, TPC, NewTypeParm->getLocation(),
              NewTypeParm->getDefaultArgument().getSourceRange()))
        NewTypeParm->removeDefaultArgument();

      // Merge default arguments for template type parameters.
      TemplateTypeParmDecl *OldTypeParm
          = OldParams? cast<TemplateTypeParmDecl>(*OldParam) : nullptr;
      if (NewTypeParm->isParameterPack()) {
        assert(!NewTypeParm->hasDefaultArgument() &&
               "Parameter packs can't have a default argument!");
        SawParameterPack = true;
      } else if (OldTypeParm && hasVisibleDefaultArgument(OldTypeParm) &&
                 NewTypeParm->hasDefaultArgument() &&
                 (!SkipBody || !SkipBody->ShouldSkip)) {
        OldDefaultLoc = OldTypeParm->getDefaultArgumentLoc();
        NewDefaultLoc = NewTypeParm->getDefaultArgumentLoc();
        SawDefaultArgument = true;

        if (!OldTypeParm->getOwningModule())
          RedundantDefaultArg = true;
        else if (!getASTContext().isSameDefaultTemplateArgument(OldTypeParm,
                                                                NewTypeParm)) {
          InconsistentDefaultArg = true;
          PrevModuleName =
              OldTypeParm->getImportedOwningModule()->getFullModuleName();
        }
        PreviousDefaultArgLoc = NewDefaultLoc;
      } else if (OldTypeParm && OldTypeParm->hasDefaultArgument()) {
        // Merge the default argument from the old declaration to the
        // new declaration.
        NewTypeParm->setInheritedDefaultArgument(Context, OldTypeParm);
        PreviousDefaultArgLoc = OldTypeParm->getDefaultArgumentLoc();
      } else if (NewTypeParm->hasDefaultArgument()) {
        SawDefaultArgument = true;
        PreviousDefaultArgLoc = NewTypeParm->getDefaultArgumentLoc();
      } else if (SawDefaultArgument)
        MissingDefaultArg = true;
    } else if (NonTypeTemplateParmDecl *NewNonTypeParm
               = dyn_cast<NonTypeTemplateParmDecl>(*NewParam)) {
      // Check for unexpanded parameter packs.
      if (!NewNonTypeParm->isParameterPack() &&
          DiagnoseUnexpandedParameterPack(NewNonTypeParm->getLocation(),
                                          NewNonTypeParm->getTypeSourceInfo(),
                                          UPPC_NonTypeTemplateParameterType)) {
        Invalid = true;
        continue;
      }

      // Check the presence of a default argument here.
      if (NewNonTypeParm->hasDefaultArgument() &&
          DiagnoseDefaultTemplateArgument(
              *this, TPC, NewNonTypeParm->getLocation(),
              NewNonTypeParm->getDefaultArgument().getSourceRange())) {
        NewNonTypeParm->removeDefaultArgument();
      }

      // Merge default arguments for non-type template parameters
      NonTypeTemplateParmDecl *OldNonTypeParm
        = OldParams? cast<NonTypeTemplateParmDecl>(*OldParam) : nullptr;
      if (NewNonTypeParm->isParameterPack()) {
        assert(!NewNonTypeParm->hasDefaultArgument() &&
               "Parameter packs can't have a default argument!");
        if (!NewNonTypeParm->isPackExpansion())
          SawParameterPack = true;
      } else if (OldNonTypeParm && hasVisibleDefaultArgument(OldNonTypeParm) &&
                 NewNonTypeParm->hasDefaultArgument() &&
                 (!SkipBody || !SkipBody->ShouldSkip)) {
        OldDefaultLoc = OldNonTypeParm->getDefaultArgumentLoc();
        NewDefaultLoc = NewNonTypeParm->getDefaultArgumentLoc();
        SawDefaultArgument = true;
        if (!OldNonTypeParm->getOwningModule())
          RedundantDefaultArg = true;
        else if (!getASTContext().isSameDefaultTemplateArgument(
                     OldNonTypeParm, NewNonTypeParm)) {
          InconsistentDefaultArg = true;
          PrevModuleName =
              OldNonTypeParm->getImportedOwningModule()->getFullModuleName();
        }
        PreviousDefaultArgLoc = NewDefaultLoc;
      } else if (OldNonTypeParm && OldNonTypeParm->hasDefaultArgument()) {
        // Merge the default argument from the old declaration to the
        // new declaration.
        NewNonTypeParm->setInheritedDefaultArgument(Context, OldNonTypeParm);
        PreviousDefaultArgLoc = OldNonTypeParm->getDefaultArgumentLoc();
      } else if (NewNonTypeParm->hasDefaultArgument()) {
        SawDefaultArgument = true;
        PreviousDefaultArgLoc = NewNonTypeParm->getDefaultArgumentLoc();
      } else if (SawDefaultArgument)
        MissingDefaultArg = true;
    } else {
      TemplateTemplateParmDecl *NewTemplateParm
        = cast<TemplateTemplateParmDecl>(*NewParam);

      // Check for unexpanded parameter packs, recursively.
      if (::DiagnoseUnexpandedParameterPacks(*this, NewTemplateParm)) {
        Invalid = true;
        continue;
      }

      // Check the presence of a default argument here.
      if (NewTemplateParm->hasDefaultArgument() &&
          DiagnoseDefaultTemplateArgument(*this, TPC,
                                          NewTemplateParm->getLocation(),
                     NewTemplateParm->getDefaultArgument().getSourceRange()))
        NewTemplateParm->removeDefaultArgument();

      // Merge default arguments for template template parameters
      TemplateTemplateParmDecl *OldTemplateParm
        = OldParams? cast<TemplateTemplateParmDecl>(*OldParam) : nullptr;
      if (NewTemplateParm->isParameterPack()) {
        assert(!NewTemplateParm->hasDefaultArgument() &&
               "Parameter packs can't have a default argument!");
        if (!NewTemplateParm->isPackExpansion())
          SawParameterPack = true;
      } else if (OldTemplateParm &&
                 hasVisibleDefaultArgument(OldTemplateParm) &&
                 NewTemplateParm->hasDefaultArgument() &&
                 (!SkipBody || !SkipBody->ShouldSkip)) {
        OldDefaultLoc = OldTemplateParm->getDefaultArgument().getLocation();
        NewDefaultLoc = NewTemplateParm->getDefaultArgument().getLocation();
        SawDefaultArgument = true;
        if (!OldTemplateParm->getOwningModule())
          RedundantDefaultArg = true;
        else if (!getASTContext().isSameDefaultTemplateArgument(
                     OldTemplateParm, NewTemplateParm)) {
          InconsistentDefaultArg = true;
          PrevModuleName =
              OldTemplateParm->getImportedOwningModule()->getFullModuleName();
        }
        PreviousDefaultArgLoc = NewDefaultLoc;
      } else if (OldTemplateParm && OldTemplateParm->hasDefaultArgument()) {
        // Merge the default argument from the old declaration to the
        // new declaration.
        NewTemplateParm->setInheritedDefaultArgument(Context, OldTemplateParm);
        PreviousDefaultArgLoc
          = OldTemplateParm->getDefaultArgument().getLocation();
      } else if (NewTemplateParm->hasDefaultArgument()) {
        SawDefaultArgument = true;
        PreviousDefaultArgLoc
          = NewTemplateParm->getDefaultArgument().getLocation();
      } else if (SawDefaultArgument)
        MissingDefaultArg = true;
    }

    // C++11 [temp.param]p11:
    //   If a template parameter of a primary class template or alias template
    //   is a template parameter pack, it shall be the last template parameter.
    if (SawParameterPack && (NewParam + 1) != NewParamEnd &&
        (TPC == TPC_ClassTemplate || TPC == TPC_VarTemplate ||
         TPC == TPC_TypeAliasTemplate)) {
      Diag((*NewParam)->getLocation(),
           diag::err_template_param_pack_must_be_last_template_parameter);
      Invalid = true;
    }

    // [basic.def.odr]/13:
    //     There can be more than one definition of a
    //     ...
    //     default template argument
    //     ...
    //     in a program provided that each definition appears in a different
    //     translation unit and the definitions satisfy the [same-meaning
    //     criteria of the ODR].
    //
    // Simply, the design of modules allows the definition of template default
    // argument to be repeated across translation unit. Note that the ODR is
    // checked elsewhere. But it is still not allowed to repeat template default
    // argument in the same translation unit.
    if (RedundantDefaultArg) {
      Diag(NewDefaultLoc, diag::err_template_param_default_arg_redefinition);
      Diag(OldDefaultLoc, diag::note_template_param_prev_default_arg);
      Invalid = true;
    } else if (InconsistentDefaultArg) {
      // We could only diagnose about the case that the OldParam is imported.
      // The case NewParam is imported should be handled in ASTReader.
      Diag(NewDefaultLoc,
           diag::err_template_param_default_arg_inconsistent_redefinition);
      Diag(OldDefaultLoc,
           diag::note_template_param_prev_default_arg_in_other_module)
          << PrevModuleName;
      Invalid = true;
    } else if (MissingDefaultArg &&
               (TPC == TPC_ClassTemplate || TPC == TPC_FriendClassTemplate ||
                TPC == TPC_VarTemplate || TPC == TPC_TypeAliasTemplate)) {
      // C++ 23[temp.param]p14:
      // If a template-parameter of a class template, variable template, or
      // alias template has a default template argument, each subsequent
      // template-parameter shall either have a default template argument
      // supplied or be a template parameter pack.
      Diag((*NewParam)->getLocation(),
           diag::err_template_param_default_arg_missing);
      Diag(PreviousDefaultArgLoc, diag::note_template_param_prev_default_arg);
      Invalid = true;
      RemoveDefaultArguments = true;
    }

    // If we have an old template parameter list that we're merging
    // in, move on to the next parameter.
    if (OldParams)
      ++OldParam;
  }

  // We were missing some default arguments at the end of the list, so remove
  // all of the default arguments.
  if (RemoveDefaultArguments) {
    for (TemplateParameterList::iterator NewParam = NewParams->begin(),
                                      NewParamEnd = NewParams->end();
         NewParam != NewParamEnd; ++NewParam) {
      if (TemplateTypeParmDecl *TTP = dyn_cast<TemplateTypeParmDecl>(*NewParam))
        TTP->removeDefaultArgument();
      else if (NonTypeTemplateParmDecl *NTTP
                                = dyn_cast<NonTypeTemplateParmDecl>(*NewParam))
        NTTP->removeDefaultArgument();
      else
        cast<TemplateTemplateParmDecl>(*NewParam)->removeDefaultArgument();
    }
  }

  return Invalid;
}

namespace {

/// A class which looks for a use of a certain level of template
/// parameter.
struct DependencyChecker : RecursiveASTVisitor<DependencyChecker> {
  typedef RecursiveASTVisitor<DependencyChecker> super;

  unsigned Depth;

  // Whether we're looking for a use of a template parameter that makes the
  // overall construct type-dependent / a dependent type. This is strictly
  // best-effort for now; we may fail to match at all for a dependent type
  // in some cases if this is set.
  bool IgnoreNonTypeDependent;

  bool Match;
  SourceLocation MatchLoc;

  DependencyChecker(unsigned Depth, bool IgnoreNonTypeDependent)
      : Depth(Depth), IgnoreNonTypeDependent(IgnoreNonTypeDependent),
        Match(false) {}

  DependencyChecker(TemplateParameterList *Params, bool IgnoreNonTypeDependent)
      : IgnoreNonTypeDependent(IgnoreNonTypeDependent), Match(false) {
    NamedDecl *ND = Params->getParam(0);
    if (TemplateTypeParmDecl *PD = dyn_cast<TemplateTypeParmDecl>(ND)) {
      Depth = PD->getDepth();
    } else if (NonTypeTemplateParmDecl *PD =
                 dyn_cast<NonTypeTemplateParmDecl>(ND)) {
      Depth = PD->getDepth();
    } else {
      Depth = cast<TemplateTemplateParmDecl>(ND)->getDepth();
    }
  }

  bool Matches(unsigned ParmDepth, SourceLocation Loc = SourceLocation()) {
    if (ParmDepth >= Depth) {
      Match = true;
      MatchLoc = Loc;
      return true;
    }
    return false;
  }

  bool TraverseStmt(Stmt *S, DataRecursionQueue *Q = nullptr) {
    // Prune out non-type-dependent expressions if requested. This can
    // sometimes result in us failing to find a template parameter reference
    // (if a value-dependent expression creates a dependent type), but this
    // mode is best-effort only.
    if (auto *E = dyn_cast_or_null<Expr>(S))
      if (IgnoreNonTypeDependent && !E->isTypeDependent())
        return true;
    return super::TraverseStmt(S, Q);
  }

  bool TraverseTypeLoc(TypeLoc TL) {
    if (IgnoreNonTypeDependent && !TL.isNull() &&
        !TL.getType()->isDependentType())
      return true;
    return super::TraverseTypeLoc(TL);
  }

  bool VisitTemplateTypeParmTypeLoc(TemplateTypeParmTypeLoc TL) {
    return !Matches(TL.getTypePtr()->getDepth(), TL.getNameLoc());
  }

  bool VisitTemplateTypeParmType(const TemplateTypeParmType *T) {
    // For a best-effort search, keep looking until we find a location.
    return IgnoreNonTypeDependent || !Matches(T->getDepth());
  }

  bool TraverseTemplateName(TemplateName N) {
    if (TemplateTemplateParmDecl *PD =
          dyn_cast_or_null<TemplateTemplateParmDecl>(N.getAsTemplateDecl()))
      if (Matches(PD->getDepth()))
        return false;
    return super::TraverseTemplateName(N);
  }

  bool VisitDeclRefExpr(DeclRefExpr *E) {
    if (NonTypeTemplateParmDecl *PD =
          dyn_cast<NonTypeTemplateParmDecl>(E->getDecl()))
      if (Matches(PD->getDepth(), E->getExprLoc()))
        return false;
    return super::VisitDeclRefExpr(E);
  }

  bool VisitSubstTemplateTypeParmType(const SubstTemplateTypeParmType *T) {
    return TraverseType(T->getReplacementType());
  }

  bool
  VisitSubstTemplateTypeParmPackType(const SubstTemplateTypeParmPackType *T) {
    return TraverseTemplateArgument(T->getArgumentPack());
  }

  bool TraverseInjectedClassNameType(const InjectedClassNameType *T) {
    return TraverseType(T->getInjectedSpecializationType());
  }
};
} // end anonymous namespace

/// Determines whether a given type depends on the given parameter
/// list.
static bool
DependsOnTemplateParameters(QualType T, TemplateParameterList *Params) {
  if (!Params->size())
    return false;

  DependencyChecker Checker(Params, /*IgnoreNonTypeDependent*/false);
  Checker.TraverseType(T);
  return Checker.Match;
}

// Find the source range corresponding to the named type in the given
// nested-name-specifier, if any.
static SourceRange getRangeOfTypeInNestedNameSpecifier(ASTContext &Context,
                                                       QualType T,
                                                       const CXXScopeSpec &SS) {
  NestedNameSpecifierLoc NNSLoc(SS.getScopeRep(), SS.location_data());
  while (NestedNameSpecifier *NNS = NNSLoc.getNestedNameSpecifier()) {
    if (const Type *CurType = NNS->getAsType()) {
      if (Context.hasSameUnqualifiedType(T, QualType(CurType, 0)))
        return NNSLoc.getTypeLoc().getSourceRange();
    } else
      break;

    NNSLoc = NNSLoc.getPrefix();
  }

  return SourceRange();
}

TemplateParameterList *Sema::MatchTemplateParametersToScopeSpecifier(
    SourceLocation DeclStartLoc, SourceLocation DeclLoc, const CXXScopeSpec &SS,
    TemplateIdAnnotation *TemplateId,
    ArrayRef<TemplateParameterList *> ParamLists, bool IsFriend,
    bool &IsMemberSpecialization, bool &Invalid, bool SuppressDiagnostic) {
  IsMemberSpecialization = false;
  Invalid = false;

  // The sequence of nested types to which we will match up the template
  // parameter lists. We first build this list by starting with the type named
  // by the nested-name-specifier and walking out until we run out of types.
  SmallVector<QualType, 4> NestedTypes;
  QualType T;
  if (SS.getScopeRep()) {
    if (CXXRecordDecl *Record
              = dyn_cast_or_null<CXXRecordDecl>(computeDeclContext(SS, true)))
      T = Context.getTypeDeclType(Record);
    else
      T = QualType(SS.getScopeRep()->getAsType(), 0);
  }

  // If we found an explicit specialization that prevents us from needing
  // 'template<>' headers, this will be set to the location of that
  // explicit specialization.
  SourceLocation ExplicitSpecLoc;

  while (!T.isNull()) {
    NestedTypes.push_back(T);

    // Retrieve the parent of a record type.
    if (CXXRecordDecl *Record = T->getAsCXXRecordDecl()) {
      // If this type is an explicit specialization, we're done.
      if (ClassTemplateSpecializationDecl *Spec
          = dyn_cast<ClassTemplateSpecializationDecl>(Record)) {
        if (!isa<ClassTemplatePartialSpecializationDecl>(Spec) &&
            Spec->getSpecializationKind() == TSK_ExplicitSpecialization) {
          ExplicitSpecLoc = Spec->getLocation();
          break;
        }
      } else if (Record->getTemplateSpecializationKind()
                                                == TSK_ExplicitSpecialization) {
        ExplicitSpecLoc = Record->getLocation();
        break;
      }

      if (TypeDecl *Parent = dyn_cast<TypeDecl>(Record->getParent()))
        T = Context.getTypeDeclType(Parent);
      else
        T = QualType();
      continue;
    }

    if (const TemplateSpecializationType *TST
                                     = T->getAs<TemplateSpecializationType>()) {
      if (TemplateDecl *Template = TST->getTemplateName().getAsTemplateDecl()) {
        if (TypeDecl *Parent = dyn_cast<TypeDecl>(Template->getDeclContext()))
          T = Context.getTypeDeclType(Parent);
        else
          T = QualType();
        continue;
      }
    }

    // Look one step prior in a dependent template specialization type.
    if (const DependentTemplateSpecializationType *DependentTST
                          = T->getAs<DependentTemplateSpecializationType>()) {
      if (NestedNameSpecifier *NNS = DependentTST->getQualifier())
        T = QualType(NNS->getAsType(), 0);
      else
        T = QualType();
      continue;
    }

    // Look one step prior in a dependent name type.
    if (const DependentNameType *DependentName = T->getAs<DependentNameType>()){
      if (NestedNameSpecifier *NNS = DependentName->getQualifier())
        T = QualType(NNS->getAsType(), 0);
      else
        T = QualType();
      continue;
    }

    // Retrieve the parent of an enumeration type.
    if (const EnumType *EnumT = T->getAs<EnumType>()) {
      // FIXME: Forward-declared enums require a TSK_ExplicitSpecialization
      // check here.
      EnumDecl *Enum = EnumT->getDecl();

      // Get to the parent type.
      if (TypeDecl *Parent = dyn_cast<TypeDecl>(Enum->getParent()))
        T = Context.getTypeDeclType(Parent);
      else
        T = QualType();
      continue;
    }

    T = QualType();
  }
  // Reverse the nested types list, since we want to traverse from the outermost
  // to the innermost while checking template-parameter-lists.
  std::reverse(NestedTypes.begin(), NestedTypes.end());

  // C++0x [temp.expl.spec]p17:
  //   A member or a member template may be nested within many
  //   enclosing class templates. In an explicit specialization for
  //   such a member, the member declaration shall be preceded by a
  //   template<> for each enclosing class template that is
  //   explicitly specialized.
  bool SawNonEmptyTemplateParameterList = false;

  auto CheckExplicitSpecialization = [&](SourceRange Range, bool Recovery) {
    if (SawNonEmptyTemplateParameterList) {
      if (!SuppressDiagnostic)
        Diag(DeclLoc, diag::err_specialize_member_of_template)
          << !Recovery << Range;
      Invalid = true;
      IsMemberSpecialization = false;
      return true;
    }

    return false;
  };

  auto DiagnoseMissingExplicitSpecialization = [&] (SourceRange Range) {
    // Check that we can have an explicit specialization here.
    if (CheckExplicitSpecialization(Range, true))
      return true;

    // We don't have a template header, but we should.
    SourceLocation ExpectedTemplateLoc;
    if (!ParamLists.empty())
      ExpectedTemplateLoc = ParamLists[0]->getTemplateLoc();
    else
      ExpectedTemplateLoc = DeclStartLoc;

    if (!SuppressDiagnostic)
      Diag(DeclLoc, diag::err_template_spec_needs_header)
        << Range
        << FixItHint::CreateInsertion(ExpectedTemplateLoc, "template<> ");
    return false;
  };

  unsigned ParamIdx = 0;
  for (unsigned TypeIdx = 0, NumTypes = NestedTypes.size(); TypeIdx != NumTypes;
       ++TypeIdx) {
    T = NestedTypes[TypeIdx];

    // Whether we expect a 'template<>' header.
    bool NeedEmptyTemplateHeader = false;

    // Whether we expect a template header with parameters.
    bool NeedNonemptyTemplateHeader = false;

    // For a dependent type, the set of template parameters that we
    // expect to see.
    TemplateParameterList *ExpectedTemplateParams = nullptr;

    // C++0x [temp.expl.spec]p15:
    //   A member or a member template may be nested within many enclosing
    //   class templates. In an explicit specialization for such a member, the
    //   member declaration shall be preceded by a template<> for each
    //   enclosing class template that is explicitly specialized.
    if (CXXRecordDecl *Record = T->getAsCXXRecordDecl()) {
      if (ClassTemplatePartialSpecializationDecl *Partial
            = dyn_cast<ClassTemplatePartialSpecializationDecl>(Record)) {
        ExpectedTemplateParams = Partial->getTemplateParameters();
        NeedNonemptyTemplateHeader = true;
      } else if (Record->isDependentType()) {
        if (Record->getDescribedClassTemplate()) {
          ExpectedTemplateParams = Record->getDescribedClassTemplate()
                                                      ->getTemplateParameters();
          NeedNonemptyTemplateHeader = true;
        }
      } else if (ClassTemplateSpecializationDecl *Spec
                     = dyn_cast<ClassTemplateSpecializationDecl>(Record)) {
        // C++0x [temp.expl.spec]p4:
        //   Members of an explicitly specialized class template are defined
        //   in the same manner as members of normal classes, and not using
        //   the template<> syntax.
        if (Spec->getSpecializationKind() != TSK_ExplicitSpecialization)
          NeedEmptyTemplateHeader = true;
        else
          continue;
      } else if (Record->getTemplateSpecializationKind()) {
        if (Record->getTemplateSpecializationKind()
                                                != TSK_ExplicitSpecialization &&
            TypeIdx == NumTypes - 1)
          IsMemberSpecialization = true;

        continue;
      }
    } else if (const TemplateSpecializationType *TST
                                     = T->getAs<TemplateSpecializationType>()) {
      if (TemplateDecl *Template = TST->getTemplateName().getAsTemplateDecl()) {
        ExpectedTemplateParams = Template->getTemplateParameters();
        NeedNonemptyTemplateHeader = true;
      }
    } else if (T->getAs<DependentTemplateSpecializationType>()) {
      // FIXME:  We actually could/should check the template arguments here
      // against the corresponding template parameter list.
      NeedNonemptyTemplateHeader = false;
    }

    // C++ [temp.expl.spec]p16:
    //   In an explicit specialization declaration for a member of a class
    //   template or a member template that appears in namespace scope, the
    //   member template and some of its enclosing class templates may remain
    //   unspecialized, except that the declaration shall not explicitly
    //   specialize a class member template if its enclosing class templates
    //   are not explicitly specialized as well.
    if (ParamIdx < ParamLists.size()) {
      if (ParamLists[ParamIdx]->size() == 0) {
        if (CheckExplicitSpecialization(ParamLists[ParamIdx]->getSourceRange(),
                                        false))
          return nullptr;
      } else
        SawNonEmptyTemplateParameterList = true;
    }

    if (NeedEmptyTemplateHeader) {
      // If we're on the last of the types, and we need a 'template<>' header
      // here, then it's a member specialization.
      if (TypeIdx == NumTypes - 1)
        IsMemberSpecialization = true;

      if (ParamIdx < ParamLists.size()) {
        if (ParamLists[ParamIdx]->size() > 0) {
          // The header has template parameters when it shouldn't. Complain.
          if (!SuppressDiagnostic)
            Diag(ParamLists[ParamIdx]->getTemplateLoc(),
                 diag::err_template_param_list_matches_nontemplate)
              << T
              << SourceRange(ParamLists[ParamIdx]->getLAngleLoc(),
                             ParamLists[ParamIdx]->getRAngleLoc())
              << getRangeOfTypeInNestedNameSpecifier(Context, T, SS);
          Invalid = true;
          return nullptr;
        }

        // Consume this template header.
        ++ParamIdx;
        continue;
      }

      if (!IsFriend)
        if (DiagnoseMissingExplicitSpecialization(
                getRangeOfTypeInNestedNameSpecifier(Context, T, SS)))
          return nullptr;

      continue;
    }

    if (NeedNonemptyTemplateHeader) {
      // In friend declarations we can have template-ids which don't
      // depend on the corresponding template parameter lists.  But
      // assume that empty parameter lists are supposed to match this
      // template-id.
      if (IsFriend && T->isDependentType()) {
        if (ParamIdx < ParamLists.size() &&
            DependsOnTemplateParameters(T, ParamLists[ParamIdx]))
          ExpectedTemplateParams = nullptr;
        else
          continue;
      }

      if (ParamIdx < ParamLists.size()) {
        // Check the template parameter list, if we can.
        if (ExpectedTemplateParams &&
            !TemplateParameterListsAreEqual(ParamLists[ParamIdx],
                                            ExpectedTemplateParams,
                                            !SuppressDiagnostic, TPL_TemplateMatch))
          Invalid = true;

        if (!Invalid &&
            CheckTemplateParameterList(ParamLists[ParamIdx], nullptr,
                                       TPC_ClassTemplateMember))
          Invalid = true;

        ++ParamIdx;
        continue;
      }

      if (!SuppressDiagnostic)
        Diag(DeclLoc, diag::err_template_spec_needs_template_parameters)
          << T
          << getRangeOfTypeInNestedNameSpecifier(Context, T, SS);
      Invalid = true;
      continue;
    }
  }

  // If there were at least as many template-ids as there were template
  // parameter lists, then there are no template parameter lists remaining for
  // the declaration itself.
  if (ParamIdx >= ParamLists.size()) {
    if (TemplateId && !IsFriend) {
      // We don't have a template header for the declaration itself, but we
      // should.
      DiagnoseMissingExplicitSpecialization(SourceRange(TemplateId->LAngleLoc,
                                                        TemplateId->RAngleLoc));

      // Fabricate an empty template parameter list for the invented header.
      return TemplateParameterList::Create(Context, SourceLocation(),
                                           SourceLocation(), std::nullopt,
                                           SourceLocation(), nullptr);
    }

    return nullptr;
  }

  // If there were too many template parameter lists, complain about that now.
  if (ParamIdx < ParamLists.size() - 1) {
    bool HasAnyExplicitSpecHeader = false;
    bool AllExplicitSpecHeaders = true;
    for (unsigned I = ParamIdx, E = ParamLists.size() - 1; I != E; ++I) {
      if (ParamLists[I]->size() == 0)
        HasAnyExplicitSpecHeader = true;
      else
        AllExplicitSpecHeaders = false;
    }

    if (!SuppressDiagnostic)
      Diag(ParamLists[ParamIdx]->getTemplateLoc(),
           AllExplicitSpecHeaders ? diag::ext_template_spec_extra_headers
                                  : diag::err_template_spec_extra_headers)
          << SourceRange(ParamLists[ParamIdx]->getTemplateLoc(),
                         ParamLists[ParamLists.size() - 2]->getRAngleLoc());

    // If there was a specialization somewhere, such that 'template<>' is
    // not required, and there were any 'template<>' headers, note where the
    // specialization occurred.
    if (ExplicitSpecLoc.isValid() && HasAnyExplicitSpecHeader &&
        !SuppressDiagnostic)
      Diag(ExplicitSpecLoc,
           diag::note_explicit_template_spec_does_not_need_header)
        << NestedTypes.back();

    // We have a template parameter list with no corresponding scope, which
    // means that the resulting template declaration can't be instantiated
    // properly (we'll end up with dependent nodes when we shouldn't).
    if (!AllExplicitSpecHeaders)
      Invalid = true;
  }

  // C++ [temp.expl.spec]p16:
  //   In an explicit specialization declaration for a member of a class
  //   template or a member template that ap- pears in namespace scope, the
  //   member template and some of its enclosing class templates may remain
  //   unspecialized, except that the declaration shall not explicitly
  //   specialize a class member template if its en- closing class templates
  //   are not explicitly specialized as well.
  if (ParamLists.back()->size() == 0 &&
      CheckExplicitSpecialization(ParamLists[ParamIdx]->getSourceRange(),
                                  false))
    return nullptr;

  // Return the last template parameter list, which corresponds to the
  // entity being declared.
  return ParamLists.back();
}

void Sema::NoteAllFoundTemplates(TemplateName Name) {
  if (TemplateDecl *Template = Name.getAsTemplateDecl()) {
    Diag(Template->getLocation(), diag::note_template_declared_here)
        << (isa<FunctionTemplateDecl>(Template)
                ? 0
                : isa<ClassTemplateDecl>(Template)
                      ? 1
                      : isa<VarTemplateDecl>(Template)
                            ? 2
                            : isa<TypeAliasTemplateDecl>(Template) ? 3 : 4)
        << Template->getDeclName();
    return;
  }

  if (OverloadedTemplateStorage *OST = Name.getAsOverloadedTemplate()) {
    for (OverloadedTemplateStorage::iterator I = OST->begin(),
                                          IEnd = OST->end();
         I != IEnd; ++I)
      Diag((*I)->getLocation(), diag::note_template_declared_here)
        << 0 << (*I)->getDeclName();

    return;
  }
}

static QualType
checkBuiltinTemplateIdType(Sema &SemaRef, BuiltinTemplateDecl *BTD,
                           ArrayRef<TemplateArgument> Converted,
                           SourceLocation TemplateLoc,
                           TemplateArgumentListInfo &TemplateArgs) {
  ASTContext &Context = SemaRef.getASTContext();

  switch (BTD->getBuiltinTemplateKind()) {
  case BTK__make_integer_seq: {
    // Specializations of __make_integer_seq<S, T, N> are treated like
    // S<T, 0, ..., N-1>.

    QualType OrigType = Converted[1].getAsType();
    // C++14 [inteseq.intseq]p1:
    //   T shall be an integer type.
    if (!OrigType->isDependentType() && !OrigType->isIntegralType(Context)) {
      SemaRef.Diag(TemplateArgs[1].getLocation(),
                   diag::err_integer_sequence_integral_element_type);
      return QualType();
    }

    TemplateArgument NumArgsArg = Converted[2];
    if (NumArgsArg.isDependent())
      return Context.getCanonicalTemplateSpecializationType(TemplateName(BTD),
                                                            Converted);

    TemplateArgumentListInfo SyntheticTemplateArgs;
    // The type argument, wrapped in substitution sugar, gets reused as the
    // first template argument in the synthetic template argument list.
    SyntheticTemplateArgs.addArgument(
        TemplateArgumentLoc(TemplateArgument(OrigType),
                            SemaRef.Context.getTrivialTypeSourceInfo(
                                OrigType, TemplateArgs[1].getLocation())));

    if (llvm::APSInt NumArgs = NumArgsArg.getAsIntegral(); NumArgs >= 0) {
      // Expand N into 0 ... N-1.
      for (llvm::APSInt I(NumArgs.getBitWidth(), NumArgs.isUnsigned());
           I < NumArgs; ++I) {
        TemplateArgument TA(Context, I, OrigType);
        SyntheticTemplateArgs.addArgument(SemaRef.getTrivialTemplateArgumentLoc(
            TA, OrigType, TemplateArgs[2].getLocation()));
      }
    } else {
      // C++14 [inteseq.make]p1:
      //   If N is negative the program is ill-formed.
      SemaRef.Diag(TemplateArgs[2].getLocation(),
                   diag::err_integer_sequence_negative_length);
      return QualType();
    }

    // The first template argument will be reused as the template decl that
    // our synthetic template arguments will be applied to.
    return SemaRef.CheckTemplateIdType(Converted[0].getAsTemplate(),
                                       TemplateLoc, SyntheticTemplateArgs);
  }

  case BTK__type_pack_element:
    // Specializations of
    //    __type_pack_element<Index, T_1, ..., T_N>
    // are treated like T_Index.
    assert(Converted.size() == 2 &&
      "__type_pack_element should be given an index and a parameter pack");

    TemplateArgument IndexArg = Converted[0], Ts = Converted[1];
    if (IndexArg.isDependent() || Ts.isDependent())
      return Context.getCanonicalTemplateSpecializationType(TemplateName(BTD),
                                                            Converted);

    llvm::APSInt Index = IndexArg.getAsIntegral();
    assert(Index >= 0 && "the index used with __type_pack_element should be of "
                         "type std::size_t, and hence be non-negative");
    // If the Index is out of bounds, the program is ill-formed.
    if (Index >= Ts.pack_size()) {
      SemaRef.Diag(TemplateArgs[0].getLocation(),
                   diag::err_type_pack_element_out_of_bounds);
      return QualType();
    }

    // We simply return the type at index `Index`.
    int64_t N = Index.getExtValue();
    return Ts.getPackAsArray()[N].getAsType();
  }
  llvm_unreachable("unexpected BuiltinTemplateDecl!");
}

/// Determine whether this alias template is "enable_if_t".
/// libc++ >=14 uses "__enable_if_t" in C++11 mode.
static bool isEnableIfAliasTemplate(TypeAliasTemplateDecl *AliasTemplate) {
  return AliasTemplate->getName() == "enable_if_t" ||
         AliasTemplate->getName() == "__enable_if_t";
}

/// Collect all of the separable terms in the given condition, which
/// might be a conjunction.
///
/// FIXME: The right answer is to convert the logical expression into
/// disjunctive normal form, so we can find the first failed term
/// within each possible clause.
static void collectConjunctionTerms(Expr *Clause,
                                    SmallVectorImpl<Expr *> &Terms) {
  if (auto BinOp = dyn_cast<BinaryOperator>(Clause->IgnoreParenImpCasts())) {
    if (BinOp->getOpcode() == BO_LAnd) {
      collectConjunctionTerms(BinOp->getLHS(), Terms);
      collectConjunctionTerms(BinOp->getRHS(), Terms);
      return;
    }
  }

  Terms.push_back(Clause);
}

// The ranges-v3 library uses an odd pattern of a top-level "||" with
// a left-hand side that is value-dependent but never true. Identify
// the idiom and ignore that term.
static Expr *lookThroughRangesV3Condition(Preprocessor &PP, Expr *Cond) {
  // Top-level '||'.
  auto *BinOp = dyn_cast<BinaryOperator>(Cond->IgnoreParenImpCasts());
  if (!BinOp) return Cond;

  if (BinOp->getOpcode() != BO_LOr) return Cond;

  // With an inner '==' that has a literal on the right-hand side.
  Expr *LHS = BinOp->getLHS();
  auto *InnerBinOp = dyn_cast<BinaryOperator>(LHS->IgnoreParenImpCasts());
  if (!InnerBinOp) return Cond;

  if (InnerBinOp->getOpcode() != BO_EQ ||
      !isa<IntegerLiteral>(InnerBinOp->getRHS()))
    return Cond;

  // If the inner binary operation came from a macro expansion named
  // CONCEPT_REQUIRES or CONCEPT_REQUIRES_, return the right-hand side
  // of the '||', which is the real, user-provided condition.
  SourceLocation Loc = InnerBinOp->getExprLoc();
  if (!Loc.isMacroID()) return Cond;

  StringRef MacroName = PP.getImmediateMacroName(Loc);
  if (MacroName == "CONCEPT_REQUIRES" || MacroName == "CONCEPT_REQUIRES_")
    return BinOp->getRHS();

  return Cond;
}

namespace {

// A PrinterHelper that prints more helpful diagnostics for some sub-expressions
// within failing boolean expression, such as substituting template parameters
// for actual types.
class FailedBooleanConditionPrinterHelper : public PrinterHelper {
public:
  explicit FailedBooleanConditionPrinterHelper(const PrintingPolicy &P)
      : Policy(P) {}

  bool handledStmt(Stmt *E, raw_ostream &OS) override {
    const auto *DR = dyn_cast<DeclRefExpr>(E);
    if (DR && DR->getQualifier()) {
      // If this is a qualified name, expand the template arguments in nested
      // qualifiers.
      DR->getQualifier()->print(OS, Policy, true);
      // Then print the decl itself.
      const ValueDecl *VD = DR->getDecl();
      OS << VD->getName();
      if (const auto *IV = dyn_cast<VarTemplateSpecializationDecl>(VD)) {
        // This is a template variable, print the expanded template arguments.
        printTemplateArgumentList(
            OS, IV->getTemplateArgs().asArray(), Policy,
            IV->getSpecializedTemplate()->getTemplateParameters());
      }
      return true;
    }
    return false;
  }

private:
  const PrintingPolicy Policy;
};

} // end anonymous namespace

std::pair<Expr *, std::string>
Sema::findFailedBooleanCondition(Expr *Cond) {
  Cond = lookThroughRangesV3Condition(PP, Cond);

  // Separate out all of the terms in a conjunction.
  SmallVector<Expr *, 4> Terms;
  collectConjunctionTerms(Cond, Terms);

  // Determine which term failed.
  Expr *FailedCond = nullptr;
  for (Expr *Term : Terms) {
    Expr *TermAsWritten = Term->IgnoreParenImpCasts();

    // Literals are uninteresting.
    if (isa<CXXBoolLiteralExpr>(TermAsWritten) ||
        isa<IntegerLiteral>(TermAsWritten))
      continue;

    // The initialization of the parameter from the argument is
    // a constant-evaluated context.
    EnterExpressionEvaluationContext ConstantEvaluated(
      *this, Sema::ExpressionEvaluationContext::ConstantEvaluated);

    bool Succeeded;
    if (Term->EvaluateAsBooleanCondition(Succeeded, Context) &&
        !Succeeded) {
      FailedCond = TermAsWritten;
      break;
    }
  }
  if (!FailedCond)
    FailedCond = Cond->IgnoreParenImpCasts();

  std::string Description;
  {
    llvm::raw_string_ostream Out(Description);
    PrintingPolicy Policy = getPrintingPolicy();
    Policy.PrintCanonicalTypes = true;
    FailedBooleanConditionPrinterHelper Helper(Policy);
    FailedCond->printPretty(Out, &Helper, Policy, 0, "\n", nullptr);
  }
  return { FailedCond, Description };
}

QualType Sema::CheckTemplateIdType(TemplateName Name,
                                   SourceLocation TemplateLoc,
                                   TemplateArgumentListInfo &TemplateArgs) {
  DependentTemplateName *DTN
    = Name.getUnderlying().getAsDependentTemplateName();
  if (DTN && DTN->isIdentifier())
    // When building a template-id where the template-name is dependent,
    // assume the template is a type template. Either our assumption is
    // correct, or the code is ill-formed and will be diagnosed when the
    // dependent name is substituted.
    return Context.getDependentTemplateSpecializationType(
        ElaboratedTypeKeyword::None, DTN->getQualifier(), DTN->getIdentifier(),
        TemplateArgs.arguments());

  if (Name.getAsAssumedTemplateName() &&
      resolveAssumedTemplateNameAsType(/*Scope*/nullptr, Name, TemplateLoc))
    return QualType();

  TemplateDecl *Template = Name.getAsTemplateDecl();
  if (!Template || isa<FunctionTemplateDecl>(Template) ||
      isa<VarTemplateDecl>(Template) || isa<ConceptDecl>(Template)) {
    // We might have a substituted template template parameter pack. If so,
    // build a template specialization type for it.
    if (Name.getAsSubstTemplateTemplateParmPack())
      return Context.getTemplateSpecializationType(Name,
                                                   TemplateArgs.arguments());

    Diag(TemplateLoc, diag::err_template_id_not_a_type)
      << Name;
    NoteAllFoundTemplates(Name);
    return QualType();
  }

  // Check that the template argument list is well-formed for this
  // template.
  SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;
  if (CheckTemplateArgumentList(Template, TemplateLoc, TemplateArgs, false,
                                SugaredConverted, CanonicalConverted,
                                /*UpdateArgsWithConversions=*/true))
    return QualType();

  QualType CanonType;

  if (TypeAliasTemplateDecl *AliasTemplate =
          dyn_cast<TypeAliasTemplateDecl>(Template)) {

    // Find the canonical type for this type alias template specialization.
    TypeAliasDecl *Pattern = AliasTemplate->getTemplatedDecl();
    if (Pattern->isInvalidDecl())
      return QualType();

    // Only substitute for the innermost template argument list.
    MultiLevelTemplateArgumentList TemplateArgLists;
    TemplateArgLists.addOuterTemplateArguments(Template, CanonicalConverted,
                                               /*Final=*/false);
    TemplateArgLists.addOuterRetainedLevels(
        AliasTemplate->getTemplateParameters()->getDepth());

    LocalInstantiationScope Scope(*this);
    InstantiatingTemplate Inst(
        *this, /*PointOfInstantiation=*/TemplateLoc,
        /*Entity=*/AliasTemplate,
        /*TemplateArgs=*/TemplateArgLists.getInnermost());

    // Diagnose uses of this alias.
    (void)DiagnoseUseOfDecl(AliasTemplate, TemplateLoc);

    if (Inst.isInvalid())
      return QualType();

    std::optional<ContextRAII> SavedContext;
    if (!AliasTemplate->getDeclContext()->isFileContext())
      SavedContext.emplace(*this, AliasTemplate->getDeclContext());

    CanonType =
        SubstType(Pattern->getUnderlyingType(), TemplateArgLists,
                  AliasTemplate->getLocation(), AliasTemplate->getDeclName());
    if (CanonType.isNull()) {
      // If this was enable_if and we failed to find the nested type
      // within enable_if in a SFINAE context, dig out the specific
      // enable_if condition that failed and present that instead.
      if (isEnableIfAliasTemplate(AliasTemplate)) {
        if (auto DeductionInfo = isSFINAEContext()) {
          if (*DeductionInfo &&
              (*DeductionInfo)->hasSFINAEDiagnostic() &&
              (*DeductionInfo)->peekSFINAEDiagnostic().second.getDiagID() ==
                diag::err_typename_nested_not_found_enable_if &&
              TemplateArgs[0].getArgument().getKind()
                == TemplateArgument::Expression) {
            Expr *FailedCond;
            std::string FailedDescription;
            std::tie(FailedCond, FailedDescription) =
              findFailedBooleanCondition(TemplateArgs[0].getSourceExpression());

            // Remove the old SFINAE diagnostic.
            PartialDiagnosticAt OldDiag =
              {SourceLocation(), PartialDiagnostic::NullDiagnostic()};
            (*DeductionInfo)->takeSFINAEDiagnostic(OldDiag);

            // Add a new SFINAE diagnostic specifying which condition
            // failed.
            (*DeductionInfo)->addSFINAEDiagnostic(
              OldDiag.first,
              PDiag(diag::err_typename_nested_not_found_requirement)
                << FailedDescription
                << FailedCond->getSourceRange());
          }
        }
      }

      return QualType();
    }
  } else if (auto *BTD = dyn_cast<BuiltinTemplateDecl>(Template)) {
    CanonType = checkBuiltinTemplateIdType(*this, BTD, SugaredConverted,
                                           TemplateLoc, TemplateArgs);
  } else if (Name.isDependent() ||
             TemplateSpecializationType::anyDependentTemplateArguments(
                 TemplateArgs, CanonicalConverted)) {
    // This class template specialization is a dependent
    // type. Therefore, its canonical type is another class template
    // specialization type that contains all of the converted
    // arguments in canonical form. This ensures that, e.g., A<T> and
    // A<T, T> have identical types when A is declared as:
    //
    //   template<typename T, typename U = T> struct A;
    CanonType = Context.getCanonicalTemplateSpecializationType(
        Name, CanonicalConverted);

    // This might work out to be a current instantiation, in which
    // case the canonical type needs to be the InjectedClassNameType.
    //
    // TODO: in theory this could be a simple hashtable lookup; most
    // changes to CurContext don't change the set of current
    // instantiations.
    if (isa<ClassTemplateDecl>(Template)) {
      for (DeclContext *Ctx = CurContext; Ctx; Ctx = Ctx->getLookupParent()) {
        // If we get out to a namespace, we're done.
        if (Ctx->isFileContext()) break;

        // If this isn't a record, keep looking.
        CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(Ctx);
        if (!Record) continue;

        // Look for one of the two cases with InjectedClassNameTypes
        // and check whether it's the same template.
        if (!isa<ClassTemplatePartialSpecializationDecl>(Record) &&
            !Record->getDescribedClassTemplate())
          continue;

        // Fetch the injected class name type and check whether its
        // injected type is equal to the type we just built.
        QualType ICNT = Context.getTypeDeclType(Record);
        QualType Injected = cast<InjectedClassNameType>(ICNT)
          ->getInjectedSpecializationType();

        if (CanonType != Injected->getCanonicalTypeInternal())
          continue;

        // If so, the canonical type of this TST is the injected
        // class name type of the record we just found.
        assert(ICNT.isCanonical());
        CanonType = ICNT;
        break;
      }
    }
  } else if (ClassTemplateDecl *ClassTemplate =
                 dyn_cast<ClassTemplateDecl>(Template)) {
    // Find the class template specialization declaration that
    // corresponds to these arguments.
    void *InsertPos = nullptr;
    ClassTemplateSpecializationDecl *Decl =
        ClassTemplate->findSpecialization(CanonicalConverted, InsertPos);
    if (!Decl) {
      // This is the first time we have referenced this class template
      // specialization. Create the canonical declaration and add it to
      // the set of specializations.
      Decl = ClassTemplateSpecializationDecl::Create(
          Context, ClassTemplate->getTemplatedDecl()->getTagKind(),
          ClassTemplate->getDeclContext(),
          ClassTemplate->getTemplatedDecl()->getBeginLoc(),
          ClassTemplate->getLocation(), ClassTemplate, CanonicalConverted,
          nullptr);
      ClassTemplate->AddSpecialization(Decl, InsertPos);
      if (ClassTemplate->isOutOfLine())
        Decl->setLexicalDeclContext(ClassTemplate->getLexicalDeclContext());
    }

    if (Decl->getSpecializationKind() == TSK_Undeclared &&
        ClassTemplate->getTemplatedDecl()->hasAttrs()) {
      InstantiatingTemplate Inst(*this, TemplateLoc, Decl);
      if (!Inst.isInvalid()) {
        MultiLevelTemplateArgumentList TemplateArgLists(Template,
                                                        CanonicalConverted,
                                                        /*Final=*/false);
        InstantiateAttrsForDecl(TemplateArgLists,
                                ClassTemplate->getTemplatedDecl(), Decl);
      }
    }

    // Diagnose uses of this specialization.
    (void)DiagnoseUseOfDecl(Decl, TemplateLoc);

    CanonType = Context.getTypeDeclType(Decl);
    assert(isa<RecordType>(CanonType) &&
           "type of non-dependent specialization is not a RecordType");
  } else {
    llvm_unreachable("Unhandled template kind");
  }

  // Build the fully-sugared type for this class template
  // specialization, which refers back to the class template
  // specialization we created or found.
  return Context.getTemplateSpecializationType(Name, TemplateArgs.arguments(),
                                               CanonType);
}

void Sema::ActOnUndeclaredTypeTemplateName(Scope *S, TemplateTy &ParsedName,
                                           TemplateNameKind &TNK,
                                           SourceLocation NameLoc,
                                           IdentifierInfo *&II) {
  assert(TNK == TNK_Undeclared_template && "not an undeclared template name");

  TemplateName Name = ParsedName.get();
  auto *ATN = Name.getAsAssumedTemplateName();
  assert(ATN && "not an assumed template name");
  II = ATN->getDeclName().getAsIdentifierInfo();

  if (!resolveAssumedTemplateNameAsType(S, Name, NameLoc, /*Diagnose*/false)) {
    // Resolved to a type template name.
    ParsedName = TemplateTy::make(Name);
    TNK = TNK_Type_template;
  }
}

bool Sema::resolveAssumedTemplateNameAsType(Scope *S, TemplateName &Name,
                                            SourceLocation NameLoc,
                                            bool Diagnose) {
  // We assumed this undeclared identifier to be an (ADL-only) function
  // template name, but it was used in a context where a type was required.
  // Try to typo-correct it now.
  AssumedTemplateStorage *ATN = Name.getAsAssumedTemplateName();
  assert(ATN && "not an assumed template name");

  LookupResult R(*this, ATN->getDeclName(), NameLoc, LookupOrdinaryName);
  struct CandidateCallback : CorrectionCandidateCallback {
    bool ValidateCandidate(const TypoCorrection &TC) override {
      return TC.getCorrectionDecl() &&
             getAsTypeTemplateDecl(TC.getCorrectionDecl());
    }
    std::unique_ptr<CorrectionCandidateCallback> clone() override {
      return std::make_unique<CandidateCallback>(*this);
    }
  } FilterCCC;

  TypoCorrection Corrected =
      CorrectTypo(R.getLookupNameInfo(), R.getLookupKind(), S, nullptr,
                  FilterCCC, CTK_ErrorRecovery);
  if (Corrected && Corrected.getFoundDecl()) {
    diagnoseTypo(Corrected, PDiag(diag::err_no_template_suggest)
                                << ATN->getDeclName());
    Name = TemplateName(Corrected.getCorrectionDeclAs<TemplateDecl>());
    return false;
  }

  if (Diagnose)
    Diag(R.getNameLoc(), diag::err_no_template) << R.getLookupName();
  return true;
}

TypeResult Sema::ActOnTemplateIdType(
    Scope *S, CXXScopeSpec &SS, SourceLocation TemplateKWLoc,
    TemplateTy TemplateD, const IdentifierInfo *TemplateII,
    SourceLocation TemplateIILoc, SourceLocation LAngleLoc,
    ASTTemplateArgsPtr TemplateArgsIn, SourceLocation RAngleLoc,
    bool IsCtorOrDtorName, bool IsClassName,
    ImplicitTypenameContext AllowImplicitTypename) {
  if (SS.isInvalid())
    return true;

  if (!IsCtorOrDtorName && !IsClassName && SS.isSet()) {
    DeclContext *LookupCtx = computeDeclContext(SS, /*EnteringContext*/false);

    // C++ [temp.res]p3:
    //   A qualified-id that refers to a type and in which the
    //   nested-name-specifier depends on a template-parameter (14.6.2)
    //   shall be prefixed by the keyword typename to indicate that the
    //   qualified-id denotes a type, forming an
    //   elaborated-type-specifier (7.1.5.3).
    if (!LookupCtx && isDependentScopeSpecifier(SS)) {
      // C++2a relaxes some of those restrictions in [temp.res]p5.
      if (AllowImplicitTypename == ImplicitTypenameContext::Yes) {
        if (getLangOpts().CPlusPlus20)
          Diag(SS.getBeginLoc(), diag::warn_cxx17_compat_implicit_typename);
        else
          Diag(SS.getBeginLoc(), diag::ext_implicit_typename)
              << SS.getScopeRep() << TemplateII->getName()
              << FixItHint::CreateInsertion(SS.getBeginLoc(), "typename ");
      } else
        Diag(SS.getBeginLoc(), diag::err_typename_missing_template)
            << SS.getScopeRep() << TemplateII->getName();

      // FIXME: This is not quite correct recovery as we don't transform SS
      // into the corresponding dependent form (and we don't diagnose missing
      // 'template' keywords within SS as a result).
      return ActOnTypenameType(nullptr, SourceLocation(), SS, TemplateKWLoc,
                               TemplateD, TemplateII, TemplateIILoc, LAngleLoc,
                               TemplateArgsIn, RAngleLoc);
    }

    // Per C++ [class.qual]p2, if the template-id was an injected-class-name,
    // it's not actually allowed to be used as a type in most cases. Because
    // we annotate it before we know whether it's valid, we have to check for
    // this case here.
    auto *LookupRD = dyn_cast_or_null<CXXRecordDecl>(LookupCtx);
    if (LookupRD && LookupRD->getIdentifier() == TemplateII) {
      Diag(TemplateIILoc,
           TemplateKWLoc.isInvalid()
               ? diag::err_out_of_line_qualified_id_type_names_constructor
               : diag::ext_out_of_line_qualified_id_type_names_constructor)
        << TemplateII << 0 /*injected-class-name used as template name*/
        << 1 /*if any keyword was present, it was 'template'*/;
    }
  }

  TemplateName Template = TemplateD.get();
  if (Template.getAsAssumedTemplateName() &&
      resolveAssumedTemplateNameAsType(S, Template, TemplateIILoc))
    return true;

  // Translate the parser's template argument list in our AST format.
  TemplateArgumentListInfo TemplateArgs(LAngleLoc, RAngleLoc);
  translateTemplateArguments(TemplateArgsIn, TemplateArgs);

  if (DependentTemplateName *DTN = Template.getAsDependentTemplateName()) {
    assert(SS.getScopeRep() == DTN->getQualifier());
    QualType T = Context.getDependentTemplateSpecializationType(
        ElaboratedTypeKeyword::None, DTN->getQualifier(), DTN->getIdentifier(),
        TemplateArgs.arguments());
    // Build type-source information.
    TypeLocBuilder TLB;
    DependentTemplateSpecializationTypeLoc SpecTL
      = TLB.push<DependentTemplateSpecializationTypeLoc>(T);
    SpecTL.setElaboratedKeywordLoc(SourceLocation());
    SpecTL.setQualifierLoc(SS.getWithLocInContext(Context));
    SpecTL.setTemplateKeywordLoc(TemplateKWLoc);
    SpecTL.setTemplateNameLoc(TemplateIILoc);
    SpecTL.setLAngleLoc(LAngleLoc);
    SpecTL.setRAngleLoc(RAngleLoc);
    for (unsigned I = 0, N = SpecTL.getNumArgs(); I != N; ++I)
      SpecTL.setArgLocInfo(I, TemplateArgs[I].getLocInfo());
    return CreateParsedType(T, TLB.getTypeSourceInfo(Context, T));
  }

  QualType SpecTy = CheckTemplateIdType(Template, TemplateIILoc, TemplateArgs);
  if (SpecTy.isNull())
    return true;

  // Build type-source information.
  TypeLocBuilder TLB;
  TemplateSpecializationTypeLoc SpecTL =
      TLB.push<TemplateSpecializationTypeLoc>(SpecTy);
  SpecTL.setTemplateKeywordLoc(TemplateKWLoc);
  SpecTL.setTemplateNameLoc(TemplateIILoc);
  SpecTL.setLAngleLoc(LAngleLoc);
  SpecTL.setRAngleLoc(RAngleLoc);
  for (unsigned i = 0, e = SpecTL.getNumArgs(); i != e; ++i)
    SpecTL.setArgLocInfo(i, TemplateArgs[i].getLocInfo());

  // Create an elaborated-type-specifier containing the nested-name-specifier.
  QualType ElTy =
      getElaboratedType(ElaboratedTypeKeyword::None,
                        !IsCtorOrDtorName ? SS : CXXScopeSpec(), SpecTy);
  ElaboratedTypeLoc ElabTL = TLB.push<ElaboratedTypeLoc>(ElTy);
  ElabTL.setElaboratedKeywordLoc(SourceLocation());
  if (!ElabTL.isEmpty())
    ElabTL.setQualifierLoc(SS.getWithLocInContext(Context));
  return CreateParsedType(ElTy, TLB.getTypeSourceInfo(Context, ElTy));
}

TypeResult Sema::ActOnTagTemplateIdType(TagUseKind TUK,
                                        TypeSpecifierType TagSpec,
                                        SourceLocation TagLoc,
                                        CXXScopeSpec &SS,
                                        SourceLocation TemplateKWLoc,
                                        TemplateTy TemplateD,
                                        SourceLocation TemplateLoc,
                                        SourceLocation LAngleLoc,
                                        ASTTemplateArgsPtr TemplateArgsIn,
                                        SourceLocation RAngleLoc) {
  if (SS.isInvalid())
    return TypeResult(true);

  TemplateName Template = TemplateD.get();

  // Translate the parser's template argument list in our AST format.
  TemplateArgumentListInfo TemplateArgs(LAngleLoc, RAngleLoc);
  translateTemplateArguments(TemplateArgsIn, TemplateArgs);

  // Determine the tag kind
  TagTypeKind TagKind = TypeWithKeyword::getTagTypeKindForTypeSpec(TagSpec);
  ElaboratedTypeKeyword Keyword
    = TypeWithKeyword::getKeywordForTagTypeKind(TagKind);

  if (DependentTemplateName *DTN = Template.getAsDependentTemplateName()) {
    assert(SS.getScopeRep() == DTN->getQualifier());
    QualType T = Context.getDependentTemplateSpecializationType(
        Keyword, DTN->getQualifier(), DTN->getIdentifier(),
        TemplateArgs.arguments());

    // Build type-source information.
    TypeLocBuilder TLB;
    DependentTemplateSpecializationTypeLoc SpecTL
      = TLB.push<DependentTemplateSpecializationTypeLoc>(T);
    SpecTL.setElaboratedKeywordLoc(TagLoc);
    SpecTL.setQualifierLoc(SS.getWithLocInContext(Context));
    SpecTL.setTemplateKeywordLoc(TemplateKWLoc);
    SpecTL.setTemplateNameLoc(TemplateLoc);
    SpecTL.setLAngleLoc(LAngleLoc);
    SpecTL.setRAngleLoc(RAngleLoc);
    for (unsigned I = 0, N = SpecTL.getNumArgs(); I != N; ++I)
      SpecTL.setArgLocInfo(I, TemplateArgs[I].getLocInfo());
    return CreateParsedType(T, TLB.getTypeSourceInfo(Context, T));
  }

  if (TypeAliasTemplateDecl *TAT =
        dyn_cast_or_null<TypeAliasTemplateDecl>(Template.getAsTemplateDecl())) {
    // C++0x [dcl.type.elab]p2:
    //   If the identifier resolves to a typedef-name or the simple-template-id
    //   resolves to an alias template specialization, the
    //   elaborated-type-specifier is ill-formed.
    Diag(TemplateLoc, diag::err_tag_reference_non_tag)
        << TAT << NTK_TypeAliasTemplate << llvm::to_underlying(TagKind);
    Diag(TAT->getLocation(), diag::note_declared_at);
  }

  QualType Result = CheckTemplateIdType(Template, TemplateLoc, TemplateArgs);
  if (Result.isNull())
    return TypeResult(true);

  // Check the tag kind
  if (const RecordType *RT = Result->getAs<RecordType>()) {
    RecordDecl *D = RT->getDecl();

    IdentifierInfo *Id = D->getIdentifier();
    assert(Id && "templated class must have an identifier");

    if (!isAcceptableTagRedeclaration(D, TagKind, TUK == TagUseKind::Definition,
                                      TagLoc, Id)) {
      Diag(TagLoc, diag::err_use_with_wrong_tag)
        << Result
        << FixItHint::CreateReplacement(SourceRange(TagLoc), D->getKindName());
      Diag(D->getLocation(), diag::note_previous_use);
    }
  }

  // Provide source-location information for the template specialization.
  TypeLocBuilder TLB;
  TemplateSpecializationTypeLoc SpecTL
    = TLB.push<TemplateSpecializationTypeLoc>(Result);
  SpecTL.setTemplateKeywordLoc(TemplateKWLoc);
  SpecTL.setTemplateNameLoc(TemplateLoc);
  SpecTL.setLAngleLoc(LAngleLoc);
  SpecTL.setRAngleLoc(RAngleLoc);
  for (unsigned i = 0, e = SpecTL.getNumArgs(); i != e; ++i)
    SpecTL.setArgLocInfo(i, TemplateArgs[i].getLocInfo());

  // Construct an elaborated type containing the nested-name-specifier (if any)
  // and tag keyword.
  Result = Context.getElaboratedType(Keyword, SS.getScopeRep(), Result);
  ElaboratedTypeLoc ElabTL = TLB.push<ElaboratedTypeLoc>(Result);
  ElabTL.setElaboratedKeywordLoc(TagLoc);
  ElabTL.setQualifierLoc(SS.getWithLocInContext(Context));
  return CreateParsedType(Result, TLB.getTypeSourceInfo(Context, Result));
}

static bool CheckTemplateSpecializationScope(Sema &S, NamedDecl *Specialized,
                                             NamedDecl *PrevDecl,
                                             SourceLocation Loc,
                                             bool IsPartialSpecialization);

static TemplateSpecializationKind getTemplateSpecializationKind(Decl *D);

static bool isTemplateArgumentTemplateParameter(
    const TemplateArgument &Arg, unsigned Depth, unsigned Index) {
  switch (Arg.getKind()) {
  case TemplateArgument::Null:
  case TemplateArgument::NullPtr:
  case TemplateArgument::Integral:
  case TemplateArgument::Declaration:
  case TemplateArgument::StructuralValue:
  case TemplateArgument::Pack:
  case TemplateArgument::TemplateExpansion:
    return false;

  case TemplateArgument::Type: {
    QualType Type = Arg.getAsType();
    const TemplateTypeParmType *TPT =
        Arg.getAsType()->getAs<TemplateTypeParmType>();
    return TPT && !Type.hasQualifiers() &&
           TPT->getDepth() == Depth && TPT->getIndex() == Index;
  }

  case TemplateArgument::Expression: {
    DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Arg.getAsExpr());
    if (!DRE || !DRE->getDecl())
      return false;
    const NonTypeTemplateParmDecl *NTTP =
        dyn_cast<NonTypeTemplateParmDecl>(DRE->getDecl());
    return NTTP && NTTP->getDepth() == Depth && NTTP->getIndex() == Index;
  }

  case TemplateArgument::Template:
    const TemplateTemplateParmDecl *TTP =
        dyn_cast_or_null<TemplateTemplateParmDecl>(
            Arg.getAsTemplateOrTemplatePattern().getAsTemplateDecl());
    return TTP && TTP->getDepth() == Depth && TTP->getIndex() == Index;
  }
  llvm_unreachable("unexpected kind of template argument");
}

static bool isSameAsPrimaryTemplate(TemplateParameterList *Params,
                                    ArrayRef<TemplateArgument> Args) {
  if (Params->size() != Args.size())
    return false;

  unsigned Depth = Params->getDepth();

  for (unsigned I = 0, N = Args.size(); I != N; ++I) {
    TemplateArgument Arg = Args[I];

    // If the parameter is a pack expansion, the argument must be a pack
    // whose only element is a pack expansion.
    if (Params->getParam(I)->isParameterPack()) {
      if (Arg.getKind() != TemplateArgument::Pack || Arg.pack_size() != 1 ||
          !Arg.pack_begin()->isPackExpansion())
        return false;
      Arg = Arg.pack_begin()->getPackExpansionPattern();
    }

    if (!isTemplateArgumentTemplateParameter(Arg, Depth, I))
      return false;
  }

  return true;
}

template<typename PartialSpecDecl>
static void checkMoreSpecializedThanPrimary(Sema &S, PartialSpecDecl *Partial) {
  if (Partial->getDeclContext()->isDependentContext())
    return;

  // FIXME: Get the TDK from deduction in order to provide better diagnostics
  // for non-substitution-failure issues?
  TemplateDeductionInfo Info(Partial->getLocation());
  if (S.isMoreSpecializedThanPrimary(Partial, Info))
    return;

  auto *Template = Partial->getSpecializedTemplate();
  S.Diag(Partial->getLocation(),
         diag::ext_partial_spec_not_more_specialized_than_primary)
      << isa<VarTemplateDecl>(Template);

  if (Info.hasSFINAEDiagnostic()) {
    PartialDiagnosticAt Diag = {SourceLocation(),
                                PartialDiagnostic::NullDiagnostic()};
    Info.takeSFINAEDiagnostic(Diag);
    SmallString<128> SFINAEArgString;
    Diag.second.EmitToString(S.getDiagnostics(), SFINAEArgString);
    S.Diag(Diag.first,
           diag::note_partial_spec_not_more_specialized_than_primary)
      << SFINAEArgString;
  }

  S.NoteTemplateLocation(*Template);
  SmallVector<const Expr *, 3> PartialAC, TemplateAC;
  Template->getAssociatedConstraints(TemplateAC);
  Partial->getAssociatedConstraints(PartialAC);
  S.MaybeEmitAmbiguousAtomicConstraintsDiagnostic(Partial, PartialAC, Template,
                                                  TemplateAC);
}

static void
noteNonDeducibleParameters(Sema &S, TemplateParameterList *TemplateParams,
                           const llvm::SmallBitVector &DeducibleParams) {
  for (unsigned I = 0, N = DeducibleParams.size(); I != N; ++I) {
    if (!DeducibleParams[I]) {
      NamedDecl *Param = TemplateParams->getParam(I);
      if (Param->getDeclName())
        S.Diag(Param->getLocation(), diag::note_non_deducible_parameter)
            << Param->getDeclName();
      else
        S.Diag(Param->getLocation(), diag::note_non_deducible_parameter)
            << "(anonymous)";
    }
  }
}


template<typename PartialSpecDecl>
static void checkTemplatePartialSpecialization(Sema &S,
                                               PartialSpecDecl *Partial) {
  // C++1z [temp.class.spec]p8: (DR1495)
  //   - The specialization shall be more specialized than the primary
  //     template (14.5.5.2).
  checkMoreSpecializedThanPrimary(S, Partial);

  // C++ [temp.class.spec]p8: (DR1315)
  //   - Each template-parameter shall appear at least once in the
  //     template-id outside a non-deduced context.
  // C++1z [temp.class.spec.match]p3 (P0127R2)
  //   If the template arguments of a partial specialization cannot be
  //   deduced because of the structure of its template-parameter-list
  //   and the template-id, the program is ill-formed.
  auto *TemplateParams = Partial->getTemplateParameters();
  llvm::SmallBitVector DeducibleParams(TemplateParams->size());
  S.MarkUsedTemplateParameters(Partial->getTemplateArgs(), true,
                               TemplateParams->getDepth(), DeducibleParams);

  if (!DeducibleParams.all()) {
    unsigned NumNonDeducible = DeducibleParams.size() - DeducibleParams.count();
    S.Diag(Partial->getLocation(), diag::ext_partial_specs_not_deducible)
      << isa<VarTemplatePartialSpecializationDecl>(Partial)
      << (NumNonDeducible > 1)
      << SourceRange(Partial->getLocation(),
                     Partial->getTemplateArgsAsWritten()->RAngleLoc);
    noteNonDeducibleParameters(S, TemplateParams, DeducibleParams);
  }
}

void Sema::CheckTemplatePartialSpecialization(
    ClassTemplatePartialSpecializationDecl *Partial) {
  checkTemplatePartialSpecialization(*this, Partial);
}

void Sema::CheckTemplatePartialSpecialization(
    VarTemplatePartialSpecializationDecl *Partial) {
  checkTemplatePartialSpecialization(*this, Partial);
}

void Sema::CheckDeductionGuideTemplate(FunctionTemplateDecl *TD) {
  // C++1z [temp.param]p11:
  //   A template parameter of a deduction guide template that does not have a
  //   default-argument shall be deducible from the parameter-type-list of the
  //   deduction guide template.
  auto *TemplateParams = TD->getTemplateParameters();
  llvm::SmallBitVector DeducibleParams(TemplateParams->size());
  MarkDeducedTemplateParameters(TD, DeducibleParams);
  for (unsigned I = 0; I != TemplateParams->size(); ++I) {
    // A parameter pack is deducible (to an empty pack).
    auto *Param = TemplateParams->getParam(I);
    if (Param->isParameterPack() || hasVisibleDefaultArgument(Param))
      DeducibleParams[I] = true;
  }

  if (!DeducibleParams.all()) {
    unsigned NumNonDeducible = DeducibleParams.size() - DeducibleParams.count();
    Diag(TD->getLocation(), diag::err_deduction_guide_template_not_deducible)
      << (NumNonDeducible > 1);
    noteNonDeducibleParameters(*this, TemplateParams, DeducibleParams);
  }
}

DeclResult Sema::ActOnVarTemplateSpecialization(
    Scope *S, Declarator &D, TypeSourceInfo *DI, LookupResult &Previous,
    SourceLocation TemplateKWLoc, TemplateParameterList *TemplateParams,
    StorageClass SC, bool IsPartialSpecialization) {
  // D must be variable template id.
  assert(D.getName().getKind() == UnqualifiedIdKind::IK_TemplateId &&
         "Variable template specialization is declared with a template id.");

  TemplateIdAnnotation *TemplateId = D.getName().TemplateId;
  TemplateArgumentListInfo TemplateArgs =
      makeTemplateArgumentListInfo(*this, *TemplateId);
  SourceLocation TemplateNameLoc = D.getIdentifierLoc();
  SourceLocation LAngleLoc = TemplateId->LAngleLoc;
  SourceLocation RAngleLoc = TemplateId->RAngleLoc;

  TemplateName Name = TemplateId->Template.get();

  // The template-id must name a variable template.
  VarTemplateDecl *VarTemplate =
      dyn_cast_or_null<VarTemplateDecl>(Name.getAsTemplateDecl());
  if (!VarTemplate) {
    NamedDecl *FnTemplate;
    if (auto *OTS = Name.getAsOverloadedTemplate())
      FnTemplate = *OTS->begin();
    else
      FnTemplate = dyn_cast_or_null<FunctionTemplateDecl>(Name.getAsTemplateDecl());
    if (FnTemplate)
      return Diag(D.getIdentifierLoc(), diag::err_var_spec_no_template_but_method)
               << FnTemplate->getDeclName();
    return Diag(D.getIdentifierLoc(), diag::err_var_spec_no_template)
             << IsPartialSpecialization;
  }

  // Check for unexpanded parameter packs in any of the template arguments.
  for (unsigned I = 0, N = TemplateArgs.size(); I != N; ++I)
    if (DiagnoseUnexpandedParameterPack(TemplateArgs[I],
                                        IsPartialSpecialization
                                            ? UPPC_PartialSpecialization
                                            : UPPC_ExplicitSpecialization))
      return true;

  // Check that the template argument list is well-formed for this
  // template.
  SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;
  if (CheckTemplateArgumentList(VarTemplate, TemplateNameLoc, TemplateArgs,
                                false, SugaredConverted, CanonicalConverted,
                                /*UpdateArgsWithConversions=*/true))
    return true;

  // Find the variable template (partial) specialization declaration that
  // corresponds to these arguments.
  if (IsPartialSpecialization) {
    if (CheckTemplatePartialSpecializationArgs(TemplateNameLoc, VarTemplate,
                                               TemplateArgs.size(),
                                               CanonicalConverted))
      return true;

    // FIXME: Move these checks to CheckTemplatePartialSpecializationArgs so we
    // also do them during instantiation.
    if (!Name.isDependent() &&
        !TemplateSpecializationType::anyDependentTemplateArguments(
            TemplateArgs, CanonicalConverted)) {
      Diag(TemplateNameLoc, diag::err_partial_spec_fully_specialized)
          << VarTemplate->getDeclName();
      IsPartialSpecialization = false;
    }

    if (isSameAsPrimaryTemplate(VarTemplate->getTemplateParameters(),
                                CanonicalConverted) &&
        (!Context.getLangOpts().CPlusPlus20 ||
         !TemplateParams->hasAssociatedConstraints())) {
      // C++ [temp.class.spec]p9b3:
      //
      //   -- The argument list of the specialization shall not be identical
      //      to the implicit argument list of the primary template.
      Diag(TemplateNameLoc, diag::err_partial_spec_args_match_primary_template)
        << /*variable template*/ 1
        << /*is definition*/(SC != SC_Extern && !CurContext->isRecord())
        << FixItHint::CreateRemoval(SourceRange(LAngleLoc, RAngleLoc));
      // FIXME: Recover from this by treating the declaration as a redeclaration
      // of the primary template.
      return true;
    }
  }

  void *InsertPos = nullptr;
  VarTemplateSpecializationDecl *PrevDecl = nullptr;

  if (IsPartialSpecialization)
    PrevDecl = VarTemplate->findPartialSpecialization(
        CanonicalConverted, TemplateParams, InsertPos);
  else
    PrevDecl = VarTemplate->findSpecialization(CanonicalConverted, InsertPos);

  VarTemplateSpecializationDecl *Specialization = nullptr;

  // Check whether we can declare a variable template specialization in
  // the current scope.
  if (CheckTemplateSpecializationScope(*this, VarTemplate, PrevDecl,
                                       TemplateNameLoc,
                                       IsPartialSpecialization))
    return true;

  if (PrevDecl && PrevDecl->getSpecializationKind() == TSK_Undeclared) {
    // Since the only prior variable template specialization with these
    // arguments was referenced but not declared,  reuse that
    // declaration node as our own, updating its source location and
    // the list of outer template parameters to reflect our new declaration.
    Specialization = PrevDecl;
    Specialization->setLocation(TemplateNameLoc);
    PrevDecl = nullptr;
  } else if (IsPartialSpecialization) {
    // Create a new class template partial specialization declaration node.
    VarTemplatePartialSpecializationDecl *PrevPartial =
        cast_or_null<VarTemplatePartialSpecializationDecl>(PrevDecl);
    VarTemplatePartialSpecializationDecl *Partial =
        VarTemplatePartialSpecializationDecl::Create(
            Context, VarTemplate->getDeclContext(), TemplateKWLoc,
            TemplateNameLoc, TemplateParams, VarTemplate, DI->getType(), DI, SC,
            CanonicalConverted);
    Partial->setTemplateArgsAsWritten(TemplateArgs);

    if (!PrevPartial)
      VarTemplate->AddPartialSpecialization(Partial, InsertPos);
    Specialization = Partial;

    // If we are providing an explicit specialization of a member variable
    // template specialization, make a note of that.
    if (PrevPartial && PrevPartial->getInstantiatedFromMember())
      PrevPartial->setMemberSpecialization();

    CheckTemplatePartialSpecialization(Partial);
  } else {
    // Create a new class template specialization declaration node for
    // this explicit specialization or friend declaration.
    Specialization = VarTemplateSpecializationDecl::Create(
        Context, VarTemplate->getDeclContext(), TemplateKWLoc, TemplateNameLoc,
        VarTemplate, DI->getType(), DI, SC, CanonicalConverted);
    Specialization->setTemplateArgsAsWritten(TemplateArgs);

    if (!PrevDecl)
      VarTemplate->AddSpecialization(Specialization, InsertPos);
  }

  // C++ [temp.expl.spec]p6:
  //   If a template, a member template or the member of a class template is
  //   explicitly specialized then that specialization shall be declared
  //   before the first use of that specialization that would cause an implicit
  //   instantiation to take place, in every translation unit in which such a
  //   use occurs; no diagnostic is required.
  if (PrevDecl && PrevDecl->getPointOfInstantiation().isValid()) {
    bool Okay = false;
    for (Decl *Prev = PrevDecl; Prev; Prev = Prev->getPreviousDecl()) {
      // Is there any previous explicit specialization declaration?
      if (getTemplateSpecializationKind(Prev) == TSK_ExplicitSpecialization) {
        Okay = true;
        break;
      }
    }

    if (!Okay) {
      SourceRange Range(TemplateNameLoc, RAngleLoc);
      Diag(TemplateNameLoc, diag::err_specialization_after_instantiation)
          << Name << Range;

      Diag(PrevDecl->getPointOfInstantiation(),
           diag::note_instantiation_required_here)
          << (PrevDecl->getTemplateSpecializationKind() !=
              TSK_ImplicitInstantiation);
      return true;
    }
  }

  Specialization->setLexicalDeclContext(CurContext);

  // Add the specialization into its lexical context, so that it can
  // be seen when iterating through the list of declarations in that
  // context. However, specializations are not found by name lookup.
  CurContext->addDecl(Specialization);

  // Note that this is an explicit specialization.
  Specialization->setSpecializationKind(TSK_ExplicitSpecialization);

  Previous.clear();
  if (PrevDecl)
    Previous.addDecl(PrevDecl);
  else if (Specialization->isStaticDataMember() &&
           Specialization->isOutOfLine())
    Specialization->setAccess(VarTemplate->getAccess());

  return Specialization;
}

namespace {
/// A partial specialization whose template arguments have matched
/// a given template-id.
struct PartialSpecMatchResult {
  VarTemplatePartialSpecializationDecl *Partial;
  TemplateArgumentList *Args;
};
} // end anonymous namespace

DeclResult
Sema::CheckVarTemplateId(VarTemplateDecl *Template, SourceLocation TemplateLoc,
                         SourceLocation TemplateNameLoc,
                         const TemplateArgumentListInfo &TemplateArgs) {
  assert(Template && "A variable template id without template?");

  // Check that the template argument list is well-formed for this template.
  SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;
  if (CheckTemplateArgumentList(
          Template, TemplateNameLoc,
          const_cast<TemplateArgumentListInfo &>(TemplateArgs), false,
          SugaredConverted, CanonicalConverted,
          /*UpdateArgsWithConversions=*/true))
    return true;

  // Produce a placeholder value if the specialization is dependent.
  if (Template->getDeclContext()->isDependentContext() ||
      TemplateSpecializationType::anyDependentTemplateArguments(
          TemplateArgs, CanonicalConverted))
    return DeclResult();

  // Find the variable template specialization declaration that
  // corresponds to these arguments.
  void *InsertPos = nullptr;
  if (VarTemplateSpecializationDecl *Spec =
          Template->findSpecialization(CanonicalConverted, InsertPos)) {
    checkSpecializationReachability(TemplateNameLoc, Spec);
    // If we already have a variable template specialization, return it.
    return Spec;
  }

  // This is the first time we have referenced this variable template
  // specialization. Create the canonical declaration and add it to
  // the set of specializations, based on the closest partial specialization
  // that it represents. That is,
  VarDecl *InstantiationPattern = Template->getTemplatedDecl();
  const TemplateArgumentList *PartialSpecArgs = nullptr;
  bool AmbiguousPartialSpec = false;
  typedef PartialSpecMatchResult MatchResult;
  SmallVector<MatchResult, 4> Matched;
  SourceLocation PointOfInstantiation = TemplateNameLoc;
  TemplateSpecCandidateSet FailedCandidates(PointOfInstantiation,
                                            /*ForTakingAddress=*/false);

  // 1. Attempt to find the closest partial specialization that this
  // specializes, if any.
  // TODO: Unify with InstantiateClassTemplateSpecialization()?
  //       Perhaps better after unification of DeduceTemplateArguments() and
  //       getMoreSpecializedPartialSpecialization().
  SmallVector<VarTemplatePartialSpecializationDecl *, 4> PartialSpecs;
  Template->getPartialSpecializations(PartialSpecs);

  for (unsigned I = 0, N = PartialSpecs.size(); I != N; ++I) {
    VarTemplatePartialSpecializationDecl *Partial = PartialSpecs[I];
    TemplateDeductionInfo Info(FailedCandidates.getLocation());

    if (TemplateDeductionResult Result =
            DeduceTemplateArguments(Partial, CanonicalConverted, Info);
        Result != TemplateDeductionResult::Success) {
      // Store the failed-deduction information for use in diagnostics, later.
      // TODO: Actually use the failed-deduction info?
      FailedCandidates.addCandidate().set(
          DeclAccessPair::make(Template, AS_public), Partial,
          MakeDeductionFailureInfo(Context, Result, Info));
      (void)Result;
    } else {
      Matched.push_back(PartialSpecMatchResult());
      Matched.back().Partial = Partial;
      Matched.back().Args = Info.takeCanonical();
    }
  }

  if (Matched.size() >= 1) {
    SmallVector<MatchResult, 4>::iterator Best = Matched.begin();
    if (Matched.size() == 1) {
      //   -- If exactly one matching specialization is found, the
      //      instantiation is generated from that specialization.
      // We don't need to do anything for this.
    } else {
      //   -- If more than one matching specialization is found, the
      //      partial order rules (14.5.4.2) are used to determine
      //      whether one of the specializations is more specialized
      //      than the others. If none of the specializations is more
      //      specialized than all of the other matching
      //      specializations, then the use of the variable template is
      //      ambiguous and the program is ill-formed.
      for (SmallVector<MatchResult, 4>::iterator P = Best + 1,
                                                 PEnd = Matched.end();
           P != PEnd; ++P) {
        if (getMoreSpecializedPartialSpecialization(P->Partial, Best->Partial,
                                                    PointOfInstantiation) ==
            P->Partial)
          Best = P;
      }

      // Determine if the best partial specialization is more specialized than
      // the others.
      for (SmallVector<MatchResult, 4>::iterator P = Matched.begin(),
                                                 PEnd = Matched.end();
           P != PEnd; ++P) {
        if (P != Best && getMoreSpecializedPartialSpecialization(
                             P->Partial, Best->Partial,
                             PointOfInstantiation) != Best->Partial) {
          AmbiguousPartialSpec = true;
          break;
        }
      }
    }

    // Instantiate using the best variable template partial specialization.
    InstantiationPattern = Best->Partial;
    PartialSpecArgs = Best->Args;
  } else {
    //   -- If no match is found, the instantiation is generated
    //      from the primary template.
    // InstantiationPattern = Template->getTemplatedDecl();
  }

  // 2. Create the canonical declaration.
  // Note that we do not instantiate a definition until we see an odr-use
  // in DoMarkVarDeclReferenced().
  // FIXME: LateAttrs et al.?
  VarTemplateSpecializationDecl *Decl = BuildVarTemplateInstantiation(
      Template, InstantiationPattern, PartialSpecArgs, TemplateArgs,
      CanonicalConverted, TemplateNameLoc /*, LateAttrs, StartingScope*/);
  if (!Decl)
    return true;

  if (AmbiguousPartialSpec) {
    // Partial ordering did not produce a clear winner. Complain.
    Decl->setInvalidDecl();
    Diag(PointOfInstantiation, diag::err_partial_spec_ordering_ambiguous)
        << Decl;

    // Print the matching partial specializations.
    for (MatchResult P : Matched)
      Diag(P.Partial->getLocation(), diag::note_partial_spec_match)
          << getTemplateArgumentBindingsText(P.Partial->getTemplateParameters(),
                                             *P.Args);
    return true;
  }

  if (VarTemplatePartialSpecializationDecl *D =
          dyn_cast<VarTemplatePartialSpecializationDecl>(InstantiationPattern))
    Decl->setInstantiationOf(D, PartialSpecArgs);

  checkSpecializationReachability(TemplateNameLoc, Decl);

  assert(Decl && "No variable template specialization?");
  return Decl;
}

ExprResult Sema::CheckVarTemplateId(
    const CXXScopeSpec &SS, const DeclarationNameInfo &NameInfo,
    VarTemplateDecl *Template, NamedDecl *FoundD, SourceLocation TemplateLoc,
    const TemplateArgumentListInfo *TemplateArgs) {

  DeclResult Decl = CheckVarTemplateId(Template, TemplateLoc, NameInfo.getLoc(),
                                       *TemplateArgs);
  if (Decl.isInvalid())
    return ExprError();

  if (!Decl.get())
    return ExprResult();

  VarDecl *Var = cast<VarDecl>(Decl.get());
  if (!Var->getTemplateSpecializationKind())
    Var->setTemplateSpecializationKind(TSK_ImplicitInstantiation,
                                       NameInfo.getLoc());

  // Build an ordinary singleton decl ref.
  return BuildDeclarationNameExpr(SS, NameInfo, Var, FoundD, TemplateArgs);
}

void Sema::diagnoseMissingTemplateArguments(TemplateName Name,
                                            SourceLocation Loc) {
  Diag(Loc, diag::err_template_missing_args)
    << (int)getTemplateNameKindForDiagnostics(Name) << Name;
  if (TemplateDecl *TD = Name.getAsTemplateDecl()) {
    NoteTemplateLocation(*TD, TD->getTemplateParameters()->getSourceRange());
  }
}

void Sema::diagnoseMissingTemplateArguments(const CXXScopeSpec &SS,
                                            bool TemplateKeyword,
                                            TemplateDecl *TD,
                                            SourceLocation Loc) {
  TemplateName Name = Context.getQualifiedTemplateName(
      SS.getScopeRep(), TemplateKeyword, TemplateName(TD));
  diagnoseMissingTemplateArguments(Name, Loc);
}

ExprResult
Sema::CheckConceptTemplateId(const CXXScopeSpec &SS,
                             SourceLocation TemplateKWLoc,
                             const DeclarationNameInfo &ConceptNameInfo,
                             NamedDecl *FoundDecl,
                             ConceptDecl *NamedConcept,
                             const TemplateArgumentListInfo *TemplateArgs) {
  assert(NamedConcept && "A concept template id without a template?");

  llvm::SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;
  if (CheckTemplateArgumentList(
          NamedConcept, ConceptNameInfo.getLoc(),
          const_cast<TemplateArgumentListInfo &>(*TemplateArgs),
          /*PartialTemplateArgs=*/false, SugaredConverted, CanonicalConverted,
          /*UpdateArgsWithConversions=*/false))
    return ExprError();

  DiagnoseUseOfDecl(NamedConcept, ConceptNameInfo.getLoc());

  auto *CSD = ImplicitConceptSpecializationDecl::Create(
      Context, NamedConcept->getDeclContext(), NamedConcept->getLocation(),
      CanonicalConverted);
  ConstraintSatisfaction Satisfaction;
  bool AreArgsDependent =
      TemplateSpecializationType::anyDependentTemplateArguments(
          *TemplateArgs, CanonicalConverted);
  MultiLevelTemplateArgumentList MLTAL(NamedConcept, CanonicalConverted,
                                       /*Final=*/false);
  LocalInstantiationScope Scope(*this);

  EnterExpressionEvaluationContext EECtx{
      *this, ExpressionEvaluationContext::Unevaluated, CSD};

  if (!AreArgsDependent &&
      CheckConstraintSatisfaction(
          NamedConcept, {NamedConcept->getConstraintExpr()}, MLTAL,
          SourceRange(SS.isSet() ? SS.getBeginLoc() : ConceptNameInfo.getLoc(),
                      TemplateArgs->getRAngleLoc()),
          Satisfaction))
    return ExprError();
  auto *CL = ConceptReference::Create(
      Context,
      SS.isSet() ? SS.getWithLocInContext(Context) : NestedNameSpecifierLoc{},
      TemplateKWLoc, ConceptNameInfo, FoundDecl, NamedConcept,
      ASTTemplateArgumentListInfo::Create(Context, *TemplateArgs));
  return ConceptSpecializationExpr::Create(
      Context, CL, CSD, AreArgsDependent ? nullptr : &Satisfaction);
}

ExprResult Sema::BuildTemplateIdExpr(const CXXScopeSpec &SS,
                                     SourceLocation TemplateKWLoc,
                                     LookupResult &R,
                                     bool RequiresADL,
                                 const TemplateArgumentListInfo *TemplateArgs) {
  // FIXME: Can we do any checking at this point? I guess we could check the
  // template arguments that we have against the template name, if the template
  // name refers to a single template. That's not a terribly common case,
  // though.
  // foo<int> could identify a single function unambiguously
  // This approach does NOT work, since f<int>(1);
  // gets resolved prior to resorting to overload resolution
  // i.e., template<class T> void f(double);
  //       vs template<class T, class U> void f(U);

  // These should be filtered out by our callers.
  assert(!R.isAmbiguous() && "ambiguous lookup when building templateid");

  // Non-function templates require a template argument list.
  if (auto *TD = R.getAsSingle<TemplateDecl>()) {
    if (!TemplateArgs && !isa<FunctionTemplateDecl>(TD)) {
      diagnoseMissingTemplateArguments(
          SS, /*TemplateKeyword=*/TemplateKWLoc.isValid(), TD, R.getNameLoc());
      return ExprError();
    }
  }
  bool KnownDependent = false;
  // In C++1y, check variable template ids.
  if (R.getAsSingle<VarTemplateDecl>()) {
    ExprResult Res = CheckVarTemplateId(
        SS, R.getLookupNameInfo(), R.getAsSingle<VarTemplateDecl>(),
        R.getRepresentativeDecl(), TemplateKWLoc, TemplateArgs);
    if (Res.isInvalid() || Res.isUsable())
      return Res;
    // Result is dependent. Carry on to build an UnresolvedLookupExpr.
    KnownDependent = true;
  }

  if (R.getAsSingle<ConceptDecl>()) {
    return CheckConceptTemplateId(SS, TemplateKWLoc, R.getLookupNameInfo(),
                                  R.getRepresentativeDecl(),
                                  R.getAsSingle<ConceptDecl>(), TemplateArgs);
  }

  // We don't want lookup warnings at this point.
  R.suppressDiagnostics();

  UnresolvedLookupExpr *ULE = UnresolvedLookupExpr::Create(
      Context, R.getNamingClass(), SS.getWithLocInContext(Context),
      TemplateKWLoc, R.getLookupNameInfo(), RequiresADL, TemplateArgs,
      R.begin(), R.end(), KnownDependent,
      /*KnownInstantiationDependent=*/false);

  // Model the templates with UnresolvedTemplateTy. The expression should then
  // either be transformed in an instantiation or be diagnosed in
  // CheckPlaceholderExpr.
  if (ULE->getType() == Context.OverloadTy && R.isSingleResult() &&
      !R.getFoundDecl()->getAsFunction())
    ULE->setType(Context.UnresolvedTemplateTy);

  return ULE;
}

ExprResult Sema::BuildQualifiedTemplateIdExpr(
    CXXScopeSpec &SS, SourceLocation TemplateKWLoc,
    const DeclarationNameInfo &NameInfo,
    const TemplateArgumentListInfo *TemplateArgs, bool IsAddressOfOperand) {
  assert(TemplateArgs || TemplateKWLoc.isValid());

  LookupResult R(*this, NameInfo, LookupOrdinaryName);
  if (LookupTemplateName(R, /*S=*/nullptr, SS, /*ObjectType=*/QualType(),
                         /*EnteringContext=*/false, TemplateKWLoc))
    return ExprError();

  if (R.isAmbiguous())
    return ExprError();

  if (R.wasNotFoundInCurrentInstantiation() || SS.isInvalid())
    return BuildDependentDeclRefExpr(SS, TemplateKWLoc, NameInfo, TemplateArgs);

  if (R.empty()) {
    DeclContext *DC = computeDeclContext(SS);
    Diag(NameInfo.getLoc(), diag::err_no_member)
      << NameInfo.getName() << DC << SS.getRange();
    return ExprError();
  }

  // If necessary, build an implicit class member access.
  if (isPotentialImplicitMemberAccess(SS, R, IsAddressOfOperand))
    return BuildPossibleImplicitMemberExpr(SS, TemplateKWLoc, R, TemplateArgs,
                                           /*S=*/nullptr);

  return BuildTemplateIdExpr(SS, TemplateKWLoc, R, /*ADL=*/false, TemplateArgs);
}

TemplateNameKind Sema::ActOnTemplateName(Scope *S,
                                         CXXScopeSpec &SS,
                                         SourceLocation TemplateKWLoc,
                                         const UnqualifiedId &Name,
                                         ParsedType ObjectType,
                                         bool EnteringContext,
                                         TemplateTy &Result,
                                         bool AllowInjectedClassName) {
  if (TemplateKWLoc.isValid() && S && !S->getTemplateParamParent())
    Diag(TemplateKWLoc,
         getLangOpts().CPlusPlus11 ?
           diag::warn_cxx98_compat_template_outside_of_template :
           diag::ext_template_outside_of_template)
      << FixItHint::CreateRemoval(TemplateKWLoc);

  if (SS.isInvalid())
    return TNK_Non_template;

  // Figure out where isTemplateName is going to look.
  DeclContext *LookupCtx = nullptr;
  if (SS.isNotEmpty())
    LookupCtx = computeDeclContext(SS, EnteringContext);
  else if (ObjectType)
    LookupCtx = computeDeclContext(GetTypeFromParser(ObjectType));

  // C++0x [temp.names]p5:
  //   If a name prefixed by the keyword template is not the name of
  //   a template, the program is ill-formed. [Note: the keyword
  //   template may not be applied to non-template members of class
  //   templates. -end note ] [ Note: as is the case with the
  //   typename prefix, the template prefix is allowed in cases
  //   where it is not strictly necessary; i.e., when the
  //   nested-name-specifier or the expression on the left of the ->
  //   or . is not dependent on a template-parameter, or the use
  //   does not appear in the scope of a template. -end note]
  //
  // Note: C++03 was more strict here, because it banned the use of
  // the "template" keyword prior to a template-name that was not a
  // dependent name. C++ DR468 relaxed this requirement (the
  // "template" keyword is now permitted). We follow the C++0x
  // rules, even in C++03 mode with a warning, retroactively applying the DR.
  bool MemberOfUnknownSpecialization;
  TemplateNameKind TNK = isTemplateName(S, SS, TemplateKWLoc.isValid(), Name,
                                        ObjectType, EnteringContext, Result,
                                        MemberOfUnknownSpecialization);
  if (TNK != TNK_Non_template) {
    // We resolved this to a (non-dependent) template name. Return it.
    auto *LookupRD = dyn_cast_or_null<CXXRecordDecl>(LookupCtx);
    if (!AllowInjectedClassName && SS.isNotEmpty() && LookupRD &&
        Name.getKind() == UnqualifiedIdKind::IK_Identifier &&
        Name.Identifier && LookupRD->getIdentifier() == Name.Identifier) {
      // C++14 [class.qual]p2:
      //   In a lookup in which function names are not ignored and the
      //   nested-name-specifier nominates a class C, if the name specified
      //   [...] is the injected-class-name of C, [...] the name is instead
      //   considered to name the constructor
      //
      // We don't get here if naming the constructor would be valid, so we
      // just reject immediately and recover by treating the
      // injected-class-name as naming the template.
      Diag(Name.getBeginLoc(),
           diag::ext_out_of_line_qualified_id_type_names_constructor)
          << Name.Identifier
          << 0 /*injected-class-name used as template name*/
          << TemplateKWLoc.isValid();
    }
    return TNK;
  }

  if (!MemberOfUnknownSpecialization) {
    // Didn't find a template name, and the lookup wasn't dependent.
    // Do the lookup again to determine if this is a "nothing found" case or
    // a "not a template" case. FIXME: Refactor isTemplateName so we don't
    // need to do this.
    DeclarationNameInfo DNI = GetNameFromUnqualifiedId(Name);
    LookupResult R(*this, DNI.getName(), Name.getBeginLoc(),
                   LookupOrdinaryName);
    // Tell LookupTemplateName that we require a template so that it diagnoses
    // cases where it finds a non-template.
    RequiredTemplateKind RTK = TemplateKWLoc.isValid()
                                   ? RequiredTemplateKind(TemplateKWLoc)
                                   : TemplateNameIsRequired;
    if (!LookupTemplateName(R, S, SS, ObjectType.get(), EnteringContext, RTK,
                            /*ATK=*/nullptr, /*AllowTypoCorrection=*/false) &&
        !R.isAmbiguous()) {
      if (LookupCtx)
        Diag(Name.getBeginLoc(), diag::err_no_member)
            << DNI.getName() << LookupCtx << SS.getRange();
      else
        Diag(Name.getBeginLoc(), diag::err_undeclared_use)
            << DNI.getName() << SS.getRange();
    }
    return TNK_Non_template;
  }

  NestedNameSpecifier *Qualifier = SS.getScopeRep();

  switch (Name.getKind()) {
  case UnqualifiedIdKind::IK_Identifier:
    Result = TemplateTy::make(
        Context.getDependentTemplateName(Qualifier, Name.Identifier));
    return TNK_Dependent_template_name;

  case UnqualifiedIdKind::IK_OperatorFunctionId:
    Result = TemplateTy::make(Context.getDependentTemplateName(
        Qualifier, Name.OperatorFunctionId.Operator));
    return TNK_Function_template;

  case UnqualifiedIdKind::IK_LiteralOperatorId:
    // This is a kind of template name, but can never occur in a dependent
    // scope (literal operators can only be declared at namespace scope).
    break;

  default:
    break;
  }

  // This name cannot possibly name a dependent template. Diagnose this now
  // rather than building a dependent template name that can never be valid.
  Diag(Name.getBeginLoc(),
       diag::err_template_kw_refers_to_dependent_non_template)
      << GetNameFromUnqualifiedId(Name).getName() << Name.getSourceRange()
      << TemplateKWLoc.isValid() << TemplateKWLoc;
  return TNK_Non_template;
}

bool Sema::CheckTemplateTypeArgument(
    TemplateTypeParmDecl *Param, TemplateArgumentLoc &AL,
    SmallVectorImpl<TemplateArgument> &SugaredConverted,
    SmallVectorImpl<TemplateArgument> &CanonicalConverted) {
  const TemplateArgument &Arg = AL.getArgument();
  QualType ArgType;
  TypeSourceInfo *TSI = nullptr;

  // Check template type parameter.
  switch(Arg.getKind()) {
  case TemplateArgument::Type:
    // C++ [temp.arg.type]p1:
    //   A template-argument for a template-parameter which is a
    //   type shall be a type-id.
    ArgType = Arg.getAsType();
    TSI = AL.getTypeSourceInfo();
    break;
  case TemplateArgument::Template:
  case TemplateArgument::TemplateExpansion: {
    // We have a template type parameter but the template argument
    // is a template without any arguments.
    SourceRange SR = AL.getSourceRange();
    TemplateName Name = Arg.getAsTemplateOrTemplatePattern();
    diagnoseMissingTemplateArguments(Name, SR.getEnd());
    return true;
  }
  case TemplateArgument::Expression: {
    // We have a template type parameter but the template argument is an
    // expression; see if maybe it is missing the "typename" keyword.
    CXXScopeSpec SS;
    DeclarationNameInfo NameInfo;

   if (DependentScopeDeclRefExpr *ArgExpr =
               dyn_cast<DependentScopeDeclRefExpr>(Arg.getAsExpr())) {
      SS.Adopt(ArgExpr->getQualifierLoc());
      NameInfo = ArgExpr->getNameInfo();
    } else if (CXXDependentScopeMemberExpr *ArgExpr =
               dyn_cast<CXXDependentScopeMemberExpr>(Arg.getAsExpr())) {
      if (ArgExpr->isImplicitAccess()) {
        SS.Adopt(ArgExpr->getQualifierLoc());
        NameInfo = ArgExpr->getMemberNameInfo();
      }
    }

    if (auto *II = NameInfo.getName().getAsIdentifierInfo()) {
      LookupResult Result(*this, NameInfo, LookupOrdinaryName);
      LookupParsedName(Result, CurScope, &SS, /*ObjectType=*/QualType());

      if (Result.getAsSingle<TypeDecl>() ||
          Result.wasNotFoundInCurrentInstantiation()) {
        assert(SS.getScopeRep() && "dependent scope expr must has a scope!");
        // Suggest that the user add 'typename' before the NNS.
        SourceLocation Loc = AL.getSourceRange().getBegin();
        Diag(Loc, getLangOpts().MSVCCompat
                      ? diag::ext_ms_template_type_arg_missing_typename
                      : diag::err_template_arg_must_be_type_suggest)
            << FixItHint::CreateInsertion(Loc, "typename ");
        NoteTemplateParameterLocation(*Param);

        // Recover by synthesizing a type using the location information that we
        // already have.
        ArgType = Context.getDependentNameType(ElaboratedTypeKeyword::Typename,
                                               SS.getScopeRep(), II);
        TypeLocBuilder TLB;
        DependentNameTypeLoc TL = TLB.push<DependentNameTypeLoc>(ArgType);
        TL.setElaboratedKeywordLoc(SourceLocation(/*synthesized*/));
        TL.setQualifierLoc(SS.getWithLocInContext(Context));
        TL.setNameLoc(NameInfo.getLoc());
        TSI = TLB.getTypeSourceInfo(Context, ArgType);

        // Overwrite our input TemplateArgumentLoc so that we can recover
        // properly.
        AL = TemplateArgumentLoc(TemplateArgument(ArgType),
                                 TemplateArgumentLocInfo(TSI));

        break;
      }
    }
    // fallthrough
    [[fallthrough]];
  }
  default: {
    // We allow instantiateing a template with template argument packs when
    // building deduction guides.
    if (Arg.getKind() == TemplateArgument::Pack &&
        CodeSynthesisContexts.back().Kind ==
            Sema::CodeSynthesisContext::BuildingDeductionGuides) {
      SugaredConverted.push_back(Arg);
      CanonicalConverted.push_back(Arg);
      return false;
    }
    // We have a template type parameter but the template argument
    // is not a type.
    SourceRange SR = AL.getSourceRange();
    Diag(SR.getBegin(), diag::err_template_arg_must_be_type) << SR;
    NoteTemplateParameterLocation(*Param);

    return true;
  }
  }

  if (CheckTemplateArgument(TSI))
    return true;

  // Objective-C ARC:
  //   If an explicitly-specified template argument type is a lifetime type
  //   with no lifetime qualifier, the __strong lifetime qualifier is inferred.
  if (getLangOpts().ObjCAutoRefCount &&
      ArgType->isObjCLifetimeType() &&
      !ArgType.getObjCLifetime()) {
    Qualifiers Qs;
    Qs.setObjCLifetime(Qualifiers::OCL_Strong);
    ArgType = Context.getQualifiedType(ArgType, Qs);
  }

  SugaredConverted.push_back(TemplateArgument(ArgType));
  CanonicalConverted.push_back(
      TemplateArgument(Context.getCanonicalType(ArgType)));
  return false;
}

/// Substitute template arguments into the default template argument for
/// the given template type parameter.
///
/// \param SemaRef the semantic analysis object for which we are performing
/// the substitution.
///
/// \param Template the template that we are synthesizing template arguments
/// for.
///
/// \param TemplateLoc the location of the template name that started the
/// template-id we are checking.
///
/// \param RAngleLoc the location of the right angle bracket ('>') that
/// terminates the template-id.
///
/// \param Param the template template parameter whose default we are
/// substituting into.
///
/// \param Converted the list of template arguments provided for template
/// parameters that precede \p Param in the template parameter list.
///
/// \param Output the resulting substituted template argument.
///
/// \returns true if an error occurred.
static bool SubstDefaultTemplateArgument(
    Sema &SemaRef, TemplateDecl *Template, SourceLocation TemplateLoc,
    SourceLocation RAngleLoc, TemplateTypeParmDecl *Param,
    ArrayRef<TemplateArgument> SugaredConverted,
    ArrayRef<TemplateArgument> CanonicalConverted,
    TemplateArgumentLoc &Output) {
  Output = Param->getDefaultArgument();

  // If the argument type is dependent, instantiate it now based
  // on the previously-computed template arguments.
  if (Output.getArgument().isInstantiationDependent()) {
    Sema::InstantiatingTemplate Inst(SemaRef, TemplateLoc, Param, Template,
                                     SugaredConverted,
                                     SourceRange(TemplateLoc, RAngleLoc));
    if (Inst.isInvalid())
      return true;

    // Only substitute for the innermost template argument list.
    MultiLevelTemplateArgumentList TemplateArgLists(Template, SugaredConverted,
                                                    /*Final=*/true);
    for (unsigned i = 0, e = Param->getDepth(); i != e; ++i)
      TemplateArgLists.addOuterTemplateArguments(std::nullopt);

    bool ForLambdaCallOperator = false;
    if (const auto *Rec = dyn_cast<CXXRecordDecl>(Template->getDeclContext()))
      ForLambdaCallOperator = Rec->isLambda();
    Sema::ContextRAII SavedContext(SemaRef, Template->getDeclContext(),
                                   !ForLambdaCallOperator);

    if (SemaRef.SubstTemplateArgument(Output, TemplateArgLists, Output,
                                      Param->getDefaultArgumentLoc(),
                                      Param->getDeclName()))
      return true;
  }

  return false;
}

/// Substitute template arguments into the default template argument for
/// the given non-type template parameter.
///
/// \param SemaRef the semantic analysis object for which we are performing
/// the substitution.
///
/// \param Template the template that we are synthesizing template arguments
/// for.
///
/// \param TemplateLoc the location of the template name that started the
/// template-id we are checking.
///
/// \param RAngleLoc the location of the right angle bracket ('>') that
/// terminates the template-id.
///
/// \param Param the non-type template parameter whose default we are
/// substituting into.
///
/// \param Converted the list of template arguments provided for template
/// parameters that precede \p Param in the template parameter list.
///
/// \returns the substituted template argument, or NULL if an error occurred.
static bool SubstDefaultTemplateArgument(
    Sema &SemaRef, TemplateDecl *Template, SourceLocation TemplateLoc,
    SourceLocation RAngleLoc, NonTypeTemplateParmDecl *Param,
    ArrayRef<TemplateArgument> SugaredConverted,
    ArrayRef<TemplateArgument> CanonicalConverted,
    TemplateArgumentLoc &Output) {
  Sema::InstantiatingTemplate Inst(SemaRef, TemplateLoc, Param, Template,
                                   SugaredConverted,
                                   SourceRange(TemplateLoc, RAngleLoc));
  if (Inst.isInvalid())
    return true;

  // Only substitute for the innermost template argument list.
  MultiLevelTemplateArgumentList TemplateArgLists(Template, SugaredConverted,
                                                  /*Final=*/true);
  for (unsigned i = 0, e = Param->getDepth(); i != e; ++i)
    TemplateArgLists.addOuterTemplateArguments(std::nullopt);

  Sema::ContextRAII SavedContext(SemaRef, Template->getDeclContext());
  EnterExpressionEvaluationContext ConstantEvaluated(
      SemaRef, Sema::ExpressionEvaluationContext::ConstantEvaluated);
  return SemaRef.SubstTemplateArgument(Param->getDefaultArgument(),
                                       TemplateArgLists, Output);
}

/// Substitute template arguments into the default template argument for
/// the given template template parameter.
///
/// \param SemaRef the semantic analysis object for which we are performing
/// the substitution.
///
/// \param Template the template that we are synthesizing template arguments
/// for.
///
/// \param TemplateLoc the location of the template name that started the
/// template-id we are checking.
///
/// \param RAngleLoc the location of the right angle bracket ('>') that
/// terminates the template-id.
///
/// \param Param the template template parameter whose default we are
/// substituting into.
///
/// \param Converted the list of template arguments provided for template
/// parameters that precede \p Param in the template parameter list.
///
/// \param QualifierLoc Will be set to the nested-name-specifier (with
/// source-location information) that precedes the template name.
///
/// \returns the substituted template argument, or NULL if an error occurred.
static TemplateName SubstDefaultTemplateArgument(
    Sema &SemaRef, TemplateDecl *Template, SourceLocation TemplateLoc,
    SourceLocation RAngleLoc, TemplateTemplateParmDecl *Param,
    ArrayRef<TemplateArgument> SugaredConverted,
    ArrayRef<TemplateArgument> CanonicalConverted,
    NestedNameSpecifierLoc &QualifierLoc) {
  Sema::InstantiatingTemplate Inst(
      SemaRef, TemplateLoc, TemplateParameter(Param), Template,
      SugaredConverted, SourceRange(TemplateLoc, RAngleLoc));
  if (Inst.isInvalid())
    return TemplateName();

  // Only substitute for the innermost template argument list.
  MultiLevelTemplateArgumentList TemplateArgLists(Template, SugaredConverted,
                                                  /*Final=*/true);
  for (unsigned i = 0, e = Param->getDepth(); i != e; ++i)
    TemplateArgLists.addOuterTemplateArguments(std::nullopt);

  Sema::ContextRAII SavedContext(SemaRef, Template->getDeclContext());
  // Substitute into the nested-name-specifier first,
  QualifierLoc = Param->getDefaultArgument().getTemplateQualifierLoc();
  if (QualifierLoc) {
    QualifierLoc =
        SemaRef.SubstNestedNameSpecifierLoc(QualifierLoc, TemplateArgLists);
    if (!QualifierLoc)
      return TemplateName();
  }

  return SemaRef.SubstTemplateName(
             QualifierLoc,
             Param->getDefaultArgument().getArgument().getAsTemplate(),
             Param->getDefaultArgument().getTemplateNameLoc(),
             TemplateArgLists);
}

TemplateArgumentLoc Sema::SubstDefaultTemplateArgumentIfAvailable(
    TemplateDecl *Template, SourceLocation TemplateLoc,
    SourceLocation RAngleLoc, Decl *Param,
    ArrayRef<TemplateArgument> SugaredConverted,
    ArrayRef<TemplateArgument> CanonicalConverted, bool &HasDefaultArg) {
  HasDefaultArg = false;

  if (TemplateTypeParmDecl *TypeParm = dyn_cast<TemplateTypeParmDecl>(Param)) {
    if (!hasReachableDefaultArgument(TypeParm))
      return TemplateArgumentLoc();

    HasDefaultArg = true;
    TemplateArgumentLoc Output;
    if (SubstDefaultTemplateArgument(*this, Template, TemplateLoc, RAngleLoc,
                                     TypeParm, SugaredConverted,
                                     CanonicalConverted, Output))
      return TemplateArgumentLoc();
    return Output;
  }

  if (NonTypeTemplateParmDecl *NonTypeParm
        = dyn_cast<NonTypeTemplateParmDecl>(Param)) {
    if (!hasReachableDefaultArgument(NonTypeParm))
      return TemplateArgumentLoc();

    HasDefaultArg = true;
    TemplateArgumentLoc Output;
    if (SubstDefaultTemplateArgument(*this, Template, TemplateLoc, RAngleLoc,
                                     NonTypeParm, SugaredConverted,
                                     CanonicalConverted, Output))
      return TemplateArgumentLoc();
    return Output;
  }

  TemplateTemplateParmDecl *TempTempParm
    = cast<TemplateTemplateParmDecl>(Param);
  if (!hasReachableDefaultArgument(TempTempParm))
    return TemplateArgumentLoc();

  HasDefaultArg = true;
  NestedNameSpecifierLoc QualifierLoc;
  TemplateName TName = SubstDefaultTemplateArgument(
      *this, Template, TemplateLoc, RAngleLoc, TempTempParm, SugaredConverted,
      CanonicalConverted, QualifierLoc);
  if (TName.isNull())
    return TemplateArgumentLoc();

  return TemplateArgumentLoc(
      Context, TemplateArgument(TName),
      TempTempParm->getDefaultArgument().getTemplateQualifierLoc(),
      TempTempParm->getDefaultArgument().getTemplateNameLoc());
}

/// Convert a template-argument that we parsed as a type into a template, if
/// possible. C++ permits injected-class-names to perform dual service as
/// template template arguments and as template type arguments.
static TemplateArgumentLoc
convertTypeTemplateArgumentToTemplate(ASTContext &Context, TypeLoc TLoc) {
  // Extract and step over any surrounding nested-name-specifier.
  NestedNameSpecifierLoc QualLoc;
  if (auto ETLoc = TLoc.getAs<ElaboratedTypeLoc>()) {
    if (ETLoc.getTypePtr()->getKeyword() != ElaboratedTypeKeyword::None)
      return TemplateArgumentLoc();

    QualLoc = ETLoc.getQualifierLoc();
    TLoc = ETLoc.getNamedTypeLoc();
  }
  // If this type was written as an injected-class-name, it can be used as a
  // template template argument.
  if (auto InjLoc = TLoc.getAs<InjectedClassNameTypeLoc>())
    return TemplateArgumentLoc(Context, InjLoc.getTypePtr()->getTemplateName(),
                               QualLoc, InjLoc.getNameLoc());

  // If this type was written as an injected-class-name, it may have been
  // converted to a RecordType during instantiation. If the RecordType is
  // *not* wrapped in a TemplateSpecializationType and denotes a class
  // template specialization, it must have come from an injected-class-name.
  if (auto RecLoc = TLoc.getAs<RecordTypeLoc>())
    if (auto *CTSD =
            dyn_cast<ClassTemplateSpecializationDecl>(RecLoc.getDecl()))
      return TemplateArgumentLoc(Context,
                                 TemplateName(CTSD->getSpecializedTemplate()),
                                 QualLoc, RecLoc.getNameLoc());

  return TemplateArgumentLoc();
}

bool Sema::CheckTemplateArgument(
    NamedDecl *Param, TemplateArgumentLoc &Arg, NamedDecl *Template,
    SourceLocation TemplateLoc, SourceLocation RAngleLoc,
    unsigned ArgumentPackIndex,
    SmallVectorImpl<TemplateArgument> &SugaredConverted,
    SmallVectorImpl<TemplateArgument> &CanonicalConverted,
    CheckTemplateArgumentKind CTAK) {
  // Check template type parameters.
  if (TemplateTypeParmDecl *TTP = dyn_cast<TemplateTypeParmDecl>(Param))
    return CheckTemplateTypeArgument(TTP, Arg, SugaredConverted,
                                     CanonicalConverted);

  // Check non-type template parameters.
  if (NonTypeTemplateParmDecl *NTTP =dyn_cast<NonTypeTemplateParmDecl>(Param)) {
    // Do substitution on the type of the non-type template parameter
    // with the template arguments we've seen thus far.  But if the
    // template has a dependent context then we cannot substitute yet.
    QualType NTTPType = NTTP->getType();
    if (NTTP->isParameterPack() && NTTP->isExpandedParameterPack())
      NTTPType = NTTP->getExpansionType(ArgumentPackIndex);

    if (NTTPType->isInstantiationDependentType() &&
        !isa<TemplateTemplateParmDecl>(Template) &&
        !Template->getDeclContext()->isDependentContext()) {
      // Do substitution on the type of the non-type template parameter.
      InstantiatingTemplate Inst(*this, TemplateLoc, Template, NTTP,
                                 SugaredConverted,
                                 SourceRange(TemplateLoc, RAngleLoc));
      if (Inst.isInvalid())
        return true;

      MultiLevelTemplateArgumentList MLTAL(Template, SugaredConverted,
                                           /*Final=*/true);
      // If the parameter is a pack expansion, expand this slice of the pack.
      if (auto *PET = NTTPType->getAs<PackExpansionType>()) {
        Sema::ArgumentPackSubstitutionIndexRAII SubstIndex(*this,
                                                           ArgumentPackIndex);
        NTTPType = SubstType(PET->getPattern(), MLTAL, NTTP->getLocation(),
                             NTTP->getDeclName());
      } else {
        NTTPType = SubstType(NTTPType, MLTAL, NTTP->getLocation(),
                             NTTP->getDeclName());
      }

      // If that worked, check the non-type template parameter type
      // for validity.
      if (!NTTPType.isNull())
        NTTPType = CheckNonTypeTemplateParameterType(NTTPType,
                                                     NTTP->getLocation());
      if (NTTPType.isNull())
        return true;
    }

    switch (Arg.getArgument().getKind()) {
    case TemplateArgument::Null:
      llvm_unreachable("Should never see a NULL template argument here");

    case TemplateArgument::Expression: {
      Expr *E = Arg.getArgument().getAsExpr();
      TemplateArgument SugaredResult, CanonicalResult;
      unsigned CurSFINAEErrors = NumSFINAEErrors;
      ExprResult Res = CheckTemplateArgument(NTTP, NTTPType, E, SugaredResult,
                                             CanonicalResult, CTAK);
      if (Res.isInvalid())
        return true;
      // If the current template argument causes an error, give up now.
      if (CurSFINAEErrors < NumSFINAEErrors)
        return true;

      // If the resulting expression is new, then use it in place of the
      // old expression in the template argument.
      if (Res.get() != E) {
        TemplateArgument TA(Res.get());
        Arg = TemplateArgumentLoc(TA, Res.get());
      }

      SugaredConverted.push_back(SugaredResult);
      CanonicalConverted.push_back(CanonicalResult);
      break;
    }

    case TemplateArgument::Declaration:
    case TemplateArgument::Integral:
    case TemplateArgument::StructuralValue:
    case TemplateArgument::NullPtr:
      // We've already checked this template argument, so just copy
      // it to the list of converted arguments.
      SugaredConverted.push_back(Arg.getArgument());
      CanonicalConverted.push_back(
          Context.getCanonicalTemplateArgument(Arg.getArgument()));
      break;

    case TemplateArgument::Template:
    case TemplateArgument::TemplateExpansion:
      // We were given a template template argument. It may not be ill-formed;
      // see below.
      if (DependentTemplateName *DTN
            = Arg.getArgument().getAsTemplateOrTemplatePattern()
                                              .getAsDependentTemplateName()) {
        // We have a template argument such as \c T::template X, which we
        // parsed as a template template argument. However, since we now
        // know that we need a non-type template argument, convert this
        // template name into an expression.

        DeclarationNameInfo NameInfo(DTN->getIdentifier(),
                                     Arg.getTemplateNameLoc());

        CXXScopeSpec SS;
        SS.Adopt(Arg.getTemplateQualifierLoc());
        // FIXME: the template-template arg was a DependentTemplateName,
        // so it was provided with a template keyword. However, its source
        // location is not stored in the template argument structure.
        SourceLocation TemplateKWLoc;
        ExprResult E = DependentScopeDeclRefExpr::Create(
            Context, SS.getWithLocInContext(Context), TemplateKWLoc, NameInfo,
            nullptr);

        // If we parsed the template argument as a pack expansion, create a
        // pack expansion expression.
        if (Arg.getArgument().getKind() == TemplateArgument::TemplateExpansion){
          E = ActOnPackExpansion(E.get(), Arg.getTemplateEllipsisLoc());
          if (E.isInvalid())
            return true;
        }

        TemplateArgument SugaredResult, CanonicalResult;
        E = CheckTemplateArgument(NTTP, NTTPType, E.get(), SugaredResult,
                                  CanonicalResult, CTAK_Specified);
        if (E.isInvalid())
          return true;

        SugaredConverted.push_back(SugaredResult);
        CanonicalConverted.push_back(CanonicalResult);
        break;
      }

      // We have a template argument that actually does refer to a class
      // template, alias template, or template template parameter, and
      // therefore cannot be a non-type template argument.
      Diag(Arg.getLocation(), diag::err_template_arg_must_be_expr)
        << Arg.getSourceRange();
      NoteTemplateParameterLocation(*Param);

      return true;

    case TemplateArgument::Type: {
      // We have a non-type template parameter but the template
      // argument is a type.

      // C++ [temp.arg]p2:
      //   In a template-argument, an ambiguity between a type-id and
      //   an expression is resolved to a type-id, regardless of the
      //   form of the corresponding template-parameter.
      //
      // We warn specifically about this case, since it can be rather
      // confusing for users.
      QualType T = Arg.getArgument().getAsType();
      SourceRange SR = Arg.getSourceRange();
      if (T->isFunctionType())
        Diag(SR.getBegin(), diag::err_template_arg_nontype_ambig) << SR << T;
      else
        Diag(SR.getBegin(), diag::err_template_arg_must_be_expr) << SR;
      NoteTemplateParameterLocation(*Param);
      return true;
    }

    case TemplateArgument::Pack:
      llvm_unreachable("Caller must expand template argument packs");
    }

    return false;
  }


  // Check template template parameters.
  TemplateTemplateParmDecl *TempParm = cast<TemplateTemplateParmDecl>(Param);

  TemplateParameterList *Params = TempParm->getTemplateParameters();
  if (TempParm->isExpandedParameterPack())
    Params = TempParm->getExpansionTemplateParameters(ArgumentPackIndex);

  // Substitute into the template parameter list of the template
  // template parameter, since previously-supplied template arguments
  // may appear within the template template parameter.
  //
  // FIXME: Skip this if the parameters aren't instantiation-dependent.
  {
    // Set up a template instantiation context.
    LocalInstantiationScope Scope(*this);
    InstantiatingTemplate Inst(*this, TemplateLoc, Template, TempParm,
                               SugaredConverted,
                               SourceRange(TemplateLoc, RAngleLoc));
    if (Inst.isInvalid())
      return true;

    Params =
        SubstTemplateParams(Params, CurContext,
                            MultiLevelTemplateArgumentList(
                                Template, SugaredConverted, /*Final=*/true),
                            /*EvaluateConstraints=*/false);
    if (!Params)
      return true;
  }

  // C++1z [temp.local]p1: (DR1004)
  //   When [the injected-class-name] is used [...] as a template-argument for
  //   a template template-parameter [...] it refers to the class template
  //   itself.
  if (Arg.getArgument().getKind() == TemplateArgument::Type) {
    TemplateArgumentLoc ConvertedArg = convertTypeTemplateArgumentToTemplate(
        Context, Arg.getTypeSourceInfo()->getTypeLoc());
    if (!ConvertedArg.getArgument().isNull())
      Arg = ConvertedArg;
  }

  switch (Arg.getArgument().getKind()) {
  case TemplateArgument::Null:
    llvm_unreachable("Should never see a NULL template argument here");

  case TemplateArgument::Template:
  case TemplateArgument::TemplateExpansion:
    if (CheckTemplateTemplateArgument(TempParm, Params, Arg,
                                      /*IsDeduced=*/CTAK != CTAK_Specified))
      return true;

    SugaredConverted.push_back(Arg.getArgument());
    CanonicalConverted.push_back(
        Context.getCanonicalTemplateArgument(Arg.getArgument()));
    break;

  case TemplateArgument::Expression:
  case TemplateArgument::Type:
    // We have a template template parameter but the template
    // argument does not refer to a template.
    Diag(Arg.getLocation(), diag::err_template_arg_must_be_template)
      << getLangOpts().CPlusPlus11;
    return true;

  case TemplateArgument::Declaration:
  case TemplateArgument::Integral:
  case TemplateArgument::StructuralValue:
  case TemplateArgument::NullPtr:
    llvm_unreachable("non-type argument with template template parameter");

  case TemplateArgument::Pack:
    llvm_unreachable("Caller must expand template argument packs");
  }

  return false;
}

/// Diagnose a missing template argument.
template<typename TemplateParmDecl>
static bool diagnoseMissingArgument(Sema &S, SourceLocation Loc,
                                    TemplateDecl *TD,
                                    const TemplateParmDecl *D,
                                    TemplateArgumentListInfo &Args) {
  // Dig out the most recent declaration of the template parameter; there may be
  // declarations of the template that are more recent than TD.
  D = cast<TemplateParmDecl>(cast<TemplateDecl>(TD->getMostRecentDecl())
                                 ->getTemplateParameters()
                                 ->getParam(D->getIndex()));

  // If there's a default argument that's not reachable, diagnose that we're
  // missing a module import.
  llvm::SmallVector<Module*, 8> Modules;
  if (D->hasDefaultArgument() && !S.hasReachableDefaultArgument(D, &Modules)) {
    S.diagnoseMissingImport(Loc, cast<NamedDecl>(TD),
                            D->getDefaultArgumentLoc(), Modules,
                            Sema::MissingImportKind::DefaultArgument,
                            /*Recover*/true);
    return true;
  }

  // FIXME: If there's a more recent default argument that *is* visible,
  // diagnose that it was declared too late.

  TemplateParameterList *Params = TD->getTemplateParameters();

  S.Diag(Loc, diag::err_template_arg_list_different_arity)
    << /*not enough args*/0
    << (int)S.getTemplateNameKindForDiagnostics(TemplateName(TD))
    << TD;
  S.NoteTemplateLocation(*TD, Params->getSourceRange());
  return true;
}

/// Check that the given template argument list is well-formed
/// for specializing the given template.
bool Sema::CheckTemplateArgumentList(
    TemplateDecl *Template, SourceLocation TemplateLoc,
    TemplateArgumentListInfo &TemplateArgs, bool PartialTemplateArgs,
    SmallVectorImpl<TemplateArgument> &SugaredConverted,
    SmallVectorImpl<TemplateArgument> &CanonicalConverted,
    bool UpdateArgsWithConversions, bool *ConstraintsNotSatisfied,
    bool PartialOrderingTTP) {

  if (ConstraintsNotSatisfied)
    *ConstraintsNotSatisfied = false;

  // Make a copy of the template arguments for processing.  Only make the
  // changes at the end when successful in matching the arguments to the
  // template.
  TemplateArgumentListInfo NewArgs = TemplateArgs;

  TemplateParameterList *Params = GetTemplateParameterList(Template);

  SourceLocation RAngleLoc = NewArgs.getRAngleLoc();

  // C++ [temp.arg]p1:
  //   [...] The type and form of each template-argument specified in
  //   a template-id shall match the type and form specified for the
  //   corresponding parameter declared by the template in its
  //   template-parameter-list.
  bool isTemplateTemplateParameter = isa<TemplateTemplateParmDecl>(Template);
  SmallVector<TemplateArgument, 2> SugaredArgumentPack;
  SmallVector<TemplateArgument, 2> CanonicalArgumentPack;
  unsigned ArgIdx = 0, NumArgs = NewArgs.size();
  LocalInstantiationScope InstScope(*this, true);
  for (TemplateParameterList::iterator Param = Params->begin(),
                                       ParamEnd = Params->end();
       Param != ParamEnd; /* increment in loop */) {
    // If we have an expanded parameter pack, make sure we don't have too
    // many arguments.
    if (std::optional<unsigned> Expansions = getExpandedPackSize(*Param)) {
      if (*Expansions == SugaredArgumentPack.size()) {
        // We're done with this parameter pack. Pack up its arguments and add
        // them to the list.
        SugaredConverted.push_back(
            TemplateArgument::CreatePackCopy(Context, SugaredArgumentPack));
        SugaredArgumentPack.clear();

        CanonicalConverted.push_back(
            TemplateArgument::CreatePackCopy(Context, CanonicalArgumentPack));
        CanonicalArgumentPack.clear();

        // This argument is assigned to the next parameter.
        ++Param;
        continue;
      } else if (ArgIdx == NumArgs && !PartialTemplateArgs) {
        // Not enough arguments for this parameter pack.
        Diag(TemplateLoc, diag::err_template_arg_list_different_arity)
          << /*not enough args*/0
          << (int)getTemplateNameKindForDiagnostics(TemplateName(Template))
          << Template;
        NoteTemplateLocation(*Template, Params->getSourceRange());
        return true;
      }
    }

    if (ArgIdx < NumArgs) {
      // Check the template argument we were given.
      if (CheckTemplateArgument(*Param, NewArgs[ArgIdx], Template, TemplateLoc,
                                RAngleLoc, SugaredArgumentPack.size(),
                                SugaredConverted, CanonicalConverted,
                                CTAK_Specified))
        return true;

      CanonicalConverted.back().setIsDefaulted(
          clang::isSubstitutedDefaultArgument(
              Context, NewArgs[ArgIdx].getArgument(), *Param,
              CanonicalConverted, Params->getDepth()));

      bool PackExpansionIntoNonPack =
          NewArgs[ArgIdx].getArgument().isPackExpansion() &&
          (!(*Param)->isTemplateParameterPack() || getExpandedPackSize(*Param));
      // CWG1430: Don't diagnose this pack expansion when partial
      // ordering template template parameters. Some uses of the template could
      // be valid, and invalid uses will be diagnosed later during
      // instantiation.
      if (PackExpansionIntoNonPack && !PartialOrderingTTP &&
          (isa<TypeAliasTemplateDecl>(Template) ||
           isa<ConceptDecl>(Template))) {
        // CWG1430: we have a pack expansion as an argument to an
        // alias template, and it's not part of a parameter pack. This
        // can't be canonicalized, so reject it now.
        // As for concepts - we cannot normalize constraints where this
        // situation exists.
        Diag(NewArgs[ArgIdx].getLocation(),
             diag::err_template_expansion_into_fixed_list)
          << (isa<ConceptDecl>(Template) ? 1 : 0)
          << NewArgs[ArgIdx].getSourceRange();
        NoteTemplateParameterLocation(**Param);
        return true;
      }

      // We're now done with this argument.
      ++ArgIdx;

      if ((*Param)->isTemplateParameterPack()) {
        // The template parameter was a template parameter pack, so take the
        // deduced argument and place it on the argument pack. Note that we
        // stay on the same template parameter so that we can deduce more
        // arguments.
        SugaredArgumentPack.push_back(SugaredConverted.pop_back_val());
        CanonicalArgumentPack.push_back(CanonicalConverted.pop_back_val());
      } else {
        // Move to the next template parameter.
        ++Param;
      }

      // If we just saw a pack expansion into a non-pack, then directly convert
      // the remaining arguments, because we don't know what parameters they'll
      // match up with.
      if (PackExpansionIntoNonPack) {
        if (!SugaredArgumentPack.empty()) {
          // If we were part way through filling in an expanded parameter pack,
          // fall back to just producing individual arguments.
          SugaredConverted.insert(SugaredConverted.end(),
                                  SugaredArgumentPack.begin(),
                                  SugaredArgumentPack.end());
          SugaredArgumentPack.clear();

          CanonicalConverted.insert(CanonicalConverted.end(),
                                    CanonicalArgumentPack.begin(),
                                    CanonicalArgumentPack.end());
          CanonicalArgumentPack.clear();
        }

        while (ArgIdx < NumArgs) {
          const TemplateArgument &Arg = NewArgs[ArgIdx].getArgument();
          SugaredConverted.push_back(Arg);
          CanonicalConverted.push_back(
              Context.getCanonicalTemplateArgument(Arg));
          ++ArgIdx;
        }

        return false;
      }

      continue;
    }

    // If we're checking a partial template argument list, we're done.
    if (PartialTemplateArgs) {
      if ((*Param)->isTemplateParameterPack() && !SugaredArgumentPack.empty()) {
        SugaredConverted.push_back(
            TemplateArgument::CreatePackCopy(Context, SugaredArgumentPack));
        CanonicalConverted.push_back(
            TemplateArgument::CreatePackCopy(Context, CanonicalArgumentPack));
      }
      return false;
    }

    // If we have a template parameter pack with no more corresponding
    // arguments, just break out now and we'll fill in the argument pack below.
    if ((*Param)->isTemplateParameterPack()) {
      assert(!getExpandedPackSize(*Param) &&
             "Should have dealt with this already");

      // A non-expanded parameter pack before the end of the parameter list
      // only occurs for an ill-formed template parameter list, unless we've
      // got a partial argument list for a function template, so just bail out.
      if (Param + 1 != ParamEnd) {
        assert(
            (Template->getMostRecentDecl()->getKind() != Decl::Kind::Concept) &&
            "Concept templates must have parameter packs at the end.");
        return true;
      }

      SugaredConverted.push_back(
          TemplateArgument::CreatePackCopy(Context, SugaredArgumentPack));
      SugaredArgumentPack.clear();

      CanonicalConverted.push_back(
          TemplateArgument::CreatePackCopy(Context, CanonicalArgumentPack));
      CanonicalArgumentPack.clear();

      ++Param;
      continue;
    }

    // Check whether we have a default argument.
    TemplateArgumentLoc Arg;

    // Retrieve the default template argument from the template
    // parameter. For each kind of template parameter, we substitute the
    // template arguments provided thus far and any "outer" template arguments
    // (when the template parameter was part of a nested template) into
    // the default argument.
    if (TemplateTypeParmDecl *TTP = dyn_cast<TemplateTypeParmDecl>(*Param)) {
      if (!hasReachableDefaultArgument(TTP))
        return diagnoseMissingArgument(*this, TemplateLoc, Template, TTP,
                                       NewArgs);

      if (SubstDefaultTemplateArgument(*this, Template, TemplateLoc, RAngleLoc,
                                       TTP, SugaredConverted,
                                       CanonicalConverted, Arg))
        return true;
    } else if (NonTypeTemplateParmDecl *NTTP
                 = dyn_cast<NonTypeTemplateParmDecl>(*Param)) {
      if (!hasReachableDefaultArgument(NTTP))
        return diagnoseMissingArgument(*this, TemplateLoc, Template, NTTP,
                                       NewArgs);

      if (SubstDefaultTemplateArgument(*this, Template, TemplateLoc, RAngleLoc,
                                       NTTP, SugaredConverted,
                                       CanonicalConverted, Arg))
        return true;
    } else {
      TemplateTemplateParmDecl *TempParm
        = cast<TemplateTemplateParmDecl>(*Param);

      if (!hasReachableDefaultArgument(TempParm))
        return diagnoseMissingArgument(*this, TemplateLoc, Template, TempParm,
                                       NewArgs);

      NestedNameSpecifierLoc QualifierLoc;
      TemplateName Name = SubstDefaultTemplateArgument(
          *this, Template, TemplateLoc, RAngleLoc, TempParm, SugaredConverted,
          CanonicalConverted, QualifierLoc);
      if (Name.isNull())
        return true;

      Arg = TemplateArgumentLoc(
          Context, TemplateArgument(Name), QualifierLoc,
          TempParm->getDefaultArgument().getTemplateNameLoc());
    }

    // Introduce an instantiation record that describes where we are using
    // the default template argument. We're not actually instantiating a
    // template here, we just create this object to put a note into the
    // context stack.
    InstantiatingTemplate Inst(*this, RAngleLoc, Template, *Param,
                               SugaredConverted,
                               SourceRange(TemplateLoc, RAngleLoc));
    if (Inst.isInvalid())
      return true;

    // Check the default template argument.
    if (CheckTemplateArgument(*Param, Arg, Template, TemplateLoc, RAngleLoc, 0,
                              SugaredConverted, CanonicalConverted,
                              CTAK_Specified))
      return true;

    CanonicalConverted.back().setIsDefaulted(true);

    // Core issue 150 (assumed resolution): if this is a template template
    // parameter, keep track of the default template arguments from the
    // template definition.
    if (isTemplateTemplateParameter)
      NewArgs.addArgument(Arg);

    // Move to the next template parameter and argument.
    ++Param;
    ++ArgIdx;
  }

  // If we're performing a partial argument substitution, allow any trailing
  // pack expansions; they might be empty. This can happen even if
  // PartialTemplateArgs is false (the list of arguments is complete but
  // still dependent).
  if (ArgIdx < NumArgs && CurrentInstantiationScope &&
      CurrentInstantiationScope->getPartiallySubstitutedPack()) {
    while (ArgIdx < NumArgs &&
           NewArgs[ArgIdx].getArgument().isPackExpansion()) {
      const TemplateArgument &Arg = NewArgs[ArgIdx++].getArgument();
      SugaredConverted.push_back(Arg);
      CanonicalConverted.push_back(Context.getCanonicalTemplateArgument(Arg));
    }
  }

  // If we have any leftover arguments, then there were too many arguments.
  // Complain and fail.
  if (ArgIdx < NumArgs) {
    Diag(TemplateLoc, diag::err_template_arg_list_different_arity)
        << /*too many args*/1
        << (int)getTemplateNameKindForDiagnostics(TemplateName(Template))
        << Template
        << SourceRange(NewArgs[ArgIdx].getLocation(), NewArgs.getRAngleLoc());
    NoteTemplateLocation(*Template, Params->getSourceRange());
    return true;
  }

  // No problems found with the new argument list, propagate changes back
  // to caller.
  if (UpdateArgsWithConversions)
    TemplateArgs = std::move(NewArgs);

  if (!PartialTemplateArgs) {
    // Setup the context/ThisScope for the case where we are needing to
    // re-instantiate constraints outside of normal instantiation.
    DeclContext *NewContext = Template->getDeclContext();

    // If this template is in a template, make sure we extract the templated
    // decl.
    if (auto *TD = dyn_cast<TemplateDecl>(NewContext))
      NewContext = Decl::castToDeclContext(TD->getTemplatedDecl());
    auto *RD = dyn_cast<CXXRecordDecl>(NewContext);

    Qualifiers ThisQuals;
    if (const auto *Method =
            dyn_cast_or_null<CXXMethodDecl>(Template->getTemplatedDecl()))
      ThisQuals = Method->getMethodQualifiers();

    ContextRAII Context(*this, NewContext);
    CXXThisScopeRAII(*this, RD, ThisQuals, RD != nullptr);

    MultiLevelTemplateArgumentList MLTAL = getTemplateInstantiationArgs(
        Template, NewContext, /*Final=*/false, CanonicalConverted,
        /*RelativeToPrimary=*/true,
        /*Pattern=*/nullptr,
        /*ForConceptInstantiation=*/true);
    if (EnsureTemplateArgumentListConstraints(
            Template, MLTAL,
            SourceRange(TemplateLoc, TemplateArgs.getRAngleLoc()))) {
      if (ConstraintsNotSatisfied)
        *ConstraintsNotSatisfied = true;
      return true;
    }
  }

  return false;
}

namespace {
  class UnnamedLocalNoLinkageFinder
    : public TypeVisitor<UnnamedLocalNoLinkageFinder, bool>
  {
    Sema &S;
    SourceRange SR;

    typedef TypeVisitor<UnnamedLocalNoLinkageFinder, bool> inherited;

  public:
    UnnamedLocalNoLinkageFinder(Sema &S, SourceRange SR) : S(S), SR(SR) { }

    bool Visit(QualType T) {
      return T.isNull() ? false : inherited::Visit(T.getTypePtr());
    }

#define TYPE(Class, Parent) \
    bool Visit##Class##Type(const Class##Type *);
#define ABSTRACT_TYPE(Class, Parent) \
    bool Visit##Class##Type(const Class##Type *) { return false; }
#define NON_CANONICAL_TYPE(Class, Parent) \
    bool Visit##Class##Type(const Class##Type *) { return false; }
#include "clang/AST/TypeNodes.inc"

    bool VisitTagDecl(const TagDecl *Tag);
    bool VisitNestedNameSpecifier(NestedNameSpecifier *NNS);
  };
} // end anonymous namespace

bool UnnamedLocalNoLinkageFinder::VisitBuiltinType(const BuiltinType*) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitComplexType(const ComplexType* T) {
  return Visit(T->getElementType());
}

bool UnnamedLocalNoLinkageFinder::VisitPointerType(const PointerType* T) {
  return Visit(T->getPointeeType());
}

bool UnnamedLocalNoLinkageFinder::VisitBlockPointerType(
                                                    const BlockPointerType* T) {
  return Visit(T->getPointeeType());
}

bool UnnamedLocalNoLinkageFinder::VisitLValueReferenceType(
                                                const LValueReferenceType* T) {
  return Visit(T->getPointeeType());
}

bool UnnamedLocalNoLinkageFinder::VisitRValueReferenceType(
                                                const RValueReferenceType* T) {
  return Visit(T->getPointeeType());
}

bool UnnamedLocalNoLinkageFinder::VisitMemberPointerType(
                                                  const MemberPointerType* T) {
  return Visit(T->getPointeeType()) || Visit(QualType(T->getClass(), 0));
}

bool UnnamedLocalNoLinkageFinder::VisitConstantArrayType(
                                                  const ConstantArrayType* T) {
  return Visit(T->getElementType());
}

bool UnnamedLocalNoLinkageFinder::VisitIncompleteArrayType(
                                                 const IncompleteArrayType* T) {
  return Visit(T->getElementType());
}

bool UnnamedLocalNoLinkageFinder::VisitVariableArrayType(
                                                   const VariableArrayType* T) {
  return Visit(T->getElementType());
}

bool UnnamedLocalNoLinkageFinder::VisitDependentSizedArrayType(
                                            const DependentSizedArrayType* T) {
  return Visit(T->getElementType());
}

bool UnnamedLocalNoLinkageFinder::VisitDependentSizedExtVectorType(
                                         const DependentSizedExtVectorType* T) {
  return Visit(T->getElementType());
}

bool UnnamedLocalNoLinkageFinder::VisitDependentSizedMatrixType(
    const DependentSizedMatrixType *T) {
  return Visit(T->getElementType());
}

bool UnnamedLocalNoLinkageFinder::VisitDependentAddressSpaceType(
    const DependentAddressSpaceType *T) {
  return Visit(T->getPointeeType());
}

bool UnnamedLocalNoLinkageFinder::VisitVectorType(const VectorType* T) {
  return Visit(T->getElementType());
}

bool UnnamedLocalNoLinkageFinder::VisitDependentVectorType(
    const DependentVectorType *T) {
  return Visit(T->getElementType());
}

bool UnnamedLocalNoLinkageFinder::VisitExtVectorType(const ExtVectorType* T) {
  return Visit(T->getElementType());
}

bool UnnamedLocalNoLinkageFinder::VisitConstantMatrixType(
    const ConstantMatrixType *T) {
  return Visit(T->getElementType());
}

bool UnnamedLocalNoLinkageFinder::VisitFunctionProtoType(
                                                  const FunctionProtoType* T) {
  for (const auto &A : T->param_types()) {
    if (Visit(A))
      return true;
  }

  return Visit(T->getReturnType());
}

bool UnnamedLocalNoLinkageFinder::VisitFunctionNoProtoType(
                                               const FunctionNoProtoType* T) {
  return Visit(T->getReturnType());
}

bool UnnamedLocalNoLinkageFinder::VisitUnresolvedUsingType(
                                                  const UnresolvedUsingType*) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitTypeOfExprType(const TypeOfExprType*) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitTypeOfType(const TypeOfType* T) {
  return Visit(T->getUnmodifiedType());
}

bool UnnamedLocalNoLinkageFinder::VisitDecltypeType(const DecltypeType*) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitPackIndexingType(
    const PackIndexingType *) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitUnaryTransformType(
                                                    const UnaryTransformType*) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitAutoType(const AutoType *T) {
  return Visit(T->getDeducedType());
}

bool UnnamedLocalNoLinkageFinder::VisitDeducedTemplateSpecializationType(
    const DeducedTemplateSpecializationType *T) {
  return Visit(T->getDeducedType());
}

bool UnnamedLocalNoLinkageFinder::VisitRecordType(const RecordType* T) {
  return VisitTagDecl(T->getDecl());
}

bool UnnamedLocalNoLinkageFinder::VisitEnumType(const EnumType* T) {
  return VisitTagDecl(T->getDecl());
}

bool UnnamedLocalNoLinkageFinder::VisitTemplateTypeParmType(
                                                 const TemplateTypeParmType*) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitSubstTemplateTypeParmPackType(
                                        const SubstTemplateTypeParmPackType *) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitTemplateSpecializationType(
                                            const TemplateSpecializationType*) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitInjectedClassNameType(
                                              const InjectedClassNameType* T) {
  return VisitTagDecl(T->getDecl());
}

bool UnnamedLocalNoLinkageFinder::VisitDependentNameType(
                                                   const DependentNameType* T) {
  return VisitNestedNameSpecifier(T->getQualifier());
}

bool UnnamedLocalNoLinkageFinder::VisitDependentTemplateSpecializationType(
                                 const DependentTemplateSpecializationType* T) {
  if (auto *Q = T->getQualifier())
    return VisitNestedNameSpecifier(Q);
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitPackExpansionType(
                                                   const PackExpansionType* T) {
  return Visit(T->getPattern());
}

bool UnnamedLocalNoLinkageFinder::VisitObjCObjectType(const ObjCObjectType *) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitObjCInterfaceType(
                                                   const ObjCInterfaceType *) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitObjCObjectPointerType(
                                                const ObjCObjectPointerType *) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitAtomicType(const AtomicType* T) {
  return Visit(T->getValueType());
}

bool UnnamedLocalNoLinkageFinder::VisitPipeType(const PipeType* T) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitBitIntType(const BitIntType *T) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitArrayParameterType(
    const ArrayParameterType *T) {
  return VisitConstantArrayType(T);
}

bool UnnamedLocalNoLinkageFinder::VisitDependentBitIntType(
    const DependentBitIntType *T) {
  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitTagDecl(const TagDecl *Tag) {
  if (Tag->getDeclContext()->isFunctionOrMethod()) {
    S.Diag(SR.getBegin(),
           S.getLangOpts().CPlusPlus11 ?
             diag::warn_cxx98_compat_template_arg_local_type :
             diag::ext_template_arg_local_type)
      << S.Context.getTypeDeclType(Tag) << SR;
    return true;
  }

  if (!Tag->hasNameForLinkage()) {
    S.Diag(SR.getBegin(),
           S.getLangOpts().CPlusPlus11 ?
             diag::warn_cxx98_compat_template_arg_unnamed_type :
             diag::ext_template_arg_unnamed_type) << SR;
    S.Diag(Tag->getLocation(), diag::note_template_unnamed_type_here);
    return true;
  }

  return false;
}

bool UnnamedLocalNoLinkageFinder::VisitNestedNameSpecifier(
                                                    NestedNameSpecifier *NNS) {
  assert(NNS);
  if (NNS->getPrefix() && VisitNestedNameSpecifier(NNS->getPrefix()))
    return true;

  switch (NNS->getKind()) {
  case NestedNameSpecifier::Identifier:
  case NestedNameSpecifier::Namespace:
  case NestedNameSpecifier::NamespaceAlias:
  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Super:
    return false;

  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    return Visit(QualType(NNS->getAsType(), 0));
  }
  llvm_unreachable("Invalid NestedNameSpecifier::Kind!");
}

bool Sema::CheckTemplateArgument(TypeSourceInfo *ArgInfo) {
  assert(ArgInfo && "invalid TypeSourceInfo");
  QualType Arg = ArgInfo->getType();
  SourceRange SR = ArgInfo->getTypeLoc().getSourceRange();
  QualType CanonArg = Context.getCanonicalType(Arg);

  if (CanonArg->isVariablyModifiedType()) {
    return Diag(SR.getBegin(), diag::err_variably_modified_template_arg) << Arg;
  } else if (Context.hasSameUnqualifiedType(Arg, Context.OverloadTy)) {
    return Diag(SR.getBegin(), diag::err_template_arg_overload_type) << SR;
  }

  // C++03 [temp.arg.type]p2:
  //   A local type, a type with no linkage, an unnamed type or a type
  //   compounded from any of these types shall not be used as a
  //   template-argument for a template type-parameter.
  //
  // C++11 allows these, and even in C++03 we allow them as an extension with
  // a warning.
  if (LangOpts.CPlusPlus11 || CanonArg->hasUnnamedOrLocalType()) {
    UnnamedLocalNoLinkageFinder Finder(*this, SR);
    (void)Finder.Visit(CanonArg);
  }

  return false;
}

enum NullPointerValueKind {
  NPV_NotNullPointer,
  NPV_NullPointer,
  NPV_Error
};

/// Determine whether the given template argument is a null pointer
/// value of the appropriate type.
static NullPointerValueKind
isNullPointerValueTemplateArgument(Sema &S, NonTypeTemplateParmDecl *Param,
                                   QualType ParamType, Expr *Arg,
                                   Decl *Entity = nullptr) {
  if (Arg->isValueDependent() || Arg->isTypeDependent())
    return NPV_NotNullPointer;

  // dllimport'd entities aren't constant but are available inside of template
  // arguments.
  if (Entity && Entity->hasAttr<DLLImportAttr>())
    return NPV_NotNullPointer;

  if (!S.isCompleteType(Arg->getExprLoc(), ParamType))
    llvm_unreachable(
        "Incomplete parameter type in isNullPointerValueTemplateArgument!");

  if (!S.getLangOpts().CPlusPlus11)
    return NPV_NotNullPointer;

  // Determine whether we have a constant expression.
  ExprResult ArgRV = S.DefaultFunctionArrayConversion(Arg);
  if (ArgRV.isInvalid())
    return NPV_Error;
  Arg = ArgRV.get();

  Expr::EvalResult EvalResult;
  SmallVector<PartialDiagnosticAt, 8> Notes;
  EvalResult.Diag = &Notes;
  if (!Arg->EvaluateAsRValue(EvalResult, S.Context) ||
      EvalResult.HasSideEffects) {
    SourceLocation DiagLoc = Arg->getExprLoc();

    // If our only note is the usual "invalid subexpression" note, just point
    // the caret at its location rather than producing an essentially
    // redundant note.
    if (Notes.size() == 1 && Notes[0].second.getDiagID() ==
        diag::note_invalid_subexpr_in_const_expr) {
      DiagLoc = Notes[0].first;
      Notes.clear();
    }

    S.Diag(DiagLoc, diag::err_template_arg_not_address_constant)
      << Arg->getType() << Arg->getSourceRange();
    for (unsigned I = 0, N = Notes.size(); I != N; ++I)
      S.Diag(Notes[I].first, Notes[I].second);

    S.NoteTemplateParameterLocation(*Param);
    return NPV_Error;
  }

  // C++11 [temp.arg.nontype]p1:
  //   - an address constant expression of type std::nullptr_t
  if (Arg->getType()->isNullPtrType())
    return NPV_NullPointer;

  //   - a constant expression that evaluates to a null pointer value (4.10); or
  //   - a constant expression that evaluates to a null member pointer value
  //     (4.11); or
  if ((EvalResult.Val.isLValue() && EvalResult.Val.isNullPointer()) ||
      (EvalResult.Val.isMemberPointer() &&
       !EvalResult.Val.getMemberPointerDecl())) {
    // If our expression has an appropriate type, we've succeeded.
    bool ObjCLifetimeConversion;
    if (S.Context.hasSameUnqualifiedType(Arg->getType(), ParamType) ||
        S.IsQualificationConversion(Arg->getType(), ParamType, false,
                                     ObjCLifetimeConversion))
      return NPV_NullPointer;

    // The types didn't match, but we know we got a null pointer; complain,
    // then recover as if the types were correct.
    S.Diag(Arg->getExprLoc(), diag::err_template_arg_wrongtype_null_constant)
      << Arg->getType() << ParamType << Arg->getSourceRange();
    S.NoteTemplateParameterLocation(*Param);
    return NPV_NullPointer;
  }

  if (EvalResult.Val.isLValue() && !EvalResult.Val.getLValueBase()) {
    // We found a pointer that isn't null, but doesn't refer to an object.
    // We could just return NPV_NotNullPointer, but we can print a better
    // message with the information we have here.
    S.Diag(Arg->getExprLoc(), diag::err_template_arg_invalid)
      << EvalResult.Val.getAsString(S.Context, ParamType);
    S.NoteTemplateParameterLocation(*Param);
    return NPV_Error;
  }

  // If we don't have a null pointer value, but we do have a NULL pointer
  // constant, suggest a cast to the appropriate type.
  if (Arg->isNullPointerConstant(S.Context, Expr::NPC_NeverValueDependent)) {
    std::string Code = "static_cast<" + ParamType.getAsString() + ">(";
    S.Diag(Arg->getExprLoc(), diag::err_template_arg_untyped_null_constant)
        << ParamType << FixItHint::CreateInsertion(Arg->getBeginLoc(), Code)
        << FixItHint::CreateInsertion(S.getLocForEndOfToken(Arg->getEndLoc()),
                                      ")");
    S.NoteTemplateParameterLocation(*Param);
    return NPV_NullPointer;
  }

  // FIXME: If we ever want to support general, address-constant expressions
  // as non-type template arguments, we should return the ExprResult here to
  // be interpreted by the caller.
  return NPV_NotNullPointer;
}

/// Checks whether the given template argument is compatible with its
/// template parameter.
static bool CheckTemplateArgumentIsCompatibleWithParameter(
    Sema &S, NonTypeTemplateParmDecl *Param, QualType ParamType, Expr *ArgIn,
    Expr *Arg, QualType ArgType) {
  bool ObjCLifetimeConversion;
  if (ParamType->isPointerType() &&
      !ParamType->castAs<PointerType>()->getPointeeType()->isFunctionType() &&
      S.IsQualificationConversion(ArgType, ParamType, false,
                                  ObjCLifetimeConversion)) {
    // For pointer-to-object types, qualification conversions are
    // permitted.
  } else {
    if (const ReferenceType *ParamRef = ParamType->getAs<ReferenceType>()) {
      if (!ParamRef->getPointeeType()->isFunctionType()) {
        // C++ [temp.arg.nontype]p5b3:
        //   For a non-type template-parameter of type reference to
        //   object, no conversions apply. The type referred to by the
        //   reference may be more cv-qualified than the (otherwise
        //   identical) type of the template- argument. The
        //   template-parameter is bound directly to the
        //   template-argument, which shall be an lvalue.

        // FIXME: Other qualifiers?
        unsigned ParamQuals = ParamRef->getPointeeType().getCVRQualifiers();
        unsigned ArgQuals = ArgType.getCVRQualifiers();

        if ((ParamQuals | ArgQuals) != ParamQuals) {
          S.Diag(Arg->getBeginLoc(),
                 diag::err_template_arg_ref_bind_ignores_quals)
              << ParamType << Arg->getType() << Arg->getSourceRange();
          S.NoteTemplateParameterLocation(*Param);
          return true;
        }
      }
    }

    // At this point, the template argument refers to an object or
    // function with external linkage. We now need to check whether the
    // argument and parameter types are compatible.
    if (!S.Context.hasSameUnqualifiedType(ArgType,
                                          ParamType.getNonReferenceType())) {
      // We can't perform this conversion or binding.
      if (ParamType->isReferenceType())
        S.Diag(Arg->getBeginLoc(), diag::err_template_arg_no_ref_bind)
            << ParamType << ArgIn->getType() << Arg->getSourceRange();
      else
        S.Diag(Arg->getBeginLoc(), diag::err_template_arg_not_convertible)
            << ArgIn->getType() << ParamType << Arg->getSourceRange();
      S.NoteTemplateParameterLocation(*Param);
      return true;
    }
  }

  return false;
}

/// Checks whether the given template argument is the address
/// of an object or function according to C++ [temp.arg.nontype]p1.
static bool CheckTemplateArgumentAddressOfObjectOrFunction(
    Sema &S, NonTypeTemplateParmDecl *Param, QualType ParamType, Expr *ArgIn,
    TemplateArgument &SugaredConverted, TemplateArgument &CanonicalConverted) {
  bool Invalid = false;
  Expr *Arg = ArgIn;
  QualType ArgType = Arg->getType();

  bool AddressTaken = false;
  SourceLocation AddrOpLoc;
  if (S.getLangOpts().MicrosoftExt) {
    // Microsoft Visual C++ strips all casts, allows an arbitrary number of
    // dereference and address-of operators.
    Arg = Arg->IgnoreParenCasts();

    bool ExtWarnMSTemplateArg = false;
    UnaryOperatorKind FirstOpKind;
    SourceLocation FirstOpLoc;
    while (UnaryOperator *UnOp = dyn_cast<UnaryOperator>(Arg)) {
      UnaryOperatorKind UnOpKind = UnOp->getOpcode();
      if (UnOpKind == UO_Deref)
        ExtWarnMSTemplateArg = true;
      if (UnOpKind == UO_AddrOf || UnOpKind == UO_Deref) {
        Arg = UnOp->getSubExpr()->IgnoreParenCasts();
        if (!AddrOpLoc.isValid()) {
          FirstOpKind = UnOpKind;
          FirstOpLoc = UnOp->getOperatorLoc();
        }
      } else
        break;
    }
    if (FirstOpLoc.isValid()) {
      if (ExtWarnMSTemplateArg)
        S.Diag(ArgIn->getBeginLoc(), diag::ext_ms_deref_template_argument)
            << ArgIn->getSourceRange();

      if (FirstOpKind == UO_AddrOf)
        AddressTaken = true;
      else if (Arg->getType()->isPointerType()) {
        // We cannot let pointers get dereferenced here, that is obviously not a
        // constant expression.
        assert(FirstOpKind == UO_Deref);
        S.Diag(Arg->getBeginLoc(), diag::err_template_arg_not_decl_ref)
            << Arg->getSourceRange();
      }
    }
  } else {
    // See through any implicit casts we added to fix the type.
    Arg = Arg->IgnoreImpCasts();

    // C++ [temp.arg.nontype]p1:
    //
    //   A template-argument for a non-type, non-template
    //   template-parameter shall be one of: [...]
    //
    //     -- the address of an object or function with external
    //        linkage, including function templates and function
    //        template-ids but excluding non-static class members,
    //        expressed as & id-expression where the & is optional if
    //        the name refers to a function or array, or if the
    //        corresponding template-parameter is a reference; or

    // In C++98/03 mode, give an extension warning on any extra parentheses.
    // See http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_defects.html#773
    bool ExtraParens = false;
    while (ParenExpr *Parens = dyn_cast<ParenExpr>(Arg)) {
      if (!Invalid && !ExtraParens) {
        S.Diag(Arg->getBeginLoc(),
               S.getLangOpts().CPlusPlus11
                   ? diag::warn_cxx98_compat_template_arg_extra_parens
                   : diag::ext_template_arg_extra_parens)
            << Arg->getSourceRange();
        ExtraParens = true;
      }

      Arg = Parens->getSubExpr();
    }

    while (SubstNonTypeTemplateParmExpr *subst =
               dyn_cast<SubstNonTypeTemplateParmExpr>(Arg))
      Arg = subst->getReplacement()->IgnoreImpCasts();

    if (UnaryOperator *UnOp = dyn_cast<UnaryOperator>(Arg)) {
      if (UnOp->getOpcode() == UO_AddrOf) {
        Arg = UnOp->getSubExpr();
        AddressTaken = true;
        AddrOpLoc = UnOp->getOperatorLoc();
      }
    }

    while (SubstNonTypeTemplateParmExpr *subst =
               dyn_cast<SubstNonTypeTemplateParmExpr>(Arg))
      Arg = subst->getReplacement()->IgnoreImpCasts();
  }

  ValueDecl *Entity = nullptr;
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Arg))
    Entity = DRE->getDecl();
  else if (CXXUuidofExpr *CUE = dyn_cast<CXXUuidofExpr>(Arg))
    Entity = CUE->getGuidDecl();

  // If our parameter has pointer type, check for a null template value.
  if (ParamType->isPointerType() || ParamType->isNullPtrType()) {
    switch (isNullPointerValueTemplateArgument(S, Param, ParamType, ArgIn,
                                               Entity)) {
    case NPV_NullPointer:
      S.Diag(Arg->getExprLoc(), diag::warn_cxx98_compat_template_arg_null);
      SugaredConverted = TemplateArgument(ParamType,
                                          /*isNullPtr=*/true);
      CanonicalConverted =
          TemplateArgument(S.Context.getCanonicalType(ParamType),
                           /*isNullPtr=*/true);
      return false;

    case NPV_Error:
      return true;

    case NPV_NotNullPointer:
      break;
    }
  }

  // Stop checking the precise nature of the argument if it is value dependent,
  // it should be checked when instantiated.
  if (Arg->isValueDependent()) {
    SugaredConverted = TemplateArgument(ArgIn);
    CanonicalConverted =
        S.Context.getCanonicalTemplateArgument(SugaredConverted);
    return false;
  }

  if (!Entity) {
    S.Diag(Arg->getBeginLoc(), diag::err_template_arg_not_decl_ref)
        << Arg->getSourceRange();
    S.NoteTemplateParameterLocation(*Param);
    return true;
  }

  // Cannot refer to non-static data members
  if (isa<FieldDecl>(Entity) || isa<IndirectFieldDecl>(Entity)) {
    S.Diag(Arg->getBeginLoc(), diag::err_template_arg_field)
        << Entity << Arg->getSourceRange();
    S.NoteTemplateParameterLocation(*Param);
    return true;
  }

  // Cannot refer to non-static member functions
  if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Entity)) {
    if (!Method->isStatic()) {
      S.Diag(Arg->getBeginLoc(), diag::err_template_arg_method)
          << Method << Arg->getSourceRange();
      S.NoteTemplateParameterLocation(*Param);
      return true;
    }
  }

  FunctionDecl *Func = dyn_cast<FunctionDecl>(Entity);
  VarDecl *Var = dyn_cast<VarDecl>(Entity);
  MSGuidDecl *Guid = dyn_cast<MSGuidDecl>(Entity);

  // A non-type template argument must refer to an object or function.
  if (!Func && !Var && !Guid) {
    // We found something, but we don't know specifically what it is.
    S.Diag(Arg->getBeginLoc(), diag::err_template_arg_not_object_or_func)
        << Arg->getSourceRange();
    S.Diag(Entity->getLocation(), diag::note_template_arg_refers_here);
    return true;
  }

  // Address / reference template args must have external linkage in C++98.
  if (Entity->getFormalLinkage() == Linkage::Internal) {
    S.Diag(Arg->getBeginLoc(),
           S.getLangOpts().CPlusPlus11
               ? diag::warn_cxx98_compat_template_arg_object_internal
               : diag::ext_template_arg_object_internal)
        << !Func << Entity << Arg->getSourceRange();
    S.Diag(Entity->getLocation(), diag::note_template_arg_internal_object)
      << !Func;
  } else if (!Entity->hasLinkage()) {
    S.Diag(Arg->getBeginLoc(), diag::err_template_arg_object_no_linkage)
        << !Func << Entity << Arg->getSourceRange();
    S.Diag(Entity->getLocation(), diag::note_template_arg_internal_object)
      << !Func;
    return true;
  }

  if (Var) {
    // A value of reference type is not an object.
    if (Var->getType()->isReferenceType()) {
      S.Diag(Arg->getBeginLoc(), diag::err_template_arg_reference_var)
          << Var->getType() << Arg->getSourceRange();
      S.NoteTemplateParameterLocation(*Param);
      return true;
    }

    // A template argument must have static storage duration.
    if (Var->getTLSKind()) {
      S.Diag(Arg->getBeginLoc(), diag::err_template_arg_thread_local)
          << Arg->getSourceRange();
      S.Diag(Var->getLocation(), diag::note_template_arg_refers_here);
      return true;
    }
  }

  if (AddressTaken && ParamType->isReferenceType()) {
    // If we originally had an address-of operator, but the
    // parameter has reference type, complain and (if things look
    // like they will work) drop the address-of operator.
    if (!S.Context.hasSameUnqualifiedType(Entity->getType(),
                                          ParamType.getNonReferenceType())) {
      S.Diag(AddrOpLoc, diag::err_template_arg_address_of_non_pointer)
        << ParamType;
      S.NoteTemplateParameterLocation(*Param);
      return true;
    }

    S.Diag(AddrOpLoc, diag::err_template_arg_address_of_non_pointer)
      << ParamType
      << FixItHint::CreateRemoval(AddrOpLoc);
    S.NoteTemplateParameterLocation(*Param);

    ArgType = Entity->getType();
  }

  // If the template parameter has pointer type, either we must have taken the
  // address or the argument must decay to a pointer.
  if (!AddressTaken && ParamType->isPointerType()) {
    if (Func) {
      // Function-to-pointer decay.
      ArgType = S.Context.getPointerType(Func->getType());
    } else if (Entity->getType()->isArrayType()) {
      // Array-to-pointer decay.
      ArgType = S.Context.getArrayDecayedType(Entity->getType());
    } else {
      // If the template parameter has pointer type but the address of
      // this object was not taken, complain and (possibly) recover by
      // taking the address of the entity.
      ArgType = S.Context.getPointerType(Entity->getType());
      if (!S.Context.hasSameUnqualifiedType(ArgType, ParamType)) {
        S.Diag(Arg->getBeginLoc(), diag::err_template_arg_not_address_of)
          << ParamType;
        S.NoteTemplateParameterLocation(*Param);
        return true;
      }

      S.Diag(Arg->getBeginLoc(), diag::err_template_arg_not_address_of)
        << ParamType << FixItHint::CreateInsertion(Arg->getBeginLoc(), "&");

      S.NoteTemplateParameterLocation(*Param);
    }
  }

  if (CheckTemplateArgumentIsCompatibleWithParameter(S, Param, ParamType, ArgIn,
                                                     Arg, ArgType))
    return true;

  // Create the template argument.
  SugaredConverted = TemplateArgument(Entity, ParamType);
  CanonicalConverted =
      TemplateArgument(cast<ValueDecl>(Entity->getCanonicalDecl()),
                       S.Context.getCanonicalType(ParamType));
  S.MarkAnyDeclReferenced(Arg->getBeginLoc(), Entity, false);
  return false;
}

/// Checks whether the given template argument is a pointer to
/// member constant according to C++ [temp.arg.nontype]p1.
static bool
CheckTemplateArgumentPointerToMember(Sema &S, NonTypeTemplateParmDecl *Param,
                                     QualType ParamType, Expr *&ResultArg,
                                     TemplateArgument &SugaredConverted,
                                     TemplateArgument &CanonicalConverted) {
  bool Invalid = false;

  Expr *Arg = ResultArg;
  bool ObjCLifetimeConversion;

  // C++ [temp.arg.nontype]p1:
  //
  //   A template-argument for a non-type, non-template
  //   template-parameter shall be one of: [...]
  //
  //     -- a pointer to member expressed as described in 5.3.1.
  DeclRefExpr *DRE = nullptr;

  // In C++98/03 mode, give an extension warning on any extra parentheses.
  // See http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_defects.html#773
  bool ExtraParens = false;
  while (ParenExpr *Parens = dyn_cast<ParenExpr>(Arg)) {
    if (!Invalid && !ExtraParens) {
      S.Diag(Arg->getBeginLoc(),
             S.getLangOpts().CPlusPlus11
                 ? diag::warn_cxx98_compat_template_arg_extra_parens
                 : diag::ext_template_arg_extra_parens)
          << Arg->getSourceRange();
      ExtraParens = true;
    }

    Arg = Parens->getSubExpr();
  }

  while (SubstNonTypeTemplateParmExpr *subst =
           dyn_cast<SubstNonTypeTemplateParmExpr>(Arg))
    Arg = subst->getReplacement()->IgnoreImpCasts();

  // A pointer-to-member constant written &Class::member.
  if (UnaryOperator *UnOp = dyn_cast<UnaryOperator>(Arg)) {
    if (UnOp->getOpcode() == UO_AddrOf) {
      DRE = dyn_cast<DeclRefExpr>(UnOp->getSubExpr());
      if (DRE && !DRE->getQualifier())
        DRE = nullptr;
    }
  }
  // A constant of pointer-to-member type.
  else if ((DRE = dyn_cast<DeclRefExpr>(Arg))) {
    ValueDecl *VD = DRE->getDecl();
    if (VD->getType()->isMemberPointerType()) {
      if (isa<NonTypeTemplateParmDecl>(VD)) {
        if (Arg->isTypeDependent() || Arg->isValueDependent()) {
          SugaredConverted = TemplateArgument(Arg);
          CanonicalConverted =
              S.Context.getCanonicalTemplateArgument(SugaredConverted);
        } else {
          SugaredConverted = TemplateArgument(VD, ParamType);
          CanonicalConverted =
              TemplateArgument(cast<ValueDecl>(VD->getCanonicalDecl()),
                               S.Context.getCanonicalType(ParamType));
        }
        return Invalid;
      }
    }

    DRE = nullptr;
  }

  ValueDecl *Entity = DRE ? DRE->getDecl() : nullptr;

  // Check for a null pointer value.
  switch (isNullPointerValueTemplateArgument(S, Param, ParamType, ResultArg,
                                             Entity)) {
  case NPV_Error:
    return true;
  case NPV_NullPointer:
    S.Diag(ResultArg->getExprLoc(), diag::warn_cxx98_compat_template_arg_null);
    SugaredConverted = TemplateArgument(ParamType,
                                        /*isNullPtr*/ true);
    CanonicalConverted = TemplateArgument(S.Context.getCanonicalType(ParamType),
                                          /*isNullPtr*/ true);
    return false;
  case NPV_NotNullPointer:
    break;
  }

  if (S.IsQualificationConversion(ResultArg->getType(),
                                  ParamType.getNonReferenceType(), false,
                                  ObjCLifetimeConversion)) {
    ResultArg = S.ImpCastExprToType(ResultArg, ParamType, CK_NoOp,
                                    ResultArg->getValueKind())
                    .get();
  } else if (!S.Context.hasSameUnqualifiedType(
                 ResultArg->getType(), ParamType.getNonReferenceType())) {
    // We can't perform this conversion.
    S.Diag(ResultArg->getBeginLoc(), diag::err_template_arg_not_convertible)
        << ResultArg->getType() << ParamType << ResultArg->getSourceRange();
    S.NoteTemplateParameterLocation(*Param);
    return true;
  }

  if (!DRE)
    return S.Diag(Arg->getBeginLoc(),
                  diag::err_template_arg_not_pointer_to_member_form)
           << Arg->getSourceRange();

  if (isa<FieldDecl>(DRE->getDecl()) ||
      isa<IndirectFieldDecl>(DRE->getDecl()) ||
      isa<CXXMethodDecl>(DRE->getDecl())) {
    assert((isa<FieldDecl>(DRE->getDecl()) ||
            isa<IndirectFieldDecl>(DRE->getDecl()) ||
            cast<CXXMethodDecl>(DRE->getDecl())
                ->isImplicitObjectMemberFunction()) &&
           "Only non-static member pointers can make it here");

    // Okay: this is the address of a non-static member, and therefore
    // a member pointer constant.
    if (Arg->isTypeDependent() || Arg->isValueDependent()) {
      SugaredConverted = TemplateArgument(Arg);
      CanonicalConverted =
          S.Context.getCanonicalTemplateArgument(SugaredConverted);
    } else {
      ValueDecl *D = DRE->getDecl();
      SugaredConverted = TemplateArgument(D, ParamType);
      CanonicalConverted =
          TemplateArgument(cast<ValueDecl>(D->getCanonicalDecl()),
                           S.Context.getCanonicalType(ParamType));
    }
    return Invalid;
  }

  // We found something else, but we don't know specifically what it is.
  S.Diag(Arg->getBeginLoc(), diag::err_template_arg_not_pointer_to_member_form)
      << Arg->getSourceRange();
  S.Diag(DRE->getDecl()->getLocation(), diag::note_template_arg_refers_here);
  return true;
}

ExprResult Sema::CheckTemplateArgument(NonTypeTemplateParmDecl *Param,
                                       QualType ParamType, Expr *Arg,
                                       TemplateArgument &SugaredConverted,
                                       TemplateArgument &CanonicalConverted,
                                       CheckTemplateArgumentKind CTAK) {
  SourceLocation StartLoc = Arg->getBeginLoc();

  // If the parameter type somehow involves auto, deduce the type now.
  DeducedType *DeducedT = ParamType->getContainedDeducedType();
  if (getLangOpts().CPlusPlus17 && DeducedT && !DeducedT->isDeduced()) {
    // During template argument deduction, we allow 'decltype(auto)' to
    // match an arbitrary dependent argument.
    // FIXME: The language rules don't say what happens in this case.
    // FIXME: We get an opaque dependent type out of decltype(auto) if the
    // expression is merely instantiation-dependent; is this enough?
    if (Arg->isTypeDependent()) {
      auto *AT = dyn_cast<AutoType>(DeducedT);
      if (AT && AT->isDecltypeAuto()) {
        SugaredConverted = TemplateArgument(Arg);
        CanonicalConverted = TemplateArgument(
            Context.getCanonicalTemplateArgument(SugaredConverted));
        return Arg;
      }
    }

    // When checking a deduced template argument, deduce from its type even if
    // the type is dependent, in order to check the types of non-type template
    // arguments line up properly in partial ordering.
    Expr *DeductionArg = Arg;
    if (auto *PE = dyn_cast<PackExpansionExpr>(DeductionArg))
      DeductionArg = PE->getPattern();
    TypeSourceInfo *TSI =
        Context.getTrivialTypeSourceInfo(ParamType, Param->getLocation());
    if (isa<DeducedTemplateSpecializationType>(DeducedT)) {
      InitializedEntity Entity =
          InitializedEntity::InitializeTemplateParameter(ParamType, Param);
      InitializationKind Kind = InitializationKind::CreateForInit(
          DeductionArg->getBeginLoc(), /*DirectInit*/false, DeductionArg);
      Expr *Inits[1] = {DeductionArg};
      ParamType =
          DeduceTemplateSpecializationFromInitializer(TSI, Entity, Kind, Inits);
      if (ParamType.isNull())
        return ExprError();
    } else {
      TemplateDeductionInfo Info(DeductionArg->getExprLoc(),
                                 Param->getDepth() + 1);
      ParamType = QualType();
      TemplateDeductionResult Result =
          DeduceAutoType(TSI->getTypeLoc(), DeductionArg, ParamType, Info,
                         /*DependentDeduction=*/true,
                         // We do not check constraints right now because the
                         // immediately-declared constraint of the auto type is
                         // also an associated constraint, and will be checked
                         // along with the other associated constraints after
                         // checking the template argument list.
                         /*IgnoreConstraints=*/true);
      if (Result == TemplateDeductionResult::AlreadyDiagnosed) {
        if (ParamType.isNull())
          return ExprError();
      } else if (Result != TemplateDeductionResult::Success) {
        Diag(Arg->getExprLoc(),
             diag::err_non_type_template_parm_type_deduction_failure)
            << Param->getDeclName() << Param->getType() << Arg->getType()
            << Arg->getSourceRange();
        NoteTemplateParameterLocation(*Param);
        return ExprError();
      }
    }
    // CheckNonTypeTemplateParameterType will produce a diagnostic if there's
    // an error. The error message normally references the parameter
    // declaration, but here we'll pass the argument location because that's
    // where the parameter type is deduced.
    ParamType = CheckNonTypeTemplateParameterType(ParamType, Arg->getExprLoc());
    if (ParamType.isNull()) {
      NoteTemplateParameterLocation(*Param);
      return ExprError();
    }
  }

  // We should have already dropped all cv-qualifiers by now.
  assert(!ParamType.hasQualifiers() &&
         "non-type template parameter type cannot be qualified");

  // FIXME: When Param is a reference, should we check that Arg is an lvalue?
  if (CTAK == CTAK_Deduced &&
      (ParamType->isReferenceType()
           ? !Context.hasSameType(ParamType.getNonReferenceType(),
                                  Arg->getType())
           : !Context.hasSameUnqualifiedType(ParamType, Arg->getType()))) {
    // FIXME: If either type is dependent, we skip the check. This isn't
    // correct, since during deduction we're supposed to have replaced each
    // template parameter with some unique (non-dependent) placeholder.
    // FIXME: If the argument type contains 'auto', we carry on and fail the
    // type check in order to force specific types to be more specialized than
    // 'auto'. It's not clear how partial ordering with 'auto' is supposed to
    // work. Similarly for CTAD, when comparing 'A<x>' against 'A'.
    if ((ParamType->isDependentType() || Arg->isTypeDependent()) &&
        !Arg->getType()->getContainedDeducedType()) {
      SugaredConverted = TemplateArgument(Arg);
      CanonicalConverted = TemplateArgument(
          Context.getCanonicalTemplateArgument(SugaredConverted));
      return Arg;
    }
    // FIXME: This attempts to implement C++ [temp.deduct.type]p17. Per DR1770,
    // we should actually be checking the type of the template argument in P,
    // not the type of the template argument deduced from A, against the
    // template parameter type.
    Diag(StartLoc, diag::err_deduced_non_type_template_arg_type_mismatch)
      << Arg->getType()
      << ParamType.getUnqualifiedType();
    NoteTemplateParameterLocation(*Param);
    return ExprError();
  }

  // If either the parameter has a dependent type or the argument is
  // type-dependent, there's nothing we can check now.
  if (ParamType->isDependentType() || Arg->isTypeDependent()) {
    // Force the argument to the type of the parameter to maintain invariants.
    auto *PE = dyn_cast<PackExpansionExpr>(Arg);
    if (PE)
      Arg = PE->getPattern();
    ExprResult E = ImpCastExprToType(
        Arg, ParamType.getNonLValueExprType(Context), CK_Dependent,
        ParamType->isLValueReferenceType()   ? VK_LValue
        : ParamType->isRValueReferenceType() ? VK_XValue
                                             : VK_PRValue);
    if (E.isInvalid())
      return ExprError();
    if (PE) {
      // Recreate a pack expansion if we unwrapped one.
      E = new (Context)
          PackExpansionExpr(E.get()->getType(), E.get(), PE->getEllipsisLoc(),
                            PE->getNumExpansions());
    }
    SugaredConverted = TemplateArgument(E.get());
    CanonicalConverted = TemplateArgument(
        Context.getCanonicalTemplateArgument(SugaredConverted));
    return E;
  }

  QualType CanonParamType = Context.getCanonicalType(ParamType);
  // Avoid making a copy when initializing a template parameter of class type
  // from a template parameter object of the same type. This is going beyond
  // the standard, but is required for soundness: in
  //   template<A a> struct X { X *p; X<a> *q; };
  // ... we need p and q to have the same type.
  //
  // Similarly, don't inject a call to a copy constructor when initializing
  // from a template parameter of the same type.
  Expr *InnerArg = Arg->IgnoreParenImpCasts();
  if (ParamType->isRecordType() && isa<DeclRefExpr>(InnerArg) &&
      Context.hasSameUnqualifiedType(ParamType, InnerArg->getType())) {
    NamedDecl *ND = cast<DeclRefExpr>(InnerArg)->getDecl();
    if (auto *TPO = dyn_cast<TemplateParamObjectDecl>(ND)) {

      SugaredConverted = TemplateArgument(TPO, ParamType);
      CanonicalConverted =
          TemplateArgument(TPO->getCanonicalDecl(), CanonParamType);
      return Arg;
    }
    if (isa<NonTypeTemplateParmDecl>(ND)) {
      SugaredConverted = TemplateArgument(Arg);
      CanonicalConverted =
          Context.getCanonicalTemplateArgument(SugaredConverted);
      return Arg;
    }
  }

  // The initialization of the parameter from the argument is
  // a constant-evaluated context.
  EnterExpressionEvaluationContext ConstantEvaluated(
      *this, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  bool IsConvertedConstantExpression = true;
  if (isa<InitListExpr>(Arg) || ParamType->isRecordType()) {
    InitializationKind Kind = InitializationKind::CreateForInit(
        Arg->getBeginLoc(), /*DirectInit=*/false, Arg);
    Expr *Inits[1] = {Arg};
    InitializedEntity Entity =
        InitializedEntity::InitializeTemplateParameter(ParamType, Param);
    InitializationSequence InitSeq(*this, Entity, Kind, Inits);
    ExprResult Result = InitSeq.Perform(*this, Entity, Kind, Inits);
    if (Result.isInvalid() || !Result.get())
      return ExprError();
    Result = ActOnConstantExpression(Result.get());
    if (Result.isInvalid() || !Result.get())
      return ExprError();
    Arg = ActOnFinishFullExpr(Result.get(), Arg->getBeginLoc(),
                              /*DiscardedValue=*/false,
                              /*IsConstexpr=*/true, /*IsTemplateArgument=*/true)
              .get();
    IsConvertedConstantExpression = false;
  }

  if (getLangOpts().CPlusPlus17) {
    // C++17 [temp.arg.nontype]p1:
    //   A template-argument for a non-type template parameter shall be
    //   a converted constant expression of the type of the template-parameter.
    APValue Value;
    ExprResult ArgResult;
    if (IsConvertedConstantExpression) {
      ArgResult = BuildConvertedConstantExpression(Arg, ParamType,
                                                   CCEK_TemplateArg, Param);
      if (ArgResult.isInvalid())
        return ExprError();
    } else {
      ArgResult = Arg;
    }

    // For a value-dependent argument, CheckConvertedConstantExpression is
    // permitted (and expected) to be unable to determine a value.
    if (ArgResult.get()->isValueDependent()) {
      SugaredConverted = TemplateArgument(ArgResult.get());
      CanonicalConverted =
          Context.getCanonicalTemplateArgument(SugaredConverted);
      return ArgResult;
    }

    APValue PreNarrowingValue;
    ArgResult = EvaluateConvertedConstantExpression(
        ArgResult.get(), ParamType, Value, CCEK_TemplateArg, /*RequireInt=*/
        false, PreNarrowingValue);
    if (ArgResult.isInvalid())
      return ExprError();

    if (Value.isLValue()) {
      APValue::LValueBase Base = Value.getLValueBase();
      auto *VD = const_cast<ValueDecl *>(Base.dyn_cast<const ValueDecl *>());
      //   For a non-type template-parameter of pointer or reference type,
      //   the value of the constant expression shall not refer to
      assert(ParamType->isPointerType() || ParamType->isReferenceType() ||
             ParamType->isNullPtrType());
      // -- a temporary object
      // -- a string literal
      // -- the result of a typeid expression, or
      // -- a predefined __func__ variable
      if (Base &&
          (!VD ||
           isa<LifetimeExtendedTemporaryDecl, UnnamedGlobalConstantDecl>(VD))) {
        Diag(Arg->getBeginLoc(), diag::err_template_arg_not_decl_ref)
            << Arg->getSourceRange();
        return ExprError();
      }

      if (Value.hasLValuePath() && Value.getLValuePath().size() == 1 && VD &&
          VD->getType()->isArrayType() &&
          Value.getLValuePath()[0].getAsArrayIndex() == 0 &&
          !Value.isLValueOnePastTheEnd() && ParamType->isPointerType()) {
        SugaredConverted = TemplateArgument(VD, ParamType);
        CanonicalConverted = TemplateArgument(
            cast<ValueDecl>(VD->getCanonicalDecl()), CanonParamType);
        return ArgResult.get();
      }

      // -- a subobject [until C++20]
      if (!getLangOpts().CPlusPlus20) {
        if (!Value.hasLValuePath() || Value.getLValuePath().size() ||
            Value.isLValueOnePastTheEnd()) {
          Diag(StartLoc, diag::err_non_type_template_arg_subobject)
              << Value.getAsString(Context, ParamType);
          return ExprError();
        }
        assert((VD || !ParamType->isReferenceType()) &&
               "null reference should not be a constant expression");
        assert((!VD || !ParamType->isNullPtrType()) &&
               "non-null value of type nullptr_t?");
      }
    }

    if (Value.isAddrLabelDiff())
      return Diag(StartLoc, diag::err_non_type_template_arg_addr_label_diff);

    SugaredConverted = TemplateArgument(Context, ParamType, Value);
    CanonicalConverted = TemplateArgument(Context, CanonParamType, Value);
    return ArgResult.get();
  }

  // C++ [temp.arg.nontype]p5:
  //   The following conversions are performed on each expression used
  //   as a non-type template-argument. If a non-type
  //   template-argument cannot be converted to the type of the
  //   corresponding template-parameter then the program is
  //   ill-formed.
  if (ParamType->isIntegralOrEnumerationType()) {
    // C++11:
    //   -- for a non-type template-parameter of integral or
    //      enumeration type, conversions permitted in a converted
    //      constant expression are applied.
    //
    // C++98:
    //   -- for a non-type template-parameter of integral or
    //      enumeration type, integral promotions (4.5) and integral
    //      conversions (4.7) are applied.

    if (getLangOpts().CPlusPlus11) {
      // C++ [temp.arg.nontype]p1:
      //   A template-argument for a non-type, non-template template-parameter
      //   shall be one of:
      //
      //     -- for a non-type template-parameter of integral or enumeration
      //        type, a converted constant expression of the type of the
      //        template-parameter; or
      llvm::APSInt Value;
      ExprResult ArgResult =
        CheckConvertedConstantExpression(Arg, ParamType, Value,
                                         CCEK_TemplateArg);
      if (ArgResult.isInvalid())
        return ExprError();

      // We can't check arbitrary value-dependent arguments.
      if (ArgResult.get()->isValueDependent()) {
        SugaredConverted = TemplateArgument(ArgResult.get());
        CanonicalConverted =
            Context.getCanonicalTemplateArgument(SugaredConverted);
        return ArgResult;
      }

      // Widen the argument value to sizeof(parameter type). This is almost
      // always a no-op, except when the parameter type is bool. In
      // that case, this may extend the argument from 1 bit to 8 bits.
      QualType IntegerType = ParamType;
      if (const EnumType *Enum = IntegerType->getAs<EnumType>())
        IntegerType = Enum->getDecl()->getIntegerType();
      Value = Value.extOrTrunc(IntegerType->isBitIntType()
                                   ? Context.getIntWidth(IntegerType)
                                   : Context.getTypeSize(IntegerType));

      SugaredConverted = TemplateArgument(Context, Value, ParamType);
      CanonicalConverted =
          TemplateArgument(Context, Value, Context.getCanonicalType(ParamType));
      return ArgResult;
    }

    ExprResult ArgResult = DefaultLvalueConversion(Arg);
    if (ArgResult.isInvalid())
      return ExprError();
    Arg = ArgResult.get();

    QualType ArgType = Arg->getType();

    // C++ [temp.arg.nontype]p1:
    //   A template-argument for a non-type, non-template
    //   template-parameter shall be one of:
    //
    //     -- an integral constant-expression of integral or enumeration
    //        type; or
    //     -- the name of a non-type template-parameter; or
    llvm::APSInt Value;
    if (!ArgType->isIntegralOrEnumerationType()) {
      Diag(Arg->getBeginLoc(), diag::err_template_arg_not_integral_or_enumeral)
          << ArgType << Arg->getSourceRange();
      NoteTemplateParameterLocation(*Param);
      return ExprError();
    } else if (!Arg->isValueDependent()) {
      class TmplArgICEDiagnoser : public VerifyICEDiagnoser {
        QualType T;

      public:
        TmplArgICEDiagnoser(QualType T) : T(T) { }

        SemaDiagnosticBuilder diagnoseNotICE(Sema &S,
                                             SourceLocation Loc) override {
          return S.Diag(Loc, diag::err_template_arg_not_ice) << T;
        }
      } Diagnoser(ArgType);

      Arg = VerifyIntegerConstantExpression(Arg, &Value, Diagnoser).get();
      if (!Arg)
        return ExprError();
    }

    // From here on out, all we care about is the unqualified form
    // of the argument type.
    ArgType = ArgType.getUnqualifiedType();

    // Try to convert the argument to the parameter's type.
    if (Context.hasSameType(ParamType, ArgType)) {
      // Okay: no conversion necessary
    } else if (ParamType->isBooleanType()) {
      // This is an integral-to-boolean conversion.
      Arg = ImpCastExprToType(Arg, ParamType, CK_IntegralToBoolean).get();
    } else if (IsIntegralPromotion(Arg, ArgType, ParamType) ||
               !ParamType->isEnumeralType()) {
      // This is an integral promotion or conversion.
      Arg = ImpCastExprToType(Arg, ParamType, CK_IntegralCast).get();
    } else {
      // We can't perform this conversion.
      Diag(Arg->getBeginLoc(), diag::err_template_arg_not_convertible)
          << Arg->getType() << ParamType << Arg->getSourceRange();
      NoteTemplateParameterLocation(*Param);
      return ExprError();
    }

    // Add the value of this argument to the list of converted
    // arguments. We use the bitwidth and signedness of the template
    // parameter.
    if (Arg->isValueDependent()) {
      // The argument is value-dependent. Create a new
      // TemplateArgument with the converted expression.
      SugaredConverted = TemplateArgument(Arg);
      CanonicalConverted =
          Context.getCanonicalTemplateArgument(SugaredConverted);
      return Arg;
    }

    QualType IntegerType = ParamType;
    if (const EnumType *Enum = IntegerType->getAs<EnumType>()) {
      IntegerType = Enum->getDecl()->getIntegerType();
    }

    if (ParamType->isBooleanType()) {
      // Value must be zero or one.
      Value = Value != 0;
      unsigned AllowedBits = Context.getTypeSize(IntegerType);
      if (Value.getBitWidth() != AllowedBits)
        Value = Value.extOrTrunc(AllowedBits);
      Value.setIsSigned(IntegerType->isSignedIntegerOrEnumerationType());
    } else {
      llvm::APSInt OldValue = Value;

      // Coerce the template argument's value to the value it will have
      // based on the template parameter's type.
      unsigned AllowedBits = IntegerType->isBitIntType()
                                 ? Context.getIntWidth(IntegerType)
                                 : Context.getTypeSize(IntegerType);
      if (Value.getBitWidth() != AllowedBits)
        Value = Value.extOrTrunc(AllowedBits);
      Value.setIsSigned(IntegerType->isSignedIntegerOrEnumerationType());

      // Complain if an unsigned parameter received a negative value.
      if (IntegerType->isUnsignedIntegerOrEnumerationType() &&
          (OldValue.isSigned() && OldValue.isNegative())) {
        Diag(Arg->getBeginLoc(), diag::warn_template_arg_negative)
            << toString(OldValue, 10) << toString(Value, 10) << Param->getType()
            << Arg->getSourceRange();
        NoteTemplateParameterLocation(*Param);
      }

      // Complain if we overflowed the template parameter's type.
      unsigned RequiredBits;
      if (IntegerType->isUnsignedIntegerOrEnumerationType())
        RequiredBits = OldValue.getActiveBits();
      else if (OldValue.isUnsigned())
        RequiredBits = OldValue.getActiveBits() + 1;
      else
        RequiredBits = OldValue.getSignificantBits();
      if (RequiredBits > AllowedBits) {
        Diag(Arg->getBeginLoc(), diag::warn_template_arg_too_large)
            << toString(OldValue, 10) << toString(Value, 10) << Param->getType()
            << Arg->getSourceRange();
        NoteTemplateParameterLocation(*Param);
      }
    }

    QualType T = ParamType->isEnumeralType() ? ParamType : IntegerType;
    SugaredConverted = TemplateArgument(Context, Value, T);
    CanonicalConverted =
        TemplateArgument(Context, Value, Context.getCanonicalType(T));
    return Arg;
  }

  QualType ArgType = Arg->getType();
  DeclAccessPair FoundResult; // temporary for ResolveOverloadedFunction

  // Handle pointer-to-function, reference-to-function, and
  // pointer-to-member-function all in (roughly) the same way.
  if (// -- For a non-type template-parameter of type pointer to
      //    function, only the function-to-pointer conversion (4.3) is
      //    applied. If the template-argument represents a set of
      //    overloaded functions (or a pointer to such), the matching
      //    function is selected from the set (13.4).
      (ParamType->isPointerType() &&
       ParamType->castAs<PointerType>()->getPointeeType()->isFunctionType()) ||
      // -- For a non-type template-parameter of type reference to
      //    function, no conversions apply. If the template-argument
      //    represents a set of overloaded functions, the matching
      //    function is selected from the set (13.4).
      (ParamType->isReferenceType() &&
       ParamType->castAs<ReferenceType>()->getPointeeType()->isFunctionType()) ||
      // -- For a non-type template-parameter of type pointer to
      //    member function, no conversions apply. If the
      //    template-argument represents a set of overloaded member
      //    functions, the matching member function is selected from
      //    the set (13.4).
      (ParamType->isMemberPointerType() &&
       ParamType->castAs<MemberPointerType>()->getPointeeType()
         ->isFunctionType())) {

    if (Arg->getType() == Context.OverloadTy) {
      if (FunctionDecl *Fn = ResolveAddressOfOverloadedFunction(Arg, ParamType,
                                                                true,
                                                                FoundResult)) {
        if (DiagnoseUseOfDecl(Fn, Arg->getBeginLoc()))
          return ExprError();

        ExprResult Res = FixOverloadedFunctionReference(Arg, FoundResult, Fn);
        if (Res.isInvalid())
          return ExprError();
        Arg = Res.get();
        ArgType = Arg->getType();
      } else
        return ExprError();
    }

    if (!ParamType->isMemberPointerType()) {
      if (CheckTemplateArgumentAddressOfObjectOrFunction(
              *this, Param, ParamType, Arg, SugaredConverted,
              CanonicalConverted))
        return ExprError();
      return Arg;
    }

    if (CheckTemplateArgumentPointerToMember(
            *this, Param, ParamType, Arg, SugaredConverted, CanonicalConverted))
      return ExprError();
    return Arg;
  }

  if (ParamType->isPointerType()) {
    //   -- for a non-type template-parameter of type pointer to
    //      object, qualification conversions (4.4) and the
    //      array-to-pointer conversion (4.2) are applied.
    // C++0x also allows a value of std::nullptr_t.
    assert(ParamType->getPointeeType()->isIncompleteOrObjectType() &&
           "Only object pointers allowed here");

    if (CheckTemplateArgumentAddressOfObjectOrFunction(
            *this, Param, ParamType, Arg, SugaredConverted, CanonicalConverted))
      return ExprError();
    return Arg;
  }

  if (const ReferenceType *ParamRefType = ParamType->getAs<ReferenceType>()) {
    //   -- For a non-type template-parameter of type reference to
    //      object, no conversions apply. The type referred to by the
    //      reference may be more cv-qualified than the (otherwise
    //      identical) type of the template-argument. The
    //      template-parameter is bound directly to the
    //      template-argument, which must be an lvalue.
    assert(ParamRefType->getPointeeType()->isIncompleteOrObjectType() &&
           "Only object references allowed here");

    if (Arg->getType() == Context.OverloadTy) {
      if (FunctionDecl *Fn = ResolveAddressOfOverloadedFunction(Arg,
                                                 ParamRefType->getPointeeType(),
                                                                true,
                                                                FoundResult)) {
        if (DiagnoseUseOfDecl(Fn, Arg->getBeginLoc()))
          return ExprError();
        ExprResult Res = FixOverloadedFunctionReference(Arg, FoundResult, Fn);
        if (Res.isInvalid())
          return ExprError();
        Arg = Res.get();
        ArgType = Arg->getType();
      } else
        return ExprError();
    }

    if (CheckTemplateArgumentAddressOfObjectOrFunction(
            *this, Param, ParamType, Arg, SugaredConverted, CanonicalConverted))
      return ExprError();
    return Arg;
  }

  // Deal with parameters of type std::nullptr_t.
  if (ParamType->isNullPtrType()) {
    if (Arg->isTypeDependent() || Arg->isValueDependent()) {
      SugaredConverted = TemplateArgument(Arg);
      CanonicalConverted =
          Context.getCanonicalTemplateArgument(SugaredConverted);
      return Arg;
    }

    switch (isNullPointerValueTemplateArgument(*this, Param, ParamType, Arg)) {
    case NPV_NotNullPointer:
      Diag(Arg->getExprLoc(), diag::err_template_arg_not_convertible)
        << Arg->getType() << ParamType;
      NoteTemplateParameterLocation(*Param);
      return ExprError();

    case NPV_Error:
      return ExprError();

    case NPV_NullPointer:
      Diag(Arg->getExprLoc(), diag::warn_cxx98_compat_template_arg_null);
      SugaredConverted = TemplateArgument(ParamType,
                                          /*isNullPtr=*/true);
      CanonicalConverted = TemplateArgument(Context.getCanonicalType(ParamType),
                                            /*isNullPtr=*/true);
      return Arg;
    }
  }

  //     -- For a non-type template-parameter of type pointer to data
  //        member, qualification conversions (4.4) are applied.
  assert(ParamType->isMemberPointerType() && "Only pointers to members remain");

  if (CheckTemplateArgumentPointerToMember(
          *this, Param, ParamType, Arg, SugaredConverted, CanonicalConverted))
    return ExprError();
  return Arg;
}

static void DiagnoseTemplateParameterListArityMismatch(
    Sema &S, TemplateParameterList *New, TemplateParameterList *Old,
    Sema::TemplateParameterListEqualKind Kind, SourceLocation TemplateArgLoc);

bool Sema::CheckTemplateTemplateArgument(TemplateTemplateParmDecl *Param,
                                         TemplateParameterList *Params,
                                         TemplateArgumentLoc &Arg,
                                         bool IsDeduced) {
  TemplateName Name = Arg.getArgument().getAsTemplateOrTemplatePattern();
  TemplateDecl *Template = Name.getAsTemplateDecl();
  if (!Template) {
    // Any dependent template name is fine.
    assert(Name.isDependent() && "Non-dependent template isn't a declaration?");
    return false;
  }

  if (Template->isInvalidDecl())
    return true;

  // C++0x [temp.arg.template]p1:
  //   A template-argument for a template template-parameter shall be
  //   the name of a class template or an alias template, expressed as an
  //   id-expression. When the template-argument names a class template, only
  //   primary class templates are considered when matching the
  //   template template argument with the corresponding parameter;
  //   partial specializations are not considered even if their
  //   parameter lists match that of the template template parameter.
  //
  // Note that we also allow template template parameters here, which
  // will happen when we are dealing with, e.g., class template
  // partial specializations.
  if (!isa<ClassTemplateDecl>(Template) &&
      !isa<TemplateTemplateParmDecl>(Template) &&
      !isa<TypeAliasTemplateDecl>(Template) &&
      !isa<BuiltinTemplateDecl>(Template)) {
    assert(isa<FunctionTemplateDecl>(Template) &&
           "Only function templates are possible here");
    Diag(Arg.getLocation(), diag::err_template_arg_not_valid_template);
    Diag(Template->getLocation(), diag::note_template_arg_refers_here_func)
      << Template;
  }

  // C++1z [temp.arg.template]p3: (DR 150)
  //   A template-argument matches a template template-parameter P when P
  //   is at least as specialized as the template-argument A.
  if (getLangOpts().RelaxedTemplateTemplateArgs) {
    // Quick check for the common case:
    //   If P contains a parameter pack, then A [...] matches P if each of A's
    //   template parameters matches the corresponding template parameter in
    //   the template-parameter-list of P.
    if (TemplateParameterListsAreEqual(
            Template->getTemplateParameters(), Params, false,
            TPL_TemplateTemplateArgumentMatch, Arg.getLocation()) &&
        // If the argument has no associated constraints, then the parameter is
        // definitely at least as specialized as the argument.
        // Otherwise - we need a more thorough check.
        !Template->hasAssociatedConstraints())
      return false;

    if (isTemplateTemplateParameterAtLeastAsSpecializedAs(
            Params, Template, Arg.getLocation(), IsDeduced)) {
      // P2113
      // C++20[temp.func.order]p2
      //   [...] If both deductions succeed, the partial ordering selects the
      // more constrained template (if one exists) as determined below.
      SmallVector<const Expr *, 3> ParamsAC, TemplateAC;
      Params->getAssociatedConstraints(ParamsAC);
      // C++2a[temp.arg.template]p3
      //   [...] In this comparison, if P is unconstrained, the constraints on A
      //   are not considered.
      if (ParamsAC.empty())
        return false;

      Template->getAssociatedConstraints(TemplateAC);

      bool IsParamAtLeastAsConstrained;
      if (IsAtLeastAsConstrained(Param, ParamsAC, Template, TemplateAC,
                                 IsParamAtLeastAsConstrained))
        return true;
      if (!IsParamAtLeastAsConstrained) {
        Diag(Arg.getLocation(),
             diag::err_template_template_parameter_not_at_least_as_constrained)
            << Template << Param << Arg.getSourceRange();
        Diag(Param->getLocation(), diag::note_entity_declared_at) << Param;
        Diag(Template->getLocation(), diag::note_entity_declared_at)
            << Template;
        MaybeEmitAmbiguousAtomicConstraintsDiagnostic(Param, ParamsAC, Template,
                                                      TemplateAC);
        return true;
      }
      return false;
    }
    // FIXME: Produce better diagnostics for deduction failures.
  }

  return !TemplateParameterListsAreEqual(Template->getTemplateParameters(),
                                         Params,
                                         true,
                                         TPL_TemplateTemplateArgumentMatch,
                                         Arg.getLocation());
}

static Sema::SemaDiagnosticBuilder noteLocation(Sema &S, const NamedDecl &Decl,
                                                unsigned HereDiagID,
                                                unsigned ExternalDiagID) {
  if (Decl.getLocation().isValid())
    return S.Diag(Decl.getLocation(), HereDiagID);

  SmallString<128> Str;
  llvm::raw_svector_ostream Out(Str);
  PrintingPolicy PP = S.getPrintingPolicy();
  PP.TerseOutput = 1;
  Decl.print(Out, PP);
  return S.Diag(Decl.getLocation(), ExternalDiagID) << Out.str();
}

void Sema::NoteTemplateLocation(const NamedDecl &Decl,
                                std::optional<SourceRange> ParamRange) {
  SemaDiagnosticBuilder DB =
      noteLocation(*this, Decl, diag::note_template_decl_here,
                   diag::note_template_decl_external);
  if (ParamRange && ParamRange->isValid()) {
    assert(Decl.getLocation().isValid() &&
           "Parameter range has location when Decl does not");
    DB << *ParamRange;
  }
}

void Sema::NoteTemplateParameterLocation(const NamedDecl &Decl) {
  noteLocation(*this, Decl, diag::note_template_param_here,
               diag::note_template_param_external);
}

ExprResult Sema::BuildExpressionFromDeclTemplateArgument(
    const TemplateArgument &Arg, QualType ParamType, SourceLocation Loc,
    NamedDecl *TemplateParam) {
  // C++ [temp.param]p8:
  //
  //   A non-type template-parameter of type "array of T" or
  //   "function returning T" is adjusted to be of type "pointer to
  //   T" or "pointer to function returning T", respectively.
  if (ParamType->isArrayType())
    ParamType = Context.getArrayDecayedType(ParamType);
  else if (ParamType->isFunctionType())
    ParamType = Context.getPointerType(ParamType);

  // For a NULL non-type template argument, return nullptr casted to the
  // parameter's type.
  if (Arg.getKind() == TemplateArgument::NullPtr) {
    return ImpCastExprToType(
             new (Context) CXXNullPtrLiteralExpr(Context.NullPtrTy, Loc),
                             ParamType,
                             ParamType->getAs<MemberPointerType>()
                               ? CK_NullToMemberPointer
                               : CK_NullToPointer);
  }
  assert(Arg.getKind() == TemplateArgument::Declaration &&
         "Only declaration template arguments permitted here");

  ValueDecl *VD = Arg.getAsDecl();

  CXXScopeSpec SS;
  if (ParamType->isMemberPointerType()) {
    // If this is a pointer to member, we need to use a qualified name to
    // form a suitable pointer-to-member constant.
    assert(VD->getDeclContext()->isRecord() &&
           (isa<CXXMethodDecl>(VD) || isa<FieldDecl>(VD) ||
            isa<IndirectFieldDecl>(VD)));
    QualType ClassType
      = Context.getTypeDeclType(cast<RecordDecl>(VD->getDeclContext()));
    NestedNameSpecifier *Qualifier
      = NestedNameSpecifier::Create(Context, nullptr, false,
                                    ClassType.getTypePtr());
    SS.MakeTrivial(Context, Qualifier, Loc);
  }

  ExprResult RefExpr = BuildDeclarationNameExpr(
      SS, DeclarationNameInfo(VD->getDeclName(), Loc), VD);
  if (RefExpr.isInvalid())
    return ExprError();

  // For a pointer, the argument declaration is the pointee. Take its address.
  QualType ElemT(RefExpr.get()->getType()->getArrayElementTypeNoTypeQual(), 0);
  if (ParamType->isPointerType() && !ElemT.isNull() &&
      Context.hasSimilarType(ElemT, ParamType->getPointeeType())) {
    // Decay an array argument if we want a pointer to its first element.
    RefExpr = DefaultFunctionArrayConversion(RefExpr.get());
    if (RefExpr.isInvalid())
      return ExprError();
  } else if (ParamType->isPointerType() || ParamType->isMemberPointerType()) {
    // For any other pointer, take the address (or form a pointer-to-member).
    RefExpr = CreateBuiltinUnaryOp(Loc, UO_AddrOf, RefExpr.get());
    if (RefExpr.isInvalid())
      return ExprError();
  } else if (ParamType->isRecordType()) {
    assert(isa<TemplateParamObjectDecl>(VD) &&
           "arg for class template param not a template parameter object");
    // No conversions apply in this case.
    return RefExpr;
  } else {
    assert(ParamType->isReferenceType() &&
           "unexpected type for decl template argument");
    if (NonTypeTemplateParmDecl *NTTP =
            dyn_cast_if_present<NonTypeTemplateParmDecl>(TemplateParam)) {
      QualType TemplateParamType = NTTP->getType();
      const AutoType *AT = TemplateParamType->getAs<AutoType>();
      if (AT && AT->isDecltypeAuto()) {
        RefExpr = new (getASTContext()) SubstNonTypeTemplateParmExpr(
            ParamType->getPointeeType(), RefExpr.get()->getValueKind(),
            RefExpr.get()->getExprLoc(), RefExpr.get(), VD, NTTP->getIndex(),
            /*PackIndex=*/std::nullopt,
            /*RefParam=*/true);
      }
    }
  }

  // At this point we should have the right value category.
  assert(ParamType->isReferenceType() == RefExpr.get()->isLValue() &&
         "value kind mismatch for non-type template argument");

  // The type of the template parameter can differ from the type of the
  // argument in various ways; convert it now if necessary.
  QualType DestExprType = ParamType.getNonLValueExprType(Context);
  if (!Context.hasSameType(RefExpr.get()->getType(), DestExprType)) {
    CastKind CK;
    QualType Ignored;
    if (Context.hasSimilarType(RefExpr.get()->getType(), DestExprType) ||
        IsFunctionConversion(RefExpr.get()->getType(), DestExprType, Ignored)) {
      CK = CK_NoOp;
    } else if (ParamType->isVoidPointerType() &&
               RefExpr.get()->getType()->isPointerType()) {
      CK = CK_BitCast;
    } else {
      // FIXME: Pointers to members can need conversion derived-to-base or
      // base-to-derived conversions. We currently don't retain enough
      // information to convert properly (we need to track a cast path or
      // subobject number in the template argument).
      llvm_unreachable(
          "unexpected conversion required for non-type template argument");
    }
    RefExpr = ImpCastExprToType(RefExpr.get(), DestExprType, CK,
                                RefExpr.get()->getValueKind());
  }

  return RefExpr;
}

/// Construct a new expression that refers to the given
/// integral template argument with the given source-location
/// information.
///
/// This routine takes care of the mapping from an integral template
/// argument (which may have any integral type) to the appropriate
/// literal value.
static Expr *BuildExpressionFromIntegralTemplateArgumentValue(
    Sema &S, QualType OrigT, const llvm::APSInt &Int, SourceLocation Loc) {
  assert(OrigT->isIntegralOrEnumerationType());

  // If this is an enum type that we're instantiating, we need to use an integer
  // type the same size as the enumerator.  We don't want to build an
  // IntegerLiteral with enum type.  The integer type of an enum type can be of
  // any integral type with C++11 enum classes, make sure we create the right
  // type of literal for it.
  QualType T = OrigT;
  if (const EnumType *ET = OrigT->getAs<EnumType>())
    T = ET->getDecl()->getIntegerType();

  Expr *E;
  if (T->isAnyCharacterType()) {
    CharacterLiteralKind Kind;
    if (T->isWideCharType())
      Kind = CharacterLiteralKind::Wide;
    else if (T->isChar8Type() && S.getLangOpts().Char8)
      Kind = CharacterLiteralKind::UTF8;
    else if (T->isChar16Type())
      Kind = CharacterLiteralKind::UTF16;
    else if (T->isChar32Type())
      Kind = CharacterLiteralKind::UTF32;
    else
      Kind = CharacterLiteralKind::Ascii;

    E = new (S.Context) CharacterLiteral(Int.getZExtValue(), Kind, T, Loc);
  } else if (T->isBooleanType()) {
    E = CXXBoolLiteralExpr::Create(S.Context, Int.getBoolValue(), T, Loc);
  } else {
    E = IntegerLiteral::Create(S.Context, Int, T, Loc);
  }

  if (OrigT->isEnumeralType()) {
    // FIXME: This is a hack. We need a better way to handle substituted
    // non-type template parameters.
    E = CStyleCastExpr::Create(S.Context, OrigT, VK_PRValue, CK_IntegralCast, E,
                               nullptr, S.CurFPFeatureOverrides(),
                               S.Context.getTrivialTypeSourceInfo(OrigT, Loc),
                               Loc, Loc);
  }

  return E;
}

static Expr *BuildExpressionFromNonTypeTemplateArgumentValue(
    Sema &S, QualType T, const APValue &Val, SourceLocation Loc) {
  auto MakeInitList = [&](ArrayRef<Expr *> Elts) -> Expr * {
    auto *ILE = new (S.Context) InitListExpr(S.Context, Loc, Elts, Loc);
    ILE->setType(T);
    return ILE;
  };

  switch (Val.getKind()) {
  case APValue::AddrLabelDiff:
    // This cannot occur in a template argument at all.
  case APValue::Array:
  case APValue::Struct:
  case APValue::Union:
    // These can only occur within a template parameter object, which is
    // represented as a TemplateArgument::Declaration.
    llvm_unreachable("unexpected template argument value");

  case APValue::Int:
    return BuildExpressionFromIntegralTemplateArgumentValue(S, T, Val.getInt(),
                                                            Loc);

  case APValue::Float:
    return FloatingLiteral::Create(S.Context, Val.getFloat(), /*IsExact=*/true,
                                   T, Loc);

  case APValue::FixedPoint:
    return FixedPointLiteral::CreateFromRawInt(
        S.Context, Val.getFixedPoint().getValue(), T, Loc,
        Val.getFixedPoint().getScale());

  case APValue::ComplexInt: {
    QualType ElemT = T->castAs<ComplexType>()->getElementType();
    return MakeInitList({BuildExpressionFromIntegralTemplateArgumentValue(
                             S, ElemT, Val.getComplexIntReal(), Loc),
                         BuildExpressionFromIntegralTemplateArgumentValue(
                             S, ElemT, Val.getComplexIntImag(), Loc)});
  }

  case APValue::ComplexFloat: {
    QualType ElemT = T->castAs<ComplexType>()->getElementType();
    return MakeInitList(
        {FloatingLiteral::Create(S.Context, Val.getComplexFloatReal(), true,
                                 ElemT, Loc),
         FloatingLiteral::Create(S.Context, Val.getComplexFloatImag(), true,
                                 ElemT, Loc)});
  }

  case APValue::Vector: {
    QualType ElemT = T->castAs<VectorType>()->getElementType();
    llvm::SmallVector<Expr *, 8> Elts;
    for (unsigned I = 0, N = Val.getVectorLength(); I != N; ++I)
      Elts.push_back(BuildExpressionFromNonTypeTemplateArgumentValue(
          S, ElemT, Val.getVectorElt(I), Loc));
    return MakeInitList(Elts);
  }

  case APValue::None:
  case APValue::Indeterminate:
    llvm_unreachable("Unexpected APValue kind.");
  case APValue::LValue:
  case APValue::MemberPointer:
    // There isn't necessarily a valid equivalent source-level syntax for
    // these; in particular, a naive lowering might violate access control.
    // So for now we lower to a ConstantExpr holding the value, wrapped around
    // an OpaqueValueExpr.
    // FIXME: We should have a better representation for this.
    ExprValueKind VK = VK_PRValue;
    if (T->isReferenceType()) {
      T = T->getPointeeType();
      VK = VK_LValue;
    }
    auto *OVE = new (S.Context) OpaqueValueExpr(Loc, T, VK);
    return ConstantExpr::Create(S.Context, OVE, Val);
  }
  llvm_unreachable("Unhandled APValue::ValueKind enum");
}

ExprResult
Sema::BuildExpressionFromNonTypeTemplateArgument(const TemplateArgument &Arg,
                                                 SourceLocation Loc) {
  switch (Arg.getKind()) {
  case TemplateArgument::Null:
  case TemplateArgument::Type:
  case TemplateArgument::Template:
  case TemplateArgument::TemplateExpansion:
  case TemplateArgument::Pack:
    llvm_unreachable("not a non-type template argument");

  case TemplateArgument::Expression:
    return Arg.getAsExpr();

  case TemplateArgument::NullPtr:
  case TemplateArgument::Declaration:
    return BuildExpressionFromDeclTemplateArgument(
        Arg, Arg.getNonTypeTemplateArgumentType(), Loc);

  case TemplateArgument::Integral:
    return BuildExpressionFromIntegralTemplateArgumentValue(
        *this, Arg.getIntegralType(), Arg.getAsIntegral(), Loc);

  case TemplateArgument::StructuralValue:
    return BuildExpressionFromNonTypeTemplateArgumentValue(
        *this, Arg.getStructuralValueType(), Arg.getAsStructuralValue(), Loc);
  }
  llvm_unreachable("Unhandled TemplateArgument::ArgKind enum");
}

/// Match two template parameters within template parameter lists.
static bool MatchTemplateParameterKind(
    Sema &S, NamedDecl *New,
    const Sema::TemplateCompareNewDeclInfo &NewInstFrom, NamedDecl *Old,
    const NamedDecl *OldInstFrom, bool Complain,
    Sema::TemplateParameterListEqualKind Kind, SourceLocation TemplateArgLoc) {
  // Check the actual kind (type, non-type, template).
  if (Old->getKind() != New->getKind()) {
    if (Complain) {
      unsigned NextDiag = diag::err_template_param_different_kind;
      if (TemplateArgLoc.isValid()) {
        S.Diag(TemplateArgLoc, diag::err_template_arg_template_params_mismatch);
        NextDiag = diag::note_template_param_different_kind;
      }
      S.Diag(New->getLocation(), NextDiag)
        << (Kind != Sema::TPL_TemplateMatch);
      S.Diag(Old->getLocation(), diag::note_template_prev_declaration)
        << (Kind != Sema::TPL_TemplateMatch);
    }

    return false;
  }

  // Check that both are parameter packs or neither are parameter packs.
  // However, if we are matching a template template argument to a
  // template template parameter, the template template parameter can have
  // a parameter pack where the template template argument does not.
  if (Old->isTemplateParameterPack() != New->isTemplateParameterPack() &&
      !(Kind == Sema::TPL_TemplateTemplateArgumentMatch &&
        Old->isTemplateParameterPack())) {
    if (Complain) {
      unsigned NextDiag = diag::err_template_parameter_pack_non_pack;
      if (TemplateArgLoc.isValid()) {
        S.Diag(TemplateArgLoc,
             diag::err_template_arg_template_params_mismatch);
        NextDiag = diag::note_template_parameter_pack_non_pack;
      }

      unsigned ParamKind = isa<TemplateTypeParmDecl>(New)? 0
                      : isa<NonTypeTemplateParmDecl>(New)? 1
                      : 2;
      S.Diag(New->getLocation(), NextDiag)
        << ParamKind << New->isParameterPack();
      S.Diag(Old->getLocation(), diag::note_template_parameter_pack_here)
        << ParamKind << Old->isParameterPack();
    }

    return false;
  }

  // For non-type template parameters, check the type of the parameter.
  if (NonTypeTemplateParmDecl *OldNTTP
                                    = dyn_cast<NonTypeTemplateParmDecl>(Old)) {
    NonTypeTemplateParmDecl *NewNTTP = cast<NonTypeTemplateParmDecl>(New);

    // If we are matching a template template argument to a template
    // template parameter and one of the non-type template parameter types
    // is dependent, then we must wait until template instantiation time
    // to actually compare the arguments.
    if (Kind != Sema::TPL_TemplateTemplateArgumentMatch ||
        (!OldNTTP->getType()->isDependentType() &&
         !NewNTTP->getType()->isDependentType())) {
      // C++20 [temp.over.link]p6:
      //   Two [non-type] template-parameters are equivalent [if] they have
      //   equivalent types ignoring the use of type-constraints for
      //   placeholder types
      QualType OldType = S.Context.getUnconstrainedType(OldNTTP->getType());
      QualType NewType = S.Context.getUnconstrainedType(NewNTTP->getType());
      if (!S.Context.hasSameType(OldType, NewType)) {
        if (Complain) {
          unsigned NextDiag = diag::err_template_nontype_parm_different_type;
          if (TemplateArgLoc.isValid()) {
            S.Diag(TemplateArgLoc,
                   diag::err_template_arg_template_params_mismatch);
            NextDiag = diag::note_template_nontype_parm_different_type;
          }
          S.Diag(NewNTTP->getLocation(), NextDiag)
            << NewNTTP->getType()
            << (Kind != Sema::TPL_TemplateMatch);
          S.Diag(OldNTTP->getLocation(),
                 diag::note_template_nontype_parm_prev_declaration)
            << OldNTTP->getType();
        }

        return false;
      }
    }
  }
  // For template template parameters, check the template parameter types.
  // The template parameter lists of template template
  // parameters must agree.
  else if (TemplateTemplateParmDecl *OldTTP =
               dyn_cast<TemplateTemplateParmDecl>(Old)) {
    TemplateTemplateParmDecl *NewTTP = cast<TemplateTemplateParmDecl>(New);
    if (!S.TemplateParameterListsAreEqual(
            NewInstFrom, NewTTP->getTemplateParameters(), OldInstFrom,
            OldTTP->getTemplateParameters(), Complain,
            (Kind == Sema::TPL_TemplateMatch
                 ? Sema::TPL_TemplateTemplateParmMatch
                 : Kind),
            TemplateArgLoc))
      return false;
  }

  if (Kind != Sema::TPL_TemplateParamsEquivalent &&
      Kind != Sema::TPL_TemplateTemplateArgumentMatch &&
      !isa<TemplateTemplateParmDecl>(Old)) {
    const Expr *NewC = nullptr, *OldC = nullptr;

    if (isa<TemplateTypeParmDecl>(New)) {
      if (const auto *TC = cast<TemplateTypeParmDecl>(New)->getTypeConstraint())
        NewC = TC->getImmediatelyDeclaredConstraint();
      if (const auto *TC = cast<TemplateTypeParmDecl>(Old)->getTypeConstraint())
        OldC = TC->getImmediatelyDeclaredConstraint();
    } else if (isa<NonTypeTemplateParmDecl>(New)) {
      if (const Expr *E = cast<NonTypeTemplateParmDecl>(New)
                              ->getPlaceholderTypeConstraint())
        NewC = E;
      if (const Expr *E = cast<NonTypeTemplateParmDecl>(Old)
                              ->getPlaceholderTypeConstraint())
        OldC = E;
    } else
      llvm_unreachable("unexpected template parameter type");

    auto Diagnose = [&] {
      S.Diag(NewC ? NewC->getBeginLoc() : New->getBeginLoc(),
           diag::err_template_different_type_constraint);
      S.Diag(OldC ? OldC->getBeginLoc() : Old->getBeginLoc(),
           diag::note_template_prev_declaration) << /*declaration*/0;
    };

    if (!NewC != !OldC) {
      if (Complain)
        Diagnose();
      return false;
    }

    if (NewC) {
      if (!S.AreConstraintExpressionsEqual(OldInstFrom, OldC, NewInstFrom,
                                           NewC)) {
        if (Complain)
          Diagnose();
        return false;
      }
    }
  }

  return true;
}

/// Diagnose a known arity mismatch when comparing template argument
/// lists.
static
void DiagnoseTemplateParameterListArityMismatch(Sema &S,
                                                TemplateParameterList *New,
                                                TemplateParameterList *Old,
                                      Sema::TemplateParameterListEqualKind Kind,
                                                SourceLocation TemplateArgLoc) {
  unsigned NextDiag = diag::err_template_param_list_different_arity;
  if (TemplateArgLoc.isValid()) {
    S.Diag(TemplateArgLoc, diag::err_template_arg_template_params_mismatch);
    NextDiag = diag::note_template_param_list_different_arity;
  }
  S.Diag(New->getTemplateLoc(), NextDiag)
    << (New->size() > Old->size())
    << (Kind != Sema::TPL_TemplateMatch)
    << SourceRange(New->getTemplateLoc(), New->getRAngleLoc());
  S.Diag(Old->getTemplateLoc(), diag::note_template_prev_declaration)
    << (Kind != Sema::TPL_TemplateMatch)
    << SourceRange(Old->getTemplateLoc(), Old->getRAngleLoc());
}

bool Sema::TemplateParameterListsAreEqual(
    const TemplateCompareNewDeclInfo &NewInstFrom, TemplateParameterList *New,
    const NamedDecl *OldInstFrom, TemplateParameterList *Old, bool Complain,
    TemplateParameterListEqualKind Kind, SourceLocation TemplateArgLoc) {
  if (Old->size() != New->size() && Kind != TPL_TemplateTemplateArgumentMatch) {
    if (Complain)
      DiagnoseTemplateParameterListArityMismatch(*this, New, Old, Kind,
                                                 TemplateArgLoc);

    return false;
  }

  // C++0x [temp.arg.template]p3:
  //   A template-argument matches a template template-parameter (call it P)
  //   when each of the template parameters in the template-parameter-list of
  //   the template-argument's corresponding class template or alias template
  //   (call it A) matches the corresponding template parameter in the
  //   template-parameter-list of P. [...]
  TemplateParameterList::iterator NewParm = New->begin();
  TemplateParameterList::iterator NewParmEnd = New->end();
  for (TemplateParameterList::iterator OldParm = Old->begin(),
                                    OldParmEnd = Old->end();
       OldParm != OldParmEnd; ++OldParm) {
    if (Kind != TPL_TemplateTemplateArgumentMatch ||
        !(*OldParm)->isTemplateParameterPack()) {
      if (NewParm == NewParmEnd) {
        if (Complain)
          DiagnoseTemplateParameterListArityMismatch(*this, New, Old, Kind,
                                                     TemplateArgLoc);

        return false;
      }

      if (!MatchTemplateParameterKind(*this, *NewParm, NewInstFrom, *OldParm,
                                      OldInstFrom, Complain, Kind,
                                      TemplateArgLoc))
        return false;

      ++NewParm;
      continue;
    }

    // C++0x [temp.arg.template]p3:
    //   [...] When P's template- parameter-list contains a template parameter
    //   pack (14.5.3), the template parameter pack will match zero or more
    //   template parameters or template parameter packs in the
    //   template-parameter-list of A with the same type and form as the
    //   template parameter pack in P (ignoring whether those template
    //   parameters are template parameter packs).
    for (; NewParm != NewParmEnd; ++NewParm) {
      if (!MatchTemplateParameterKind(*this, *NewParm, NewInstFrom, *OldParm,
                                      OldInstFrom, Complain, Kind,
                                      TemplateArgLoc))
        return false;
    }
  }

  // Make sure we exhausted all of the arguments.
  if (NewParm != NewParmEnd) {
    if (Complain)
      DiagnoseTemplateParameterListArityMismatch(*this, New, Old, Kind,
                                                 TemplateArgLoc);

    return false;
  }

  if (Kind != TPL_TemplateTemplateArgumentMatch &&
      Kind != TPL_TemplateParamsEquivalent) {
    const Expr *NewRC = New->getRequiresClause();
    const Expr *OldRC = Old->getRequiresClause();

    auto Diagnose = [&] {
      Diag(NewRC ? NewRC->getBeginLoc() : New->getTemplateLoc(),
           diag::err_template_different_requires_clause);
      Diag(OldRC ? OldRC->getBeginLoc() : Old->getTemplateLoc(),
           diag::note_template_prev_declaration) << /*declaration*/0;
    };

    if (!NewRC != !OldRC) {
      if (Complain)
        Diagnose();
      return false;
    }

    if (NewRC) {
      if (!AreConstraintExpressionsEqual(OldInstFrom, OldRC, NewInstFrom,
                                         NewRC)) {
        if (Complain)
          Diagnose();
        return false;
      }
    }
  }

  return true;
}

bool
Sema::CheckTemplateDeclScope(Scope *S, TemplateParameterList *TemplateParams) {
  if (!S)
    return false;

  // Find the nearest enclosing declaration scope.
  S = S->getDeclParent();

  // C++ [temp.pre]p6: [P2096]
  //   A template, explicit specialization, or partial specialization shall not
  //   have C linkage.
  DeclContext *Ctx = S->getEntity();
  if (Ctx && Ctx->isExternCContext()) {
    Diag(TemplateParams->getTemplateLoc(), diag::err_template_linkage)
        << TemplateParams->getSourceRange();
    if (const LinkageSpecDecl *LSD = Ctx->getExternCContext())
      Diag(LSD->getExternLoc(), diag::note_extern_c_begins_here);
    return true;
  }
  Ctx = Ctx ? Ctx->getRedeclContext() : nullptr;

  // C++ [temp]p2:
  //   A template-declaration can appear only as a namespace scope or
  //   class scope declaration.
  // C++ [temp.expl.spec]p3:
  //   An explicit specialization may be declared in any scope in which the
  //   corresponding primary template may be defined.
  // C++ [temp.class.spec]p6: [P2096]
  //   A partial specialization may be declared in any scope in which the
  //   corresponding primary template may be defined.
  if (Ctx) {
    if (Ctx->isFileContext())
      return false;
    if (CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(Ctx)) {
      // C++ [temp.mem]p2:
      //   A local class shall not have member templates.
      if (RD->isLocalClass())
        return Diag(TemplateParams->getTemplateLoc(),
                    diag::err_template_inside_local_class)
          << TemplateParams->getSourceRange();
      else
        return false;
    }
  }

  return Diag(TemplateParams->getTemplateLoc(),
              diag::err_template_outside_namespace_or_class_scope)
    << TemplateParams->getSourceRange();
}

/// Determine what kind of template specialization the given declaration
/// is.
static TemplateSpecializationKind getTemplateSpecializationKind(Decl *D) {
  if (!D)
    return TSK_Undeclared;

  if (CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(D))
    return Record->getTemplateSpecializationKind();
  if (FunctionDecl *Function = dyn_cast<FunctionDecl>(D))
    return Function->getTemplateSpecializationKind();
  if (VarDecl *Var = dyn_cast<VarDecl>(D))
    return Var->getTemplateSpecializationKind();

  return TSK_Undeclared;
}

/// Check whether a specialization is well-formed in the current
/// context.
///
/// This routine determines whether a template specialization can be declared
/// in the current context (C++ [temp.expl.spec]p2).
///
/// \param S the semantic analysis object for which this check is being
/// performed.
///
/// \param Specialized the entity being specialized or instantiated, which
/// may be a kind of template (class template, function template, etc.) or
/// a member of a class template (member function, static data member,
/// member class).
///
/// \param PrevDecl the previous declaration of this entity, if any.
///
/// \param Loc the location of the explicit specialization or instantiation of
/// this entity.
///
/// \param IsPartialSpecialization whether this is a partial specialization of
/// a class template.
///
/// \returns true if there was an error that we cannot recover from, false
/// otherwise.
static bool CheckTemplateSpecializationScope(Sema &S,
                                             NamedDecl *Specialized,
                                             NamedDecl *PrevDecl,
                                             SourceLocation Loc,
                                             bool IsPartialSpecialization) {
  // Keep these "kind" numbers in sync with the %select statements in the
  // various diagnostics emitted by this routine.
  int EntityKind = 0;
  if (isa<ClassTemplateDecl>(Specialized))
    EntityKind = IsPartialSpecialization? 1 : 0;
  else if (isa<VarTemplateDecl>(Specialized))
    EntityKind = IsPartialSpecialization ? 3 : 2;
  else if (isa<FunctionTemplateDecl>(Specialized))
    EntityKind = 4;
  else if (isa<CXXMethodDecl>(Specialized))
    EntityKind = 5;
  else if (isa<VarDecl>(Specialized))
    EntityKind = 6;
  else if (isa<RecordDecl>(Specialized))
    EntityKind = 7;
  else if (isa<EnumDecl>(Specialized) && S.getLangOpts().CPlusPlus11)
    EntityKind = 8;
  else {
    S.Diag(Loc, diag::err_template_spec_unknown_kind)
      << S.getLangOpts().CPlusPlus11;
    S.Diag(Specialized->getLocation(), diag::note_specialized_entity);
    return true;
  }

  // C++ [temp.expl.spec]p2:
  //   An explicit specialization may be declared in any scope in which
  //   the corresponding primary template may be defined.
  if (S.CurContext->getRedeclContext()->isFunctionOrMethod()) {
    S.Diag(Loc, diag::err_template_spec_decl_function_scope)
      << Specialized;
    return true;
  }

  // C++ [temp.class.spec]p6:
  //   A class template partial specialization may be declared in any
  //   scope in which the primary template may be defined.
  DeclContext *SpecializedContext =
      Specialized->getDeclContext()->getRedeclContext();
  DeclContext *DC = S.CurContext->getRedeclContext();

  // Make sure that this redeclaration (or definition) occurs in the same
  // scope or an enclosing namespace.
  if (!(DC->isFileContext() ? DC->Encloses(SpecializedContext)
                            : DC->Equals(SpecializedContext))) {
    if (isa<TranslationUnitDecl>(SpecializedContext))
      S.Diag(Loc, diag::err_template_spec_redecl_global_scope)
        << EntityKind << Specialized;
    else {
      auto *ND = cast<NamedDecl>(SpecializedContext);
      int Diag = diag::err_template_spec_redecl_out_of_scope;
      if (S.getLangOpts().MicrosoftExt && !DC->isRecord())
        Diag = diag::ext_ms_template_spec_redecl_out_of_scope;
      S.Diag(Loc, Diag) << EntityKind << Specialized
                        << ND << isa<CXXRecordDecl>(ND);
    }

    S.Diag(Specialized->getLocation(), diag::note_specialized_entity);

    // Don't allow specializing in the wrong class during error recovery.
    // Otherwise, things can go horribly wrong.
    if (DC->isRecord())
      return true;
  }

  return false;
}

static SourceRange findTemplateParameterInType(unsigned Depth, Expr *E) {
  if (!E->isTypeDependent())
    return SourceLocation();
  DependencyChecker Checker(Depth, /*IgnoreNonTypeDependent*/true);
  Checker.TraverseStmt(E);
  if (Checker.MatchLoc.isInvalid())
    return E->getSourceRange();
  return Checker.MatchLoc;
}

static SourceRange findTemplateParameter(unsigned Depth, TypeLoc TL) {
  if (!TL.getType()->isDependentType())
    return SourceLocation();
  DependencyChecker Checker(Depth, /*IgnoreNonTypeDependent*/true);
  Checker.TraverseTypeLoc(TL);
  if (Checker.MatchLoc.isInvalid())
    return TL.getSourceRange();
  return Checker.MatchLoc;
}

/// Subroutine of Sema::CheckTemplatePartialSpecializationArgs
/// that checks non-type template partial specialization arguments.
static bool CheckNonTypeTemplatePartialSpecializationArgs(
    Sema &S, SourceLocation TemplateNameLoc, NonTypeTemplateParmDecl *Param,
    const TemplateArgument *Args, unsigned NumArgs, bool IsDefaultArgument) {
  for (unsigned I = 0; I != NumArgs; ++I) {
    if (Args[I].getKind() == TemplateArgument::Pack) {
      if (CheckNonTypeTemplatePartialSpecializationArgs(
              S, TemplateNameLoc, Param, Args[I].pack_begin(),
              Args[I].pack_size(), IsDefaultArgument))
        return true;

      continue;
    }

    if (Args[I].getKind() != TemplateArgument::Expression)
      continue;

    Expr *ArgExpr = Args[I].getAsExpr();

    // We can have a pack expansion of any of the bullets below.
    if (PackExpansionExpr *Expansion = dyn_cast<PackExpansionExpr>(ArgExpr))
      ArgExpr = Expansion->getPattern();

    // Strip off any implicit casts we added as part of type checking.
    while (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(ArgExpr))
      ArgExpr = ICE->getSubExpr();

    // C++ [temp.class.spec]p8:
    //   A non-type argument is non-specialized if it is the name of a
    //   non-type parameter. All other non-type arguments are
    //   specialized.
    //
    // Below, we check the two conditions that only apply to
    // specialized non-type arguments, so skip any non-specialized
    // arguments.
    if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(ArgExpr))
      if (isa<NonTypeTemplateParmDecl>(DRE->getDecl()))
        continue;

    // C++ [temp.class.spec]p9:
    //   Within the argument list of a class template partial
    //   specialization, the following restrictions apply:
    //     -- A partially specialized non-type argument expression
    //        shall not involve a template parameter of the partial
    //        specialization except when the argument expression is a
    //        simple identifier.
    //     -- The type of a template parameter corresponding to a
    //        specialized non-type argument shall not be dependent on a
    //        parameter of the specialization.
    // DR1315 removes the first bullet, leaving an incoherent set of rules.
    // We implement a compromise between the original rules and DR1315:
    //     --  A specialized non-type template argument shall not be
    //         type-dependent and the corresponding template parameter
    //         shall have a non-dependent type.
    SourceRange ParamUseRange =
        findTemplateParameterInType(Param->getDepth(), ArgExpr);
    if (ParamUseRange.isValid()) {
      if (IsDefaultArgument) {
        S.Diag(TemplateNameLoc,
               diag::err_dependent_non_type_arg_in_partial_spec);
        S.Diag(ParamUseRange.getBegin(),
               diag::note_dependent_non_type_default_arg_in_partial_spec)
          << ParamUseRange;
      } else {
        S.Diag(ParamUseRange.getBegin(),
               diag::err_dependent_non_type_arg_in_partial_spec)
          << ParamUseRange;
      }
      return true;
    }

    ParamUseRange = findTemplateParameter(
        Param->getDepth(), Param->getTypeSourceInfo()->getTypeLoc());
    if (ParamUseRange.isValid()) {
      S.Diag(IsDefaultArgument ? TemplateNameLoc : ArgExpr->getBeginLoc(),
             diag::err_dependent_typed_non_type_arg_in_partial_spec)
          << Param->getType();
      S.NoteTemplateParameterLocation(*Param);
      return true;
    }
  }

  return false;
}

bool Sema::CheckTemplatePartialSpecializationArgs(
    SourceLocation TemplateNameLoc, TemplateDecl *PrimaryTemplate,
    unsigned NumExplicit, ArrayRef<TemplateArgument> TemplateArgs) {
  // We have to be conservative when checking a template in a dependent
  // context.
  if (PrimaryTemplate->getDeclContext()->isDependentContext())
    return false;

  TemplateParameterList *TemplateParams =
      PrimaryTemplate->getTemplateParameters();
  for (unsigned I = 0, N = TemplateParams->size(); I != N; ++I) {
    NonTypeTemplateParmDecl *Param
      = dyn_cast<NonTypeTemplateParmDecl>(TemplateParams->getParam(I));
    if (!Param)
      continue;

    if (CheckNonTypeTemplatePartialSpecializationArgs(*this, TemplateNameLoc,
                                                      Param, &TemplateArgs[I],
                                                      1, I >= NumExplicit))
      return true;
  }

  return false;
}

DeclResult Sema::ActOnClassTemplateSpecialization(
    Scope *S, unsigned TagSpec, TagUseKind TUK, SourceLocation KWLoc,
    SourceLocation ModulePrivateLoc, CXXScopeSpec &SS,
    TemplateIdAnnotation &TemplateId, const ParsedAttributesView &Attr,
    MultiTemplateParamsArg TemplateParameterLists, SkipBodyInfo *SkipBody) {
  assert(TUK != TagUseKind::Reference && "References are not specializations");

  SourceLocation TemplateNameLoc = TemplateId.TemplateNameLoc;
  SourceLocation LAngleLoc = TemplateId.LAngleLoc;
  SourceLocation RAngleLoc = TemplateId.RAngleLoc;

  // Find the class template we're specializing
  TemplateName Name = TemplateId.Template.get();
  ClassTemplateDecl *ClassTemplate
    = dyn_cast_or_null<ClassTemplateDecl>(Name.getAsTemplateDecl());

  if (!ClassTemplate) {
    Diag(TemplateNameLoc, diag::err_not_class_template_specialization)
      << (Name.getAsTemplateDecl() &&
          isa<TemplateTemplateParmDecl>(Name.getAsTemplateDecl()));
    return true;
  }

  bool isMemberSpecialization = false;
  bool isPartialSpecialization = false;

  if (SS.isSet()) {
    if (TUK != TagUseKind::Reference && TUK != TagUseKind::Friend &&
        diagnoseQualifiedDeclaration(SS, ClassTemplate->getDeclContext(),
                                     ClassTemplate->getDeclName(),
                                     TemplateNameLoc, &TemplateId,
                                     /*IsMemberSpecialization=*/false))
      return true;
  }

  // Check the validity of the template headers that introduce this
  // template.
  // FIXME: We probably shouldn't complain about these headers for
  // friend declarations.
  bool Invalid = false;
  TemplateParameterList *TemplateParams =
      MatchTemplateParametersToScopeSpecifier(
          KWLoc, TemplateNameLoc, SS, &TemplateId, TemplateParameterLists,
          TUK == TagUseKind::Friend, isMemberSpecialization, Invalid);
  if (Invalid)
    return true;

  // Check that we can declare a template specialization here.
  if (TemplateParams && CheckTemplateDeclScope(S, TemplateParams))
    return true;

  if (TemplateParams && TemplateParams->size() > 0) {
    isPartialSpecialization = true;

    if (TUK == TagUseKind::Friend) {
      Diag(KWLoc, diag::err_partial_specialization_friend)
        << SourceRange(LAngleLoc, RAngleLoc);
      return true;
    }

    // C++ [temp.class.spec]p10:
    //   The template parameter list of a specialization shall not
    //   contain default template argument values.
    for (unsigned I = 0, N = TemplateParams->size(); I != N; ++I) {
      Decl *Param = TemplateParams->getParam(I);
      if (TemplateTypeParmDecl *TTP = dyn_cast<TemplateTypeParmDecl>(Param)) {
        if (TTP->hasDefaultArgument()) {
          Diag(TTP->getDefaultArgumentLoc(),
               diag::err_default_arg_in_partial_spec);
          TTP->removeDefaultArgument();
        }
      } else if (NonTypeTemplateParmDecl *NTTP
                   = dyn_cast<NonTypeTemplateParmDecl>(Param)) {
        if (NTTP->hasDefaultArgument()) {
          Diag(NTTP->getDefaultArgumentLoc(),
               diag::err_default_arg_in_partial_spec)
              << NTTP->getDefaultArgument().getSourceRange();
          NTTP->removeDefaultArgument();
        }
      } else {
        TemplateTemplateParmDecl *TTP = cast<TemplateTemplateParmDecl>(Param);
        if (TTP->hasDefaultArgument()) {
          Diag(TTP->getDefaultArgument().getLocation(),
               diag::err_default_arg_in_partial_spec)
            << TTP->getDefaultArgument().getSourceRange();
          TTP->removeDefaultArgument();
        }
      }
    }
  } else if (TemplateParams) {
    if (TUK == TagUseKind::Friend)
      Diag(KWLoc, diag::err_template_spec_friend)
        << FixItHint::CreateRemoval(
                                SourceRange(TemplateParams->getTemplateLoc(),
                                            TemplateParams->getRAngleLoc()))
        << SourceRange(LAngleLoc, RAngleLoc);
  } else {
    assert(TUK == TagUseKind::Friend &&
           "should have a 'template<>' for this decl");
  }

  // Check that the specialization uses the same tag kind as the
  // original template.
  TagTypeKind Kind = TypeWithKeyword::getTagTypeKindForTypeSpec(TagSpec);
  assert(Kind != TagTypeKind::Enum &&
         "Invalid enum tag in class template spec!");
  if (!isAcceptableTagRedeclaration(ClassTemplate->getTemplatedDecl(), Kind,
                                    TUK == TagUseKind::Definition, KWLoc,
                                    ClassTemplate->getIdentifier())) {
    Diag(KWLoc, diag::err_use_with_wrong_tag)
      << ClassTemplate
      << FixItHint::CreateReplacement(KWLoc,
                            ClassTemplate->getTemplatedDecl()->getKindName());
    Diag(ClassTemplate->getTemplatedDecl()->getLocation(),
         diag::note_previous_use);
    Kind = ClassTemplate->getTemplatedDecl()->getTagKind();
  }

  // Translate the parser's template argument list in our AST format.
  TemplateArgumentListInfo TemplateArgs =
      makeTemplateArgumentListInfo(*this, TemplateId);

  // Check for unexpanded parameter packs in any of the template arguments.
  for (unsigned I = 0, N = TemplateArgs.size(); I != N; ++I)
    if (DiagnoseUnexpandedParameterPack(TemplateArgs[I],
                                        isPartialSpecialization
                                            ? UPPC_PartialSpecialization
                                            : UPPC_ExplicitSpecialization))
      return true;

  // Check that the template argument list is well-formed for this
  // template.
  SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;
  if (CheckTemplateArgumentList(ClassTemplate, TemplateNameLoc, TemplateArgs,
                                false, SugaredConverted, CanonicalConverted,
                                /*UpdateArgsWithConversions=*/true))
    return true;

  // Find the class template (partial) specialization declaration that
  // corresponds to these arguments.
  if (isPartialSpecialization) {
    if (CheckTemplatePartialSpecializationArgs(TemplateNameLoc, ClassTemplate,
                                               TemplateArgs.size(),
                                               CanonicalConverted))
      return true;

    // FIXME: Move this to CheckTemplatePartialSpecializationArgs so we
    // also do it during instantiation.
    if (!Name.isDependent() &&
        !TemplateSpecializationType::anyDependentTemplateArguments(
            TemplateArgs, CanonicalConverted)) {
      Diag(TemplateNameLoc, diag::err_partial_spec_fully_specialized)
        << ClassTemplate->getDeclName();
      isPartialSpecialization = false;
      Invalid = true;
    }
  }

  void *InsertPos = nullptr;
  ClassTemplateSpecializationDecl *PrevDecl = nullptr;

  if (isPartialSpecialization)
    PrevDecl = ClassTemplate->findPartialSpecialization(
        CanonicalConverted, TemplateParams, InsertPos);
  else
    PrevDecl = ClassTemplate->findSpecialization(CanonicalConverted, InsertPos);

  ClassTemplateSpecializationDecl *Specialization = nullptr;

  // Check whether we can declare a class template specialization in
  // the current scope.
  if (TUK != TagUseKind::Friend &&
      CheckTemplateSpecializationScope(*this, ClassTemplate, PrevDecl,
                                       TemplateNameLoc,
                                       isPartialSpecialization))
    return true;

  // The canonical type
  QualType CanonType;
  if (isPartialSpecialization) {
    // Build the canonical type that describes the converted template
    // arguments of the class template partial specialization.
    TemplateName CanonTemplate = Context.getCanonicalTemplateName(Name);
    CanonType = Context.getTemplateSpecializationType(CanonTemplate,
                                                      CanonicalConverted);

    if (Context.hasSameType(CanonType,
                        ClassTemplate->getInjectedClassNameSpecialization()) &&
        (!Context.getLangOpts().CPlusPlus20 ||
         !TemplateParams->hasAssociatedConstraints())) {
      // C++ [temp.class.spec]p9b3:
      //
      //   -- The argument list of the specialization shall not be identical
      //      to the implicit argument list of the primary template.
      //
      // This rule has since been removed, because it's redundant given DR1495,
      // but we keep it because it produces better diagnostics and recovery.
      Diag(TemplateNameLoc, diag::err_partial_spec_args_match_primary_template)
          << /*class template*/ 0 << (TUK == TagUseKind::Definition)
          << FixItHint::CreateRemoval(SourceRange(LAngleLoc, RAngleLoc));
      return CheckClassTemplate(S, TagSpec, TUK, KWLoc, SS,
                                ClassTemplate->getIdentifier(),
                                TemplateNameLoc,
                                Attr,
                                TemplateParams,
                                AS_none, /*ModulePrivateLoc=*/SourceLocation(),
                                /*FriendLoc*/SourceLocation(),
                                TemplateParameterLists.size() - 1,
                                TemplateParameterLists.data());
    }

    // Create a new class template partial specialization declaration node.
    ClassTemplatePartialSpecializationDecl *PrevPartial
      = cast_or_null<ClassTemplatePartialSpecializationDecl>(PrevDecl);
    ClassTemplatePartialSpecializationDecl *Partial =
        ClassTemplatePartialSpecializationDecl::Create(
            Context, Kind, ClassTemplate->getDeclContext(), KWLoc,
            TemplateNameLoc, TemplateParams, ClassTemplate, CanonicalConverted,
            CanonType, PrevPartial);
    Partial->setTemplateArgsAsWritten(TemplateArgs);
    SetNestedNameSpecifier(*this, Partial, SS);
    if (TemplateParameterLists.size() > 1 && SS.isSet()) {
      Partial->setTemplateParameterListsInfo(
          Context, TemplateParameterLists.drop_back(1));
    }

    if (!PrevPartial)
      ClassTemplate->AddPartialSpecialization(Partial, InsertPos);
    Specialization = Partial;

    // If we are providing an explicit specialization of a member class
    // template specialization, make a note of that.
    if (PrevPartial && PrevPartial->getInstantiatedFromMember())
      PrevPartial->setMemberSpecialization();

    CheckTemplatePartialSpecialization(Partial);
  } else {
    // Create a new class template specialization declaration node for
    // this explicit specialization or friend declaration.
    Specialization = ClassTemplateSpecializationDecl::Create(
        Context, Kind, ClassTemplate->getDeclContext(), KWLoc, TemplateNameLoc,
        ClassTemplate, CanonicalConverted, PrevDecl);
    Specialization->setTemplateArgsAsWritten(TemplateArgs);
    SetNestedNameSpecifier(*this, Specialization, SS);
    if (TemplateParameterLists.size() > 0) {
      Specialization->setTemplateParameterListsInfo(Context,
                                                    TemplateParameterLists);
    }

    if (!PrevDecl)
      ClassTemplate->AddSpecialization(Specialization, InsertPos);

    if (CurContext->isDependentContext()) {
      TemplateName CanonTemplate = Context.getCanonicalTemplateName(Name);
      CanonType = Context.getTemplateSpecializationType(CanonTemplate,
                                                        CanonicalConverted);
    } else {
      CanonType = Context.getTypeDeclType(Specialization);
    }
  }

  // C++ [temp.expl.spec]p6:
  //   If a template, a member template or the member of a class template is
  //   explicitly specialized then that specialization shall be declared
  //   before the first use of that specialization that would cause an implicit
  //   instantiation to take place, in every translation unit in which such a
  //   use occurs; no diagnostic is required.
  if (PrevDecl && PrevDecl->getPointOfInstantiation().isValid()) {
    bool Okay = false;
    for (Decl *Prev = PrevDecl; Prev; Prev = Prev->getPreviousDecl()) {
      // Is there any previous explicit specialization declaration?
      if (getTemplateSpecializationKind(Prev) == TSK_ExplicitSpecialization) {
        Okay = true;
        break;
      }
    }

    if (!Okay) {
      SourceRange Range(TemplateNameLoc, RAngleLoc);
      Diag(TemplateNameLoc, diag::err_specialization_after_instantiation)
        << Context.getTypeDeclType(Specialization) << Range;

      Diag(PrevDecl->getPointOfInstantiation(),
           diag::note_instantiation_required_here)
        << (PrevDecl->getTemplateSpecializationKind()
                                                != TSK_ImplicitInstantiation);
      return true;
    }
  }

  // If this is not a friend, note that this is an explicit specialization.
  if (TUK != TagUseKind::Friend)
    Specialization->setSpecializationKind(TSK_ExplicitSpecialization);

  // Check that this isn't a redefinition of this specialization.
  if (TUK == TagUseKind::Definition) {
    RecordDecl *Def = Specialization->getDefinition();
    NamedDecl *Hidden = nullptr;
    if (Def && SkipBody && !hasVisibleDefinition(Def, &Hidden)) {
      SkipBody->ShouldSkip = true;
      SkipBody->Previous = Def;
      makeMergedDefinitionVisible(Hidden);
    } else if (Def) {
      SourceRange Range(TemplateNameLoc, RAngleLoc);
      Diag(TemplateNameLoc, diag::err_redefinition) << Specialization << Range;
      Diag(Def->getLocation(), diag::note_previous_definition);
      Specialization->setInvalidDecl();
      return true;
    }
  }

  ProcessDeclAttributeList(S, Specialization, Attr);
  ProcessAPINotes(Specialization);

  // Add alignment attributes if necessary; these attributes are checked when
  // the ASTContext lays out the structure.
  if (TUK == TagUseKind::Definition && (!SkipBody || !SkipBody->ShouldSkip)) {
    AddAlignmentAttributesForRecord(Specialization);
    AddMsStructLayoutForRecord(Specialization);
  }

  if (ModulePrivateLoc.isValid())
    Diag(Specialization->getLocation(), diag::err_module_private_specialization)
      << (isPartialSpecialization? 1 : 0)
      << FixItHint::CreateRemoval(ModulePrivateLoc);

  // C++ [temp.expl.spec]p9:
  //   A template explicit specialization is in the scope of the
  //   namespace in which the template was defined.
  //
  // We actually implement this paragraph where we set the semantic
  // context (in the creation of the ClassTemplateSpecializationDecl),
  // but we also maintain the lexical context where the actual
  // definition occurs.
  Specialization->setLexicalDeclContext(CurContext);

  // We may be starting the definition of this specialization.
  if (TUK == TagUseKind::Definition && (!SkipBody || !SkipBody->ShouldSkip))
    Specialization->startDefinition();

  if (TUK == TagUseKind::Friend) {
    // Build the fully-sugared type for this class template
    // specialization as the user wrote in the specialization
    // itself. This means that we'll pretty-print the type retrieved
    // from the specialization's declaration the way that the user
    // actually wrote the specialization, rather than formatting the
    // name based on the "canonical" representation used to store the
    // template arguments in the specialization.
    TypeSourceInfo *WrittenTy = Context.getTemplateSpecializationTypeInfo(
        Name, TemplateNameLoc, TemplateArgs, CanonType);
    FriendDecl *Friend = FriendDecl::Create(Context, CurContext,
                                            TemplateNameLoc,
                                            WrittenTy,
                                            /*FIXME:*/KWLoc);
    Friend->setAccess(AS_public);
    CurContext->addDecl(Friend);
  } else {
    // Add the specialization into its lexical context, so that it can
    // be seen when iterating through the list of declarations in that
    // context. However, specializations are not found by name lookup.
    CurContext->addDecl(Specialization);
  }

  if (SkipBody && SkipBody->ShouldSkip)
    return SkipBody->Previous;

  Specialization->setInvalidDecl(Invalid);
  return Specialization;
}

Decl *Sema::ActOnTemplateDeclarator(Scope *S,
                              MultiTemplateParamsArg TemplateParameterLists,
                                    Declarator &D) {
  Decl *NewDecl = HandleDeclarator(S, D, TemplateParameterLists);
  ActOnDocumentableDecl(NewDecl);
  return NewDecl;
}

Decl *Sema::ActOnConceptDefinition(
    Scope *S, MultiTemplateParamsArg TemplateParameterLists,
    const IdentifierInfo *Name, SourceLocation NameLoc, Expr *ConstraintExpr,
    const ParsedAttributesView &Attrs) {
  DeclContext *DC = CurContext;

  if (!DC->getRedeclContext()->isFileContext()) {
    Diag(NameLoc,
      diag::err_concept_decls_may_only_appear_in_global_namespace_scope);
    return nullptr;
  }

  if (TemplateParameterLists.size() > 1) {
    Diag(NameLoc, diag::err_concept_extra_headers);
    return nullptr;
  }

  TemplateParameterList *Params = TemplateParameterLists.front();

  if (Params->size() == 0) {
    Diag(NameLoc, diag::err_concept_no_parameters);
    return nullptr;
  }

  // Ensure that the parameter pack, if present, is the last parameter in the
  // template.
  for (TemplateParameterList::const_iterator ParamIt = Params->begin(),
                                             ParamEnd = Params->end();
       ParamIt != ParamEnd; ++ParamIt) {
    Decl const *Param = *ParamIt;
    if (Param->isParameterPack()) {
      if (++ParamIt == ParamEnd)
        break;
      Diag(Param->getLocation(),
           diag::err_template_param_pack_must_be_last_template_parameter);
      return nullptr;
    }
  }

  if (DiagnoseUnexpandedParameterPack(ConstraintExpr))
    return nullptr;

  ConceptDecl *NewDecl =
      ConceptDecl::Create(Context, DC, NameLoc, Name, Params, ConstraintExpr);

  if (NewDecl->hasAssociatedConstraints()) {
    // C++2a [temp.concept]p4:
    // A concept shall not have associated constraints.
    Diag(NameLoc, diag::err_concept_no_associated_constraints);
    NewDecl->setInvalidDecl();
  }

  // Check for conflicting previous declaration.
  DeclarationNameInfo NameInfo(NewDecl->getDeclName(), NameLoc);
  LookupResult Previous(*this, NameInfo, LookupOrdinaryName,
                        forRedeclarationInCurContext());
  LookupName(Previous, S);
  FilterLookupForScope(Previous, DC, S, /*ConsiderLinkage=*/false,
                       /*AllowInlineNamespace*/false);
  bool AddToScope = true;
  CheckConceptRedefinition(NewDecl, Previous, AddToScope);

  ActOnDocumentableDecl(NewDecl);
  if (AddToScope)
    PushOnScopeChains(NewDecl, S);

  ProcessDeclAttributeList(S, NewDecl, Attrs);

  return NewDecl;
}

void Sema::CheckConceptRedefinition(ConceptDecl *NewDecl,
                                    LookupResult &Previous, bool &AddToScope) {
  AddToScope = true;

  if (Previous.empty())
    return;

  auto *OldConcept = dyn_cast<ConceptDecl>(Previous.getRepresentativeDecl()->getUnderlyingDecl());
  if (!OldConcept) {
    auto *Old = Previous.getRepresentativeDecl();
    Diag(NewDecl->getLocation(), diag::err_redefinition_different_kind)
        << NewDecl->getDeclName();
    notePreviousDefinition(Old, NewDecl->getLocation());
    AddToScope = false;
    return;
  }
  // Check if we can merge with a concept declaration.
  bool IsSame = Context.isSameEntity(NewDecl, OldConcept);
  if (!IsSame) {
    Diag(NewDecl->getLocation(), diag::err_redefinition_different_concept)
        << NewDecl->getDeclName();
    notePreviousDefinition(OldConcept, NewDecl->getLocation());
    AddToScope = false;
    return;
  }
  if (hasReachableDefinition(OldConcept) &&
      IsRedefinitionInModule(NewDecl, OldConcept)) {
    Diag(NewDecl->getLocation(), diag::err_redefinition)
        << NewDecl->getDeclName();
    notePreviousDefinition(OldConcept, NewDecl->getLocation());
    AddToScope = false;
    return;
  }
  if (!Previous.isSingleResult()) {
    // FIXME: we should produce an error in case of ambig and failed lookups.
    //        Other decls (e.g. namespaces) also have this shortcoming.
    return;
  }
  // We unwrap canonical decl late to check for module visibility.
  Context.setPrimaryMergedDecl(NewDecl, OldConcept->getCanonicalDecl());
}

/// \brief Strips various properties off an implicit instantiation
/// that has just been explicitly specialized.
static void StripImplicitInstantiation(NamedDecl *D, bool MinGW) {
  if (MinGW || (isa<FunctionDecl>(D) &&
                cast<FunctionDecl>(D)->isFunctionTemplateSpecialization()))
    D->dropAttrs<DLLImportAttr, DLLExportAttr>();

  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
    FD->setInlineSpecified(false);
}

/// Compute the diagnostic location for an explicit instantiation
//  declaration or definition.
static SourceLocation DiagLocForExplicitInstantiation(
    NamedDecl* D, SourceLocation PointOfInstantiation) {
  // Explicit instantiations following a specialization have no effect and
  // hence no PointOfInstantiation. In that case, walk decl backwards
  // until a valid name loc is found.
  SourceLocation PrevDiagLoc = PointOfInstantiation;
  for (Decl *Prev = D; Prev && !PrevDiagLoc.isValid();
       Prev = Prev->getPreviousDecl()) {
    PrevDiagLoc = Prev->getLocation();
  }
  assert(PrevDiagLoc.isValid() &&
         "Explicit instantiation without point of instantiation?");
  return PrevDiagLoc;
}

bool
Sema::CheckSpecializationInstantiationRedecl(SourceLocation NewLoc,
                                             TemplateSpecializationKind NewTSK,
                                             NamedDecl *PrevDecl,
                                             TemplateSpecializationKind PrevTSK,
                                        SourceLocation PrevPointOfInstantiation,
                                             bool &HasNoEffect) {
  HasNoEffect = false;

  switch (NewTSK) {
  case TSK_Undeclared:
  case TSK_ImplicitInstantiation:
    assert(
        (PrevTSK == TSK_Undeclared || PrevTSK == TSK_ImplicitInstantiation) &&
        "previous declaration must be implicit!");
    return false;

  case TSK_ExplicitSpecialization:
    switch (PrevTSK) {
    case TSK_Undeclared:
    case TSK_ExplicitSpecialization:
      // Okay, we're just specializing something that is either already
      // explicitly specialized or has merely been mentioned without any
      // instantiation.
      return false;

    case TSK_ImplicitInstantiation:
      if (PrevPointOfInstantiation.isInvalid()) {
        // The declaration itself has not actually been instantiated, so it is
        // still okay to specialize it.
        StripImplicitInstantiation(
            PrevDecl,
            Context.getTargetInfo().getTriple().isWindowsGNUEnvironment());
        return false;
      }
      // Fall through
      [[fallthrough]];

    case TSK_ExplicitInstantiationDeclaration:
    case TSK_ExplicitInstantiationDefinition:
      assert((PrevTSK == TSK_ImplicitInstantiation ||
              PrevPointOfInstantiation.isValid()) &&
             "Explicit instantiation without point of instantiation?");

      // C++ [temp.expl.spec]p6:
      //   If a template, a member template or the member of a class template
      //   is explicitly specialized then that specialization shall be declared
      //   before the first use of that specialization that would cause an
      //   implicit instantiation to take place, in every translation unit in
      //   which such a use occurs; no diagnostic is required.
      for (Decl *Prev = PrevDecl; Prev; Prev = Prev->getPreviousDecl()) {
        // Is there any previous explicit specialization declaration?
        if (getTemplateSpecializationKind(Prev) == TSK_ExplicitSpecialization)
          return false;
      }

      Diag(NewLoc, diag::err_specialization_after_instantiation)
        << PrevDecl;
      Diag(PrevPointOfInstantiation, diag::note_instantiation_required_here)
        << (PrevTSK != TSK_ImplicitInstantiation);

      return true;
    }
    llvm_unreachable("The switch over PrevTSK must be exhaustive.");

  case TSK_ExplicitInstantiationDeclaration:
    switch (PrevTSK) {
    case TSK_ExplicitInstantiationDeclaration:
      // This explicit instantiation declaration is redundant (that's okay).
      HasNoEffect = true;
      return false;

    case TSK_Undeclared:
    case TSK_ImplicitInstantiation:
      // We're explicitly instantiating something that may have already been
      // implicitly instantiated; that's fine.
      return false;

    case TSK_ExplicitSpecialization:
      // C++0x [temp.explicit]p4:
      //   For a given set of template parameters, if an explicit instantiation
      //   of a template appears after a declaration of an explicit
      //   specialization for that template, the explicit instantiation has no
      //   effect.
      HasNoEffect = true;
      return false;

    case TSK_ExplicitInstantiationDefinition:
      // C++0x [temp.explicit]p10:
      //   If an entity is the subject of both an explicit instantiation
      //   declaration and an explicit instantiation definition in the same
      //   translation unit, the definition shall follow the declaration.
      Diag(NewLoc,
           diag::err_explicit_instantiation_declaration_after_definition);

      // Explicit instantiations following a specialization have no effect and
      // hence no PrevPointOfInstantiation. In that case, walk decl backwards
      // until a valid name loc is found.
      Diag(DiagLocForExplicitInstantiation(PrevDecl, PrevPointOfInstantiation),
           diag::note_explicit_instantiation_definition_here);
      HasNoEffect = true;
      return false;
    }
    llvm_unreachable("Unexpected TemplateSpecializationKind!");

  case TSK_ExplicitInstantiationDefinition:
    switch (PrevTSK) {
    case TSK_Undeclared:
    case TSK_ImplicitInstantiation:
      // We're explicitly instantiating something that may have already been
      // implicitly instantiated; that's fine.
      return false;

    case TSK_ExplicitSpecialization:
      // C++ DR 259, C++0x [temp.explicit]p4:
      //   For a given set of template parameters, if an explicit
      //   instantiation of a template appears after a declaration of
      //   an explicit specialization for that template, the explicit
      //   instantiation has no effect.
      Diag(NewLoc, diag::warn_explicit_instantiation_after_specialization)
        << PrevDecl;
      Diag(PrevDecl->getLocation(),
           diag::note_previous_template_specialization);
      HasNoEffect = true;
      return false;

    case TSK_ExplicitInstantiationDeclaration:
      // We're explicitly instantiating a definition for something for which we
      // were previously asked to suppress instantiations. That's fine.

      // C++0x [temp.explicit]p4:
      //   For a given set of template parameters, if an explicit instantiation
      //   of a template appears after a declaration of an explicit
      //   specialization for that template, the explicit instantiation has no
      //   effect.
      for (Decl *Prev = PrevDecl; Prev; Prev = Prev->getPreviousDecl()) {
        // Is there any previous explicit specialization declaration?
        if (getTemplateSpecializationKind(Prev) == TSK_ExplicitSpecialization) {
          HasNoEffect = true;
          break;
        }
      }

      return false;

    case TSK_ExplicitInstantiationDefinition:
      // C++0x [temp.spec]p5:
      //   For a given template and a given set of template-arguments,
      //     - an explicit instantiation definition shall appear at most once
      //       in a program,

      // MSVCCompat: MSVC silently ignores duplicate explicit instantiations.
      Diag(NewLoc, (getLangOpts().MSVCCompat)
                       ? diag::ext_explicit_instantiation_duplicate
                       : diag::err_explicit_instantiation_duplicate)
          << PrevDecl;
      Diag(DiagLocForExplicitInstantiation(PrevDecl, PrevPointOfInstantiation),
           diag::note_previous_explicit_instantiation);
      HasNoEffect = true;
      return false;
    }
  }

  llvm_unreachable("Missing specialization/instantiation case?");
}

bool Sema::CheckDependentFunctionTemplateSpecialization(
    FunctionDecl *FD, const TemplateArgumentListInfo *ExplicitTemplateArgs,
    LookupResult &Previous) {
  // Remove anything from Previous that isn't a function template in
  // the correct context.
  DeclContext *FDLookupContext = FD->getDeclContext()->getRedeclContext();
  LookupResult::Filter F = Previous.makeFilter();
  enum DiscardReason { NotAFunctionTemplate, NotAMemberOfEnclosing };
  SmallVector<std::pair<DiscardReason, Decl *>, 8> DiscardedCandidates;
  while (F.hasNext()) {
    NamedDecl *D = F.next()->getUnderlyingDecl();
    if (!isa<FunctionTemplateDecl>(D)) {
      F.erase();
      DiscardedCandidates.push_back(std::make_pair(NotAFunctionTemplate, D));
      continue;
    }

    if (!FDLookupContext->InEnclosingNamespaceSetOf(
            D->getDeclContext()->getRedeclContext())) {
      F.erase();
      DiscardedCandidates.push_back(std::make_pair(NotAMemberOfEnclosing, D));
      continue;
    }
  }
  F.done();

  bool IsFriend = FD->getFriendObjectKind() != Decl::FOK_None;
  if (Previous.empty()) {
    Diag(FD->getLocation(), diag::err_dependent_function_template_spec_no_match)
        << IsFriend;
    for (auto &P : DiscardedCandidates)
      Diag(P.second->getLocation(),
           diag::note_dependent_function_template_spec_discard_reason)
          << P.first << IsFriend;
    return true;
  }

  FD->setDependentTemplateSpecialization(Context, Previous.asUnresolvedSet(),
                                         ExplicitTemplateArgs);
  return false;
}

bool Sema::CheckFunctionTemplateSpecialization(
    FunctionDecl *FD, TemplateArgumentListInfo *ExplicitTemplateArgs,
    LookupResult &Previous, bool QualifiedFriend) {
  // The set of function template specializations that could match this
  // explicit function template specialization.
  UnresolvedSet<8> Candidates;
  TemplateSpecCandidateSet FailedCandidates(FD->getLocation(),
                                            /*ForTakingAddress=*/false);

  llvm::SmallDenseMap<FunctionDecl *, TemplateArgumentListInfo, 8>
      ConvertedTemplateArgs;

  DeclContext *FDLookupContext = FD->getDeclContext()->getRedeclContext();
  for (LookupResult::iterator I = Previous.begin(), E = Previous.end();
         I != E; ++I) {
    NamedDecl *Ovl = (*I)->getUnderlyingDecl();
    if (FunctionTemplateDecl *FunTmpl = dyn_cast<FunctionTemplateDecl>(Ovl)) {
      // Only consider templates found within the same semantic lookup scope as
      // FD.
      if (!FDLookupContext->InEnclosingNamespaceSetOf(
                                Ovl->getDeclContext()->getRedeclContext()))
        continue;

      QualType FT = FD->getType();
      // C++11 [dcl.constexpr]p8:
      //   A constexpr specifier for a non-static member function that is not
      //   a constructor declares that member function to be const.
      //
      // When matching a constexpr member function template specialization
      // against the primary template, we don't yet know whether the
      // specialization has an implicit 'const' (because we don't know whether
      // it will be a static member function until we know which template it
      // specializes). This rule was removed in C++14.
      if (auto *NewMD = dyn_cast<CXXMethodDecl>(FD);
          !getLangOpts().CPlusPlus14 && NewMD && NewMD->isConstexpr() &&
          !isa<CXXConstructorDecl, CXXDestructorDecl>(NewMD)) {
        auto *OldMD = dyn_cast<CXXMethodDecl>(FunTmpl->getTemplatedDecl());
        if (OldMD && OldMD->isConst()) {
          const FunctionProtoType *FPT = FT->castAs<FunctionProtoType>();
          FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
          EPI.TypeQuals.addConst();
          FT = Context.getFunctionType(FPT->getReturnType(),
                                       FPT->getParamTypes(), EPI);
        }
      }

      TemplateArgumentListInfo Args;
      if (ExplicitTemplateArgs)
        Args = *ExplicitTemplateArgs;

      // C++ [temp.expl.spec]p11:
      //   A trailing template-argument can be left unspecified in the
      //   template-id naming an explicit function template specialization
      //   provided it can be deduced from the function argument type.
      // Perform template argument deduction to determine whether we may be
      // specializing this template.
      // FIXME: It is somewhat wasteful to build
      TemplateDeductionInfo Info(FailedCandidates.getLocation());
      FunctionDecl *Specialization = nullptr;
      if (TemplateDeductionResult TDK = DeduceTemplateArguments(
              cast<FunctionTemplateDecl>(FunTmpl->getFirstDecl()),
              ExplicitTemplateArgs ? &Args : nullptr, FT, Specialization, Info);
          TDK != TemplateDeductionResult::Success) {
        // Template argument deduction failed; record why it failed, so
        // that we can provide nifty diagnostics.
        FailedCandidates.addCandidate().set(
            I.getPair(), FunTmpl->getTemplatedDecl(),
            MakeDeductionFailureInfo(Context, TDK, Info));
        (void)TDK;
        continue;
      }

      // Target attributes are part of the cuda function signature, so
      // the deduced template's cuda target must match that of the
      // specialization.  Given that C++ template deduction does not
      // take target attributes into account, we reject candidates
      // here that have a different target.
      if (LangOpts.CUDA &&
          CUDA().IdentifyTarget(Specialization,
                                /* IgnoreImplicitHDAttr = */ true) !=
              CUDA().IdentifyTarget(FD, /* IgnoreImplicitHDAttr = */ true)) {
        FailedCandidates.addCandidate().set(
            I.getPair(), FunTmpl->getTemplatedDecl(),
            MakeDeductionFailureInfo(
                Context, TemplateDeductionResult::CUDATargetMismatch, Info));
        continue;
      }

      // Record this candidate.
      if (ExplicitTemplateArgs)
        ConvertedTemplateArgs[Specialization] = std::move(Args);
      Candidates.addDecl(Specialization, I.getAccess());
    }
  }

  // For a qualified friend declaration (with no explicit marker to indicate
  // that a template specialization was intended), note all (template and
  // non-template) candidates.
  if (QualifiedFriend && Candidates.empty()) {
    Diag(FD->getLocation(), diag::err_qualified_friend_no_match)
        << FD->getDeclName() << FDLookupContext;
    // FIXME: We should form a single candidate list and diagnose all
    // candidates at once, to get proper sorting and limiting.
    for (auto *OldND : Previous) {
      if (auto *OldFD = dyn_cast<FunctionDecl>(OldND->getUnderlyingDecl()))
        NoteOverloadCandidate(OldND, OldFD, CRK_None, FD->getType(), false);
    }
    FailedCandidates.NoteCandidates(*this, FD->getLocation());
    return true;
  }

  // Find the most specialized function template.
  UnresolvedSetIterator Result = getMostSpecialized(
      Candidates.begin(), Candidates.end(), FailedCandidates, FD->getLocation(),
      PDiag(diag::err_function_template_spec_no_match) << FD->getDeclName(),
      PDiag(diag::err_function_template_spec_ambiguous)
          << FD->getDeclName() << (ExplicitTemplateArgs != nullptr),
      PDiag(diag::note_function_template_spec_matched));

  if (Result == Candidates.end())
    return true;

  // Ignore access information;  it doesn't figure into redeclaration checking.
  FunctionDecl *Specialization = cast<FunctionDecl>(*Result);

  // C++23 [except.spec]p13:
  //   An exception specification is considered to be needed when:
  //   - [...]
  //   - the exception specification is compared to that of another declaration
  //     (e.g., an explicit specialization or an overriding virtual function);
  //   - [...]
  //
  //  The exception specification of a defaulted function is evaluated as
  //  described above only when needed; similarly, the noexcept-specifier of a
  //  specialization of a function template or member function of a class
  //  template is instantiated only when needed.
  //
  // The standard doesn't specify what the "comparison with another declaration"
  // entails, nor the exact circumstances in which it occurs. Moreover, it does
  // not state which properties of an explicit specialization must match the
  // primary template.
  //
  // We assume that an explicit specialization must correspond with (per
  // [basic.scope.scope]p4) and declare the same entity as (per [basic.link]p8)
  // the declaration produced by substitution into the function template.
  //
  // Since the determination whether two function declarations correspond does
  // not consider exception specification, we only need to instantiate it once
  // we determine the primary template when comparing types per
  // [basic.link]p11.1.
  auto *SpecializationFPT =
      Specialization->getType()->castAs<FunctionProtoType>();
  // If the function has a dependent exception specification, resolve it after
  // we have selected the primary template so we can check whether it matches.
  if (getLangOpts().CPlusPlus17 &&
      isUnresolvedExceptionSpec(SpecializationFPT->getExceptionSpecType()) &&
      !ResolveExceptionSpec(FD->getLocation(), SpecializationFPT))
    return true;

  FunctionTemplateSpecializationInfo *SpecInfo
    = Specialization->getTemplateSpecializationInfo();
  assert(SpecInfo && "Function template specialization info missing?");

  // Note: do not overwrite location info if previous template
  // specialization kind was explicit.
  TemplateSpecializationKind TSK = SpecInfo->getTemplateSpecializationKind();
  if (TSK == TSK_Undeclared || TSK == TSK_ImplicitInstantiation) {
    Specialization->setLocation(FD->getLocation());
    Specialization->setLexicalDeclContext(FD->getLexicalDeclContext());
    // C++11 [dcl.constexpr]p1: An explicit specialization of a constexpr
    // function can differ from the template declaration with respect to
    // the constexpr specifier.
    // FIXME: We need an update record for this AST mutation.
    // FIXME: What if there are multiple such prior declarations (for instance,
    // from different modules)?
    Specialization->setConstexprKind(FD->getConstexprKind());
  }

  // FIXME: Check if the prior specialization has a point of instantiation.
  // If so, we have run afoul of .

  // If this is a friend declaration, then we're not really declaring
  // an explicit specialization.
  bool isFriend = (FD->getFriendObjectKind() != Decl::FOK_None);

  // Check the scope of this explicit specialization.
  if (!isFriend &&
      CheckTemplateSpecializationScope(*this,
                                       Specialization->getPrimaryTemplate(),
                                       Specialization, FD->getLocation(),
                                       false))
    return true;

  // C++ [temp.expl.spec]p6:
  //   If a template, a member template or the member of a class template is
  //   explicitly specialized then that specialization shall be declared
  //   before the first use of that specialization that would cause an implicit
  //   instantiation to take place, in every translation unit in which such a
  //   use occurs; no diagnostic is required.
  bool HasNoEffect = false;
  if (!isFriend &&
      CheckSpecializationInstantiationRedecl(FD->getLocation(),
                                             TSK_ExplicitSpecialization,
                                             Specialization,
                                   SpecInfo->getTemplateSpecializationKind(),
                                         SpecInfo->getPointOfInstantiation(),
                                             HasNoEffect))
    return true;

  // Mark the prior declaration as an explicit specialization, so that later
  // clients know that this is an explicit specialization.
  if (!isFriend) {
    // Since explicit specializations do not inherit '=delete' from their
    // primary function template - check if the 'specialization' that was
    // implicitly generated (during template argument deduction for partial
    // ordering) from the most specialized of all the function templates that
    // 'FD' could have been specializing, has a 'deleted' definition.  If so,
    // first check that it was implicitly generated during template argument
    // deduction by making sure it wasn't referenced, and then reset the deleted
    // flag to not-deleted, so that we can inherit that information from 'FD'.
    if (Specialization->isDeleted() && !SpecInfo->isExplicitSpecialization() &&
        !Specialization->getCanonicalDecl()->isReferenced()) {
      // FIXME: This assert will not hold in the presence of modules.
      assert(
          Specialization->getCanonicalDecl() == Specialization &&
          "This must be the only existing declaration of this specialization");
      // FIXME: We need an update record for this AST mutation.
      Specialization->setDeletedAsWritten(false);
    }
    // FIXME: We need an update record for this AST mutation.
    SpecInfo->setTemplateSpecializationKind(TSK_ExplicitSpecialization);
    MarkUnusedFileScopedDecl(Specialization);
  }

  // Turn the given function declaration into a function template
  // specialization, with the template arguments from the previous
  // specialization.
  // Take copies of (semantic and syntactic) template argument lists.
  TemplateArgumentList *TemplArgs = TemplateArgumentList::CreateCopy(
      Context, Specialization->getTemplateSpecializationArgs()->asArray());
  FD->setFunctionTemplateSpecialization(
      Specialization->getPrimaryTemplate(), TemplArgs, /*InsertPos=*/nullptr,
      SpecInfo->getTemplateSpecializationKind(),
      ExplicitTemplateArgs ? &ConvertedTemplateArgs[Specialization] : nullptr);

  // A function template specialization inherits the target attributes
  // of its template.  (We require the attributes explicitly in the
  // code to match, but a template may have implicit attributes by
  // virtue e.g. of being constexpr, and it passes these implicit
  // attributes on to its specializations.)
  if (LangOpts.CUDA)
    CUDA().inheritTargetAttrs(FD, *Specialization->getPrimaryTemplate());

  // The "previous declaration" for this function template specialization is
  // the prior function template specialization.
  Previous.clear();
  Previous.addDecl(Specialization);
  return false;
}

bool
Sema::CheckMemberSpecialization(NamedDecl *Member, LookupResult &Previous) {
  assert(!isa<TemplateDecl>(Member) && "Only for non-template members");

  // Try to find the member we are instantiating.
  NamedDecl *FoundInstantiation = nullptr;
  NamedDecl *Instantiation = nullptr;
  NamedDecl *InstantiatedFrom = nullptr;
  MemberSpecializationInfo *MSInfo = nullptr;

  if (Previous.empty()) {
    // Nowhere to look anyway.
  } else if (FunctionDecl *Function = dyn_cast<FunctionDecl>(Member)) {
    SmallVector<FunctionDecl *> Candidates;
    bool Ambiguous = false;
    for (LookupResult::iterator I = Previous.begin(), E = Previous.end();
           I != E; ++I) {
      CXXMethodDecl *Method =
          dyn_cast<CXXMethodDecl>((*I)->getUnderlyingDecl());
      if (!Method)
        continue;
      QualType Adjusted = Function->getType();
      if (!hasExplicitCallingConv(Adjusted))
        Adjusted = adjustCCAndNoReturn(Adjusted, Method->getType());
      // This doesn't handle deduced return types, but both function
      // declarations should be undeduced at this point.
      if (!Context.hasSameType(Adjusted, Method->getType()))
        continue;
      if (ConstraintSatisfaction Satisfaction;
          Method->getTrailingRequiresClause() &&
          (CheckFunctionConstraints(Method, Satisfaction,
                                    /*UsageLoc=*/Member->getLocation(),
                                    /*ForOverloadResolution=*/true) ||
           !Satisfaction.IsSatisfied))
        continue;
      Candidates.push_back(Method);
      FunctionDecl *MoreConstrained =
          Instantiation ? getMoreConstrainedFunction(
                              Method, cast<FunctionDecl>(Instantiation))
                        : Method;
      if (!MoreConstrained) {
        Ambiguous = true;
        continue;
      }
      if (MoreConstrained == Method) {
        Ambiguous = false;
        FoundInstantiation = *I;
        Instantiation = Method;
        InstantiatedFrom = Method->getInstantiatedFromMemberFunction();
        MSInfo = Method->getMemberSpecializationInfo();
      }
    }
    if (Ambiguous) {
      Diag(Member->getLocation(), diag::err_function_member_spec_ambiguous)
          << Member << (InstantiatedFrom ? InstantiatedFrom : Instantiation);
      for (FunctionDecl *Candidate : Candidates)
        Diag(Candidate->getLocation(), diag::note_function_member_spec_matched)
            << Candidate;
      return true;
    }
  } else if (isa<VarDecl>(Member)) {
    VarDecl *PrevVar;
    if (Previous.isSingleResult() &&
        (PrevVar = dyn_cast<VarDecl>(Previous.getFoundDecl())))
      if (PrevVar->isStaticDataMember()) {
        FoundInstantiation = Previous.getRepresentativeDecl();
        Instantiation = PrevVar;
        InstantiatedFrom = PrevVar->getInstantiatedFromStaticDataMember();
        MSInfo = PrevVar->getMemberSpecializationInfo();
      }
  } else if (isa<RecordDecl>(Member)) {
    CXXRecordDecl *PrevRecord;
    if (Previous.isSingleResult() &&
        (PrevRecord = dyn_cast<CXXRecordDecl>(Previous.getFoundDecl()))) {
      FoundInstantiation = Previous.getRepresentativeDecl();
      Instantiation = PrevRecord;
      InstantiatedFrom = PrevRecord->getInstantiatedFromMemberClass();
      MSInfo = PrevRecord->getMemberSpecializationInfo();
    }
  } else if (isa<EnumDecl>(Member)) {
    EnumDecl *PrevEnum;
    if (Previous.isSingleResult() &&
        (PrevEnum = dyn_cast<EnumDecl>(Previous.getFoundDecl()))) {
      FoundInstantiation = Previous.getRepresentativeDecl();
      Instantiation = PrevEnum;
      InstantiatedFrom = PrevEnum->getInstantiatedFromMemberEnum();
      MSInfo = PrevEnum->getMemberSpecializationInfo();
    }
  }

  if (!Instantiation) {
    // There is no previous declaration that matches. Since member
    // specializations are always out-of-line, the caller will complain about
    // this mismatch later.
    return false;
  }

  // A member specialization in a friend declaration isn't really declaring
  // an explicit specialization, just identifying a specific (possibly implicit)
  // specialization. Don't change the template specialization kind.
  //
  // FIXME: Is this really valid? Other compilers reject.
  if (Member->getFriendObjectKind() != Decl::FOK_None) {
    // Preserve instantiation information.
    if (InstantiatedFrom && isa<CXXMethodDecl>(Member)) {
      cast<CXXMethodDecl>(Member)->setInstantiationOfMemberFunction(
                                      cast<CXXMethodDecl>(InstantiatedFrom),
        cast<CXXMethodDecl>(Instantiation)->getTemplateSpecializationKind());
    } else if (InstantiatedFrom && isa<CXXRecordDecl>(Member)) {
      cast<CXXRecordDecl>(Member)->setInstantiationOfMemberClass(
                                      cast<CXXRecordDecl>(InstantiatedFrom),
        cast<CXXRecordDecl>(Instantiation)->getTemplateSpecializationKind());
    }

    Previous.clear();
    Previous.addDecl(FoundInstantiation);
    return false;
  }

  // Make sure that this is a specialization of a member.
  if (!InstantiatedFrom) {
    Diag(Member->getLocation(), diag::err_spec_member_not_instantiated)
      << Member;
    Diag(Instantiation->getLocation(), diag::note_specialized_decl);
    return true;
  }

  // C++ [temp.expl.spec]p6:
  //   If a template, a member template or the member of a class template is
  //   explicitly specialized then that specialization shall be declared
  //   before the first use of that specialization that would cause an implicit
  //   instantiation to take place, in every translation unit in which such a
  //   use occurs; no diagnostic is required.
  assert(MSInfo && "Member specialization info missing?");

  bool HasNoEffect = false;
  if (CheckSpecializationInstantiationRedecl(Member->getLocation(),
                                             TSK_ExplicitSpecialization,
                                             Instantiation,
                                     MSInfo->getTemplateSpecializationKind(),
                                           MSInfo->getPointOfInstantiation(),
                                             HasNoEffect))
    return true;

  // Check the scope of this explicit specialization.
  if (CheckTemplateSpecializationScope(*this,
                                       InstantiatedFrom,
                                       Instantiation, Member->getLocation(),
                                       false))
    return true;

  // Note that this member specialization is an "instantiation of" the
  // corresponding member of the original template.
  if (auto *MemberFunction = dyn_cast<FunctionDecl>(Member)) {
    FunctionDecl *InstantiationFunction = cast<FunctionDecl>(Instantiation);
    if (InstantiationFunction->getTemplateSpecializationKind() ==
          TSK_ImplicitInstantiation) {
      // Explicit specializations of member functions of class templates do not
      // inherit '=delete' from the member function they are specializing.
      if (InstantiationFunction->isDeleted()) {
        // FIXME: This assert will not hold in the presence of modules.
        assert(InstantiationFunction->getCanonicalDecl() ==
               InstantiationFunction);
        // FIXME: We need an update record for this AST mutation.
        InstantiationFunction->setDeletedAsWritten(false);
      }
    }

    MemberFunction->setInstantiationOfMemberFunction(
        cast<CXXMethodDecl>(InstantiatedFrom), TSK_ExplicitSpecialization);
  } else if (auto *MemberVar = dyn_cast<VarDecl>(Member)) {
    MemberVar->setInstantiationOfStaticDataMember(
        cast<VarDecl>(InstantiatedFrom), TSK_ExplicitSpecialization);
  } else if (auto *MemberClass = dyn_cast<CXXRecordDecl>(Member)) {
    MemberClass->setInstantiationOfMemberClass(
        cast<CXXRecordDecl>(InstantiatedFrom), TSK_ExplicitSpecialization);
  } else if (auto *MemberEnum = dyn_cast<EnumDecl>(Member)) {
    MemberEnum->setInstantiationOfMemberEnum(
        cast<EnumDecl>(InstantiatedFrom), TSK_ExplicitSpecialization);
  } else {
    llvm_unreachable("unknown member specialization kind");
  }

  // Save the caller the trouble of having to figure out which declaration
  // this specialization matches.
  Previous.clear();
  Previous.addDecl(FoundInstantiation);
  return false;
}

/// Complete the explicit specialization of a member of a class template by
/// updating the instantiated member to be marked as an explicit specialization.
///
/// \param OrigD The member declaration instantiated from the template.
/// \param Loc The location of the explicit specialization of the member.
template<typename DeclT>
static void completeMemberSpecializationImpl(Sema &S, DeclT *OrigD,
                                             SourceLocation Loc) {
  if (OrigD->getTemplateSpecializationKind() != TSK_ImplicitInstantiation)
    return;

  // FIXME: Inform AST mutation listeners of this AST mutation.
  // FIXME: If there are multiple in-class declarations of the member (from
  // multiple modules, or a declaration and later definition of a member type),
  // should we update all of them?
  OrigD->setTemplateSpecializationKind(TSK_ExplicitSpecialization);
  OrigD->setLocation(Loc);
}

void Sema::CompleteMemberSpecialization(NamedDecl *Member,
                                        LookupResult &Previous) {
  NamedDecl *Instantiation = cast<NamedDecl>(Member->getCanonicalDecl());
  if (Instantiation == Member)
    return;

  if (auto *Function = dyn_cast<CXXMethodDecl>(Instantiation))
    completeMemberSpecializationImpl(*this, Function, Member->getLocation());
  else if (auto *Var = dyn_cast<VarDecl>(Instantiation))
    completeMemberSpecializationImpl(*this, Var, Member->getLocation());
  else if (auto *Record = dyn_cast<CXXRecordDecl>(Instantiation))
    completeMemberSpecializationImpl(*this, Record, Member->getLocation());
  else if (auto *Enum = dyn_cast<EnumDecl>(Instantiation))
    completeMemberSpecializationImpl(*this, Enum, Member->getLocation());
  else
    llvm_unreachable("unknown member specialization kind");
}

/// Check the scope of an explicit instantiation.
///
/// \returns true if a serious error occurs, false otherwise.
static bool CheckExplicitInstantiationScope(Sema &S, NamedDecl *D,
                                            SourceLocation InstLoc,
                                            bool WasQualifiedName) {
  DeclContext *OrigContext= D->getDeclContext()->getEnclosingNamespaceContext();
  DeclContext *CurContext = S.CurContext->getRedeclContext();

  if (CurContext->isRecord()) {
    S.Diag(InstLoc, diag::err_explicit_instantiation_in_class)
      << D;
    return true;
  }

  // C++11 [temp.explicit]p3:
  //   An explicit instantiation shall appear in an enclosing namespace of its
  //   template. If the name declared in the explicit instantiation is an
  //   unqualified name, the explicit instantiation shall appear in the
  //   namespace where its template is declared or, if that namespace is inline
  //   (7.3.1), any namespace from its enclosing namespace set.
  //
  // This is DR275, which we do not retroactively apply to C++98/03.
  if (WasQualifiedName) {
    if (CurContext->Encloses(OrigContext))
      return false;
  } else {
    if (CurContext->InEnclosingNamespaceSetOf(OrigContext))
      return false;
  }

  if (NamespaceDecl *NS = dyn_cast<NamespaceDecl>(OrigContext)) {
    if (WasQualifiedName)
      S.Diag(InstLoc,
             S.getLangOpts().CPlusPlus11?
               diag::err_explicit_instantiation_out_of_scope :
               diag::warn_explicit_instantiation_out_of_scope_0x)
        << D << NS;
    else
      S.Diag(InstLoc,
             S.getLangOpts().CPlusPlus11?
               diag::err_explicit_instantiation_unqualified_wrong_namespace :
               diag::warn_explicit_instantiation_unqualified_wrong_namespace_0x)
        << D << NS;
  } else
    S.Diag(InstLoc,
           S.getLangOpts().CPlusPlus11?
             diag::err_explicit_instantiation_must_be_global :
             diag::warn_explicit_instantiation_must_be_global_0x)
      << D;
  S.Diag(D->getLocation(), diag::note_explicit_instantiation_here);
  return false;
}

/// Common checks for whether an explicit instantiation of \p D is valid.
static bool CheckExplicitInstantiation(Sema &S, NamedDecl *D,
                                       SourceLocation InstLoc,
                                       bool WasQualifiedName,
                                       TemplateSpecializationKind TSK) {
  // C++ [temp.explicit]p13:
  //   An explicit instantiation declaration shall not name a specialization of
  //   a template with internal linkage.
  if (TSK == TSK_ExplicitInstantiationDeclaration &&
      D->getFormalLinkage() == Linkage::Internal) {
    S.Diag(InstLoc, diag::err_explicit_instantiation_internal_linkage) << D;
    return true;
  }

  // C++11 [temp.explicit]p3: [DR 275]
  //   An explicit instantiation shall appear in an enclosing namespace of its
  //   template.
  if (CheckExplicitInstantiationScope(S, D, InstLoc, WasQualifiedName))
    return true;

  return false;
}

/// Determine whether the given scope specifier has a template-id in it.
static bool ScopeSpecifierHasTemplateId(const CXXScopeSpec &SS) {
  if (!SS.isSet())
    return false;

  // C++11 [temp.explicit]p3:
  //   If the explicit instantiation is for a member function, a member class
  //   or a static data member of a class template specialization, the name of
  //   the class template specialization in the qualified-id for the member
  //   name shall be a simple-template-id.
  //
  // C++98 has the same restriction, just worded differently.
  for (NestedNameSpecifier *NNS = SS.getScopeRep(); NNS;
       NNS = NNS->getPrefix())
    if (const Type *T = NNS->getAsType())
      if (isa<TemplateSpecializationType>(T))
        return true;

  return false;
}

/// Make a dllexport or dllimport attr on a class template specialization take
/// effect.
static void dllExportImportClassTemplateSpecialization(
    Sema &S, ClassTemplateSpecializationDecl *Def) {
  auto *A = cast_or_null<InheritableAttr>(getDLLAttr(Def));
  assert(A && "dllExportImportClassTemplateSpecialization called "
              "on Def without dllexport or dllimport");

  // We reject explicit instantiations in class scope, so there should
  // never be any delayed exported classes to worry about.
  assert(S.DelayedDllExportClasses.empty() &&
         "delayed exports present at explicit instantiation");
  S.checkClassLevelDLLAttribute(Def);

  // Propagate attribute to base class templates.
  for (auto &B : Def->bases()) {
    if (auto *BT = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
            B.getType()->getAsCXXRecordDecl()))
      S.propagateDLLAttrToBaseClassTemplate(Def, A, BT, B.getBeginLoc());
  }

  S.referenceDLLExportedClassMethods();
}

DeclResult Sema::ActOnExplicitInstantiation(
    Scope *S, SourceLocation ExternLoc, SourceLocation TemplateLoc,
    unsigned TagSpec, SourceLocation KWLoc, const CXXScopeSpec &SS,
    TemplateTy TemplateD, SourceLocation TemplateNameLoc,
    SourceLocation LAngleLoc, ASTTemplateArgsPtr TemplateArgsIn,
    SourceLocation RAngleLoc, const ParsedAttributesView &Attr) {
  // Find the class template we're specializing
  TemplateName Name = TemplateD.get();
  TemplateDecl *TD = Name.getAsTemplateDecl();
  // Check that the specialization uses the same tag kind as the
  // original template.
  TagTypeKind Kind = TypeWithKeyword::getTagTypeKindForTypeSpec(TagSpec);
  assert(Kind != TagTypeKind::Enum &&
         "Invalid enum tag in class template explicit instantiation!");

  ClassTemplateDecl *ClassTemplate = dyn_cast<ClassTemplateDecl>(TD);

  if (!ClassTemplate) {
    NonTagKind NTK = getNonTagTypeDeclKind(TD, Kind);
    Diag(TemplateNameLoc, diag::err_tag_reference_non_tag)
        << TD << NTK << llvm::to_underlying(Kind);
    Diag(TD->getLocation(), diag::note_previous_use);
    return true;
  }

  if (!isAcceptableTagRedeclaration(ClassTemplate->getTemplatedDecl(),
                                    Kind, /*isDefinition*/false, KWLoc,
                                    ClassTemplate->getIdentifier())) {
    Diag(KWLoc, diag::err_use_with_wrong_tag)
      << ClassTemplate
      << FixItHint::CreateReplacement(KWLoc,
                            ClassTemplate->getTemplatedDecl()->getKindName());
    Diag(ClassTemplate->getTemplatedDecl()->getLocation(),
         diag::note_previous_use);
    Kind = ClassTemplate->getTemplatedDecl()->getTagKind();
  }

  // C++0x [temp.explicit]p2:
  //   There are two forms of explicit instantiation: an explicit instantiation
  //   definition and an explicit instantiation declaration. An explicit
  //   instantiation declaration begins with the extern keyword. [...]
  TemplateSpecializationKind TSK = ExternLoc.isInvalid()
                                       ? TSK_ExplicitInstantiationDefinition
                                       : TSK_ExplicitInstantiationDeclaration;

  if (TSK == TSK_ExplicitInstantiationDeclaration &&
      !Context.getTargetInfo().getTriple().isWindowsGNUEnvironment()) {
    // Check for dllexport class template instantiation declarations,
    // except for MinGW mode.
    for (const ParsedAttr &AL : Attr) {
      if (AL.getKind() == ParsedAttr::AT_DLLExport) {
        Diag(ExternLoc,
             diag::warn_attribute_dllexport_explicit_instantiation_decl);
        Diag(AL.getLoc(), diag::note_attribute);
        break;
      }
    }

    if (auto *A = ClassTemplate->getTemplatedDecl()->getAttr<DLLExportAttr>()) {
      Diag(ExternLoc,
           diag::warn_attribute_dllexport_explicit_instantiation_decl);
      Diag(A->getLocation(), diag::note_attribute);
    }
  }

  // In MSVC mode, dllimported explicit instantiation definitions are treated as
  // instantiation declarations for most purposes.
  bool DLLImportExplicitInstantiationDef = false;
  if (TSK == TSK_ExplicitInstantiationDefinition &&
      Context.getTargetInfo().getCXXABI().isMicrosoft()) {
    // Check for dllimport class template instantiation definitions.
    bool DLLImport =
        ClassTemplate->getTemplatedDecl()->getAttr<DLLImportAttr>();
    for (const ParsedAttr &AL : Attr) {
      if (AL.getKind() == ParsedAttr::AT_DLLImport)
        DLLImport = true;
      if (AL.getKind() == ParsedAttr::AT_DLLExport) {
        // dllexport trumps dllimport here.
        DLLImport = false;
        break;
      }
    }
    if (DLLImport) {
      TSK = TSK_ExplicitInstantiationDeclaration;
      DLLImportExplicitInstantiationDef = true;
    }
  }

  // Translate the parser's template argument list in our AST format.
  TemplateArgumentListInfo TemplateArgs(LAngleLoc, RAngleLoc);
  translateTemplateArguments(TemplateArgsIn, TemplateArgs);

  // Check that the template argument list is well-formed for this
  // template.
  SmallVector<TemplateArgument, 4> SugaredConverted, CanonicalConverted;
  if (CheckTemplateArgumentList(ClassTemplate, TemplateNameLoc, TemplateArgs,
                                false, SugaredConverted, CanonicalConverted,
                                /*UpdateArgsWithConversions=*/true))
    return true;

  // Find the class template specialization declaration that
  // corresponds to these arguments.
  void *InsertPos = nullptr;
  ClassTemplateSpecializationDecl *PrevDecl =
      ClassTemplate->findSpecialization(CanonicalConverted, InsertPos);

  TemplateSpecializationKind PrevDecl_TSK
    = PrevDecl ? PrevDecl->getTemplateSpecializationKind() : TSK_Undeclared;

  if (TSK == TSK_ExplicitInstantiationDefinition && PrevDecl != nullptr &&
      Context.getTargetInfo().getTriple().isWindowsGNUEnvironment()) {
    // Check for dllexport class template instantiation definitions in MinGW
    // mode, if a previous declaration of the instantiation was seen.
    for (const ParsedAttr &AL : Attr) {
      if (AL.getKind() == ParsedAttr::AT_DLLExport) {
        Diag(AL.getLoc(),
             diag::warn_attribute_dllexport_explicit_instantiation_def);
        break;
      }
    }
  }

  if (CheckExplicitInstantiation(*this, ClassTemplate, TemplateNameLoc,
                                 SS.isSet(), TSK))
    return true;

  ClassTemplateSpecializationDecl *Specialization = nullptr;

  bool HasNoEffect = false;
  if (PrevDecl) {
    if (CheckSpecializationInstantiationRedecl(TemplateNameLoc, TSK,
                                               PrevDecl, PrevDecl_TSK,
                                            PrevDecl->getPointOfInstantiation(),
                                               HasNoEffect))
      return PrevDecl;

    // Even though HasNoEffect == true means that this explicit instantiation
    // has no effect on semantics, we go on to put its syntax in the AST.

    if (PrevDecl_TSK == TSK_ImplicitInstantiation ||
        PrevDecl_TSK == TSK_Undeclared) {
      // Since the only prior class template specialization with these
      // arguments was referenced but not declared, reuse that
      // declaration node as our own, updating the source location
      // for the template name to reflect our new declaration.
      // (Other source locations will be updated later.)
      Specialization = PrevDecl;
      Specialization->setLocation(TemplateNameLoc);
      PrevDecl = nullptr;
    }

    if (PrevDecl_TSK == TSK_ExplicitInstantiationDeclaration &&
        DLLImportExplicitInstantiationDef) {
      // The new specialization might add a dllimport attribute.
      HasNoEffect = false;
    }
  }

  if (!Specialization) {
    // Create a new class template specialization declaration node for
    // this explicit specialization.
    Specialization = ClassTemplateSpecializationDecl::Create(
        Context, Kind, ClassTemplate->getDeclContext(), KWLoc, TemplateNameLoc,
        ClassTemplate, CanonicalConverted, PrevDecl);
    SetNestedNameSpecifier(*this, Specialization, SS);

    // A MSInheritanceAttr attached to the previous declaration must be
    // propagated to the new node prior to instantiation.
    if (PrevDecl) {
      if (const auto *A = PrevDecl->getAttr<MSInheritanceAttr>()) {
        auto *Clone = A->clone(getASTContext());
        Clone->setInherited(true);
        Specialization->addAttr(Clone);
        Consumer.AssignInheritanceModel(Specialization);
      }
    }

    if (!HasNoEffect && !PrevDecl) {
      // Insert the new specialization.
      ClassTemplate->AddSpecialization(Specialization, InsertPos);
    }
  }

  Specialization->setTemplateArgsAsWritten(TemplateArgs);

  // Set source locations for keywords.
  Specialization->setExternKeywordLoc(ExternLoc);
  Specialization->setTemplateKeywordLoc(TemplateLoc);
  Specialization->setBraceRange(SourceRange());

  bool PreviouslyDLLExported = Specialization->hasAttr<DLLExportAttr>();
  ProcessDeclAttributeList(S, Specialization, Attr);
  ProcessAPINotes(Specialization);

  // Add the explicit instantiation into its lexical context. However,
  // since explicit instantiations are never found by name lookup, we
  // just put it into the declaration context directly.
  Specialization->setLexicalDeclContext(CurContext);
  CurContext->addDecl(Specialization);

  // Syntax is now OK, so return if it has no other effect on semantics.
  if (HasNoEffect) {
    // Set the template specialization kind.
    Specialization->setTemplateSpecializationKind(TSK);
    return Specialization;
  }

  // C++ [temp.explicit]p3:
  //   A definition of a class template or class member template
  //   shall be in scope at the point of the explicit instantiation of
  //   the class template or class member template.
  //
  // This check comes when we actually try to perform the
  // instantiation.
  ClassTemplateSpecializationDecl *Def
    = cast_or_null<ClassTemplateSpecializationDecl>(
                                              Specialization->getDefinition());
  if (!Def)
    InstantiateClassTemplateSpecialization(TemplateNameLoc, Specialization, TSK);
  else if (TSK == TSK_ExplicitInstantiationDefinition) {
    MarkVTableUsed(TemplateNameLoc, Specialization, true);
    Specialization->setPointOfInstantiation(Def->getPointOfInstantiation());
  }

  // Instantiate the members of this class template specialization.
  Def = cast_or_null<ClassTemplateSpecializationDecl>(
                                       Specialization->getDefinition());
  if (Def) {
    TemplateSpecializationKind Old_TSK = Def->getTemplateSpecializationKind();
    // Fix a TSK_ExplicitInstantiationDeclaration followed by a
    // TSK_ExplicitInstantiationDefinition
    if (Old_TSK == TSK_ExplicitInstantiationDeclaration &&
        (TSK == TSK_ExplicitInstantiationDefinition ||
         DLLImportExplicitInstantiationDef)) {
      // FIXME: Need to notify the ASTMutationListener that we did this.
      Def->setTemplateSpecializationKind(TSK);

      if (!getDLLAttr(Def) && getDLLAttr(Specialization) &&
          Context.getTargetInfo().shouldDLLImportComdatSymbols()) {
        // An explicit instantiation definition can add a dll attribute to a
        // template with a previous instantiation declaration. MinGW doesn't
        // allow this.
        auto *A = cast<InheritableAttr>(
            getDLLAttr(Specialization)->clone(getASTContext()));
        A->setInherited(true);
        Def->addAttr(A);
        dllExportImportClassTemplateSpecialization(*this, Def);
      }
    }

    // Fix a TSK_ImplicitInstantiation followed by a
    // TSK_ExplicitInstantiationDefinition
    bool NewlyDLLExported =
        !PreviouslyDLLExported && Specialization->hasAttr<DLLExportAttr>();
    if (Old_TSK == TSK_ImplicitInstantiation && NewlyDLLExported &&
        Context.getTargetInfo().shouldDLLImportComdatSymbols()) {
      // An explicit instantiation definition can add a dll attribute to a
      // template with a previous implicit instantiation. MinGW doesn't allow
      // this. We limit clang to only adding dllexport, to avoid potentially
      // strange codegen behavior. For example, if we extend this conditional
      // to dllimport, and we have a source file calling a method on an
      // implicitly instantiated template class instance and then declaring a
      // dllimport explicit instantiation definition for the same template
      // class, the codegen for the method call will not respect the dllimport,
      // while it will with cl. The Def will already have the DLL attribute,
      // since the Def and Specialization will be the same in the case of
      // Old_TSK == TSK_ImplicitInstantiation, and we already added the
      // attribute to the Specialization; we just need to make it take effect.
      assert(Def == Specialization &&
             "Def and Specialization should match for implicit instantiation");
      dllExportImportClassTemplateSpecialization(*this, Def);
    }

    // In MinGW mode, export the template instantiation if the declaration
    // was marked dllexport.
    if (PrevDecl_TSK == TSK_ExplicitInstantiationDeclaration &&
        Context.getTargetInfo().getTriple().isWindowsGNUEnvironment() &&
        PrevDecl->hasAttr<DLLExportAttr>()) {
      dllExportImportClassTemplateSpecialization(*this, Def);
    }

    // Set the template specialization kind. Make sure it is set before
    // instantiating the members which will trigger ASTConsumer callbacks.
    Specialization->setTemplateSpecializationKind(TSK);
    InstantiateClassTemplateSpecializationMembers(TemplateNameLoc, Def, TSK);
  } else {

    // Set the template specialization kind.
    Specialization->setTemplateSpecializationKind(TSK);
  }

  return Specialization;
}

DeclResult
Sema::ActOnExplicitInstantiation(Scope *S, SourceLocation ExternLoc,
                                 SourceLocation TemplateLoc, unsigned TagSpec,
                                 SourceLocation KWLoc, CXXScopeSpec &SS,
                                 IdentifierInfo *Name, SourceLocation NameLoc,
                                 const ParsedAttributesView &Attr) {

  bool Owned = false;
  bool IsDependent = false;
  Decl *TagD =
      ActOnTag(S, TagSpec, TagUseKind::Reference, KWLoc, SS, Name, NameLoc,
               Attr, AS_none, /*ModulePrivateLoc=*/SourceLocation(),
               MultiTemplateParamsArg(), Owned, IsDependent, SourceLocation(),
               false, TypeResult(), /*IsTypeSpecifier*/ false,
               /*IsTemplateParamOrArg*/ false, /*OOK=*/OOK_Outside)
          .get();
  assert(!IsDependent && "explicit instantiation of dependent name not yet handled");

  if (!TagD)
    return true;

  TagDecl *Tag = cast<TagDecl>(TagD);
  assert(!Tag->isEnum() && "shouldn't see enumerations here");

  if (Tag->isInvalidDecl())
    return true;

  CXXRecordDecl *Record = cast<CXXRecordDecl>(Tag);
  CXXRecordDecl *Pattern = Record->getInstantiatedFromMemberClass();
  if (!Pattern) {
    Diag(TemplateLoc, diag::err_explicit_instantiation_nontemplate_type)
      << Context.getTypeDeclType(Record);
    Diag(Record->getLocation(), diag::note_nontemplate_decl_here);
    return true;
  }

  // C++0x [temp.explicit]p2:
  //   If the explicit instantiation is for a class or member class, the
  //   elaborated-type-specifier in the declaration shall include a
  //   simple-template-id.
  //
  // C++98 has the same restriction, just worded differently.
  if (!ScopeSpecifierHasTemplateId(SS))
    Diag(TemplateLoc, diag::ext_explicit_instantiation_without_qualified_id)
      << Record << SS.getRange();

  // C++0x [temp.explicit]p2:
  //   There are two forms of explicit instantiation: an explicit instantiation
  //   definition and an explicit instantiation declaration. An explicit
  //   instantiation declaration begins with the extern keyword. [...]
  TemplateSpecializationKind TSK
    = ExternLoc.isInvalid()? TSK_ExplicitInstantiationDefinition
                           : TSK_ExplicitInstantiationDeclaration;

  CheckExplicitInstantiation(*this, Record, NameLoc, true, TSK);

  // Verify that it is okay to explicitly instantiate here.
  CXXRecordDecl *PrevDecl
    = cast_or_null<CXXRecordDecl>(Record->getPreviousDecl());
  if (!PrevDecl && Record->getDefinition())
    PrevDecl = Record;
  if (PrevDecl) {
    MemberSpecializationInfo *MSInfo = PrevDecl->getMemberSpecializationInfo();
    bool HasNoEffect = false;
    assert(MSInfo && "No member specialization information?");
    if (CheckSpecializationInstantiationRedecl(TemplateLoc, TSK,
                                               PrevDecl,
                                        MSInfo->getTemplateSpecializationKind(),
                                             MSInfo->getPointOfInstantiation(),
                                               HasNoEffect))
      return true;
    if (HasNoEffect)
      return TagD;
  }

  CXXRecordDecl *RecordDef
    = cast_or_null<CXXRecordDecl>(Record->getDefinition());
  if (!RecordDef) {
    // C++ [temp.explicit]p3:
    //   A definition of a member class of a class template shall be in scope
    //   at the point of an explicit instantiation of the member class.
    CXXRecordDecl *Def
      = cast_or_null<CXXRecordDecl>(Pattern->getDefinition());
    if (!Def) {
      Diag(TemplateLoc, diag::err_explicit_instantiation_undefined_member)
        << 0 << Record->getDeclName() << Record->getDeclContext();
      Diag(Pattern->getLocation(), diag::note_forward_declaration)
        << Pattern;
      return true;
    } else {
      if (InstantiateClass(NameLoc, Record, Def,
                           getTemplateInstantiationArgs(Record),
                           TSK))
        return true;

      RecordDef = cast_or_null<CXXRecordDecl>(Record->getDefinition());
      if (!RecordDef)
        return true;
    }
  }

  // Instantiate all of the members of the class.
  InstantiateClassMembers(NameLoc, RecordDef,
                          getTemplateInstantiationArgs(Record), TSK);

  if (TSK == TSK_ExplicitInstantiationDefinition)
    MarkVTableUsed(NameLoc, RecordDef, true);

  // FIXME: We don't have any representation for explicit instantiations of
  // member classes. Such a representation is not needed for compilation, but it
  // should be available for clients that want to see all of the declarations in
  // the source code.
  return TagD;
}

DeclResult Sema::ActOnExplicitInstantiation(Scope *S,
                                            SourceLocation ExternLoc,
                                            SourceLocation TemplateLoc,
                                            Declarator &D) {
  // Explicit instantiations always require a name.
  // TODO: check if/when DNInfo should replace Name.
  DeclarationNameInfo NameInfo = GetNameForDeclarator(D);
  DeclarationName Name = NameInfo.getName();
  if (!Name) {
    if (!D.isInvalidType())
      Diag(D.getDeclSpec().getBeginLoc(),
           diag::err_explicit_instantiation_requires_name)
          << D.getDeclSpec().getSourceRange() << D.getSourceRange();

    return true;
  }

  // Get the innermost enclosing declaration scope.
  S = S->getDeclParent();

  // Determine the type of the declaration.
  TypeSourceInfo *T = GetTypeForDeclarator(D);
  QualType R = T->getType();
  if (R.isNull())
    return true;

  // C++ [dcl.stc]p1:
  //   A storage-class-specifier shall not be specified in [...] an explicit
  //   instantiation (14.7.2) directive.
  if (D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_typedef) {
    Diag(D.getIdentifierLoc(), diag::err_explicit_instantiation_of_typedef)
      << Name;
    return true;
  } else if (D.getDeclSpec().getStorageClassSpec()
                                                != DeclSpec::SCS_unspecified) {
    // Complain about then remove the storage class specifier.
    Diag(D.getIdentifierLoc(), diag::err_explicit_instantiation_storage_class)
      << FixItHint::CreateRemoval(D.getDeclSpec().getStorageClassSpecLoc());

    D.getMutableDeclSpec().ClearStorageClassSpecs();
  }

  // C++0x [temp.explicit]p1:
  //   [...] An explicit instantiation of a function template shall not use the
  //   inline or constexpr specifiers.
  // Presumably, this also applies to member functions of class templates as
  // well.
  if (D.getDeclSpec().isInlineSpecified())
    Diag(D.getDeclSpec().getInlineSpecLoc(),
         getLangOpts().CPlusPlus11 ?
           diag::err_explicit_instantiation_inline :
           diag::warn_explicit_instantiation_inline_0x)
      << FixItHint::CreateRemoval(D.getDeclSpec().getInlineSpecLoc());
  if (D.getDeclSpec().hasConstexprSpecifier() && R->isFunctionType())
    // FIXME: Add a fix-it to remove the 'constexpr' and add a 'const' if one is
    // not already specified.
    Diag(D.getDeclSpec().getConstexprSpecLoc(),
         diag::err_explicit_instantiation_constexpr);

  // A deduction guide is not on the list of entities that can be explicitly
  // instantiated.
  if (Name.getNameKind() == DeclarationName::CXXDeductionGuideName) {
    Diag(D.getDeclSpec().getBeginLoc(), diag::err_deduction_guide_specialized)
        << /*explicit instantiation*/ 0;
    return true;
  }

  // C++0x [temp.explicit]p2:
  //   There are two forms of explicit instantiation: an explicit instantiation
  //   definition and an explicit instantiation declaration. An explicit
  //   instantiation declaration begins with the extern keyword. [...]
  TemplateSpecializationKind TSK
    = ExternLoc.isInvalid()? TSK_ExplicitInstantiationDefinition
                           : TSK_ExplicitInstantiationDeclaration;

  LookupResult Previous(*this, NameInfo, LookupOrdinaryName);
  LookupParsedName(Previous, S, &D.getCXXScopeSpec(),
                   /*ObjectType=*/QualType());

  if (!R->isFunctionType()) {
    // C++ [temp.explicit]p1:
    //   A [...] static data member of a class template can be explicitly
    //   instantiated from the member definition associated with its class
    //   template.
    // C++1y [temp.explicit]p1:
    //   A [...] variable [...] template specialization can be explicitly
    //   instantiated from its template.
    if (Previous.isAmbiguous())
      return true;

    VarDecl *Prev = Previous.getAsSingle<VarDecl>();
    VarTemplateDecl *PrevTemplate = Previous.getAsSingle<VarTemplateDecl>();

    if (!PrevTemplate) {
      if (!Prev || !Prev->isStaticDataMember()) {
        // We expect to see a static data member here.
        Diag(D.getIdentifierLoc(), diag::err_explicit_instantiation_not_known)
            << Name;
        for (LookupResult::iterator P = Previous.begin(), PEnd = Previous.end();
             P != PEnd; ++P)
          Diag((*P)->getLocation(), diag::note_explicit_instantiation_here);
        return true;
      }

      if (!Prev->getInstantiatedFromStaticDataMember()) {
        // FIXME: Check for explicit specialization?
        Diag(D.getIdentifierLoc(),
             diag::err_explicit_instantiation_data_member_not_instantiated)
            << Prev;
        Diag(Prev->getLocation(), diag::note_explicit_instantiation_here);
        // FIXME: Can we provide a note showing where this was declared?
        return true;
      }
    } else {
      // Explicitly instantiate a variable template.

      // C++1y [dcl.spec.auto]p6:
      //   ... A program that uses auto or decltype(auto) in a context not
      //   explicitly allowed in this section is ill-formed.
      //
      // This includes auto-typed variable template instantiations.
      if (R->isUndeducedType()) {
        Diag(T->getTypeLoc().getBeginLoc(),
             diag::err_auto_not_allowed_var_inst);
        return true;
      }

      if (D.getName().getKind() != UnqualifiedIdKind::IK_TemplateId) {
        // C++1y [temp.explicit]p3:
        //   If the explicit instantiation is for a variable, the unqualified-id
        //   in the declaration shall be a template-id.
        Diag(D.getIdentifierLoc(),
             diag::err_explicit_instantiation_without_template_id)
          << PrevTemplate;
        Diag(PrevTemplate->getLocation(),
             diag::note_explicit_instantiation_here);
        return true;
      }

      // Translate the parser's template argument list into our AST format.
      TemplateArgumentListInfo TemplateArgs =
          makeTemplateArgumentListInfo(*this, *D.getName().TemplateId);

      DeclResult Res = CheckVarTemplateId(PrevTemplate, TemplateLoc,
                                          D.getIdentifierLoc(), TemplateArgs);
      if (Res.isInvalid())
        return true;

      if (!Res.isUsable()) {
        // We somehow specified dependent template arguments in an explicit
        // instantiation. This should probably only happen during error
        // recovery.
        Diag(D.getIdentifierLoc(), diag::err_explicit_instantiation_dependent);
        return true;
      }

      // Ignore access control bits, we don't need them for redeclaration
      // checking.
      Prev = cast<VarDecl>(Res.get());
    }

    // C++0x [temp.explicit]p2:
    //   If the explicit instantiation is for a member function, a member class
    //   or a static data member of a class template specialization, the name of
    //   the class template specialization in the qualified-id for the member
    //   name shall be a simple-template-id.
    //
    // C++98 has the same restriction, just worded differently.
    //
    // This does not apply to variable template specializations, where the
    // template-id is in the unqualified-id instead.
    if (!ScopeSpecifierHasTemplateId(D.getCXXScopeSpec()) && !PrevTemplate)
      Diag(D.getIdentifierLoc(),
           diag::ext_explicit_instantiation_without_qualified_id)
        << Prev << D.getCXXScopeSpec().getRange();

    CheckExplicitInstantiation(*this, Prev, D.getIdentifierLoc(), true, TSK);

    // Verify that it is okay to explicitly instantiate here.
    TemplateSpecializationKind PrevTSK = Prev->getTemplateSpecializationKind();
    SourceLocation POI = Prev->getPointOfInstantiation();
    bool HasNoEffect = false;
    if (CheckSpecializationInstantiationRedecl(D.getIdentifierLoc(), TSK, Prev,
                                               PrevTSK, POI, HasNoEffect))
      return true;

    if (!HasNoEffect) {
      // Instantiate static data member or variable template.
      Prev->setTemplateSpecializationKind(TSK, D.getIdentifierLoc());
      if (auto *VTSD = dyn_cast<VarTemplatePartialSpecializationDecl>(Prev)) {
        VTSD->setExternKeywordLoc(ExternLoc);
        VTSD->setTemplateKeywordLoc(TemplateLoc);
      }

      // Merge attributes.
      ProcessDeclAttributeList(S, Prev, D.getDeclSpec().getAttributes());
      if (PrevTemplate)
        ProcessAPINotes(Prev);

      if (TSK == TSK_ExplicitInstantiationDefinition)
        InstantiateVariableDefinition(D.getIdentifierLoc(), Prev);
    }

    // Check the new variable specialization against the parsed input.
    if (PrevTemplate && !Context.hasSameType(Prev->getType(), R)) {
      Diag(T->getTypeLoc().getBeginLoc(),
           diag::err_invalid_var_template_spec_type)
          << 0 << PrevTemplate << R << Prev->getType();
      Diag(PrevTemplate->getLocation(), diag::note_template_declared_here)
          << 2 << PrevTemplate->getDeclName();
      return true;
    }

    // FIXME: Create an ExplicitInstantiation node?
    return (Decl*) nullptr;
  }

  // If the declarator is a template-id, translate the parser's template
  // argument list into our AST format.
  bool HasExplicitTemplateArgs = false;
  TemplateArgumentListInfo TemplateArgs;
  if (D.getName().getKind() == UnqualifiedIdKind::IK_TemplateId) {
    TemplateArgs = makeTemplateArgumentListInfo(*this, *D.getName().TemplateId);
    HasExplicitTemplateArgs = true;
  }

  // C++ [temp.explicit]p1:
  //   A [...] function [...] can be explicitly instantiated from its template.
  //   A member function [...] of a class template can be explicitly
  //  instantiated from the member definition associated with its class
  //  template.
  UnresolvedSet<8> TemplateMatches;
  FunctionDecl *NonTemplateMatch = nullptr;
  TemplateSpecCandidateSet FailedCandidates(D.getIdentifierLoc());
  for (LookupResult::iterator P = Previous.begin(), PEnd = Previous.end();
       P != PEnd; ++P) {
    NamedDecl *Prev = *P;
    if (!HasExplicitTemplateArgs) {
      if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Prev)) {
        QualType Adjusted = adjustCCAndNoReturn(R, Method->getType(),
                                                /*AdjustExceptionSpec*/true);
        if (Context.hasSameUnqualifiedType(Method->getType(), Adjusted)) {
          if (Method->getPrimaryTemplate()) {
            TemplateMatches.addDecl(Method, P.getAccess());
          } else {
            // FIXME: Can this assert ever happen?  Needs a test.
            assert(!NonTemplateMatch && "Multiple NonTemplateMatches");
            NonTemplateMatch = Method;
          }
        }
      }
    }

    FunctionTemplateDecl *FunTmpl = dyn_cast<FunctionTemplateDecl>(Prev);
    if (!FunTmpl)
      continue;

    TemplateDeductionInfo Info(FailedCandidates.getLocation());
    FunctionDecl *Specialization = nullptr;
    if (TemplateDeductionResult TDK = DeduceTemplateArguments(
            FunTmpl, (HasExplicitTemplateArgs ? &TemplateArgs : nullptr), R,
            Specialization, Info);
        TDK != TemplateDeductionResult::Success) {
      // Keep track of almost-matches.
      FailedCandidates.addCandidate()
          .set(P.getPair(), FunTmpl->getTemplatedDecl(),
               MakeDeductionFailureInfo(Context, TDK, Info));
      (void)TDK;
      continue;
    }

    // Target attributes are part of the cuda function signature, so
    // the cuda target of the instantiated function must match that of its
    // template.  Given that C++ template deduction does not take
    // target attributes into account, we reject candidates here that
    // have a different target.
    if (LangOpts.CUDA &&
        CUDA().IdentifyTarget(Specialization,
                              /* IgnoreImplicitHDAttr = */ true) !=
            CUDA().IdentifyTarget(D.getDeclSpec().getAttributes())) {
      FailedCandidates.addCandidate().set(
          P.getPair(), FunTmpl->getTemplatedDecl(),
          MakeDeductionFailureInfo(
              Context, TemplateDeductionResult::CUDATargetMismatch, Info));
      continue;
    }

    TemplateMatches.addDecl(Specialization, P.getAccess());
  }

  FunctionDecl *Specialization = NonTemplateMatch;
  if (!Specialization) {
    // Find the most specialized function template specialization.
    UnresolvedSetIterator Result = getMostSpecialized(
        TemplateMatches.begin(), TemplateMatches.end(), FailedCandidates,
        D.getIdentifierLoc(),
        PDiag(diag::err_explicit_instantiation_not_known) << Name,
        PDiag(diag::err_explicit_instantiation_ambiguous) << Name,
        PDiag(diag::note_explicit_instantiation_candidate));

    if (Result == TemplateMatches.end())
      return true;

    // Ignore access control bits, we don't need them for redeclaration checking.
    Specialization = cast<FunctionDecl>(*Result);
  }

  // C++11 [except.spec]p4
  // In an explicit instantiation an exception-specification may be specified,
  // but is not required.
  // If an exception-specification is specified in an explicit instantiation
  // directive, it shall be compatible with the exception-specifications of
  // other declarations of that function.
  if (auto *FPT = R->getAs<FunctionProtoType>())
    if (FPT->hasExceptionSpec()) {
      unsigned DiagID =
          diag::err_mismatched_exception_spec_explicit_instantiation;
      if (getLangOpts().MicrosoftExt)
        DiagID = diag::ext_mismatched_exception_spec_explicit_instantiation;
      bool Result = CheckEquivalentExceptionSpec(
          PDiag(DiagID) << Specialization->getType(),
          PDiag(diag::note_explicit_instantiation_here),
          Specialization->getType()->getAs<FunctionProtoType>(),
          Specialization->getLocation(), FPT, D.getBeginLoc());
      // In Microsoft mode, mismatching exception specifications just cause a
      // warning.
      if (!getLangOpts().MicrosoftExt && Result)
        return true;
    }

  if (Specialization->getTemplateSpecializationKind() == TSK_Undeclared) {
    Diag(D.getIdentifierLoc(),
         diag::err_explicit_instantiation_member_function_not_instantiated)
      << Specialization
      << (Specialization->getTemplateSpecializationKind() ==
          TSK_ExplicitSpecialization);
    Diag(Specialization->getLocation(), diag::note_explicit_instantiation_here);
    return true;
  }

  FunctionDecl *PrevDecl = Specialization->getPreviousDecl();
  if (!PrevDecl && Specialization->isThisDeclarationADefinition())
    PrevDecl = Specialization;

  if (PrevDecl) {
    bool HasNoEffect = false;
    if (CheckSpecializationInstantiationRedecl(D.getIdentifierLoc(), TSK,
                                               PrevDecl,
                                     PrevDecl->getTemplateSpecializationKind(),
                                          PrevDecl->getPointOfInstantiation(),
                                               HasNoEffect))
      return true;

    // FIXME: We may still want to build some representation of this
    // explicit specialization.
    if (HasNoEffect)
      return (Decl*) nullptr;
  }

  // HACK: libc++ has a bug where it attempts to explicitly instantiate the
  // functions
  //     valarray<size_t>::valarray(size_t) and
  //     valarray<size_t>::~valarray()
  // that it declared to have internal linkage with the internal_linkage
  // attribute. Ignore the explicit instantiation declaration in this case.
  if (Specialization->hasAttr<InternalLinkageAttr>() &&
      TSK == TSK_ExplicitInstantiationDeclaration) {
    if (auto *RD = dyn_cast<CXXRecordDecl>(Specialization->getDeclContext()))
      if (RD->getIdentifier() && RD->getIdentifier()->isStr("valarray") &&
          RD->isInStdNamespace())
        return (Decl*) nullptr;
  }

  ProcessDeclAttributeList(S, Specialization, D.getDeclSpec().getAttributes());
  ProcessAPINotes(Specialization);

  // In MSVC mode, dllimported explicit instantiation definitions are treated as
  // instantiation declarations.
  if (TSK == TSK_ExplicitInstantiationDefinition &&
      Specialization->hasAttr<DLLImportAttr>() &&
      Context.getTargetInfo().getCXXABI().isMicrosoft())
    TSK = TSK_ExplicitInstantiationDeclaration;

  Specialization->setTemplateSpecializationKind(TSK, D.getIdentifierLoc());

  if (Specialization->isDefined()) {
    // Let the ASTConsumer know that this function has been explicitly
    // instantiated now, and its linkage might have changed.
    Consumer.HandleTopLevelDecl(DeclGroupRef(Specialization));
  } else if (TSK == TSK_ExplicitInstantiationDefinition)
    InstantiateFunctionDefinition(D.getIdentifierLoc(), Specialization);

  // C++0x [temp.explicit]p2:
  //   If the explicit instantiation is for a member function, a member class
  //   or a static data member of a class template specialization, the name of
  //   the class template specialization in the qualified-id for the member
  //   name shall be a simple-template-id.
  //
  // C++98 has the same restriction, just worded differently.
  FunctionTemplateDecl *FunTmpl = Specialization->getPrimaryTemplate();
  if (D.getName().getKind() != UnqualifiedIdKind::IK_TemplateId && !FunTmpl &&
      D.getCXXScopeSpec().isSet() &&
      !ScopeSpecifierHasTemplateId(D.getCXXScopeSpec()))
    Diag(D.getIdentifierLoc(),
         diag::ext_explicit_instantiation_without_qualified_id)
    << Specialization << D.getCXXScopeSpec().getRange();

  CheckExplicitInstantiation(
      *this,
      FunTmpl ? (NamedDecl *)FunTmpl
              : Specialization->getInstantiatedFromMemberFunction(),
      D.getIdentifierLoc(), D.getCXXScopeSpec().isSet(), TSK);

  // FIXME: Create some kind of ExplicitInstantiationDecl here.
  return (Decl*) nullptr;
}

TypeResult Sema::ActOnDependentTag(Scope *S, unsigned TagSpec, TagUseKind TUK,
                                   const CXXScopeSpec &SS,
                                   const IdentifierInfo *Name,
                                   SourceLocation TagLoc,
                                   SourceLocation NameLoc) {
  // This has to hold, because SS is expected to be defined.
  assert(Name && "Expected a name in a dependent tag");

  NestedNameSpecifier *NNS = SS.getScopeRep();
  if (!NNS)
    return true;

  TagTypeKind Kind = TypeWithKeyword::getTagTypeKindForTypeSpec(TagSpec);

  if (TUK == TagUseKind::Declaration || TUK == TagUseKind::Definition) {
    Diag(NameLoc, diag::err_dependent_tag_decl)
        << (TUK == TagUseKind::Definition) << llvm::to_underlying(Kind)
        << SS.getRange();
    return true;
  }

  // Create the resulting type.
  ElaboratedTypeKeyword Kwd = TypeWithKeyword::getKeywordForTagTypeKind(Kind);
  QualType Result = Context.getDependentNameType(Kwd, NNS, Name);

  // Create type-source location information for this type.
  TypeLocBuilder TLB;
  DependentNameTypeLoc TL = TLB.push<DependentNameTypeLoc>(Result);
  TL.setElaboratedKeywordLoc(TagLoc);
  TL.setQualifierLoc(SS.getWithLocInContext(Context));
  TL.setNameLoc(NameLoc);
  return CreateParsedType(Result, TLB.getTypeSourceInfo(Context, Result));
}

TypeResult Sema::ActOnTypenameType(Scope *S, SourceLocation TypenameLoc,
                                   const CXXScopeSpec &SS,
                                   const IdentifierInfo &II,
                                   SourceLocation IdLoc,
                                   ImplicitTypenameContext IsImplicitTypename) {
  if (SS.isInvalid())
    return true;

  if (TypenameLoc.isValid() && S && !S->getTemplateParamParent())
    Diag(TypenameLoc,
         getLangOpts().CPlusPlus11 ?
           diag::warn_cxx98_compat_typename_outside_of_template :
           diag::ext_typename_outside_of_template)
      << FixItHint::CreateRemoval(TypenameLoc);

  NestedNameSpecifierLoc QualifierLoc = SS.getWithLocInContext(Context);
  TypeSourceInfo *TSI = nullptr;
  QualType T =
      CheckTypenameType((TypenameLoc.isValid() ||
                         IsImplicitTypename == ImplicitTypenameContext::Yes)
                            ? ElaboratedTypeKeyword::Typename
                            : ElaboratedTypeKeyword::None,
                        TypenameLoc, QualifierLoc, II, IdLoc, &TSI,
                        /*DeducedTSTContext=*/true);
  if (T.isNull())
    return true;
  return CreateParsedType(T, TSI);
}

TypeResult
Sema::ActOnTypenameType(Scope *S, SourceLocation TypenameLoc,
                        const CXXScopeSpec &SS, SourceLocation TemplateKWLoc,
                        TemplateTy TemplateIn, const IdentifierInfo *TemplateII,
                        SourceLocation TemplateIILoc, SourceLocation LAngleLoc,
                        ASTTemplateArgsPtr TemplateArgsIn,
                        SourceLocation RAngleLoc) {
  if (TypenameLoc.isValid() && S && !S->getTemplateParamParent())
    Diag(TypenameLoc,
         getLangOpts().CPlusPlus11 ?
           diag::warn_cxx98_compat_typename_outside_of_template :
           diag::ext_typename_outside_of_template)
      << FixItHint::CreateRemoval(TypenameLoc);

  // Strangely, non-type results are not ignored by this lookup, so the
  // program is ill-formed if it finds an injected-class-name.
  if (TypenameLoc.isValid()) {
    auto *LookupRD =
        dyn_cast_or_null<CXXRecordDecl>(computeDeclContext(SS, false));
    if (LookupRD && LookupRD->getIdentifier() == TemplateII) {
      Diag(TemplateIILoc,
           diag::ext_out_of_line_qualified_id_type_names_constructor)
        << TemplateII << 0 /*injected-class-name used as template name*/
        << (TemplateKWLoc.isValid() ? 1 : 0 /*'template'/'typename' keyword*/);
    }
  }

  // Translate the parser's template argument list in our AST format.
  TemplateArgumentListInfo TemplateArgs(LAngleLoc, RAngleLoc);
  translateTemplateArguments(TemplateArgsIn, TemplateArgs);

  TemplateName Template = TemplateIn.get();
  if (DependentTemplateName *DTN = Template.getAsDependentTemplateName()) {
    // Construct a dependent template specialization type.
    assert(DTN && "dependent template has non-dependent name?");
    assert(DTN->getQualifier() == SS.getScopeRep());

    if (!DTN->isIdentifier()) {
      Diag(TemplateIILoc, diag::err_template_id_not_a_type) << Template;
      NoteAllFoundTemplates(Template);
      return true;
    }

    QualType T = Context.getDependentTemplateSpecializationType(
        ElaboratedTypeKeyword::Typename, DTN->getQualifier(),
        DTN->getIdentifier(), TemplateArgs.arguments());

    // Create source-location information for this type.
    TypeLocBuilder Builder;
    DependentTemplateSpecializationTypeLoc SpecTL
    = Builder.push<DependentTemplateSpecializationTypeLoc>(T);
    SpecTL.setElaboratedKeywordLoc(TypenameLoc);
    SpecTL.setQualifierLoc(SS.getWithLocInContext(Context));
    SpecTL.setTemplateKeywordLoc(TemplateKWLoc);
    SpecTL.setTemplateNameLoc(TemplateIILoc);
    SpecTL.setLAngleLoc(LAngleLoc);
    SpecTL.setRAngleLoc(RAngleLoc);
    for (unsigned I = 0, N = TemplateArgs.size(); I != N; ++I)
      SpecTL.setArgLocInfo(I, TemplateArgs[I].getLocInfo());
    return CreateParsedType(T, Builder.getTypeSourceInfo(Context, T));
  }

  QualType T = CheckTemplateIdType(Template, TemplateIILoc, TemplateArgs);
  if (T.isNull())
    return true;

  // Provide source-location information for the template specialization type.
  TypeLocBuilder Builder;
  TemplateSpecializationTypeLoc SpecTL
    = Builder.push<TemplateSpecializationTypeLoc>(T);
  SpecTL.setTemplateKeywordLoc(TemplateKWLoc);
  SpecTL.setTemplateNameLoc(TemplateIILoc);
  SpecTL.setLAngleLoc(LAngleLoc);
  SpecTL.setRAngleLoc(RAngleLoc);
  for (unsigned I = 0, N = TemplateArgs.size(); I != N; ++I)
    SpecTL.setArgLocInfo(I, TemplateArgs[I].getLocInfo());

  T = Context.getElaboratedType(ElaboratedTypeKeyword::Typename,
                                SS.getScopeRep(), T);
  ElaboratedTypeLoc TL = Builder.push<ElaboratedTypeLoc>(T);
  TL.setElaboratedKeywordLoc(TypenameLoc);
  TL.setQualifierLoc(SS.getWithLocInContext(Context));

  TypeSourceInfo *TSI = Builder.getTypeSourceInfo(Context, T);
  return CreateParsedType(T, TSI);
}

/// Determine whether this failed name lookup should be treated as being
/// disabled by a usage of std::enable_if.
static bool isEnableIf(NestedNameSpecifierLoc NNS, const IdentifierInfo &II,
                       SourceRange &CondRange, Expr *&Cond) {
  // We must be looking for a ::type...
  if (!II.isStr("type"))
    return false;

  // ... within an explicitly-written template specialization...
  if (!NNS || !NNS.getNestedNameSpecifier()->getAsType())
    return false;
  TypeLoc EnableIfTy = NNS.getTypeLoc();
  TemplateSpecializationTypeLoc EnableIfTSTLoc =
      EnableIfTy.getAs<TemplateSpecializationTypeLoc>();
  if (!EnableIfTSTLoc || EnableIfTSTLoc.getNumArgs() == 0)
    return false;
  const TemplateSpecializationType *EnableIfTST = EnableIfTSTLoc.getTypePtr();

  // ... which names a complete class template declaration...
  const TemplateDecl *EnableIfDecl =
    EnableIfTST->getTemplateName().getAsTemplateDecl();
  if (!EnableIfDecl || EnableIfTST->isIncompleteType())
    return false;

  // ... called "enable_if".
  const IdentifierInfo *EnableIfII =
    EnableIfDecl->getDeclName().getAsIdentifierInfo();
  if (!EnableIfII || !EnableIfII->isStr("enable_if"))
    return false;

  // Assume the first template argument is the condition.
  CondRange = EnableIfTSTLoc.getArgLoc(0).getSourceRange();

  // Dig out the condition.
  Cond = nullptr;
  if (EnableIfTSTLoc.getArgLoc(0).getArgument().getKind()
        != TemplateArgument::Expression)
    return true;

  Cond = EnableIfTSTLoc.getArgLoc(0).getSourceExpression();

  // Ignore Boolean literals; they add no value.
  if (isa<CXXBoolLiteralExpr>(Cond->IgnoreParenCasts()))
    Cond = nullptr;

  return true;
}

QualType
Sema::CheckTypenameType(ElaboratedTypeKeyword Keyword,
                        SourceLocation KeywordLoc,
                        NestedNameSpecifierLoc QualifierLoc,
                        const IdentifierInfo &II,
                        SourceLocation IILoc,
                        TypeSourceInfo **TSI,
                        bool DeducedTSTContext) {
  QualType T = CheckTypenameType(Keyword, KeywordLoc, QualifierLoc, II, IILoc,
                                 DeducedTSTContext);
  if (T.isNull())
    return QualType();

  *TSI = Context.CreateTypeSourceInfo(T);
  if (isa<DependentNameType>(T)) {
    DependentNameTypeLoc TL =
        (*TSI)->getTypeLoc().castAs<DependentNameTypeLoc>();
    TL.setElaboratedKeywordLoc(KeywordLoc);
    TL.setQualifierLoc(QualifierLoc);
    TL.setNameLoc(IILoc);
  } else {
    ElaboratedTypeLoc TL = (*TSI)->getTypeLoc().castAs<ElaboratedTypeLoc>();
    TL.setElaboratedKeywordLoc(KeywordLoc);
    TL.setQualifierLoc(QualifierLoc);
    TL.getNamedTypeLoc().castAs<TypeSpecTypeLoc>().setNameLoc(IILoc);
  }
  return T;
}

/// Build the type that describes a C++ typename specifier,
/// e.g., "typename T::type".
QualType
Sema::CheckTypenameType(ElaboratedTypeKeyword Keyword,
                        SourceLocation KeywordLoc,
                        NestedNameSpecifierLoc QualifierLoc,
                        const IdentifierInfo &II,
                        SourceLocation IILoc, bool DeducedTSTContext) {
  CXXScopeSpec SS;
  SS.Adopt(QualifierLoc);

  DeclContext *Ctx = nullptr;
  if (QualifierLoc) {
    Ctx = computeDeclContext(SS);
    if (!Ctx) {
      // If the nested-name-specifier is dependent and couldn't be
      // resolved to a type, build a typename type.
      assert(QualifierLoc.getNestedNameSpecifier()->isDependent());
      return Context.getDependentNameType(Keyword,
                                          QualifierLoc.getNestedNameSpecifier(),
                                          &II);
    }

    // If the nested-name-specifier refers to the current instantiation,
    // the "typename" keyword itself is superfluous. In C++03, the
    // program is actually ill-formed. However, DR 382 (in C++0x CD1)
    // allows such extraneous "typename" keywords, and we retroactively
    // apply this DR to C++03 code with only a warning. In any case we continue.

    if (RequireCompleteDeclContext(SS, Ctx))
      return QualType();
  }

  DeclarationName Name(&II);
  LookupResult Result(*this, Name, IILoc, LookupOrdinaryName);
  if (Ctx)
    LookupQualifiedName(Result, Ctx, SS);
  else
    LookupName(Result, CurScope);
  unsigned DiagID = 0;
  Decl *Referenced = nullptr;
  switch (Result.getResultKind()) {
  case LookupResult::NotFound: {
    // If we're looking up 'type' within a template named 'enable_if', produce
    // a more specific diagnostic.
    SourceRange CondRange;
    Expr *Cond = nullptr;
    if (Ctx && isEnableIf(QualifierLoc, II, CondRange, Cond)) {
      // If we have a condition, narrow it down to the specific failed
      // condition.
      if (Cond) {
        Expr *FailedCond;
        std::string FailedDescription;
        std::tie(FailedCond, FailedDescription) =
          findFailedBooleanCondition(Cond);

        Diag(FailedCond->getExprLoc(),
             diag::err_typename_nested_not_found_requirement)
          << FailedDescription
          << FailedCond->getSourceRange();
        return QualType();
      }

      Diag(CondRange.getBegin(),
           diag::err_typename_nested_not_found_enable_if)
          << Ctx << CondRange;
      return QualType();
    }

    DiagID = Ctx ? diag::err_typename_nested_not_found
                 : diag::err_unknown_typename;
    break;
  }

  case LookupResult::FoundUnresolvedValue: {
    // We found a using declaration that is a value. Most likely, the using
    // declaration itself is meant to have the 'typename' keyword.
    SourceRange FullRange(KeywordLoc.isValid() ? KeywordLoc : SS.getBeginLoc(),
                          IILoc);
    Diag(IILoc, diag::err_typename_refers_to_using_value_decl)
      << Name << Ctx << FullRange;
    if (UnresolvedUsingValueDecl *Using
          = dyn_cast<UnresolvedUsingValueDecl>(Result.getRepresentativeDecl())){
      SourceLocation Loc = Using->getQualifierLoc().getBeginLoc();
      Diag(Loc, diag::note_using_value_decl_missing_typename)
        << FixItHint::CreateInsertion(Loc, "typename ");
    }
  }
  // Fall through to create a dependent typename type, from which we can recover
  // better.
  [[fallthrough]];

  case LookupResult::NotFoundInCurrentInstantiation:
    // Okay, it's a member of an unknown instantiation.
    return Context.getDependentNameType(Keyword,
                                        QualifierLoc.getNestedNameSpecifier(),
                                        &II);

  case LookupResult::Found:
    if (TypeDecl *Type = dyn_cast<TypeDecl>(Result.getFoundDecl())) {
      // C++ [class.qual]p2:
      //   In a lookup in which function names are not ignored and the
      //   nested-name-specifier nominates a class C, if the name specified
      //   after the nested-name-specifier, when looked up in C, is the
      //   injected-class-name of C [...] then the name is instead considered
      //   to name the constructor of class C.
      //
      // Unlike in an elaborated-type-specifier, function names are not ignored
      // in typename-specifier lookup. However, they are ignored in all the
      // contexts where we form a typename type with no keyword (that is, in
      // mem-initializer-ids, base-specifiers, and elaborated-type-specifiers).
      //
      // FIXME: That's not strictly true: mem-initializer-id lookup does not
      // ignore functions, but that appears to be an oversight.
      auto *LookupRD = dyn_cast_or_null<CXXRecordDecl>(Ctx);
      auto *FoundRD = dyn_cast<CXXRecordDecl>(Type);
      if (Keyword == ElaboratedTypeKeyword::Typename && LookupRD && FoundRD &&
          FoundRD->isInjectedClassName() &&
          declaresSameEntity(LookupRD, cast<Decl>(FoundRD->getParent())))
        Diag(IILoc, diag::ext_out_of_line_qualified_id_type_names_constructor)
            << &II << 1 << 0 /*'typename' keyword used*/;

      // We found a type. Build an ElaboratedType, since the
      // typename-specifier was just sugar.
      MarkAnyDeclReferenced(Type->getLocation(), Type, /*OdrUse=*/false);
      return Context.getElaboratedType(Keyword,
                                       QualifierLoc.getNestedNameSpecifier(),
                                       Context.getTypeDeclType(Type));
    }

    // C++ [dcl.type.simple]p2:
    //   A type-specifier of the form
    //     typename[opt] nested-name-specifier[opt] template-name
    //   is a placeholder for a deduced class type [...].
    if (getLangOpts().CPlusPlus17) {
      if (auto *TD = getAsTypeTemplateDecl(Result.getFoundDecl())) {
        if (!DeducedTSTContext) {
          QualType T(QualifierLoc
                         ? QualifierLoc.getNestedNameSpecifier()->getAsType()
                         : nullptr, 0);
          if (!T.isNull())
            Diag(IILoc, diag::err_dependent_deduced_tst)
              << (int)getTemplateNameKindForDiagnostics(TemplateName(TD)) << T;
          else
            Diag(IILoc, diag::err_deduced_tst)
              << (int)getTemplateNameKindForDiagnostics(TemplateName(TD));
          NoteTemplateLocation(*TD);
          return QualType();
        }
        return Context.getElaboratedType(
            Keyword, QualifierLoc.getNestedNameSpecifier(),
            Context.getDeducedTemplateSpecializationType(TemplateName(TD),
                                                         QualType(), false));
      }
    }

    DiagID = Ctx ? diag::err_typename_nested_not_type
                 : diag::err_typename_not_type;
    Referenced = Result.getFoundDecl();
    break;

  case LookupResult::FoundOverloaded:
    DiagID = Ctx ? diag::err_typename_nested_not_type
                 : diag::err_typename_not_type;
    Referenced = *Result.begin();
    break;

  case LookupResult::Ambiguous:
    return QualType();
  }

  // If we get here, it's because name lookup did not find a
  // type. Emit an appropriate diagnostic and return an error.
  SourceRange FullRange(KeywordLoc.isValid() ? KeywordLoc : SS.getBeginLoc(),
                        IILoc);
  if (Ctx)
    Diag(IILoc, DiagID) << FullRange << Name << Ctx;
  else
    Diag(IILoc, DiagID) << FullRange << Name;
  if (Referenced)
    Diag(Referenced->getLocation(),
         Ctx ? diag::note_typename_member_refers_here
             : diag::note_typename_refers_here)
      << Name;
  return QualType();
}

namespace {
  // See Sema::RebuildTypeInCurrentInstantiation
  class CurrentInstantiationRebuilder
    : public TreeTransform<CurrentInstantiationRebuilder> {
    SourceLocation Loc;
    DeclarationName Entity;

  public:
    typedef TreeTransform<CurrentInstantiationRebuilder> inherited;

    CurrentInstantiationRebuilder(Sema &SemaRef,
                                  SourceLocation Loc,
                                  DeclarationName Entity)
    : TreeTransform<CurrentInstantiationRebuilder>(SemaRef),
      Loc(Loc), Entity(Entity) { }

    /// Determine whether the given type \p T has already been
    /// transformed.
    ///
    /// For the purposes of type reconstruction, a type has already been
    /// transformed if it is NULL or if it is not dependent.
    bool AlreadyTransformed(QualType T) {
      return T.isNull() || !T->isInstantiationDependentType();
    }

    /// Returns the location of the entity whose type is being
    /// rebuilt.
    SourceLocation getBaseLocation() { return Loc; }

    /// Returns the name of the entity whose type is being rebuilt.
    DeclarationName getBaseEntity() { return Entity; }

    /// Sets the "base" location and entity when that
    /// information is known based on another transformation.
    void setBase(SourceLocation Loc, DeclarationName Entity) {
      this->Loc = Loc;
      this->Entity = Entity;
    }

    ExprResult TransformLambdaExpr(LambdaExpr *E) {
      // Lambdas never need to be transformed.
      return E;
    }
  };
} // end anonymous namespace

TypeSourceInfo *Sema::RebuildTypeInCurrentInstantiation(TypeSourceInfo *T,
                                                        SourceLocation Loc,
                                                        DeclarationName Name) {
  if (!T || !T->getType()->isInstantiationDependentType())
    return T;

  CurrentInstantiationRebuilder Rebuilder(*this, Loc, Name);
  return Rebuilder.TransformType(T);
}

ExprResult Sema::RebuildExprInCurrentInstantiation(Expr *E) {
  CurrentInstantiationRebuilder Rebuilder(*this, E->getExprLoc(),
                                          DeclarationName());
  return Rebuilder.TransformExpr(E);
}

bool Sema::RebuildNestedNameSpecifierInCurrentInstantiation(CXXScopeSpec &SS) {
  if (SS.isInvalid())
    return true;

  NestedNameSpecifierLoc QualifierLoc = SS.getWithLocInContext(Context);
  CurrentInstantiationRebuilder Rebuilder(*this, SS.getRange().getBegin(),
                                          DeclarationName());
  NestedNameSpecifierLoc Rebuilt
    = Rebuilder.TransformNestedNameSpecifierLoc(QualifierLoc);
  if (!Rebuilt)
    return true;

  SS.Adopt(Rebuilt);
  return false;
}

bool Sema::RebuildTemplateParamsInCurrentInstantiation(
                                               TemplateParameterList *Params) {
  for (unsigned I = 0, N = Params->size(); I != N; ++I) {
    Decl *Param = Params->getParam(I);

    // There is nothing to rebuild in a type parameter.
    if (isa<TemplateTypeParmDecl>(Param))
      continue;

    // Rebuild the template parameter list of a template template parameter.
    if (TemplateTemplateParmDecl *TTP
        = dyn_cast<TemplateTemplateParmDecl>(Param)) {
      if (RebuildTemplateParamsInCurrentInstantiation(
            TTP->getTemplateParameters()))
        return true;

      continue;
    }

    // Rebuild the type of a non-type template parameter.
    NonTypeTemplateParmDecl *NTTP = cast<NonTypeTemplateParmDecl>(Param);
    TypeSourceInfo *NewTSI
      = RebuildTypeInCurrentInstantiation(NTTP->getTypeSourceInfo(),
                                          NTTP->getLocation(),
                                          NTTP->getDeclName());
    if (!NewTSI)
      return true;

    if (NewTSI->getType()->isUndeducedType()) {
      // C++17 [temp.dep.expr]p3:
      //   An id-expression is type-dependent if it contains
      //    - an identifier associated by name lookup with a non-type
      //      template-parameter declared with a type that contains a
      //      placeholder type (7.1.7.4),
      NewTSI = SubstAutoTypeSourceInfoDependent(NewTSI);
    }

    if (NewTSI != NTTP->getTypeSourceInfo()) {
      NTTP->setTypeSourceInfo(NewTSI);
      NTTP->setType(NewTSI->getType());
    }
  }

  return false;
}

std::string
Sema::getTemplateArgumentBindingsText(const TemplateParameterList *Params,
                                      const TemplateArgumentList &Args) {
  return getTemplateArgumentBindingsText(Params, Args.data(), Args.size());
}

std::string
Sema::getTemplateArgumentBindingsText(const TemplateParameterList *Params,
                                      const TemplateArgument *Args,
                                      unsigned NumArgs) {
  SmallString<128> Str;
  llvm::raw_svector_ostream Out(Str);

  if (!Params || Params->size() == 0 || NumArgs == 0)
    return std::string();

  for (unsigned I = 0, N = Params->size(); I != N; ++I) {
    if (I >= NumArgs)
      break;

    if (I == 0)
      Out << "[with ";
    else
      Out << ", ";

    if (const IdentifierInfo *Id = Params->getParam(I)->getIdentifier()) {
      Out << Id->getName();
    } else {
      Out << '$' << I;
    }

    Out << " = ";
    Args[I].print(getPrintingPolicy(), Out,
                  TemplateParameterList::shouldIncludeTypeForArgument(
                      getPrintingPolicy(), Params, I));
  }

  Out << ']';
  return std::string(Out.str());
}

void Sema::MarkAsLateParsedTemplate(FunctionDecl *FD, Decl *FnD,
                                    CachedTokens &Toks) {
  if (!FD)
    return;

  auto LPT = std::make_unique<LateParsedTemplate>();

  // Take tokens to avoid allocations
  LPT->Toks.swap(Toks);
  LPT->D = FnD;
  LPT->FPO = getCurFPFeatures();
  LateParsedTemplateMap.insert(std::make_pair(FD, std::move(LPT)));

  FD->setLateTemplateParsed(true);
}

void Sema::UnmarkAsLateParsedTemplate(FunctionDecl *FD) {
  if (!FD)
    return;
  FD->setLateTemplateParsed(false);
}

bool Sema::IsInsideALocalClassWithinATemplateFunction() {
  DeclContext *DC = CurContext;

  while (DC) {
    if (CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(CurContext)) {
      const FunctionDecl *FD = RD->isLocalClass();
      return (FD && FD->getTemplatedKind() != FunctionDecl::TK_NonTemplate);
    } else if (DC->isTranslationUnit() || DC->isNamespace())
      return false;

    DC = DC->getParent();
  }
  return false;
}

namespace {
/// Walk the path from which a declaration was instantiated, and check
/// that every explicit specialization along that path is visible. This enforces
/// C++ [temp.expl.spec]/6:
///
///   If a template, a member template or a member of a class template is
///   explicitly specialized then that specialization shall be declared before
///   the first use of that specialization that would cause an implicit
///   instantiation to take place, in every translation unit in which such a
///   use occurs; no diagnostic is required.
///
/// and also C++ [temp.class.spec]/1:
///
///   A partial specialization shall be declared before the first use of a
///   class template specialization that would make use of the partial
///   specialization as the result of an implicit or explicit instantiation
///   in every translation unit in which such a use occurs; no diagnostic is
///   required.
class ExplicitSpecializationVisibilityChecker {
  Sema &S;
  SourceLocation Loc;
  llvm::SmallVector<Module *, 8> Modules;
  Sema::AcceptableKind Kind;

public:
  ExplicitSpecializationVisibilityChecker(Sema &S, SourceLocation Loc,
                                          Sema::AcceptableKind Kind)
      : S(S), Loc(Loc), Kind(Kind) {}

  void check(NamedDecl *ND) {
    if (auto *FD = dyn_cast<FunctionDecl>(ND))
      return checkImpl(FD);
    if (auto *RD = dyn_cast<CXXRecordDecl>(ND))
      return checkImpl(RD);
    if (auto *VD = dyn_cast<VarDecl>(ND))
      return checkImpl(VD);
    if (auto *ED = dyn_cast<EnumDecl>(ND))
      return checkImpl(ED);
  }

private:
  void diagnose(NamedDecl *D, bool IsPartialSpec) {
    auto Kind = IsPartialSpec ? Sema::MissingImportKind::PartialSpecialization
                              : Sema::MissingImportKind::ExplicitSpecialization;
    const bool Recover = true;

    // If we got a custom set of modules (because only a subset of the
    // declarations are interesting), use them, otherwise let
    // diagnoseMissingImport intelligently pick some.
    if (Modules.empty())
      S.diagnoseMissingImport(Loc, D, Kind, Recover);
    else
      S.diagnoseMissingImport(Loc, D, D->getLocation(), Modules, Kind, Recover);
  }

  bool CheckMemberSpecialization(const NamedDecl *D) {
    return Kind == Sema::AcceptableKind::Visible
               ? S.hasVisibleMemberSpecialization(D)
               : S.hasReachableMemberSpecialization(D);
  }

  bool CheckExplicitSpecialization(const NamedDecl *D) {
    return Kind == Sema::AcceptableKind::Visible
               ? S.hasVisibleExplicitSpecialization(D)
               : S.hasReachableExplicitSpecialization(D);
  }

  bool CheckDeclaration(const NamedDecl *D) {
    return Kind == Sema::AcceptableKind::Visible ? S.hasVisibleDeclaration(D)
                                                 : S.hasReachableDeclaration(D);
  }

  // Check a specific declaration. There are three problematic cases:
  //
  //  1) The declaration is an explicit specialization of a template
  //     specialization.
  //  2) The declaration is an explicit specialization of a member of an
  //     templated class.
  //  3) The declaration is an instantiation of a template, and that template
  //     is an explicit specialization of a member of a templated class.
  //
  // We don't need to go any deeper than that, as the instantiation of the
  // surrounding class / etc is not triggered by whatever triggered this
  // instantiation, and thus should be checked elsewhere.
  template<typename SpecDecl>
  void checkImpl(SpecDecl *Spec) {
    bool IsHiddenExplicitSpecialization = false;
    if (Spec->getTemplateSpecializationKind() == TSK_ExplicitSpecialization) {
      IsHiddenExplicitSpecialization = Spec->getMemberSpecializationInfo()
                                           ? !CheckMemberSpecialization(Spec)
                                           : !CheckExplicitSpecialization(Spec);
    } else {
      checkInstantiated(Spec);
    }

    if (IsHiddenExplicitSpecialization)
      diagnose(Spec->getMostRecentDecl(), false);
  }

  void checkInstantiated(FunctionDecl *FD) {
    if (auto *TD = FD->getPrimaryTemplate())
      checkTemplate(TD);
  }

  void checkInstantiated(CXXRecordDecl *RD) {
    auto *SD = dyn_cast<ClassTemplateSpecializationDecl>(RD);
    if (!SD)
      return;

    auto From = SD->getSpecializedTemplateOrPartial();
    if (auto *TD = From.dyn_cast<ClassTemplateDecl *>())
      checkTemplate(TD);
    else if (auto *TD =
                 From.dyn_cast<ClassTemplatePartialSpecializationDecl *>()) {
      if (!CheckDeclaration(TD))
        diagnose(TD, true);
      checkTemplate(TD);
    }
  }

  void checkInstantiated(VarDecl *RD) {
    auto *SD = dyn_cast<VarTemplateSpecializationDecl>(RD);
    if (!SD)
      return;

    auto From = SD->getSpecializedTemplateOrPartial();
    if (auto *TD = From.dyn_cast<VarTemplateDecl *>())
      checkTemplate(TD);
    else if (auto *TD =
                 From.dyn_cast<VarTemplatePartialSpecializationDecl *>()) {
      if (!CheckDeclaration(TD))
        diagnose(TD, true);
      checkTemplate(TD);
    }
  }

  void checkInstantiated(EnumDecl *FD) {}

  template<typename TemplDecl>
  void checkTemplate(TemplDecl *TD) {
    if (TD->isMemberSpecialization()) {
      if (!CheckMemberSpecialization(TD))
        diagnose(TD->getMostRecentDecl(), false);
    }
  }
};
} // end anonymous namespace

void Sema::checkSpecializationVisibility(SourceLocation Loc, NamedDecl *Spec) {
  if (!getLangOpts().Modules)
    return;

  ExplicitSpecializationVisibilityChecker(*this, Loc,
                                          Sema::AcceptableKind::Visible)
      .check(Spec);
}

void Sema::checkSpecializationReachability(SourceLocation Loc,
                                           NamedDecl *Spec) {
  if (!getLangOpts().CPlusPlusModules)
    return checkSpecializationVisibility(Loc, Spec);

  ExplicitSpecializationVisibilityChecker(*this, Loc,
                                          Sema::AcceptableKind::Reachable)
      .check(Spec);
}

SourceLocation Sema::getTopMostPointOfInstantiation(const NamedDecl *N) const {
  if (!getLangOpts().CPlusPlus || CodeSynthesisContexts.empty())
    return N->getLocation();
  if (const auto *FD = dyn_cast<FunctionDecl>(N)) {
    if (!FD->isFunctionTemplateSpecialization())
      return FD->getLocation();
  } else if (!isa<ClassTemplateSpecializationDecl,
                  VarTemplateSpecializationDecl>(N)) {
    return N->getLocation();
  }
  for (const CodeSynthesisContext &CSC : CodeSynthesisContexts) {
    if (!CSC.isInstantiationRecord() || CSC.PointOfInstantiation.isInvalid())
      continue;
    return CSC.PointOfInstantiation;
  }
  return N->getLocation();
}
