//===--- SemaDecl.cpp - Semantic Analysis for Declarations ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for declarations.
//
//===----------------------------------------------------------------------===//

#include "TypeLocBuilder.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/CommentDiagnostic.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/NonTrivialTypeVisitor.h"
#include "clang/AST/Randstruct.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/Type.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/HLSLRuntime.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/HeaderSearch.h" // TODO: Sema shouldn't depend on Lex
#include "clang/Lex/Lexer.h" // TODO: Extract static functions to fix layering.
#include "clang/Lex/ModuleLoader.h" // TODO: Sema shouldn't depend on Lex
#include "clang/Lex/Preprocessor.h" // Included for isCodeCompletionEnabled()
#include "clang/Sema/CXXFieldCollector.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/DelayedDiagnostic.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaHLSL.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaOpenMP.h"
#include "clang/Sema/SemaPPC.h"
#include "clang/Sema/SemaRISCV.h"
#include "clang/Sema/SemaSwift.h"
#include "clang/Sema/SemaWasm.h"
#include "clang/Sema/Template.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>
#include <unordered_map>

using namespace clang;
using namespace sema;

Sema::DeclGroupPtrTy Sema::ConvertDeclToDeclGroup(Decl *Ptr, Decl *OwnedType) {
  if (OwnedType) {
    Decl *Group[2] = { OwnedType, Ptr };
    return DeclGroupPtrTy::make(DeclGroupRef::Create(Context, Group, 2));
  }

  return DeclGroupPtrTy::make(DeclGroupRef(Ptr));
}

namespace {

class TypeNameValidatorCCC final : public CorrectionCandidateCallback {
 public:
   TypeNameValidatorCCC(bool AllowInvalid, bool WantClass = false,
                        bool AllowTemplates = false,
                        bool AllowNonTemplates = true)
       : AllowInvalidDecl(AllowInvalid), WantClassName(WantClass),
         AllowTemplates(AllowTemplates), AllowNonTemplates(AllowNonTemplates) {
     WantExpressionKeywords = false;
     WantCXXNamedCasts = false;
     WantRemainingKeywords = false;
  }

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    if (NamedDecl *ND = candidate.getCorrectionDecl()) {
      if (!AllowInvalidDecl && ND->isInvalidDecl())
        return false;

      if (getAsTypeTemplateDecl(ND))
        return AllowTemplates;

      bool IsType = isa<TypeDecl>(ND) || isa<ObjCInterfaceDecl>(ND);
      if (!IsType)
        return false;

      if (AllowNonTemplates)
        return true;

      // An injected-class-name of a class template (specialization) is valid
      // as a template or as a non-template.
      if (AllowTemplates) {
        auto *RD = dyn_cast<CXXRecordDecl>(ND);
        if (!RD || !RD->isInjectedClassName())
          return false;
        RD = cast<CXXRecordDecl>(RD->getDeclContext());
        return RD->getDescribedClassTemplate() ||
               isa<ClassTemplateSpecializationDecl>(RD);
      }

      return false;
    }

    return !WantClassName && candidate.isKeyword();
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<TypeNameValidatorCCC>(*this);
  }

 private:
  bool AllowInvalidDecl;
  bool WantClassName;
  bool AllowTemplates;
  bool AllowNonTemplates;
};

} // end anonymous namespace

namespace {
enum class UnqualifiedTypeNameLookupResult {
  NotFound,
  FoundNonType,
  FoundType
};
} // end anonymous namespace

/// Tries to perform unqualified lookup of the type decls in bases for
/// dependent class.
/// \return \a NotFound if no any decls is found, \a FoundNotType if found not a
/// type decl, \a FoundType if only type decls are found.
static UnqualifiedTypeNameLookupResult
lookupUnqualifiedTypeNameInBase(Sema &S, const IdentifierInfo &II,
                                SourceLocation NameLoc,
                                const CXXRecordDecl *RD) {
  if (!RD->hasDefinition())
    return UnqualifiedTypeNameLookupResult::NotFound;
  // Look for type decls in base classes.
  UnqualifiedTypeNameLookupResult FoundTypeDecl =
      UnqualifiedTypeNameLookupResult::NotFound;
  for (const auto &Base : RD->bases()) {
    const CXXRecordDecl *BaseRD = nullptr;
    if (auto *BaseTT = Base.getType()->getAs<TagType>())
      BaseRD = BaseTT->getAsCXXRecordDecl();
    else if (auto *TST = Base.getType()->getAs<TemplateSpecializationType>()) {
      // Look for type decls in dependent base classes that have known primary
      // templates.
      if (!TST || !TST->isDependentType())
        continue;
      auto *TD = TST->getTemplateName().getAsTemplateDecl();
      if (!TD)
        continue;
      if (auto *BasePrimaryTemplate =
          dyn_cast_or_null<CXXRecordDecl>(TD->getTemplatedDecl())) {
        if (BasePrimaryTemplate->getCanonicalDecl() != RD->getCanonicalDecl())
          BaseRD = BasePrimaryTemplate;
        else if (auto *CTD = dyn_cast<ClassTemplateDecl>(TD)) {
          if (const ClassTemplatePartialSpecializationDecl *PS =
                  CTD->findPartialSpecialization(Base.getType()))
            if (PS->getCanonicalDecl() != RD->getCanonicalDecl())
              BaseRD = PS;
        }
      }
    }
    if (BaseRD) {
      for (NamedDecl *ND : BaseRD->lookup(&II)) {
        if (!isa<TypeDecl>(ND))
          return UnqualifiedTypeNameLookupResult::FoundNonType;
        FoundTypeDecl = UnqualifiedTypeNameLookupResult::FoundType;
      }
      if (FoundTypeDecl == UnqualifiedTypeNameLookupResult::NotFound) {
        switch (lookupUnqualifiedTypeNameInBase(S, II, NameLoc, BaseRD)) {
        case UnqualifiedTypeNameLookupResult::FoundNonType:
          return UnqualifiedTypeNameLookupResult::FoundNonType;
        case UnqualifiedTypeNameLookupResult::FoundType:
          FoundTypeDecl = UnqualifiedTypeNameLookupResult::FoundType;
          break;
        case UnqualifiedTypeNameLookupResult::NotFound:
          break;
        }
      }
    }
  }

  return FoundTypeDecl;
}

static ParsedType recoverFromTypeInKnownDependentBase(Sema &S,
                                                      const IdentifierInfo &II,
                                                      SourceLocation NameLoc) {
  // Lookup in the parent class template context, if any.
  const CXXRecordDecl *RD = nullptr;
  UnqualifiedTypeNameLookupResult FoundTypeDecl =
      UnqualifiedTypeNameLookupResult::NotFound;
  for (DeclContext *DC = S.CurContext;
       DC && FoundTypeDecl == UnqualifiedTypeNameLookupResult::NotFound;
       DC = DC->getParent()) {
    // Look for type decls in dependent base classes that have known primary
    // templates.
    RD = dyn_cast<CXXRecordDecl>(DC);
    if (RD && RD->getDescribedClassTemplate())
      FoundTypeDecl = lookupUnqualifiedTypeNameInBase(S, II, NameLoc, RD);
  }
  if (FoundTypeDecl != UnqualifiedTypeNameLookupResult::FoundType)
    return nullptr;

  // We found some types in dependent base classes.  Recover as if the user
  // wrote 'typename MyClass::II' instead of 'II'.  We'll fully resolve the
  // lookup during template instantiation.
  S.Diag(NameLoc, diag::ext_found_in_dependent_base) << &II;

  ASTContext &Context = S.Context;
  auto *NNS = NestedNameSpecifier::Create(Context, nullptr, false,
                                          cast<Type>(Context.getRecordType(RD)));
  QualType T =
      Context.getDependentNameType(ElaboratedTypeKeyword::Typename, NNS, &II);

  CXXScopeSpec SS;
  SS.MakeTrivial(Context, NNS, SourceRange(NameLoc));

  TypeLocBuilder Builder;
  DependentNameTypeLoc DepTL = Builder.push<DependentNameTypeLoc>(T);
  DepTL.setNameLoc(NameLoc);
  DepTL.setElaboratedKeywordLoc(SourceLocation());
  DepTL.setQualifierLoc(SS.getWithLocInContext(Context));
  return S.CreateParsedType(T, Builder.getTypeSourceInfo(Context, T));
}

/// Build a ParsedType for a simple-type-specifier with a nested-name-specifier.
static ParsedType buildNamedType(Sema &S, const CXXScopeSpec *SS, QualType T,
                                 SourceLocation NameLoc,
                                 bool WantNontrivialTypeSourceInfo = true) {
  switch (T->getTypeClass()) {
  case Type::DeducedTemplateSpecialization:
  case Type::Enum:
  case Type::InjectedClassName:
  case Type::Record:
  case Type::Typedef:
  case Type::UnresolvedUsing:
  case Type::Using:
    break;
  // These can never be qualified so an ElaboratedType node
  // would carry no additional meaning.
  case Type::ObjCInterface:
  case Type::ObjCTypeParam:
  case Type::TemplateTypeParm:
    return ParsedType::make(T);
  default:
    llvm_unreachable("Unexpected Type Class");
  }

  if (!SS || SS->isEmpty())
    return ParsedType::make(S.Context.getElaboratedType(
        ElaboratedTypeKeyword::None, nullptr, T, nullptr));

  QualType ElTy = S.getElaboratedType(ElaboratedTypeKeyword::None, *SS, T);
  if (!WantNontrivialTypeSourceInfo)
    return ParsedType::make(ElTy);

  TypeLocBuilder Builder;
  Builder.pushTypeSpec(T).setNameLoc(NameLoc);
  ElaboratedTypeLoc ElabTL = Builder.push<ElaboratedTypeLoc>(ElTy);
  ElabTL.setElaboratedKeywordLoc(SourceLocation());
  ElabTL.setQualifierLoc(SS->getWithLocInContext(S.Context));
  return S.CreateParsedType(ElTy, Builder.getTypeSourceInfo(S.Context, ElTy));
}

ParsedType Sema::getTypeName(const IdentifierInfo &II, SourceLocation NameLoc,
                             Scope *S, CXXScopeSpec *SS, bool isClassName,
                             bool HasTrailingDot, ParsedType ObjectTypePtr,
                             bool IsCtorOrDtorName,
                             bool WantNontrivialTypeSourceInfo,
                             bool IsClassTemplateDeductionContext,
                             ImplicitTypenameContext AllowImplicitTypename,
                             IdentifierInfo **CorrectedII) {
  // FIXME: Consider allowing this outside C++1z mode as an extension.
  bool AllowDeducedTemplate = IsClassTemplateDeductionContext &&
                              getLangOpts().CPlusPlus17 && !IsCtorOrDtorName &&
                              !isClassName && !HasTrailingDot;

  // Determine where we will perform name lookup.
  DeclContext *LookupCtx = nullptr;
  if (ObjectTypePtr) {
    QualType ObjectType = ObjectTypePtr.get();
    if (ObjectType->isRecordType())
      LookupCtx = computeDeclContext(ObjectType);
  } else if (SS && SS->isNotEmpty()) {
    LookupCtx = computeDeclContext(*SS, false);

    if (!LookupCtx) {
      if (isDependentScopeSpecifier(*SS)) {
        // C++ [temp.res]p3:
        //   A qualified-id that refers to a type and in which the
        //   nested-name-specifier depends on a template-parameter (14.6.2)
        //   shall be prefixed by the keyword typename to indicate that the
        //   qualified-id denotes a type, forming an
        //   elaborated-type-specifier (7.1.5.3).
        //
        // We therefore do not perform any name lookup if the result would
        // refer to a member of an unknown specialization.
        // In C++2a, in several contexts a 'typename' is not required. Also
        // allow this as an extension.
        if (AllowImplicitTypename == ImplicitTypenameContext::No &&
            !isClassName && !IsCtorOrDtorName)
          return nullptr;
        bool IsImplicitTypename = !isClassName && !IsCtorOrDtorName;
        if (IsImplicitTypename) {
          SourceLocation QualifiedLoc = SS->getRange().getBegin();
          if (getLangOpts().CPlusPlus20)
            Diag(QualifiedLoc, diag::warn_cxx17_compat_implicit_typename);
          else
            Diag(QualifiedLoc, diag::ext_implicit_typename)
                << SS->getScopeRep() << II.getName()
                << FixItHint::CreateInsertion(QualifiedLoc, "typename ");
        }

        // We know from the grammar that this name refers to a type,
        // so build a dependent node to describe the type.
        if (WantNontrivialTypeSourceInfo)
          return ActOnTypenameType(S, SourceLocation(), *SS, II, NameLoc,
                                   (ImplicitTypenameContext)IsImplicitTypename)
              .get();

        NestedNameSpecifierLoc QualifierLoc = SS->getWithLocInContext(Context);
        QualType T = CheckTypenameType(
            IsImplicitTypename ? ElaboratedTypeKeyword::Typename
                               : ElaboratedTypeKeyword::None,
            SourceLocation(), QualifierLoc, II, NameLoc);
        return ParsedType::make(T);
      }

      return nullptr;
    }

    if (!LookupCtx->isDependentContext() &&
        RequireCompleteDeclContext(*SS, LookupCtx))
      return nullptr;
  }

  // FIXME: LookupNestedNameSpecifierName isn't the right kind of
  // lookup for class-names.
  LookupNameKind Kind = isClassName ? LookupNestedNameSpecifierName :
                                      LookupOrdinaryName;
  LookupResult Result(*this, &II, NameLoc, Kind);
  if (LookupCtx) {
    // Perform "qualified" name lookup into the declaration context we
    // computed, which is either the type of the base of a member access
    // expression or the declaration context associated with a prior
    // nested-name-specifier.
    LookupQualifiedName(Result, LookupCtx);

    if (ObjectTypePtr && Result.empty()) {
      // C++ [basic.lookup.classref]p3:
      //   If the unqualified-id is ~type-name, the type-name is looked up
      //   in the context of the entire postfix-expression. If the type T of
      //   the object expression is of a class type C, the type-name is also
      //   looked up in the scope of class C. At least one of the lookups shall
      //   find a name that refers to (possibly cv-qualified) T.
      LookupName(Result, S);
    }
  } else {
    // Perform unqualified name lookup.
    LookupName(Result, S);

    // For unqualified lookup in a class template in MSVC mode, look into
    // dependent base classes where the primary class template is known.
    if (Result.empty() && getLangOpts().MSVCCompat && (!SS || SS->isEmpty())) {
      if (ParsedType TypeInBase =
              recoverFromTypeInKnownDependentBase(*this, II, NameLoc))
        return TypeInBase;
    }
  }

  NamedDecl *IIDecl = nullptr;
  UsingShadowDecl *FoundUsingShadow = nullptr;
  switch (Result.getResultKind()) {
  case LookupResult::NotFound:
    if (CorrectedII) {
      TypeNameValidatorCCC CCC(/*AllowInvalid=*/true, isClassName,
                               AllowDeducedTemplate);
      TypoCorrection Correction = CorrectTypo(Result.getLookupNameInfo(), Kind,
                                              S, SS, CCC, CTK_ErrorRecovery);
      IdentifierInfo *NewII = Correction.getCorrectionAsIdentifierInfo();
      TemplateTy Template;
      bool MemberOfUnknownSpecialization;
      UnqualifiedId TemplateName;
      TemplateName.setIdentifier(NewII, NameLoc);
      NestedNameSpecifier *NNS = Correction.getCorrectionSpecifier();
      CXXScopeSpec NewSS, *NewSSPtr = SS;
      if (SS && NNS) {
        NewSS.MakeTrivial(Context, NNS, SourceRange(NameLoc));
        NewSSPtr = &NewSS;
      }
      if (Correction && (NNS || NewII != &II) &&
          // Ignore a correction to a template type as the to-be-corrected
          // identifier is not a template (typo correction for template names
          // is handled elsewhere).
          !(getLangOpts().CPlusPlus && NewSSPtr &&
            isTemplateName(S, *NewSSPtr, false, TemplateName, nullptr, false,
                           Template, MemberOfUnknownSpecialization))) {
        ParsedType Ty = getTypeName(*NewII, NameLoc, S, NewSSPtr,
                                    isClassName, HasTrailingDot, ObjectTypePtr,
                                    IsCtorOrDtorName,
                                    WantNontrivialTypeSourceInfo,
                                    IsClassTemplateDeductionContext);
        if (Ty) {
          diagnoseTypo(Correction,
                       PDiag(diag::err_unknown_type_or_class_name_suggest)
                         << Result.getLookupName() << isClassName);
          if (SS && NNS)
            SS->MakeTrivial(Context, NNS, SourceRange(NameLoc));
          *CorrectedII = NewII;
          return Ty;
        }
      }
    }
    Result.suppressDiagnostics();
    return nullptr;
  case LookupResult::NotFoundInCurrentInstantiation:
    if (AllowImplicitTypename == ImplicitTypenameContext::Yes) {
      QualType T = Context.getDependentNameType(ElaboratedTypeKeyword::None,
                                                SS->getScopeRep(), &II);
      TypeLocBuilder TLB;
      DependentNameTypeLoc TL = TLB.push<DependentNameTypeLoc>(T);
      TL.setElaboratedKeywordLoc(SourceLocation());
      TL.setQualifierLoc(SS->getWithLocInContext(Context));
      TL.setNameLoc(NameLoc);
      return CreateParsedType(T, TLB.getTypeSourceInfo(Context, T));
    }
    [[fallthrough]];
  case LookupResult::FoundOverloaded:
  case LookupResult::FoundUnresolvedValue:
    Result.suppressDiagnostics();
    return nullptr;

  case LookupResult::Ambiguous:
    // Recover from type-hiding ambiguities by hiding the type.  We'll
    // do the lookup again when looking for an object, and we can
    // diagnose the error then.  If we don't do this, then the error
    // about hiding the type will be immediately followed by an error
    // that only makes sense if the identifier was treated like a type.
    if (Result.getAmbiguityKind() == LookupResult::AmbiguousTagHiding) {
      Result.suppressDiagnostics();
      return nullptr;
    }

    // Look to see if we have a type anywhere in the list of results.
    for (LookupResult::iterator Res = Result.begin(), ResEnd = Result.end();
         Res != ResEnd; ++Res) {
      NamedDecl *RealRes = (*Res)->getUnderlyingDecl();
      if (isa<TypeDecl, ObjCInterfaceDecl, UnresolvedUsingIfExistsDecl>(
              RealRes) ||
          (AllowDeducedTemplate && getAsTypeTemplateDecl(RealRes))) {
        if (!IIDecl ||
            // Make the selection of the recovery decl deterministic.
            RealRes->getLocation() < IIDecl->getLocation()) {
          IIDecl = RealRes;
          FoundUsingShadow = dyn_cast<UsingShadowDecl>(*Res);
        }
      }
    }

    if (!IIDecl) {
      // None of the entities we found is a type, so there is no way
      // to even assume that the result is a type. In this case, don't
      // complain about the ambiguity. The parser will either try to
      // perform this lookup again (e.g., as an object name), which
      // will produce the ambiguity, or will complain that it expected
      // a type name.
      Result.suppressDiagnostics();
      return nullptr;
    }

    // We found a type within the ambiguous lookup; diagnose the
    // ambiguity and then return that type. This might be the right
    // answer, or it might not be, but it suppresses any attempt to
    // perform the name lookup again.
    break;

  case LookupResult::Found:
    IIDecl = Result.getFoundDecl();
    FoundUsingShadow = dyn_cast<UsingShadowDecl>(*Result.begin());
    break;
  }

  assert(IIDecl && "Didn't find decl");

  QualType T;
  if (TypeDecl *TD = dyn_cast<TypeDecl>(IIDecl)) {
    // C++ [class.qual]p2: A lookup that would find the injected-class-name
    // instead names the constructors of the class, except when naming a class.
    // This is ill-formed when we're not actually forming a ctor or dtor name.
    auto *LookupRD = dyn_cast_or_null<CXXRecordDecl>(LookupCtx);
    auto *FoundRD = dyn_cast<CXXRecordDecl>(TD);
    if (!isClassName && !IsCtorOrDtorName && LookupRD && FoundRD &&
        FoundRD->isInjectedClassName() &&
        declaresSameEntity(LookupRD, cast<Decl>(FoundRD->getParent())))
      Diag(NameLoc, diag::err_out_of_line_qualified_id_type_names_constructor)
          << &II << /*Type*/1;

    DiagnoseUseOfDecl(IIDecl, NameLoc);

    T = Context.getTypeDeclType(TD);
    MarkAnyDeclReferenced(TD->getLocation(), TD, /*OdrUse=*/false);
  } else if (ObjCInterfaceDecl *IDecl = dyn_cast<ObjCInterfaceDecl>(IIDecl)) {
    (void)DiagnoseUseOfDecl(IDecl, NameLoc);
    if (!HasTrailingDot)
      T = Context.getObjCInterfaceType(IDecl);
    FoundUsingShadow = nullptr; // FIXME: Target must be a TypeDecl.
  } else if (auto *UD = dyn_cast<UnresolvedUsingIfExistsDecl>(IIDecl)) {
    (void)DiagnoseUseOfDecl(UD, NameLoc);
    // Recover with 'int'
    return ParsedType::make(Context.IntTy);
  } else if (AllowDeducedTemplate) {
    if (auto *TD = getAsTypeTemplateDecl(IIDecl)) {
      assert(!FoundUsingShadow || FoundUsingShadow->getTargetDecl() == TD);
      TemplateName Template = Context.getQualifiedTemplateName(
          SS ? SS->getScopeRep() : nullptr, /*TemplateKeyword=*/false,
          FoundUsingShadow ? TemplateName(FoundUsingShadow) : TemplateName(TD));
      T = Context.getDeducedTemplateSpecializationType(Template, QualType(),
                                                       false);
      // Don't wrap in a further UsingType.
      FoundUsingShadow = nullptr;
    }
  }

  if (T.isNull()) {
    // If it's not plausibly a type, suppress diagnostics.
    Result.suppressDiagnostics();
    return nullptr;
  }

  if (FoundUsingShadow)
    T = Context.getUsingType(FoundUsingShadow, T);

  return buildNamedType(*this, SS, T, NameLoc, WantNontrivialTypeSourceInfo);
}

// Builds a fake NNS for the given decl context.
static NestedNameSpecifier *
synthesizeCurrentNestedNameSpecifier(ASTContext &Context, DeclContext *DC) {
  for (;; DC = DC->getLookupParent()) {
    DC = DC->getPrimaryContext();
    auto *ND = dyn_cast<NamespaceDecl>(DC);
    if (ND && !ND->isInline() && !ND->isAnonymousNamespace())
      return NestedNameSpecifier::Create(Context, nullptr, ND);
    else if (auto *RD = dyn_cast<CXXRecordDecl>(DC))
      return NestedNameSpecifier::Create(Context, nullptr, RD->isTemplateDecl(),
                                         RD->getTypeForDecl());
    else if (isa<TranslationUnitDecl>(DC))
      return NestedNameSpecifier::GlobalSpecifier(Context);
  }
  llvm_unreachable("something isn't in TU scope?");
}

/// Find the parent class with dependent bases of the innermost enclosing method
/// context. Do not look for enclosing CXXRecordDecls directly, or we will end
/// up allowing unqualified dependent type names at class-level, which MSVC
/// correctly rejects.
static const CXXRecordDecl *
findRecordWithDependentBasesOfEnclosingMethod(const DeclContext *DC) {
  for (; DC && DC->isDependentContext(); DC = DC->getLookupParent()) {
    DC = DC->getPrimaryContext();
    if (const auto *MD = dyn_cast<CXXMethodDecl>(DC))
      if (MD->getParent()->hasAnyDependentBases())
        return MD->getParent();
  }
  return nullptr;
}

ParsedType Sema::ActOnMSVCUnknownTypeName(const IdentifierInfo &II,
                                          SourceLocation NameLoc,
                                          bool IsTemplateTypeArg) {
  assert(getLangOpts().MSVCCompat && "shouldn't be called in non-MSVC mode");

  NestedNameSpecifier *NNS = nullptr;
  if (IsTemplateTypeArg && getCurScope()->isTemplateParamScope()) {
    // If we weren't able to parse a default template argument, delay lookup
    // until instantiation time by making a non-dependent DependentTypeName. We
    // pretend we saw a NestedNameSpecifier referring to the current scope, and
    // lookup is retried.
    // FIXME: This hurts our diagnostic quality, since we get errors like "no
    // type named 'Foo' in 'current_namespace'" when the user didn't write any
    // name specifiers.
    NNS = synthesizeCurrentNestedNameSpecifier(Context, CurContext);
    Diag(NameLoc, diag::ext_ms_delayed_template_argument) << &II;
  } else if (const CXXRecordDecl *RD =
                 findRecordWithDependentBasesOfEnclosingMethod(CurContext)) {
    // Build a DependentNameType that will perform lookup into RD at
    // instantiation time.
    NNS = NestedNameSpecifier::Create(Context, nullptr, RD->isTemplateDecl(),
                                      RD->getTypeForDecl());

    // Diagnose that this identifier was undeclared, and retry the lookup during
    // template instantiation.
    Diag(NameLoc, diag::ext_undeclared_unqual_id_with_dependent_base) << &II
                                                                      << RD;
  } else {
    // This is not a situation that we should recover from.
    return ParsedType();
  }

  QualType T =
      Context.getDependentNameType(ElaboratedTypeKeyword::None, NNS, &II);

  // Build type location information.  We synthesized the qualifier, so we have
  // to build a fake NestedNameSpecifierLoc.
  NestedNameSpecifierLocBuilder NNSLocBuilder;
  NNSLocBuilder.MakeTrivial(Context, NNS, SourceRange(NameLoc));
  NestedNameSpecifierLoc QualifierLoc = NNSLocBuilder.getWithLocInContext(Context);

  TypeLocBuilder Builder;
  DependentNameTypeLoc DepTL = Builder.push<DependentNameTypeLoc>(T);
  DepTL.setNameLoc(NameLoc);
  DepTL.setElaboratedKeywordLoc(SourceLocation());
  DepTL.setQualifierLoc(QualifierLoc);
  return CreateParsedType(T, Builder.getTypeSourceInfo(Context, T));
}

DeclSpec::TST Sema::isTagName(IdentifierInfo &II, Scope *S) {
  // Do a tag name lookup in this scope.
  LookupResult R(*this, &II, SourceLocation(), LookupTagName);
  LookupName(R, S, false);
  R.suppressDiagnostics();
  if (R.getResultKind() == LookupResult::Found)
    if (const TagDecl *TD = R.getAsSingle<TagDecl>()) {
      switch (TD->getTagKind()) {
      case TagTypeKind::Struct:
        return DeclSpec::TST_struct;
      case TagTypeKind::Interface:
        return DeclSpec::TST_interface;
      case TagTypeKind::Union:
        return DeclSpec::TST_union;
      case TagTypeKind::Class:
        return DeclSpec::TST_class;
      case TagTypeKind::Enum:
        return DeclSpec::TST_enum;
      }
    }

  return DeclSpec::TST_unspecified;
}

bool Sema::isMicrosoftMissingTypename(const CXXScopeSpec *SS, Scope *S) {
  if (CurContext->isRecord()) {
    if (SS->getScopeRep()->getKind() == NestedNameSpecifier::Super)
      return true;

    const Type *Ty = SS->getScopeRep()->getAsType();

    CXXRecordDecl *RD = cast<CXXRecordDecl>(CurContext);
    for (const auto &Base : RD->bases())
      if (Ty && Context.hasSameUnqualifiedType(QualType(Ty, 1), Base.getType()))
        return true;
    return S->isFunctionPrototypeScope();
  }
  return CurContext->isFunctionOrMethod() || S->isFunctionPrototypeScope();
}

void Sema::DiagnoseUnknownTypeName(IdentifierInfo *&II,
                                   SourceLocation IILoc,
                                   Scope *S,
                                   CXXScopeSpec *SS,
                                   ParsedType &SuggestedType,
                                   bool IsTemplateName) {
  // Don't report typename errors for editor placeholders.
  if (II->isEditorPlaceholder())
    return;
  // We don't have anything to suggest (yet).
  SuggestedType = nullptr;

  // There may have been a typo in the name of the type. Look up typo
  // results, in case we have something that we can suggest.
  TypeNameValidatorCCC CCC(/*AllowInvalid=*/false, /*WantClass=*/false,
                           /*AllowTemplates=*/IsTemplateName,
                           /*AllowNonTemplates=*/!IsTemplateName);
  if (TypoCorrection Corrected =
          CorrectTypo(DeclarationNameInfo(II, IILoc), LookupOrdinaryName, S, SS,
                      CCC, CTK_ErrorRecovery)) {
    // FIXME: Support error recovery for the template-name case.
    bool CanRecover = !IsTemplateName;
    if (Corrected.isKeyword()) {
      // We corrected to a keyword.
      diagnoseTypo(Corrected,
                   PDiag(IsTemplateName ? diag::err_no_template_suggest
                                        : diag::err_unknown_typename_suggest)
                       << II);
      II = Corrected.getCorrectionAsIdentifierInfo();
    } else {
      // We found a similarly-named type or interface; suggest that.
      if (!SS || !SS->isSet()) {
        diagnoseTypo(Corrected,
                     PDiag(IsTemplateName ? diag::err_no_template_suggest
                                          : diag::err_unknown_typename_suggest)
                         << II, CanRecover);
      } else if (DeclContext *DC = computeDeclContext(*SS, false)) {
        std::string CorrectedStr(Corrected.getAsString(getLangOpts()));
        bool DroppedSpecifier =
            Corrected.WillReplaceSpecifier() && II->getName() == CorrectedStr;
        diagnoseTypo(Corrected,
                     PDiag(IsTemplateName
                               ? diag::err_no_member_template_suggest
                               : diag::err_unknown_nested_typename_suggest)
                         << II << DC << DroppedSpecifier << SS->getRange(),
                     CanRecover);
      } else {
        llvm_unreachable("could not have corrected a typo here");
      }

      if (!CanRecover)
        return;

      CXXScopeSpec tmpSS;
      if (Corrected.getCorrectionSpecifier())
        tmpSS.MakeTrivial(Context, Corrected.getCorrectionSpecifier(),
                          SourceRange(IILoc));
      // FIXME: Support class template argument deduction here.
      SuggestedType =
          getTypeName(*Corrected.getCorrectionAsIdentifierInfo(), IILoc, S,
                      tmpSS.isSet() ? &tmpSS : SS, false, false, nullptr,
                      /*IsCtorOrDtorName=*/false,
                      /*WantNontrivialTypeSourceInfo=*/true);
    }
    return;
  }

  if (getLangOpts().CPlusPlus && !IsTemplateName) {
    // See if II is a class template that the user forgot to pass arguments to.
    UnqualifiedId Name;
    Name.setIdentifier(II, IILoc);
    CXXScopeSpec EmptySS;
    TemplateTy TemplateResult;
    bool MemberOfUnknownSpecialization;
    if (isTemplateName(S, SS ? *SS : EmptySS, /*hasTemplateKeyword=*/false,
                       Name, nullptr, true, TemplateResult,
                       MemberOfUnknownSpecialization) == TNK_Type_template) {
      diagnoseMissingTemplateArguments(TemplateResult.get(), IILoc);
      return;
    }
  }

  // FIXME: Should we move the logic that tries to recover from a missing tag
  // (struct, union, enum) from Parser::ParseImplicitInt here, instead?

  if (!SS || (!SS->isSet() && !SS->isInvalid()))
    Diag(IILoc, IsTemplateName ? diag::err_no_template
                               : diag::err_unknown_typename)
        << II;
  else if (DeclContext *DC = computeDeclContext(*SS, false))
    Diag(IILoc, IsTemplateName ? diag::err_no_member_template
                               : diag::err_typename_nested_not_found)
        << II << DC << SS->getRange();
  else if (SS->isValid() && SS->getScopeRep()->containsErrors()) {
    SuggestedType =
        ActOnTypenameType(S, SourceLocation(), *SS, *II, IILoc).get();
  } else if (isDependentScopeSpecifier(*SS)) {
    unsigned DiagID = diag::err_typename_missing;
    if (getLangOpts().MSVCCompat && isMicrosoftMissingTypename(SS, S))
      DiagID = diag::ext_typename_missing;

    Diag(SS->getRange().getBegin(), DiagID)
      << SS->getScopeRep() << II->getName()
      << SourceRange(SS->getRange().getBegin(), IILoc)
      << FixItHint::CreateInsertion(SS->getRange().getBegin(), "typename ");
    SuggestedType = ActOnTypenameType(S, SourceLocation(),
                                      *SS, *II, IILoc).get();
  } else {
    assert(SS && SS->isInvalid() &&
           "Invalid scope specifier has already been diagnosed");
  }
}

/// Determine whether the given result set contains either a type name
/// or
static bool isResultTypeOrTemplate(LookupResult &R, const Token &NextToken) {
  bool CheckTemplate = R.getSema().getLangOpts().CPlusPlus &&
                       NextToken.is(tok::less);

  for (LookupResult::iterator I = R.begin(), IEnd = R.end(); I != IEnd; ++I) {
    if (isa<TypeDecl>(*I) || isa<ObjCInterfaceDecl>(*I))
      return true;

    if (CheckTemplate && isa<TemplateDecl>(*I))
      return true;
  }

  return false;
}

static bool isTagTypeWithMissingTag(Sema &SemaRef, LookupResult &Result,
                                    Scope *S, CXXScopeSpec &SS,
                                    IdentifierInfo *&Name,
                                    SourceLocation NameLoc) {
  LookupResult R(SemaRef, Name, NameLoc, Sema::LookupTagName);
  SemaRef.LookupParsedName(R, S, &SS, /*ObjectType=*/QualType());
  if (TagDecl *Tag = R.getAsSingle<TagDecl>()) {
    StringRef FixItTagName;
    switch (Tag->getTagKind()) {
    case TagTypeKind::Class:
      FixItTagName = "class ";
      break;

    case TagTypeKind::Enum:
      FixItTagName = "enum ";
      break;

    case TagTypeKind::Struct:
      FixItTagName = "struct ";
      break;

    case TagTypeKind::Interface:
      FixItTagName = "__interface ";
      break;

    case TagTypeKind::Union:
      FixItTagName = "union ";
      break;
    }

    StringRef TagName = FixItTagName.drop_back();
    SemaRef.Diag(NameLoc, diag::err_use_of_tag_name_without_tag)
      << Name << TagName << SemaRef.getLangOpts().CPlusPlus
      << FixItHint::CreateInsertion(NameLoc, FixItTagName);

    for (LookupResult::iterator I = Result.begin(), IEnd = Result.end();
         I != IEnd; ++I)
      SemaRef.Diag((*I)->getLocation(), diag::note_decl_hiding_tag_type)
        << Name << TagName;

    // Replace lookup results with just the tag decl.
    Result.clear(Sema::LookupTagName);
    SemaRef.LookupParsedName(Result, S, &SS, /*ObjectType=*/QualType());
    return true;
  }

  return false;
}

Sema::NameClassification Sema::ClassifyName(Scope *S, CXXScopeSpec &SS,
                                            IdentifierInfo *&Name,
                                            SourceLocation NameLoc,
                                            const Token &NextToken,
                                            CorrectionCandidateCallback *CCC) {
  DeclarationNameInfo NameInfo(Name, NameLoc);
  ObjCMethodDecl *CurMethod = getCurMethodDecl();

  assert(NextToken.isNot(tok::coloncolon) &&
         "parse nested name specifiers before calling ClassifyName");
  if (getLangOpts().CPlusPlus && SS.isSet() &&
      isCurrentClassName(*Name, S, &SS)) {
    // Per [class.qual]p2, this names the constructors of SS, not the
    // injected-class-name. We don't have a classification for that.
    // There's not much point caching this result, since the parser
    // will reject it later.
    return NameClassification::Unknown();
  }

  LookupResult Result(*this, Name, NameLoc, LookupOrdinaryName);
  LookupParsedName(Result, S, &SS, /*ObjectType=*/QualType(),
                   /*AllowBuiltinCreation=*/!CurMethod);

  if (SS.isInvalid())
    return NameClassification::Error();

  // For unqualified lookup in a class template in MSVC mode, look into
  // dependent base classes where the primary class template is known.
  if (Result.empty() && SS.isEmpty() && getLangOpts().MSVCCompat) {
    if (ParsedType TypeInBase =
            recoverFromTypeInKnownDependentBase(*this, *Name, NameLoc))
      return TypeInBase;
  }

  // Perform lookup for Objective-C instance variables (including automatically
  // synthesized instance variables), if we're in an Objective-C method.
  // FIXME: This lookup really, really needs to be folded in to the normal
  // unqualified lookup mechanism.
  if (SS.isEmpty() && CurMethod && !isResultTypeOrTemplate(Result, NextToken)) {
    DeclResult Ivar = ObjC().LookupIvarInObjCMethod(Result, S, Name);
    if (Ivar.isInvalid())
      return NameClassification::Error();
    if (Ivar.isUsable())
      return NameClassification::NonType(cast<NamedDecl>(Ivar.get()));

    // We defer builtin creation until after ivar lookup inside ObjC methods.
    if (Result.empty())
      LookupBuiltin(Result);
  }

  bool SecondTry = false;
  bool IsFilteredTemplateName = false;

Corrected:
  switch (Result.getResultKind()) {
  case LookupResult::NotFound:
    // If an unqualified-id is followed by a '(', then we have a function
    // call.
    if (SS.isEmpty() && NextToken.is(tok::l_paren)) {
      // In C++, this is an ADL-only call.
      // FIXME: Reference?
      if (getLangOpts().CPlusPlus)
        return NameClassification::UndeclaredNonType();

      // C90 6.3.2.2:
      //   If the expression that precedes the parenthesized argument list in a
      //   function call consists solely of an identifier, and if no
      //   declaration is visible for this identifier, the identifier is
      //   implicitly declared exactly as if, in the innermost block containing
      //   the function call, the declaration
      //
      //     extern int identifier ();
      //
      //   appeared.
      //
      // We also allow this in C99 as an extension. However, this is not
      // allowed in all language modes as functions without prototypes may not
      // be supported.
      if (getLangOpts().implicitFunctionsAllowed()) {
        if (NamedDecl *D = ImplicitlyDefineFunction(NameLoc, *Name, S))
          return NameClassification::NonType(D);
      }
    }

    if (getLangOpts().CPlusPlus20 && SS.isEmpty() && NextToken.is(tok::less)) {
      // In C++20 onwards, this could be an ADL-only call to a function
      // template, and we're required to assume that this is a template name.
      //
      // FIXME: Find a way to still do typo correction in this case.
      TemplateName Template =
          Context.getAssumedTemplateName(NameInfo.getName());
      return NameClassification::UndeclaredTemplate(Template);
    }

    // In C, we first see whether there is a tag type by the same name, in
    // which case it's likely that the user just forgot to write "enum",
    // "struct", or "union".
    if (!getLangOpts().CPlusPlus && !SecondTry &&
        isTagTypeWithMissingTag(*this, Result, S, SS, Name, NameLoc)) {
      break;
    }

    // Perform typo correction to determine if there is another name that is
    // close to this name.
    if (!SecondTry && CCC) {
      SecondTry = true;
      if (TypoCorrection Corrected =
              CorrectTypo(Result.getLookupNameInfo(), Result.getLookupKind(), S,
                          &SS, *CCC, CTK_ErrorRecovery)) {
        unsigned UnqualifiedDiag = diag::err_undeclared_var_use_suggest;
        unsigned QualifiedDiag = diag::err_no_member_suggest;

        NamedDecl *FirstDecl = Corrected.getFoundDecl();
        NamedDecl *UnderlyingFirstDecl = Corrected.getCorrectionDecl();
        if (getLangOpts().CPlusPlus && NextToken.is(tok::less) &&
            UnderlyingFirstDecl && isa<TemplateDecl>(UnderlyingFirstDecl)) {
          UnqualifiedDiag = diag::err_no_template_suggest;
          QualifiedDiag = diag::err_no_member_template_suggest;
        } else if (UnderlyingFirstDecl &&
                   (isa<TypeDecl>(UnderlyingFirstDecl) ||
                    isa<ObjCInterfaceDecl>(UnderlyingFirstDecl) ||
                    isa<ObjCCompatibleAliasDecl>(UnderlyingFirstDecl))) {
          UnqualifiedDiag = diag::err_unknown_typename_suggest;
          QualifiedDiag = diag::err_unknown_nested_typename_suggest;
        }

        if (SS.isEmpty()) {
          diagnoseTypo(Corrected, PDiag(UnqualifiedDiag) << Name);
        } else {// FIXME: is this even reachable? Test it.
          std::string CorrectedStr(Corrected.getAsString(getLangOpts()));
          bool DroppedSpecifier = Corrected.WillReplaceSpecifier() &&
                                  Name->getName() == CorrectedStr;
          diagnoseTypo(Corrected, PDiag(QualifiedDiag)
                                    << Name << computeDeclContext(SS, false)
                                    << DroppedSpecifier << SS.getRange());
        }

        // Update the name, so that the caller has the new name.
        Name = Corrected.getCorrectionAsIdentifierInfo();

        // Typo correction corrected to a keyword.
        if (Corrected.isKeyword())
          return Name;

        // Also update the LookupResult...
        // FIXME: This should probably go away at some point
        Result.clear();
        Result.setLookupName(Corrected.getCorrection());
        if (FirstDecl)
          Result.addDecl(FirstDecl);

        // If we found an Objective-C instance variable, let
        // LookupInObjCMethod build the appropriate expression to
        // reference the ivar.
        // FIXME: This is a gross hack.
        if (ObjCIvarDecl *Ivar = Result.getAsSingle<ObjCIvarDecl>()) {
          DeclResult R =
              ObjC().LookupIvarInObjCMethod(Result, S, Ivar->getIdentifier());
          if (R.isInvalid())
            return NameClassification::Error();
          if (R.isUsable())
            return NameClassification::NonType(Ivar);
        }

        goto Corrected;
      }
    }

    // We failed to correct; just fall through and let the parser deal with it.
    Result.suppressDiagnostics();
    return NameClassification::Unknown();

  case LookupResult::NotFoundInCurrentInstantiation: {
    // We performed name lookup into the current instantiation, and there were
    // dependent bases, so we treat this result the same way as any other
    // dependent nested-name-specifier.

    // C++ [temp.res]p2:
    //   A name used in a template declaration or definition and that is
    //   dependent on a template-parameter is assumed not to name a type
    //   unless the applicable name lookup finds a type name or the name is
    //   qualified by the keyword typename.
    //
    // FIXME: If the next token is '<', we might want to ask the parser to
    // perform some heroics to see if we actually have a
    // template-argument-list, which would indicate a missing 'template'
    // keyword here.
    return NameClassification::DependentNonType();
  }

  case LookupResult::Found:
  case LookupResult::FoundOverloaded:
  case LookupResult::FoundUnresolvedValue:
    break;

  case LookupResult::Ambiguous:
    if (getLangOpts().CPlusPlus && NextToken.is(tok::less) &&
        hasAnyAcceptableTemplateNames(Result, /*AllowFunctionTemplates=*/true,
                                      /*AllowDependent=*/false)) {
      // C++ [temp.local]p3:
      //   A lookup that finds an injected-class-name (10.2) can result in an
      //   ambiguity in certain cases (for example, if it is found in more than
      //   one base class). If all of the injected-class-names that are found
      //   refer to specializations of the same class template, and if the name
      //   is followed by a template-argument-list, the reference refers to the
      //   class template itself and not a specialization thereof, and is not
      //   ambiguous.
      //
      // This filtering can make an ambiguous result into an unambiguous one,
      // so try again after filtering out template names.
      FilterAcceptableTemplateNames(Result);
      if (!Result.isAmbiguous()) {
        IsFilteredTemplateName = true;
        break;
      }
    }

    // Diagnose the ambiguity and return an error.
    return NameClassification::Error();
  }

  if (getLangOpts().CPlusPlus && NextToken.is(tok::less) &&
      (IsFilteredTemplateName ||
       hasAnyAcceptableTemplateNames(
           Result, /*AllowFunctionTemplates=*/true,
           /*AllowDependent=*/false,
           /*AllowNonTemplateFunctions*/ SS.isEmpty() &&
               getLangOpts().CPlusPlus20))) {
    // C++ [temp.names]p3:
    //   After name lookup (3.4) finds that a name is a template-name or that
    //   an operator-function-id or a literal- operator-id refers to a set of
    //   overloaded functions any member of which is a function template if
    //   this is followed by a <, the < is always taken as the delimiter of a
    //   template-argument-list and never as the less-than operator.
    // C++2a [temp.names]p2:
    //   A name is also considered to refer to a template if it is an
    //   unqualified-id followed by a < and name lookup finds either one
    //   or more functions or finds nothing.
    if (!IsFilteredTemplateName)
      FilterAcceptableTemplateNames(Result);

    bool IsFunctionTemplate;
    bool IsVarTemplate;
    TemplateName Template;
    if (Result.end() - Result.begin() > 1) {
      IsFunctionTemplate = true;
      Template = Context.getOverloadedTemplateName(Result.begin(),
                                                   Result.end());
    } else if (!Result.empty()) {
      auto *TD = cast<TemplateDecl>(getAsTemplateNameDecl(
          *Result.begin(), /*AllowFunctionTemplates=*/true,
          /*AllowDependent=*/false));
      IsFunctionTemplate = isa<FunctionTemplateDecl>(TD);
      IsVarTemplate = isa<VarTemplateDecl>(TD);

      UsingShadowDecl *FoundUsingShadow =
          dyn_cast<UsingShadowDecl>(*Result.begin());
      assert(!FoundUsingShadow ||
             TD == cast<TemplateDecl>(FoundUsingShadow->getTargetDecl()));
      Template = Context.getQualifiedTemplateName(
          SS.getScopeRep(),
          /*TemplateKeyword=*/false,
          FoundUsingShadow ? TemplateName(FoundUsingShadow) : TemplateName(TD));
    } else {
      // All results were non-template functions. This is a function template
      // name.
      IsFunctionTemplate = true;
      Template = Context.getAssumedTemplateName(NameInfo.getName());
    }

    if (IsFunctionTemplate) {
      // Function templates always go through overload resolution, at which
      // point we'll perform the various checks (e.g., accessibility) we need
      // to based on which function we selected.
      Result.suppressDiagnostics();

      return NameClassification::FunctionTemplate(Template);
    }

    return IsVarTemplate ? NameClassification::VarTemplate(Template)
                         : NameClassification::TypeTemplate(Template);
  }

  auto BuildTypeFor = [&](TypeDecl *Type, NamedDecl *Found) {
    QualType T = Context.getTypeDeclType(Type);
    if (const auto *USD = dyn_cast<UsingShadowDecl>(Found))
      T = Context.getUsingType(USD, T);
    return buildNamedType(*this, &SS, T, NameLoc);
  };

  NamedDecl *FirstDecl = (*Result.begin())->getUnderlyingDecl();
  if (TypeDecl *Type = dyn_cast<TypeDecl>(FirstDecl)) {
    DiagnoseUseOfDecl(Type, NameLoc);
    MarkAnyDeclReferenced(Type->getLocation(), Type, /*OdrUse=*/false);
    return BuildTypeFor(Type, *Result.begin());
  }

  ObjCInterfaceDecl *Class = dyn_cast<ObjCInterfaceDecl>(FirstDecl);
  if (!Class) {
    // FIXME: It's unfortunate that we don't have a Type node for handling this.
    if (ObjCCompatibleAliasDecl *Alias =
            dyn_cast<ObjCCompatibleAliasDecl>(FirstDecl))
      Class = Alias->getClassInterface();
  }

  if (Class) {
    DiagnoseUseOfDecl(Class, NameLoc);

    if (NextToken.is(tok::period)) {
      // Interface. <something> is parsed as a property reference expression.
      // Just return "unknown" as a fall-through for now.
      Result.suppressDiagnostics();
      return NameClassification::Unknown();
    }

    QualType T = Context.getObjCInterfaceType(Class);
    return ParsedType::make(T);
  }

  if (isa<ConceptDecl>(FirstDecl)) {
    // We want to preserve the UsingShadowDecl for concepts.
    if (auto *USD = dyn_cast<UsingShadowDecl>(Result.getRepresentativeDecl()))
      return NameClassification::Concept(TemplateName(USD));
    return NameClassification::Concept(
        TemplateName(cast<TemplateDecl>(FirstDecl)));
  }

  if (auto *EmptyD = dyn_cast<UnresolvedUsingIfExistsDecl>(FirstDecl)) {
    (void)DiagnoseUseOfDecl(EmptyD, NameLoc);
    return NameClassification::Error();
  }

  // We can have a type template here if we're classifying a template argument.
  if (isa<TemplateDecl>(FirstDecl) && !isa<FunctionTemplateDecl>(FirstDecl) &&
      !isa<VarTemplateDecl>(FirstDecl))
    return NameClassification::TypeTemplate(
        TemplateName(cast<TemplateDecl>(FirstDecl)));

  // Check for a tag type hidden by a non-type decl in a few cases where it
  // seems likely a type is wanted instead of the non-type that was found.
  bool NextIsOp = NextToken.isOneOf(tok::amp, tok::star);
  if ((NextToken.is(tok::identifier) ||
       (NextIsOp &&
        FirstDecl->getUnderlyingDecl()->isFunctionOrFunctionTemplate())) &&
      isTagTypeWithMissingTag(*this, Result, S, SS, Name, NameLoc)) {
    TypeDecl *Type = Result.getAsSingle<TypeDecl>();
    DiagnoseUseOfDecl(Type, NameLoc);
    return BuildTypeFor(Type, *Result.begin());
  }

  // If we already know which single declaration is referenced, just annotate
  // that declaration directly. Defer resolving even non-overloaded class
  // member accesses, as we need to defer certain access checks until we know
  // the context.
  bool ADL = UseArgumentDependentLookup(SS, Result, NextToken.is(tok::l_paren));
  if (Result.isSingleResult() && !ADL &&
      (!FirstDecl->isCXXClassMember() || isa<EnumConstantDecl>(FirstDecl)))
    return NameClassification::NonType(Result.getRepresentativeDecl());

  // Otherwise, this is an overload set that we will need to resolve later.
  Result.suppressDiagnostics();
  return NameClassification::OverloadSet(UnresolvedLookupExpr::Create(
      Context, Result.getNamingClass(), SS.getWithLocInContext(Context),
      Result.getLookupNameInfo(), ADL, Result.begin(), Result.end(),
      /*KnownDependent=*/false, /*KnownInstantiationDependent=*/false));
}

ExprResult
Sema::ActOnNameClassifiedAsUndeclaredNonType(IdentifierInfo *Name,
                                             SourceLocation NameLoc) {
  assert(getLangOpts().CPlusPlus && "ADL-only call in C?");
  CXXScopeSpec SS;
  LookupResult Result(*this, Name, NameLoc, LookupOrdinaryName);
  return BuildDeclarationNameExpr(SS, Result, /*ADL=*/true);
}

ExprResult
Sema::ActOnNameClassifiedAsDependentNonType(const CXXScopeSpec &SS,
                                            IdentifierInfo *Name,
                                            SourceLocation NameLoc,
                                            bool IsAddressOfOperand) {
  DeclarationNameInfo NameInfo(Name, NameLoc);
  return ActOnDependentIdExpression(SS, /*TemplateKWLoc=*/SourceLocation(),
                                    NameInfo, IsAddressOfOperand,
                                    /*TemplateArgs=*/nullptr);
}

ExprResult Sema::ActOnNameClassifiedAsNonType(Scope *S, const CXXScopeSpec &SS,
                                              NamedDecl *Found,
                                              SourceLocation NameLoc,
                                              const Token &NextToken) {
  if (getCurMethodDecl() && SS.isEmpty())
    if (auto *Ivar = dyn_cast<ObjCIvarDecl>(Found->getUnderlyingDecl()))
      return ObjC().BuildIvarRefExpr(S, NameLoc, Ivar);

  // Reconstruct the lookup result.
  LookupResult Result(*this, Found->getDeclName(), NameLoc, LookupOrdinaryName);
  Result.addDecl(Found);
  Result.resolveKind();

  bool ADL = UseArgumentDependentLookup(SS, Result, NextToken.is(tok::l_paren));
  return BuildDeclarationNameExpr(SS, Result, ADL, /*AcceptInvalidDecl=*/true);
}

ExprResult Sema::ActOnNameClassifiedAsOverloadSet(Scope *S, Expr *E) {
  // For an implicit class member access, transform the result into a member
  // access expression if necessary.
  auto *ULE = cast<UnresolvedLookupExpr>(E);
  if ((*ULE->decls_begin())->isCXXClassMember()) {
    CXXScopeSpec SS;
    SS.Adopt(ULE->getQualifierLoc());

    // Reconstruct the lookup result.
    LookupResult Result(*this, ULE->getName(), ULE->getNameLoc(),
                        LookupOrdinaryName);
    Result.setNamingClass(ULE->getNamingClass());
    for (auto I = ULE->decls_begin(), E = ULE->decls_end(); I != E; ++I)
      Result.addDecl(*I, I.getAccess());
    Result.resolveKind();
    return BuildPossibleImplicitMemberExpr(SS, SourceLocation(), Result,
                                           nullptr, S);
  }

  // Otherwise, this is already in the form we needed, and no further checks
  // are necessary.
  return ULE;
}

Sema::TemplateNameKindForDiagnostics
Sema::getTemplateNameKindForDiagnostics(TemplateName Name) {
  auto *TD = Name.getAsTemplateDecl();
  if (!TD)
    return TemplateNameKindForDiagnostics::DependentTemplate;
  if (isa<ClassTemplateDecl>(TD))
    return TemplateNameKindForDiagnostics::ClassTemplate;
  if (isa<FunctionTemplateDecl>(TD))
    return TemplateNameKindForDiagnostics::FunctionTemplate;
  if (isa<VarTemplateDecl>(TD))
    return TemplateNameKindForDiagnostics::VarTemplate;
  if (isa<TypeAliasTemplateDecl>(TD))
    return TemplateNameKindForDiagnostics::AliasTemplate;
  if (isa<TemplateTemplateParmDecl>(TD))
    return TemplateNameKindForDiagnostics::TemplateTemplateParam;
  if (isa<ConceptDecl>(TD))
    return TemplateNameKindForDiagnostics::Concept;
  return TemplateNameKindForDiagnostics::DependentTemplate;
}

void Sema::PushDeclContext(Scope *S, DeclContext *DC) {
  assert(DC->getLexicalParent() == CurContext &&
      "The next DeclContext should be lexically contained in the current one.");
  CurContext = DC;
  S->setEntity(DC);
}

void Sema::PopDeclContext() {
  assert(CurContext && "DeclContext imbalance!");

  CurContext = CurContext->getLexicalParent();
  assert(CurContext && "Popped translation unit!");
}

Sema::SkippedDefinitionContext Sema::ActOnTagStartSkippedDefinition(Scope *S,
                                                                    Decl *D) {
  // Unlike PushDeclContext, the context to which we return is not necessarily
  // the containing DC of TD, because the new context will be some pre-existing
  // TagDecl definition instead of a fresh one.
  auto Result = static_cast<SkippedDefinitionContext>(CurContext);
  CurContext = cast<TagDecl>(D)->getDefinition();
  assert(CurContext && "skipping definition of undefined tag");
  // Start lookups from the parent of the current context; we don't want to look
  // into the pre-existing complete definition.
  S->setEntity(CurContext->getLookupParent());
  return Result;
}

void Sema::ActOnTagFinishSkippedDefinition(SkippedDefinitionContext Context) {
  CurContext = static_cast<decltype(CurContext)>(Context);
}

void Sema::EnterDeclaratorContext(Scope *S, DeclContext *DC) {
  // C++0x [basic.lookup.unqual]p13:
  //   A name used in the definition of a static data member of class
  //   X (after the qualified-id of the static member) is looked up as
  //   if the name was used in a member function of X.
  // C++0x [basic.lookup.unqual]p14:
  //   If a variable member of a namespace is defined outside of the
  //   scope of its namespace then any name used in the definition of
  //   the variable member (after the declarator-id) is looked up as
  //   if the definition of the variable member occurred in its
  //   namespace.
  // Both of these imply that we should push a scope whose context
  // is the semantic context of the declaration.  We can't use
  // PushDeclContext here because that context is not necessarily
  // lexically contained in the current context.  Fortunately,
  // the containing scope should have the appropriate information.

  assert(!S->getEntity() && "scope already has entity");

#ifndef NDEBUG
  Scope *Ancestor = S->getParent();
  while (!Ancestor->getEntity()) Ancestor = Ancestor->getParent();
  assert(Ancestor->getEntity() == CurContext && "ancestor context mismatch");
#endif

  CurContext = DC;
  S->setEntity(DC);

  if (S->getParent()->isTemplateParamScope()) {
    // Also set the corresponding entities for all immediately-enclosing
    // template parameter scopes.
    EnterTemplatedContext(S->getParent(), DC);
  }
}

void Sema::ExitDeclaratorContext(Scope *S) {
  assert(S->getEntity() == CurContext && "Context imbalance!");

  // Switch back to the lexical context.  The safety of this is
  // enforced by an assert in EnterDeclaratorContext.
  Scope *Ancestor = S->getParent();
  while (!Ancestor->getEntity()) Ancestor = Ancestor->getParent();
  CurContext = Ancestor->getEntity();

  // We don't need to do anything with the scope, which is going to
  // disappear.
}

void Sema::EnterTemplatedContext(Scope *S, DeclContext *DC) {
  assert(S->isTemplateParamScope() &&
         "expected to be initializing a template parameter scope");

  // C++20 [temp.local]p7:
  //   In the definition of a member of a class template that appears outside
  //   of the class template definition, the name of a member of the class
  //   template hides the name of a template-parameter of any enclosing class
  //   templates (but not a template-parameter of the member if the member is a
  //   class or function template).
  // C++20 [temp.local]p9:
  //   In the definition of a class template or in the definition of a member
  //   of such a template that appears outside of the template definition, for
  //   each non-dependent base class (13.8.2.1), if the name of the base class
  //   or the name of a member of the base class is the same as the name of a
  //   template-parameter, the base class name or member name hides the
  //   template-parameter name (6.4.10).
  //
  // This means that a template parameter scope should be searched immediately
  // after searching the DeclContext for which it is a template parameter
  // scope. For example, for
  //   template<typename T> template<typename U> template<typename V>
  //     void N::A<T>::B<U>::f(...)
  // we search V then B<U> (and base classes) then U then A<T> (and base
  // classes) then T then N then ::.
  unsigned ScopeDepth = getTemplateDepth(S);
  for (; S && S->isTemplateParamScope(); S = S->getParent(), --ScopeDepth) {
    DeclContext *SearchDCAfterScope = DC;
    for (; DC; DC = DC->getLookupParent()) {
      if (const TemplateParameterList *TPL =
              cast<Decl>(DC)->getDescribedTemplateParams()) {
        unsigned DCDepth = TPL->getDepth() + 1;
        if (DCDepth > ScopeDepth)
          continue;
        if (ScopeDepth == DCDepth)
          SearchDCAfterScope = DC = DC->getLookupParent();
        break;
      }
    }
    S->setLookupEntity(SearchDCAfterScope);
  }
}

void Sema::ActOnReenterFunctionContext(Scope* S, Decl *D) {
  // We assume that the caller has already called
  // ActOnReenterTemplateScope so getTemplatedDecl() works.
  FunctionDecl *FD = D->getAsFunction();
  if (!FD)
    return;

  // Same implementation as PushDeclContext, but enters the context
  // from the lexical parent, rather than the top-level class.
  assert(CurContext == FD->getLexicalParent() &&
    "The next DeclContext should be lexically contained in the current one.");
  CurContext = FD;
  S->setEntity(CurContext);

  for (unsigned P = 0, NumParams = FD->getNumParams(); P < NumParams; ++P) {
    ParmVarDecl *Param = FD->getParamDecl(P);
    // If the parameter has an identifier, then add it to the scope
    if (Param->getIdentifier()) {
      S->AddDecl(Param);
      IdResolver.AddDecl(Param);
    }
  }
}

void Sema::ActOnExitFunctionContext() {
  // Same implementation as PopDeclContext, but returns to the lexical parent,
  // rather than the top-level class.
  assert(CurContext && "DeclContext imbalance!");
  CurContext = CurContext->getLexicalParent();
  assert(CurContext && "Popped translation unit!");
}

/// Determine whether overloading is allowed for a new function
/// declaration considering prior declarations of the same name.
///
/// This routine determines whether overloading is possible, not
/// whether a new declaration actually overloads a previous one.
/// It will return true in C++ (where overloads are always permitted)
/// or, as a C extension, when either the new declaration or a
/// previous one is declared with the 'overloadable' attribute.
static bool AllowOverloadingOfFunction(const LookupResult &Previous,
                                       ASTContext &Context,
                                       const FunctionDecl *New) {
  if (Context.getLangOpts().CPlusPlus || New->hasAttr<OverloadableAttr>())
    return true;

  // Multiversion function declarations are not overloads in the
  // usual sense of that term, but lookup will report that an
  // overload set was found if more than one multiversion function
  // declaration is present for the same name. It is therefore
  // inadequate to assume that some prior declaration(s) had
  // the overloadable attribute; checking is required. Since one
  // declaration is permitted to omit the attribute, it is necessary
  // to check at least two; hence the 'any_of' check below. Note that
  // the overloadable attribute is implicitly added to declarations
  // that were required to have it but did not.
  if (Previous.getResultKind() == LookupResult::FoundOverloaded) {
    return llvm::any_of(Previous, [](const NamedDecl *ND) {
      return ND->hasAttr<OverloadableAttr>();
    });
  } else if (Previous.getResultKind() == LookupResult::Found)
    return Previous.getFoundDecl()->hasAttr<OverloadableAttr>();

  return false;
}

void Sema::PushOnScopeChains(NamedDecl *D, Scope *S, bool AddToContext) {
  // Move up the scope chain until we find the nearest enclosing
  // non-transparent context. The declaration will be introduced into this
  // scope.
  while (S->getEntity() && S->getEntity()->isTransparentContext())
    S = S->getParent();

  // Add scoped declarations into their context, so that they can be
  // found later. Declarations without a context won't be inserted
  // into any context.
  if (AddToContext)
    CurContext->addDecl(D);

  // Out-of-line definitions shouldn't be pushed into scope in C++, unless they
  // are function-local declarations.
  if (getLangOpts().CPlusPlus && D->isOutOfLine() && !S->getFnParent())
    return;

  // Template instantiations should also not be pushed into scope.
  if (isa<FunctionDecl>(D) &&
      cast<FunctionDecl>(D)->isFunctionTemplateSpecialization())
    return;

  if (isa<UsingEnumDecl>(D) && D->getDeclName().isEmpty()) {
    S->AddDecl(D);
    return;
  }
  // If this replaces anything in the current scope,
  IdentifierResolver::iterator I = IdResolver.begin(D->getDeclName()),
                               IEnd = IdResolver.end();
  for (; I != IEnd; ++I) {
    if (S->isDeclScope(*I) && D->declarationReplaces(*I)) {
      S->RemoveDecl(*I);
      IdResolver.RemoveDecl(*I);

      // Should only need to replace one decl.
      break;
    }
  }

  S->AddDecl(D);

  if (isa<LabelDecl>(D) && !cast<LabelDecl>(D)->isGnuLocal()) {
    // Implicitly-generated labels may end up getting generated in an order that
    // isn't strictly lexical, which breaks name lookup. Be careful to insert
    // the label at the appropriate place in the identifier chain.
    for (I = IdResolver.begin(D->getDeclName()); I != IEnd; ++I) {
      DeclContext *IDC = (*I)->getLexicalDeclContext()->getRedeclContext();
      if (IDC == CurContext) {
        if (!S->isDeclScope(*I))
          continue;
      } else if (IDC->Encloses(CurContext))
        break;
    }

    IdResolver.InsertDeclAfter(I, D);
  } else {
    IdResolver.AddDecl(D);
  }
  warnOnReservedIdentifier(D);
}

bool Sema::isDeclInScope(NamedDecl *D, DeclContext *Ctx, Scope *S,
                         bool AllowInlineNamespace) const {
  return IdResolver.isDeclInScope(D, Ctx, S, AllowInlineNamespace);
}

Scope *Sema::getScopeForDeclContext(Scope *S, DeclContext *DC) {
  DeclContext *TargetDC = DC->getPrimaryContext();
  do {
    if (DeclContext *ScopeDC = S->getEntity())
      if (ScopeDC->getPrimaryContext() == TargetDC)
        return S;
  } while ((S = S->getParent()));

  return nullptr;
}

static bool isOutOfScopePreviousDeclaration(NamedDecl *,
                                            DeclContext*,
                                            ASTContext&);

void Sema::FilterLookupForScope(LookupResult &R, DeclContext *Ctx, Scope *S,
                                bool ConsiderLinkage,
                                bool AllowInlineNamespace) {
  LookupResult::Filter F = R.makeFilter();
  while (F.hasNext()) {
    NamedDecl *D = F.next();

    if (isDeclInScope(D, Ctx, S, AllowInlineNamespace))
      continue;

    if (ConsiderLinkage && isOutOfScopePreviousDeclaration(D, Ctx, Context))
      continue;

    F.erase();
  }

  F.done();
}

bool Sema::CheckRedeclarationModuleOwnership(NamedDecl *New, NamedDecl *Old) {
  // [module.interface]p7:
  // A declaration is attached to a module as follows:
  // - If the declaration is a non-dependent friend declaration that nominates a
  // function with a declarator-id that is a qualified-id or template-id or that
  // nominates a class other than with an elaborated-type-specifier with neither
  // a nested-name-specifier nor a simple-template-id, it is attached to the
  // module to which the friend is attached ([basic.link]).
  if (New->getFriendObjectKind() &&
      Old->getOwningModuleForLinkage() != New->getOwningModuleForLinkage()) {
    New->setLocalOwningModule(Old->getOwningModule());
    makeMergedDefinitionVisible(New);
    return false;
  }

  Module *NewM = New->getOwningModule();
  Module *OldM = Old->getOwningModule();

  if (NewM && NewM->isPrivateModule())
    NewM = NewM->Parent;
  if (OldM && OldM->isPrivateModule())
    OldM = OldM->Parent;

  if (NewM == OldM)
    return false;

  if (NewM && OldM) {
    // A module implementation unit has visibility of the decls in its
    // implicitly imported interface.
    if (NewM->isModuleImplementation() && OldM == ThePrimaryInterface)
      return false;

    // Partitions are part of the module, but a partition could import another
    // module, so verify that the PMIs agree.
    if ((NewM->isModulePartition() || OldM->isModulePartition()) &&
        getASTContext().isInSameModule(NewM, OldM))
      return false;
  }

  bool NewIsModuleInterface = NewM && NewM->isNamedModule();
  bool OldIsModuleInterface = OldM && OldM->isNamedModule();
  if (NewIsModuleInterface || OldIsModuleInterface) {
    // C++ Modules TS [basic.def.odr] 6.2/6.7 [sic]:
    //   if a declaration of D [...] appears in the purview of a module, all
    //   other such declarations shall appear in the purview of the same module
    Diag(New->getLocation(), diag::err_mismatched_owning_module)
      << New
      << NewIsModuleInterface
      << (NewIsModuleInterface ? NewM->getFullModuleName() : "")
      << OldIsModuleInterface
      << (OldIsModuleInterface ? OldM->getFullModuleName() : "");
    Diag(Old->getLocation(), diag::note_previous_declaration);
    New->setInvalidDecl();
    return true;
  }

  return false;
}

bool Sema::CheckRedeclarationExported(NamedDecl *New, NamedDecl *Old) {
  // [module.interface]p1:
  // An export-declaration shall inhabit a namespace scope.
  //
  // So it is meaningless to talk about redeclaration which is not at namespace
  // scope.
  if (!New->getLexicalDeclContext()
           ->getNonTransparentContext()
           ->isFileContext() ||
      !Old->getLexicalDeclContext()
           ->getNonTransparentContext()
           ->isFileContext())
    return false;

  bool IsNewExported = New->isInExportDeclContext();
  bool IsOldExported = Old->isInExportDeclContext();

  // It should be irrevelant if both of them are not exported.
  if (!IsNewExported && !IsOldExported)
    return false;

  if (IsOldExported)
    return false;

  // If the Old declaration are not attached to named modules
  // and the New declaration are attached to global module.
  // It should be fine to allow the export since it doesn't change
  // the linkage of declarations. See
  // https://github.com/llvm/llvm-project/issues/98583 for details.
  if (!Old->isInNamedModule() && New->getOwningModule() &&
      New->getOwningModule()->isImplicitGlobalModule())
    return false;

  assert(IsNewExported);

  auto Lk = Old->getFormalLinkage();
  int S = 0;
  if (Lk == Linkage::Internal)
    S = 1;
  else if (Lk == Linkage::Module)
    S = 2;
  Diag(New->getLocation(), diag::err_redeclaration_non_exported) << New << S;
  Diag(Old->getLocation(), diag::note_previous_declaration);
  return true;
}

bool Sema::CheckRedeclarationInModule(NamedDecl *New, NamedDecl *Old) {
  if (CheckRedeclarationModuleOwnership(New, Old))
    return true;

  if (CheckRedeclarationExported(New, Old))
    return true;

  return false;
}

bool Sema::IsRedefinitionInModule(const NamedDecl *New,
                                     const NamedDecl *Old) const {
  assert(getASTContext().isSameEntity(New, Old) &&
         "New and Old are not the same definition, we should diagnostic it "
         "immediately instead of checking it.");
  assert(const_cast<Sema *>(this)->isReachable(New) &&
         const_cast<Sema *>(this)->isReachable(Old) &&
         "We shouldn't see unreachable definitions here.");

  Module *NewM = New->getOwningModule();
  Module *OldM = Old->getOwningModule();

  // We only checks for named modules here. The header like modules is skipped.
  // FIXME: This is not right if we import the header like modules in the module
  // purview.
  //
  // For example, assuming "header.h" provides definition for `D`.
  // ```C++
  // //--- M.cppm
  // export module M;
  // import "header.h"; // or #include "header.h" but import it by clang modules
  // actually.
  //
  // //--- Use.cpp
  // import M;
  // import "header.h"; // or uses clang modules.
  // ```
  //
  // In this case, `D` has multiple definitions in multiple TU (M.cppm and
  // Use.cpp) and `D` is attached to a named module `M`. The compiler should
  // reject it. But the current implementation couldn't detect the case since we
  // don't record the information about the importee modules.
  //
  // But this might not be painful in practice. Since the design of C++20 Named
  // Modules suggests us to use headers in global module fragment instead of
  // module purview.
  if (NewM && NewM->isHeaderLikeModule())
    NewM = nullptr;
  if (OldM && OldM->isHeaderLikeModule())
    OldM = nullptr;

  if (!NewM && !OldM)
    return true;

  // [basic.def.odr]p14.3
  // Each such definition shall not be attached to a named module
  // ([module.unit]).
  if ((NewM && NewM->isNamedModule()) || (OldM && OldM->isNamedModule()))
    return true;

  // Then New and Old lives in the same TU if their share one same module unit.
  if (NewM)
    NewM = NewM->getTopLevelModule();
  if (OldM)
    OldM = OldM->getTopLevelModule();
  return OldM == NewM;
}

static bool isUsingDeclNotAtClassScope(NamedDecl *D) {
  if (D->getDeclContext()->isFileContext())
    return false;

  return isa<UsingShadowDecl>(D) ||
         isa<UnresolvedUsingTypenameDecl>(D) ||
         isa<UnresolvedUsingValueDecl>(D);
}

/// Removes using shadow declarations not at class scope from the lookup
/// results.
static void RemoveUsingDecls(LookupResult &R) {
  LookupResult::Filter F = R.makeFilter();
  while (F.hasNext())
    if (isUsingDeclNotAtClassScope(F.next()))
      F.erase();

  F.done();
}

/// Check for this common pattern:
/// @code
/// class S {
///   S(const S&); // DO NOT IMPLEMENT
///   void operator=(const S&); // DO NOT IMPLEMENT
/// };
/// @endcode
static bool IsDisallowedCopyOrAssign(const CXXMethodDecl *D) {
  // FIXME: Should check for private access too but access is set after we get
  // the decl here.
  if (D->doesThisDeclarationHaveABody())
    return false;

  if (const CXXConstructorDecl *CD = dyn_cast<CXXConstructorDecl>(D))
    return CD->isCopyConstructor();
  return D->isCopyAssignmentOperator();
}

bool Sema::mightHaveNonExternalLinkage(const DeclaratorDecl *D) {
  const DeclContext *DC = D->getDeclContext();
  while (!DC->isTranslationUnit()) {
    if (const RecordDecl *RD = dyn_cast<RecordDecl>(DC)){
      if (!RD->hasNameForLinkage())
        return true;
    }
    DC = DC->getParent();
  }

  return !D->isExternallyVisible();
}

// FIXME: This needs to be refactored; some other isInMainFile users want
// these semantics.
static bool isMainFileLoc(const Sema &S, SourceLocation Loc) {
  if (S.TUKind != TU_Complete || S.getLangOpts().IsHeaderFile)
    return false;
  return S.SourceMgr.isInMainFile(Loc);
}

bool Sema::ShouldWarnIfUnusedFileScopedDecl(const DeclaratorDecl *D) const {
  assert(D);

  if (D->isInvalidDecl() || D->isUsed() || D->hasAttr<UnusedAttr>())
    return false;

  // Ignore all entities declared within templates, and out-of-line definitions
  // of members of class templates.
  if (D->getDeclContext()->isDependentContext() ||
      D->getLexicalDeclContext()->isDependentContext())
    return false;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
      return false;
    // A non-out-of-line declaration of a member specialization was implicitly
    // instantiated; it's the out-of-line declaration that we're interested in.
    if (FD->getTemplateSpecializationKind() == TSK_ExplicitSpecialization &&
        FD->getMemberSpecializationInfo() && !FD->isOutOfLine())
      return false;

    if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD)) {
      if (MD->isVirtual() || IsDisallowedCopyOrAssign(MD))
        return false;
    } else {
      // 'static inline' functions are defined in headers; don't warn.
      if (FD->isInlined() && !isMainFileLoc(*this, FD->getLocation()))
        return false;
    }

    if (FD->doesThisDeclarationHaveABody() &&
        Context.DeclMustBeEmitted(FD))
      return false;
  } else if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    // Constants and utility variables are defined in headers with internal
    // linkage; don't warn.  (Unlike functions, there isn't a convenient marker
    // like "inline".)
    if (!isMainFileLoc(*this, VD->getLocation()))
      return false;

    if (Context.DeclMustBeEmitted(VD))
      return false;

    if (VD->isStaticDataMember() &&
        VD->getTemplateSpecializationKind() == TSK_ImplicitInstantiation)
      return false;
    if (VD->isStaticDataMember() &&
        VD->getTemplateSpecializationKind() == TSK_ExplicitSpecialization &&
        VD->getMemberSpecializationInfo() && !VD->isOutOfLine())
      return false;

    if (VD->isInline() && !isMainFileLoc(*this, VD->getLocation()))
      return false;
  } else {
    return false;
  }

  // Only warn for unused decls internal to the translation unit.
  // FIXME: This seems like a bogus check; it suppresses -Wunused-function
  // for inline functions defined in the main source file, for instance.
  return mightHaveNonExternalLinkage(D);
}

void Sema::MarkUnusedFileScopedDecl(const DeclaratorDecl *D) {
  if (!D)
    return;

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    const FunctionDecl *First = FD->getFirstDecl();
    if (FD != First && ShouldWarnIfUnusedFileScopedDecl(First))
      return; // First should already be in the vector.
  }

  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    const VarDecl *First = VD->getFirstDecl();
    if (VD != First && ShouldWarnIfUnusedFileScopedDecl(First))
      return; // First should already be in the vector.
  }

  if (ShouldWarnIfUnusedFileScopedDecl(D))
    UnusedFileScopedDecls.push_back(D);
}

static bool ShouldDiagnoseUnusedDecl(const LangOptions &LangOpts,
                                     const NamedDecl *D) {
  if (D->isInvalidDecl())
    return false;

  if (const auto *DD = dyn_cast<DecompositionDecl>(D)) {
    // For a decomposition declaration, warn if none of the bindings are
    // referenced, instead of if the variable itself is referenced (which
    // it is, by the bindings' expressions).
    bool IsAllPlaceholders = true;
    for (const auto *BD : DD->bindings()) {
      if (BD->isReferenced() || BD->hasAttr<UnusedAttr>())
        return false;
      IsAllPlaceholders = IsAllPlaceholders && BD->isPlaceholderVar(LangOpts);
    }
    if (IsAllPlaceholders)
      return false;
  } else if (!D->getDeclName()) {
    return false;
  } else if (D->isReferenced() || D->isUsed()) {
    return false;
  }

  if (D->isPlaceholderVar(LangOpts))
    return false;

  if (D->hasAttr<UnusedAttr>() || D->hasAttr<ObjCPreciseLifetimeAttr>() ||
      D->hasAttr<CleanupAttr>())
    return false;

  if (isa<LabelDecl>(D))
    return true;

  // Except for labels, we only care about unused decls that are local to
  // functions.
  bool WithinFunction = D->getDeclContext()->isFunctionOrMethod();
  if (const auto *R = dyn_cast<CXXRecordDecl>(D->getDeclContext()))
    // For dependent types, the diagnostic is deferred.
    WithinFunction =
        WithinFunction || (R->isLocalClass() && !R->isDependentType());
  if (!WithinFunction)
    return false;

  if (isa<TypedefNameDecl>(D))
    return true;

  // White-list anything that isn't a local variable.
  if (!isa<VarDecl>(D) || isa<ParmVarDecl>(D) || isa<ImplicitParamDecl>(D))
    return false;

  // Types of valid local variables should be complete, so this should succeed.
  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {

    const Expr *Init = VD->getInit();
    if (const auto *Cleanups = dyn_cast_if_present<ExprWithCleanups>(Init))
      Init = Cleanups->getSubExpr();

    const auto *Ty = VD->getType().getTypePtr();

    // Only look at the outermost level of typedef.
    if (const TypedefType *TT = Ty->getAs<TypedefType>()) {
      // Allow anything marked with __attribute__((unused)).
      if (TT->getDecl()->hasAttr<UnusedAttr>())
        return false;
    }

    // Warn for reference variables whose initializtion performs lifetime
    // extension.
    if (const auto *MTE = dyn_cast_if_present<MaterializeTemporaryExpr>(Init);
        MTE && MTE->getExtendingDecl()) {
      Ty = VD->getType().getNonReferenceType().getTypePtr();
      Init = MTE->getSubExpr()->IgnoreImplicitAsWritten();
    }

    // If we failed to complete the type for some reason, or if the type is
    // dependent, don't diagnose the variable.
    if (Ty->isIncompleteType() || Ty->isDependentType())
      return false;

    // Look at the element type to ensure that the warning behaviour is
    // consistent for both scalars and arrays.
    Ty = Ty->getBaseElementTypeUnsafe();

    if (const TagType *TT = Ty->getAs<TagType>()) {
      const TagDecl *Tag = TT->getDecl();
      if (Tag->hasAttr<UnusedAttr>())
        return false;

      if (const auto *RD = dyn_cast<CXXRecordDecl>(Tag)) {
        if (!RD->hasTrivialDestructor() && !RD->hasAttr<WarnUnusedAttr>())
          return false;

        if (Init) {
          const auto *Construct =
              dyn_cast<CXXConstructExpr>(Init->IgnoreImpCasts());
          if (Construct && !Construct->isElidable()) {
            const CXXConstructorDecl *CD = Construct->getConstructor();
            if (!CD->isTrivial() && !RD->hasAttr<WarnUnusedAttr>() &&
                (VD->getInit()->isValueDependent() || !VD->evaluateValue()))
              return false;
          }

          // Suppress the warning if we don't know how this is constructed, and
          // it could possibly be non-trivial constructor.
          if (Init->isTypeDependent()) {
            for (const CXXConstructorDecl *Ctor : RD->ctors())
              if (!Ctor->isTrivial())
                return false;
          }

          // Suppress the warning if the constructor is unresolved because
          // its arguments are dependent.
          if (isa<CXXUnresolvedConstructExpr>(Init))
            return false;
        }
      }
    }

    // TODO: __attribute__((unused)) templates?
  }

  return true;
}

static void GenerateFixForUnusedDecl(const NamedDecl *D, ASTContext &Ctx,
                                     FixItHint &Hint) {
  if (isa<LabelDecl>(D)) {
    SourceLocation AfterColon = Lexer::findLocationAfterToken(
        D->getEndLoc(), tok::colon, Ctx.getSourceManager(), Ctx.getLangOpts(),
        /*SkipTrailingWhitespaceAndNewline=*/false);
    if (AfterColon.isInvalid())
      return;
    Hint = FixItHint::CreateRemoval(
        CharSourceRange::getCharRange(D->getBeginLoc(), AfterColon));
  }
}

void Sema::DiagnoseUnusedNestedTypedefs(const RecordDecl *D) {
  DiagnoseUnusedNestedTypedefs(
      D, [this](SourceLocation Loc, PartialDiagnostic PD) { Diag(Loc, PD); });
}

void Sema::DiagnoseUnusedNestedTypedefs(const RecordDecl *D,
                                        DiagReceiverTy DiagReceiver) {
  if (D->getTypeForDecl()->isDependentType())
    return;

  for (auto *TmpD : D->decls()) {
    if (const auto *T = dyn_cast<TypedefNameDecl>(TmpD))
      DiagnoseUnusedDecl(T, DiagReceiver);
    else if(const auto *R = dyn_cast<RecordDecl>(TmpD))
      DiagnoseUnusedNestedTypedefs(R, DiagReceiver);
  }
}

void Sema::DiagnoseUnusedDecl(const NamedDecl *D) {
  DiagnoseUnusedDecl(
      D, [this](SourceLocation Loc, PartialDiagnostic PD) { Diag(Loc, PD); });
}

void Sema::DiagnoseUnusedDecl(const NamedDecl *D, DiagReceiverTy DiagReceiver) {
  if (!ShouldDiagnoseUnusedDecl(getLangOpts(), D))
    return;

  if (auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    // typedefs can be referenced later on, so the diagnostics are emitted
    // at end-of-translation-unit.
    UnusedLocalTypedefNameCandidates.insert(TD);
    return;
  }

  FixItHint Hint;
  GenerateFixForUnusedDecl(D, Context, Hint);

  unsigned DiagID;
  if (isa<VarDecl>(D) && cast<VarDecl>(D)->isExceptionVariable())
    DiagID = diag::warn_unused_exception_param;
  else if (isa<LabelDecl>(D))
    DiagID = diag::warn_unused_label;
  else
    DiagID = diag::warn_unused_variable;

  SourceLocation DiagLoc = D->getLocation();
  DiagReceiver(DiagLoc, PDiag(DiagID) << D << Hint << SourceRange(DiagLoc));
}

void Sema::DiagnoseUnusedButSetDecl(const VarDecl *VD,
                                    DiagReceiverTy DiagReceiver) {
  // If it's not referenced, it can't be set. If it has the Cleanup attribute,
  // it's not really unused.
  if (!VD->isReferenced() || !VD->getDeclName() || VD->hasAttr<CleanupAttr>())
    return;

  //  In C++, `_` variables behave as if they were maybe_unused
  if (VD->hasAttr<UnusedAttr>() || VD->isPlaceholderVar(getLangOpts()))
    return;

  const auto *Ty = VD->getType().getTypePtr()->getBaseElementTypeUnsafe();

  if (Ty->isReferenceType() || Ty->isDependentType())
    return;

  if (const TagType *TT = Ty->getAs<TagType>()) {
    const TagDecl *Tag = TT->getDecl();
    if (Tag->hasAttr<UnusedAttr>())
      return;
    // In C++, don't warn for record types that don't have WarnUnusedAttr, to
    // mimic gcc's behavior.
    if (const auto *RD = dyn_cast<CXXRecordDecl>(Tag);
        RD && !RD->hasAttr<WarnUnusedAttr>())
      return;
  }

  // Don't warn about __block Objective-C pointer variables, as they might
  // be assigned in the block but not used elsewhere for the purpose of lifetime
  // extension.
  if (VD->hasAttr<BlocksAttr>() && Ty->isObjCObjectPointerType())
    return;

  // Don't warn about Objective-C pointer variables with precise lifetime
  // semantics; they can be used to ensure ARC releases the object at a known
  // time, which may mean assignment but no other references.
  if (VD->hasAttr<ObjCPreciseLifetimeAttr>() && Ty->isObjCObjectPointerType())
    return;

  auto iter = RefsMinusAssignments.find(VD);
  if (iter == RefsMinusAssignments.end())
    return;

  assert(iter->getSecond() >= 0 &&
         "Found a negative number of references to a VarDecl");
  if (int RefCnt = iter->getSecond(); RefCnt > 0) {
    // Assume the given VarDecl is "used" if its ref count stored in
    // `RefMinusAssignments` is positive, with one exception.
    //
    // For a C++ variable whose decl (with initializer) entirely consist the
    // condition expression of a if/while/for construct,
    // Clang creates a DeclRefExpr for the condition expression rather than a
    // BinaryOperator of AssignmentOp. Thus, the C++ variable's ref
    // count stored in `RefMinusAssignment` equals 1 when the variable is never
    // used in the body of the if/while/for construct.
    bool UnusedCXXCondDecl = VD->isCXXCondDecl() && (RefCnt == 1);
    if (!UnusedCXXCondDecl)
      return;
  }

  unsigned DiagID = isa<ParmVarDecl>(VD) ? diag::warn_unused_but_set_parameter
                                         : diag::warn_unused_but_set_variable;
  DiagReceiver(VD->getLocation(), PDiag(DiagID) << VD);
}

static void CheckPoppedLabel(LabelDecl *L, Sema &S,
                             Sema::DiagReceiverTy DiagReceiver) {
  // Verify that we have no forward references left.  If so, there was a goto
  // or address of a label taken, but no definition of it.  Label fwd
  // definitions are indicated with a null substmt which is also not a resolved
  // MS inline assembly label name.
  bool Diagnose = false;
  if (L->isMSAsmLabel())
    Diagnose = !L->isResolvedMSAsmLabel();
  else
    Diagnose = L->getStmt() == nullptr;
  if (Diagnose)
    DiagReceiver(L->getLocation(), S.PDiag(diag::err_undeclared_label_use)
                                       << L);
}

void Sema::ActOnPopScope(SourceLocation Loc, Scope *S) {
  S->applyNRVO();

  if (S->decl_empty()) return;
  assert((S->getFlags() & (Scope::DeclScope | Scope::TemplateParamScope)) &&
         "Scope shouldn't contain decls!");

  /// We visit the decls in non-deterministic order, but we want diagnostics
  /// emitted in deterministic order. Collect any diagnostic that may be emitted
  /// and sort the diagnostics before emitting them, after we visited all decls.
  struct LocAndDiag {
    SourceLocation Loc;
    std::optional<SourceLocation> PreviousDeclLoc;
    PartialDiagnostic PD;
  };
  SmallVector<LocAndDiag, 16> DeclDiags;
  auto addDiag = [&DeclDiags](SourceLocation Loc, PartialDiagnostic PD) {
    DeclDiags.push_back(LocAndDiag{Loc, std::nullopt, std::move(PD)});
  };
  auto addDiagWithPrev = [&DeclDiags](SourceLocation Loc,
                                      SourceLocation PreviousDeclLoc,
                                      PartialDiagnostic PD) {
    DeclDiags.push_back(LocAndDiag{Loc, PreviousDeclLoc, std::move(PD)});
  };

  for (auto *TmpD : S->decls()) {
    assert(TmpD && "This decl didn't get pushed??");

    assert(isa<NamedDecl>(TmpD) && "Decl isn't NamedDecl?");
    NamedDecl *D = cast<NamedDecl>(TmpD);

    // Diagnose unused variables in this scope.
    if (!S->hasUnrecoverableErrorOccurred()) {
      DiagnoseUnusedDecl(D, addDiag);
      if (const auto *RD = dyn_cast<RecordDecl>(D))
        DiagnoseUnusedNestedTypedefs(RD, addDiag);
      if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
        DiagnoseUnusedButSetDecl(VD, addDiag);
        RefsMinusAssignments.erase(VD);
      }
    }

    if (!D->getDeclName()) continue;

    // If this was a forward reference to a label, verify it was defined.
    if (LabelDecl *LD = dyn_cast<LabelDecl>(D))
      CheckPoppedLabel(LD, *this, addDiag);

    // Partial translation units that are created in incremental processing must
    // not clean up the IdResolver because PTUs should take into account the
    // declarations that came from previous PTUs.
    if (!PP.isIncrementalProcessingEnabled() || getLangOpts().ObjC ||
        getLangOpts().CPlusPlus)
      IdResolver.RemoveDecl(D);

    // Warn on it if we are shadowing a declaration.
    auto ShadowI = ShadowingDecls.find(D);
    if (ShadowI != ShadowingDecls.end()) {
      if (const auto *FD = dyn_cast<FieldDecl>(ShadowI->second)) {
        addDiagWithPrev(D->getLocation(), FD->getLocation(),
                        PDiag(diag::warn_ctor_parm_shadows_field)
                            << D << FD << FD->getParent());
      }
      ShadowingDecls.erase(ShadowI);
    }
  }

  llvm::sort(DeclDiags,
             [](const LocAndDiag &LHS, const LocAndDiag &RHS) -> bool {
               // The particular order for diagnostics is not important, as long
               // as the order is deterministic. Using the raw location is going
               // to generally be in source order unless there are macro
               // expansions involved.
               return LHS.Loc.getRawEncoding() < RHS.Loc.getRawEncoding();
             });
  for (const LocAndDiag &D : DeclDiags) {
    Diag(D.Loc, D.PD);
    if (D.PreviousDeclLoc)
      Diag(*D.PreviousDeclLoc, diag::note_previous_declaration);
  }
}

Scope *Sema::getNonFieldDeclScope(Scope *S) {
  while (((S->getFlags() & Scope::DeclScope) == 0) ||
         (S->getEntity() && S->getEntity()->isTransparentContext()) ||
         (S->isClassScope() && !getLangOpts().CPlusPlus))
    S = S->getParent();
  return S;
}

static StringRef getHeaderName(Builtin::Context &BuiltinInfo, unsigned ID,
                               ASTContext::GetBuiltinTypeError Error) {
  switch (Error) {
  case ASTContext::GE_None:
    return "";
  case ASTContext::GE_Missing_type:
    return BuiltinInfo.getHeaderName(ID);
  case ASTContext::GE_Missing_stdio:
    return "stdio.h";
  case ASTContext::GE_Missing_setjmp:
    return "setjmp.h";
  case ASTContext::GE_Missing_ucontext:
    return "ucontext.h";
  }
  llvm_unreachable("unhandled error kind");
}

FunctionDecl *Sema::CreateBuiltin(IdentifierInfo *II, QualType Type,
                                  unsigned ID, SourceLocation Loc) {
  DeclContext *Parent = Context.getTranslationUnitDecl();

  if (getLangOpts().CPlusPlus) {
    LinkageSpecDecl *CLinkageDecl = LinkageSpecDecl::Create(
        Context, Parent, Loc, Loc, LinkageSpecLanguageIDs::C, false);
    CLinkageDecl->setImplicit();
    Parent->addDecl(CLinkageDecl);
    Parent = CLinkageDecl;
  }

  ConstexprSpecKind ConstexprKind = ConstexprSpecKind::Unspecified;
  if (Context.BuiltinInfo.isImmediate(ID)) {
    assert(getLangOpts().CPlusPlus20 &&
           "consteval builtins should only be available in C++20 mode");
    ConstexprKind = ConstexprSpecKind::Consteval;
  }

  FunctionDecl *New = FunctionDecl::Create(
      Context, Parent, Loc, Loc, II, Type, /*TInfo=*/nullptr, SC_Extern,
      getCurFPFeatures().isFPConstrained(), /*isInlineSpecified=*/false,
      Type->isFunctionProtoType(), ConstexprKind);
  New->setImplicit();
  New->addAttr(BuiltinAttr::CreateImplicit(Context, ID));

  // Create Decl objects for each parameter, adding them to the
  // FunctionDecl.
  if (const FunctionProtoType *FT = dyn_cast<FunctionProtoType>(Type)) {
    SmallVector<ParmVarDecl *, 16> Params;
    for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i) {
      ParmVarDecl *parm = ParmVarDecl::Create(
          Context, New, SourceLocation(), SourceLocation(), nullptr,
          FT->getParamType(i), /*TInfo=*/nullptr, SC_None, nullptr);
      parm->setScopeInfo(0, i);
      Params.push_back(parm);
    }
    New->setParams(Params);
  }

  AddKnownFunctionAttributes(New);
  return New;
}

NamedDecl *Sema::LazilyCreateBuiltin(IdentifierInfo *II, unsigned ID,
                                     Scope *S, bool ForRedeclaration,
                                     SourceLocation Loc) {
  LookupNecessaryTypesForBuiltin(S, ID);

  ASTContext::GetBuiltinTypeError Error;
  QualType R = Context.GetBuiltinType(ID, Error);
  if (Error) {
    if (!ForRedeclaration)
      return nullptr;

    // If we have a builtin without an associated type we should not emit a
    // warning when we were not able to find a type for it.
    if (Error == ASTContext::GE_Missing_type ||
        Context.BuiltinInfo.allowTypeMismatch(ID))
      return nullptr;

    // If we could not find a type for setjmp it is because the jmp_buf type was
    // not defined prior to the setjmp declaration.
    if (Error == ASTContext::GE_Missing_setjmp) {
      Diag(Loc, diag::warn_implicit_decl_no_jmp_buf)
          << Context.BuiltinInfo.getName(ID);
      return nullptr;
    }

    // Generally, we emit a warning that the declaration requires the
    // appropriate header.
    Diag(Loc, diag::warn_implicit_decl_requires_sysheader)
        << getHeaderName(Context.BuiltinInfo, ID, Error)
        << Context.BuiltinInfo.getName(ID);
    return nullptr;
  }

  if (!ForRedeclaration &&
      (Context.BuiltinInfo.isPredefinedLibFunction(ID) ||
       Context.BuiltinInfo.isHeaderDependentFunction(ID))) {
    Diag(Loc, LangOpts.C99 ? diag::ext_implicit_lib_function_decl_c99
                           : diag::ext_implicit_lib_function_decl)
        << Context.BuiltinInfo.getName(ID) << R;
    if (const char *Header = Context.BuiltinInfo.getHeaderName(ID))
      Diag(Loc, diag::note_include_header_or_declare)
          << Header << Context.BuiltinInfo.getName(ID);
  }

  if (R.isNull())
    return nullptr;

  FunctionDecl *New = CreateBuiltin(II, R, ID, Loc);
  RegisterLocallyScopedExternCDecl(New, S);

  // TUScope is the translation-unit scope to insert this function into.
  // FIXME: This is hideous. We need to teach PushOnScopeChains to
  // relate Scopes to DeclContexts, and probably eliminate CurContext
  // entirely, but we're not there yet.
  DeclContext *SavedContext = CurContext;
  CurContext = New->getDeclContext();
  PushOnScopeChains(New, TUScope);
  CurContext = SavedContext;
  return New;
}

/// Typedef declarations don't have linkage, but they still denote the same
/// entity if their types are the same.
/// FIXME: This is notionally doing the same thing as ASTReaderDecl's
/// isSameEntity.
static void
filterNonConflictingPreviousTypedefDecls(Sema &S, const TypedefNameDecl *Decl,
                                         LookupResult &Previous) {
  // This is only interesting when modules are enabled.
  if (!S.getLangOpts().Modules && !S.getLangOpts().ModulesLocalVisibility)
    return;

  // Empty sets are uninteresting.
  if (Previous.empty())
    return;

  LookupResult::Filter Filter = Previous.makeFilter();
  while (Filter.hasNext()) {
    NamedDecl *Old = Filter.next();

    // Non-hidden declarations are never ignored.
    if (S.isVisible(Old))
      continue;

    // Declarations of the same entity are not ignored, even if they have
    // different linkages.
    if (auto *OldTD = dyn_cast<TypedefNameDecl>(Old)) {
      if (S.Context.hasSameType(OldTD->getUnderlyingType(),
                                Decl->getUnderlyingType()))
        continue;

      // If both declarations give a tag declaration a typedef name for linkage
      // purposes, then they declare the same entity.
      if (OldTD->getAnonDeclWithTypedefName(/*AnyRedecl*/true) &&
          Decl->getAnonDeclWithTypedefName())
        continue;
    }

    Filter.erase();
  }

  Filter.done();
}

bool Sema::isIncompatibleTypedef(const TypeDecl *Old, TypedefNameDecl *New) {
  QualType OldType;
  if (const TypedefNameDecl *OldTypedef = dyn_cast<TypedefNameDecl>(Old))
    OldType = OldTypedef->getUnderlyingType();
  else
    OldType = Context.getTypeDeclType(Old);
  QualType NewType = New->getUnderlyingType();

  if (NewType->isVariablyModifiedType()) {
    // Must not redefine a typedef with a variably-modified type.
    int Kind = isa<TypeAliasDecl>(Old) ? 1 : 0;
    Diag(New->getLocation(), diag::err_redefinition_variably_modified_typedef)
      << Kind << NewType;
    if (Old->getLocation().isValid())
      notePreviousDefinition(Old, New->getLocation());
    New->setInvalidDecl();
    return true;
  }

  if (OldType != NewType &&
      !OldType->isDependentType() &&
      !NewType->isDependentType() &&
      !Context.hasSameType(OldType, NewType)) {
    int Kind = isa<TypeAliasDecl>(Old) ? 1 : 0;
    Diag(New->getLocation(), diag::err_redefinition_different_typedef)
      << Kind << NewType << OldType;
    if (Old->getLocation().isValid())
      notePreviousDefinition(Old, New->getLocation());
    New->setInvalidDecl();
    return true;
  }
  return false;
}

void Sema::MergeTypedefNameDecl(Scope *S, TypedefNameDecl *New,
                                LookupResult &OldDecls) {
  // If the new decl is known invalid already, don't bother doing any
  // merging checks.
  if (New->isInvalidDecl()) return;

  // Allow multiple definitions for ObjC built-in typedefs.
  // FIXME: Verify the underlying types are equivalent!
  if (getLangOpts().ObjC) {
    const IdentifierInfo *TypeID = New->getIdentifier();
    switch (TypeID->getLength()) {
    default: break;
    case 2:
      {
        if (!TypeID->isStr("id"))
          break;
        QualType T = New->getUnderlyingType();
        if (!T->isPointerType())
          break;
        if (!T->isVoidPointerType()) {
          QualType PT = T->castAs<PointerType>()->getPointeeType();
          if (!PT->isStructureType())
            break;
        }
        Context.setObjCIdRedefinitionType(T);
        // Install the built-in type for 'id', ignoring the current definition.
        New->setTypeForDecl(Context.getObjCIdType().getTypePtr());
        return;
      }
    case 5:
      if (!TypeID->isStr("Class"))
        break;
      Context.setObjCClassRedefinitionType(New->getUnderlyingType());
      // Install the built-in type for 'Class', ignoring the current definition.
      New->setTypeForDecl(Context.getObjCClassType().getTypePtr());
      return;
    case 3:
      if (!TypeID->isStr("SEL"))
        break;
      Context.setObjCSelRedefinitionType(New->getUnderlyingType());
      // Install the built-in type for 'SEL', ignoring the current definition.
      New->setTypeForDecl(Context.getObjCSelType().getTypePtr());
      return;
    }
    // Fall through - the typedef name was not a builtin type.
  }

  // Verify the old decl was also a type.
  TypeDecl *Old = OldDecls.getAsSingle<TypeDecl>();
  if (!Old) {
    Diag(New->getLocation(), diag::err_redefinition_different_kind)
      << New->getDeclName();

    NamedDecl *OldD = OldDecls.getRepresentativeDecl();
    if (OldD->getLocation().isValid())
      notePreviousDefinition(OldD, New->getLocation());

    return New->setInvalidDecl();
  }

  // If the old declaration is invalid, just give up here.
  if (Old->isInvalidDecl())
    return New->setInvalidDecl();

  if (auto *OldTD = dyn_cast<TypedefNameDecl>(Old)) {
    auto *OldTag = OldTD->getAnonDeclWithTypedefName(/*AnyRedecl*/true);
    auto *NewTag = New->getAnonDeclWithTypedefName();
    NamedDecl *Hidden = nullptr;
    if (OldTag && NewTag &&
        OldTag->getCanonicalDecl() != NewTag->getCanonicalDecl() &&
        !hasVisibleDefinition(OldTag, &Hidden)) {
      // There is a definition of this tag, but it is not visible. Use it
      // instead of our tag.
      New->setTypeForDecl(OldTD->getTypeForDecl());
      if (OldTD->isModed())
        New->setModedTypeSourceInfo(OldTD->getTypeSourceInfo(),
                                    OldTD->getUnderlyingType());
      else
        New->setTypeSourceInfo(OldTD->getTypeSourceInfo());

      // Make the old tag definition visible.
      makeMergedDefinitionVisible(Hidden);

      // If this was an unscoped enumeration, yank all of its enumerators
      // out of the scope.
      if (isa<EnumDecl>(NewTag)) {
        Scope *EnumScope = getNonFieldDeclScope(S);
        for (auto *D : NewTag->decls()) {
          auto *ED = cast<EnumConstantDecl>(D);
          assert(EnumScope->isDeclScope(ED));
          EnumScope->RemoveDecl(ED);
          IdResolver.RemoveDecl(ED);
          ED->getLexicalDeclContext()->removeDecl(ED);
        }
      }
    }
  }

  // If the typedef types are not identical, reject them in all languages and
  // with any extensions enabled.
  if (isIncompatibleTypedef(Old, New))
    return;

  // The types match.  Link up the redeclaration chain and merge attributes if
  // the old declaration was a typedef.
  if (TypedefNameDecl *Typedef = dyn_cast<TypedefNameDecl>(Old)) {
    New->setPreviousDecl(Typedef);
    mergeDeclAttributes(New, Old);
  }

  if (getLangOpts().MicrosoftExt)
    return;

  if (getLangOpts().CPlusPlus) {
    // C++ [dcl.typedef]p2:
    //   In a given non-class scope, a typedef specifier can be used to
    //   redefine the name of any type declared in that scope to refer
    //   to the type to which it already refers.
    if (!isa<CXXRecordDecl>(CurContext))
      return;

    // C++0x [dcl.typedef]p4:
    //   In a given class scope, a typedef specifier can be used to redefine
    //   any class-name declared in that scope that is not also a typedef-name
    //   to refer to the type to which it already refers.
    //
    // This wording came in via DR424, which was a correction to the
    // wording in DR56, which accidentally banned code like:
    //
    //   struct S {
    //     typedef struct A { } A;
    //   };
    //
    // in the C++03 standard. We implement the C++0x semantics, which
    // allow the above but disallow
    //
    //   struct S {
    //     typedef int I;
    //     typedef int I;
    //   };
    //
    // since that was the intent of DR56.
    if (!isa<TypedefNameDecl>(Old))
      return;

    Diag(New->getLocation(), diag::err_redefinition)
      << New->getDeclName();
    notePreviousDefinition(Old, New->getLocation());
    return New->setInvalidDecl();
  }

  // Modules always permit redefinition of typedefs, as does C11.
  if (getLangOpts().Modules || getLangOpts().C11)
    return;

  // If we have a redefinition of a typedef in C, emit a warning.  This warning
  // is normally mapped to an error, but can be controlled with
  // -Wtypedef-redefinition.  If either the original or the redefinition is
  // in a system header, don't emit this for compatibility with GCC.
  if (getDiagnostics().getSuppressSystemWarnings() &&
      // Some standard types are defined implicitly in Clang (e.g. OpenCL).
      (Old->isImplicit() ||
       Context.getSourceManager().isInSystemHeader(Old->getLocation()) ||
       Context.getSourceManager().isInSystemHeader(New->getLocation())))
    return;

  Diag(New->getLocation(), diag::ext_redefinition_of_typedef)
    << New->getDeclName();
  notePreviousDefinition(Old, New->getLocation());
}

/// DeclhasAttr - returns true if decl Declaration already has the target
/// attribute.
static bool DeclHasAttr(const Decl *D, const Attr *A) {
  const OwnershipAttr *OA = dyn_cast<OwnershipAttr>(A);
  const AnnotateAttr *Ann = dyn_cast<AnnotateAttr>(A);
  for (const auto *i : D->attrs())
    if (i->getKind() == A->getKind()) {
      if (Ann) {
        if (Ann->getAnnotation() == cast<AnnotateAttr>(i)->getAnnotation())
          return true;
        continue;
      }
      // FIXME: Don't hardcode this check
      if (OA && isa<OwnershipAttr>(i))
        return OA->getOwnKind() == cast<OwnershipAttr>(i)->getOwnKind();
      return true;
    }

  return false;
}

static bool isAttributeTargetADefinition(Decl *D) {
  if (VarDecl *VD = dyn_cast<VarDecl>(D))
    return VD->isThisDeclarationADefinition();
  if (TagDecl *TD = dyn_cast<TagDecl>(D))
    return TD->isCompleteDefinition() || TD->isBeingDefined();
  return true;
}

/// Merge alignment attributes from \p Old to \p New, taking into account the
/// special semantics of C11's _Alignas specifier and C++11's alignas attribute.
///
/// \return \c true if any attributes were added to \p New.
static bool mergeAlignedAttrs(Sema &S, NamedDecl *New, Decl *Old) {
  // Look for alignas attributes on Old, and pick out whichever attribute
  // specifies the strictest alignment requirement.
  AlignedAttr *OldAlignasAttr = nullptr;
  AlignedAttr *OldStrictestAlignAttr = nullptr;
  unsigned OldAlign = 0;
  for (auto *I : Old->specific_attrs<AlignedAttr>()) {
    // FIXME: We have no way of representing inherited dependent alignments
    // in a case like:
    //   template<int A, int B> struct alignas(A) X;
    //   template<int A, int B> struct alignas(B) X {};
    // For now, we just ignore any alignas attributes which are not on the
    // definition in such a case.
    if (I->isAlignmentDependent())
      return false;

    if (I->isAlignas())
      OldAlignasAttr = I;

    unsigned Align = I->getAlignment(S.Context);
    if (Align > OldAlign) {
      OldAlign = Align;
      OldStrictestAlignAttr = I;
    }
  }

  // Look for alignas attributes on New.
  AlignedAttr *NewAlignasAttr = nullptr;
  unsigned NewAlign = 0;
  for (auto *I : New->specific_attrs<AlignedAttr>()) {
    if (I->isAlignmentDependent())
      return false;

    if (I->isAlignas())
      NewAlignasAttr = I;

    unsigned Align = I->getAlignment(S.Context);
    if (Align > NewAlign)
      NewAlign = Align;
  }

  if (OldAlignasAttr && NewAlignasAttr && OldAlign != NewAlign) {
    // Both declarations have 'alignas' attributes. We require them to match.
    // C++11 [dcl.align]p6 and C11 6.7.5/7 both come close to saying this, but
    // fall short. (If two declarations both have alignas, they must both match
    // every definition, and so must match each other if there is a definition.)

    // If either declaration only contains 'alignas(0)' specifiers, then it
    // specifies the natural alignment for the type.
    if (OldAlign == 0 || NewAlign == 0) {
      QualType Ty;
      if (ValueDecl *VD = dyn_cast<ValueDecl>(New))
        Ty = VD->getType();
      else
        Ty = S.Context.getTagDeclType(cast<TagDecl>(New));

      if (OldAlign == 0)
        OldAlign = S.Context.getTypeAlign(Ty);
      if (NewAlign == 0)
        NewAlign = S.Context.getTypeAlign(Ty);
    }

    if (OldAlign != NewAlign) {
      S.Diag(NewAlignasAttr->getLocation(), diag::err_alignas_mismatch)
        << (unsigned)S.Context.toCharUnitsFromBits(OldAlign).getQuantity()
        << (unsigned)S.Context.toCharUnitsFromBits(NewAlign).getQuantity();
      S.Diag(OldAlignasAttr->getLocation(), diag::note_previous_declaration);
    }
  }

  if (OldAlignasAttr && !NewAlignasAttr && isAttributeTargetADefinition(New)) {
    // C++11 [dcl.align]p6:
    //   if any declaration of an entity has an alignment-specifier,
    //   every defining declaration of that entity shall specify an
    //   equivalent alignment.
    // C11 6.7.5/7:
    //   If the definition of an object does not have an alignment
    //   specifier, any other declaration of that object shall also
    //   have no alignment specifier.
    S.Diag(New->getLocation(), diag::err_alignas_missing_on_definition)
      << OldAlignasAttr;
    S.Diag(OldAlignasAttr->getLocation(), diag::note_alignas_on_declaration)
      << OldAlignasAttr;
  }

  bool AnyAdded = false;

  // Ensure we have an attribute representing the strictest alignment.
  if (OldAlign > NewAlign) {
    AlignedAttr *Clone = OldStrictestAlignAttr->clone(S.Context);
    Clone->setInherited(true);
    New->addAttr(Clone);
    AnyAdded = true;
  }

  // Ensure we have an alignas attribute if the old declaration had one.
  if (OldAlignasAttr && !NewAlignasAttr &&
      !(AnyAdded && OldStrictestAlignAttr->isAlignas())) {
    AlignedAttr *Clone = OldAlignasAttr->clone(S.Context);
    Clone->setInherited(true);
    New->addAttr(Clone);
    AnyAdded = true;
  }

  return AnyAdded;
}

#define WANT_DECL_MERGE_LOGIC
#include "clang/Sema/AttrParsedAttrImpl.inc"
#undef WANT_DECL_MERGE_LOGIC

static bool mergeDeclAttribute(Sema &S, NamedDecl *D,
                               const InheritableAttr *Attr,
                               Sema::AvailabilityMergeKind AMK) {
  // Diagnose any mutual exclusions between the attribute that we want to add
  // and attributes that already exist on the declaration.
  if (!DiagnoseMutualExclusions(S, D, Attr))
    return false;

  // This function copies an attribute Attr from a previous declaration to the
  // new declaration D if the new declaration doesn't itself have that attribute
  // yet or if that attribute allows duplicates.
  // If you're adding a new attribute that requires logic different from
  // "use explicit attribute on decl if present, else use attribute from
  // previous decl", for example if the attribute needs to be consistent
  // between redeclarations, you need to call a custom merge function here.
  InheritableAttr *NewAttr = nullptr;
  if (const auto *AA = dyn_cast<AvailabilityAttr>(Attr))
    NewAttr = S.mergeAvailabilityAttr(
        D, *AA, AA->getPlatform(), AA->isImplicit(), AA->getIntroduced(),
        AA->getDeprecated(), AA->getObsoleted(), AA->getUnavailable(),
        AA->getMessage(), AA->getStrict(), AA->getReplacement(), AMK,
        AA->getPriority(), AA->getEnvironment());
  else if (const auto *VA = dyn_cast<VisibilityAttr>(Attr))
    NewAttr = S.mergeVisibilityAttr(D, *VA, VA->getVisibility());
  else if (const auto *VA = dyn_cast<TypeVisibilityAttr>(Attr))
    NewAttr = S.mergeTypeVisibilityAttr(D, *VA, VA->getVisibility());
  else if (const auto *ImportA = dyn_cast<DLLImportAttr>(Attr))
    NewAttr = S.mergeDLLImportAttr(D, *ImportA);
  else if (const auto *ExportA = dyn_cast<DLLExportAttr>(Attr))
    NewAttr = S.mergeDLLExportAttr(D, *ExportA);
  else if (const auto *EA = dyn_cast<ErrorAttr>(Attr))
    NewAttr = S.mergeErrorAttr(D, *EA, EA->getUserDiagnostic());
  else if (const auto *FA = dyn_cast<FormatAttr>(Attr))
    NewAttr = S.mergeFormatAttr(D, *FA, FA->getType(), FA->getFormatIdx(),
                                FA->getFirstArg());
  else if (const auto *SA = dyn_cast<SectionAttr>(Attr))
    NewAttr = S.mergeSectionAttr(D, *SA, SA->getName());
  else if (const auto *CSA = dyn_cast<CodeSegAttr>(Attr))
    NewAttr = S.mergeCodeSegAttr(D, *CSA, CSA->getName());
  else if (const auto *IA = dyn_cast<MSInheritanceAttr>(Attr))
    NewAttr = S.mergeMSInheritanceAttr(D, *IA, IA->getBestCase(),
                                       IA->getInheritanceModel());
  else if (const auto *AA = dyn_cast<AlwaysInlineAttr>(Attr))
    NewAttr = S.mergeAlwaysInlineAttr(D, *AA,
                                      &S.Context.Idents.get(AA->getSpelling()));
  else if (S.getLangOpts().CUDA && isa<FunctionDecl>(D) &&
           (isa<CUDAHostAttr>(Attr) || isa<CUDADeviceAttr>(Attr) ||
            isa<CUDAGlobalAttr>(Attr))) {
    // CUDA target attributes are part of function signature for
    // overloading purposes and must not be merged.
    return false;
  } else if (const auto *MA = dyn_cast<MinSizeAttr>(Attr))
    NewAttr = S.mergeMinSizeAttr(D, *MA);
  else if (const auto *SNA = dyn_cast<SwiftNameAttr>(Attr))
    NewAttr = S.Swift().mergeNameAttr(D, *SNA, SNA->getName());
  else if (const auto *OA = dyn_cast<OptimizeNoneAttr>(Attr))
    NewAttr = S.mergeOptimizeNoneAttr(D, *OA);
  else if (const auto *InternalLinkageA = dyn_cast<InternalLinkageAttr>(Attr))
    NewAttr = S.mergeInternalLinkageAttr(D, *InternalLinkageA);
  else if (isa<AlignedAttr>(Attr))
    // AlignedAttrs are handled separately, because we need to handle all
    // such attributes on a declaration at the same time.
    NewAttr = nullptr;
  else if ((isa<DeprecatedAttr>(Attr) || isa<UnavailableAttr>(Attr)) &&
           (AMK == Sema::AMK_Override ||
            AMK == Sema::AMK_ProtocolImplementation ||
            AMK == Sema::AMK_OptionalProtocolImplementation))
    NewAttr = nullptr;
  else if (const auto *UA = dyn_cast<UuidAttr>(Attr))
    NewAttr = S.mergeUuidAttr(D, *UA, UA->getGuid(), UA->getGuidDecl());
  else if (const auto *IMA = dyn_cast<WebAssemblyImportModuleAttr>(Attr))
    NewAttr = S.Wasm().mergeImportModuleAttr(D, *IMA);
  else if (const auto *INA = dyn_cast<WebAssemblyImportNameAttr>(Attr))
    NewAttr = S.Wasm().mergeImportNameAttr(D, *INA);
  else if (const auto *TCBA = dyn_cast<EnforceTCBAttr>(Attr))
    NewAttr = S.mergeEnforceTCBAttr(D, *TCBA);
  else if (const auto *TCBLA = dyn_cast<EnforceTCBLeafAttr>(Attr))
    NewAttr = S.mergeEnforceTCBLeafAttr(D, *TCBLA);
  else if (const auto *BTFA = dyn_cast<BTFDeclTagAttr>(Attr))
    NewAttr = S.mergeBTFDeclTagAttr(D, *BTFA);
  else if (const auto *NT = dyn_cast<HLSLNumThreadsAttr>(Attr))
    NewAttr = S.HLSL().mergeNumThreadsAttr(D, *NT, NT->getX(), NT->getY(),
                                           NT->getZ());
  else if (const auto *SA = dyn_cast<HLSLShaderAttr>(Attr))
    NewAttr = S.HLSL().mergeShaderAttr(D, *SA, SA->getType());
  else if (isa<SuppressAttr>(Attr))
    // Do nothing. Each redeclaration should be suppressed separately.
    NewAttr = nullptr;
  else if (Attr->shouldInheritEvenIfAlreadyPresent() || !DeclHasAttr(D, Attr))
    NewAttr = cast<InheritableAttr>(Attr->clone(S.Context));

  if (NewAttr) {
    NewAttr->setInherited(true);
    D->addAttr(NewAttr);
    if (isa<MSInheritanceAttr>(NewAttr))
      S.Consumer.AssignInheritanceModel(cast<CXXRecordDecl>(D));
    return true;
  }

  return false;
}

static const NamedDecl *getDefinition(const Decl *D) {
  if (const TagDecl *TD = dyn_cast<TagDecl>(D))
    return TD->getDefinition();
  if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
    const VarDecl *Def = VD->getDefinition();
    if (Def)
      return Def;
    return VD->getActingDefinition();
  }
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    const FunctionDecl *Def = nullptr;
    if (FD->isDefined(Def, true))
      return Def;
  }
  return nullptr;
}

static bool hasAttribute(const Decl *D, attr::Kind Kind) {
  for (const auto *Attribute : D->attrs())
    if (Attribute->getKind() == Kind)
      return true;
  return false;
}

/// checkNewAttributesAfterDef - If we already have a definition, check that
/// there are no new attributes in this declaration.
static void checkNewAttributesAfterDef(Sema &S, Decl *New, const Decl *Old) {
  if (!New->hasAttrs())
    return;

  const NamedDecl *Def = getDefinition(Old);
  if (!Def || Def == New)
    return;

  AttrVec &NewAttributes = New->getAttrs();
  for (unsigned I = 0, E = NewAttributes.size(); I != E;) {
    const Attr *NewAttribute = NewAttributes[I];

    if (isa<AliasAttr>(NewAttribute) || isa<IFuncAttr>(NewAttribute)) {
      if (FunctionDecl *FD = dyn_cast<FunctionDecl>(New)) {
        SkipBodyInfo SkipBody;
        S.CheckForFunctionRedefinition(FD, cast<FunctionDecl>(Def), &SkipBody);

        // If we're skipping this definition, drop the "alias" attribute.
        if (SkipBody.ShouldSkip) {
          NewAttributes.erase(NewAttributes.begin() + I);
          --E;
          continue;
        }
      } else {
        VarDecl *VD = cast<VarDecl>(New);
        unsigned Diag = cast<VarDecl>(Def)->isThisDeclarationADefinition() ==
                                VarDecl::TentativeDefinition
                            ? diag::err_alias_after_tentative
                            : diag::err_redefinition;
        S.Diag(VD->getLocation(), Diag) << VD->getDeclName();
        if (Diag == diag::err_redefinition)
          S.notePreviousDefinition(Def, VD->getLocation());
        else
          S.Diag(Def->getLocation(), diag::note_previous_definition);
        VD->setInvalidDecl();
      }
      ++I;
      continue;
    }

    if (const VarDecl *VD = dyn_cast<VarDecl>(Def)) {
      // Tentative definitions are only interesting for the alias check above.
      if (VD->isThisDeclarationADefinition() != VarDecl::Definition) {
        ++I;
        continue;
      }
    }

    if (hasAttribute(Def, NewAttribute->getKind())) {
      ++I;
      continue; // regular attr merging will take care of validating this.
    }

    if (isa<C11NoReturnAttr>(NewAttribute)) {
      // C's _Noreturn is allowed to be added to a function after it is defined.
      ++I;
      continue;
    } else if (isa<UuidAttr>(NewAttribute)) {
      // msvc will allow a subsequent definition to add an uuid to a class
      ++I;
      continue;
    } else if (const AlignedAttr *AA = dyn_cast<AlignedAttr>(NewAttribute)) {
      if (AA->isAlignas()) {
        // C++11 [dcl.align]p6:
        //   if any declaration of an entity has an alignment-specifier,
        //   every defining declaration of that entity shall specify an
        //   equivalent alignment.
        // C11 6.7.5/7:
        //   If the definition of an object does not have an alignment
        //   specifier, any other declaration of that object shall also
        //   have no alignment specifier.
        S.Diag(Def->getLocation(), diag::err_alignas_missing_on_definition)
          << AA;
        S.Diag(NewAttribute->getLocation(), diag::note_alignas_on_declaration)
          << AA;
        NewAttributes.erase(NewAttributes.begin() + I);
        --E;
        continue;
      }
    } else if (isa<LoaderUninitializedAttr>(NewAttribute)) {
      // If there is a C definition followed by a redeclaration with this
      // attribute then there are two different definitions. In C++, prefer the
      // standard diagnostics.
      if (!S.getLangOpts().CPlusPlus) {
        S.Diag(NewAttribute->getLocation(),
               diag::err_loader_uninitialized_redeclaration);
        S.Diag(Def->getLocation(), diag::note_previous_definition);
        NewAttributes.erase(NewAttributes.begin() + I);
        --E;
        continue;
      }
    } else if (isa<SelectAnyAttr>(NewAttribute) &&
               cast<VarDecl>(New)->isInline() &&
               !cast<VarDecl>(New)->isInlineSpecified()) {
      // Don't warn about applying selectany to implicitly inline variables.
      // Older compilers and language modes would require the use of selectany
      // to make such variables inline, and it would have no effect if we
      // honored it.
      ++I;
      continue;
    } else if (isa<OMPDeclareVariantAttr>(NewAttribute)) {
      // We allow to add OMP[Begin]DeclareVariantAttr to be added to
      // declarations after definitions.
      ++I;
      continue;
    }

    S.Diag(NewAttribute->getLocation(),
           diag::warn_attribute_precede_definition);
    S.Diag(Def->getLocation(), diag::note_previous_definition);
    NewAttributes.erase(NewAttributes.begin() + I);
    --E;
  }
}

static void diagnoseMissingConstinit(Sema &S, const VarDecl *InitDecl,
                                     const ConstInitAttr *CIAttr,
                                     bool AttrBeforeInit) {
  SourceLocation InsertLoc = InitDecl->getInnerLocStart();

  // Figure out a good way to write this specifier on the old declaration.
  // FIXME: We should just use the spelling of CIAttr, but we don't preserve
  // enough of the attribute list spelling information to extract that without
  // heroics.
  std::string SuitableSpelling;
  if (S.getLangOpts().CPlusPlus20)
    SuitableSpelling = std::string(
        S.PP.getLastMacroWithSpelling(InsertLoc, {tok::kw_constinit}));
  if (SuitableSpelling.empty() && S.getLangOpts().CPlusPlus11)
    SuitableSpelling = std::string(S.PP.getLastMacroWithSpelling(
        InsertLoc, {tok::l_square, tok::l_square,
                    S.PP.getIdentifierInfo("clang"), tok::coloncolon,
                    S.PP.getIdentifierInfo("require_constant_initialization"),
                    tok::r_square, tok::r_square}));
  if (SuitableSpelling.empty())
    SuitableSpelling = std::string(S.PP.getLastMacroWithSpelling(
        InsertLoc, {tok::kw___attribute, tok::l_paren, tok::r_paren,
                    S.PP.getIdentifierInfo("require_constant_initialization"),
                    tok::r_paren, tok::r_paren}));
  if (SuitableSpelling.empty() && S.getLangOpts().CPlusPlus20)
    SuitableSpelling = "constinit";
  if (SuitableSpelling.empty() && S.getLangOpts().CPlusPlus11)
    SuitableSpelling = "[[clang::require_constant_initialization]]";
  if (SuitableSpelling.empty())
    SuitableSpelling = "__attribute__((require_constant_initialization))";
  SuitableSpelling += " ";

  if (AttrBeforeInit) {
    // extern constinit int a;
    // int a = 0; // error (missing 'constinit'), accepted as extension
    assert(CIAttr->isConstinit() && "should not diagnose this for attribute");
    S.Diag(InitDecl->getLocation(), diag::ext_constinit_missing)
        << InitDecl << FixItHint::CreateInsertion(InsertLoc, SuitableSpelling);
    S.Diag(CIAttr->getLocation(), diag::note_constinit_specified_here);
  } else {
    // int a = 0;
    // constinit extern int a; // error (missing 'constinit')
    S.Diag(CIAttr->getLocation(),
           CIAttr->isConstinit() ? diag::err_constinit_added_too_late
                                 : diag::warn_require_const_init_added_too_late)
        << FixItHint::CreateRemoval(SourceRange(CIAttr->getLocation()));
    S.Diag(InitDecl->getLocation(), diag::note_constinit_missing_here)
        << CIAttr->isConstinit()
        << FixItHint::CreateInsertion(InsertLoc, SuitableSpelling);
  }
}

void Sema::mergeDeclAttributes(NamedDecl *New, Decl *Old,
                               AvailabilityMergeKind AMK) {
  if (UsedAttr *OldAttr = Old->getMostRecentDecl()->getAttr<UsedAttr>()) {
    UsedAttr *NewAttr = OldAttr->clone(Context);
    NewAttr->setInherited(true);
    New->addAttr(NewAttr);
  }
  if (RetainAttr *OldAttr = Old->getMostRecentDecl()->getAttr<RetainAttr>()) {
    RetainAttr *NewAttr = OldAttr->clone(Context);
    NewAttr->setInherited(true);
    New->addAttr(NewAttr);
  }

  if (!Old->hasAttrs() && !New->hasAttrs())
    return;

  // [dcl.constinit]p1:
  //   If the [constinit] specifier is applied to any declaration of a
  //   variable, it shall be applied to the initializing declaration.
  const auto *OldConstInit = Old->getAttr<ConstInitAttr>();
  const auto *NewConstInit = New->getAttr<ConstInitAttr>();
  if (bool(OldConstInit) != bool(NewConstInit)) {
    const auto *OldVD = cast<VarDecl>(Old);
    auto *NewVD = cast<VarDecl>(New);

    // Find the initializing declaration. Note that we might not have linked
    // the new declaration into the redeclaration chain yet.
    const VarDecl *InitDecl = OldVD->getInitializingDeclaration();
    if (!InitDecl &&
        (NewVD->hasInit() || NewVD->isThisDeclarationADefinition()))
      InitDecl = NewVD;

    if (InitDecl == NewVD) {
      // This is the initializing declaration. If it would inherit 'constinit',
      // that's ill-formed. (Note that we do not apply this to the attribute
      // form).
      if (OldConstInit && OldConstInit->isConstinit())
        diagnoseMissingConstinit(*this, NewVD, OldConstInit,
                                 /*AttrBeforeInit=*/true);
    } else if (NewConstInit) {
      // This is the first time we've been told that this declaration should
      // have a constant initializer. If we already saw the initializing
      // declaration, this is too late.
      if (InitDecl && InitDecl != NewVD) {
        diagnoseMissingConstinit(*this, InitDecl, NewConstInit,
                                 /*AttrBeforeInit=*/false);
        NewVD->dropAttr<ConstInitAttr>();
      }
    }
  }

  // Attributes declared post-definition are currently ignored.
  checkNewAttributesAfterDef(*this, New, Old);

  if (AsmLabelAttr *NewA = New->getAttr<AsmLabelAttr>()) {
    if (AsmLabelAttr *OldA = Old->getAttr<AsmLabelAttr>()) {
      if (!OldA->isEquivalent(NewA)) {
        // This redeclaration changes __asm__ label.
        Diag(New->getLocation(), diag::err_different_asm_label);
        Diag(OldA->getLocation(), diag::note_previous_declaration);
      }
    } else if (Old->isUsed()) {
      // This redeclaration adds an __asm__ label to a declaration that has
      // already been ODR-used.
      Diag(New->getLocation(), diag::err_late_asm_label_name)
        << isa<FunctionDecl>(Old) << New->getAttr<AsmLabelAttr>()->getRange();
    }
  }

  // Re-declaration cannot add abi_tag's.
  if (const auto *NewAbiTagAttr = New->getAttr<AbiTagAttr>()) {
    if (const auto *OldAbiTagAttr = Old->getAttr<AbiTagAttr>()) {
      for (const auto &NewTag : NewAbiTagAttr->tags()) {
        if (!llvm::is_contained(OldAbiTagAttr->tags(), NewTag)) {
          Diag(NewAbiTagAttr->getLocation(),
               diag::err_new_abi_tag_on_redeclaration)
              << NewTag;
          Diag(OldAbiTagAttr->getLocation(), diag::note_previous_declaration);
        }
      }
    } else {
      Diag(NewAbiTagAttr->getLocation(), diag::err_abi_tag_on_redeclaration);
      Diag(Old->getLocation(), diag::note_previous_declaration);
    }
  }

  // This redeclaration adds a section attribute.
  if (New->hasAttr<SectionAttr>() && !Old->hasAttr<SectionAttr>()) {
    if (auto *VD = dyn_cast<VarDecl>(New)) {
      if (VD->isThisDeclarationADefinition() == VarDecl::DeclarationOnly) {
        Diag(New->getLocation(), diag::warn_attribute_section_on_redeclaration);
        Diag(Old->getLocation(), diag::note_previous_declaration);
      }
    }
  }

  // Redeclaration adds code-seg attribute.
  const auto *NewCSA = New->getAttr<CodeSegAttr>();
  if (NewCSA && !Old->hasAttr<CodeSegAttr>() &&
      !NewCSA->isImplicit() && isa<CXXMethodDecl>(New)) {
    Diag(New->getLocation(), diag::warn_mismatched_section)
         << 0 /*codeseg*/;
    Diag(Old->getLocation(), diag::note_previous_declaration);
  }

  if (!Old->hasAttrs())
    return;

  bool foundAny = New->hasAttrs();

  // Ensure that any moving of objects within the allocated map is done before
  // we process them.
  if (!foundAny) New->setAttrs(AttrVec());

  for (auto *I : Old->specific_attrs<InheritableAttr>()) {
    // Ignore deprecated/unavailable/availability attributes if requested.
    AvailabilityMergeKind LocalAMK = AMK_None;
    if (isa<DeprecatedAttr>(I) ||
        isa<UnavailableAttr>(I) ||
        isa<AvailabilityAttr>(I)) {
      switch (AMK) {
      case AMK_None:
        continue;

      case AMK_Redeclaration:
      case AMK_Override:
      case AMK_ProtocolImplementation:
      case AMK_OptionalProtocolImplementation:
        LocalAMK = AMK;
        break;
      }
    }

    // Already handled.
    if (isa<UsedAttr>(I) || isa<RetainAttr>(I))
      continue;

    if (mergeDeclAttribute(*this, New, I, LocalAMK))
      foundAny = true;
  }

  if (mergeAlignedAttrs(*this, New, Old))
    foundAny = true;

  if (!foundAny) New->dropAttrs();
}

/// mergeParamDeclAttributes - Copy attributes from the old parameter
/// to the new one.
static void mergeParamDeclAttributes(ParmVarDecl *newDecl,
                                     const ParmVarDecl *oldDecl,
                                     Sema &S) {
  // C++11 [dcl.attr.depend]p2:
  //   The first declaration of a function shall specify the
  //   carries_dependency attribute for its declarator-id if any declaration
  //   of the function specifies the carries_dependency attribute.
  const CarriesDependencyAttr *CDA = newDecl->getAttr<CarriesDependencyAttr>();
  if (CDA && !oldDecl->hasAttr<CarriesDependencyAttr>()) {
    S.Diag(CDA->getLocation(),
           diag::err_carries_dependency_missing_on_first_decl) << 1/*Param*/;
    // Find the first declaration of the parameter.
    // FIXME: Should we build redeclaration chains for function parameters?
    const FunctionDecl *FirstFD =
      cast<FunctionDecl>(oldDecl->getDeclContext())->getFirstDecl();
    const ParmVarDecl *FirstVD =
      FirstFD->getParamDecl(oldDecl->getFunctionScopeIndex());
    S.Diag(FirstVD->getLocation(),
           diag::note_carries_dependency_missing_first_decl) << 1/*Param*/;
  }

  // HLSL parameter declarations for inout and out must match between
  // declarations. In HLSL inout and out are ambiguous at the call site, but
  // have different calling behavior, so you cannot overload a method based on a
  // difference between inout and out annotations.
  if (S.getLangOpts().HLSL) {
    const auto *NDAttr = newDecl->getAttr<HLSLParamModifierAttr>();
    const auto *ODAttr = oldDecl->getAttr<HLSLParamModifierAttr>();
    // We don't need to cover the case where one declaration doesn't have an
    // attribute. The only possible case there is if one declaration has an `in`
    // attribute and the other declaration has no attribute. This case is
    // allowed since parameters are `in` by default.
    if (NDAttr && ODAttr &&
        NDAttr->getSpellingListIndex() != ODAttr->getSpellingListIndex()) {
      S.Diag(newDecl->getLocation(), diag::err_hlsl_param_qualifier_mismatch)
          << NDAttr << newDecl;
      S.Diag(oldDecl->getLocation(), diag::note_previous_declaration_as)
          << ODAttr;
    }
  }

  if (!oldDecl->hasAttrs())
    return;

  bool foundAny = newDecl->hasAttrs();

  // Ensure that any moving of objects within the allocated map is
  // done before we process them.
  if (!foundAny) newDecl->setAttrs(AttrVec());

  for (const auto *I : oldDecl->specific_attrs<InheritableParamAttr>()) {
    if (!DeclHasAttr(newDecl, I)) {
      InheritableAttr *newAttr =
        cast<InheritableParamAttr>(I->clone(S.Context));
      newAttr->setInherited(true);
      newDecl->addAttr(newAttr);
      foundAny = true;
    }
  }

  if (!foundAny) newDecl->dropAttrs();
}

static bool EquivalentArrayTypes(QualType Old, QualType New,
                                 const ASTContext &Ctx) {

  auto NoSizeInfo = [&Ctx](QualType Ty) {
    if (Ty->isIncompleteArrayType() || Ty->isPointerType())
      return true;
    if (const auto *VAT = Ctx.getAsVariableArrayType(Ty))
      return VAT->getSizeModifier() == ArraySizeModifier::Star;
    return false;
  };

  // `type[]` is equivalent to `type *` and `type[*]`.
  if (NoSizeInfo(Old) && NoSizeInfo(New))
    return true;

  // Don't try to compare VLA sizes, unless one of them has the star modifier.
  if (Old->isVariableArrayType() && New->isVariableArrayType()) {
    const auto *OldVAT = Ctx.getAsVariableArrayType(Old);
    const auto *NewVAT = Ctx.getAsVariableArrayType(New);
    if ((OldVAT->getSizeModifier() == ArraySizeModifier::Star) ^
        (NewVAT->getSizeModifier() == ArraySizeModifier::Star))
      return false;
    return true;
  }

  // Only compare size, ignore Size modifiers and CVR.
  if (Old->isConstantArrayType() && New->isConstantArrayType()) {
    return Ctx.getAsConstantArrayType(Old)->getSize() ==
           Ctx.getAsConstantArrayType(New)->getSize();
  }

  // Don't try to compare dependent sized array
  if (Old->isDependentSizedArrayType() && New->isDependentSizedArrayType()) {
    return true;
  }

  return Old == New;
}

static void mergeParamDeclTypes(ParmVarDecl *NewParam,
                                const ParmVarDecl *OldParam,
                                Sema &S) {
  if (auto Oldnullability = OldParam->getType()->getNullability()) {
    if (auto Newnullability = NewParam->getType()->getNullability()) {
      if (*Oldnullability != *Newnullability) {
        S.Diag(NewParam->getLocation(), diag::warn_mismatched_nullability_attr)
          << DiagNullabilityKind(
               *Newnullability,
               ((NewParam->getObjCDeclQualifier() & Decl::OBJC_TQ_CSNullability)
                != 0))
          << DiagNullabilityKind(
               *Oldnullability,
               ((OldParam->getObjCDeclQualifier() & Decl::OBJC_TQ_CSNullability)
                != 0));
        S.Diag(OldParam->getLocation(), diag::note_previous_declaration);
      }
    } else {
      QualType NewT = NewParam->getType();
      NewT = S.Context.getAttributedType(
                         AttributedType::getNullabilityAttrKind(*Oldnullability),
                         NewT, NewT);
      NewParam->setType(NewT);
    }
  }
  const auto *OldParamDT = dyn_cast<DecayedType>(OldParam->getType());
  const auto *NewParamDT = dyn_cast<DecayedType>(NewParam->getType());
  if (OldParamDT && NewParamDT &&
      OldParamDT->getPointeeType() == NewParamDT->getPointeeType()) {
    QualType OldParamOT = OldParamDT->getOriginalType();
    QualType NewParamOT = NewParamDT->getOriginalType();
    if (!EquivalentArrayTypes(OldParamOT, NewParamOT, S.getASTContext())) {
      S.Diag(NewParam->getLocation(), diag::warn_inconsistent_array_form)
          << NewParam << NewParamOT;
      S.Diag(OldParam->getLocation(), diag::note_previous_declaration_as)
          << OldParamOT;
    }
  }
}

namespace {

/// Used in MergeFunctionDecl to keep track of function parameters in
/// C.
struct GNUCompatibleParamWarning {
  ParmVarDecl *OldParm;
  ParmVarDecl *NewParm;
  QualType PromotedType;
};

} // end anonymous namespace

// Determine whether the previous declaration was a definition, implicit
// declaration, or a declaration.
template <typename T>
static std::pair<diag::kind, SourceLocation>
getNoteDiagForInvalidRedeclaration(const T *Old, const T *New) {
  diag::kind PrevDiag;
  SourceLocation OldLocation = Old->getLocation();
  if (Old->isThisDeclarationADefinition())
    PrevDiag = diag::note_previous_definition;
  else if (Old->isImplicit()) {
    PrevDiag = diag::note_previous_implicit_declaration;
    if (const auto *FD = dyn_cast<FunctionDecl>(Old)) {
      if (FD->getBuiltinID())
        PrevDiag = diag::note_previous_builtin_declaration;
    }
    if (OldLocation.isInvalid())
      OldLocation = New->getLocation();
  } else
    PrevDiag = diag::note_previous_declaration;
  return std::make_pair(PrevDiag, OldLocation);
}

/// canRedefineFunction - checks if a function can be redefined. Currently,
/// only extern inline functions can be redefined, and even then only in
/// GNU89 mode.
static bool canRedefineFunction(const FunctionDecl *FD,
                                const LangOptions& LangOpts) {
  return ((FD->hasAttr<GNUInlineAttr>() || LangOpts.GNUInline) &&
          !LangOpts.CPlusPlus &&
          FD->isInlineSpecified() &&
          FD->getStorageClass() == SC_Extern);
}

const AttributedType *Sema::getCallingConvAttributedType(QualType T) const {
  const AttributedType *AT = T->getAs<AttributedType>();
  while (AT && !AT->isCallingConv())
    AT = AT->getModifiedType()->getAs<AttributedType>();
  return AT;
}

template <typename T>
static bool haveIncompatibleLanguageLinkages(const T *Old, const T *New) {
  const DeclContext *DC = Old->getDeclContext();
  if (DC->isRecord())
    return false;

  LanguageLinkage OldLinkage = Old->getLanguageLinkage();
  if (OldLinkage == CXXLanguageLinkage && New->isInExternCContext())
    return true;
  if (OldLinkage == CLanguageLinkage && New->isInExternCXXContext())
    return true;
  return false;
}

template<typename T> static bool isExternC(T *D) { return D->isExternC(); }
static bool isExternC(VarTemplateDecl *) { return false; }
static bool isExternC(FunctionTemplateDecl *) { return false; }

/// Check whether a redeclaration of an entity introduced by a
/// using-declaration is valid, given that we know it's not an overload
/// (nor a hidden tag declaration).
template<typename ExpectedDecl>
static bool checkUsingShadowRedecl(Sema &S, UsingShadowDecl *OldS,
                                   ExpectedDecl *New) {
  // C++11 [basic.scope.declarative]p4:
  //   Given a set of declarations in a single declarative region, each of
  //   which specifies the same unqualified name,
  //   -- they shall all refer to the same entity, or all refer to functions
  //      and function templates; or
  //   -- exactly one declaration shall declare a class name or enumeration
  //      name that is not a typedef name and the other declarations shall all
  //      refer to the same variable or enumerator, or all refer to functions
  //      and function templates; in this case the class name or enumeration
  //      name is hidden (3.3.10).

  // C++11 [namespace.udecl]p14:
  //   If a function declaration in namespace scope or block scope has the
  //   same name and the same parameter-type-list as a function introduced
  //   by a using-declaration, and the declarations do not declare the same
  //   function, the program is ill-formed.

  auto *Old = dyn_cast<ExpectedDecl>(OldS->getTargetDecl());
  if (Old &&
      !Old->getDeclContext()->getRedeclContext()->Equals(
          New->getDeclContext()->getRedeclContext()) &&
      !(isExternC(Old) && isExternC(New)))
    Old = nullptr;

  if (!Old) {
    S.Diag(New->getLocation(), diag::err_using_decl_conflict_reverse);
    S.Diag(OldS->getTargetDecl()->getLocation(), diag::note_using_decl_target);
    S.Diag(OldS->getIntroducer()->getLocation(), diag::note_using_decl) << 0;
    return true;
  }
  return false;
}

static bool hasIdenticalPassObjectSizeAttrs(const FunctionDecl *A,
                                            const FunctionDecl *B) {
  assert(A->getNumParams() == B->getNumParams());

  auto AttrEq = [](const ParmVarDecl *A, const ParmVarDecl *B) {
    const auto *AttrA = A->getAttr<PassObjectSizeAttr>();
    const auto *AttrB = B->getAttr<PassObjectSizeAttr>();
    if (AttrA == AttrB)
      return true;
    return AttrA && AttrB && AttrA->getType() == AttrB->getType() &&
           AttrA->isDynamic() == AttrB->isDynamic();
  };

  return std::equal(A->param_begin(), A->param_end(), B->param_begin(), AttrEq);
}

/// If necessary, adjust the semantic declaration context for a qualified
/// declaration to name the correct inline namespace within the qualifier.
static void adjustDeclContextForDeclaratorDecl(DeclaratorDecl *NewD,
                                               DeclaratorDecl *OldD) {
  // The only case where we need to update the DeclContext is when
  // redeclaration lookup for a qualified name finds a declaration
  // in an inline namespace within the context named by the qualifier:
  //
  //   inline namespace N { int f(); }
  //   int ::f(); // Sema DC needs adjusting from :: to N::.
  //
  // For unqualified declarations, the semantic context *can* change
  // along the redeclaration chain (for local extern declarations,
  // extern "C" declarations, and friend declarations in particular).
  if (!NewD->getQualifier())
    return;

  // NewD is probably already in the right context.
  auto *NamedDC = NewD->getDeclContext()->getRedeclContext();
  auto *SemaDC = OldD->getDeclContext()->getRedeclContext();
  if (NamedDC->Equals(SemaDC))
    return;

  assert((NamedDC->InEnclosingNamespaceSetOf(SemaDC) ||
          NewD->isInvalidDecl() || OldD->isInvalidDecl()) &&
         "unexpected context for redeclaration");

  auto *LexDC = NewD->getLexicalDeclContext();
  auto FixSemaDC = [=](NamedDecl *D) {
    if (!D)
      return;
    D->setDeclContext(SemaDC);
    D->setLexicalDeclContext(LexDC);
  };

  FixSemaDC(NewD);
  if (auto *FD = dyn_cast<FunctionDecl>(NewD))
    FixSemaDC(FD->getDescribedFunctionTemplate());
  else if (auto *VD = dyn_cast<VarDecl>(NewD))
    FixSemaDC(VD->getDescribedVarTemplate());
}

bool Sema::MergeFunctionDecl(FunctionDecl *New, NamedDecl *&OldD, Scope *S,
                             bool MergeTypeWithOld, bool NewDeclIsDefn) {
  // Verify the old decl was also a function.
  FunctionDecl *Old = OldD->getAsFunction();
  if (!Old) {
    if (UsingShadowDecl *Shadow = dyn_cast<UsingShadowDecl>(OldD)) {
      if (New->getFriendObjectKind()) {
        Diag(New->getLocation(), diag::err_using_decl_friend);
        Diag(Shadow->getTargetDecl()->getLocation(),
             diag::note_using_decl_target);
        Diag(Shadow->getIntroducer()->getLocation(), diag::note_using_decl)
            << 0;
        return true;
      }

      // Check whether the two declarations might declare the same function or
      // function template.
      if (FunctionTemplateDecl *NewTemplate =
              New->getDescribedFunctionTemplate()) {
        if (checkUsingShadowRedecl<FunctionTemplateDecl>(*this, Shadow,
                                                         NewTemplate))
          return true;
        OldD = Old = cast<FunctionTemplateDecl>(Shadow->getTargetDecl())
                         ->getAsFunction();
      } else {
        if (checkUsingShadowRedecl<FunctionDecl>(*this, Shadow, New))
          return true;
        OldD = Old = cast<FunctionDecl>(Shadow->getTargetDecl());
      }
    } else {
      Diag(New->getLocation(), diag::err_redefinition_different_kind)
        << New->getDeclName();
      notePreviousDefinition(OldD, New->getLocation());
      return true;
    }
  }

  // If the old declaration was found in an inline namespace and the new
  // declaration was qualified, update the DeclContext to match.
  adjustDeclContextForDeclaratorDecl(New, Old);

  // If the old declaration is invalid, just give up here.
  if (Old->isInvalidDecl())
    return true;

  // Disallow redeclaration of some builtins.
  if (!getASTContext().canBuiltinBeRedeclared(Old)) {
    Diag(New->getLocation(), diag::err_builtin_redeclare) << Old->getDeclName();
    Diag(Old->getLocation(), diag::note_previous_builtin_declaration)
        << Old << Old->getType();
    return true;
  }

  diag::kind PrevDiag;
  SourceLocation OldLocation;
  std::tie(PrevDiag, OldLocation) =
      getNoteDiagForInvalidRedeclaration(Old, New);

  // Don't complain about this if we're in GNU89 mode and the old function
  // is an extern inline function.
  // Don't complain about specializations. They are not supposed to have
  // storage classes.
  if (!isa<CXXMethodDecl>(New) && !isa<CXXMethodDecl>(Old) &&
      New->getStorageClass() == SC_Static &&
      Old->hasExternalFormalLinkage() &&
      !New->getTemplateSpecializationInfo() &&
      !canRedefineFunction(Old, getLangOpts())) {
    if (getLangOpts().MicrosoftExt) {
      Diag(New->getLocation(), diag::ext_static_non_static) << New;
      Diag(OldLocation, PrevDiag) << Old << Old->getType();
    } else {
      Diag(New->getLocation(), diag::err_static_non_static) << New;
      Diag(OldLocation, PrevDiag) << Old << Old->getType();
      return true;
    }
  }

  if (const auto *ILA = New->getAttr<InternalLinkageAttr>())
    if (!Old->hasAttr<InternalLinkageAttr>()) {
      Diag(New->getLocation(), diag::err_attribute_missing_on_first_decl)
          << ILA;
      Diag(Old->getLocation(), diag::note_previous_declaration);
      New->dropAttr<InternalLinkageAttr>();
    }

  if (auto *EA = New->getAttr<ErrorAttr>()) {
    if (!Old->hasAttr<ErrorAttr>()) {
      Diag(EA->getLocation(), diag::err_attribute_missing_on_first_decl) << EA;
      Diag(Old->getLocation(), diag::note_previous_declaration);
      New->dropAttr<ErrorAttr>();
    }
  }

  if (CheckRedeclarationInModule(New, Old))
    return true;

  if (!getLangOpts().CPlusPlus) {
    bool OldOvl = Old->hasAttr<OverloadableAttr>();
    if (OldOvl != New->hasAttr<OverloadableAttr>() && !Old->isImplicit()) {
      Diag(New->getLocation(), diag::err_attribute_overloadable_mismatch)
        << New << OldOvl;

      // Try our best to find a decl that actually has the overloadable
      // attribute for the note. In most cases (e.g. programs with only one
      // broken declaration/definition), this won't matter.
      //
      // FIXME: We could do this if we juggled some extra state in
      // OverloadableAttr, rather than just removing it.
      const Decl *DiagOld = Old;
      if (OldOvl) {
        auto OldIter = llvm::find_if(Old->redecls(), [](const Decl *D) {
          const auto *A = D->getAttr<OverloadableAttr>();
          return A && !A->isImplicit();
        });
        // If we've implicitly added *all* of the overloadable attrs to this
        // chain, emitting a "previous redecl" note is pointless.
        DiagOld = OldIter == Old->redecls_end() ? nullptr : *OldIter;
      }

      if (DiagOld)
        Diag(DiagOld->getLocation(),
             diag::note_attribute_overloadable_prev_overload)
          << OldOvl;

      if (OldOvl)
        New->addAttr(OverloadableAttr::CreateImplicit(Context));
      else
        New->dropAttr<OverloadableAttr>();
    }
  }

  // It is not permitted to redeclare an SME function with different SME
  // attributes.
  if (IsInvalidSMECallConversion(Old->getType(), New->getType())) {
    Diag(New->getLocation(), diag::err_sme_attr_mismatch)
        << New->getType() << Old->getType();
    Diag(OldLocation, diag::note_previous_declaration);
    return true;
  }

  // If a function is first declared with a calling convention, but is later
  // declared or defined without one, all following decls assume the calling
  // convention of the first.
  //
  // It's OK if a function is first declared without a calling convention,
  // but is later declared or defined with the default calling convention.
  //
  // To test if either decl has an explicit calling convention, we look for
  // AttributedType sugar nodes on the type as written.  If they are missing or
  // were canonicalized away, we assume the calling convention was implicit.
  //
  // Note also that we DO NOT return at this point, because we still have
  // other tests to run.
  QualType OldQType = Context.getCanonicalType(Old->getType());
  QualType NewQType = Context.getCanonicalType(New->getType());
  const FunctionType *OldType = cast<FunctionType>(OldQType);
  const FunctionType *NewType = cast<FunctionType>(NewQType);
  FunctionType::ExtInfo OldTypeInfo = OldType->getExtInfo();
  FunctionType::ExtInfo NewTypeInfo = NewType->getExtInfo();
  bool RequiresAdjustment = false;

  if (OldTypeInfo.getCC() != NewTypeInfo.getCC()) {
    FunctionDecl *First = Old->getFirstDecl();
    const FunctionType *FT =
        First->getType().getCanonicalType()->castAs<FunctionType>();
    FunctionType::ExtInfo FI = FT->getExtInfo();
    bool NewCCExplicit = getCallingConvAttributedType(New->getType());
    if (!NewCCExplicit) {
      // Inherit the CC from the previous declaration if it was specified
      // there but not here.
      NewTypeInfo = NewTypeInfo.withCallingConv(OldTypeInfo.getCC());
      RequiresAdjustment = true;
    } else if (Old->getBuiltinID()) {
      // Builtin attribute isn't propagated to the new one yet at this point,
      // so we check if the old one is a builtin.

      // Calling Conventions on a Builtin aren't really useful and setting a
      // default calling convention and cdecl'ing some builtin redeclarations is
      // common, so warn and ignore the calling convention on the redeclaration.
      Diag(New->getLocation(), diag::warn_cconv_unsupported)
          << FunctionType::getNameForCallConv(NewTypeInfo.getCC())
          << (int)CallingConventionIgnoredReason::BuiltinFunction;
      NewTypeInfo = NewTypeInfo.withCallingConv(OldTypeInfo.getCC());
      RequiresAdjustment = true;
    } else {
      // Calling conventions aren't compatible, so complain.
      bool FirstCCExplicit = getCallingConvAttributedType(First->getType());
      Diag(New->getLocation(), diag::err_cconv_change)
        << FunctionType::getNameForCallConv(NewTypeInfo.getCC())
        << !FirstCCExplicit
        << (!FirstCCExplicit ? "" :
            FunctionType::getNameForCallConv(FI.getCC()));

      // Put the note on the first decl, since it is the one that matters.
      Diag(First->getLocation(), diag::note_previous_declaration);
      return true;
    }
  }

  // FIXME: diagnose the other way around?
  if (OldTypeInfo.getNoReturn() && !NewTypeInfo.getNoReturn()) {
    NewTypeInfo = NewTypeInfo.withNoReturn(true);
    RequiresAdjustment = true;
  }

  // Merge regparm attribute.
  if (OldTypeInfo.getHasRegParm() != NewTypeInfo.getHasRegParm() ||
      OldTypeInfo.getRegParm() != NewTypeInfo.getRegParm()) {
    if (NewTypeInfo.getHasRegParm()) {
      Diag(New->getLocation(), diag::err_regparm_mismatch)
        << NewType->getRegParmType()
        << OldType->getRegParmType();
      Diag(OldLocation, diag::note_previous_declaration);
      return true;
    }

    NewTypeInfo = NewTypeInfo.withRegParm(OldTypeInfo.getRegParm());
    RequiresAdjustment = true;
  }

  // Merge ns_returns_retained attribute.
  if (OldTypeInfo.getProducesResult() != NewTypeInfo.getProducesResult()) {
    if (NewTypeInfo.getProducesResult()) {
      Diag(New->getLocation(), diag::err_function_attribute_mismatch)
          << "'ns_returns_retained'";
      Diag(OldLocation, diag::note_previous_declaration);
      return true;
    }

    NewTypeInfo = NewTypeInfo.withProducesResult(true);
    RequiresAdjustment = true;
  }

  if (OldTypeInfo.getNoCallerSavedRegs() !=
      NewTypeInfo.getNoCallerSavedRegs()) {
    if (NewTypeInfo.getNoCallerSavedRegs()) {
      AnyX86NoCallerSavedRegistersAttr *Attr =
        New->getAttr<AnyX86NoCallerSavedRegistersAttr>();
      Diag(New->getLocation(), diag::err_function_attribute_mismatch) << Attr;
      Diag(OldLocation, diag::note_previous_declaration);
      return true;
    }

    NewTypeInfo = NewTypeInfo.withNoCallerSavedRegs(true);
    RequiresAdjustment = true;
  }

  if (RequiresAdjustment) {
    const FunctionType *AdjustedType = New->getType()->getAs<FunctionType>();
    AdjustedType = Context.adjustFunctionType(AdjustedType, NewTypeInfo);
    New->setType(QualType(AdjustedType, 0));
    NewQType = Context.getCanonicalType(New->getType());
  }

  // If this redeclaration makes the function inline, we may need to add it to
  // UndefinedButUsed.
  if (!Old->isInlined() && New->isInlined() &&
      !New->hasAttr<GNUInlineAttr>() &&
      !getLangOpts().GNUInline &&
      Old->isUsed(false) &&
      !Old->isDefined() && !New->isThisDeclarationADefinition())
    UndefinedButUsed.insert(std::make_pair(Old->getCanonicalDecl(),
                                           SourceLocation()));

  // If this redeclaration makes it newly gnu_inline, we don't want to warn
  // about it.
  if (New->hasAttr<GNUInlineAttr>() &&
      Old->isInlined() && !Old->hasAttr<GNUInlineAttr>()) {
    UndefinedButUsed.erase(Old->getCanonicalDecl());
  }

  // If pass_object_size params don't match up perfectly, this isn't a valid
  // redeclaration.
  if (Old->getNumParams() > 0 && Old->getNumParams() == New->getNumParams() &&
      !hasIdenticalPassObjectSizeAttrs(Old, New)) {
    Diag(New->getLocation(), diag::err_different_pass_object_size_params)
        << New->getDeclName();
    Diag(OldLocation, PrevDiag) << Old << Old->getType();
    return true;
  }

  QualType OldQTypeForComparison = OldQType;
  if (Context.hasAnyFunctionEffects()) {
    const auto OldFX = Old->getFunctionEffects();
    const auto NewFX = New->getFunctionEffects();
    if (OldFX != NewFX) {
      const auto Diffs = FunctionEffectDifferences(OldFX, NewFX);
      for (const auto &Diff : Diffs) {
        if (Diff.shouldDiagnoseRedeclaration(*Old, OldFX, *New, NewFX)) {
          Diag(New->getLocation(),
               diag::warn_mismatched_func_effect_redeclaration)
              << Diff.effectName();
          Diag(Old->getLocation(), diag::note_previous_declaration);
        }
      }
      // Following a warning, we could skip merging effects from the previous
      // declaration, but that would trigger an additional "conflicting types"
      // error.
      if (const auto *NewFPT = NewQType->getAs<FunctionProtoType>()) {
        FunctionEffectSet::Conflicts MergeErrs;
        FunctionEffectSet MergedFX =
            FunctionEffectSet::getUnion(OldFX, NewFX, MergeErrs);
        if (!MergeErrs.empty())
          diagnoseFunctionEffectMergeConflicts(MergeErrs, New->getLocation(),
                                               Old->getLocation());

        FunctionProtoType::ExtProtoInfo EPI = NewFPT->getExtProtoInfo();
        EPI.FunctionEffects = FunctionEffectsRef(MergedFX);
        QualType ModQT = Context.getFunctionType(NewFPT->getReturnType(),
                                                 NewFPT->getParamTypes(), EPI);

        New->setType(ModQT);
        NewQType = New->getType();

        // Revise OldQTForComparison to include the merged effects,
        // so as not to fail due to differences later.
        if (const auto *OldFPT = OldQType->getAs<FunctionProtoType>()) {
          EPI = OldFPT->getExtProtoInfo();
          EPI.FunctionEffects = FunctionEffectsRef(MergedFX);
          OldQTypeForComparison = Context.getFunctionType(
              OldFPT->getReturnType(), OldFPT->getParamTypes(), EPI);
        }
      }
    }
  }

  if (getLangOpts().CPlusPlus) {
    OldQType = Context.getCanonicalType(Old->getType());
    NewQType = Context.getCanonicalType(New->getType());

    // Go back to the type source info to compare the declared return types,
    // per C++1y [dcl.type.auto]p13:
    //   Redeclarations or specializations of a function or function template
    //   with a declared return type that uses a placeholder type shall also
    //   use that placeholder, not a deduced type.
    QualType OldDeclaredReturnType = Old->getDeclaredReturnType();
    QualType NewDeclaredReturnType = New->getDeclaredReturnType();
    if (!Context.hasSameType(OldDeclaredReturnType, NewDeclaredReturnType) &&
        canFullyTypeCheckRedeclaration(New, Old, NewDeclaredReturnType,
                                       OldDeclaredReturnType)) {
      QualType ResQT;
      if (NewDeclaredReturnType->isObjCObjectPointerType() &&
          OldDeclaredReturnType->isObjCObjectPointerType())
        // FIXME: This does the wrong thing for a deduced return type.
        ResQT = Context.mergeObjCGCQualifiers(NewQType, OldQType);
      if (ResQT.isNull()) {
        if (New->isCXXClassMember() && New->isOutOfLine())
          Diag(New->getLocation(), diag::err_member_def_does_not_match_ret_type)
              << New << New->getReturnTypeSourceRange();
        else
          Diag(New->getLocation(), diag::err_ovl_diff_return_type)
              << New->getReturnTypeSourceRange();
        Diag(OldLocation, PrevDiag) << Old << Old->getType()
                                    << Old->getReturnTypeSourceRange();
        return true;
      }
      else
        NewQType = ResQT;
    }

    QualType OldReturnType = OldType->getReturnType();
    QualType NewReturnType = cast<FunctionType>(NewQType)->getReturnType();
    if (OldReturnType != NewReturnType) {
      // If this function has a deduced return type and has already been
      // defined, copy the deduced value from the old declaration.
      AutoType *OldAT = Old->getReturnType()->getContainedAutoType();
      if (OldAT && OldAT->isDeduced()) {
        QualType DT = OldAT->getDeducedType();
        if (DT.isNull()) {
          New->setType(SubstAutoTypeDependent(New->getType()));
          NewQType = Context.getCanonicalType(SubstAutoTypeDependent(NewQType));
        } else {
          New->setType(SubstAutoType(New->getType(), DT));
          NewQType = Context.getCanonicalType(SubstAutoType(NewQType, DT));
        }
      }
    }

    const CXXMethodDecl *OldMethod = dyn_cast<CXXMethodDecl>(Old);
    CXXMethodDecl *NewMethod = dyn_cast<CXXMethodDecl>(New);
    if (OldMethod && NewMethod) {
      // Preserve triviality.
      NewMethod->setTrivial(OldMethod->isTrivial());

      // MSVC allows explicit template specialization at class scope:
      // 2 CXXMethodDecls referring to the same function will be injected.
      // We don't want a redeclaration error.
      bool IsClassScopeExplicitSpecialization =
                              OldMethod->isFunctionTemplateSpecialization() &&
                              NewMethod->isFunctionTemplateSpecialization();
      bool isFriend = NewMethod->getFriendObjectKind();

      if (!isFriend && NewMethod->getLexicalDeclContext()->isRecord() &&
          !IsClassScopeExplicitSpecialization) {
        //    -- Member function declarations with the same name and the
        //       same parameter types cannot be overloaded if any of them
        //       is a static member function declaration.
        if (OldMethod->isStatic() != NewMethod->isStatic()) {
          Diag(New->getLocation(), diag::err_ovl_static_nonstatic_member);
          Diag(OldLocation, PrevDiag) << Old << Old->getType();
          return true;
        }

        // C++ [class.mem]p1:
        //   [...] A member shall not be declared twice in the
        //   member-specification, except that a nested class or member
        //   class template can be declared and then later defined.
        if (!inTemplateInstantiation()) {
          unsigned NewDiag;
          if (isa<CXXConstructorDecl>(OldMethod))
            NewDiag = diag::err_constructor_redeclared;
          else if (isa<CXXDestructorDecl>(NewMethod))
            NewDiag = diag::err_destructor_redeclared;
          else if (isa<CXXConversionDecl>(NewMethod))
            NewDiag = diag::err_conv_function_redeclared;
          else
            NewDiag = diag::err_member_redeclared;

          Diag(New->getLocation(), NewDiag);
        } else {
          Diag(New->getLocation(), diag::err_member_redeclared_in_instantiation)
            << New << New->getType();
        }
        Diag(OldLocation, PrevDiag) << Old << Old->getType();
        return true;

      // Complain if this is an explicit declaration of a special
      // member that was initially declared implicitly.
      //
      // As an exception, it's okay to befriend such methods in order
      // to permit the implicit constructor/destructor/operator calls.
      } else if (OldMethod->isImplicit()) {
        if (isFriend) {
          NewMethod->setImplicit();
        } else {
          Diag(NewMethod->getLocation(),
               diag::err_definition_of_implicitly_declared_member)
              << New << llvm::to_underlying(getSpecialMember(OldMethod));
          return true;
        }
      } else if (OldMethod->getFirstDecl()->isExplicitlyDefaulted() && !isFriend) {
        Diag(NewMethod->getLocation(),
             diag::err_definition_of_explicitly_defaulted_member)
            << llvm::to_underlying(getSpecialMember(OldMethod));
        return true;
      }
    }

    // C++1z [over.load]p2
    //   Certain function declarations cannot be overloaded:
    //     -- Function declarations that differ only in the return type,
    //        the exception specification, or both cannot be overloaded.

    // Check the exception specifications match. This may recompute the type of
    // both Old and New if it resolved exception specifications, so grab the
    // types again after this. Because this updates the type, we do this before
    // any of the other checks below, which may update the "de facto" NewQType
    // but do not necessarily update the type of New.
    if (CheckEquivalentExceptionSpec(Old, New))
      return true;

    // C++11 [dcl.attr.noreturn]p1:
    //   The first declaration of a function shall specify the noreturn
    //   attribute if any declaration of that function specifies the noreturn
    //   attribute.
    if (const auto *NRA = New->getAttr<CXX11NoReturnAttr>())
      if (!Old->hasAttr<CXX11NoReturnAttr>()) {
        Diag(NRA->getLocation(), diag::err_attribute_missing_on_first_decl)
            << NRA;
        Diag(Old->getLocation(), diag::note_previous_declaration);
      }

    // C++11 [dcl.attr.depend]p2:
    //   The first declaration of a function shall specify the
    //   carries_dependency attribute for its declarator-id if any declaration
    //   of the function specifies the carries_dependency attribute.
    const CarriesDependencyAttr *CDA = New->getAttr<CarriesDependencyAttr>();
    if (CDA && !Old->hasAttr<CarriesDependencyAttr>()) {
      Diag(CDA->getLocation(),
           diag::err_carries_dependency_missing_on_first_decl) << 0/*Function*/;
      Diag(Old->getFirstDecl()->getLocation(),
           diag::note_carries_dependency_missing_first_decl) << 0/*Function*/;
    }

    // (C++98 8.3.5p3):
    //   All declarations for a function shall agree exactly in both the
    //   return type and the parameter-type-list.
    // We also want to respect all the extended bits except noreturn.

    // noreturn should now match unless the old type info didn't have it.
    if (!OldTypeInfo.getNoReturn() && NewTypeInfo.getNoReturn()) {
      auto *OldType = OldQTypeForComparison->castAs<FunctionProtoType>();
      const FunctionType *OldTypeForComparison
        = Context.adjustFunctionType(OldType, OldTypeInfo.withNoReturn(true));
      OldQTypeForComparison = QualType(OldTypeForComparison, 0);
      assert(OldQTypeForComparison.isCanonical());
    }

    if (haveIncompatibleLanguageLinkages(Old, New)) {
      // As a special case, retain the language linkage from previous
      // declarations of a friend function as an extension.
      //
      // This liberal interpretation of C++ [class.friend]p3 matches GCC/MSVC
      // and is useful because there's otherwise no way to specify language
      // linkage within class scope.
      //
      // Check cautiously as the friend object kind isn't yet complete.
      if (New->getFriendObjectKind() != Decl::FOK_None) {
        Diag(New->getLocation(), diag::ext_retained_language_linkage) << New;
        Diag(OldLocation, PrevDiag);
      } else {
        Diag(New->getLocation(), diag::err_different_language_linkage) << New;
        Diag(OldLocation, PrevDiag);
        return true;
      }
    }

    // If the function types are compatible, merge the declarations. Ignore the
    // exception specifier because it was already checked above in
    // CheckEquivalentExceptionSpec, and we don't want follow-on diagnostics
    // about incompatible types under -fms-compatibility.
    if (Context.hasSameFunctionTypeIgnoringExceptionSpec(OldQTypeForComparison,
                                                         NewQType))
      return MergeCompatibleFunctionDecls(New, Old, S, MergeTypeWithOld);

    // If the types are imprecise (due to dependent constructs in friends or
    // local extern declarations), it's OK if they differ. We'll check again
    // during instantiation.
    if (!canFullyTypeCheckRedeclaration(New, Old, NewQType, OldQType))
      return false;

    // Fall through for conflicting redeclarations and redefinitions.
  }

  // C: Function types need to be compatible, not identical. This handles
  // duplicate function decls like "void f(int); void f(enum X);" properly.
  if (!getLangOpts().CPlusPlus) {
    // C99 6.7.5.3p15: ...If one type has a parameter type list and the other
    // type is specified by a function definition that contains a (possibly
    // empty) identifier list, both shall agree in the number of parameters
    // and the type of each parameter shall be compatible with the type that
    // results from the application of default argument promotions to the
    // type of the corresponding identifier. ...
    // This cannot be handled by ASTContext::typesAreCompatible() because that
    // doesn't know whether the function type is for a definition or not when
    // eventually calling ASTContext::mergeFunctionTypes(). The only situation
    // we need to cover here is that the number of arguments agree as the
    // default argument promotion rules were already checked by
    // ASTContext::typesAreCompatible().
    if (Old->hasPrototype() && !New->hasWrittenPrototype() && NewDeclIsDefn &&
        Old->getNumParams() != New->getNumParams() && !Old->isImplicit()) {
      if (Old->hasInheritedPrototype())
        Old = Old->getCanonicalDecl();
      Diag(New->getLocation(), diag::err_conflicting_types) << New;
      Diag(Old->getLocation(), PrevDiag) << Old << Old->getType();
      return true;
    }

    // If we are merging two functions where only one of them has a prototype,
    // we may have enough information to decide to issue a diagnostic that the
    // function without a prototype will change behavior in C23. This handles
    // cases like:
    //   void i(); void i(int j);
    //   void i(int j); void i();
    //   void i(); void i(int j) {}
    // See ActOnFinishFunctionBody() for other cases of the behavior change
    // diagnostic. See GetFullTypeForDeclarator() for handling of a function
    // type without a prototype.
    if (New->hasWrittenPrototype() != Old->hasWrittenPrototype() &&
        !New->isImplicit() && !Old->isImplicit()) {
      const FunctionDecl *WithProto, *WithoutProto;
      if (New->hasWrittenPrototype()) {
        WithProto = New;
        WithoutProto = Old;
      } else {
        WithProto = Old;
        WithoutProto = New;
      }

      if (WithProto->getNumParams() != 0) {
        if (WithoutProto->getBuiltinID() == 0 && !WithoutProto->isImplicit()) {
          // The one without the prototype will be changing behavior in C23, so
          // warn about that one so long as it's a user-visible declaration.
          bool IsWithoutProtoADef = false, IsWithProtoADef = false;
          if (WithoutProto == New)
            IsWithoutProtoADef = NewDeclIsDefn;
          else
            IsWithProtoADef = NewDeclIsDefn;
          Diag(WithoutProto->getLocation(),
               diag::warn_non_prototype_changes_behavior)
              << IsWithoutProtoADef << (WithoutProto->getNumParams() ? 0 : 1)
              << (WithoutProto == Old) << IsWithProtoADef;

          // The reason the one without the prototype will be changing behavior
          // is because of the one with the prototype, so note that so long as
          // it's a user-visible declaration. There is one exception to this:
          // when the new declaration is a definition without a prototype, the
          // old declaration with a prototype is not the cause of the issue,
          // and that does not need to be noted because the one with a
          // prototype will not change behavior in C23.
          if (WithProto->getBuiltinID() == 0 && !WithProto->isImplicit() &&
              !IsWithoutProtoADef)
            Diag(WithProto->getLocation(), diag::note_conflicting_prototype);
        }
      }
    }

    if (Context.typesAreCompatible(OldQType, NewQType)) {
      const FunctionType *OldFuncType = OldQType->getAs<FunctionType>();
      const FunctionType *NewFuncType = NewQType->getAs<FunctionType>();
      const FunctionProtoType *OldProto = nullptr;
      if (MergeTypeWithOld && isa<FunctionNoProtoType>(NewFuncType) &&
          (OldProto = dyn_cast<FunctionProtoType>(OldFuncType))) {
        // The old declaration provided a function prototype, but the
        // new declaration does not. Merge in the prototype.
        assert(!OldProto->hasExceptionSpec() && "Exception spec in C");
        NewQType = Context.getFunctionType(NewFuncType->getReturnType(),
                                           OldProto->getParamTypes(),
                                           OldProto->getExtProtoInfo());
        New->setType(NewQType);
        New->setHasInheritedPrototype();

        // Synthesize parameters with the same types.
        SmallVector<ParmVarDecl *, 16> Params;
        for (const auto &ParamType : OldProto->param_types()) {
          ParmVarDecl *Param = ParmVarDecl::Create(
              Context, New, SourceLocation(), SourceLocation(), nullptr,
              ParamType, /*TInfo=*/nullptr, SC_None, nullptr);
          Param->setScopeInfo(0, Params.size());
          Param->setImplicit();
          Params.push_back(Param);
        }

        New->setParams(Params);
      }

      return MergeCompatibleFunctionDecls(New, Old, S, MergeTypeWithOld);
    }
  }

  // Check if the function types are compatible when pointer size address
  // spaces are ignored.
  if (Context.hasSameFunctionTypeIgnoringPtrSizes(OldQType, NewQType))
    return false;

  // GNU C permits a K&R definition to follow a prototype declaration
  // if the declared types of the parameters in the K&R definition
  // match the types in the prototype declaration, even when the
  // promoted types of the parameters from the K&R definition differ
  // from the types in the prototype. GCC then keeps the types from
  // the prototype.
  //
  // If a variadic prototype is followed by a non-variadic K&R definition,
  // the K&R definition becomes variadic.  This is sort of an edge case, but
  // it's legal per the standard depending on how you read C99 6.7.5.3p15 and
  // C99 6.9.1p8.
  if (!getLangOpts().CPlusPlus &&
      Old->hasPrototype() && !New->hasPrototype() &&
      New->getType()->getAs<FunctionProtoType>() &&
      Old->getNumParams() == New->getNumParams()) {
    SmallVector<QualType, 16> ArgTypes;
    SmallVector<GNUCompatibleParamWarning, 16> Warnings;
    const FunctionProtoType *OldProto
      = Old->getType()->getAs<FunctionProtoType>();
    const FunctionProtoType *NewProto
      = New->getType()->getAs<FunctionProtoType>();

    // Determine whether this is the GNU C extension.
    QualType MergedReturn = Context.mergeTypes(OldProto->getReturnType(),
                                               NewProto->getReturnType());
    bool LooseCompatible = !MergedReturn.isNull();
    for (unsigned Idx = 0, End = Old->getNumParams();
         LooseCompatible && Idx != End; ++Idx) {
      ParmVarDecl *OldParm = Old->getParamDecl(Idx);
      ParmVarDecl *NewParm = New->getParamDecl(Idx);
      if (Context.typesAreCompatible(OldParm->getType(),
                                     NewProto->getParamType(Idx))) {
        ArgTypes.push_back(NewParm->getType());
      } else if (Context.typesAreCompatible(OldParm->getType(),
                                            NewParm->getType(),
                                            /*CompareUnqualified=*/true)) {
        GNUCompatibleParamWarning Warn = { OldParm, NewParm,
                                           NewProto->getParamType(Idx) };
        Warnings.push_back(Warn);
        ArgTypes.push_back(NewParm->getType());
      } else
        LooseCompatible = false;
    }

    if (LooseCompatible) {
      for (unsigned Warn = 0; Warn < Warnings.size(); ++Warn) {
        Diag(Warnings[Warn].NewParm->getLocation(),
             diag::ext_param_promoted_not_compatible_with_prototype)
          << Warnings[Warn].PromotedType
          << Warnings[Warn].OldParm->getType();
        if (Warnings[Warn].OldParm->getLocation().isValid())
          Diag(Warnings[Warn].OldParm->getLocation(),
               diag::note_previous_declaration);
      }

      if (MergeTypeWithOld)
        New->setType(Context.getFunctionType(MergedReturn, ArgTypes,
                                             OldProto->getExtProtoInfo()));
      return MergeCompatibleFunctionDecls(New, Old, S, MergeTypeWithOld);
    }

    // Fall through to diagnose conflicting types.
  }

  // A function that has already been declared has been redeclared or
  // defined with a different type; show an appropriate diagnostic.

  // If the previous declaration was an implicitly-generated builtin
  // declaration, then at the very least we should use a specialized note.
  unsigned BuiltinID;
  if (Old->isImplicit() && (BuiltinID = Old->getBuiltinID())) {
    // If it's actually a library-defined builtin function like 'malloc'
    // or 'printf', just warn about the incompatible redeclaration.
    if (Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID)) {
      Diag(New->getLocation(), diag::warn_redecl_library_builtin) << New;
      Diag(OldLocation, diag::note_previous_builtin_declaration)
        << Old << Old->getType();
      return false;
    }

    PrevDiag = diag::note_previous_builtin_declaration;
  }

  Diag(New->getLocation(), diag::err_conflicting_types) << New->getDeclName();
  Diag(OldLocation, PrevDiag) << Old << Old->getType();
  return true;
}

bool Sema::MergeCompatibleFunctionDecls(FunctionDecl *New, FunctionDecl *Old,
                                        Scope *S, bool MergeTypeWithOld) {
  // Merge the attributes
  mergeDeclAttributes(New, Old);

  // Merge "pure" flag.
  if (Old->isPureVirtual())
    New->setIsPureVirtual();

  // Merge "used" flag.
  if (Old->getMostRecentDecl()->isUsed(false))
    New->setIsUsed();

  // Merge attributes from the parameters.  These can mismatch with K&R
  // declarations.
  if (New->getNumParams() == Old->getNumParams())
      for (unsigned i = 0, e = New->getNumParams(); i != e; ++i) {
        ParmVarDecl *NewParam = New->getParamDecl(i);
        ParmVarDecl *OldParam = Old->getParamDecl(i);
        mergeParamDeclAttributes(NewParam, OldParam, *this);
        mergeParamDeclTypes(NewParam, OldParam, *this);
      }

  if (getLangOpts().CPlusPlus)
    return MergeCXXFunctionDecl(New, Old, S);

  // Merge the function types so the we get the composite types for the return
  // and argument types. Per C11 6.2.7/4, only update the type if the old decl
  // was visible.
  QualType Merged = Context.mergeTypes(Old->getType(), New->getType());
  if (!Merged.isNull() && MergeTypeWithOld)
    New->setType(Merged);

  return false;
}

void Sema::mergeObjCMethodDecls(ObjCMethodDecl *newMethod,
                                ObjCMethodDecl *oldMethod) {
  // Merge the attributes, including deprecated/unavailable
  AvailabilityMergeKind MergeKind =
      isa<ObjCProtocolDecl>(oldMethod->getDeclContext())
          ? (oldMethod->isOptional() ? AMK_OptionalProtocolImplementation
                                     : AMK_ProtocolImplementation)
          : isa<ObjCImplDecl>(newMethod->getDeclContext()) ? AMK_Redeclaration
                                                           : AMK_Override;

  mergeDeclAttributes(newMethod, oldMethod, MergeKind);

  // Merge attributes from the parameters.
  ObjCMethodDecl::param_const_iterator oi = oldMethod->param_begin(),
                                       oe = oldMethod->param_end();
  for (ObjCMethodDecl::param_iterator
         ni = newMethod->param_begin(), ne = newMethod->param_end();
       ni != ne && oi != oe; ++ni, ++oi)
    mergeParamDeclAttributes(*ni, *oi, *this);

  ObjC().CheckObjCMethodOverride(newMethod, oldMethod);
}

static void diagnoseVarDeclTypeMismatch(Sema &S, VarDecl *New, VarDecl* Old) {
  assert(!S.Context.hasSameType(New->getType(), Old->getType()));

  S.Diag(New->getLocation(), New->isThisDeclarationADefinition()
         ? diag::err_redefinition_different_type
         : diag::err_redeclaration_different_type)
    << New->getDeclName() << New->getType() << Old->getType();

  diag::kind PrevDiag;
  SourceLocation OldLocation;
  std::tie(PrevDiag, OldLocation)
    = getNoteDiagForInvalidRedeclaration(Old, New);
  S.Diag(OldLocation, PrevDiag) << Old << Old->getType();
  New->setInvalidDecl();
}

void Sema::MergeVarDeclTypes(VarDecl *New, VarDecl *Old,
                             bool MergeTypeWithOld) {
  if (New->isInvalidDecl() || Old->isInvalidDecl() || New->getType()->containsErrors() || Old->getType()->containsErrors())
    return;

  QualType MergedT;
  if (getLangOpts().CPlusPlus) {
    if (New->getType()->isUndeducedType()) {
      // We don't know what the new type is until the initializer is attached.
      return;
    } else if (Context.hasSameType(New->getType(), Old->getType())) {
      // These could still be something that needs exception specs checked.
      return MergeVarDeclExceptionSpecs(New, Old);
    }
    // C++ [basic.link]p10:
    //   [...] the types specified by all declarations referring to a given
    //   object or function shall be identical, except that declarations for an
    //   array object can specify array types that differ by the presence or
    //   absence of a major array bound (8.3.4).
    else if (Old->getType()->isArrayType() && New->getType()->isArrayType()) {
      const ArrayType *OldArray = Context.getAsArrayType(Old->getType());
      const ArrayType *NewArray = Context.getAsArrayType(New->getType());

      // We are merging a variable declaration New into Old. If it has an array
      // bound, and that bound differs from Old's bound, we should diagnose the
      // mismatch.
      if (!NewArray->isIncompleteArrayType() && !NewArray->isDependentType()) {
        for (VarDecl *PrevVD = Old->getMostRecentDecl(); PrevVD;
             PrevVD = PrevVD->getPreviousDecl()) {
          QualType PrevVDTy = PrevVD->getType();
          if (PrevVDTy->isIncompleteArrayType() || PrevVDTy->isDependentType())
            continue;

          if (!Context.hasSameType(New->getType(), PrevVDTy))
            return diagnoseVarDeclTypeMismatch(*this, New, PrevVD);
        }
      }

      if (OldArray->isIncompleteArrayType() && NewArray->isArrayType()) {
        if (Context.hasSameType(OldArray->getElementType(),
                                NewArray->getElementType()))
          MergedT = New->getType();
      }
      // FIXME: Check visibility. New is hidden but has a complete type. If New
      // has no array bound, it should not inherit one from Old, if Old is not
      // visible.
      else if (OldArray->isArrayType() && NewArray->isIncompleteArrayType()) {
        if (Context.hasSameType(OldArray->getElementType(),
                                NewArray->getElementType()))
          MergedT = Old->getType();
      }
    }
    else if (New->getType()->isObjCObjectPointerType() &&
               Old->getType()->isObjCObjectPointerType()) {
      MergedT = Context.mergeObjCGCQualifiers(New->getType(),
                                              Old->getType());
    }
  } else {
    // C 6.2.7p2:
    //   All declarations that refer to the same object or function shall have
    //   compatible type.
    MergedT = Context.mergeTypes(New->getType(), Old->getType());
  }
  if (MergedT.isNull()) {
    // It's OK if we couldn't merge types if either type is dependent, for a
    // block-scope variable. In other cases (static data members of class
    // templates, variable templates, ...), we require the types to be
    // equivalent.
    // FIXME: The C++ standard doesn't say anything about this.
    if ((New->getType()->isDependentType() ||
         Old->getType()->isDependentType()) && New->isLocalVarDecl()) {
      // If the old type was dependent, we can't merge with it, so the new type
      // becomes dependent for now. We'll reproduce the original type when we
      // instantiate the TypeSourceInfo for the variable.
      if (!New->getType()->isDependentType() && MergeTypeWithOld)
        New->setType(Context.DependentTy);
      return;
    }
    return diagnoseVarDeclTypeMismatch(*this, New, Old);
  }

  // Don't actually update the type on the new declaration if the old
  // declaration was an extern declaration in a different scope.
  if (MergeTypeWithOld)
    New->setType(MergedT);
}

static bool mergeTypeWithPrevious(Sema &S, VarDecl *NewVD, VarDecl *OldVD,
                                  LookupResult &Previous) {
  // C11 6.2.7p4:
  //   For an identifier with internal or external linkage declared
  //   in a scope in which a prior declaration of that identifier is
  //   visible, if the prior declaration specifies internal or
  //   external linkage, the type of the identifier at the later
  //   declaration becomes the composite type.
  //
  // If the variable isn't visible, we do not merge with its type.
  if (Previous.isShadowed())
    return false;

  if (S.getLangOpts().CPlusPlus) {
    // C++11 [dcl.array]p3:
    //   If there is a preceding declaration of the entity in the same
    //   scope in which the bound was specified, an omitted array bound
    //   is taken to be the same as in that earlier declaration.
    return NewVD->isPreviousDeclInSameBlockScope() ||
           (!OldVD->getLexicalDeclContext()->isFunctionOrMethod() &&
            !NewVD->getLexicalDeclContext()->isFunctionOrMethod());
  } else {
    // If the old declaration was function-local, don't merge with its
    // type unless we're in the same function.
    return !OldVD->getLexicalDeclContext()->isFunctionOrMethod() ||
           OldVD->getLexicalDeclContext() == NewVD->getLexicalDeclContext();
  }
}

void Sema::MergeVarDecl(VarDecl *New, LookupResult &Previous) {
  // If the new decl is already invalid, don't do any other checking.
  if (New->isInvalidDecl())
    return;

  if (!shouldLinkPossiblyHiddenDecl(Previous, New))
    return;

  VarTemplateDecl *NewTemplate = New->getDescribedVarTemplate();

  // Verify the old decl was also a variable or variable template.
  VarDecl *Old = nullptr;
  VarTemplateDecl *OldTemplate = nullptr;
  if (Previous.isSingleResult()) {
    if (NewTemplate) {
      OldTemplate = dyn_cast<VarTemplateDecl>(Previous.getFoundDecl());
      Old = OldTemplate ? OldTemplate->getTemplatedDecl() : nullptr;

      if (auto *Shadow =
              dyn_cast<UsingShadowDecl>(Previous.getRepresentativeDecl()))
        if (checkUsingShadowRedecl<VarTemplateDecl>(*this, Shadow, NewTemplate))
          return New->setInvalidDecl();
    } else {
      Old = dyn_cast<VarDecl>(Previous.getFoundDecl());

      if (auto *Shadow =
              dyn_cast<UsingShadowDecl>(Previous.getRepresentativeDecl()))
        if (checkUsingShadowRedecl<VarDecl>(*this, Shadow, New))
          return New->setInvalidDecl();
    }
  }
  if (!Old) {
    Diag(New->getLocation(), diag::err_redefinition_different_kind)
        << New->getDeclName();
    notePreviousDefinition(Previous.getRepresentativeDecl(),
                           New->getLocation());
    return New->setInvalidDecl();
  }

  // If the old declaration was found in an inline namespace and the new
  // declaration was qualified, update the DeclContext to match.
  adjustDeclContextForDeclaratorDecl(New, Old);

  // Ensure the template parameters are compatible.
  if (NewTemplate &&
      !TemplateParameterListsAreEqual(NewTemplate->getTemplateParameters(),
                                      OldTemplate->getTemplateParameters(),
                                      /*Complain=*/true, TPL_TemplateMatch))
    return New->setInvalidDecl();

  // C++ [class.mem]p1:
  //   A member shall not be declared twice in the member-specification [...]
  //
  // Here, we need only consider static data members.
  if (Old->isStaticDataMember() && !New->isOutOfLine()) {
    Diag(New->getLocation(), diag::err_duplicate_member)
      << New->getIdentifier();
    Diag(Old->getLocation(), diag::note_previous_declaration);
    New->setInvalidDecl();
  }

  mergeDeclAttributes(New, Old);
  // Warn if an already-defined variable is made a weak_import in a subsequent
  // declaration
  if (New->hasAttr<WeakImportAttr>())
    for (auto *D = Old; D; D = D->getPreviousDecl()) {
      if (D->isThisDeclarationADefinition() != VarDecl::DeclarationOnly) {
        Diag(New->getLocation(), diag::warn_weak_import) << New->getDeclName();
        Diag(D->getLocation(), diag::note_previous_definition);
        // Remove weak_import attribute on new declaration.
        New->dropAttr<WeakImportAttr>();
        break;
      }
    }

  if (const auto *ILA = New->getAttr<InternalLinkageAttr>())
    if (!Old->hasAttr<InternalLinkageAttr>()) {
      Diag(New->getLocation(), diag::err_attribute_missing_on_first_decl)
          << ILA;
      Diag(Old->getLocation(), diag::note_previous_declaration);
      New->dropAttr<InternalLinkageAttr>();
    }

  // Merge the types.
  VarDecl *MostRecent = Old->getMostRecentDecl();
  if (MostRecent != Old) {
    MergeVarDeclTypes(New, MostRecent,
                      mergeTypeWithPrevious(*this, New, MostRecent, Previous));
    if (New->isInvalidDecl())
      return;
  }

  MergeVarDeclTypes(New, Old, mergeTypeWithPrevious(*this, New, Old, Previous));
  if (New->isInvalidDecl())
    return;

  diag::kind PrevDiag;
  SourceLocation OldLocation;
  std::tie(PrevDiag, OldLocation) =
      getNoteDiagForInvalidRedeclaration(Old, New);

  // [dcl.stc]p8: Check if we have a non-static decl followed by a static.
  if (New->getStorageClass() == SC_Static &&
      !New->isStaticDataMember() &&
      Old->hasExternalFormalLinkage()) {
    if (getLangOpts().MicrosoftExt) {
      Diag(New->getLocation(), diag::ext_static_non_static)
          << New->getDeclName();
      Diag(OldLocation, PrevDiag);
    } else {
      Diag(New->getLocation(), diag::err_static_non_static)
          << New->getDeclName();
      Diag(OldLocation, PrevDiag);
      return New->setInvalidDecl();
    }
  }
  // C99 6.2.2p4:
  //   For an identifier declared with the storage-class specifier
  //   extern in a scope in which a prior declaration of that
  //   identifier is visible,23) if the prior declaration specifies
  //   internal or external linkage, the linkage of the identifier at
  //   the later declaration is the same as the linkage specified at
  //   the prior declaration. If no prior declaration is visible, or
  //   if the prior declaration specifies no linkage, then the
  //   identifier has external linkage.
  if (New->hasExternalStorage() && Old->hasLinkage())
    /* Okay */;
  else if (New->getCanonicalDecl()->getStorageClass() != SC_Static &&
           !New->isStaticDataMember() &&
           Old->getCanonicalDecl()->getStorageClass() == SC_Static) {
    Diag(New->getLocation(), diag::err_non_static_static) << New->getDeclName();
    Diag(OldLocation, PrevDiag);
    return New->setInvalidDecl();
  }

  // Check if extern is followed by non-extern and vice-versa.
  if (New->hasExternalStorage() &&
      !Old->hasLinkage() && Old->isLocalVarDeclOrParm()) {
    Diag(New->getLocation(), diag::err_extern_non_extern) << New->getDeclName();
    Diag(OldLocation, PrevDiag);
    return New->setInvalidDecl();
  }
  if (Old->hasLinkage() && New->isLocalVarDeclOrParm() &&
      !New->hasExternalStorage()) {
    Diag(New->getLocation(), diag::err_non_extern_extern) << New->getDeclName();
    Diag(OldLocation, PrevDiag);
    return New->setInvalidDecl();
  }

  if (CheckRedeclarationInModule(New, Old))
    return;

  // Variables with external linkage are analyzed in FinalizeDeclaratorGroup.

  // FIXME: The test for external storage here seems wrong? We still
  // need to check for mismatches.
  if (!New->hasExternalStorage() && !New->isFileVarDecl() &&
      // Don't complain about out-of-line definitions of static members.
      !(Old->getLexicalDeclContext()->isRecord() &&
        !New->getLexicalDeclContext()->isRecord())) {
    Diag(New->getLocation(), diag::err_redefinition) << New->getDeclName();
    Diag(OldLocation, PrevDiag);
    return New->setInvalidDecl();
  }

  if (New->isInline() && !Old->getMostRecentDecl()->isInline()) {
    if (VarDecl *Def = Old->getDefinition()) {
      // C++1z [dcl.fcn.spec]p4:
      //   If the definition of a variable appears in a translation unit before
      //   its first declaration as inline, the program is ill-formed.
      Diag(New->getLocation(), diag::err_inline_decl_follows_def) << New;
      Diag(Def->getLocation(), diag::note_previous_definition);
    }
  }

  // If this redeclaration makes the variable inline, we may need to add it to
  // UndefinedButUsed.
  if (!Old->isInline() && New->isInline() && Old->isUsed(false) &&
      !Old->getDefinition() && !New->isThisDeclarationADefinition())
    UndefinedButUsed.insert(std::make_pair(Old->getCanonicalDecl(),
                                           SourceLocation()));

  if (New->getTLSKind() != Old->getTLSKind()) {
    if (!Old->getTLSKind()) {
      Diag(New->getLocation(), diag::err_thread_non_thread) << New->getDeclName();
      Diag(OldLocation, PrevDiag);
    } else if (!New->getTLSKind()) {
      Diag(New->getLocation(), diag::err_non_thread_thread) << New->getDeclName();
      Diag(OldLocation, PrevDiag);
    } else {
      // Do not allow redeclaration to change the variable between requiring
      // static and dynamic initialization.
      // FIXME: GCC allows this, but uses the TLS keyword on the first
      // declaration to determine the kind. Do we need to be compatible here?
      Diag(New->getLocation(), diag::err_thread_thread_different_kind)
        << New->getDeclName() << (New->getTLSKind() == VarDecl::TLS_Dynamic);
      Diag(OldLocation, PrevDiag);
    }
  }

  // C++ doesn't have tentative definitions, so go right ahead and check here.
  if (getLangOpts().CPlusPlus) {
    if (Old->isStaticDataMember() && Old->getCanonicalDecl()->isInline() &&
        Old->getCanonicalDecl()->isConstexpr()) {
      // This definition won't be a definition any more once it's been merged.
      Diag(New->getLocation(),
           diag::warn_deprecated_redundant_constexpr_static_def);
    } else if (New->isThisDeclarationADefinition() == VarDecl::Definition) {
      VarDecl *Def = Old->getDefinition();
      if (Def && checkVarDeclRedefinition(Def, New))
        return;
    }
  }

  if (haveIncompatibleLanguageLinkages(Old, New)) {
    Diag(New->getLocation(), diag::err_different_language_linkage) << New;
    Diag(OldLocation, PrevDiag);
    New->setInvalidDecl();
    return;
  }

  // Merge "used" flag.
  if (Old->getMostRecentDecl()->isUsed(false))
    New->setIsUsed();

  // Keep a chain of previous declarations.
  New->setPreviousDecl(Old);
  if (NewTemplate)
    NewTemplate->setPreviousDecl(OldTemplate);

  // Inherit access appropriately.
  New->setAccess(Old->getAccess());
  if (NewTemplate)
    NewTemplate->setAccess(New->getAccess());

  if (Old->isInline())
    New->setImplicitlyInline();
}

void Sema::notePreviousDefinition(const NamedDecl *Old, SourceLocation New) {
  SourceManager &SrcMgr = getSourceManager();
  auto FNewDecLoc = SrcMgr.getDecomposedLoc(New);
  auto FOldDecLoc = SrcMgr.getDecomposedLoc(Old->getLocation());
  auto *FNew = SrcMgr.getFileEntryForID(FNewDecLoc.first);
  auto FOld = SrcMgr.getFileEntryRefForID(FOldDecLoc.first);
  auto &HSI = PP.getHeaderSearchInfo();
  StringRef HdrFilename =
      SrcMgr.getFilename(SrcMgr.getSpellingLoc(Old->getLocation()));

  auto noteFromModuleOrInclude = [&](Module *Mod,
                                     SourceLocation IncLoc) -> bool {
    // Redefinition errors with modules are common with non modular mapped
    // headers, example: a non-modular header H in module A that also gets
    // included directly in a TU. Pointing twice to the same header/definition
    // is confusing, try to get better diagnostics when modules is on.
    if (IncLoc.isValid()) {
      if (Mod) {
        Diag(IncLoc, diag::note_redefinition_modules_same_file)
            << HdrFilename.str() << Mod->getFullModuleName();
        if (!Mod->DefinitionLoc.isInvalid())
          Diag(Mod->DefinitionLoc, diag::note_defined_here)
              << Mod->getFullModuleName();
      } else {
        Diag(IncLoc, diag::note_redefinition_include_same_file)
            << HdrFilename.str();
      }
      return true;
    }

    return false;
  };

  // Is it the same file and same offset? Provide more information on why
  // this leads to a redefinition error.
  if (FNew == FOld && FNewDecLoc.second == FOldDecLoc.second) {
    SourceLocation OldIncLoc = SrcMgr.getIncludeLoc(FOldDecLoc.first);
    SourceLocation NewIncLoc = SrcMgr.getIncludeLoc(FNewDecLoc.first);
    bool EmittedDiag =
        noteFromModuleOrInclude(Old->getOwningModule(), OldIncLoc);
    EmittedDiag |= noteFromModuleOrInclude(getCurrentModule(), NewIncLoc);

    // If the header has no guards, emit a note suggesting one.
    if (FOld && !HSI.isFileMultipleIncludeGuarded(*FOld))
      Diag(Old->getLocation(), diag::note_use_ifdef_guards);

    if (EmittedDiag)
      return;
  }

  // Redefinition coming from different files or couldn't do better above.
  if (Old->getLocation().isValid())
    Diag(Old->getLocation(), diag::note_previous_definition);
}

bool Sema::checkVarDeclRedefinition(VarDecl *Old, VarDecl *New) {
  if (!hasVisibleDefinition(Old) &&
      (New->getFormalLinkage() == Linkage::Internal || New->isInline() ||
       isa<VarTemplateSpecializationDecl>(New) ||
       New->getDescribedVarTemplate() || New->getNumTemplateParameterLists() ||
       New->getDeclContext()->isDependentContext())) {
    // The previous definition is hidden, and multiple definitions are
    // permitted (in separate TUs). Demote this to a declaration.
    New->demoteThisDefinitionToDeclaration();

    // Make the canonical definition visible.
    if (auto *OldTD = Old->getDescribedVarTemplate())
      makeMergedDefinitionVisible(OldTD);
    makeMergedDefinitionVisible(Old);
    return false;
  } else {
    Diag(New->getLocation(), diag::err_redefinition) << New;
    notePreviousDefinition(Old, New->getLocation());
    New->setInvalidDecl();
    return true;
  }
}

Decl *Sema::ParsedFreeStandingDeclSpec(Scope *S, AccessSpecifier AS,
                                       DeclSpec &DS,
                                       const ParsedAttributesView &DeclAttrs,
                                       RecordDecl *&AnonRecord) {
  return ParsedFreeStandingDeclSpec(
      S, AS, DS, DeclAttrs, MultiTemplateParamsArg(), false, AnonRecord);
}

// The MS ABI changed between VS2013 and VS2015 with regard to numbers used to
// disambiguate entities defined in different scopes.
// While the VS2015 ABI fixes potential miscompiles, it is also breaks
// compatibility.
// We will pick our mangling number depending on which version of MSVC is being
// targeted.
static unsigned getMSManglingNumber(const LangOptions &LO, Scope *S) {
  return LO.isCompatibleWithMSVC(LangOptions::MSVC2015)
             ? S->getMSCurManglingNumber()
             : S->getMSLastManglingNumber();
}

void Sema::handleTagNumbering(const TagDecl *Tag, Scope *TagScope) {
  if (!Context.getLangOpts().CPlusPlus)
    return;

  if (isa<CXXRecordDecl>(Tag->getParent())) {
    // If this tag is the direct child of a class, number it if
    // it is anonymous.
    if (!Tag->getName().empty() || Tag->getTypedefNameForAnonDecl())
      return;
    MangleNumberingContext &MCtx =
        Context.getManglingNumberContext(Tag->getParent());
    Context.setManglingNumber(
        Tag, MCtx.getManglingNumber(
                 Tag, getMSManglingNumber(getLangOpts(), TagScope)));
    return;
  }

  // If this tag isn't a direct child of a class, number it if it is local.
  MangleNumberingContext *MCtx;
  Decl *ManglingContextDecl;
  std::tie(MCtx, ManglingContextDecl) =
      getCurrentMangleNumberContext(Tag->getDeclContext());
  if (MCtx) {
    Context.setManglingNumber(
        Tag, MCtx->getManglingNumber(
                 Tag, getMSManglingNumber(getLangOpts(), TagScope)));
  }
}

namespace {
struct NonCLikeKind {
  enum {
    None,
    BaseClass,
    DefaultMemberInit,
    Lambda,
    Friend,
    OtherMember,
    Invalid,
  } Kind = None;
  SourceRange Range;

  explicit operator bool() { return Kind != None; }
};
}

/// Determine whether a class is C-like, according to the rules of C++
/// [dcl.typedef] for anonymous classes with typedef names for linkage.
static NonCLikeKind getNonCLikeKindForAnonymousStruct(const CXXRecordDecl *RD) {
  if (RD->isInvalidDecl())
    return {NonCLikeKind::Invalid, {}};

  // C++ [dcl.typedef]p9: [P1766R1]
  //   An unnamed class with a typedef name for linkage purposes shall not
  //
  //    -- have any base classes
  if (RD->getNumBases())
    return {NonCLikeKind::BaseClass,
            SourceRange(RD->bases_begin()->getBeginLoc(),
                        RD->bases_end()[-1].getEndLoc())};
  bool Invalid = false;
  for (Decl *D : RD->decls()) {
    // Don't complain about things we already diagnosed.
    if (D->isInvalidDecl()) {
      Invalid = true;
      continue;
    }

    //  -- have any [...] default member initializers
    if (auto *FD = dyn_cast<FieldDecl>(D)) {
      if (FD->hasInClassInitializer()) {
        auto *Init = FD->getInClassInitializer();
        return {NonCLikeKind::DefaultMemberInit,
                Init ? Init->getSourceRange() : D->getSourceRange()};
      }
      continue;
    }

    // FIXME: We don't allow friend declarations. This violates the wording of
    // P1766, but not the intent.
    if (isa<FriendDecl>(D))
      return {NonCLikeKind::Friend, D->getSourceRange()};

    //  -- declare any members other than non-static data members, member
    //     enumerations, or member classes,
    if (isa<StaticAssertDecl>(D) || isa<IndirectFieldDecl>(D) ||
        isa<EnumDecl>(D))
      continue;
    auto *MemberRD = dyn_cast<CXXRecordDecl>(D);
    if (!MemberRD) {
      if (D->isImplicit())
        continue;
      return {NonCLikeKind::OtherMember, D->getSourceRange()};
    }

    //  -- contain a lambda-expression,
    if (MemberRD->isLambda())
      return {NonCLikeKind::Lambda, MemberRD->getSourceRange()};

    //  and all member classes shall also satisfy these requirements
    //  (recursively).
    if (MemberRD->isThisDeclarationADefinition()) {
      if (auto Kind = getNonCLikeKindForAnonymousStruct(MemberRD))
        return Kind;
    }
  }

  return {Invalid ? NonCLikeKind::Invalid : NonCLikeKind::None, {}};
}

void Sema::setTagNameForLinkagePurposes(TagDecl *TagFromDeclSpec,
                                        TypedefNameDecl *NewTD) {
  if (TagFromDeclSpec->isInvalidDecl())
    return;

  // Do nothing if the tag already has a name for linkage purposes.
  if (TagFromDeclSpec->hasNameForLinkage())
    return;

  // A well-formed anonymous tag must always be a TagUseKind::Definition.
  assert(TagFromDeclSpec->isThisDeclarationADefinition());

  // The type must match the tag exactly;  no qualifiers allowed.
  if (!Context.hasSameType(NewTD->getUnderlyingType(),
                           Context.getTagDeclType(TagFromDeclSpec))) {
    if (getLangOpts().CPlusPlus)
      Context.addTypedefNameForUnnamedTagDecl(TagFromDeclSpec, NewTD);
    return;
  }

  // C++ [dcl.typedef]p9: [P1766R1, applied as DR]
  //   An unnamed class with a typedef name for linkage purposes shall [be
  //   C-like].
  //
  // FIXME: Also diagnose if we've already computed the linkage. That ideally
  // shouldn't happen, but there are constructs that the language rule doesn't
  // disallow for which we can't reasonably avoid computing linkage early.
  const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(TagFromDeclSpec);
  NonCLikeKind NonCLike = RD ? getNonCLikeKindForAnonymousStruct(RD)
                             : NonCLikeKind();
  bool ChangesLinkage = TagFromDeclSpec->hasLinkageBeenComputed();
  if (NonCLike || ChangesLinkage) {
    if (NonCLike.Kind == NonCLikeKind::Invalid)
      return;

    unsigned DiagID = diag::ext_non_c_like_anon_struct_in_typedef;
    if (ChangesLinkage) {
      // If the linkage changes, we can't accept this as an extension.
      if (NonCLike.Kind == NonCLikeKind::None)
        DiagID = diag::err_typedef_changes_linkage;
      else
        DiagID = diag::err_non_c_like_anon_struct_in_typedef;
    }

    SourceLocation FixitLoc =
        getLocForEndOfToken(TagFromDeclSpec->getInnerLocStart());
    llvm::SmallString<40> TextToInsert;
    TextToInsert += ' ';
    TextToInsert += NewTD->getIdentifier()->getName();

    Diag(FixitLoc, DiagID)
      << isa<TypeAliasDecl>(NewTD)
      << FixItHint::CreateInsertion(FixitLoc, TextToInsert);
    if (NonCLike.Kind != NonCLikeKind::None) {
      Diag(NonCLike.Range.getBegin(), diag::note_non_c_like_anon_struct)
        << NonCLike.Kind - 1 << NonCLike.Range;
    }
    Diag(NewTD->getLocation(), diag::note_typedef_for_linkage_here)
      << NewTD << isa<TypeAliasDecl>(NewTD);

    if (ChangesLinkage)
      return;
  }

  // Otherwise, set this as the anon-decl typedef for the tag.
  TagFromDeclSpec->setTypedefNameForAnonDecl(NewTD);
}

static unsigned GetDiagnosticTypeSpecifierID(const DeclSpec &DS) {
  DeclSpec::TST T = DS.getTypeSpecType();
  switch (T) {
  case DeclSpec::TST_class:
    return 0;
  case DeclSpec::TST_struct:
    return 1;
  case DeclSpec::TST_interface:
    return 2;
  case DeclSpec::TST_union:
    return 3;
  case DeclSpec::TST_enum:
    if (const auto *ED = dyn_cast<EnumDecl>(DS.getRepAsDecl())) {
      if (ED->isScopedUsingClassTag())
        return 5;
      if (ED->isScoped())
        return 6;
    }
    return 4;
  default:
    llvm_unreachable("unexpected type specifier");
  }
}

Decl *Sema::ParsedFreeStandingDeclSpec(Scope *S, AccessSpecifier AS,
                                       DeclSpec &DS,
                                       const ParsedAttributesView &DeclAttrs,
                                       MultiTemplateParamsArg TemplateParams,
                                       bool IsExplicitInstantiation,
                                       RecordDecl *&AnonRecord) {
  Decl *TagD = nullptr;
  TagDecl *Tag = nullptr;
  if (DS.getTypeSpecType() == DeclSpec::TST_class ||
      DS.getTypeSpecType() == DeclSpec::TST_struct ||
      DS.getTypeSpecType() == DeclSpec::TST_interface ||
      DS.getTypeSpecType() == DeclSpec::TST_union ||
      DS.getTypeSpecType() == DeclSpec::TST_enum) {
    TagD = DS.getRepAsDecl();

    if (!TagD) // We probably had an error
      return nullptr;

    // Note that the above type specs guarantee that the
    // type rep is a Decl, whereas in many of the others
    // it's a Type.
    if (isa<TagDecl>(TagD))
      Tag = cast<TagDecl>(TagD);
    else if (ClassTemplateDecl *CTD = dyn_cast<ClassTemplateDecl>(TagD))
      Tag = CTD->getTemplatedDecl();
  }

  if (Tag) {
    handleTagNumbering(Tag, S);
    Tag->setFreeStanding();
    if (Tag->isInvalidDecl())
      return Tag;
  }

  if (unsigned TypeQuals = DS.getTypeQualifiers()) {
    // Enforce C99 6.7.3p2: "Types other than pointer types derived from object
    // or incomplete types shall not be restrict-qualified."
    if (TypeQuals & DeclSpec::TQ_restrict)
      Diag(DS.getRestrictSpecLoc(),
           diag::err_typecheck_invalid_restrict_not_pointer_noarg)
           << DS.getSourceRange();
  }

  if (DS.isInlineSpecified())
    Diag(DS.getInlineSpecLoc(), diag::err_inline_non_function)
        << getLangOpts().CPlusPlus17;

  if (DS.hasConstexprSpecifier()) {
    // C++0x [dcl.constexpr]p1: constexpr can only be applied to declarations
    // and definitions of functions and variables.
    // C++2a [dcl.constexpr]p1: The consteval specifier shall be applied only to
    // the declaration of a function or function template
    if (Tag)
      Diag(DS.getConstexprSpecLoc(), diag::err_constexpr_tag)
          << GetDiagnosticTypeSpecifierID(DS)
          << static_cast<int>(DS.getConstexprSpecifier());
    else if (getLangOpts().C23)
      Diag(DS.getConstexprSpecLoc(), diag::err_c23_constexpr_not_variable);
    else
      Diag(DS.getConstexprSpecLoc(), diag::err_constexpr_wrong_decl_kind)
          << static_cast<int>(DS.getConstexprSpecifier());
    // Don't emit warnings after this error.
    return TagD;
  }

  DiagnoseFunctionSpecifiers(DS);

  if (DS.isFriendSpecified()) {
    // If we're dealing with a decl but not a TagDecl, assume that
    // whatever routines created it handled the friendship aspect.
    if (TagD && !Tag)
      return nullptr;
    return ActOnFriendTypeDecl(S, DS, TemplateParams);
  }

  // Track whether this decl-specifier declares anything.
  bool DeclaresAnything = true;

  // Handle anonymous struct definitions.
  if (RecordDecl *Record = dyn_cast_or_null<RecordDecl>(Tag)) {
    if (!Record->getDeclName() && Record->isCompleteDefinition() &&
        DS.getStorageClassSpec() != DeclSpec::SCS_typedef) {
      if (getLangOpts().CPlusPlus ||
          Record->getDeclContext()->isRecord()) {
        // If CurContext is a DeclContext that can contain statements,
        // RecursiveASTVisitor won't visit the decls that
        // BuildAnonymousStructOrUnion() will put into CurContext.
        // Also store them here so that they can be part of the
        // DeclStmt that gets created in this case.
        // FIXME: Also return the IndirectFieldDecls created by
        // BuildAnonymousStructOr union, for the same reason?
        if (CurContext->isFunctionOrMethod())
          AnonRecord = Record;
        return BuildAnonymousStructOrUnion(S, DS, AS, Record,
                                           Context.getPrintingPolicy());
      }

      DeclaresAnything = false;
    }
  }

  // C11 6.7.2.1p2:
  //   A struct-declaration that does not declare an anonymous structure or
  //   anonymous union shall contain a struct-declarator-list.
  //
  // This rule also existed in C89 and C99; the grammar for struct-declaration
  // did not permit a struct-declaration without a struct-declarator-list.
  if (!getLangOpts().CPlusPlus && CurContext->isRecord() &&
      DS.getStorageClassSpec() == DeclSpec::SCS_unspecified) {
    // Check for Microsoft C extension: anonymous struct/union member.
    // Handle 2 kinds of anonymous struct/union:
    //   struct STRUCT;
    //   union UNION;
    // and
    //   STRUCT_TYPE;  <- where STRUCT_TYPE is a typedef struct.
    //   UNION_TYPE;   <- where UNION_TYPE is a typedef union.
    if ((Tag && Tag->getDeclName()) ||
        DS.getTypeSpecType() == DeclSpec::TST_typename) {
      RecordDecl *Record = nullptr;
      if (Tag)
        Record = dyn_cast<RecordDecl>(Tag);
      else if (const RecordType *RT =
                   DS.getRepAsType().get()->getAsStructureType())
        Record = RT->getDecl();
      else if (const RecordType *UT = DS.getRepAsType().get()->getAsUnionType())
        Record = UT->getDecl();

      if (Record && getLangOpts().MicrosoftExt) {
        Diag(DS.getBeginLoc(), diag::ext_ms_anonymous_record)
            << Record->isUnion() << DS.getSourceRange();
        return BuildMicrosoftCAnonymousStruct(S, DS, Record);
      }

      DeclaresAnything = false;
    }
  }

  // Skip all the checks below if we have a type error.
  if (DS.getTypeSpecType() == DeclSpec::TST_error ||
      (TagD && TagD->isInvalidDecl()))
    return TagD;

  if (getLangOpts().CPlusPlus &&
      DS.getStorageClassSpec() != DeclSpec::SCS_typedef)
    if (EnumDecl *Enum = dyn_cast_or_null<EnumDecl>(Tag))
      if (Enum->enumerator_begin() == Enum->enumerator_end() &&
          !Enum->getIdentifier() && !Enum->isInvalidDecl())
        DeclaresAnything = false;

  if (!DS.isMissingDeclaratorOk()) {
    // Customize diagnostic for a typedef missing a name.
    if (DS.getStorageClassSpec() == DeclSpec::SCS_typedef)
      Diag(DS.getBeginLoc(), diag::ext_typedef_without_a_name)
          << DS.getSourceRange();
    else
      DeclaresAnything = false;
  }

  if (DS.isModulePrivateSpecified() &&
      Tag && Tag->getDeclContext()->isFunctionOrMethod())
    Diag(DS.getModulePrivateSpecLoc(), diag::err_module_private_local_class)
        << llvm::to_underlying(Tag->getTagKind())
        << FixItHint::CreateRemoval(DS.getModulePrivateSpecLoc());

  ActOnDocumentableDecl(TagD);

  // C 6.7/2:
  //   A declaration [...] shall declare at least a declarator [...], a tag,
  //   or the members of an enumeration.
  // C++ [dcl.dcl]p3:
  //   [If there are no declarators], and except for the declaration of an
  //   unnamed bit-field, the decl-specifier-seq shall introduce one or more
  //   names into the program, or shall redeclare a name introduced by a
  //   previous declaration.
  if (!DeclaresAnything) {
    // In C, we allow this as a (popular) extension / bug. Don't bother
    // producing further diagnostics for redundant qualifiers after this.
    Diag(DS.getBeginLoc(), (IsExplicitInstantiation || !TemplateParams.empty())
                               ? diag::err_no_declarators
                               : diag::ext_no_declarators)
        << DS.getSourceRange();
    return TagD;
  }

  // C++ [dcl.stc]p1:
  //   If a storage-class-specifier appears in a decl-specifier-seq, [...] the
  //   init-declarator-list of the declaration shall not be empty.
  // C++ [dcl.fct.spec]p1:
  //   If a cv-qualifier appears in a decl-specifier-seq, the
  //   init-declarator-list of the declaration shall not be empty.
  //
  // Spurious qualifiers here appear to be valid in C.
  unsigned DiagID = diag::warn_standalone_specifier;
  if (getLangOpts().CPlusPlus)
    DiagID = diag::ext_standalone_specifier;

  // Note that a linkage-specification sets a storage class, but
  // 'extern "C" struct foo;' is actually valid and not theoretically
  // useless.
  if (DeclSpec::SCS SCS = DS.getStorageClassSpec()) {
    if (SCS == DeclSpec::SCS_mutable)
      // Since mutable is not a viable storage class specifier in C, there is
      // no reason to treat it as an extension. Instead, diagnose as an error.
      Diag(DS.getStorageClassSpecLoc(), diag::err_mutable_nonmember);
    else if (!DS.isExternInLinkageSpec() && SCS != DeclSpec::SCS_typedef)
      Diag(DS.getStorageClassSpecLoc(), DiagID)
        << DeclSpec::getSpecifierName(SCS);
  }

  if (DeclSpec::TSCS TSCS = DS.getThreadStorageClassSpec())
    Diag(DS.getThreadStorageClassSpecLoc(), DiagID)
      << DeclSpec::getSpecifierName(TSCS);
  if (DS.getTypeQualifiers()) {
    if (DS.getTypeQualifiers() & DeclSpec::TQ_const)
      Diag(DS.getConstSpecLoc(), DiagID) << "const";
    if (DS.getTypeQualifiers() & DeclSpec::TQ_volatile)
      Diag(DS.getConstSpecLoc(), DiagID) << "volatile";
    // Restrict is covered above.
    if (DS.getTypeQualifiers() & DeclSpec::TQ_atomic)
      Diag(DS.getAtomicSpecLoc(), DiagID) << "_Atomic";
    if (DS.getTypeQualifiers() & DeclSpec::TQ_unaligned)
      Diag(DS.getUnalignedSpecLoc(), DiagID) << "__unaligned";
  }

  // Warn about ignored type attributes, for example:
  // __attribute__((aligned)) struct A;
  // Attributes should be placed after tag to apply to type declaration.
  if (!DS.getAttributes().empty() || !DeclAttrs.empty()) {
    DeclSpec::TST TypeSpecType = DS.getTypeSpecType();
    if (TypeSpecType == DeclSpec::TST_class ||
        TypeSpecType == DeclSpec::TST_struct ||
        TypeSpecType == DeclSpec::TST_interface ||
        TypeSpecType == DeclSpec::TST_union ||
        TypeSpecType == DeclSpec::TST_enum) {

      auto EmitAttributeDiagnostic = [this, &DS](const ParsedAttr &AL) {
        unsigned DiagnosticId = diag::warn_declspec_attribute_ignored;
        if (AL.isAlignas() && !getLangOpts().CPlusPlus)
          DiagnosticId = diag::warn_attribute_ignored;
        else if (AL.isRegularKeywordAttribute())
          DiagnosticId = diag::err_declspec_keyword_has_no_effect;
        else
          DiagnosticId = diag::warn_declspec_attribute_ignored;
        Diag(AL.getLoc(), DiagnosticId)
            << AL << GetDiagnosticTypeSpecifierID(DS);
      };

      llvm::for_each(DS.getAttributes(), EmitAttributeDiagnostic);
      llvm::for_each(DeclAttrs, EmitAttributeDiagnostic);
    }
  }

  return TagD;
}

/// We are trying to inject an anonymous member into the given scope;
/// check if there's an existing declaration that can't be overloaded.
///
/// \return true if this is a forbidden redeclaration
static bool CheckAnonMemberRedeclaration(Sema &SemaRef, Scope *S,
                                         DeclContext *Owner,
                                         DeclarationName Name,
                                         SourceLocation NameLoc, bool IsUnion,
                                         StorageClass SC) {
  LookupResult R(SemaRef, Name, NameLoc,
                 Owner->isRecord() ? Sema::LookupMemberName
                                   : Sema::LookupOrdinaryName,
                 RedeclarationKind::ForVisibleRedeclaration);
  if (!SemaRef.LookupName(R, S)) return false;

  // Pick a representative declaration.
  NamedDecl *PrevDecl = R.getRepresentativeDecl()->getUnderlyingDecl();
  assert(PrevDecl && "Expected a non-null Decl");

  if (!SemaRef.isDeclInScope(PrevDecl, Owner, S))
    return false;

  if (SC == StorageClass::SC_None &&
      PrevDecl->isPlaceholderVar(SemaRef.getLangOpts()) &&
      (Owner->isFunctionOrMethod() || Owner->isRecord())) {
    if (!Owner->isRecord())
      SemaRef.DiagPlaceholderVariableDefinition(NameLoc);
    return false;
  }

  SemaRef.Diag(NameLoc, diag::err_anonymous_record_member_redecl)
    << IsUnion << Name;
  SemaRef.Diag(PrevDecl->getLocation(), diag::note_previous_declaration);

  return true;
}

void Sema::ActOnDefinedDeclarationSpecifier(Decl *D) {
  if (auto *RD = dyn_cast_if_present<RecordDecl>(D))
    DiagPlaceholderFieldDeclDefinitions(RD);
}

void Sema::DiagPlaceholderFieldDeclDefinitions(RecordDecl *Record) {
  if (!getLangOpts().CPlusPlus)
    return;

  // This function can be parsed before we have validated the
  // structure as an anonymous struct
  if (Record->isAnonymousStructOrUnion())
    return;

  const NamedDecl *First = 0;
  for (const Decl *D : Record->decls()) {
    const NamedDecl *ND = dyn_cast<NamedDecl>(D);
    if (!ND || !ND->isPlaceholderVar(getLangOpts()))
      continue;
    if (!First)
      First = ND;
    else
      DiagPlaceholderVariableDefinition(ND->getLocation());
  }
}

/// InjectAnonymousStructOrUnionMembers - Inject the members of the
/// anonymous struct or union AnonRecord into the owning context Owner
/// and scope S. This routine will be invoked just after we realize
/// that an unnamed union or struct is actually an anonymous union or
/// struct, e.g.,
///
/// @code
/// union {
///   int i;
///   float f;
/// }; // InjectAnonymousStructOrUnionMembers called here to inject i and
///    // f into the surrounding scope.x
/// @endcode
///
/// This routine is recursive, injecting the names of nested anonymous
/// structs/unions into the owning context and scope as well.
static bool
InjectAnonymousStructOrUnionMembers(Sema &SemaRef, Scope *S, DeclContext *Owner,
                                    RecordDecl *AnonRecord, AccessSpecifier AS,
                                    StorageClass SC,
                                    SmallVectorImpl<NamedDecl *> &Chaining) {
  bool Invalid = false;

  // Look every FieldDecl and IndirectFieldDecl with a name.
  for (auto *D : AnonRecord->decls()) {
    if ((isa<FieldDecl>(D) || isa<IndirectFieldDecl>(D)) &&
        cast<NamedDecl>(D)->getDeclName()) {
      ValueDecl *VD = cast<ValueDecl>(D);
      if (CheckAnonMemberRedeclaration(SemaRef, S, Owner, VD->getDeclName(),
                                       VD->getLocation(), AnonRecord->isUnion(),
                                       SC)) {
        // C++ [class.union]p2:
        //   The names of the members of an anonymous union shall be
        //   distinct from the names of any other entity in the
        //   scope in which the anonymous union is declared.
        Invalid = true;
      } else {
        // C++ [class.union]p2:
        //   For the purpose of name lookup, after the anonymous union
        //   definition, the members of the anonymous union are
        //   considered to have been defined in the scope in which the
        //   anonymous union is declared.
        unsigned OldChainingSize = Chaining.size();
        if (IndirectFieldDecl *IF = dyn_cast<IndirectFieldDecl>(VD))
          Chaining.append(IF->chain_begin(), IF->chain_end());
        else
          Chaining.push_back(VD);

        assert(Chaining.size() >= 2);
        NamedDecl **NamedChain =
          new (SemaRef.Context)NamedDecl*[Chaining.size()];
        for (unsigned i = 0; i < Chaining.size(); i++)
          NamedChain[i] = Chaining[i];

        IndirectFieldDecl *IndirectField = IndirectFieldDecl::Create(
            SemaRef.Context, Owner, VD->getLocation(), VD->getIdentifier(),
            VD->getType(), {NamedChain, Chaining.size()});

        for (const auto *Attr : VD->attrs())
          IndirectField->addAttr(Attr->clone(SemaRef.Context));

        IndirectField->setAccess(AS);
        IndirectField->setImplicit();
        SemaRef.PushOnScopeChains(IndirectField, S);

        // That includes picking up the appropriate access specifier.
        if (AS != AS_none) IndirectField->setAccess(AS);

        Chaining.resize(OldChainingSize);
      }
    }
  }

  return Invalid;
}

/// StorageClassSpecToVarDeclStorageClass - Maps a DeclSpec::SCS to
/// a VarDecl::StorageClass. Any error reporting is up to the caller:
/// illegal input values are mapped to SC_None.
static StorageClass
StorageClassSpecToVarDeclStorageClass(const DeclSpec &DS) {
  DeclSpec::SCS StorageClassSpec = DS.getStorageClassSpec();
  assert(StorageClassSpec != DeclSpec::SCS_typedef &&
         "Parser allowed 'typedef' as storage class VarDecl.");
  switch (StorageClassSpec) {
  case DeclSpec::SCS_unspecified:    return SC_None;
  case DeclSpec::SCS_extern:
    if (DS.isExternInLinkageSpec())
      return SC_None;
    return SC_Extern;
  case DeclSpec::SCS_static:         return SC_Static;
  case DeclSpec::SCS_auto:           return SC_Auto;
  case DeclSpec::SCS_register:       return SC_Register;
  case DeclSpec::SCS_private_extern: return SC_PrivateExtern;
    // Illegal SCSs map to None: error reporting is up to the caller.
  case DeclSpec::SCS_mutable:        // Fall through.
  case DeclSpec::SCS_typedef:        return SC_None;
  }
  llvm_unreachable("unknown storage class specifier");
}

static SourceLocation findDefaultInitializer(const CXXRecordDecl *Record) {
  assert(Record->hasInClassInitializer());

  for (const auto *I : Record->decls()) {
    const auto *FD = dyn_cast<FieldDecl>(I);
    if (const auto *IFD = dyn_cast<IndirectFieldDecl>(I))
      FD = IFD->getAnonField();
    if (FD && FD->hasInClassInitializer())
      return FD->getLocation();
  }

  llvm_unreachable("couldn't find in-class initializer");
}

static void checkDuplicateDefaultInit(Sema &S, CXXRecordDecl *Parent,
                                      SourceLocation DefaultInitLoc) {
  if (!Parent->isUnion() || !Parent->hasInClassInitializer())
    return;

  S.Diag(DefaultInitLoc, diag::err_multiple_mem_union_initialization);
  S.Diag(findDefaultInitializer(Parent), diag::note_previous_initializer) << 0;
}

static void checkDuplicateDefaultInit(Sema &S, CXXRecordDecl *Parent,
                                      CXXRecordDecl *AnonUnion) {
  if (!Parent->isUnion() || !Parent->hasInClassInitializer())
    return;

  checkDuplicateDefaultInit(S, Parent, findDefaultInitializer(AnonUnion));
}

Decl *Sema::BuildAnonymousStructOrUnion(Scope *S, DeclSpec &DS,
                                        AccessSpecifier AS,
                                        RecordDecl *Record,
                                        const PrintingPolicy &Policy) {
  DeclContext *Owner = Record->getDeclContext();

  // Diagnose whether this anonymous struct/union is an extension.
  if (Record->isUnion() && !getLangOpts().CPlusPlus && !getLangOpts().C11)
    Diag(Record->getLocation(), diag::ext_anonymous_union);
  else if (!Record->isUnion() && getLangOpts().CPlusPlus)
    Diag(Record->getLocation(), diag::ext_gnu_anonymous_struct);
  else if (!Record->isUnion() && !getLangOpts().C11)
    Diag(Record->getLocation(), diag::ext_c11_anonymous_struct);

  // C and C++ require different kinds of checks for anonymous
  // structs/unions.
  bool Invalid = false;
  if (getLangOpts().CPlusPlus) {
    const char *PrevSpec = nullptr;
    if (Record->isUnion()) {
      // C++ [class.union]p6:
      // C++17 [class.union.anon]p2:
      //   Anonymous unions declared in a named namespace or in the
      //   global namespace shall be declared static.
      unsigned DiagID;
      DeclContext *OwnerScope = Owner->getRedeclContext();
      if (DS.getStorageClassSpec() != DeclSpec::SCS_static &&
          (OwnerScope->isTranslationUnit() ||
           (OwnerScope->isNamespace() &&
            !cast<NamespaceDecl>(OwnerScope)->isAnonymousNamespace()))) {
        Diag(Record->getLocation(), diag::err_anonymous_union_not_static)
          << FixItHint::CreateInsertion(Record->getLocation(), "static ");

        // Recover by adding 'static'.
        DS.SetStorageClassSpec(*this, DeclSpec::SCS_static, SourceLocation(),
                               PrevSpec, DiagID, Policy);
      }
      // C++ [class.union]p6:
      //   A storage class is not allowed in a declaration of an
      //   anonymous union in a class scope.
      else if (DS.getStorageClassSpec() != DeclSpec::SCS_unspecified &&
               isa<RecordDecl>(Owner)) {
        Diag(DS.getStorageClassSpecLoc(),
             diag::err_anonymous_union_with_storage_spec)
          << FixItHint::CreateRemoval(DS.getStorageClassSpecLoc());

        // Recover by removing the storage specifier.
        DS.SetStorageClassSpec(*this, DeclSpec::SCS_unspecified,
                               SourceLocation(),
                               PrevSpec, DiagID, Context.getPrintingPolicy());
      }
    }

    // Ignore const/volatile/restrict qualifiers.
    if (DS.getTypeQualifiers()) {
      if (DS.getTypeQualifiers() & DeclSpec::TQ_const)
        Diag(DS.getConstSpecLoc(), diag::ext_anonymous_struct_union_qualified)
          << Record->isUnion() << "const"
          << FixItHint::CreateRemoval(DS.getConstSpecLoc());
      if (DS.getTypeQualifiers() & DeclSpec::TQ_volatile)
        Diag(DS.getVolatileSpecLoc(),
             diag::ext_anonymous_struct_union_qualified)
          << Record->isUnion() << "volatile"
          << FixItHint::CreateRemoval(DS.getVolatileSpecLoc());
      if (DS.getTypeQualifiers() & DeclSpec::TQ_restrict)
        Diag(DS.getRestrictSpecLoc(),
             diag::ext_anonymous_struct_union_qualified)
          << Record->isUnion() << "restrict"
          << FixItHint::CreateRemoval(DS.getRestrictSpecLoc());
      if (DS.getTypeQualifiers() & DeclSpec::TQ_atomic)
        Diag(DS.getAtomicSpecLoc(),
             diag::ext_anonymous_struct_union_qualified)
          << Record->isUnion() << "_Atomic"
          << FixItHint::CreateRemoval(DS.getAtomicSpecLoc());
      if (DS.getTypeQualifiers() & DeclSpec::TQ_unaligned)
        Diag(DS.getUnalignedSpecLoc(),
             diag::ext_anonymous_struct_union_qualified)
          << Record->isUnion() << "__unaligned"
          << FixItHint::CreateRemoval(DS.getUnalignedSpecLoc());

      DS.ClearTypeQualifiers();
    }

    // C++ [class.union]p2:
    //   The member-specification of an anonymous union shall only
    //   define non-static data members. [Note: nested types and
    //   functions cannot be declared within an anonymous union. ]
    for (auto *Mem : Record->decls()) {
      // Ignore invalid declarations; we already diagnosed them.
      if (Mem->isInvalidDecl())
        continue;

      if (auto *FD = dyn_cast<FieldDecl>(Mem)) {
        // C++ [class.union]p3:
        //   An anonymous union shall not have private or protected
        //   members (clause 11).
        assert(FD->getAccess() != AS_none);
        if (FD->getAccess() != AS_public) {
          Diag(FD->getLocation(), diag::err_anonymous_record_nonpublic_member)
            << Record->isUnion() << (FD->getAccess() == AS_protected);
          Invalid = true;
        }

        // C++ [class.union]p1
        //   An object of a class with a non-trivial constructor, a non-trivial
        //   copy constructor, a non-trivial destructor, or a non-trivial copy
        //   assignment operator cannot be a member of a union, nor can an
        //   array of such objects.
        if (CheckNontrivialField(FD))
          Invalid = true;
      } else if (Mem->isImplicit()) {
        // Any implicit members are fine.
      } else if (isa<TagDecl>(Mem) && Mem->getDeclContext() != Record) {
        // This is a type that showed up in an
        // elaborated-type-specifier inside the anonymous struct or
        // union, but which actually declares a type outside of the
        // anonymous struct or union. It's okay.
      } else if (auto *MemRecord = dyn_cast<RecordDecl>(Mem)) {
        if (!MemRecord->isAnonymousStructOrUnion() &&
            MemRecord->getDeclName()) {
          // Visual C++ allows type definition in anonymous struct or union.
          if (getLangOpts().MicrosoftExt)
            Diag(MemRecord->getLocation(), diag::ext_anonymous_record_with_type)
              << Record->isUnion();
          else {
            // This is a nested type declaration.
            Diag(MemRecord->getLocation(), diag::err_anonymous_record_with_type)
              << Record->isUnion();
            Invalid = true;
          }
        } else {
          // This is an anonymous type definition within another anonymous type.
          // This is a popular extension, provided by Plan9, MSVC and GCC, but
          // not part of standard C++.
          Diag(MemRecord->getLocation(),
               diag::ext_anonymous_record_with_anonymous_type)
            << Record->isUnion();
        }
      } else if (isa<AccessSpecDecl>(Mem)) {
        // Any access specifier is fine.
      } else if (isa<StaticAssertDecl>(Mem)) {
        // In C++1z, static_assert declarations are also fine.
      } else {
        // We have something that isn't a non-static data
        // member. Complain about it.
        unsigned DK = diag::err_anonymous_record_bad_member;
        if (isa<TypeDecl>(Mem))
          DK = diag::err_anonymous_record_with_type;
        else if (isa<FunctionDecl>(Mem))
          DK = diag::err_anonymous_record_with_function;
        else if (isa<VarDecl>(Mem))
          DK = diag::err_anonymous_record_with_static;

        // Visual C++ allows type definition in anonymous struct or union.
        if (getLangOpts().MicrosoftExt &&
            DK == diag::err_anonymous_record_with_type)
          Diag(Mem->getLocation(), diag::ext_anonymous_record_with_type)
            << Record->isUnion();
        else {
          Diag(Mem->getLocation(), DK) << Record->isUnion();
          Invalid = true;
        }
      }
    }

    // C++11 [class.union]p8 (DR1460):
    //   At most one variant member of a union may have a
    //   brace-or-equal-initializer.
    if (cast<CXXRecordDecl>(Record)->hasInClassInitializer() &&
        Owner->isRecord())
      checkDuplicateDefaultInit(*this, cast<CXXRecordDecl>(Owner),
                                cast<CXXRecordDecl>(Record));
  }

  if (!Record->isUnion() && !Owner->isRecord()) {
    Diag(Record->getLocation(), diag::err_anonymous_struct_not_member)
      << getLangOpts().CPlusPlus;
    Invalid = true;
  }

  // C++ [dcl.dcl]p3:
  //   [If there are no declarators], and except for the declaration of an
  //   unnamed bit-field, the decl-specifier-seq shall introduce one or more
  //   names into the program
  // C++ [class.mem]p2:
  //   each such member-declaration shall either declare at least one member
  //   name of the class or declare at least one unnamed bit-field
  //
  // For C this is an error even for a named struct, and is diagnosed elsewhere.
  if (getLangOpts().CPlusPlus && Record->field_empty())
    Diag(DS.getBeginLoc(), diag::ext_no_declarators) << DS.getSourceRange();

  // Mock up a declarator.
  Declarator Dc(DS, ParsedAttributesView::none(), DeclaratorContext::Member);
  StorageClass SC = StorageClassSpecToVarDeclStorageClass(DS);
  TypeSourceInfo *TInfo = GetTypeForDeclarator(Dc);
  assert(TInfo && "couldn't build declarator info for anonymous struct/union");

  // Create a declaration for this anonymous struct/union.
  NamedDecl *Anon = nullptr;
  if (RecordDecl *OwningClass = dyn_cast<RecordDecl>(Owner)) {
    Anon = FieldDecl::Create(
        Context, OwningClass, DS.getBeginLoc(), Record->getLocation(),
        /*IdentifierInfo=*/nullptr, Context.getTypeDeclType(Record), TInfo,
        /*BitWidth=*/nullptr, /*Mutable=*/false,
        /*InitStyle=*/ICIS_NoInit);
    Anon->setAccess(AS);
    ProcessDeclAttributes(S, Anon, Dc);

    if (getLangOpts().CPlusPlus)
      FieldCollector->Add(cast<FieldDecl>(Anon));
  } else {
    DeclSpec::SCS SCSpec = DS.getStorageClassSpec();
    if (SCSpec == DeclSpec::SCS_mutable) {
      // mutable can only appear on non-static class members, so it's always
      // an error here
      Diag(Record->getLocation(), diag::err_mutable_nonmember);
      Invalid = true;
      SC = SC_None;
    }

    Anon = VarDecl::Create(Context, Owner, DS.getBeginLoc(),
                           Record->getLocation(), /*IdentifierInfo=*/nullptr,
                           Context.getTypeDeclType(Record), TInfo, SC);
    if (Invalid)
      Anon->setInvalidDecl();

    ProcessDeclAttributes(S, Anon, Dc);

    // Default-initialize the implicit variable. This initialization will be
    // trivial in almost all cases, except if a union member has an in-class
    // initializer:
    //   union { int n = 0; };
    ActOnUninitializedDecl(Anon);
  }
  Anon->setImplicit();

  // Mark this as an anonymous struct/union type.
  Record->setAnonymousStructOrUnion(true);

  // Add the anonymous struct/union object to the current
  // context. We'll be referencing this object when we refer to one of
  // its members.
  Owner->addDecl(Anon);

  // Inject the members of the anonymous struct/union into the owning
  // context and into the identifier resolver chain for name lookup
  // purposes.
  SmallVector<NamedDecl*, 2> Chain;
  Chain.push_back(Anon);

  if (InjectAnonymousStructOrUnionMembers(*this, S, Owner, Record, AS, SC,
                                          Chain))
    Invalid = true;

  if (VarDecl *NewVD = dyn_cast<VarDecl>(Anon)) {
    if (getLangOpts().CPlusPlus && NewVD->isStaticLocal()) {
      MangleNumberingContext *MCtx;
      Decl *ManglingContextDecl;
      std::tie(MCtx, ManglingContextDecl) =
          getCurrentMangleNumberContext(NewVD->getDeclContext());
      if (MCtx) {
        Context.setManglingNumber(
            NewVD, MCtx->getManglingNumber(
                       NewVD, getMSManglingNumber(getLangOpts(), S)));
        Context.setStaticLocalNumber(NewVD, MCtx->getStaticLocalNumber(NewVD));
      }
    }
  }

  if (Invalid)
    Anon->setInvalidDecl();

  return Anon;
}

Decl *Sema::BuildMicrosoftCAnonymousStruct(Scope *S, DeclSpec &DS,
                                           RecordDecl *Record) {
  assert(Record && "expected a record!");

  // Mock up a declarator.
  Declarator Dc(DS, ParsedAttributesView::none(), DeclaratorContext::TypeName);
  TypeSourceInfo *TInfo = GetTypeForDeclarator(Dc);
  assert(TInfo && "couldn't build declarator info for anonymous struct");

  auto *ParentDecl = cast<RecordDecl>(CurContext);
  QualType RecTy = Context.getTypeDeclType(Record);

  // Create a declaration for this anonymous struct.
  NamedDecl *Anon =
      FieldDecl::Create(Context, ParentDecl, DS.getBeginLoc(), DS.getBeginLoc(),
                        /*IdentifierInfo=*/nullptr, RecTy, TInfo,
                        /*BitWidth=*/nullptr, /*Mutable=*/false,
                        /*InitStyle=*/ICIS_NoInit);
  Anon->setImplicit();

  // Add the anonymous struct object to the current context.
  CurContext->addDecl(Anon);

  // Inject the members of the anonymous struct into the current
  // context and into the identifier resolver chain for name lookup
  // purposes.
  SmallVector<NamedDecl*, 2> Chain;
  Chain.push_back(Anon);

  RecordDecl *RecordDef = Record->getDefinition();
  if (RequireCompleteSizedType(Anon->getLocation(), RecTy,
                               diag::err_field_incomplete_or_sizeless) ||
      InjectAnonymousStructOrUnionMembers(
          *this, S, CurContext, RecordDef, AS_none,
          StorageClassSpecToVarDeclStorageClass(DS), Chain)) {
    Anon->setInvalidDecl();
    ParentDecl->setInvalidDecl();
  }

  return Anon;
}

DeclarationNameInfo Sema::GetNameForDeclarator(Declarator &D) {
  return GetNameFromUnqualifiedId(D.getName());
}

DeclarationNameInfo
Sema::GetNameFromUnqualifiedId(const UnqualifiedId &Name) {
  DeclarationNameInfo NameInfo;
  NameInfo.setLoc(Name.StartLocation);

  switch (Name.getKind()) {

  case UnqualifiedIdKind::IK_ImplicitSelfParam:
  case UnqualifiedIdKind::IK_Identifier:
    NameInfo.setName(Name.Identifier);
    return NameInfo;

  case UnqualifiedIdKind::IK_DeductionGuideName: {
    // C++ [temp.deduct.guide]p3:
    //   The simple-template-id shall name a class template specialization.
    //   The template-name shall be the same identifier as the template-name
    //   of the simple-template-id.
    // These together intend to imply that the template-name shall name a
    // class template.
    // FIXME: template<typename T> struct X {};
    //        template<typename T> using Y = X<T>;
    //        Y(int) -> Y<int>;
    //   satisfies these rules but does not name a class template.
    TemplateName TN = Name.TemplateName.get().get();
    auto *Template = TN.getAsTemplateDecl();
    if (!Template || !isa<ClassTemplateDecl>(Template)) {
      Diag(Name.StartLocation,
           diag::err_deduction_guide_name_not_class_template)
        << (int)getTemplateNameKindForDiagnostics(TN) << TN;
      if (Template)
        NoteTemplateLocation(*Template);
      return DeclarationNameInfo();
    }

    NameInfo.setName(
        Context.DeclarationNames.getCXXDeductionGuideName(Template));
    return NameInfo;
  }

  case UnqualifiedIdKind::IK_OperatorFunctionId:
    NameInfo.setName(Context.DeclarationNames.getCXXOperatorName(
                                           Name.OperatorFunctionId.Operator));
    NameInfo.setCXXOperatorNameRange(SourceRange(
        Name.OperatorFunctionId.SymbolLocations[0], Name.EndLocation));
    return NameInfo;

  case UnqualifiedIdKind::IK_LiteralOperatorId:
    NameInfo.setName(Context.DeclarationNames.getCXXLiteralOperatorName(
                                                           Name.Identifier));
    NameInfo.setCXXLiteralOperatorNameLoc(Name.EndLocation);
    return NameInfo;

  case UnqualifiedIdKind::IK_ConversionFunctionId: {
    TypeSourceInfo *TInfo;
    QualType Ty = GetTypeFromParser(Name.ConversionFunctionId, &TInfo);
    if (Ty.isNull())
      return DeclarationNameInfo();
    NameInfo.setName(Context.DeclarationNames.getCXXConversionFunctionName(
                                               Context.getCanonicalType(Ty)));
    NameInfo.setNamedTypeInfo(TInfo);
    return NameInfo;
  }

  case UnqualifiedIdKind::IK_ConstructorName: {
    TypeSourceInfo *TInfo;
    QualType Ty = GetTypeFromParser(Name.ConstructorName, &TInfo);
    if (Ty.isNull())
      return DeclarationNameInfo();
    NameInfo.setName(Context.DeclarationNames.getCXXConstructorName(
                                              Context.getCanonicalType(Ty)));
    NameInfo.setNamedTypeInfo(TInfo);
    return NameInfo;
  }

  case UnqualifiedIdKind::IK_ConstructorTemplateId: {
    // In well-formed code, we can only have a constructor
    // template-id that refers to the current context, so go there
    // to find the actual type being constructed.
    CXXRecordDecl *CurClass = dyn_cast<CXXRecordDecl>(CurContext);
    if (!CurClass || CurClass->getIdentifier() != Name.TemplateId->Name)
      return DeclarationNameInfo();

    // Determine the type of the class being constructed.
    QualType CurClassType = Context.getTypeDeclType(CurClass);

    // FIXME: Check two things: that the template-id names the same type as
    // CurClassType, and that the template-id does not occur when the name
    // was qualified.

    NameInfo.setName(Context.DeclarationNames.getCXXConstructorName(
                                    Context.getCanonicalType(CurClassType)));
    // FIXME: should we retrieve TypeSourceInfo?
    NameInfo.setNamedTypeInfo(nullptr);
    return NameInfo;
  }

  case UnqualifiedIdKind::IK_DestructorName: {
    TypeSourceInfo *TInfo;
    QualType Ty = GetTypeFromParser(Name.DestructorName, &TInfo);
    if (Ty.isNull())
      return DeclarationNameInfo();
    NameInfo.setName(Context.DeclarationNames.getCXXDestructorName(
                                              Context.getCanonicalType(Ty)));
    NameInfo.setNamedTypeInfo(TInfo);
    return NameInfo;
  }

  case UnqualifiedIdKind::IK_TemplateId: {
    TemplateName TName = Name.TemplateId->Template.get();
    SourceLocation TNameLoc = Name.TemplateId->TemplateNameLoc;
    return Context.getNameForTemplate(TName, TNameLoc);
  }

  } // switch (Name.getKind())

  llvm_unreachable("Unknown name kind");
}

static QualType getCoreType(QualType Ty) {
  do {
    if (Ty->isPointerType() || Ty->isReferenceType())
      Ty = Ty->getPointeeType();
    else if (Ty->isArrayType())
      Ty = Ty->castAsArrayTypeUnsafe()->getElementType();
    else
      return Ty.withoutLocalFastQualifiers();
  } while (true);
}

/// hasSimilarParameters - Determine whether the C++ functions Declaration
/// and Definition have "nearly" matching parameters. This heuristic is
/// used to improve diagnostics in the case where an out-of-line function
/// definition doesn't match any declaration within the class or namespace.
/// Also sets Params to the list of indices to the parameters that differ
/// between the declaration and the definition. If hasSimilarParameters
/// returns true and Params is empty, then all of the parameters match.
static bool hasSimilarParameters(ASTContext &Context,
                                     FunctionDecl *Declaration,
                                     FunctionDecl *Definition,
                                     SmallVectorImpl<unsigned> &Params) {
  Params.clear();
  if (Declaration->param_size() != Definition->param_size())
    return false;
  for (unsigned Idx = 0; Idx < Declaration->param_size(); ++Idx) {
    QualType DeclParamTy = Declaration->getParamDecl(Idx)->getType();
    QualType DefParamTy = Definition->getParamDecl(Idx)->getType();

    // The parameter types are identical
    if (Context.hasSameUnqualifiedType(DefParamTy, DeclParamTy))
      continue;

    QualType DeclParamBaseTy = getCoreType(DeclParamTy);
    QualType DefParamBaseTy = getCoreType(DefParamTy);
    const IdentifierInfo *DeclTyName = DeclParamBaseTy.getBaseTypeIdentifier();
    const IdentifierInfo *DefTyName = DefParamBaseTy.getBaseTypeIdentifier();

    if (Context.hasSameUnqualifiedType(DeclParamBaseTy, DefParamBaseTy) ||
        (DeclTyName && DeclTyName == DefTyName))
      Params.push_back(Idx);
    else  // The two parameters aren't even close
      return false;
  }

  return true;
}

/// RebuildDeclaratorInCurrentInstantiation - Checks whether the given
/// declarator needs to be rebuilt in the current instantiation.
/// Any bits of declarator which appear before the name are valid for
/// consideration here.  That's specifically the type in the decl spec
/// and the base type in any member-pointer chunks.
static bool RebuildDeclaratorInCurrentInstantiation(Sema &S, Declarator &D,
                                                    DeclarationName Name) {
  // The types we specifically need to rebuild are:
  //   - typenames, typeofs, and decltypes
  //   - types which will become injected class names
  // Of course, we also need to rebuild any type referencing such a
  // type.  It's safest to just say "dependent", but we call out a
  // few cases here.

  DeclSpec &DS = D.getMutableDeclSpec();
  switch (DS.getTypeSpecType()) {
  case DeclSpec::TST_typename:
  case DeclSpec::TST_typeofType:
  case DeclSpec::TST_typeof_unqualType:
#define TRANSFORM_TYPE_TRAIT_DEF(_, Trait) case DeclSpec::TST_##Trait:
#include "clang/Basic/TransformTypeTraits.def"
  case DeclSpec::TST_atomic: {
    // Grab the type from the parser.
    TypeSourceInfo *TSI = nullptr;
    QualType T = S.GetTypeFromParser(DS.getRepAsType(), &TSI);
    if (T.isNull() || !T->isInstantiationDependentType()) break;

    // Make sure there's a type source info.  This isn't really much
    // of a waste; most dependent types should have type source info
    // attached already.
    if (!TSI)
      TSI = S.Context.getTrivialTypeSourceInfo(T, DS.getTypeSpecTypeLoc());

    // Rebuild the type in the current instantiation.
    TSI = S.RebuildTypeInCurrentInstantiation(TSI, D.getIdentifierLoc(), Name);
    if (!TSI) return true;

    // Store the new type back in the decl spec.
    ParsedType LocType = S.CreateParsedType(TSI->getType(), TSI);
    DS.UpdateTypeRep(LocType);
    break;
  }

  case DeclSpec::TST_decltype:
  case DeclSpec::TST_typeof_unqualExpr:
  case DeclSpec::TST_typeofExpr: {
    Expr *E = DS.getRepAsExpr();
    ExprResult Result = S.RebuildExprInCurrentInstantiation(E);
    if (Result.isInvalid()) return true;
    DS.UpdateExprRep(Result.get());
    break;
  }

  default:
    // Nothing to do for these decl specs.
    break;
  }

  // It doesn't matter what order we do this in.
  for (unsigned I = 0, E = D.getNumTypeObjects(); I != E; ++I) {
    DeclaratorChunk &Chunk = D.getTypeObject(I);

    // The only type information in the declarator which can come
    // before the declaration name is the base type of a member
    // pointer.
    if (Chunk.Kind != DeclaratorChunk::MemberPointer)
      continue;

    // Rebuild the scope specifier in-place.
    CXXScopeSpec &SS = Chunk.Mem.Scope();
    if (S.RebuildNestedNameSpecifierInCurrentInstantiation(SS))
      return true;
  }

  return false;
}

/// Returns true if the declaration is declared in a system header or from a
/// system macro.
static bool isFromSystemHeader(SourceManager &SM, const Decl *D) {
  return SM.isInSystemHeader(D->getLocation()) ||
         SM.isInSystemMacro(D->getLocation());
}

void Sema::warnOnReservedIdentifier(const NamedDecl *D) {
  // Avoid warning twice on the same identifier, and don't warn on redeclaration
  // of system decl.
  if (D->getPreviousDecl() || D->isImplicit())
    return;
  ReservedIdentifierStatus Status = D->isReserved(getLangOpts());
  if (Status != ReservedIdentifierStatus::NotReserved &&
      !isFromSystemHeader(Context.getSourceManager(), D)) {
    Diag(D->getLocation(), diag::warn_reserved_extern_symbol)
        << D << static_cast<int>(Status);
  }
}

Decl *Sema::ActOnDeclarator(Scope *S, Declarator &D) {
  D.setFunctionDefinitionKind(FunctionDefinitionKind::Declaration);

  // Check if we are in an `omp begin/end declare variant` scope. Handle this
  // declaration only if the `bind_to_declaration` extension is set.
  SmallVector<FunctionDecl *, 4> Bases;
  if (LangOpts.OpenMP && OpenMP().isInOpenMPDeclareVariantScope())
    if (OpenMP().getOMPTraitInfoForSurroundingScope()->isExtensionActive(
            llvm::omp::TraitProperty::
                implementation_extension_bind_to_declaration))
      OpenMP().ActOnStartOfFunctionDefinitionInOpenMPDeclareVariantScope(
          S, D, MultiTemplateParamsArg(), Bases);

  Decl *Dcl = HandleDeclarator(S, D, MultiTemplateParamsArg());

  if (OriginalLexicalContext && OriginalLexicalContext->isObjCContainer() &&
      Dcl && Dcl->getDeclContext()->isFileContext())
    Dcl->setTopLevelDeclInObjCContainer();

  if (!Bases.empty())
    OpenMP().ActOnFinishedFunctionDefinitionInOpenMPDeclareVariantScope(Dcl,
                                                                        Bases);

  return Dcl;
}

bool Sema::DiagnoseClassNameShadow(DeclContext *DC,
                                   DeclarationNameInfo NameInfo) {
  DeclarationName Name = NameInfo.getName();

  CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(DC);
  while (Record && Record->isAnonymousStructOrUnion())
    Record = dyn_cast<CXXRecordDecl>(Record->getParent());
  if (Record && Record->getIdentifier() && Record->getDeclName() == Name) {
    Diag(NameInfo.getLoc(), diag::err_member_name_of_class) << Name;
    return true;
  }

  return false;
}

bool Sema::diagnoseQualifiedDeclaration(CXXScopeSpec &SS, DeclContext *DC,
                                        DeclarationName Name,
                                        SourceLocation Loc,
                                        TemplateIdAnnotation *TemplateId,
                                        bool IsMemberSpecialization) {
  assert(SS.isValid() && "diagnoseQualifiedDeclaration called for declaration "
                         "without nested-name-specifier");
  DeclContext *Cur = CurContext;
  while (isa<LinkageSpecDecl>(Cur) || isa<CapturedDecl>(Cur))
    Cur = Cur->getParent();

  // If the user provided a superfluous scope specifier that refers back to the
  // class in which the entity is already declared, diagnose and ignore it.
  //
  // class X {
  //   void X::f();
  // };
  //
  // Note, it was once ill-formed to give redundant qualification in all
  // contexts, but that rule was removed by DR482.
  if (Cur->Equals(DC)) {
    if (Cur->isRecord()) {
      Diag(Loc, LangOpts.MicrosoftExt ? diag::warn_member_extra_qualification
                                      : diag::err_member_extra_qualification)
        << Name << FixItHint::CreateRemoval(SS.getRange());
      SS.clear();
    } else {
      Diag(Loc, diag::warn_namespace_member_extra_qualification) << Name;
    }
    return false;
  }

  // Check whether the qualifying scope encloses the scope of the original
  // declaration. For a template-id, we perform the checks in
  // CheckTemplateSpecializationScope.
  if (!Cur->Encloses(DC) && !(TemplateId || IsMemberSpecialization)) {
    if (Cur->isRecord())
      Diag(Loc, diag::err_member_qualification)
        << Name << SS.getRange();
    else if (isa<TranslationUnitDecl>(DC))
      Diag(Loc, diag::err_invalid_declarator_global_scope)
        << Name << SS.getRange();
    else if (isa<FunctionDecl>(Cur))
      Diag(Loc, diag::err_invalid_declarator_in_function)
        << Name << SS.getRange();
    else if (isa<BlockDecl>(Cur))
      Diag(Loc, diag::err_invalid_declarator_in_block)
        << Name << SS.getRange();
    else if (isa<ExportDecl>(Cur)) {
      if (!isa<NamespaceDecl>(DC))
        Diag(Loc, diag::err_export_non_namespace_scope_name)
            << Name << SS.getRange();
      else
        // The cases that DC is not NamespaceDecl should be handled in
        // CheckRedeclarationExported.
        return false;
    } else
      Diag(Loc, diag::err_invalid_declarator_scope)
      << Name << cast<NamedDecl>(Cur) << cast<NamedDecl>(DC) << SS.getRange();

    return true;
  }

  if (Cur->isRecord()) {
    // Cannot qualify members within a class.
    Diag(Loc, diag::err_member_qualification)
      << Name << SS.getRange();
    SS.clear();

    // C++ constructors and destructors with incorrect scopes can break
    // our AST invariants by having the wrong underlying types. If
    // that's the case, then drop this declaration entirely.
    if ((Name.getNameKind() == DeclarationName::CXXConstructorName ||
         Name.getNameKind() == DeclarationName::CXXDestructorName) &&
        !Context.hasSameType(Name.getCXXNameType(),
                             Context.getTypeDeclType(cast<CXXRecordDecl>(Cur))))
      return true;

    return false;
  }

  // C++23 [temp.names]p5:
  //   The keyword template shall not appear immediately after a declarative
  //   nested-name-specifier.
  //
  // First check the template-id (if any), and then check each component of the
  // nested-name-specifier in reverse order.
  //
  // FIXME: nested-name-specifiers in friend declarations are declarative,
  // but we don't call diagnoseQualifiedDeclaration for them. We should.
  if (TemplateId && TemplateId->TemplateKWLoc.isValid())
    Diag(Loc, diag::ext_template_after_declarative_nns)
        << FixItHint::CreateRemoval(TemplateId->TemplateKWLoc);

  NestedNameSpecifierLoc SpecLoc(SS.getScopeRep(), SS.location_data());
  do {
    if (SpecLoc.getNestedNameSpecifier()->getKind() ==
        NestedNameSpecifier::TypeSpecWithTemplate)
      Diag(Loc, diag::ext_template_after_declarative_nns)
          << FixItHint::CreateRemoval(
                 SpecLoc.getTypeLoc().getTemplateKeywordLoc());

    if (const Type *T = SpecLoc.getNestedNameSpecifier()->getAsType()) {
      if (const auto *TST = T->getAsAdjusted<TemplateSpecializationType>()) {
        // C++23 [expr.prim.id.qual]p3:
        //   [...] If a nested-name-specifier N is declarative and has a
        //   simple-template-id with a template argument list A that involves a
        //   template parameter, let T be the template nominated by N without A.
        //   T shall be a class template.
        if (TST->isDependentType() && TST->isTypeAlias())
          Diag(Loc, diag::ext_alias_template_in_declarative_nns)
              << SpecLoc.getLocalSourceRange();
      } else if (T->isDecltypeType() || T->getAsAdjusted<PackIndexingType>()) {
        // C++23 [expr.prim.id.qual]p2:
        //   [...] A declarative nested-name-specifier shall not have a
        //   computed-type-specifier.
        //
        // CWG2858 changed this from 'decltype-specifier' to
        // 'computed-type-specifier'.
        Diag(Loc, diag::err_computed_type_in_declarative_nns)
            << T->isDecltypeType() << SpecLoc.getTypeLoc().getSourceRange();
      }
    }
  } while ((SpecLoc = SpecLoc.getPrefix()));

  return false;
}

NamedDecl *Sema::HandleDeclarator(Scope *S, Declarator &D,
                                  MultiTemplateParamsArg TemplateParamLists) {
  // TODO: consider using NameInfo for diagnostic.
  DeclarationNameInfo NameInfo = GetNameForDeclarator(D);
  DeclarationName Name = NameInfo.getName();

  // All of these full declarators require an identifier.  If it doesn't have
  // one, the ParsedFreeStandingDeclSpec action should be used.
  if (D.isDecompositionDeclarator()) {
    return ActOnDecompositionDeclarator(S, D, TemplateParamLists);
  } else if (!Name) {
    if (!D.isInvalidType())  // Reject this if we think it is valid.
      Diag(D.getDeclSpec().getBeginLoc(), diag::err_declarator_need_ident)
          << D.getDeclSpec().getSourceRange() << D.getSourceRange();
    return nullptr;
  } else if (DiagnoseUnexpandedParameterPack(NameInfo, UPPC_DeclarationType))
    return nullptr;

  DeclContext *DC = CurContext;
  if (D.getCXXScopeSpec().isInvalid())
    D.setInvalidType();
  else if (D.getCXXScopeSpec().isSet()) {
    if (DiagnoseUnexpandedParameterPack(D.getCXXScopeSpec(),
                                        UPPC_DeclarationQualifier))
      return nullptr;

    bool EnteringContext = !D.getDeclSpec().isFriendSpecified();
    DC = computeDeclContext(D.getCXXScopeSpec(), EnteringContext);
    if (!DC || isa<EnumDecl>(DC)) {
      // If we could not compute the declaration context, it's because the
      // declaration context is dependent but does not refer to a class,
      // class template, or class template partial specialization. Complain
      // and return early, to avoid the coming semantic disaster.
      Diag(D.getIdentifierLoc(),
           diag::err_template_qualified_declarator_no_match)
        << D.getCXXScopeSpec().getScopeRep()
        << D.getCXXScopeSpec().getRange();
      return nullptr;
    }
    bool IsDependentContext = DC->isDependentContext();

    if (!IsDependentContext &&
        RequireCompleteDeclContext(D.getCXXScopeSpec(), DC))
      return nullptr;

    // If a class is incomplete, do not parse entities inside it.
    if (isa<CXXRecordDecl>(DC) && !cast<CXXRecordDecl>(DC)->hasDefinition()) {
      Diag(D.getIdentifierLoc(),
           diag::err_member_def_undefined_record)
        << Name << DC << D.getCXXScopeSpec().getRange();
      return nullptr;
    }
    if (!D.getDeclSpec().isFriendSpecified()) {
      TemplateIdAnnotation *TemplateId =
          D.getName().getKind() == UnqualifiedIdKind::IK_TemplateId
              ? D.getName().TemplateId
              : nullptr;
      if (diagnoseQualifiedDeclaration(D.getCXXScopeSpec(), DC, Name,
                                       D.getIdentifierLoc(), TemplateId,
                                       /*IsMemberSpecialization=*/false)) {
        if (DC->isRecord())
          return nullptr;

        D.setInvalidType();
      }
    }

    // Check whether we need to rebuild the type of the given
    // declaration in the current instantiation.
    if (EnteringContext && IsDependentContext &&
        TemplateParamLists.size() != 0) {
      ContextRAII SavedContext(*this, DC);
      if (RebuildDeclaratorInCurrentInstantiation(*this, D, Name))
        D.setInvalidType();
    }
  }

  TypeSourceInfo *TInfo = GetTypeForDeclarator(D);
  QualType R = TInfo->getType();

  if (DiagnoseUnexpandedParameterPack(D.getIdentifierLoc(), TInfo,
                                      UPPC_DeclarationType))
    D.setInvalidType();

  LookupResult Previous(*this, NameInfo, LookupOrdinaryName,
                        forRedeclarationInCurContext());

  // See if this is a redefinition of a variable in the same scope.
  if (!D.getCXXScopeSpec().isSet()) {
    bool IsLinkageLookup = false;
    bool CreateBuiltins = false;

    // If the declaration we're planning to build will be a function
    // or object with linkage, then look for another declaration with
    // linkage (C99 6.2.2p4-5 and C++ [basic.link]p6).
    //
    // If the declaration we're planning to build will be declared with
    // external linkage in the translation unit, create any builtin with
    // the same name.
    if (D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_typedef)
      /* Do nothing*/;
    else if (CurContext->isFunctionOrMethod() &&
             (D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_extern ||
              R->isFunctionType())) {
      IsLinkageLookup = true;
      CreateBuiltins =
          CurContext->getEnclosingNamespaceContext()->isTranslationUnit();
    } else if (CurContext->getRedeclContext()->isTranslationUnit() &&
               D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_static)
      CreateBuiltins = true;

    if (IsLinkageLookup) {
      Previous.clear(LookupRedeclarationWithLinkage);
      Previous.setRedeclarationKind(
          RedeclarationKind::ForExternalRedeclaration);
    }

    LookupName(Previous, S, CreateBuiltins);
  } else { // Something like "int foo::x;"
    LookupQualifiedName(Previous, DC);

    // C++ [dcl.meaning]p1:
    //   When the declarator-id is qualified, the declaration shall refer to a
    //  previously declared member of the class or namespace to which the
    //  qualifier refers (or, in the case of a namespace, of an element of the
    //  inline namespace set of that namespace (7.3.1)) or to a specialization
    //  thereof; [...]
    //
    // Note that we already checked the context above, and that we do not have
    // enough information to make sure that Previous contains the declaration
    // we want to match. For example, given:
    //
    //   class X {
    //     void f();
    //     void f(float);
    //   };
    //
    //   void X::f(int) { } // ill-formed
    //
    // In this case, Previous will point to the overload set
    // containing the two f's declared in X, but neither of them
    // matches.

    RemoveUsingDecls(Previous);
  }

  if (auto *TPD = Previous.getAsSingle<NamedDecl>();
      TPD && TPD->isTemplateParameter()) {
    // Older versions of clang allowed the names of function/variable templates
    // to shadow the names of their template parameters. For the compatibility
    // purposes we detect such cases and issue a default-to-error warning that
    // can be disabled with -Wno-strict-primary-template-shadow.
    if (!D.isInvalidType()) {
      bool AllowForCompatibility = false;
      if (Scope *DeclParent = S->getDeclParent();
          Scope *TemplateParamParent = S->getTemplateParamParent()) {
        AllowForCompatibility = DeclParent->Contains(*TemplateParamParent) &&
                                TemplateParamParent->isDeclScope(TPD);
      }
      DiagnoseTemplateParameterShadow(D.getIdentifierLoc(), TPD,
                                      AllowForCompatibility);
    }

    // Just pretend that we didn't see the previous declaration.
    Previous.clear();
  }

  if (!R->isFunctionType() && DiagnoseClassNameShadow(DC, NameInfo))
    // Forget that the previous declaration is the injected-class-name.
    Previous.clear();

  // In C++, the previous declaration we find might be a tag type
  // (class or enum). In this case, the new declaration will hide the
  // tag type. Note that this applies to functions, function templates, and
  // variables, but not to typedefs (C++ [dcl.typedef]p4) or variable templates.
  if (Previous.isSingleTagDecl() &&
      D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_typedef &&
      (TemplateParamLists.size() == 0 || R->isFunctionType()))
    Previous.clear();

  // Check that there are no default arguments other than in the parameters
  // of a function declaration (C++ only).
  if (getLangOpts().CPlusPlus)
    CheckExtraCXXDefaultArguments(D);

  /// Get the innermost enclosing declaration scope.
  S = S->getDeclParent();

  NamedDecl *New;

  bool AddToScope = true;
  if (D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_typedef) {
    if (TemplateParamLists.size()) {
      Diag(D.getIdentifierLoc(), diag::err_template_typedef);
      return nullptr;
    }

    New = ActOnTypedefDeclarator(S, D, DC, TInfo, Previous);
  } else if (R->isFunctionType()) {
    New = ActOnFunctionDeclarator(S, D, DC, TInfo, Previous,
                                  TemplateParamLists,
                                  AddToScope);
  } else {
    New = ActOnVariableDeclarator(S, D, DC, TInfo, Previous, TemplateParamLists,
                                  AddToScope);
  }

  if (!New)
    return nullptr;

  // If this has an identifier and is not a function template specialization,
  // add it to the scope stack.
  if (New->getDeclName() && AddToScope)
    PushOnScopeChains(New, S);

  if (OpenMP().isInOpenMPDeclareTargetContext())
    OpenMP().checkDeclIsAllowedInOpenMPTarget(nullptr, New);

  return New;
}

/// Helper method to turn variable array types into constant array
/// types in certain situations which would otherwise be errors (for
/// GCC compatibility).
static QualType TryToFixInvalidVariablyModifiedType(QualType T,
                                                    ASTContext &Context,
                                                    bool &SizeIsNegative,
                                                    llvm::APSInt &Oversized) {
  // This method tries to turn a variable array into a constant
  // array even when the size isn't an ICE.  This is necessary
  // for compatibility with code that depends on gcc's buggy
  // constant expression folding, like struct {char x[(int)(char*)2];}
  SizeIsNegative = false;
  Oversized = 0;

  if (T->isDependentType())
    return QualType();

  QualifierCollector Qs;
  const Type *Ty = Qs.strip(T);

  if (const PointerType* PTy = dyn_cast<PointerType>(Ty)) {
    QualType Pointee = PTy->getPointeeType();
    QualType FixedType =
        TryToFixInvalidVariablyModifiedType(Pointee, Context, SizeIsNegative,
                                            Oversized);
    if (FixedType.isNull()) return FixedType;
    FixedType = Context.getPointerType(FixedType);
    return Qs.apply(Context, FixedType);
  }
  if (const ParenType* PTy = dyn_cast<ParenType>(Ty)) {
    QualType Inner = PTy->getInnerType();
    QualType FixedType =
        TryToFixInvalidVariablyModifiedType(Inner, Context, SizeIsNegative,
                                            Oversized);
    if (FixedType.isNull()) return FixedType;
    FixedType = Context.getParenType(FixedType);
    return Qs.apply(Context, FixedType);
  }

  const VariableArrayType* VLATy = dyn_cast<VariableArrayType>(T);
  if (!VLATy)
    return QualType();

  QualType ElemTy = VLATy->getElementType();
  if (ElemTy->isVariablyModifiedType()) {
    ElemTy = TryToFixInvalidVariablyModifiedType(ElemTy, Context,
                                                 SizeIsNegative, Oversized);
    if (ElemTy.isNull())
      return QualType();
  }

  Expr::EvalResult Result;
  if (!VLATy->getSizeExpr() ||
      !VLATy->getSizeExpr()->EvaluateAsInt(Result, Context))
    return QualType();

  llvm::APSInt Res = Result.Val.getInt();

  // Check whether the array size is negative.
  if (Res.isSigned() && Res.isNegative()) {
    SizeIsNegative = true;
    return QualType();
  }

  // Check whether the array is too large to be addressed.
  unsigned ActiveSizeBits =
      (!ElemTy->isDependentType() && !ElemTy->isVariablyModifiedType() &&
       !ElemTy->isIncompleteType() && !ElemTy->isUndeducedType())
          ? ConstantArrayType::getNumAddressingBits(Context, ElemTy, Res)
          : Res.getActiveBits();
  if (ActiveSizeBits > ConstantArrayType::getMaxSizeBits(Context)) {
    Oversized = Res;
    return QualType();
  }

  QualType FoldedArrayType = Context.getConstantArrayType(
      ElemTy, Res, VLATy->getSizeExpr(), ArraySizeModifier::Normal, 0);
  return Qs.apply(Context, FoldedArrayType);
}

static void
FixInvalidVariablyModifiedTypeLoc(TypeLoc SrcTL, TypeLoc DstTL) {
  SrcTL = SrcTL.getUnqualifiedLoc();
  DstTL = DstTL.getUnqualifiedLoc();
  if (PointerTypeLoc SrcPTL = SrcTL.getAs<PointerTypeLoc>()) {
    PointerTypeLoc DstPTL = DstTL.castAs<PointerTypeLoc>();
    FixInvalidVariablyModifiedTypeLoc(SrcPTL.getPointeeLoc(),
                                      DstPTL.getPointeeLoc());
    DstPTL.setStarLoc(SrcPTL.getStarLoc());
    return;
  }
  if (ParenTypeLoc SrcPTL = SrcTL.getAs<ParenTypeLoc>()) {
    ParenTypeLoc DstPTL = DstTL.castAs<ParenTypeLoc>();
    FixInvalidVariablyModifiedTypeLoc(SrcPTL.getInnerLoc(),
                                      DstPTL.getInnerLoc());
    DstPTL.setLParenLoc(SrcPTL.getLParenLoc());
    DstPTL.setRParenLoc(SrcPTL.getRParenLoc());
    return;
  }
  ArrayTypeLoc SrcATL = SrcTL.castAs<ArrayTypeLoc>();
  ArrayTypeLoc DstATL = DstTL.castAs<ArrayTypeLoc>();
  TypeLoc SrcElemTL = SrcATL.getElementLoc();
  TypeLoc DstElemTL = DstATL.getElementLoc();
  if (VariableArrayTypeLoc SrcElemATL =
          SrcElemTL.getAs<VariableArrayTypeLoc>()) {
    ConstantArrayTypeLoc DstElemATL = DstElemTL.castAs<ConstantArrayTypeLoc>();
    FixInvalidVariablyModifiedTypeLoc(SrcElemATL, DstElemATL);
  } else {
    DstElemTL.initializeFullCopy(SrcElemTL);
  }
  DstATL.setLBracketLoc(SrcATL.getLBracketLoc());
  DstATL.setSizeExpr(SrcATL.getSizeExpr());
  DstATL.setRBracketLoc(SrcATL.getRBracketLoc());
}

/// Helper method to turn variable array types into constant array
/// types in certain situations which would otherwise be errors (for
/// GCC compatibility).
static TypeSourceInfo*
TryToFixInvalidVariablyModifiedTypeSourceInfo(TypeSourceInfo *TInfo,
                                              ASTContext &Context,
                                              bool &SizeIsNegative,
                                              llvm::APSInt &Oversized) {
  QualType FixedTy
    = TryToFixInvalidVariablyModifiedType(TInfo->getType(), Context,
                                          SizeIsNegative, Oversized);
  if (FixedTy.isNull())
    return nullptr;
  TypeSourceInfo *FixedTInfo = Context.getTrivialTypeSourceInfo(FixedTy);
  FixInvalidVariablyModifiedTypeLoc(TInfo->getTypeLoc(),
                                    FixedTInfo->getTypeLoc());
  return FixedTInfo;
}

bool Sema::tryToFixVariablyModifiedVarType(TypeSourceInfo *&TInfo,
                                           QualType &T, SourceLocation Loc,
                                           unsigned FailedFoldDiagID) {
  bool SizeIsNegative;
  llvm::APSInt Oversized;
  TypeSourceInfo *FixedTInfo = TryToFixInvalidVariablyModifiedTypeSourceInfo(
      TInfo, Context, SizeIsNegative, Oversized);
  if (FixedTInfo) {
    Diag(Loc, diag::ext_vla_folded_to_constant);
    TInfo = FixedTInfo;
    T = FixedTInfo->getType();
    return true;
  }

  if (SizeIsNegative)
    Diag(Loc, diag::err_typecheck_negative_array_size);
  else if (Oversized.getBoolValue())
    Diag(Loc, diag::err_array_too_large) << toString(Oversized, 10);
  else if (FailedFoldDiagID)
    Diag(Loc, FailedFoldDiagID);
  return false;
}

void
Sema::RegisterLocallyScopedExternCDecl(NamedDecl *ND, Scope *S) {
  if (!getLangOpts().CPlusPlus &&
      ND->getLexicalDeclContext()->getRedeclContext()->isTranslationUnit())
    // Don't need to track declarations in the TU in C.
    return;

  // Note that we have a locally-scoped external with this name.
  Context.getExternCContextDecl()->makeDeclVisibleInContext(ND);
}

NamedDecl *Sema::findLocallyScopedExternCDecl(DeclarationName Name) {
  // FIXME: We can have multiple results via __attribute__((overloadable)).
  auto Result = Context.getExternCContextDecl()->lookup(Name);
  return Result.empty() ? nullptr : *Result.begin();
}

void Sema::DiagnoseFunctionSpecifiers(const DeclSpec &DS) {
  // FIXME: We should probably indicate the identifier in question to avoid
  // confusion for constructs like "virtual int a(), b;"
  if (DS.isVirtualSpecified())
    Diag(DS.getVirtualSpecLoc(),
         diag::err_virtual_non_function);

  if (DS.hasExplicitSpecifier())
    Diag(DS.getExplicitSpecLoc(),
         diag::err_explicit_non_function);

  if (DS.isNoreturnSpecified())
    Diag(DS.getNoreturnSpecLoc(),
         diag::err_noreturn_non_function);
}

NamedDecl*
Sema::ActOnTypedefDeclarator(Scope* S, Declarator& D, DeclContext* DC,
                             TypeSourceInfo *TInfo, LookupResult &Previous) {
  // Typedef declarators cannot be qualified (C++ [dcl.meaning]p1).
  if (D.getCXXScopeSpec().isSet()) {
    Diag(D.getIdentifierLoc(), diag::err_qualified_typedef_declarator)
      << D.getCXXScopeSpec().getRange();
    D.setInvalidType();
    // Pretend we didn't see the scope specifier.
    DC = CurContext;
    Previous.clear();
  }

  DiagnoseFunctionSpecifiers(D.getDeclSpec());

  if (D.getDeclSpec().isInlineSpecified())
    Diag(D.getDeclSpec().getInlineSpecLoc(), diag::err_inline_non_function)
        << getLangOpts().CPlusPlus17;
  if (D.getDeclSpec().hasConstexprSpecifier())
    Diag(D.getDeclSpec().getConstexprSpecLoc(), diag::err_invalid_constexpr)
        << 1 << static_cast<int>(D.getDeclSpec().getConstexprSpecifier());

  if (D.getName().getKind() != UnqualifiedIdKind::IK_Identifier) {
    if (D.getName().getKind() == UnqualifiedIdKind::IK_DeductionGuideName)
      Diag(D.getName().StartLocation,
           diag::err_deduction_guide_invalid_specifier)
          << "typedef";
    else
      Diag(D.getName().StartLocation, diag::err_typedef_not_identifier)
          << D.getName().getSourceRange();
    return nullptr;
  }

  TypedefDecl *NewTD = ParseTypedefDecl(S, D, TInfo->getType(), TInfo);
  if (!NewTD) return nullptr;

  // Handle attributes prior to checking for duplicates in MergeVarDecl
  ProcessDeclAttributes(S, NewTD, D);

  CheckTypedefForVariablyModifiedType(S, NewTD);

  bool Redeclaration = D.isRedeclaration();
  NamedDecl *ND = ActOnTypedefNameDecl(S, DC, NewTD, Previous, Redeclaration);
  D.setRedeclaration(Redeclaration);
  return ND;
}

void
Sema::CheckTypedefForVariablyModifiedType(Scope *S, TypedefNameDecl *NewTD) {
  // C99 6.7.7p2: If a typedef name specifies a variably modified type
  // then it shall have block scope.
  // Note that variably modified types must be fixed before merging the decl so
  // that redeclarations will match.
  TypeSourceInfo *TInfo = NewTD->getTypeSourceInfo();
  QualType T = TInfo->getType();
  if (T->isVariablyModifiedType()) {
    setFunctionHasBranchProtectedScope();

    if (S->getFnParent() == nullptr) {
      bool SizeIsNegative;
      llvm::APSInt Oversized;
      TypeSourceInfo *FixedTInfo =
        TryToFixInvalidVariablyModifiedTypeSourceInfo(TInfo, Context,
                                                      SizeIsNegative,
                                                      Oversized);
      if (FixedTInfo) {
        Diag(NewTD->getLocation(), diag::ext_vla_folded_to_constant);
        NewTD->setTypeSourceInfo(FixedTInfo);
      } else {
        if (SizeIsNegative)
          Diag(NewTD->getLocation(), diag::err_typecheck_negative_array_size);
        else if (T->isVariableArrayType())
          Diag(NewTD->getLocation(), diag::err_vla_decl_in_file_scope);
        else if (Oversized.getBoolValue())
          Diag(NewTD->getLocation(), diag::err_array_too_large)
            << toString(Oversized, 10);
        else
          Diag(NewTD->getLocation(), diag::err_vm_decl_in_file_scope);
        NewTD->setInvalidDecl();
      }
    }
  }
}

NamedDecl*
Sema::ActOnTypedefNameDecl(Scope *S, DeclContext *DC, TypedefNameDecl *NewTD,
                           LookupResult &Previous, bool &Redeclaration) {

  // Find the shadowed declaration before filtering for scope.
  NamedDecl *ShadowedDecl = getShadowedDeclaration(NewTD, Previous);

  // Merge the decl with the existing one if appropriate. If the decl is
  // in an outer scope, it isn't the same thing.
  FilterLookupForScope(Previous, DC, S, /*ConsiderLinkage*/false,
                       /*AllowInlineNamespace*/false);
  filterNonConflictingPreviousTypedefDecls(*this, NewTD, Previous);
  if (!Previous.empty()) {
    Redeclaration = true;
    MergeTypedefNameDecl(S, NewTD, Previous);
  } else {
    inferGslPointerAttribute(NewTD);
  }

  if (ShadowedDecl && !Redeclaration)
    CheckShadow(NewTD, ShadowedDecl, Previous);

  // If this is the C FILE type, notify the AST context.
  if (IdentifierInfo *II = NewTD->getIdentifier())
    if (!NewTD->isInvalidDecl() &&
        NewTD->getDeclContext()->getRedeclContext()->isTranslationUnit()) {
      switch (II->getNotableIdentifierID()) {
      case tok::NotableIdentifierKind::FILE:
        Context.setFILEDecl(NewTD);
        break;
      case tok::NotableIdentifierKind::jmp_buf:
        Context.setjmp_bufDecl(NewTD);
        break;
      case tok::NotableIdentifierKind::sigjmp_buf:
        Context.setsigjmp_bufDecl(NewTD);
        break;
      case tok::NotableIdentifierKind::ucontext_t:
        Context.setucontext_tDecl(NewTD);
        break;
      case tok::NotableIdentifierKind::float_t:
      case tok::NotableIdentifierKind::double_t:
        NewTD->addAttr(AvailableOnlyInDefaultEvalMethodAttr::Create(Context));
        break;
      default:
        break;
      }
    }

  return NewTD;
}

/// Determines whether the given declaration is an out-of-scope
/// previous declaration.
///
/// This routine should be invoked when name lookup has found a
/// previous declaration (PrevDecl) that is not in the scope where a
/// new declaration by the same name is being introduced. If the new
/// declaration occurs in a local scope, previous declarations with
/// linkage may still be considered previous declarations (C99
/// 6.2.2p4-5, C++ [basic.link]p6).
///
/// \param PrevDecl the previous declaration found by name
/// lookup
///
/// \param DC the context in which the new declaration is being
/// declared.
///
/// \returns true if PrevDecl is an out-of-scope previous declaration
/// for a new delcaration with the same name.
static bool
isOutOfScopePreviousDeclaration(NamedDecl *PrevDecl, DeclContext *DC,
                                ASTContext &Context) {
  if (!PrevDecl)
    return false;

  if (!PrevDecl->hasLinkage())
    return false;

  if (Context.getLangOpts().CPlusPlus) {
    // C++ [basic.link]p6:
    //   If there is a visible declaration of an entity with linkage
    //   having the same name and type, ignoring entities declared
    //   outside the innermost enclosing namespace scope, the block
    //   scope declaration declares that same entity and receives the
    //   linkage of the previous declaration.
    DeclContext *OuterContext = DC->getRedeclContext();
    if (!OuterContext->isFunctionOrMethod())
      // This rule only applies to block-scope declarations.
      return false;

    DeclContext *PrevOuterContext = PrevDecl->getDeclContext();
    if (PrevOuterContext->isRecord())
      // We found a member function: ignore it.
      return false;

    // Find the innermost enclosing namespace for the new and
    // previous declarations.
    OuterContext = OuterContext->getEnclosingNamespaceContext();
    PrevOuterContext = PrevOuterContext->getEnclosingNamespaceContext();

    // The previous declaration is in a different namespace, so it
    // isn't the same function.
    if (!OuterContext->Equals(PrevOuterContext))
      return false;
  }

  return true;
}

static void SetNestedNameSpecifier(Sema &S, DeclaratorDecl *DD, Declarator &D) {
  CXXScopeSpec &SS = D.getCXXScopeSpec();
  if (!SS.isSet()) return;
  DD->setQualifierInfo(SS.getWithLocInContext(S.Context));
}

void Sema::deduceOpenCLAddressSpace(ValueDecl *Decl) {
  if (Decl->getType().hasAddressSpace())
    return;
  if (Decl->getType()->isDependentType())
    return;
  if (VarDecl *Var = dyn_cast<VarDecl>(Decl)) {
    QualType Type = Var->getType();
    if (Type->isSamplerT() || Type->isVoidType())
      return;
    LangAS ImplAS = LangAS::opencl_private;
    // OpenCL C v3.0 s6.7.8 - For OpenCL C 2.0 or with the
    // __opencl_c_program_scope_global_variables feature, the address space
    // for a variable at program scope or a static or extern variable inside
    // a function are inferred to be __global.
    if (getOpenCLOptions().areProgramScopeVariablesSupported(getLangOpts()) &&
        Var->hasGlobalStorage())
      ImplAS = LangAS::opencl_global;
    // If the original type from a decayed type is an array type and that array
    // type has no address space yet, deduce it now.
    if (auto DT = dyn_cast<DecayedType>(Type)) {
      auto OrigTy = DT->getOriginalType();
      if (!OrigTy.hasAddressSpace() && OrigTy->isArrayType()) {
        // Add the address space to the original array type and then propagate
        // that to the element type through `getAsArrayType`.
        OrigTy = Context.getAddrSpaceQualType(OrigTy, ImplAS);
        OrigTy = QualType(Context.getAsArrayType(OrigTy), 0);
        // Re-generate the decayed type.
        Type = Context.getDecayedType(OrigTy);
      }
    }
    Type = Context.getAddrSpaceQualType(Type, ImplAS);
    // Apply any qualifiers (including address space) from the array type to
    // the element type. This implements C99 6.7.3p8: "If the specification of
    // an array type includes any type qualifiers, the element type is so
    // qualified, not the array type."
    if (Type->isArrayType())
      Type = QualType(Context.getAsArrayType(Type), 0);
    Decl->setType(Type);
  }
}

static void checkAttributesAfterMerging(Sema &S, NamedDecl &ND) {
  // Ensure that an auto decl is deduced otherwise the checks below might cache
  // the wrong linkage.
  assert(S.ParsingInitForAutoVars.count(&ND) == 0);

  // 'weak' only applies to declarations with external linkage.
  if (WeakAttr *Attr = ND.getAttr<WeakAttr>()) {
    if (!ND.isExternallyVisible()) {
      S.Diag(Attr->getLocation(), diag::err_attribute_weak_static);
      ND.dropAttr<WeakAttr>();
    }
  }
  if (WeakRefAttr *Attr = ND.getAttr<WeakRefAttr>()) {
    if (ND.isExternallyVisible()) {
      S.Diag(Attr->getLocation(), diag::err_attribute_weakref_not_static);
      ND.dropAttrs<WeakRefAttr, AliasAttr>();
    }
  }

  if (auto *VD = dyn_cast<VarDecl>(&ND)) {
    if (VD->hasInit()) {
      if (const auto *Attr = VD->getAttr<AliasAttr>()) {
        assert(VD->isThisDeclarationADefinition() &&
               !VD->isExternallyVisible() && "Broken AliasAttr handled late!");
        S.Diag(Attr->getLocation(), diag::err_alias_is_definition) << VD << 0;
        VD->dropAttr<AliasAttr>();
      }
    }
  }

  // 'selectany' only applies to externally visible variable declarations.
  // It does not apply to functions.
  if (SelectAnyAttr *Attr = ND.getAttr<SelectAnyAttr>()) {
    if (isa<FunctionDecl>(ND) || !ND.isExternallyVisible()) {
      S.Diag(Attr->getLocation(),
             diag::err_attribute_selectany_non_extern_data);
      ND.dropAttr<SelectAnyAttr>();
    }
  }

  if (HybridPatchableAttr *Attr = ND.getAttr<HybridPatchableAttr>()) {
    if (!ND.isExternallyVisible())
      S.Diag(Attr->getLocation(),
             diag::warn_attribute_hybrid_patchable_non_extern);
  }
  if (const InheritableAttr *Attr = getDLLAttr(&ND)) {
    auto *VD = dyn_cast<VarDecl>(&ND);
    bool IsAnonymousNS = false;
    bool IsMicrosoft = S.Context.getTargetInfo().getCXXABI().isMicrosoft();
    if (VD) {
      const NamespaceDecl *NS = dyn_cast<NamespaceDecl>(VD->getDeclContext());
      while (NS && !IsAnonymousNS) {
        IsAnonymousNS = NS->isAnonymousNamespace();
        NS = dyn_cast<NamespaceDecl>(NS->getParent());
      }
    }
    // dll attributes require external linkage. Static locals may have external
    // linkage but still cannot be explicitly imported or exported.
    // In Microsoft mode, a variable defined in anonymous namespace must have
    // external linkage in order to be exported.
    bool AnonNSInMicrosoftMode = IsAnonymousNS && IsMicrosoft;
    if ((ND.isExternallyVisible() && AnonNSInMicrosoftMode) ||
        (!AnonNSInMicrosoftMode &&
         (!ND.isExternallyVisible() || (VD && VD->isStaticLocal())))) {
      S.Diag(ND.getLocation(), diag::err_attribute_dll_not_extern)
        << &ND << Attr;
      ND.setInvalidDecl();
    }
  }

  // Check the attributes on the function type, if any.
  if (const auto *FD = dyn_cast<FunctionDecl>(&ND)) {
    // Don't declare this variable in the second operand of the for-statement;
    // GCC miscompiles that by ending its lifetime before evaluating the
    // third operand. See gcc.gnu.org/PR86769.
    AttributedTypeLoc ATL;
    for (TypeLoc TL = FD->getTypeSourceInfo()->getTypeLoc();
         (ATL = TL.getAsAdjusted<AttributedTypeLoc>());
         TL = ATL.getModifiedLoc()) {
      // The [[lifetimebound]] attribute can be applied to the implicit object
      // parameter of a non-static member function (other than a ctor or dtor)
      // by applying it to the function type.
      if (const auto *A = ATL.getAttrAs<LifetimeBoundAttr>()) {
        const auto *MD = dyn_cast<CXXMethodDecl>(FD);
        if (!MD || MD->isStatic()) {
          S.Diag(A->getLocation(), diag::err_lifetimebound_no_object_param)
              << !MD << A->getRange();
        } else if (isa<CXXConstructorDecl>(MD) || isa<CXXDestructorDecl>(MD)) {
          S.Diag(A->getLocation(), diag::err_lifetimebound_ctor_dtor)
              << isa<CXXDestructorDecl>(MD) << A->getRange();
        }
      }
    }
  }
}

static void checkDLLAttributeRedeclaration(Sema &S, NamedDecl *OldDecl,
                                           NamedDecl *NewDecl,
                                           bool IsSpecialization,
                                           bool IsDefinition) {
  if (OldDecl->isInvalidDecl() || NewDecl->isInvalidDecl())
    return;

  bool IsTemplate = false;
  if (TemplateDecl *OldTD = dyn_cast<TemplateDecl>(OldDecl)) {
    OldDecl = OldTD->getTemplatedDecl();
    IsTemplate = true;
    if (!IsSpecialization)
      IsDefinition = false;
  }
  if (TemplateDecl *NewTD = dyn_cast<TemplateDecl>(NewDecl)) {
    NewDecl = NewTD->getTemplatedDecl();
    IsTemplate = true;
  }

  if (!OldDecl || !NewDecl)
    return;

  const DLLImportAttr *OldImportAttr = OldDecl->getAttr<DLLImportAttr>();
  const DLLExportAttr *OldExportAttr = OldDecl->getAttr<DLLExportAttr>();
  const DLLImportAttr *NewImportAttr = NewDecl->getAttr<DLLImportAttr>();
  const DLLExportAttr *NewExportAttr = NewDecl->getAttr<DLLExportAttr>();

  // dllimport and dllexport are inheritable attributes so we have to exclude
  // inherited attribute instances.
  bool HasNewAttr = (NewImportAttr && !NewImportAttr->isInherited()) ||
                    (NewExportAttr && !NewExportAttr->isInherited());

  // A redeclaration is not allowed to add a dllimport or dllexport attribute,
  // the only exception being explicit specializations.
  // Implicitly generated declarations are also excluded for now because there
  // is no other way to switch these to use dllimport or dllexport.
  bool AddsAttr = !(OldImportAttr || OldExportAttr) && HasNewAttr;

  if (AddsAttr && !IsSpecialization && !OldDecl->isImplicit()) {
    // Allow with a warning for free functions and global variables.
    bool JustWarn = false;
    if (!OldDecl->isCXXClassMember()) {
      auto *VD = dyn_cast<VarDecl>(OldDecl);
      if (VD && !VD->getDescribedVarTemplate())
        JustWarn = true;
      auto *FD = dyn_cast<FunctionDecl>(OldDecl);
      if (FD && FD->getTemplatedKind() == FunctionDecl::TK_NonTemplate)
        JustWarn = true;
    }

    // We cannot change a declaration that's been used because IR has already
    // been emitted. Dllimported functions will still work though (modulo
    // address equality) as they can use the thunk.
    if (OldDecl->isUsed())
      if (!isa<FunctionDecl>(OldDecl) || !NewImportAttr)
        JustWarn = false;

    unsigned DiagID = JustWarn ? diag::warn_attribute_dll_redeclaration
                               : diag::err_attribute_dll_redeclaration;
    S.Diag(NewDecl->getLocation(), DiagID)
        << NewDecl
        << (NewImportAttr ? (const Attr *)NewImportAttr : NewExportAttr);
    S.Diag(OldDecl->getLocation(), diag::note_previous_declaration);
    if (!JustWarn) {
      NewDecl->setInvalidDecl();
      return;
    }
  }

  // A redeclaration is not allowed to drop a dllimport attribute, the only
  // exceptions being inline function definitions (except for function
  // templates), local extern declarations, qualified friend declarations or
  // special MSVC extension: in the last case, the declaration is treated as if
  // it were marked dllexport.
  bool IsInline = false, IsStaticDataMember = false, IsQualifiedFriend = false;
  bool IsMicrosoftABI  = S.Context.getTargetInfo().shouldDLLImportComdatSymbols();
  if (const auto *VD = dyn_cast<VarDecl>(NewDecl)) {
    // Ignore static data because out-of-line definitions are diagnosed
    // separately.
    IsStaticDataMember = VD->isStaticDataMember();
    IsDefinition = VD->isThisDeclarationADefinition(S.Context) !=
                   VarDecl::DeclarationOnly;
  } else if (const auto *FD = dyn_cast<FunctionDecl>(NewDecl)) {
    IsInline = FD->isInlined();
    IsQualifiedFriend = FD->getQualifier() &&
                        FD->getFriendObjectKind() == Decl::FOK_Declared;
  }

  if (OldImportAttr && !HasNewAttr &&
      (!IsInline || (IsMicrosoftABI && IsTemplate)) && !IsStaticDataMember &&
      !NewDecl->isLocalExternDecl() && !IsQualifiedFriend) {
    if (IsMicrosoftABI && IsDefinition) {
      if (IsSpecialization) {
        S.Diag(
            NewDecl->getLocation(),
            diag::err_attribute_dllimport_function_specialization_definition);
        S.Diag(OldImportAttr->getLocation(), diag::note_attribute);
        NewDecl->dropAttr<DLLImportAttr>();
      } else {
        S.Diag(NewDecl->getLocation(),
               diag::warn_redeclaration_without_import_attribute)
            << NewDecl;
        S.Diag(OldDecl->getLocation(), diag::note_previous_declaration);
        NewDecl->dropAttr<DLLImportAttr>();
        NewDecl->addAttr(DLLExportAttr::CreateImplicit(
            S.Context, NewImportAttr->getRange()));
      }
    } else if (IsMicrosoftABI && IsSpecialization) {
      assert(!IsDefinition);
      // MSVC allows this. Keep the inherited attribute.
    } else {
      S.Diag(NewDecl->getLocation(),
             diag::warn_redeclaration_without_attribute_prev_attribute_ignored)
          << NewDecl << OldImportAttr;
      S.Diag(OldDecl->getLocation(), diag::note_previous_declaration);
      S.Diag(OldImportAttr->getLocation(), diag::note_previous_attribute);
      OldDecl->dropAttr<DLLImportAttr>();
      NewDecl->dropAttr<DLLImportAttr>();
    }
  } else if (IsInline && OldImportAttr && !IsMicrosoftABI) {
    // In MinGW, seeing a function declared inline drops the dllimport
    // attribute.
    OldDecl->dropAttr<DLLImportAttr>();
    NewDecl->dropAttr<DLLImportAttr>();
    S.Diag(NewDecl->getLocation(),
           diag::warn_dllimport_dropped_from_inline_function)
        << NewDecl << OldImportAttr;
  }

  // A specialization of a class template member function is processed here
  // since it's a redeclaration. If the parent class is dllexport, the
  // specialization inherits that attribute. This doesn't happen automatically
  // since the parent class isn't instantiated until later.
  if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(NewDecl)) {
    if (MD->getTemplatedKind() == FunctionDecl::TK_MemberSpecialization &&
        !NewImportAttr && !NewExportAttr) {
      if (const DLLExportAttr *ParentExportAttr =
              MD->getParent()->getAttr<DLLExportAttr>()) {
        DLLExportAttr *NewAttr = ParentExportAttr->clone(S.Context);
        NewAttr->setInherited(true);
        NewDecl->addAttr(NewAttr);
      }
    }
  }
}

/// Given that we are within the definition of the given function,
/// will that definition behave like C99's 'inline', where the
/// definition is discarded except for optimization purposes?
static bool isFunctionDefinitionDiscarded(Sema &S, FunctionDecl *FD) {
  // Try to avoid calling GetGVALinkageForFunction.

  // All cases of this require the 'inline' keyword.
  if (!FD->isInlined()) return false;

  // This is only possible in C++ with the gnu_inline attribute.
  if (S.getLangOpts().CPlusPlus && !FD->hasAttr<GNUInlineAttr>())
    return false;

  // Okay, go ahead and call the relatively-more-expensive function.
  return S.Context.GetGVALinkageForFunction(FD) == GVA_AvailableExternally;
}

/// Determine whether a variable is extern "C" prior to attaching
/// an initializer. We can't just call isExternC() here, because that
/// will also compute and cache whether the declaration is externally
/// visible, which might change when we attach the initializer.
///
/// This can only be used if the declaration is known to not be a
/// redeclaration of an internal linkage declaration.
///
/// For instance:
///
///   auto x = []{};
///
/// Attaching the initializer here makes this declaration not externally
/// visible, because its type has internal linkage.
///
/// FIXME: This is a hack.
template<typename T>
static bool isIncompleteDeclExternC(Sema &S, const T *D) {
  if (S.getLangOpts().CPlusPlus) {
    // In C++, the overloadable attribute negates the effects of extern "C".
    if (!D->isInExternCContext() || D->template hasAttr<OverloadableAttr>())
      return false;

    // So do CUDA's host/device attributes.
    if (S.getLangOpts().CUDA && (D->template hasAttr<CUDADeviceAttr>() ||
                                 D->template hasAttr<CUDAHostAttr>()))
      return false;
  }
  return D->isExternC();
}

static bool shouldConsiderLinkage(const VarDecl *VD) {
  const DeclContext *DC = VD->getDeclContext()->getRedeclContext();
  if (DC->isFunctionOrMethod() || isa<OMPDeclareReductionDecl>(DC) ||
      isa<OMPDeclareMapperDecl>(DC))
    return VD->hasExternalStorage();
  if (DC->isFileContext())
    return true;
  if (DC->isRecord())
    return false;
  if (DC->getDeclKind() == Decl::HLSLBuffer)
    return false;

  if (isa<RequiresExprBodyDecl>(DC))
    return false;
  llvm_unreachable("Unexpected context");
}

static bool shouldConsiderLinkage(const FunctionDecl *FD) {
  const DeclContext *DC = FD->getDeclContext()->getRedeclContext();
  if (DC->isFileContext() || DC->isFunctionOrMethod() ||
      isa<OMPDeclareReductionDecl>(DC) || isa<OMPDeclareMapperDecl>(DC))
    return true;
  if (DC->isRecord())
    return false;
  llvm_unreachable("Unexpected context");
}

static bool hasParsedAttr(Scope *S, const Declarator &PD,
                          ParsedAttr::Kind Kind) {
  // Check decl attributes on the DeclSpec.
  if (PD.getDeclSpec().getAttributes().hasAttribute(Kind))
    return true;

  // Walk the declarator structure, checking decl attributes that were in a type
  // position to the decl itself.
  for (unsigned I = 0, E = PD.getNumTypeObjects(); I != E; ++I) {
    if (PD.getTypeObject(I).getAttrs().hasAttribute(Kind))
      return true;
  }

  // Finally, check attributes on the decl itself.
  return PD.getAttributes().hasAttribute(Kind) ||
         PD.getDeclarationAttributes().hasAttribute(Kind);
}

bool Sema::adjustContextForLocalExternDecl(DeclContext *&DC) {
  if (!DC->isFunctionOrMethod())
    return false;

  // If this is a local extern function or variable declared within a function
  // template, don't add it into the enclosing namespace scope until it is
  // instantiated; it might have a dependent type right now.
  if (DC->isDependentContext())
    return true;

  // C++11 [basic.link]p7:
  //   When a block scope declaration of an entity with linkage is not found to
  //   refer to some other declaration, then that entity is a member of the
  //   innermost enclosing namespace.
  //
  // Per C++11 [namespace.def]p6, the innermost enclosing namespace is a
  // semantically-enclosing namespace, not a lexically-enclosing one.
  while (!DC->isFileContext() && !isa<LinkageSpecDecl>(DC))
    DC = DC->getParent();
  return true;
}

/// Returns true if given declaration has external C language linkage.
static bool isDeclExternC(const Decl *D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    return FD->isExternC();
  if (const auto *VD = dyn_cast<VarDecl>(D))
    return VD->isExternC();

  llvm_unreachable("Unknown type of decl!");
}

/// Returns true if there hasn't been any invalid type diagnosed.
static bool diagnoseOpenCLTypes(Sema &Se, VarDecl *NewVD) {
  DeclContext *DC = NewVD->getDeclContext();
  QualType R = NewVD->getType();

  // OpenCL v2.0 s6.9.b - Image type can only be used as a function argument.
  // OpenCL v2.0 s6.13.16.1 - Pipe type can only be used as a function
  // argument.
  if (R->isImageType() || R->isPipeType()) {
    Se.Diag(NewVD->getLocation(),
            diag::err_opencl_type_can_only_be_used_as_function_parameter)
        << R;
    NewVD->setInvalidDecl();
    return false;
  }

  // OpenCL v1.2 s6.9.r:
  // The event type cannot be used to declare a program scope variable.
  // OpenCL v2.0 s6.9.q:
  // The clk_event_t and reserve_id_t types cannot be declared in program
  // scope.
  if (NewVD->hasGlobalStorage() && !NewVD->isStaticLocal()) {
    if (R->isReserveIDT() || R->isClkEventT() || R->isEventT()) {
      Se.Diag(NewVD->getLocation(),
              diag::err_invalid_type_for_program_scope_var)
          << R;
      NewVD->setInvalidDecl();
      return false;
    }
  }

  // OpenCL v1.0 s6.8.a.3: Pointers to functions are not allowed.
  if (!Se.getOpenCLOptions().isAvailableOption("__cl_clang_function_pointers",
                                               Se.getLangOpts())) {
    QualType NR = R.getCanonicalType();
    while (NR->isPointerType() || NR->isMemberFunctionPointerType() ||
           NR->isReferenceType()) {
      if (NR->isFunctionPointerType() || NR->isMemberFunctionPointerType() ||
          NR->isFunctionReferenceType()) {
        Se.Diag(NewVD->getLocation(), diag::err_opencl_function_pointer)
            << NR->isReferenceType();
        NewVD->setInvalidDecl();
        return false;
      }
      NR = NR->getPointeeType();
    }
  }

  if (!Se.getOpenCLOptions().isAvailableOption("cl_khr_fp16",
                                               Se.getLangOpts())) {
    // OpenCL v1.2 s6.1.1.1: reject declaring variables of the half and
    // half array type (unless the cl_khr_fp16 extension is enabled).
    if (Se.Context.getBaseElementType(R)->isHalfType()) {
      Se.Diag(NewVD->getLocation(), diag::err_opencl_half_declaration) << R;
      NewVD->setInvalidDecl();
      return false;
    }
  }

  // OpenCL v1.2 s6.9.r:
  // The event type cannot be used with the __local, __constant and __global
  // address space qualifiers.
  if (R->isEventT()) {
    if (R.getAddressSpace() != LangAS::opencl_private) {
      Se.Diag(NewVD->getBeginLoc(), diag::err_event_t_addr_space_qual);
      NewVD->setInvalidDecl();
      return false;
    }
  }

  if (R->isSamplerT()) {
    // OpenCL v1.2 s6.9.b p4:
    // The sampler type cannot be used with the __local and __global address
    // space qualifiers.
    if (R.getAddressSpace() == LangAS::opencl_local ||
        R.getAddressSpace() == LangAS::opencl_global) {
      Se.Diag(NewVD->getLocation(), diag::err_wrong_sampler_addressspace);
      NewVD->setInvalidDecl();
    }

    // OpenCL v1.2 s6.12.14.1:
    // A global sampler must be declared with either the constant address
    // space qualifier or with the const qualifier.
    if (DC->isTranslationUnit() &&
        !(R.getAddressSpace() == LangAS::opencl_constant ||
          R.isConstQualified())) {
      Se.Diag(NewVD->getLocation(), diag::err_opencl_nonconst_global_sampler);
      NewVD->setInvalidDecl();
    }
    if (NewVD->isInvalidDecl())
      return false;
  }

  return true;
}

template <typename AttrTy>
static void copyAttrFromTypedefToDecl(Sema &S, Decl *D, const TypedefType *TT) {
  const TypedefNameDecl *TND = TT->getDecl();
  if (const auto *Attribute = TND->getAttr<AttrTy>()) {
    AttrTy *Clone = Attribute->clone(S.Context);
    Clone->setInherited(true);
    D->addAttr(Clone);
  }
}

// This function emits warning and a corresponding note based on the
// ReadOnlyPlacementAttr attribute. The warning checks that all global variable
// declarations of an annotated type must be const qualified.
void emitReadOnlyPlacementAttrWarning(Sema &S, const VarDecl *VD) {
  QualType VarType = VD->getType().getCanonicalType();

  // Ignore local declarations (for now) and those with const qualification.
  // TODO: Local variables should not be allowed if their type declaration has
  // ReadOnlyPlacementAttr attribute. To be handled in follow-up patch.
  if (!VD || VD->hasLocalStorage() || VD->getType().isConstQualified())
    return;

  if (VarType->isArrayType()) {
    // Retrieve element type for array declarations.
    VarType = S.getASTContext().getBaseElementType(VarType);
  }

  const RecordDecl *RD = VarType->getAsRecordDecl();

  // Check if the record declaration is present and if it has any attributes.
  if (RD == nullptr)
    return;

  if (const auto *ConstDecl = RD->getAttr<ReadOnlyPlacementAttr>()) {
    S.Diag(VD->getLocation(), diag::warn_var_decl_not_read_only) << RD;
    S.Diag(ConstDecl->getLocation(), diag::note_enforce_read_only_placement);
    return;
  }
}

NamedDecl *Sema::ActOnVariableDeclarator(
    Scope *S, Declarator &D, DeclContext *DC, TypeSourceInfo *TInfo,
    LookupResult &Previous, MultiTemplateParamsArg TemplateParamLists,
    bool &AddToScope, ArrayRef<BindingDecl *> Bindings) {
  QualType R = TInfo->getType();
  DeclarationName Name = GetNameForDeclarator(D).getName();

  IdentifierInfo *II = Name.getAsIdentifierInfo();
  bool IsPlaceholderVariable = false;

  if (D.isDecompositionDeclarator()) {
    // Take the name of the first declarator as our name for diagnostic
    // purposes.
    auto &Decomp = D.getDecompositionDeclarator();
    if (!Decomp.bindings().empty()) {
      II = Decomp.bindings()[0].Name;
      Name = II;
    }
  } else if (!II) {
    Diag(D.getIdentifierLoc(), diag::err_bad_variable_name) << Name;
    return nullptr;
  }


  DeclSpec::SCS SCSpec = D.getDeclSpec().getStorageClassSpec();
  StorageClass SC = StorageClassSpecToVarDeclStorageClass(D.getDeclSpec());

  if (LangOpts.CPlusPlus && (DC->isClosure() || DC->isFunctionOrMethod()) &&
      SC != SC_Static && SC != SC_Extern && II && II->isPlaceholder()) {
    IsPlaceholderVariable = true;
    if (!Previous.empty()) {
      NamedDecl *PrevDecl = *Previous.begin();
      bool SameDC = PrevDecl->getDeclContext()->getRedeclContext()->Equals(
          DC->getRedeclContext());
      if (SameDC && isDeclInScope(PrevDecl, CurContext, S, false))
        DiagPlaceholderVariableDefinition(D.getIdentifierLoc());
    }
  }

  // dllimport globals without explicit storage class are treated as extern. We
  // have to change the storage class this early to get the right DeclContext.
  if (SC == SC_None && !DC->isRecord() &&
      hasParsedAttr(S, D, ParsedAttr::AT_DLLImport) &&
      !hasParsedAttr(S, D, ParsedAttr::AT_DLLExport))
    SC = SC_Extern;

  DeclContext *OriginalDC = DC;
  bool IsLocalExternDecl = SC == SC_Extern &&
                           adjustContextForLocalExternDecl(DC);

  if (SCSpec == DeclSpec::SCS_mutable) {
    // mutable can only appear on non-static class members, so it's always
    // an error here
    Diag(D.getIdentifierLoc(), diag::err_mutable_nonmember);
    D.setInvalidType();
    SC = SC_None;
  }

  if (getLangOpts().CPlusPlus11 && SCSpec == DeclSpec::SCS_register &&
      !D.getAsmLabel() && !getSourceManager().isInSystemMacro(
                              D.getDeclSpec().getStorageClassSpecLoc())) {
    // In C++11, the 'register' storage class specifier is deprecated.
    // Suppress the warning in system macros, it's used in macros in some
    // popular C system headers, such as in glibc's htonl() macro.
    Diag(D.getDeclSpec().getStorageClassSpecLoc(),
         getLangOpts().CPlusPlus17 ? diag::ext_register_storage_class
                                   : diag::warn_deprecated_register)
      << FixItHint::CreateRemoval(D.getDeclSpec().getStorageClassSpecLoc());
  }

  DiagnoseFunctionSpecifiers(D.getDeclSpec());

  if (!DC->isRecord() && S->getFnParent() == nullptr) {
    // C99 6.9p2: The storage-class specifiers auto and register shall not
    // appear in the declaration specifiers in an external declaration.
    // Global Register+Asm is a GNU extension we support.
    if (SC == SC_Auto || (SC == SC_Register && !D.getAsmLabel())) {
      Diag(D.getIdentifierLoc(), diag::err_typecheck_sclass_fscope);
      D.setInvalidType();
    }
  }

  // If this variable has a VLA type and an initializer, try to
  // fold to a constant-sized type. This is otherwise invalid.
  if (D.hasInitializer() && R->isVariableArrayType())
    tryToFixVariablyModifiedVarType(TInfo, R, D.getIdentifierLoc(),
                                    /*DiagID=*/0);

  if (AutoTypeLoc TL = TInfo->getTypeLoc().getContainedAutoTypeLoc()) {
    const AutoType *AT = TL.getTypePtr();
    CheckConstrainedAuto(AT, TL.getConceptNameLoc());
  }

  bool IsMemberSpecialization = false;
  bool IsVariableTemplateSpecialization = false;
  bool IsPartialSpecialization = false;
  bool IsVariableTemplate = false;
  VarDecl *NewVD = nullptr;
  VarTemplateDecl *NewTemplate = nullptr;
  TemplateParameterList *TemplateParams = nullptr;
  if (!getLangOpts().CPlusPlus) {
    NewVD = VarDecl::Create(Context, DC, D.getBeginLoc(), D.getIdentifierLoc(),
                            II, R, TInfo, SC);

    if (R->getContainedDeducedType())
      ParsingInitForAutoVars.insert(NewVD);

    if (D.isInvalidType())
      NewVD->setInvalidDecl();

    if (NewVD->getType().hasNonTrivialToPrimitiveDestructCUnion() &&
        NewVD->hasLocalStorage())
      checkNonTrivialCUnion(NewVD->getType(), NewVD->getLocation(),
                            NTCUC_AutoVar, NTCUK_Destruct);
  } else {
    bool Invalid = false;
    // Match up the template parameter lists with the scope specifier, then
    // determine whether we have a template or a template specialization.
    TemplateParams = MatchTemplateParametersToScopeSpecifier(
        D.getDeclSpec().getBeginLoc(), D.getIdentifierLoc(),
        D.getCXXScopeSpec(),
        D.getName().getKind() == UnqualifiedIdKind::IK_TemplateId
            ? D.getName().TemplateId
            : nullptr,
        TemplateParamLists,
        /*never a friend*/ false, IsMemberSpecialization, Invalid);

    if (TemplateParams) {
      if (!TemplateParams->size() &&
          D.getName().getKind() != UnqualifiedIdKind::IK_TemplateId) {
        // There is an extraneous 'template<>' for this variable. Complain
        // about it, but allow the declaration of the variable.
        Diag(TemplateParams->getTemplateLoc(),
             diag::err_template_variable_noparams)
          << II
          << SourceRange(TemplateParams->getTemplateLoc(),
                         TemplateParams->getRAngleLoc());
        TemplateParams = nullptr;
      } else {
        // Check that we can declare a template here.
        if (CheckTemplateDeclScope(S, TemplateParams))
          return nullptr;

        if (D.getName().getKind() == UnqualifiedIdKind::IK_TemplateId) {
          // This is an explicit specialization or a partial specialization.
          IsVariableTemplateSpecialization = true;
          IsPartialSpecialization = TemplateParams->size() > 0;
        } else { // if (TemplateParams->size() > 0)
          // This is a template declaration.
          IsVariableTemplate = true;

          // Only C++1y supports variable templates (N3651).
          Diag(D.getIdentifierLoc(),
               getLangOpts().CPlusPlus14
                   ? diag::warn_cxx11_compat_variable_template
                   : diag::ext_variable_template);
        }
      }
    } else {
      // Check that we can declare a member specialization here.
      if (!TemplateParamLists.empty() && IsMemberSpecialization &&
          CheckTemplateDeclScope(S, TemplateParamLists.back()))
        return nullptr;
      assert((Invalid ||
              D.getName().getKind() != UnqualifiedIdKind::IK_TemplateId) &&
             "should have a 'template<>' for this decl");
    }

    bool IsExplicitSpecialization =
        IsVariableTemplateSpecialization && !IsPartialSpecialization;

    // C++ [temp.expl.spec]p2:
    //   The declaration in an explicit-specialization shall not be an
    //   export-declaration. An explicit specialization shall not use a
    //   storage-class-specifier other than thread_local.
    //
    // We use the storage-class-specifier from DeclSpec because we may have
    // added implicit 'extern' for declarations with __declspec(dllimport)!
    if (SCSpec != DeclSpec::SCS_unspecified &&
        (IsExplicitSpecialization || IsMemberSpecialization)) {
      Diag(D.getDeclSpec().getStorageClassSpecLoc(),
           diag::ext_explicit_specialization_storage_class)
          << FixItHint::CreateRemoval(D.getDeclSpec().getStorageClassSpecLoc());
    }

    if (CurContext->isRecord()) {
      if (SC == SC_Static) {
        if (const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(DC)) {
          // Walk up the enclosing DeclContexts to check for any that are
          // incompatible with static data members.
          const DeclContext *FunctionOrMethod = nullptr;
          const CXXRecordDecl *AnonStruct = nullptr;
          for (DeclContext *Ctxt = DC; Ctxt; Ctxt = Ctxt->getParent()) {
            if (Ctxt->isFunctionOrMethod()) {
              FunctionOrMethod = Ctxt;
              break;
            }
            const CXXRecordDecl *ParentDecl = dyn_cast<CXXRecordDecl>(Ctxt);
            if (ParentDecl && !ParentDecl->getDeclName()) {
              AnonStruct = ParentDecl;
              break;
            }
          }
          if (FunctionOrMethod) {
            // C++ [class.static.data]p5: A local class shall not have static
            // data members.
            Diag(D.getIdentifierLoc(),
                 diag::err_static_data_member_not_allowed_in_local_class)
                << Name << RD->getDeclName()
                << llvm::to_underlying(RD->getTagKind());
          } else if (AnonStruct) {
            // C++ [class.static.data]p4: Unnamed classes and classes contained
            // directly or indirectly within unnamed classes shall not contain
            // static data members.
            Diag(D.getIdentifierLoc(),
                 diag::err_static_data_member_not_allowed_in_anon_struct)
                << Name << llvm::to_underlying(AnonStruct->getTagKind());
            Invalid = true;
          } else if (RD->isUnion()) {
            // C++98 [class.union]p1: If a union contains a static data member,
            // the program is ill-formed. C++11 drops this restriction.
            Diag(D.getIdentifierLoc(),
                 getLangOpts().CPlusPlus11
                     ? diag::warn_cxx98_compat_static_data_member_in_union
                     : diag::ext_static_data_member_in_union)
                << Name;
          }
        }
      } else if (IsVariableTemplate || IsPartialSpecialization) {
        // There is no such thing as a member field template.
        Diag(D.getIdentifierLoc(), diag::err_template_member)
            << II << TemplateParams->getSourceRange();
        // Recover by pretending this is a static data member template.
        SC = SC_Static;
      }
    } else if (DC->isRecord()) {
      // This is an out-of-line definition of a static data member.
      switch (SC) {
      case SC_None:
        break;
      case SC_Static:
        Diag(D.getDeclSpec().getStorageClassSpecLoc(),
             diag::err_static_out_of_line)
            << FixItHint::CreateRemoval(
                   D.getDeclSpec().getStorageClassSpecLoc());
        break;
      case SC_Auto:
      case SC_Register:
      case SC_Extern:
        // [dcl.stc] p2: The auto or register specifiers shall be applied only
        // to names of variables declared in a block or to function parameters.
        // [dcl.stc] p6: The extern specifier cannot be used in the declaration
        // of class members

        Diag(D.getDeclSpec().getStorageClassSpecLoc(),
             diag::err_storage_class_for_static_member)
            << FixItHint::CreateRemoval(
                   D.getDeclSpec().getStorageClassSpecLoc());
        break;
      case SC_PrivateExtern:
        llvm_unreachable("C storage class in c++!");
      }
    }

    if (IsVariableTemplateSpecialization) {
      SourceLocation TemplateKWLoc =
          TemplateParamLists.size() > 0
              ? TemplateParamLists[0]->getTemplateLoc()
              : SourceLocation();
      DeclResult Res = ActOnVarTemplateSpecialization(
          S, D, TInfo, Previous, TemplateKWLoc, TemplateParams, SC,
          IsPartialSpecialization);
      if (Res.isInvalid())
        return nullptr;
      NewVD = cast<VarDecl>(Res.get());
      AddToScope = false;
    } else if (D.isDecompositionDeclarator()) {
      NewVD = DecompositionDecl::Create(Context, DC, D.getBeginLoc(),
                                        D.getIdentifierLoc(), R, TInfo, SC,
                                        Bindings);
    } else
      NewVD = VarDecl::Create(Context, DC, D.getBeginLoc(),
                              D.getIdentifierLoc(), II, R, TInfo, SC);

    // If this is supposed to be a variable template, create it as such.
    if (IsVariableTemplate) {
      NewTemplate =
          VarTemplateDecl::Create(Context, DC, D.getIdentifierLoc(), Name,
                                  TemplateParams, NewVD);
      NewVD->setDescribedVarTemplate(NewTemplate);
    }

    // If this decl has an auto type in need of deduction, make a note of the
    // Decl so we can diagnose uses of it in its own initializer.
    if (R->getContainedDeducedType())
      ParsingInitForAutoVars.insert(NewVD);

    if (D.isInvalidType() || Invalid) {
      NewVD->setInvalidDecl();
      if (NewTemplate)
        NewTemplate->setInvalidDecl();
    }

    SetNestedNameSpecifier(*this, NewVD, D);

    // If we have any template parameter lists that don't directly belong to
    // the variable (matching the scope specifier), store them.
    // An explicit variable template specialization does not own any template
    // parameter lists.
    unsigned VDTemplateParamLists =
        (TemplateParams && !IsExplicitSpecialization) ? 1 : 0;
    if (TemplateParamLists.size() > VDTemplateParamLists)
      NewVD->setTemplateParameterListsInfo(
          Context, TemplateParamLists.drop_back(VDTemplateParamLists));
  }

  if (D.getDeclSpec().isInlineSpecified()) {
    if (!getLangOpts().CPlusPlus) {
      Diag(D.getDeclSpec().getInlineSpecLoc(), diag::err_inline_non_function)
          << 0;
    } else if (CurContext->isFunctionOrMethod()) {
      // 'inline' is not allowed on block scope variable declaration.
      Diag(D.getDeclSpec().getInlineSpecLoc(),
           diag::err_inline_declaration_block_scope) << Name
        << FixItHint::CreateRemoval(D.getDeclSpec().getInlineSpecLoc());
    } else {
      Diag(D.getDeclSpec().getInlineSpecLoc(),
           getLangOpts().CPlusPlus17 ? diag::warn_cxx14_compat_inline_variable
                                     : diag::ext_inline_variable);
      NewVD->setInlineSpecified();
    }
  }

  // Set the lexical context. If the declarator has a C++ scope specifier, the
  // lexical context will be different from the semantic context.
  NewVD->setLexicalDeclContext(CurContext);
  if (NewTemplate)
    NewTemplate->setLexicalDeclContext(CurContext);

  if (IsLocalExternDecl) {
    if (D.isDecompositionDeclarator())
      for (auto *B : Bindings)
        B->setLocalExternDecl();
    else
      NewVD->setLocalExternDecl();
  }

  bool EmitTLSUnsupportedError = false;
  if (DeclSpec::TSCS TSCS = D.getDeclSpec().getThreadStorageClassSpec()) {
    // C++11 [dcl.stc]p4:
    //   When thread_local is applied to a variable of block scope the
    //   storage-class-specifier static is implied if it does not appear
    //   explicitly.
    // Core issue: 'static' is not implied if the variable is declared
    //   'extern'.
    if (NewVD->hasLocalStorage() &&
        (SCSpec != DeclSpec::SCS_unspecified ||
         TSCS != DeclSpec::TSCS_thread_local ||
         !DC->isFunctionOrMethod()))
      Diag(D.getDeclSpec().getThreadStorageClassSpecLoc(),
           diag::err_thread_non_global)
        << DeclSpec::getSpecifierName(TSCS);
    else if (!Context.getTargetInfo().isTLSSupported()) {
      if (getLangOpts().CUDA || getLangOpts().OpenMPIsTargetDevice ||
          getLangOpts().SYCLIsDevice) {
        // Postpone error emission until we've collected attributes required to
        // figure out whether it's a host or device variable and whether the
        // error should be ignored.
        EmitTLSUnsupportedError = true;
        // We still need to mark the variable as TLS so it shows up in AST with
        // proper storage class for other tools to use even if we're not going
        // to emit any code for it.
        NewVD->setTSCSpec(TSCS);
      } else
        Diag(D.getDeclSpec().getThreadStorageClassSpecLoc(),
             diag::err_thread_unsupported);
    } else
      NewVD->setTSCSpec(TSCS);
  }

  switch (D.getDeclSpec().getConstexprSpecifier()) {
  case ConstexprSpecKind::Unspecified:
    break;

  case ConstexprSpecKind::Consteval:
    Diag(D.getDeclSpec().getConstexprSpecLoc(),
         diag::err_constexpr_wrong_decl_kind)
        << static_cast<int>(D.getDeclSpec().getConstexprSpecifier());
    [[fallthrough]];

  case ConstexprSpecKind::Constexpr:
    NewVD->setConstexpr(true);
    // C++1z [dcl.spec.constexpr]p1:
    //   A static data member declared with the constexpr specifier is
    //   implicitly an inline variable.
    if (NewVD->isStaticDataMember() &&
        (getLangOpts().CPlusPlus17 ||
         Context.getTargetInfo().getCXXABI().isMicrosoft()))
      NewVD->setImplicitlyInline();
    break;

  case ConstexprSpecKind::Constinit:
    if (!NewVD->hasGlobalStorage())
      Diag(D.getDeclSpec().getConstexprSpecLoc(),
           diag::err_constinit_local_variable);
    else
      NewVD->addAttr(
          ConstInitAttr::Create(Context, D.getDeclSpec().getConstexprSpecLoc(),
                                ConstInitAttr::Keyword_constinit));
    break;
  }

  // C99 6.7.4p3
  //   An inline definition of a function with external linkage shall
  //   not contain a definition of a modifiable object with static or
  //   thread storage duration...
  // We only apply this when the function is required to be defined
  // elsewhere, i.e. when the function is not 'extern inline'.  Note
  // that a local variable with thread storage duration still has to
  // be marked 'static'.  Also note that it's possible to get these
  // semantics in C++ using __attribute__((gnu_inline)).
  if (SC == SC_Static && S->getFnParent() != nullptr &&
      !NewVD->getType().isConstQualified()) {
    FunctionDecl *CurFD = getCurFunctionDecl();
    if (CurFD && isFunctionDefinitionDiscarded(*this, CurFD)) {
      Diag(D.getDeclSpec().getStorageClassSpecLoc(),
           diag::warn_static_local_in_extern_inline);
      MaybeSuggestAddingStaticToDecl(CurFD);
    }
  }

  if (D.getDeclSpec().isModulePrivateSpecified()) {
    if (IsVariableTemplateSpecialization)
      Diag(NewVD->getLocation(), diag::err_module_private_specialization)
          << (IsPartialSpecialization ? 1 : 0)
          << FixItHint::CreateRemoval(
                 D.getDeclSpec().getModulePrivateSpecLoc());
    else if (IsMemberSpecialization)
      Diag(NewVD->getLocation(), diag::err_module_private_specialization)
        << 2
        << FixItHint::CreateRemoval(D.getDeclSpec().getModulePrivateSpecLoc());
    else if (NewVD->hasLocalStorage())
      Diag(NewVD->getLocation(), diag::err_module_private_local)
          << 0 << NewVD
          << SourceRange(D.getDeclSpec().getModulePrivateSpecLoc())
          << FixItHint::CreateRemoval(
                 D.getDeclSpec().getModulePrivateSpecLoc());
    else {
      NewVD->setModulePrivate();
      if (NewTemplate)
        NewTemplate->setModulePrivate();
      for (auto *B : Bindings)
        B->setModulePrivate();
    }
  }

  if (getLangOpts().OpenCL) {
    deduceOpenCLAddressSpace(NewVD);

    DeclSpec::TSCS TSC = D.getDeclSpec().getThreadStorageClassSpec();
    if (TSC != TSCS_unspecified) {
      Diag(D.getDeclSpec().getThreadStorageClassSpecLoc(),
           diag::err_opencl_unknown_type_specifier)
          << getLangOpts().getOpenCLVersionString()
          << DeclSpec::getSpecifierName(TSC) << 1;
      NewVD->setInvalidDecl();
    }
  }

  // WebAssembly tables are always in address space 1 (wasm_var). Don't apply
  // address space if the table has local storage (semantic checks elsewhere
  // will produce an error anyway).
  if (const auto *ATy = dyn_cast<ArrayType>(NewVD->getType())) {
    if (ATy && ATy->getElementType().isWebAssemblyReferenceType() &&
        !NewVD->hasLocalStorage()) {
      QualType Type = Context.getAddrSpaceQualType(
          NewVD->getType(), Context.getLangASForBuiltinAddressSpace(1));
      NewVD->setType(Type);
    }
  }

  // Handle attributes prior to checking for duplicates in MergeVarDecl
  ProcessDeclAttributes(S, NewVD, D);

  // FIXME: This is probably the wrong location to be doing this and we should
  // probably be doing this for more attributes (especially for function
  // pointer attributes such as format, warn_unused_result, etc.). Ideally
  // the code to copy attributes would be generated by TableGen.
  if (R->isFunctionPointerType())
    if (const auto *TT = R->getAs<TypedefType>())
      copyAttrFromTypedefToDecl<AllocSizeAttr>(*this, NewVD, TT);

  if (getLangOpts().CUDA || getLangOpts().OpenMPIsTargetDevice ||
      getLangOpts().SYCLIsDevice) {
    if (EmitTLSUnsupportedError &&
        ((getLangOpts().CUDA && DeclAttrsMatchCUDAMode(getLangOpts(), NewVD)) ||
         (getLangOpts().OpenMPIsTargetDevice &&
          OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(NewVD))))
      Diag(D.getDeclSpec().getThreadStorageClassSpecLoc(),
           diag::err_thread_unsupported);

    if (EmitTLSUnsupportedError &&
        (LangOpts.SYCLIsDevice ||
         (LangOpts.OpenMP && LangOpts.OpenMPIsTargetDevice)))
      targetDiag(D.getIdentifierLoc(), diag::err_thread_unsupported);
    // CUDA B.2.5: "__shared__ and __constant__ variables have implied static
    // storage [duration]."
    if (SC == SC_None && S->getFnParent() != nullptr &&
        (NewVD->hasAttr<CUDASharedAttr>() ||
         NewVD->hasAttr<CUDAConstantAttr>())) {
      NewVD->setStorageClass(SC_Static);
    }
  }

  // Ensure that dllimport globals without explicit storage class are treated as
  // extern. The storage class is set above using parsed attributes. Now we can
  // check the VarDecl itself.
  assert(!NewVD->hasAttr<DLLImportAttr>() ||
         NewVD->getAttr<DLLImportAttr>()->isInherited() ||
         NewVD->isStaticDataMember() || NewVD->getStorageClass() != SC_None);

  // In auto-retain/release, infer strong retension for variables of
  // retainable type.
  if (getLangOpts().ObjCAutoRefCount && ObjC().inferObjCARCLifetime(NewVD))
    NewVD->setInvalidDecl();

  // Handle GNU asm-label extension (encoded as an attribute).
  if (Expr *E = (Expr*)D.getAsmLabel()) {
    // The parser guarantees this is a string.
    StringLiteral *SE = cast<StringLiteral>(E);
    StringRef Label = SE->getString();
    if (S->getFnParent() != nullptr) {
      switch (SC) {
      case SC_None:
      case SC_Auto:
        Diag(E->getExprLoc(), diag::warn_asm_label_on_auto_decl) << Label;
        break;
      case SC_Register:
        // Local Named register
        if (!Context.getTargetInfo().isValidGCCRegisterName(Label) &&
            DeclAttrsMatchCUDAMode(getLangOpts(), getCurFunctionDecl()))
          Diag(E->getExprLoc(), diag::err_asm_unknown_register_name) << Label;
        break;
      case SC_Static:
      case SC_Extern:
      case SC_PrivateExtern:
        break;
      }
    } else if (SC == SC_Register) {
      // Global Named register
      if (DeclAttrsMatchCUDAMode(getLangOpts(), NewVD)) {
        const auto &TI = Context.getTargetInfo();
        bool HasSizeMismatch;

        if (!TI.isValidGCCRegisterName(Label))
          Diag(E->getExprLoc(), diag::err_asm_unknown_register_name) << Label;
        else if (!TI.validateGlobalRegisterVariable(Label,
                                                    Context.getTypeSize(R),
                                                    HasSizeMismatch))
          Diag(E->getExprLoc(), diag::err_asm_invalid_global_var_reg) << Label;
        else if (HasSizeMismatch)
          Diag(E->getExprLoc(), diag::err_asm_register_size_mismatch) << Label;
      }

      if (!R->isIntegralType(Context) && !R->isPointerType()) {
        Diag(D.getBeginLoc(), diag::err_asm_bad_register_type);
        NewVD->setInvalidDecl(true);
      }
    }

    NewVD->addAttr(AsmLabelAttr::Create(Context, Label,
                                        /*IsLiteralLabel=*/true,
                                        SE->getStrTokenLoc(0)));
  } else if (!ExtnameUndeclaredIdentifiers.empty()) {
    llvm::DenseMap<IdentifierInfo*,AsmLabelAttr*>::iterator I =
      ExtnameUndeclaredIdentifiers.find(NewVD->getIdentifier());
    if (I != ExtnameUndeclaredIdentifiers.end()) {
      if (isDeclExternC(NewVD)) {
        NewVD->addAttr(I->second);
        ExtnameUndeclaredIdentifiers.erase(I);
      } else
        Diag(NewVD->getLocation(), diag::warn_redefine_extname_not_applied)
            << /*Variable*/1 << NewVD;
    }
  }

  // Find the shadowed declaration before filtering for scope.
  NamedDecl *ShadowedDecl = D.getCXXScopeSpec().isEmpty()
                                ? getShadowedDeclaration(NewVD, Previous)
                                : nullptr;

  // Don't consider existing declarations that are in a different
  // scope and are out-of-semantic-context declarations (if the new
  // declaration has linkage).
  FilterLookupForScope(Previous, OriginalDC, S, shouldConsiderLinkage(NewVD),
                       D.getCXXScopeSpec().isNotEmpty() ||
                       IsMemberSpecialization ||
                       IsVariableTemplateSpecialization);

  // Check whether the previous declaration is in the same block scope. This
  // affects whether we merge types with it, per C++11 [dcl.array]p3.
  if (getLangOpts().CPlusPlus &&
      NewVD->isLocalVarDecl() && NewVD->hasExternalStorage())
    NewVD->setPreviousDeclInSameBlockScope(
        Previous.isSingleResult() && !Previous.isShadowed() &&
        isDeclInScope(Previous.getFoundDecl(), OriginalDC, S, false));

  if (!getLangOpts().CPlusPlus) {
    D.setRedeclaration(CheckVariableDeclaration(NewVD, Previous));
  } else {
    // If this is an explicit specialization of a static data member, check it.
    if (IsMemberSpecialization && !IsVariableTemplateSpecialization &&
        !NewVD->isInvalidDecl() && CheckMemberSpecialization(NewVD, Previous))
      NewVD->setInvalidDecl();

    // Merge the decl with the existing one if appropriate.
    if (!Previous.empty()) {
      if (Previous.isSingleResult() &&
          isa<FieldDecl>(Previous.getFoundDecl()) &&
          D.getCXXScopeSpec().isSet()) {
        // The user tried to define a non-static data member
        // out-of-line (C++ [dcl.meaning]p1).
        Diag(NewVD->getLocation(), diag::err_nonstatic_member_out_of_line)
          << D.getCXXScopeSpec().getRange();
        Previous.clear();
        NewVD->setInvalidDecl();
      }
    } else if (D.getCXXScopeSpec().isSet() &&
               !IsVariableTemplateSpecialization) {
      // No previous declaration in the qualifying scope.
      Diag(D.getIdentifierLoc(), diag::err_no_member)
        << Name << computeDeclContext(D.getCXXScopeSpec(), true)
        << D.getCXXScopeSpec().getRange();
      NewVD->setInvalidDecl();
    }

    if (!IsPlaceholderVariable)
      D.setRedeclaration(CheckVariableDeclaration(NewVD, Previous));

    // CheckVariableDeclaration will set NewVD as invalid if something is in
    // error like WebAssembly tables being declared as arrays with a non-zero
    // size, but then parsing continues and emits further errors on that line.
    // To avoid that we check here if it happened and return nullptr.
    if (NewVD->getType()->isWebAssemblyTableType() && NewVD->isInvalidDecl())
      return nullptr;

    if (NewTemplate) {
      VarTemplateDecl *PrevVarTemplate =
          NewVD->getPreviousDecl()
              ? NewVD->getPreviousDecl()->getDescribedVarTemplate()
              : nullptr;

      // Check the template parameter list of this declaration, possibly
      // merging in the template parameter list from the previous variable
      // template declaration.
      if (CheckTemplateParameterList(
              TemplateParams,
              PrevVarTemplate ? PrevVarTemplate->getTemplateParameters()
                              : nullptr,
              (D.getCXXScopeSpec().isSet() && DC && DC->isRecord() &&
               DC->isDependentContext())
                  ? TPC_ClassTemplateMember
                  : TPC_VarTemplate))
        NewVD->setInvalidDecl();

      // If we are providing an explicit specialization of a static variable
      // template, make a note of that.
      if (PrevVarTemplate &&
          PrevVarTemplate->getInstantiatedFromMemberTemplate())
        PrevVarTemplate->setMemberSpecialization();
    }
  }

  // Diagnose shadowed variables iff this isn't a redeclaration.
  if (!IsPlaceholderVariable && ShadowedDecl && !D.isRedeclaration())
    CheckShadow(NewVD, ShadowedDecl, Previous);

  ProcessPragmaWeak(S, NewVD);

  // If this is the first declaration of an extern C variable, update
  // the map of such variables.
  if (NewVD->isFirstDecl() && !NewVD->isInvalidDecl() &&
      isIncompleteDeclExternC(*this, NewVD))
    RegisterLocallyScopedExternCDecl(NewVD, S);

  if (getLangOpts().CPlusPlus && NewVD->isStaticLocal()) {
    MangleNumberingContext *MCtx;
    Decl *ManglingContextDecl;
    std::tie(MCtx, ManglingContextDecl) =
        getCurrentMangleNumberContext(NewVD->getDeclContext());
    if (MCtx) {
      Context.setManglingNumber(
          NewVD, MCtx->getManglingNumber(
                     NewVD, getMSManglingNumber(getLangOpts(), S)));
      Context.setStaticLocalNumber(NewVD, MCtx->getStaticLocalNumber(NewVD));
    }
  }

  // Special handling of variable named 'main'.
  if (Name.getAsIdentifierInfo() && Name.getAsIdentifierInfo()->isStr("main") &&
      NewVD->getDeclContext()->getRedeclContext()->isTranslationUnit() &&
      !getLangOpts().Freestanding && !NewVD->getDescribedVarTemplate()) {

    // C++ [basic.start.main]p3
    // A program that declares a variable main at global scope is ill-formed.
    if (getLangOpts().CPlusPlus)
      Diag(D.getBeginLoc(), diag::err_main_global_variable);

    // In C, and external-linkage variable named main results in undefined
    // behavior.
    else if (NewVD->hasExternalFormalLinkage())
      Diag(D.getBeginLoc(), diag::warn_main_redefined);
  }

  if (D.isRedeclaration() && !Previous.empty()) {
    NamedDecl *Prev = Previous.getRepresentativeDecl();
    checkDLLAttributeRedeclaration(*this, Prev, NewVD, IsMemberSpecialization,
                                   D.isFunctionDefinition());
  }

  if (NewTemplate) {
    if (NewVD->isInvalidDecl())
      NewTemplate->setInvalidDecl();
    ActOnDocumentableDecl(NewTemplate);
    return NewTemplate;
  }

  if (IsMemberSpecialization && !NewVD->isInvalidDecl())
    CompleteMemberSpecialization(NewVD, Previous);

  emitReadOnlyPlacementAttrWarning(*this, NewVD);

  return NewVD;
}

/// Enum describing the %select options in diag::warn_decl_shadow.
enum ShadowedDeclKind {
  SDK_Local,
  SDK_Global,
  SDK_StaticMember,
  SDK_Field,
  SDK_Typedef,
  SDK_Using,
  SDK_StructuredBinding
};

/// Determine what kind of declaration we're shadowing.
static ShadowedDeclKind computeShadowedDeclKind(const NamedDecl *ShadowedDecl,
                                                const DeclContext *OldDC) {
  if (isa<TypeAliasDecl>(ShadowedDecl))
    return SDK_Using;
  else if (isa<TypedefDecl>(ShadowedDecl))
    return SDK_Typedef;
  else if (isa<BindingDecl>(ShadowedDecl))
    return SDK_StructuredBinding;
  else if (isa<RecordDecl>(OldDC))
    return isa<FieldDecl>(ShadowedDecl) ? SDK_Field : SDK_StaticMember;

  return OldDC->isFileContext() ? SDK_Global : SDK_Local;
}

/// Return the location of the capture if the given lambda captures the given
/// variable \p VD, or an invalid source location otherwise.
static SourceLocation getCaptureLocation(const LambdaScopeInfo *LSI,
                                         const VarDecl *VD) {
  for (const Capture &Capture : LSI->Captures) {
    if (Capture.isVariableCapture() && Capture.getVariable() == VD)
      return Capture.getLocation();
  }
  return SourceLocation();
}

static bool shouldWarnIfShadowedDecl(const DiagnosticsEngine &Diags,
                                     const LookupResult &R) {
  // Only diagnose if we're shadowing an unambiguous field or variable.
  if (R.getResultKind() != LookupResult::Found)
    return false;

  // Return false if warning is ignored.
  return !Diags.isIgnored(diag::warn_decl_shadow, R.getNameLoc());
}

NamedDecl *Sema::getShadowedDeclaration(const VarDecl *D,
                                        const LookupResult &R) {
  if (!shouldWarnIfShadowedDecl(Diags, R))
    return nullptr;

  // Don't diagnose declarations at file scope.
  if (D->hasGlobalStorage() && !D->isStaticLocal())
    return nullptr;

  NamedDecl *ShadowedDecl = R.getFoundDecl();
  return isa<VarDecl, FieldDecl, BindingDecl>(ShadowedDecl) ? ShadowedDecl
                                                            : nullptr;
}

NamedDecl *Sema::getShadowedDeclaration(const TypedefNameDecl *D,
                                        const LookupResult &R) {
  // Don't warn if typedef declaration is part of a class
  if (D->getDeclContext()->isRecord())
    return nullptr;

  if (!shouldWarnIfShadowedDecl(Diags, R))
    return nullptr;

  NamedDecl *ShadowedDecl = R.getFoundDecl();
  return isa<TypedefNameDecl>(ShadowedDecl) ? ShadowedDecl : nullptr;
}

NamedDecl *Sema::getShadowedDeclaration(const BindingDecl *D,
                                        const LookupResult &R) {
  if (!shouldWarnIfShadowedDecl(Diags, R))
    return nullptr;

  NamedDecl *ShadowedDecl = R.getFoundDecl();
  return isa<VarDecl, FieldDecl, BindingDecl>(ShadowedDecl) ? ShadowedDecl
                                                            : nullptr;
}

void Sema::CheckShadow(NamedDecl *D, NamedDecl *ShadowedDecl,
                       const LookupResult &R) {
  DeclContext *NewDC = D->getDeclContext();

  if (FieldDecl *FD = dyn_cast<FieldDecl>(ShadowedDecl)) {
    // Fields are not shadowed by variables in C++ static methods.
    if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(NewDC))
      if (MD->isStatic())
        return;

    // Fields shadowed by constructor parameters are a special case. Usually
    // the constructor initializes the field with the parameter.
    if (isa<CXXConstructorDecl>(NewDC))
      if (const auto PVD = dyn_cast<ParmVarDecl>(D)) {
        // Remember that this was shadowed so we can either warn about its
        // modification or its existence depending on warning settings.
        ShadowingDecls.insert({PVD->getCanonicalDecl(), FD});
        return;
      }
  }

  if (VarDecl *shadowedVar = dyn_cast<VarDecl>(ShadowedDecl))
    if (shadowedVar->isExternC()) {
      // For shadowing external vars, make sure that we point to the global
      // declaration, not a locally scoped extern declaration.
      for (auto *I : shadowedVar->redecls())
        if (I->isFileVarDecl()) {
          ShadowedDecl = I;
          break;
        }
    }

  DeclContext *OldDC = ShadowedDecl->getDeclContext()->getRedeclContext();

  unsigned WarningDiag = diag::warn_decl_shadow;
  SourceLocation CaptureLoc;
  if (isa<VarDecl>(D) && NewDC && isa<CXXMethodDecl>(NewDC)) {
    if (const auto *RD = dyn_cast<CXXRecordDecl>(NewDC->getParent())) {
      if (RD->isLambda() && OldDC->Encloses(NewDC->getLexicalParent())) {
        if (const auto *VD = dyn_cast<VarDecl>(ShadowedDecl)) {
          const auto *LSI = cast<LambdaScopeInfo>(getCurFunction());
          if (RD->getLambdaCaptureDefault() == LCD_None) {
            // Try to avoid warnings for lambdas with an explicit capture
            // list. Warn only when the lambda captures the shadowed decl
            // explicitly.
            CaptureLoc = getCaptureLocation(LSI, VD);
            if (CaptureLoc.isInvalid())
              WarningDiag = diag::warn_decl_shadow_uncaptured_local;
          } else {
            // Remember that this was shadowed so we can avoid the warning if
            // the shadowed decl isn't captured and the warning settings allow
            // it.
            cast<LambdaScopeInfo>(getCurFunction())
                ->ShadowingDecls.push_back({D, VD});
            return;
          }
        }
        if (isa<FieldDecl>(ShadowedDecl)) {
          // If lambda can capture this, then emit default shadowing warning,
          // Otherwise it is not really a shadowing case since field is not
          // available in lambda's body.
          // At this point we don't know that lambda can capture this, so
          // remember that this was shadowed and delay until we know.
          cast<LambdaScopeInfo>(getCurFunction())
              ->ShadowingDecls.push_back({D, ShadowedDecl});
          return;
        }
      }
      if (const auto *VD = dyn_cast<VarDecl>(ShadowedDecl);
          VD && VD->hasLocalStorage()) {
        // A variable can't shadow a local variable in an enclosing scope, if
        // they are separated by a non-capturing declaration context.
        for (DeclContext *ParentDC = NewDC;
             ParentDC && !ParentDC->Equals(OldDC);
             ParentDC = getLambdaAwareParentOfDeclContext(ParentDC)) {
          // Only block literals, captured statements, and lambda expressions
          // can capture; other scopes don't.
          if (!isa<BlockDecl>(ParentDC) && !isa<CapturedDecl>(ParentDC) &&
              !isLambdaCallOperator(ParentDC)) {
            return;
          }
        }
      }
    }
  }

  // Never warn about shadowing a placeholder variable.
  if (ShadowedDecl->isPlaceholderVar(getLangOpts()))
    return;

  // Only warn about certain kinds of shadowing for class members.
  if (NewDC && NewDC->isRecord()) {
    // In particular, don't warn about shadowing non-class members.
    if (!OldDC->isRecord())
      return;

    // TODO: should we warn about static data members shadowing
    // static data members from base classes?

    // TODO: don't diagnose for inaccessible shadowed members.
    // This is hard to do perfectly because we might friend the
    // shadowing context, but that's just a false negative.
  }


  DeclarationName Name = R.getLookupName();

  // Emit warning and note.
  ShadowedDeclKind Kind = computeShadowedDeclKind(ShadowedDecl, OldDC);
  Diag(R.getNameLoc(), WarningDiag) << Name << Kind << OldDC;
  if (!CaptureLoc.isInvalid())
    Diag(CaptureLoc, diag::note_var_explicitly_captured_here)
        << Name << /*explicitly*/ 1;
  Diag(ShadowedDecl->getLocation(), diag::note_previous_declaration);
}

void Sema::DiagnoseShadowingLambdaDecls(const LambdaScopeInfo *LSI) {
  for (const auto &Shadow : LSI->ShadowingDecls) {
    const NamedDecl *ShadowedDecl = Shadow.ShadowedDecl;
    // Try to avoid the warning when the shadowed decl isn't captured.
    const DeclContext *OldDC = ShadowedDecl->getDeclContext();
    if (const auto *VD = dyn_cast<VarDecl>(ShadowedDecl)) {
      SourceLocation CaptureLoc = getCaptureLocation(LSI, VD);
      Diag(Shadow.VD->getLocation(),
           CaptureLoc.isInvalid() ? diag::warn_decl_shadow_uncaptured_local
                                  : diag::warn_decl_shadow)
          << Shadow.VD->getDeclName()
          << computeShadowedDeclKind(ShadowedDecl, OldDC) << OldDC;
      if (CaptureLoc.isValid())
        Diag(CaptureLoc, diag::note_var_explicitly_captured_here)
            << Shadow.VD->getDeclName() << /*explicitly*/ 0;
      Diag(ShadowedDecl->getLocation(), diag::note_previous_declaration);
    } else if (isa<FieldDecl>(ShadowedDecl)) {
      Diag(Shadow.VD->getLocation(),
           LSI->isCXXThisCaptured() ? diag::warn_decl_shadow
                                    : diag::warn_decl_shadow_uncaptured_local)
          << Shadow.VD->getDeclName()
          << computeShadowedDeclKind(ShadowedDecl, OldDC) << OldDC;
      Diag(ShadowedDecl->getLocation(), diag::note_previous_declaration);
    }
  }
}

void Sema::CheckShadow(Scope *S, VarDecl *D) {
  if (Diags.isIgnored(diag::warn_decl_shadow, D->getLocation()))
    return;

  LookupResult R(*this, D->getDeclName(), D->getLocation(),
                 Sema::LookupOrdinaryName,
                 RedeclarationKind::ForVisibleRedeclaration);
  LookupName(R, S);
  if (NamedDecl *ShadowedDecl = getShadowedDeclaration(D, R))
    CheckShadow(D, ShadowedDecl, R);
}

/// Check if 'E', which is an expression that is about to be modified, refers
/// to a constructor parameter that shadows a field.
void Sema::CheckShadowingDeclModification(Expr *E, SourceLocation Loc) {
  // Quickly ignore expressions that can't be shadowing ctor parameters.
  if (!getLangOpts().CPlusPlus || ShadowingDecls.empty())
    return;
  E = E->IgnoreParenImpCasts();
  auto *DRE = dyn_cast<DeclRefExpr>(E);
  if (!DRE)
    return;
  const NamedDecl *D = cast<NamedDecl>(DRE->getDecl()->getCanonicalDecl());
  auto I = ShadowingDecls.find(D);
  if (I == ShadowingDecls.end())
    return;
  const NamedDecl *ShadowedDecl = I->second;
  const DeclContext *OldDC = ShadowedDecl->getDeclContext();
  Diag(Loc, diag::warn_modifying_shadowing_decl) << D << OldDC;
  Diag(D->getLocation(), diag::note_var_declared_here) << D;
  Diag(ShadowedDecl->getLocation(), diag::note_previous_declaration);

  // Avoid issuing multiple warnings about the same decl.
  ShadowingDecls.erase(I);
}

/// Check for conflict between this global or extern "C" declaration and
/// previous global or extern "C" declarations. This is only used in C++.
template<typename T>
static bool checkGlobalOrExternCConflict(
    Sema &S, const T *ND, bool IsGlobal, LookupResult &Previous) {
  assert(S.getLangOpts().CPlusPlus && "only C++ has extern \"C\"");
  NamedDecl *Prev = S.findLocallyScopedExternCDecl(ND->getDeclName());

  if (!Prev && IsGlobal && !isIncompleteDeclExternC(S, ND)) {
    // The common case: this global doesn't conflict with any extern "C"
    // declaration.
    return false;
  }

  if (Prev) {
    if (!IsGlobal || isIncompleteDeclExternC(S, ND)) {
      // Both the old and new declarations have C language linkage. This is a
      // redeclaration.
      Previous.clear();
      Previous.addDecl(Prev);
      return true;
    }

    // This is a global, non-extern "C" declaration, and there is a previous
    // non-global extern "C" declaration. Diagnose if this is a variable
    // declaration.
    if (!isa<VarDecl>(ND))
      return false;
  } else {
    // The declaration is extern "C". Check for any declaration in the
    // translation unit which might conflict.
    if (IsGlobal) {
      // We have already performed the lookup into the translation unit.
      IsGlobal = false;
      for (LookupResult::iterator I = Previous.begin(), E = Previous.end();
           I != E; ++I) {
        if (isa<VarDecl>(*I)) {
          Prev = *I;
          break;
        }
      }
    } else {
      DeclContext::lookup_result R =
          S.Context.getTranslationUnitDecl()->lookup(ND->getDeclName());
      for (DeclContext::lookup_result::iterator I = R.begin(), E = R.end();
           I != E; ++I) {
        if (isa<VarDecl>(*I)) {
          Prev = *I;
          break;
        }
        // FIXME: If we have any other entity with this name in global scope,
        // the declaration is ill-formed, but that is a defect: it breaks the
        // 'stat' hack, for instance. Only variables can have mangled name
        // clashes with extern "C" declarations, so only they deserve a
        // diagnostic.
      }
    }

    if (!Prev)
      return false;
  }

  // Use the first declaration's location to ensure we point at something which
  // is lexically inside an extern "C" linkage-spec.
  assert(Prev && "should have found a previous declaration to diagnose");
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(Prev))
    Prev = FD->getFirstDecl();
  else
    Prev = cast<VarDecl>(Prev)->getFirstDecl();

  S.Diag(ND->getLocation(), diag::err_extern_c_global_conflict)
    << IsGlobal << ND;
  S.Diag(Prev->getLocation(), diag::note_extern_c_global_conflict)
    << IsGlobal;
  return false;
}

/// Apply special rules for handling extern "C" declarations. Returns \c true
/// if we have found that this is a redeclaration of some prior entity.
///
/// Per C++ [dcl.link]p6:
///   Two declarations [for a function or variable] with C language linkage
///   with the same name that appear in different scopes refer to the same
///   [entity]. An entity with C language linkage shall not be declared with
///   the same name as an entity in global scope.
template<typename T>
static bool checkForConflictWithNonVisibleExternC(Sema &S, const T *ND,
                                                  LookupResult &Previous) {
  if (!S.getLangOpts().CPlusPlus) {
    // In C, when declaring a global variable, look for a corresponding 'extern'
    // variable declared in function scope. We don't need this in C++, because
    // we find local extern decls in the surrounding file-scope DeclContext.
    if (ND->getDeclContext()->getRedeclContext()->isTranslationUnit()) {
      if (NamedDecl *Prev = S.findLocallyScopedExternCDecl(ND->getDeclName())) {
        Previous.clear();
        Previous.addDecl(Prev);
        return true;
      }
    }
    return false;
  }

  // A declaration in the translation unit can conflict with an extern "C"
  // declaration.
  if (ND->getDeclContext()->getRedeclContext()->isTranslationUnit())
    return checkGlobalOrExternCConflict(S, ND, /*IsGlobal*/true, Previous);

  // An extern "C" declaration can conflict with a declaration in the
  // translation unit or can be a redeclaration of an extern "C" declaration
  // in another scope.
  if (isIncompleteDeclExternC(S,ND))
    return checkGlobalOrExternCConflict(S, ND, /*IsGlobal*/false, Previous);

  // Neither global nor extern "C": nothing to do.
  return false;
}

static bool CheckC23ConstexprVarType(Sema &SemaRef, SourceLocation VarLoc,
                                     QualType T) {
  QualType CanonT = SemaRef.Context.getCanonicalType(T);
  // C23 6.7.1p5: An object declared with storage-class specifier constexpr or
  // any of its members, even recursively, shall not have an atomic type, or a
  // variably modified type, or a type that is volatile or restrict qualified.
  if (CanonT->isVariablyModifiedType()) {
    SemaRef.Diag(VarLoc, diag::err_c23_constexpr_invalid_type) << T;
    return true;
  }

  // Arrays are qualified by their element type, so get the base type (this
  // works on non-arrays as well).
  CanonT = SemaRef.Context.getBaseElementType(CanonT);

  if (CanonT->isAtomicType() || CanonT.isVolatileQualified() ||
      CanonT.isRestrictQualified()) {
    SemaRef.Diag(VarLoc, diag::err_c23_constexpr_invalid_type) << T;
    return true;
  }

  if (CanonT->isRecordType()) {
    const RecordDecl *RD = CanonT->getAsRecordDecl();
    if (llvm::any_of(RD->fields(), [&SemaRef, VarLoc](const FieldDecl *F) {
          return CheckC23ConstexprVarType(SemaRef, VarLoc, F->getType());
        }))
      return true;
  }

  return false;
}

void Sema::CheckVariableDeclarationType(VarDecl *NewVD) {
  // If the decl is already known invalid, don't check it.
  if (NewVD->isInvalidDecl())
    return;

  QualType T = NewVD->getType();

  // Defer checking an 'auto' type until its initializer is attached.
  if (T->isUndeducedType())
    return;

  if (NewVD->hasAttrs())
    CheckAlignasUnderalignment(NewVD);

  if (T->isObjCObjectType()) {
    Diag(NewVD->getLocation(), diag::err_statically_allocated_object)
      << FixItHint::CreateInsertion(NewVD->getLocation(), "*");
    T = Context.getObjCObjectPointerType(T);
    NewVD->setType(T);
  }

  // Emit an error if an address space was applied to decl with local storage.
  // This includes arrays of objects with address space qualifiers, but not
  // automatic variables that point to other address spaces.
  // ISO/IEC TR 18037 S5.1.2
  if (!getLangOpts().OpenCL && NewVD->hasLocalStorage() &&
      T.getAddressSpace() != LangAS::Default) {
    Diag(NewVD->getLocation(), diag::err_as_qualified_auto_decl) << 0;
    NewVD->setInvalidDecl();
    return;
  }

  // OpenCL v1.2 s6.8 - The static qualifier is valid only in program
  // scope.
  if (getLangOpts().OpenCLVersion == 120 &&
      !getOpenCLOptions().isAvailableOption("cl_clang_storage_class_specifiers",
                                            getLangOpts()) &&
      NewVD->isStaticLocal()) {
    Diag(NewVD->getLocation(), diag::err_static_function_scope);
    NewVD->setInvalidDecl();
    return;
  }

  if (getLangOpts().OpenCL) {
    if (!diagnoseOpenCLTypes(*this, NewVD))
      return;

    // OpenCL v2.0 s6.12.5 - The __block storage type is not supported.
    if (NewVD->hasAttr<BlocksAttr>()) {
      Diag(NewVD->getLocation(), diag::err_opencl_block_storage_type);
      return;
    }

    if (T->isBlockPointerType()) {
      // OpenCL v2.0 s6.12.5 - Any block declaration must be const qualified and
      // can't use 'extern' storage class.
      if (!T.isConstQualified()) {
        Diag(NewVD->getLocation(), diag::err_opencl_invalid_block_declaration)
            << 0 /*const*/;
        NewVD->setInvalidDecl();
        return;
      }
      if (NewVD->hasExternalStorage()) {
        Diag(NewVD->getLocation(), diag::err_opencl_extern_block_declaration);
        NewVD->setInvalidDecl();
        return;
      }
    }

    // FIXME: Adding local AS in C++ for OpenCL might make sense.
    if (NewVD->isFileVarDecl() || NewVD->isStaticLocal() ||
        NewVD->hasExternalStorage()) {
      if (!T->isSamplerT() && !T->isDependentType() &&
          !(T.getAddressSpace() == LangAS::opencl_constant ||
            (T.getAddressSpace() == LangAS::opencl_global &&
             getOpenCLOptions().areProgramScopeVariablesSupported(
                 getLangOpts())))) {
        int Scope = NewVD->isStaticLocal() | NewVD->hasExternalStorage() << 1;
        if (getOpenCLOptions().areProgramScopeVariablesSupported(getLangOpts()))
          Diag(NewVD->getLocation(), diag::err_opencl_global_invalid_addr_space)
              << Scope << "global or constant";
        else
          Diag(NewVD->getLocation(), diag::err_opencl_global_invalid_addr_space)
              << Scope << "constant";
        NewVD->setInvalidDecl();
        return;
      }
    } else {
      if (T.getAddressSpace() == LangAS::opencl_global) {
        Diag(NewVD->getLocation(), diag::err_opencl_function_variable)
            << 1 /*is any function*/ << "global";
        NewVD->setInvalidDecl();
        return;
      }
      if (T.getAddressSpace() == LangAS::opencl_constant ||
          T.getAddressSpace() == LangAS::opencl_local) {
        FunctionDecl *FD = getCurFunctionDecl();
        // OpenCL v1.1 s6.5.2 and s6.5.3: no local or constant variables
        // in functions.
        if (FD && !FD->hasAttr<OpenCLKernelAttr>()) {
          if (T.getAddressSpace() == LangAS::opencl_constant)
            Diag(NewVD->getLocation(), diag::err_opencl_function_variable)
                << 0 /*non-kernel only*/ << "constant";
          else
            Diag(NewVD->getLocation(), diag::err_opencl_function_variable)
                << 0 /*non-kernel only*/ << "local";
          NewVD->setInvalidDecl();
          return;
        }
        // OpenCL v2.0 s6.5.2 and s6.5.3: local and constant variables must be
        // in the outermost scope of a kernel function.
        if (FD && FD->hasAttr<OpenCLKernelAttr>()) {
          if (!getCurScope()->isFunctionScope()) {
            if (T.getAddressSpace() == LangAS::opencl_constant)
              Diag(NewVD->getLocation(), diag::err_opencl_addrspace_scope)
                  << "constant";
            else
              Diag(NewVD->getLocation(), diag::err_opencl_addrspace_scope)
                  << "local";
            NewVD->setInvalidDecl();
            return;
          }
        }
      } else if (T.getAddressSpace() != LangAS::opencl_private &&
                 // If we are parsing a template we didn't deduce an addr
                 // space yet.
                 T.getAddressSpace() != LangAS::Default) {
        // Do not allow other address spaces on automatic variable.
        Diag(NewVD->getLocation(), diag::err_as_qualified_auto_decl) << 1;
        NewVD->setInvalidDecl();
        return;
      }
    }
  }

  if (NewVD->hasLocalStorage() && T.isObjCGCWeak()
      && !NewVD->hasAttr<BlocksAttr>()) {
    if (getLangOpts().getGC() != LangOptions::NonGC)
      Diag(NewVD->getLocation(), diag::warn_gc_attribute_weak_on_local);
    else {
      assert(!getLangOpts().ObjCAutoRefCount);
      Diag(NewVD->getLocation(), diag::warn_attribute_weak_on_local);
    }
  }

  // WebAssembly tables must be static with a zero length and can't be
  // declared within functions.
  if (T->isWebAssemblyTableType()) {
    if (getCurScope()->getParent()) { // Parent is null at top-level
      Diag(NewVD->getLocation(), diag::err_wasm_table_in_function);
      NewVD->setInvalidDecl();
      return;
    }
    if (NewVD->getStorageClass() != SC_Static) {
      Diag(NewVD->getLocation(), diag::err_wasm_table_must_be_static);
      NewVD->setInvalidDecl();
      return;
    }
    const auto *ATy = dyn_cast<ConstantArrayType>(T.getTypePtr());
    if (!ATy || ATy->getZExtSize() != 0) {
      Diag(NewVD->getLocation(),
           diag::err_typecheck_wasm_table_must_have_zero_length);
      NewVD->setInvalidDecl();
      return;
    }
  }

  bool isVM = T->isVariablyModifiedType();
  if (isVM || NewVD->hasAttr<CleanupAttr>() ||
      NewVD->hasAttr<BlocksAttr>())
    setFunctionHasBranchProtectedScope();

  if ((isVM && NewVD->hasLinkage()) ||
      (T->isVariableArrayType() && NewVD->hasGlobalStorage())) {
    bool SizeIsNegative;
    llvm::APSInt Oversized;
    TypeSourceInfo *FixedTInfo = TryToFixInvalidVariablyModifiedTypeSourceInfo(
        NewVD->getTypeSourceInfo(), Context, SizeIsNegative, Oversized);
    QualType FixedT;
    if (FixedTInfo &&  T == NewVD->getTypeSourceInfo()->getType())
      FixedT = FixedTInfo->getType();
    else if (FixedTInfo) {
      // Type and type-as-written are canonically different. We need to fix up
      // both types separately.
      FixedT = TryToFixInvalidVariablyModifiedType(T, Context, SizeIsNegative,
                                                   Oversized);
    }
    if ((!FixedTInfo || FixedT.isNull()) && T->isVariableArrayType()) {
      const VariableArrayType *VAT = Context.getAsVariableArrayType(T);
      // FIXME: This won't give the correct result for
      // int a[10][n];
      SourceRange SizeRange = VAT->getSizeExpr()->getSourceRange();

      if (NewVD->isFileVarDecl())
        Diag(NewVD->getLocation(), diag::err_vla_decl_in_file_scope)
        << SizeRange;
      else if (NewVD->isStaticLocal())
        Diag(NewVD->getLocation(), diag::err_vla_decl_has_static_storage)
        << SizeRange;
      else
        Diag(NewVD->getLocation(), diag::err_vla_decl_has_extern_linkage)
        << SizeRange;
      NewVD->setInvalidDecl();
      return;
    }

    if (!FixedTInfo) {
      if (NewVD->isFileVarDecl())
        Diag(NewVD->getLocation(), diag::err_vm_decl_in_file_scope);
      else
        Diag(NewVD->getLocation(), diag::err_vm_decl_has_extern_linkage);
      NewVD->setInvalidDecl();
      return;
    }

    Diag(NewVD->getLocation(), diag::ext_vla_folded_to_constant);
    NewVD->setType(FixedT);
    NewVD->setTypeSourceInfo(FixedTInfo);
  }

  if (T->isVoidType()) {
    // C++98 [dcl.stc]p5: The extern specifier can be applied only to the names
    //                    of objects and functions.
    if (NewVD->isThisDeclarationADefinition() || getLangOpts().CPlusPlus) {
      Diag(NewVD->getLocation(), diag::err_typecheck_decl_incomplete_type)
        << T;
      NewVD->setInvalidDecl();
      return;
    }
  }

  if (!NewVD->hasLocalStorage() && NewVD->hasAttr<BlocksAttr>()) {
    Diag(NewVD->getLocation(), diag::err_block_on_nonlocal);
    NewVD->setInvalidDecl();
    return;
  }

  if (!NewVD->hasLocalStorage() && T->isSizelessType() &&
      !T.isWebAssemblyReferenceType()) {
    Diag(NewVD->getLocation(), diag::err_sizeless_nonlocal) << T;
    NewVD->setInvalidDecl();
    return;
  }

  if (isVM && NewVD->hasAttr<BlocksAttr>()) {
    Diag(NewVD->getLocation(), diag::err_block_on_vm);
    NewVD->setInvalidDecl();
    return;
  }

  if (getLangOpts().C23 && NewVD->isConstexpr() &&
      CheckC23ConstexprVarType(*this, NewVD->getLocation(), T)) {
    NewVD->setInvalidDecl();
    return;
  }

  if (NewVD->isConstexpr() && !T->isDependentType() &&
      RequireLiteralType(NewVD->getLocation(), T,
                         diag::err_constexpr_var_non_literal)) {
    NewVD->setInvalidDecl();
    return;
  }

  // PPC MMA non-pointer types are not allowed as non-local variable types.
  if (Context.getTargetInfo().getTriple().isPPC64() &&
      !NewVD->isLocalVarDecl() &&
      PPC().CheckPPCMMAType(T, NewVD->getLocation())) {
    NewVD->setInvalidDecl();
    return;
  }

  // Check that SVE types are only used in functions with SVE available.
  if (T->isSVESizelessBuiltinType() && isa<FunctionDecl>(CurContext)) {
    const FunctionDecl *FD = cast<FunctionDecl>(CurContext);
    llvm::StringMap<bool> CallerFeatureMap;
    Context.getFunctionFeatureMap(CallerFeatureMap, FD);

    if (!Builtin::evaluateRequiredTargetFeatures("sve", CallerFeatureMap)) {
      if (!Builtin::evaluateRequiredTargetFeatures("sme", CallerFeatureMap)) {
        Diag(NewVD->getLocation(), diag::err_sve_vector_in_non_sve_target) << T;
        NewVD->setInvalidDecl();
        return;
      } else if (!IsArmStreamingFunction(FD,
                                         /*IncludeLocallyStreaming=*/true)) {
        Diag(NewVD->getLocation(),
             diag::err_sve_vector_in_non_streaming_function)
            << T;
        NewVD->setInvalidDecl();
        return;
      }
    }
  }

  if (T->isRVVSizelessBuiltinType() && isa<FunctionDecl>(CurContext)) {
    const FunctionDecl *FD = cast<FunctionDecl>(CurContext);
    llvm::StringMap<bool> CallerFeatureMap;
    Context.getFunctionFeatureMap(CallerFeatureMap, FD);
    RISCV().checkRVVTypeSupport(T, NewVD->getLocation(), cast<Decl>(CurContext),
                                CallerFeatureMap);
  }
}

bool Sema::CheckVariableDeclaration(VarDecl *NewVD, LookupResult &Previous) {
  CheckVariableDeclarationType(NewVD);

  // If the decl is already known invalid, don't check it.
  if (NewVD->isInvalidDecl())
    return false;

  // If we did not find anything by this name, look for a non-visible
  // extern "C" declaration with the same name.
  if (Previous.empty() &&
      checkForConflictWithNonVisibleExternC(*this, NewVD, Previous))
    Previous.setShadowed();

  if (!Previous.empty()) {
    MergeVarDecl(NewVD, Previous);
    return true;
  }
  return false;
}

bool Sema::AddOverriddenMethods(CXXRecordDecl *DC, CXXMethodDecl *MD) {
  llvm::SmallPtrSet<const CXXMethodDecl*, 4> Overridden;

  // Look for methods in base classes that this method might override.
  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/false,
                     /*DetectVirtual=*/false);
  auto VisitBase = [&] (const CXXBaseSpecifier *Specifier, CXXBasePath &Path) {
    CXXRecordDecl *BaseRecord = Specifier->getType()->getAsCXXRecordDecl();
    DeclarationName Name = MD->getDeclName();

    if (Name.getNameKind() == DeclarationName::CXXDestructorName) {
      // We really want to find the base class destructor here.
      QualType T = Context.getTypeDeclType(BaseRecord);
      CanQualType CT = Context.getCanonicalType(T);
      Name = Context.DeclarationNames.getCXXDestructorName(CT);
    }

    for (NamedDecl *BaseND : BaseRecord->lookup(Name)) {
      CXXMethodDecl *BaseMD =
          dyn_cast<CXXMethodDecl>(BaseND->getCanonicalDecl());
      if (!BaseMD || !BaseMD->isVirtual() ||
          IsOverride(MD, BaseMD, /*UseMemberUsingDeclRules=*/false,
                     /*ConsiderCudaAttrs=*/true))
        continue;
      if (!CheckExplicitObjectOverride(MD, BaseMD))
        continue;
      if (Overridden.insert(BaseMD).second) {
        MD->addOverriddenMethod(BaseMD);
        CheckOverridingFunctionReturnType(MD, BaseMD);
        CheckOverridingFunctionAttributes(MD, BaseMD);
        CheckOverridingFunctionExceptionSpec(MD, BaseMD);
        CheckIfOverriddenFunctionIsMarkedFinal(MD, BaseMD);
      }

      // A method can only override one function from each base class. We
      // don't track indirectly overridden methods from bases of bases.
      return true;
    }

    return false;
  };

  DC->lookupInBases(VisitBase, Paths);
  return !Overridden.empty();
}

namespace {
  // Struct for holding all of the extra arguments needed by
  // DiagnoseInvalidRedeclaration to call Sema::ActOnFunctionDeclarator.
  struct ActOnFDArgs {
    Scope *S;
    Declarator &D;
    MultiTemplateParamsArg TemplateParamLists;
    bool AddToScope;
  };
} // end anonymous namespace

namespace {

// Callback to only accept typo corrections that have a non-zero edit distance.
// Also only accept corrections that have the same parent decl.
class DifferentNameValidatorCCC final : public CorrectionCandidateCallback {
 public:
  DifferentNameValidatorCCC(ASTContext &Context, FunctionDecl *TypoFD,
                            CXXRecordDecl *Parent)
      : Context(Context), OriginalFD(TypoFD),
        ExpectedParent(Parent ? Parent->getCanonicalDecl() : nullptr) {}

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    if (candidate.getEditDistance() == 0)
      return false;

    SmallVector<unsigned, 1> MismatchedParams;
    for (TypoCorrection::const_decl_iterator CDecl = candidate.begin(),
                                          CDeclEnd = candidate.end();
         CDecl != CDeclEnd; ++CDecl) {
      FunctionDecl *FD = dyn_cast<FunctionDecl>(*CDecl);

      if (FD && !FD->hasBody() &&
          hasSimilarParameters(Context, FD, OriginalFD, MismatchedParams)) {
        if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD)) {
          CXXRecordDecl *Parent = MD->getParent();
          if (Parent && Parent->getCanonicalDecl() == ExpectedParent)
            return true;
        } else if (!ExpectedParent) {
          return true;
        }
      }
    }

    return false;
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<DifferentNameValidatorCCC>(*this);
  }

 private:
  ASTContext &Context;
  FunctionDecl *OriginalFD;
  CXXRecordDecl *ExpectedParent;
};

} // end anonymous namespace

void Sema::MarkTypoCorrectedFunctionDefinition(const NamedDecl *F) {
  TypoCorrectedFunctionDefinitions.insert(F);
}

/// Generate diagnostics for an invalid function redeclaration.
///
/// This routine handles generating the diagnostic messages for an invalid
/// function redeclaration, including finding possible similar declarations
/// or performing typo correction if there are no previous declarations with
/// the same name.
///
/// Returns a NamedDecl iff typo correction was performed and substituting in
/// the new declaration name does not cause new errors.
static NamedDecl *DiagnoseInvalidRedeclaration(
    Sema &SemaRef, LookupResult &Previous, FunctionDecl *NewFD,
    ActOnFDArgs &ExtraArgs, bool IsLocalFriend, Scope *S) {
  DeclarationName Name = NewFD->getDeclName();
  DeclContext *NewDC = NewFD->getDeclContext();
  SmallVector<unsigned, 1> MismatchedParams;
  SmallVector<std::pair<FunctionDecl *, unsigned>, 1> NearMatches;
  TypoCorrection Correction;
  bool IsDefinition = ExtraArgs.D.isFunctionDefinition();
  unsigned DiagMsg =
    IsLocalFriend ? diag::err_no_matching_local_friend :
    NewFD->getFriendObjectKind() ? diag::err_qualified_friend_no_match :
    diag::err_member_decl_does_not_match;
  LookupResult Prev(SemaRef, Name, NewFD->getLocation(),
                    IsLocalFriend ? Sema::LookupLocalFriendName
                                  : Sema::LookupOrdinaryName,
                    RedeclarationKind::ForVisibleRedeclaration);

  NewFD->setInvalidDecl();
  if (IsLocalFriend)
    SemaRef.LookupName(Prev, S);
  else
    SemaRef.LookupQualifiedName(Prev, NewDC);
  assert(!Prev.isAmbiguous() &&
         "Cannot have an ambiguity in previous-declaration lookup");
  CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(NewFD);
  DifferentNameValidatorCCC CCC(SemaRef.Context, NewFD,
                                MD ? MD->getParent() : nullptr);
  if (!Prev.empty()) {
    for (LookupResult::iterator Func = Prev.begin(), FuncEnd = Prev.end();
         Func != FuncEnd; ++Func) {
      FunctionDecl *FD = dyn_cast<FunctionDecl>(*Func);
      if (FD &&
          hasSimilarParameters(SemaRef.Context, FD, NewFD, MismatchedParams)) {
        // Add 1 to the index so that 0 can mean the mismatch didn't
        // involve a parameter
        unsigned ParamNum =
            MismatchedParams.empty() ? 0 : MismatchedParams.front() + 1;
        NearMatches.push_back(std::make_pair(FD, ParamNum));
      }
    }
  // If the qualified name lookup yielded nothing, try typo correction
  } else if ((Correction = SemaRef.CorrectTypo(
                  Prev.getLookupNameInfo(), Prev.getLookupKind(), S,
                  &ExtraArgs.D.getCXXScopeSpec(), CCC, Sema::CTK_ErrorRecovery,
                  IsLocalFriend ? nullptr : NewDC))) {
    // Set up everything for the call to ActOnFunctionDeclarator
    ExtraArgs.D.SetIdentifier(Correction.getCorrectionAsIdentifierInfo(),
                              ExtraArgs.D.getIdentifierLoc());
    Previous.clear();
    Previous.setLookupName(Correction.getCorrection());
    for (TypoCorrection::decl_iterator CDecl = Correction.begin(),
                                    CDeclEnd = Correction.end();
         CDecl != CDeclEnd; ++CDecl) {
      FunctionDecl *FD = dyn_cast<FunctionDecl>(*CDecl);
      if (FD && !FD->hasBody() &&
          hasSimilarParameters(SemaRef.Context, FD, NewFD, MismatchedParams)) {
        Previous.addDecl(FD);
      }
    }
    bool wasRedeclaration = ExtraArgs.D.isRedeclaration();

    NamedDecl *Result;
    // Retry building the function declaration with the new previous
    // declarations, and with errors suppressed.
    {
      // Trap errors.
      Sema::SFINAETrap Trap(SemaRef);

      // TODO: Refactor ActOnFunctionDeclarator so that we can call only the
      // pieces need to verify the typo-corrected C++ declaration and hopefully
      // eliminate the need for the parameter pack ExtraArgs.
      Result = SemaRef.ActOnFunctionDeclarator(
          ExtraArgs.S, ExtraArgs.D,
          Correction.getCorrectionDecl()->getDeclContext(),
          NewFD->getTypeSourceInfo(), Previous, ExtraArgs.TemplateParamLists,
          ExtraArgs.AddToScope);

      if (Trap.hasErrorOccurred())
        Result = nullptr;
    }

    if (Result) {
      // Determine which correction we picked.
      Decl *Canonical = Result->getCanonicalDecl();
      for (LookupResult::iterator I = Previous.begin(), E = Previous.end();
           I != E; ++I)
        if ((*I)->getCanonicalDecl() == Canonical)
          Correction.setCorrectionDecl(*I);

      // Let Sema know about the correction.
      SemaRef.MarkTypoCorrectedFunctionDefinition(Result);
      SemaRef.diagnoseTypo(
          Correction,
          SemaRef.PDiag(IsLocalFriend
                          ? diag::err_no_matching_local_friend_suggest
                          : diag::err_member_decl_does_not_match_suggest)
            << Name << NewDC << IsDefinition);
      return Result;
    }

    // Pretend the typo correction never occurred
    ExtraArgs.D.SetIdentifier(Name.getAsIdentifierInfo(),
                              ExtraArgs.D.getIdentifierLoc());
    ExtraArgs.D.setRedeclaration(wasRedeclaration);
    Previous.clear();
    Previous.setLookupName(Name);
  }

  SemaRef.Diag(NewFD->getLocation(), DiagMsg)
      << Name << NewDC << IsDefinition << NewFD->getLocation();

  bool NewFDisConst = false;
  if (CXXMethodDecl *NewMD = dyn_cast<CXXMethodDecl>(NewFD))
    NewFDisConst = NewMD->isConst();

  for (SmallVectorImpl<std::pair<FunctionDecl *, unsigned> >::iterator
       NearMatch = NearMatches.begin(), NearMatchEnd = NearMatches.end();
       NearMatch != NearMatchEnd; ++NearMatch) {
    FunctionDecl *FD = NearMatch->first;
    CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(FD);
    bool FDisConst = MD && MD->isConst();
    bool IsMember = MD || !IsLocalFriend;

    // FIXME: These notes are poorly worded for the local friend case.
    if (unsigned Idx = NearMatch->second) {
      ParmVarDecl *FDParam = FD->getParamDecl(Idx-1);
      SourceLocation Loc = FDParam->getTypeSpecStartLoc();
      if (Loc.isInvalid()) Loc = FD->getLocation();
      SemaRef.Diag(Loc, IsMember ? diag::note_member_def_close_param_match
                                 : diag::note_local_decl_close_param_match)
        << Idx << FDParam->getType()
        << NewFD->getParamDecl(Idx - 1)->getType();
    } else if (FDisConst != NewFDisConst) {
      auto DB = SemaRef.Diag(FD->getLocation(),
                             diag::note_member_def_close_const_match)
                << NewFDisConst << FD->getSourceRange().getEnd();
      if (const auto &FTI = ExtraArgs.D.getFunctionTypeInfo(); !NewFDisConst)
        DB << FixItHint::CreateInsertion(FTI.getRParenLoc().getLocWithOffset(1),
                                         " const");
      else if (FTI.hasMethodTypeQualifiers() &&
               FTI.getConstQualifierLoc().isValid())
        DB << FixItHint::CreateRemoval(FTI.getConstQualifierLoc());
    } else {
      SemaRef.Diag(FD->getLocation(),
                   IsMember ? diag::note_member_def_close_match
                            : diag::note_local_decl_close_match);
    }
  }
  return nullptr;
}

static StorageClass getFunctionStorageClass(Sema &SemaRef, Declarator &D) {
  switch (D.getDeclSpec().getStorageClassSpec()) {
  default: llvm_unreachable("Unknown storage class!");
  case DeclSpec::SCS_auto:
  case DeclSpec::SCS_register:
  case DeclSpec::SCS_mutable:
    SemaRef.Diag(D.getDeclSpec().getStorageClassSpecLoc(),
                 diag::err_typecheck_sclass_func);
    D.getMutableDeclSpec().ClearStorageClassSpecs();
    D.setInvalidType();
    break;
  case DeclSpec::SCS_unspecified: break;
  case DeclSpec::SCS_extern:
    if (D.getDeclSpec().isExternInLinkageSpec())
      return SC_None;
    return SC_Extern;
  case DeclSpec::SCS_static: {
    if (SemaRef.CurContext->getRedeclContext()->isFunctionOrMethod()) {
      // C99 6.7.1p5:
      //   The declaration of an identifier for a function that has
      //   block scope shall have no explicit storage-class specifier
      //   other than extern
      // See also (C++ [dcl.stc]p4).
      SemaRef.Diag(D.getDeclSpec().getStorageClassSpecLoc(),
                   diag::err_static_block_func);
      break;
    } else
      return SC_Static;
  }
  case DeclSpec::SCS_private_extern: return SC_PrivateExtern;
  }

  // No explicit storage class has already been returned
  return SC_None;
}

static FunctionDecl *CreateNewFunctionDecl(Sema &SemaRef, Declarator &D,
                                           DeclContext *DC, QualType &R,
                                           TypeSourceInfo *TInfo,
                                           StorageClass SC,
                                           bool &IsVirtualOkay) {
  DeclarationNameInfo NameInfo = SemaRef.GetNameForDeclarator(D);
  DeclarationName Name = NameInfo.getName();

  FunctionDecl *NewFD = nullptr;
  bool isInline = D.getDeclSpec().isInlineSpecified();

  ConstexprSpecKind ConstexprKind = D.getDeclSpec().getConstexprSpecifier();
  if (ConstexprKind == ConstexprSpecKind::Constinit ||
      (SemaRef.getLangOpts().C23 &&
       ConstexprKind == ConstexprSpecKind::Constexpr)) {

    if (SemaRef.getLangOpts().C23)
      SemaRef.Diag(D.getDeclSpec().getConstexprSpecLoc(),
                   diag::err_c23_constexpr_not_variable);
    else
      SemaRef.Diag(D.getDeclSpec().getConstexprSpecLoc(),
                   diag::err_constexpr_wrong_decl_kind)
          << static_cast<int>(ConstexprKind);
    ConstexprKind = ConstexprSpecKind::Unspecified;
    D.getMutableDeclSpec().ClearConstexprSpec();
  }

  if (!SemaRef.getLangOpts().CPlusPlus) {
    // Determine whether the function was written with a prototype. This is
    // true when:
    //   - there is a prototype in the declarator, or
    //   - the type R of the function is some kind of typedef or other non-
    //     attributed reference to a type name (which eventually refers to a
    //     function type). Note, we can't always look at the adjusted type to
    //     check this case because attributes may cause a non-function
    //     declarator to still have a function type. e.g.,
    //       typedef void func(int a);
    //       __attribute__((noreturn)) func other_func; // This has a prototype
    bool HasPrototype =
        (D.isFunctionDeclarator() && D.getFunctionTypeInfo().hasPrototype) ||
        (D.getDeclSpec().isTypeRep() &&
         SemaRef.GetTypeFromParser(D.getDeclSpec().getRepAsType(), nullptr)
             ->isFunctionProtoType()) ||
        (!R->getAsAdjusted<FunctionType>() && R->isFunctionProtoType());
    assert(
        (HasPrototype || !SemaRef.getLangOpts().requiresStrictPrototypes()) &&
        "Strict prototypes are required");

    NewFD = FunctionDecl::Create(
        SemaRef.Context, DC, D.getBeginLoc(), NameInfo, R, TInfo, SC,
        SemaRef.getCurFPFeatures().isFPConstrained(), isInline, HasPrototype,
        ConstexprSpecKind::Unspecified,
        /*TrailingRequiresClause=*/nullptr);
    if (D.isInvalidType())
      NewFD->setInvalidDecl();

    return NewFD;
  }

  ExplicitSpecifier ExplicitSpecifier = D.getDeclSpec().getExplicitSpecifier();
  Expr *TrailingRequiresClause = D.getTrailingRequiresClause();

  SemaRef.CheckExplicitObjectMemberFunction(DC, D, Name, R);

  if (Name.getNameKind() == DeclarationName::CXXConstructorName) {
    // This is a C++ constructor declaration.
    assert(DC->isRecord() &&
           "Constructors can only be declared in a member context");

    R = SemaRef.CheckConstructorDeclarator(D, R, SC);
    return CXXConstructorDecl::Create(
        SemaRef.Context, cast<CXXRecordDecl>(DC), D.getBeginLoc(), NameInfo, R,
        TInfo, ExplicitSpecifier, SemaRef.getCurFPFeatures().isFPConstrained(),
        isInline, /*isImplicitlyDeclared=*/false, ConstexprKind,
        InheritedConstructor(), TrailingRequiresClause);

  } else if (Name.getNameKind() == DeclarationName::CXXDestructorName) {
    // This is a C++ destructor declaration.
    if (DC->isRecord()) {
      R = SemaRef.CheckDestructorDeclarator(D, R, SC);
      CXXRecordDecl *Record = cast<CXXRecordDecl>(DC);
      CXXDestructorDecl *NewDD = CXXDestructorDecl::Create(
          SemaRef.Context, Record, D.getBeginLoc(), NameInfo, R, TInfo,
          SemaRef.getCurFPFeatures().isFPConstrained(), isInline,
          /*isImplicitlyDeclared=*/false, ConstexprKind,
          TrailingRequiresClause);
      // User defined destructors start as not selected if the class definition is still
      // not done.
      if (Record->isBeingDefined())
        NewDD->setIneligibleOrNotSelected(true);

      // If the destructor needs an implicit exception specification, set it
      // now. FIXME: It'd be nice to be able to create the right type to start
      // with, but the type needs to reference the destructor declaration.
      if (SemaRef.getLangOpts().CPlusPlus11)
        SemaRef.AdjustDestructorExceptionSpec(NewDD);

      IsVirtualOkay = true;
      return NewDD;

    } else {
      SemaRef.Diag(D.getIdentifierLoc(), diag::err_destructor_not_member);
      D.setInvalidType();

      // Create a FunctionDecl to satisfy the function definition parsing
      // code path.
      return FunctionDecl::Create(
          SemaRef.Context, DC, D.getBeginLoc(), D.getIdentifierLoc(), Name, R,
          TInfo, SC, SemaRef.getCurFPFeatures().isFPConstrained(), isInline,
          /*hasPrototype=*/true, ConstexprKind, TrailingRequiresClause);
    }

  } else if (Name.getNameKind() == DeclarationName::CXXConversionFunctionName) {
    if (!DC->isRecord()) {
      SemaRef.Diag(D.getIdentifierLoc(),
           diag::err_conv_function_not_member);
      return nullptr;
    }

    SemaRef.CheckConversionDeclarator(D, R, SC);
    if (D.isInvalidType())
      return nullptr;

    IsVirtualOkay = true;
    return CXXConversionDecl::Create(
        SemaRef.Context, cast<CXXRecordDecl>(DC), D.getBeginLoc(), NameInfo, R,
        TInfo, SemaRef.getCurFPFeatures().isFPConstrained(), isInline,
        ExplicitSpecifier, ConstexprKind, SourceLocation(),
        TrailingRequiresClause);

  } else if (Name.getNameKind() == DeclarationName::CXXDeductionGuideName) {
    if (TrailingRequiresClause)
      SemaRef.Diag(TrailingRequiresClause->getBeginLoc(),
                   diag::err_trailing_requires_clause_on_deduction_guide)
          << TrailingRequiresClause->getSourceRange();
    if (SemaRef.CheckDeductionGuideDeclarator(D, R, SC))
      return nullptr;
    return CXXDeductionGuideDecl::Create(SemaRef.Context, DC, D.getBeginLoc(),
                                         ExplicitSpecifier, NameInfo, R, TInfo,
                                         D.getEndLoc());
  } else if (DC->isRecord()) {
    // If the name of the function is the same as the name of the record,
    // then this must be an invalid constructor that has a return type.
    // (The parser checks for a return type and makes the declarator a
    // constructor if it has no return type).
    if (Name.getAsIdentifierInfo() &&
        Name.getAsIdentifierInfo() == cast<CXXRecordDecl>(DC)->getIdentifier()){
      SemaRef.Diag(D.getIdentifierLoc(), diag::err_constructor_return_type)
        << SourceRange(D.getDeclSpec().getTypeSpecTypeLoc())
        << SourceRange(D.getIdentifierLoc());
      return nullptr;
    }

    // This is a C++ method declaration.
    CXXMethodDecl *Ret = CXXMethodDecl::Create(
        SemaRef.Context, cast<CXXRecordDecl>(DC), D.getBeginLoc(), NameInfo, R,
        TInfo, SC, SemaRef.getCurFPFeatures().isFPConstrained(), isInline,
        ConstexprKind, SourceLocation(), TrailingRequiresClause);
    IsVirtualOkay = !Ret->isStatic();
    return Ret;
  } else {
    bool isFriend =
        SemaRef.getLangOpts().CPlusPlus && D.getDeclSpec().isFriendSpecified();
    if (!isFriend && SemaRef.CurContext->isRecord())
      return nullptr;

    // Determine whether the function was written with a
    // prototype. This true when:
    //   - we're in C++ (where every function has a prototype),
    return FunctionDecl::Create(
        SemaRef.Context, DC, D.getBeginLoc(), NameInfo, R, TInfo, SC,
        SemaRef.getCurFPFeatures().isFPConstrained(), isInline,
        true /*HasPrototype*/, ConstexprKind, TrailingRequiresClause);
  }
}

enum OpenCLParamType {
  ValidKernelParam,
  PtrPtrKernelParam,
  PtrKernelParam,
  InvalidAddrSpacePtrKernelParam,
  InvalidKernelParam,
  RecordKernelParam
};

static bool isOpenCLSizeDependentType(ASTContext &C, QualType Ty) {
  // Size dependent types are just typedefs to normal integer types
  // (e.g. unsigned long), so we cannot distinguish them from other typedefs to
  // integers other than by their names.
  StringRef SizeTypeNames[] = {"size_t", "intptr_t", "uintptr_t", "ptrdiff_t"};

  // Remove typedefs one by one until we reach a typedef
  // for a size dependent type.
  QualType DesugaredTy = Ty;
  do {
    ArrayRef<StringRef> Names(SizeTypeNames);
    auto Match = llvm::find(Names, DesugaredTy.getUnqualifiedType().getAsString());
    if (Names.end() != Match)
      return true;

    Ty = DesugaredTy;
    DesugaredTy = Ty.getSingleStepDesugaredType(C);
  } while (DesugaredTy != Ty);

  return false;
}

static OpenCLParamType getOpenCLKernelParameterType(Sema &S, QualType PT) {
  if (PT->isDependentType())
    return InvalidKernelParam;

  if (PT->isPointerType() || PT->isReferenceType()) {
    QualType PointeeType = PT->getPointeeType();
    if (PointeeType.getAddressSpace() == LangAS::opencl_generic ||
        PointeeType.getAddressSpace() == LangAS::opencl_private ||
        PointeeType.getAddressSpace() == LangAS::Default)
      return InvalidAddrSpacePtrKernelParam;

    if (PointeeType->isPointerType()) {
      // This is a pointer to pointer parameter.
      // Recursively check inner type.
      OpenCLParamType ParamKind = getOpenCLKernelParameterType(S, PointeeType);
      if (ParamKind == InvalidAddrSpacePtrKernelParam ||
          ParamKind == InvalidKernelParam)
        return ParamKind;

      // OpenCL v3.0 s6.11.a:
      // A restriction to pass pointers to pointers only applies to OpenCL C
      // v1.2 or below.
      if (S.getLangOpts().getOpenCLCompatibleVersion() > 120)
        return ValidKernelParam;

      return PtrPtrKernelParam;
    }

    // C++ for OpenCL v1.0 s2.4:
    // Moreover the types used in parameters of the kernel functions must be:
    // Standard layout types for pointer parameters. The same applies to
    // reference if an implementation supports them in kernel parameters.
    if (S.getLangOpts().OpenCLCPlusPlus &&
        !S.getOpenCLOptions().isAvailableOption(
            "__cl_clang_non_portable_kernel_param_types", S.getLangOpts())) {
     auto CXXRec = PointeeType.getCanonicalType()->getAsCXXRecordDecl();
     bool IsStandardLayoutType = true;
     if (CXXRec) {
       // If template type is not ODR-used its definition is only available
       // in the template definition not its instantiation.
       // FIXME: This logic doesn't work for types that depend on template
       // parameter (PR58590).
       if (!CXXRec->hasDefinition())
         CXXRec = CXXRec->getTemplateInstantiationPattern();
       if (!CXXRec || !CXXRec->hasDefinition() || !CXXRec->isStandardLayout())
         IsStandardLayoutType = false;
     }
     if (!PointeeType->isAtomicType() && !PointeeType->isVoidType() &&
        !IsStandardLayoutType)
      return InvalidKernelParam;
    }

    // OpenCL v1.2 s6.9.p:
    // A restriction to pass pointers only applies to OpenCL C v1.2 or below.
    if (S.getLangOpts().getOpenCLCompatibleVersion() > 120)
      return ValidKernelParam;

    return PtrKernelParam;
  }

  // OpenCL v1.2 s6.9.k:
  // Arguments to kernel functions in a program cannot be declared with the
  // built-in scalar types bool, half, size_t, ptrdiff_t, intptr_t, and
  // uintptr_t or a struct and/or union that contain fields declared to be one
  // of these built-in scalar types.
  if (isOpenCLSizeDependentType(S.getASTContext(), PT))
    return InvalidKernelParam;

  if (PT->isImageType())
    return PtrKernelParam;

  if (PT->isBooleanType() || PT->isEventT() || PT->isReserveIDT())
    return InvalidKernelParam;

  // OpenCL extension spec v1.2 s9.5:
  // This extension adds support for half scalar and vector types as built-in
  // types that can be used for arithmetic operations, conversions etc.
  if (!S.getOpenCLOptions().isAvailableOption("cl_khr_fp16", S.getLangOpts()) &&
      PT->isHalfType())
    return InvalidKernelParam;

  // Look into an array argument to check if it has a forbidden type.
  if (PT->isArrayType()) {
    const Type *UnderlyingTy = PT->getPointeeOrArrayElementType();
    // Call ourself to check an underlying type of an array. Since the
    // getPointeeOrArrayElementType returns an innermost type which is not an
    // array, this recursive call only happens once.
    return getOpenCLKernelParameterType(S, QualType(UnderlyingTy, 0));
  }

  // C++ for OpenCL v1.0 s2.4:
  // Moreover the types used in parameters of the kernel functions must be:
  // Trivial and standard-layout types C++17 [basic.types] (plain old data
  // types) for parameters passed by value;
  if (S.getLangOpts().OpenCLCPlusPlus &&
      !S.getOpenCLOptions().isAvailableOption(
          "__cl_clang_non_portable_kernel_param_types", S.getLangOpts()) &&
      !PT->isOpenCLSpecificType() && !PT.isPODType(S.Context))
    return InvalidKernelParam;

  if (PT->isRecordType())
    return RecordKernelParam;

  return ValidKernelParam;
}

static void checkIsValidOpenCLKernelParameter(
  Sema &S,
  Declarator &D,
  ParmVarDecl *Param,
  llvm::SmallPtrSetImpl<const Type *> &ValidTypes) {
  QualType PT = Param->getType();

  // Cache the valid types we encounter to avoid rechecking structs that are
  // used again
  if (ValidTypes.count(PT.getTypePtr()))
    return;

  switch (getOpenCLKernelParameterType(S, PT)) {
  case PtrPtrKernelParam:
    // OpenCL v3.0 s6.11.a:
    // A kernel function argument cannot be declared as a pointer to a pointer
    // type. [...] This restriction only applies to OpenCL C 1.2 or below.
    S.Diag(Param->getLocation(), diag::err_opencl_ptrptr_kernel_param);
    D.setInvalidType();
    return;

  case InvalidAddrSpacePtrKernelParam:
    // OpenCL v1.0 s6.5:
    // __kernel function arguments declared to be a pointer of a type can point
    // to one of the following address spaces only : __global, __local or
    // __constant.
    S.Diag(Param->getLocation(), diag::err_kernel_arg_address_space);
    D.setInvalidType();
    return;

    // OpenCL v1.2 s6.9.k:
    // Arguments to kernel functions in a program cannot be declared with the
    // built-in scalar types bool, half, size_t, ptrdiff_t, intptr_t, and
    // uintptr_t or a struct and/or union that contain fields declared to be
    // one of these built-in scalar types.

  case InvalidKernelParam:
    // OpenCL v1.2 s6.8 n:
    // A kernel function argument cannot be declared
    // of event_t type.
    // Do not diagnose half type since it is diagnosed as invalid argument
    // type for any function elsewhere.
    if (!PT->isHalfType()) {
      S.Diag(Param->getLocation(), diag::err_bad_kernel_param_type) << PT;

      // Explain what typedefs are involved.
      const TypedefType *Typedef = nullptr;
      while ((Typedef = PT->getAs<TypedefType>())) {
        SourceLocation Loc = Typedef->getDecl()->getLocation();
        // SourceLocation may be invalid for a built-in type.
        if (Loc.isValid())
          S.Diag(Loc, diag::note_entity_declared_at) << PT;
        PT = Typedef->desugar();
      }
    }

    D.setInvalidType();
    return;

  case PtrKernelParam:
  case ValidKernelParam:
    ValidTypes.insert(PT.getTypePtr());
    return;

  case RecordKernelParam:
    break;
  }

  // Track nested structs we will inspect
  SmallVector<const Decl *, 4> VisitStack;

  // Track where we are in the nested structs. Items will migrate from
  // VisitStack to HistoryStack as we do the DFS for bad field.
  SmallVector<const FieldDecl *, 4> HistoryStack;
  HistoryStack.push_back(nullptr);

  // At this point we already handled everything except of a RecordType or
  // an ArrayType of a RecordType.
  assert((PT->isArrayType() || PT->isRecordType()) && "Unexpected type.");
  const RecordType *RecTy =
      PT->getPointeeOrArrayElementType()->getAs<RecordType>();
  const RecordDecl *OrigRecDecl = RecTy->getDecl();

  VisitStack.push_back(RecTy->getDecl());
  assert(VisitStack.back() && "First decl null?");

  do {
    const Decl *Next = VisitStack.pop_back_val();
    if (!Next) {
      assert(!HistoryStack.empty());
      // Found a marker, we have gone up a level
      if (const FieldDecl *Hist = HistoryStack.pop_back_val())
        ValidTypes.insert(Hist->getType().getTypePtr());

      continue;
    }

    // Adds everything except the original parameter declaration (which is not a
    // field itself) to the history stack.
    const RecordDecl *RD;
    if (const FieldDecl *Field = dyn_cast<FieldDecl>(Next)) {
      HistoryStack.push_back(Field);

      QualType FieldTy = Field->getType();
      // Other field types (known to be valid or invalid) are handled while we
      // walk around RecordDecl::fields().
      assert((FieldTy->isArrayType() || FieldTy->isRecordType()) &&
             "Unexpected type.");
      const Type *FieldRecTy = FieldTy->getPointeeOrArrayElementType();

      RD = FieldRecTy->castAs<RecordType>()->getDecl();
    } else {
      RD = cast<RecordDecl>(Next);
    }

    // Add a null marker so we know when we've gone back up a level
    VisitStack.push_back(nullptr);

    for (const auto *FD : RD->fields()) {
      QualType QT = FD->getType();

      if (ValidTypes.count(QT.getTypePtr()))
        continue;

      OpenCLParamType ParamType = getOpenCLKernelParameterType(S, QT);
      if (ParamType == ValidKernelParam)
        continue;

      if (ParamType == RecordKernelParam) {
        VisitStack.push_back(FD);
        continue;
      }

      // OpenCL v1.2 s6.9.p:
      // Arguments to kernel functions that are declared to be a struct or union
      // do not allow OpenCL objects to be passed as elements of the struct or
      // union. This restriction was lifted in OpenCL v2.0 with the introduction
      // of SVM.
      if (ParamType == PtrKernelParam || ParamType == PtrPtrKernelParam ||
          ParamType == InvalidAddrSpacePtrKernelParam) {
        S.Diag(Param->getLocation(),
               diag::err_record_with_pointers_kernel_param)
          << PT->isUnionType()
          << PT;
      } else {
        S.Diag(Param->getLocation(), diag::err_bad_kernel_param_type) << PT;
      }

      S.Diag(OrigRecDecl->getLocation(), diag::note_within_field_of_type)
          << OrigRecDecl->getDeclName();

      // We have an error, now let's go back up through history and show where
      // the offending field came from
      for (ArrayRef<const FieldDecl *>::const_iterator
               I = HistoryStack.begin() + 1,
               E = HistoryStack.end();
           I != E; ++I) {
        const FieldDecl *OuterField = *I;
        S.Diag(OuterField->getLocation(), diag::note_within_field_of_type)
          << OuterField->getType();
      }

      S.Diag(FD->getLocation(), diag::note_illegal_field_declared_here)
        << QT->isPointerType()
        << QT;
      D.setInvalidType();
      return;
    }
  } while (!VisitStack.empty());
}

/// Find the DeclContext in which a tag is implicitly declared if we see an
/// elaborated type specifier in the specified context, and lookup finds
/// nothing.
static DeclContext *getTagInjectionContext(DeclContext *DC) {
  while (!DC->isFileContext() && !DC->isFunctionOrMethod())
    DC = DC->getParent();
  return DC;
}

/// Find the Scope in which a tag is implicitly declared if we see an
/// elaborated type specifier in the specified context, and lookup finds
/// nothing.
static Scope *getTagInjectionScope(Scope *S, const LangOptions &LangOpts) {
  while (S->isClassScope() ||
         (LangOpts.CPlusPlus &&
          S->isFunctionPrototypeScope()) ||
         ((S->getFlags() & Scope::DeclScope) == 0) ||
         (S->getEntity() && S->getEntity()->isTransparentContext()))
    S = S->getParent();
  return S;
}

/// Determine whether a declaration matches a known function in namespace std.
static bool isStdBuiltin(ASTContext &Ctx, FunctionDecl *FD,
                         unsigned BuiltinID) {
  switch (BuiltinID) {
  case Builtin::BI__GetExceptionInfo:
    // No type checking whatsoever.
    return Ctx.getTargetInfo().getCXXABI().isMicrosoft();

  case Builtin::BIaddressof:
  case Builtin::BI__addressof:
  case Builtin::BIforward:
  case Builtin::BIforward_like:
  case Builtin::BImove:
  case Builtin::BImove_if_noexcept:
  case Builtin::BIas_const: {
    // Ensure that we don't treat the algorithm
    //   OutputIt std::move(InputIt, InputIt, OutputIt)
    // as the builtin std::move.
    const auto *FPT = FD->getType()->castAs<FunctionProtoType>();
    return FPT->getNumParams() == 1 && !FPT->isVariadic();
  }

  default:
    return false;
  }
}

NamedDecl*
Sema::ActOnFunctionDeclarator(Scope *S, Declarator &D, DeclContext *DC,
                              TypeSourceInfo *TInfo, LookupResult &Previous,
                              MultiTemplateParamsArg TemplateParamListsRef,
                              bool &AddToScope) {
  QualType R = TInfo->getType();

  assert(R->isFunctionType());
  if (R.getCanonicalType()->castAs<FunctionType>()->getCmseNSCallAttr())
    Diag(D.getIdentifierLoc(), diag::err_function_decl_cmse_ns_call);

  SmallVector<TemplateParameterList *, 4> TemplateParamLists;
  llvm::append_range(TemplateParamLists, TemplateParamListsRef);
  if (TemplateParameterList *Invented = D.getInventedTemplateParameterList()) {
    if (!TemplateParamLists.empty() && !TemplateParamLists.back()->empty() &&
        Invented->getDepth() == TemplateParamLists.back()->getDepth())
      TemplateParamLists.back() = Invented;
    else
      TemplateParamLists.push_back(Invented);
  }

  // TODO: consider using NameInfo for diagnostic.
  DeclarationNameInfo NameInfo = GetNameForDeclarator(D);
  DeclarationName Name = NameInfo.getName();
  StorageClass SC = getFunctionStorageClass(*this, D);

  if (DeclSpec::TSCS TSCS = D.getDeclSpec().getThreadStorageClassSpec())
    Diag(D.getDeclSpec().getThreadStorageClassSpecLoc(),
         diag::err_invalid_thread)
      << DeclSpec::getSpecifierName(TSCS);

  if (D.isFirstDeclarationOfMember())
    adjustMemberFunctionCC(
        R, !(D.isStaticMember() || D.isExplicitObjectMemberFunction()),
        D.isCtorOrDtor(), D.getIdentifierLoc());

  bool isFriend = false;
  FunctionTemplateDecl *FunctionTemplate = nullptr;
  bool isMemberSpecialization = false;
  bool isFunctionTemplateSpecialization = false;

  bool HasExplicitTemplateArgs = false;
  TemplateArgumentListInfo TemplateArgs;

  bool isVirtualOkay = false;

  DeclContext *OriginalDC = DC;
  bool IsLocalExternDecl = adjustContextForLocalExternDecl(DC);

  FunctionDecl *NewFD = CreateNewFunctionDecl(*this, D, DC, R, TInfo, SC,
                                              isVirtualOkay);
  if (!NewFD) return nullptr;

  if (OriginalLexicalContext && OriginalLexicalContext->isObjCContainer())
    NewFD->setTopLevelDeclInObjCContainer();

  // Set the lexical context. If this is a function-scope declaration, or has a
  // C++ scope specifier, or is the object of a friend declaration, the lexical
  // context will be different from the semantic context.
  NewFD->setLexicalDeclContext(CurContext);

  if (IsLocalExternDecl)
    NewFD->setLocalExternDecl();

  if (getLangOpts().CPlusPlus) {
    // The rules for implicit inlines changed in C++20 for methods and friends
    // with an in-class definition (when such a definition is not attached to
    // the global module).  User-specified 'inline' overrides this (set when
    // the function decl is created above).
    // FIXME: We need a better way to separate C++ standard and clang modules.
    bool ImplicitInlineCXX20 = !getLangOpts().CPlusPlusModules ||
                               NewFD->isConstexpr() || NewFD->isConsteval() ||
                               !NewFD->getOwningModule() ||
                               NewFD->isFromGlobalModule() ||
                               NewFD->getOwningModule()->isHeaderLikeModule();
    bool isInline = D.getDeclSpec().isInlineSpecified();
    bool isVirtual = D.getDeclSpec().isVirtualSpecified();
    bool hasExplicit = D.getDeclSpec().hasExplicitSpecifier();
    isFriend = D.getDeclSpec().isFriendSpecified();
    if (isFriend && !isInline && D.isFunctionDefinition()) {
      // Pre-C++20 [class.friend]p5
      //   A function can be defined in a friend declaration of a
      //   class . . . . Such a function is implicitly inline.
      // Post C++20 [class.friend]p7
      //   Such a function is implicitly an inline function if it is attached
      //   to the global module.
      NewFD->setImplicitlyInline(ImplicitInlineCXX20);
    }

    // If this is a method defined in an __interface, and is not a constructor
    // or an overloaded operator, then set the pure flag (isVirtual will already
    // return true).
    if (const CXXRecordDecl *Parent =
          dyn_cast<CXXRecordDecl>(NewFD->getDeclContext())) {
      if (Parent->isInterface() && cast<CXXMethodDecl>(NewFD)->isUserProvided())
        NewFD->setIsPureVirtual(true);

      // C++ [class.union]p2
      //   A union can have member functions, but not virtual functions.
      if (isVirtual && Parent->isUnion()) {
        Diag(D.getDeclSpec().getVirtualSpecLoc(), diag::err_virtual_in_union);
        NewFD->setInvalidDecl();
      }
      if ((Parent->isClass() || Parent->isStruct()) &&
          Parent->hasAttr<SYCLSpecialClassAttr>() &&
          NewFD->getKind() == Decl::Kind::CXXMethod && NewFD->getIdentifier() &&
          NewFD->getName() == "__init" && D.isFunctionDefinition()) {
        if (auto *Def = Parent->getDefinition())
          Def->setInitMethod(true);
      }
    }

    SetNestedNameSpecifier(*this, NewFD, D);
    isMemberSpecialization = false;
    isFunctionTemplateSpecialization = false;
    if (D.isInvalidType())
      NewFD->setInvalidDecl();

    // Match up the template parameter lists with the scope specifier, then
    // determine whether we have a template or a template specialization.
    bool Invalid = false;
    TemplateIdAnnotation *TemplateId =
        D.getName().getKind() == UnqualifiedIdKind::IK_TemplateId
            ? D.getName().TemplateId
            : nullptr;
    TemplateParameterList *TemplateParams =
        MatchTemplateParametersToScopeSpecifier(
            D.getDeclSpec().getBeginLoc(), D.getIdentifierLoc(),
            D.getCXXScopeSpec(), TemplateId, TemplateParamLists, isFriend,
            isMemberSpecialization, Invalid);
    if (TemplateParams) {
      // Check that we can declare a template here.
      if (CheckTemplateDeclScope(S, TemplateParams))
        NewFD->setInvalidDecl();

      if (TemplateParams->size() > 0) {
        // This is a function template

        // A destructor cannot be a template.
        if (Name.getNameKind() == DeclarationName::CXXDestructorName) {
          Diag(NewFD->getLocation(), diag::err_destructor_template);
          NewFD->setInvalidDecl();
          // Function template with explicit template arguments.
        } else if (TemplateId) {
          Diag(D.getIdentifierLoc(), diag::err_function_template_partial_spec)
              << SourceRange(TemplateId->LAngleLoc, TemplateId->RAngleLoc);
          NewFD->setInvalidDecl();
        }

        // If we're adding a template to a dependent context, we may need to
        // rebuilding some of the types used within the template parameter list,
        // now that we know what the current instantiation is.
        if (DC->isDependentContext()) {
          ContextRAII SavedContext(*this, DC);
          if (RebuildTemplateParamsInCurrentInstantiation(TemplateParams))
            Invalid = true;
        }

        FunctionTemplate = FunctionTemplateDecl::Create(Context, DC,
                                                        NewFD->getLocation(),
                                                        Name, TemplateParams,
                                                        NewFD);
        FunctionTemplate->setLexicalDeclContext(CurContext);
        NewFD->setDescribedFunctionTemplate(FunctionTemplate);

        // For source fidelity, store the other template param lists.
        if (TemplateParamLists.size() > 1) {
          NewFD->setTemplateParameterListsInfo(Context,
              ArrayRef<TemplateParameterList *>(TemplateParamLists)
                  .drop_back(1));
        }
      } else {
        // This is a function template specialization.
        isFunctionTemplateSpecialization = true;
        // For source fidelity, store all the template param lists.
        if (TemplateParamLists.size() > 0)
          NewFD->setTemplateParameterListsInfo(Context, TemplateParamLists);

        // C++0x [temp.expl.spec]p20 forbids "template<> friend void foo(int);".
        if (isFriend) {
          // We want to remove the "template<>", found here.
          SourceRange RemoveRange = TemplateParams->getSourceRange();

          // If we remove the template<> and the name is not a
          // template-id, we're actually silently creating a problem:
          // the friend declaration will refer to an untemplated decl,
          // and clearly the user wants a template specialization.  So
          // we need to insert '<>' after the name.
          SourceLocation InsertLoc;
          if (D.getName().getKind() != UnqualifiedIdKind::IK_TemplateId) {
            InsertLoc = D.getName().getSourceRange().getEnd();
            InsertLoc = getLocForEndOfToken(InsertLoc);
          }

          Diag(D.getIdentifierLoc(), diag::err_template_spec_decl_friend)
            << Name << RemoveRange
            << FixItHint::CreateRemoval(RemoveRange)
            << FixItHint::CreateInsertion(InsertLoc, "<>");
          Invalid = true;

          // Recover by faking up an empty template argument list.
          HasExplicitTemplateArgs = true;
          TemplateArgs.setLAngleLoc(InsertLoc);
          TemplateArgs.setRAngleLoc(InsertLoc);
        }
      }
    } else {
      // Check that we can declare a template here.
      if (!TemplateParamLists.empty() && isMemberSpecialization &&
          CheckTemplateDeclScope(S, TemplateParamLists.back()))
        NewFD->setInvalidDecl();

      // All template param lists were matched against the scope specifier:
      // this is NOT (an explicit specialization of) a template.
      if (TemplateParamLists.size() > 0)
        // For source fidelity, store all the template param lists.
        NewFD->setTemplateParameterListsInfo(Context, TemplateParamLists);

      // "friend void foo<>(int);" is an implicit specialization decl.
      if (isFriend && TemplateId)
        isFunctionTemplateSpecialization = true;
    }

    // If this is a function template specialization and the unqualified-id of
    // the declarator-id is a template-id, convert the template argument list
    // into our AST format and check for unexpanded packs.
    if (isFunctionTemplateSpecialization && TemplateId) {
      HasExplicitTemplateArgs = true;

      TemplateArgs.setLAngleLoc(TemplateId->LAngleLoc);
      TemplateArgs.setRAngleLoc(TemplateId->RAngleLoc);
      ASTTemplateArgsPtr TemplateArgsPtr(TemplateId->getTemplateArgs(),
                                         TemplateId->NumArgs);
      translateTemplateArguments(TemplateArgsPtr, TemplateArgs);

      // FIXME: Should we check for unexpanded packs if this was an (invalid)
      // declaration of a function template partial specialization? Should we
      // consider the unexpanded pack context to be a partial specialization?
      for (const TemplateArgumentLoc &ArgLoc : TemplateArgs.arguments()) {
        if (DiagnoseUnexpandedParameterPack(
                ArgLoc, isFriend ? UPPC_FriendDeclaration
                                 : UPPC_ExplicitSpecialization))
          NewFD->setInvalidDecl();
      }
    }

    if (Invalid) {
      NewFD->setInvalidDecl();
      if (FunctionTemplate)
        FunctionTemplate->setInvalidDecl();
    }

    // C++ [dcl.fct.spec]p5:
    //   The virtual specifier shall only be used in declarations of
    //   nonstatic class member functions that appear within a
    //   member-specification of a class declaration; see 10.3.
    //
    if (isVirtual && !NewFD->isInvalidDecl()) {
      if (!isVirtualOkay) {
        Diag(D.getDeclSpec().getVirtualSpecLoc(),
             diag::err_virtual_non_function);
      } else if (!CurContext->isRecord()) {
        // 'virtual' was specified outside of the class.
        Diag(D.getDeclSpec().getVirtualSpecLoc(),
             diag::err_virtual_out_of_class)
          << FixItHint::CreateRemoval(D.getDeclSpec().getVirtualSpecLoc());
      } else if (NewFD->getDescribedFunctionTemplate()) {
        // C++ [temp.mem]p3:
        //  A member function template shall not be virtual.
        Diag(D.getDeclSpec().getVirtualSpecLoc(),
             diag::err_virtual_member_function_template)
          << FixItHint::CreateRemoval(D.getDeclSpec().getVirtualSpecLoc());
      } else {
        // Okay: Add virtual to the method.
        NewFD->setVirtualAsWritten(true);
      }

      if (getLangOpts().CPlusPlus14 &&
          NewFD->getReturnType()->isUndeducedType())
        Diag(D.getDeclSpec().getVirtualSpecLoc(), diag::err_auto_fn_virtual);
    }

    // C++ [dcl.fct.spec]p3:
    //  The inline specifier shall not appear on a block scope function
    //  declaration.
    if (isInline && !NewFD->isInvalidDecl()) {
      if (CurContext->isFunctionOrMethod()) {
        // 'inline' is not allowed on block scope function declaration.
        Diag(D.getDeclSpec().getInlineSpecLoc(),
             diag::err_inline_declaration_block_scope) << Name
          << FixItHint::CreateRemoval(D.getDeclSpec().getInlineSpecLoc());
      }
    }

    // C++ [dcl.fct.spec]p6:
    //  The explicit specifier shall be used only in the declaration of a
    //  constructor or conversion function within its class definition;
    //  see 12.3.1 and 12.3.2.
    if (hasExplicit && !NewFD->isInvalidDecl() &&
        !isa<CXXDeductionGuideDecl>(NewFD)) {
      if (!CurContext->isRecord()) {
        // 'explicit' was specified outside of the class.
        Diag(D.getDeclSpec().getExplicitSpecLoc(),
             diag::err_explicit_out_of_class)
            << FixItHint::CreateRemoval(D.getDeclSpec().getExplicitSpecRange());
      } else if (!isa<CXXConstructorDecl>(NewFD) &&
                 !isa<CXXConversionDecl>(NewFD)) {
        // 'explicit' was specified on a function that wasn't a constructor
        // or conversion function.
        Diag(D.getDeclSpec().getExplicitSpecLoc(),
             diag::err_explicit_non_ctor_or_conv_function)
            << FixItHint::CreateRemoval(D.getDeclSpec().getExplicitSpecRange());
      }
    }

    ConstexprSpecKind ConstexprKind = D.getDeclSpec().getConstexprSpecifier();
    if (ConstexprKind != ConstexprSpecKind::Unspecified) {
      // C++11 [dcl.constexpr]p2: constexpr functions and constexpr constructors
      // are implicitly inline.
      NewFD->setImplicitlyInline();

      // C++11 [dcl.constexpr]p3: functions declared constexpr are required to
      // be either constructors or to return a literal type. Therefore,
      // destructors cannot be declared constexpr.
      if (isa<CXXDestructorDecl>(NewFD) &&
          (!getLangOpts().CPlusPlus20 ||
           ConstexprKind == ConstexprSpecKind::Consteval)) {
        Diag(D.getDeclSpec().getConstexprSpecLoc(), diag::err_constexpr_dtor)
            << static_cast<int>(ConstexprKind);
        NewFD->setConstexprKind(getLangOpts().CPlusPlus20
                                    ? ConstexprSpecKind::Unspecified
                                    : ConstexprSpecKind::Constexpr);
      }
      // C++20 [dcl.constexpr]p2: An allocation function, or a
      // deallocation function shall not be declared with the consteval
      // specifier.
      if (ConstexprKind == ConstexprSpecKind::Consteval &&
          (NewFD->getOverloadedOperator() == OO_New ||
           NewFD->getOverloadedOperator() == OO_Array_New ||
           NewFD->getOverloadedOperator() == OO_Delete ||
           NewFD->getOverloadedOperator() == OO_Array_Delete)) {
        Diag(D.getDeclSpec().getConstexprSpecLoc(),
             diag::err_invalid_consteval_decl_kind)
            << NewFD;
        NewFD->setConstexprKind(ConstexprSpecKind::Constexpr);
      }
    }

    // If __module_private__ was specified, mark the function accordingly.
    if (D.getDeclSpec().isModulePrivateSpecified()) {
      if (isFunctionTemplateSpecialization) {
        SourceLocation ModulePrivateLoc
          = D.getDeclSpec().getModulePrivateSpecLoc();
        Diag(ModulePrivateLoc, diag::err_module_private_specialization)
          << 0
          << FixItHint::CreateRemoval(ModulePrivateLoc);
      } else {
        NewFD->setModulePrivate();
        if (FunctionTemplate)
          FunctionTemplate->setModulePrivate();
      }
    }

    if (isFriend) {
      if (FunctionTemplate) {
        FunctionTemplate->setObjectOfFriendDecl();
        FunctionTemplate->setAccess(AS_public);
      }
      NewFD->setObjectOfFriendDecl();
      NewFD->setAccess(AS_public);
    }

    // If a function is defined as defaulted or deleted, mark it as such now.
    // We'll do the relevant checks on defaulted / deleted functions later.
    switch (D.getFunctionDefinitionKind()) {
    case FunctionDefinitionKind::Declaration:
    case FunctionDefinitionKind::Definition:
      break;

    case FunctionDefinitionKind::Defaulted:
      NewFD->setDefaulted();
      break;

    case FunctionDefinitionKind::Deleted:
      NewFD->setDeletedAsWritten();
      break;
    }

    if (isa<CXXMethodDecl>(NewFD) && DC == CurContext &&
        D.isFunctionDefinition() && !isInline) {
      // Pre C++20 [class.mfct]p2:
      //   A member function may be defined (8.4) in its class definition, in
      //   which case it is an inline member function (7.1.2)
      // Post C++20 [class.mfct]p1:
      //   If a member function is attached to the global module and is defined
      //   in its class definition, it is inline.
      NewFD->setImplicitlyInline(ImplicitInlineCXX20);
    }

    if (!isFriend && SC != SC_None) {
      // C++ [temp.expl.spec]p2:
      //   The declaration in an explicit-specialization shall not be an
      //   export-declaration. An explicit specialization shall not use a
      //   storage-class-specifier other than thread_local.
      //
      // We diagnose friend declarations with storage-class-specifiers
      // elsewhere.
      if (isFunctionTemplateSpecialization || isMemberSpecialization) {
        Diag(D.getDeclSpec().getStorageClassSpecLoc(),
             diag::ext_explicit_specialization_storage_class)
            << FixItHint::CreateRemoval(
                   D.getDeclSpec().getStorageClassSpecLoc());
      }

      if (SC == SC_Static && !CurContext->isRecord() && DC->isRecord()) {
        assert(isa<CXXMethodDecl>(NewFD) &&
               "Out-of-line member function should be a CXXMethodDecl");
        // C++ [class.static]p1:
        //   A data or function member of a class may be declared static
        //   in a class definition, in which case it is a static member of
        //   the class.

        // Complain about the 'static' specifier if it's on an out-of-line
        // member function definition.

        // MSVC permits the use of a 'static' storage specifier on an
        // out-of-line member function template declaration and class member
        // template declaration (MSVC versions before 2015), warn about this.
        Diag(D.getDeclSpec().getStorageClassSpecLoc(),
             ((!getLangOpts().isCompatibleWithMSVC(LangOptions::MSVC2015) &&
               cast<CXXRecordDecl>(DC)->getDescribedClassTemplate()) ||
              (getLangOpts().MSVCCompat &&
               NewFD->getDescribedFunctionTemplate()))
                 ? diag::ext_static_out_of_line
                 : diag::err_static_out_of_line)
            << FixItHint::CreateRemoval(
                   D.getDeclSpec().getStorageClassSpecLoc());
      }
    }

    // C++11 [except.spec]p15:
    //   A deallocation function with no exception-specification is treated
    //   as if it were specified with noexcept(true).
    const FunctionProtoType *FPT = R->getAs<FunctionProtoType>();
    if ((Name.getCXXOverloadedOperator() == OO_Delete ||
         Name.getCXXOverloadedOperator() == OO_Array_Delete) &&
        getLangOpts().CPlusPlus11 && FPT && !FPT->hasExceptionSpec())
      NewFD->setType(Context.getFunctionType(
          FPT->getReturnType(), FPT->getParamTypes(),
          FPT->getExtProtoInfo().withExceptionSpec(EST_BasicNoexcept)));

    // C++20 [dcl.inline]/7
    // If an inline function or variable that is attached to a named module
    // is declared in a definition domain, it shall be defined in that
    // domain.
    // So, if the current declaration does not have a definition, we must
    // check at the end of the TU (or when the PMF starts) to see that we
    // have a definition at that point.
    if (isInline && !D.isFunctionDefinition() && getLangOpts().CPlusPlus20 &&
        NewFD->isInNamedModule()) {
      PendingInlineFuncDecls.insert(NewFD);
    }
  }

  // Filter out previous declarations that don't match the scope.
  FilterLookupForScope(Previous, OriginalDC, S, shouldConsiderLinkage(NewFD),
                       D.getCXXScopeSpec().isNotEmpty() ||
                       isMemberSpecialization ||
                       isFunctionTemplateSpecialization);

  // Handle GNU asm-label extension (encoded as an attribute).
  if (Expr *E = (Expr*) D.getAsmLabel()) {
    // The parser guarantees this is a string.
    StringLiteral *SE = cast<StringLiteral>(E);
    NewFD->addAttr(AsmLabelAttr::Create(Context, SE->getString(),
                                        /*IsLiteralLabel=*/true,
                                        SE->getStrTokenLoc(0)));
  } else if (!ExtnameUndeclaredIdentifiers.empty()) {
    llvm::DenseMap<IdentifierInfo*,AsmLabelAttr*>::iterator I =
      ExtnameUndeclaredIdentifiers.find(NewFD->getIdentifier());
    if (I != ExtnameUndeclaredIdentifiers.end()) {
      if (isDeclExternC(NewFD)) {
        NewFD->addAttr(I->second);
        ExtnameUndeclaredIdentifiers.erase(I);
      } else
        Diag(NewFD->getLocation(), diag::warn_redefine_extname_not_applied)
            << /*Variable*/0 << NewFD;
    }
  }

  // Copy the parameter declarations from the declarator D to the function
  // declaration NewFD, if they are available.  First scavenge them into Params.
  SmallVector<ParmVarDecl*, 16> Params;
  unsigned FTIIdx;
  if (D.isFunctionDeclarator(FTIIdx)) {
    DeclaratorChunk::FunctionTypeInfo &FTI = D.getTypeObject(FTIIdx).Fun;

    // Check for C99 6.7.5.3p10 - foo(void) is a non-varargs
    // function that takes no arguments, not a function that takes a
    // single void argument.
    // We let through "const void" here because Sema::GetTypeForDeclarator
    // already checks for that case.
    if (FTIHasNonVoidParameters(FTI) && FTI.Params[0].Param) {
      for (unsigned i = 0, e = FTI.NumParams; i != e; ++i) {
        ParmVarDecl *Param = cast<ParmVarDecl>(FTI.Params[i].Param);
        assert(Param->getDeclContext() != NewFD && "Was set before ?");
        Param->setDeclContext(NewFD);
        Params.push_back(Param);

        if (Param->isInvalidDecl())
          NewFD->setInvalidDecl();
      }
    }

    if (!getLangOpts().CPlusPlus) {
      // In C, find all the tag declarations from the prototype and move them
      // into the function DeclContext. Remove them from the surrounding tag
      // injection context of the function, which is typically but not always
      // the TU.
      DeclContext *PrototypeTagContext =
          getTagInjectionContext(NewFD->getLexicalDeclContext());
      for (NamedDecl *NonParmDecl : FTI.getDeclsInPrototype()) {
        auto *TD = dyn_cast<TagDecl>(NonParmDecl);

        // We don't want to reparent enumerators. Look at their parent enum
        // instead.
        if (!TD) {
          if (auto *ECD = dyn_cast<EnumConstantDecl>(NonParmDecl))
            TD = cast<EnumDecl>(ECD->getDeclContext());
        }
        if (!TD)
          continue;
        DeclContext *TagDC = TD->getLexicalDeclContext();
        if (!TagDC->containsDecl(TD))
          continue;
        TagDC->removeDecl(TD);
        TD->setDeclContext(NewFD);
        NewFD->addDecl(TD);

        // Preserve the lexical DeclContext if it is not the surrounding tag
        // injection context of the FD. In this example, the semantic context of
        // E will be f and the lexical context will be S, while both the
        // semantic and lexical contexts of S will be f:
        //   void f(struct S { enum E { a } f; } s);
        if (TagDC != PrototypeTagContext)
          TD->setLexicalDeclContext(TagDC);
      }
    }
  } else if (const FunctionProtoType *FT = R->getAs<FunctionProtoType>()) {
    // When we're declaring a function with a typedef, typeof, etc as in the
    // following example, we'll need to synthesize (unnamed)
    // parameters for use in the declaration.
    //
    // @code
    // typedef void fn(int);
    // fn f;
    // @endcode

    // Synthesize a parameter for each argument type.
    for (const auto &AI : FT->param_types()) {
      ParmVarDecl *Param =
          BuildParmVarDeclForTypedef(NewFD, D.getIdentifierLoc(), AI);
      Param->setScopeInfo(0, Params.size());
      Params.push_back(Param);
    }
  } else {
    assert(R->isFunctionNoProtoType() && NewFD->getNumParams() == 0 &&
           "Should not need args for typedef of non-prototype fn");
  }

  // Finally, we know we have the right number of parameters, install them.
  NewFD->setParams(Params);

  if (D.getDeclSpec().isNoreturnSpecified())
    NewFD->addAttr(
        C11NoReturnAttr::Create(Context, D.getDeclSpec().getNoreturnSpecLoc()));

  // Functions returning a variably modified type violate C99 6.7.5.2p2
  // because all functions have linkage.
  if (!NewFD->isInvalidDecl() &&
      NewFD->getReturnType()->isVariablyModifiedType()) {
    Diag(NewFD->getLocation(), diag::err_vm_func_decl);
    NewFD->setInvalidDecl();
  }

  // Apply an implicit SectionAttr if '#pragma clang section text' is active
  if (PragmaClangTextSection.Valid && D.isFunctionDefinition() &&
      !NewFD->hasAttr<SectionAttr>())
    NewFD->addAttr(PragmaClangTextSectionAttr::CreateImplicit(
        Context, PragmaClangTextSection.SectionName,
        PragmaClangTextSection.PragmaLocation));

  // Apply an implicit SectionAttr if #pragma code_seg is active.
  if (CodeSegStack.CurrentValue && D.isFunctionDefinition() &&
      !NewFD->hasAttr<SectionAttr>()) {
    NewFD->addAttr(SectionAttr::CreateImplicit(
        Context, CodeSegStack.CurrentValue->getString(),
        CodeSegStack.CurrentPragmaLocation, SectionAttr::Declspec_allocate));
    if (UnifySection(CodeSegStack.CurrentValue->getString(),
                     ASTContext::PSF_Implicit | ASTContext::PSF_Execute |
                         ASTContext::PSF_Read,
                     NewFD))
      NewFD->dropAttr<SectionAttr>();
  }

  // Apply an implicit StrictGuardStackCheckAttr if #pragma strict_gs_check is
  // active.
  if (StrictGuardStackCheckStack.CurrentValue && D.isFunctionDefinition() &&
      !NewFD->hasAttr<StrictGuardStackCheckAttr>())
    NewFD->addAttr(StrictGuardStackCheckAttr::CreateImplicit(
        Context, PragmaClangTextSection.PragmaLocation));

  // Apply an implicit CodeSegAttr from class declspec or
  // apply an implicit SectionAttr from #pragma code_seg if active.
  if (!NewFD->hasAttr<CodeSegAttr>()) {
    if (Attr *SAttr = getImplicitCodeSegOrSectionAttrForFunction(NewFD,
                                                                 D.isFunctionDefinition())) {
      NewFD->addAttr(SAttr);
    }
  }

  // Handle attributes.
  ProcessDeclAttributes(S, NewFD, D);
  const auto *NewTVA = NewFD->getAttr<TargetVersionAttr>();
  if (NewTVA && !NewTVA->isDefaultVersion() &&
      !Context.getTargetInfo().hasFeature("fmv")) {
    // Don't add to scope fmv functions declarations if fmv disabled
    AddToScope = false;
    return NewFD;
  }

  if (getLangOpts().OpenCL || getLangOpts().HLSL) {
    // Neither OpenCL nor HLSL allow an address space qualifyer on a return
    // type.
    //
    // OpenCL v1.1 s6.5: Using an address space qualifier in a function return
    // type declaration will generate a compilation error.
    LangAS AddressSpace = NewFD->getReturnType().getAddressSpace();
    if (AddressSpace != LangAS::Default) {
      Diag(NewFD->getLocation(), diag::err_return_value_with_address_space);
      NewFD->setInvalidDecl();
    }
  }

  if (!getLangOpts().CPlusPlus) {
    // Perform semantic checking on the function declaration.
    if (!NewFD->isInvalidDecl() && NewFD->isMain())
      CheckMain(NewFD, D.getDeclSpec());

    if (!NewFD->isInvalidDecl() && NewFD->isMSVCRTEntryPoint())
      CheckMSVCRTEntryPoint(NewFD);

    if (!NewFD->isInvalidDecl())
      D.setRedeclaration(CheckFunctionDeclaration(S, NewFD, Previous,
                                                  isMemberSpecialization,
                                                  D.isFunctionDefinition()));
    else if (!Previous.empty())
      // Recover gracefully from an invalid redeclaration.
      D.setRedeclaration(true);
    assert((NewFD->isInvalidDecl() || !D.isRedeclaration() ||
            Previous.getResultKind() != LookupResult::FoundOverloaded) &&
           "previous declaration set still overloaded");

    // Diagnose no-prototype function declarations with calling conventions that
    // don't support variadic calls. Only do this in C and do it after merging
    // possibly prototyped redeclarations.
    const FunctionType *FT = NewFD->getType()->castAs<FunctionType>();
    if (isa<FunctionNoProtoType>(FT) && !D.isFunctionDefinition()) {
      CallingConv CC = FT->getExtInfo().getCC();
      if (!supportsVariadicCall(CC)) {
        // Windows system headers sometimes accidentally use stdcall without
        // (void) parameters, so we relax this to a warning.
        int DiagID =
            CC == CC_X86StdCall ? diag::warn_cconv_knr : diag::err_cconv_knr;
        Diag(NewFD->getLocation(), DiagID)
            << FunctionType::getNameForCallConv(CC);
      }
    }

   if (NewFD->getReturnType().hasNonTrivialToPrimitiveDestructCUnion() ||
       NewFD->getReturnType().hasNonTrivialToPrimitiveCopyCUnion())
     checkNonTrivialCUnion(NewFD->getReturnType(),
                           NewFD->getReturnTypeSourceRange().getBegin(),
                           NTCUC_FunctionReturn, NTCUK_Destruct|NTCUK_Copy);
  } else {
    // C++11 [replacement.functions]p3:
    //  The program's definitions shall not be specified as inline.
    //
    // N.B. We diagnose declarations instead of definitions per LWG issue 2340.
    //
    // Suppress the diagnostic if the function is __attribute__((used)), since
    // that forces an external definition to be emitted.
    if (D.getDeclSpec().isInlineSpecified() &&
        NewFD->isReplaceableGlobalAllocationFunction() &&
        !NewFD->hasAttr<UsedAttr>())
      Diag(D.getDeclSpec().getInlineSpecLoc(),
           diag::ext_operator_new_delete_declared_inline)
        << NewFD->getDeclName();

    if (Expr *TRC = NewFD->getTrailingRequiresClause()) {
      // C++20 [dcl.decl.general]p4:
      //   The optional requires-clause in an init-declarator or
      //   member-declarator shall be present only if the declarator declares a
      //   templated function.
      //
      // C++20 [temp.pre]p8:
      //   An entity is templated if it is
      //     - a template,
      //     - an entity defined or created in a templated entity,
      //     - a member of a templated entity,
      //     - an enumerator for an enumeration that is a templated entity, or
      //     - the closure type of a lambda-expression appearing in the
      //       declaration of a templated entity.
      //
      //   [Note 6: A local class, a local or block variable, or a friend
      //   function defined in a templated entity is a templated entity.
      //   — end note]
      //
      //   A templated function is a function template or a function that is
      //   templated. A templated class is a class template or a class that is
      //   templated. A templated variable is a variable template or a variable
      //   that is templated.
      if (!FunctionTemplate) {
        if (isFunctionTemplateSpecialization || isMemberSpecialization) {
          // C++ [temp.expl.spec]p8 (proposed resolution for CWG2847):
          //   An explicit specialization shall not have a trailing
          //   requires-clause unless it declares a function template.
          //
          // Since a friend function template specialization cannot be
          // definition, and since a non-template friend declaration with a
          // trailing requires-clause must be a definition, we diagnose
          // friend function template specializations with trailing
          // requires-clauses on the same path as explicit specializations
          // even though they aren't necessarily prohibited by the same
          // language rule.
          Diag(TRC->getBeginLoc(), diag::err_non_temp_spec_requires_clause)
              << isFriend;
        } else if (isFriend && NewFD->isTemplated() &&
                   !D.isFunctionDefinition()) {
          // C++ [temp.friend]p9:
          //   A non-template friend declaration with a requires-clause shall be
          //   a definition.
          Diag(NewFD->getBeginLoc(),
               diag::err_non_temp_friend_decl_with_requires_clause_must_be_def);
          NewFD->setInvalidDecl();
        } else if (!NewFD->isTemplated() ||
                   !(isa<CXXMethodDecl>(NewFD) || D.isFunctionDefinition())) {
          Diag(TRC->getBeginLoc(),
               diag::err_constrained_non_templated_function);
        }
      }
    }

    // We do not add HD attributes to specializations here because
    // they may have different constexpr-ness compared to their
    // templates and, after maybeAddHostDeviceAttrs() is applied,
    // may end up with different effective targets. Instead, a
    // specialization inherits its target attributes from its template
    // in the CheckFunctionTemplateSpecialization() call below.
    if (getLangOpts().CUDA && !isFunctionTemplateSpecialization)
      CUDA().maybeAddHostDeviceAttrs(NewFD, Previous);

    // Handle explicit specializations of function templates
    // and friend function declarations with an explicit
    // template argument list.
    if (isFunctionTemplateSpecialization) {
      bool isDependentSpecialization = false;
      if (isFriend) {
        // For friend function specializations, this is a dependent
        // specialization if its semantic context is dependent, its
        // type is dependent, or if its template-id is dependent.
        isDependentSpecialization =
            DC->isDependentContext() || NewFD->getType()->isDependentType() ||
            (HasExplicitTemplateArgs &&
             TemplateSpecializationType::
                 anyInstantiationDependentTemplateArguments(
                     TemplateArgs.arguments()));
        assert((!isDependentSpecialization ||
                (HasExplicitTemplateArgs == isDependentSpecialization)) &&
               "dependent friend function specialization without template "
               "args");
      } else {
        // For class-scope explicit specializations of function templates,
        // if the lexical context is dependent, then the specialization
        // is dependent.
        isDependentSpecialization =
            CurContext->isRecord() && CurContext->isDependentContext();
      }

      TemplateArgumentListInfo *ExplicitTemplateArgs =
          HasExplicitTemplateArgs ? &TemplateArgs : nullptr;
      if (isDependentSpecialization) {
        // If it's a dependent specialization, it may not be possible
        // to determine the primary template (for explicit specializations)
        // or befriended declaration (for friends) until the enclosing
        // template is instantiated. In such cases, we store the declarations
        // found by name lookup and defer resolution until instantiation.
        if (CheckDependentFunctionTemplateSpecialization(
                NewFD, ExplicitTemplateArgs, Previous))
          NewFD->setInvalidDecl();
      } else if (!NewFD->isInvalidDecl()) {
        if (CheckFunctionTemplateSpecialization(NewFD, ExplicitTemplateArgs,
                                                Previous))
          NewFD->setInvalidDecl();
      }
    } else if (isMemberSpecialization && isa<CXXMethodDecl>(NewFD)) {
      if (CheckMemberSpecialization(NewFD, Previous))
          NewFD->setInvalidDecl();
    }

    // Perform semantic checking on the function declaration.
    if (!NewFD->isInvalidDecl() && NewFD->isMain())
      CheckMain(NewFD, D.getDeclSpec());

    if (!NewFD->isInvalidDecl() && NewFD->isMSVCRTEntryPoint())
      CheckMSVCRTEntryPoint(NewFD);

    if (!NewFD->isInvalidDecl())
      D.setRedeclaration(CheckFunctionDeclaration(S, NewFD, Previous,
                                                  isMemberSpecialization,
                                                  D.isFunctionDefinition()));
    else if (!Previous.empty())
      // Recover gracefully from an invalid redeclaration.
      D.setRedeclaration(true);

    assert((NewFD->isInvalidDecl() || NewFD->isMultiVersion() ||
            !D.isRedeclaration() ||
            Previous.getResultKind() != LookupResult::FoundOverloaded) &&
           "previous declaration set still overloaded");

    NamedDecl *PrincipalDecl = (FunctionTemplate
                                ? cast<NamedDecl>(FunctionTemplate)
                                : NewFD);

    if (isFriend && NewFD->getPreviousDecl()) {
      AccessSpecifier Access = AS_public;
      if (!NewFD->isInvalidDecl())
        Access = NewFD->getPreviousDecl()->getAccess();

      NewFD->setAccess(Access);
      if (FunctionTemplate) FunctionTemplate->setAccess(Access);
    }

    if (NewFD->isOverloadedOperator() && !DC->isRecord() &&
        PrincipalDecl->isInIdentifierNamespace(Decl::IDNS_Ordinary))
      PrincipalDecl->setNonMemberOperator();

    // If we have a function template, check the template parameter
    // list. This will check and merge default template arguments.
    if (FunctionTemplate) {
      FunctionTemplateDecl *PrevTemplate =
                                     FunctionTemplate->getPreviousDecl();
      CheckTemplateParameterList(FunctionTemplate->getTemplateParameters(),
                       PrevTemplate ? PrevTemplate->getTemplateParameters()
                                    : nullptr,
                            D.getDeclSpec().isFriendSpecified()
                              ? (D.isFunctionDefinition()
                                   ? TPC_FriendFunctionTemplateDefinition
                                   : TPC_FriendFunctionTemplate)
                              : (D.getCXXScopeSpec().isSet() &&
                                 DC && DC->isRecord() &&
                                 DC->isDependentContext())
                                  ? TPC_ClassTemplateMember
                                  : TPC_FunctionTemplate);
    }

    if (NewFD->isInvalidDecl()) {
      // Ignore all the rest of this.
    } else if (!D.isRedeclaration()) {
      struct ActOnFDArgs ExtraArgs = { S, D, TemplateParamLists,
                                       AddToScope };
      // Fake up an access specifier if it's supposed to be a class member.
      if (isa<CXXRecordDecl>(NewFD->getDeclContext()))
        NewFD->setAccess(AS_public);

      // Qualified decls generally require a previous declaration.
      if (D.getCXXScopeSpec().isSet()) {
        // ...with the major exception of templated-scope or
        // dependent-scope friend declarations.

        // TODO: we currently also suppress this check in dependent
        // contexts because (1) the parameter depth will be off when
        // matching friend templates and (2) we might actually be
        // selecting a friend based on a dependent factor.  But there
        // are situations where these conditions don't apply and we
        // can actually do this check immediately.
        //
        // Unless the scope is dependent, it's always an error if qualified
        // redeclaration lookup found nothing at all. Diagnose that now;
        // nothing will diagnose that error later.
        if (isFriend &&
            (D.getCXXScopeSpec().getScopeRep()->isDependent() ||
             (!Previous.empty() && CurContext->isDependentContext()))) {
          // ignore these
        } else if (NewFD->isCPUDispatchMultiVersion() ||
                   NewFD->isCPUSpecificMultiVersion()) {
          // ignore this, we allow the redeclaration behavior here to create new
          // versions of the function.
        } else {
          // The user tried to provide an out-of-line definition for a
          // function that is a member of a class or namespace, but there
          // was no such member function declared (C++ [class.mfct]p2,
          // C++ [namespace.memdef]p2). For example:
          //
          // class X {
          //   void f() const;
          // };
          //
          // void X::f() { } // ill-formed
          //
          // Complain about this problem, and attempt to suggest close
          // matches (e.g., those that differ only in cv-qualifiers and
          // whether the parameter types are references).

          if (NamedDecl *Result = DiagnoseInvalidRedeclaration(
                  *this, Previous, NewFD, ExtraArgs, false, nullptr)) {
            AddToScope = ExtraArgs.AddToScope;
            return Result;
          }
        }

        // Unqualified local friend declarations are required to resolve
        // to something.
      } else if (isFriend && cast<CXXRecordDecl>(CurContext)->isLocalClass()) {
        if (NamedDecl *Result = DiagnoseInvalidRedeclaration(
                *this, Previous, NewFD, ExtraArgs, true, S)) {
          AddToScope = ExtraArgs.AddToScope;
          return Result;
        }
      }
    } else if (!D.isFunctionDefinition() &&
               isa<CXXMethodDecl>(NewFD) && NewFD->isOutOfLine() &&
               !isFriend && !isFunctionTemplateSpecialization &&
               !isMemberSpecialization) {
      // An out-of-line member function declaration must also be a
      // definition (C++ [class.mfct]p2).
      // Note that this is not the case for explicit specializations of
      // function templates or member functions of class templates, per
      // C++ [temp.expl.spec]p2. We also allow these declarations as an
      // extension for compatibility with old SWIG code which likes to
      // generate them.
      Diag(NewFD->getLocation(), diag::ext_out_of_line_declaration)
        << D.getCXXScopeSpec().getRange();
    }
  }

  if (getLangOpts().HLSL && D.isFunctionDefinition()) {
    // Any top level function could potentially be specified as an entry.
    if (!NewFD->isInvalidDecl() && S->getDepth() == 0 && Name.isIdentifier())
      HLSL().ActOnTopLevelFunction(NewFD);

    if (NewFD->hasAttr<HLSLShaderAttr>())
      HLSL().CheckEntryPoint(NewFD);
  }

  // If this is the first declaration of a library builtin function, add
  // attributes as appropriate.
  if (!D.isRedeclaration()) {
    if (IdentifierInfo *II = Previous.getLookupName().getAsIdentifierInfo()) {
      if (unsigned BuiltinID = II->getBuiltinID()) {
        bool InStdNamespace = Context.BuiltinInfo.isInStdNamespace(BuiltinID);
        if (!InStdNamespace &&
            NewFD->getDeclContext()->getRedeclContext()->isFileContext()) {
          if (NewFD->getLanguageLinkage() == CLanguageLinkage) {
            // Validate the type matches unless this builtin is specified as
            // matching regardless of its declared type.
            if (Context.BuiltinInfo.allowTypeMismatch(BuiltinID)) {
              NewFD->addAttr(BuiltinAttr::CreateImplicit(Context, BuiltinID));
            } else {
              ASTContext::GetBuiltinTypeError Error;
              LookupNecessaryTypesForBuiltin(S, BuiltinID);
              QualType BuiltinType = Context.GetBuiltinType(BuiltinID, Error);

              if (!Error && !BuiltinType.isNull() &&
                  Context.hasSameFunctionTypeIgnoringExceptionSpec(
                      NewFD->getType(), BuiltinType))
                NewFD->addAttr(BuiltinAttr::CreateImplicit(Context, BuiltinID));
            }
          }
        } else if (InStdNamespace && NewFD->isInStdNamespace() &&
                   isStdBuiltin(Context, NewFD, BuiltinID)) {
          NewFD->addAttr(BuiltinAttr::CreateImplicit(Context, BuiltinID));
        }
      }
    }
  }

  ProcessPragmaWeak(S, NewFD);
  checkAttributesAfterMerging(*this, *NewFD);

  AddKnownFunctionAttributes(NewFD);

  if (NewFD->hasAttr<OverloadableAttr>() &&
      !NewFD->getType()->getAs<FunctionProtoType>()) {
    Diag(NewFD->getLocation(),
         diag::err_attribute_overloadable_no_prototype)
      << NewFD;
    NewFD->dropAttr<OverloadableAttr>();
  }

  // If there's a #pragma GCC visibility in scope, and this isn't a class
  // member, set the visibility of this function.
  if (!DC->isRecord() && NewFD->isExternallyVisible())
    AddPushedVisibilityAttribute(NewFD);

  // If there's a #pragma clang arc_cf_code_audited in scope, consider
  // marking the function.
  ObjC().AddCFAuditedAttribute(NewFD);

  // If this is a function definition, check if we have to apply any
  // attributes (i.e. optnone and no_builtin) due to a pragma.
  if (D.isFunctionDefinition()) {
    AddRangeBasedOptnone(NewFD);
    AddImplicitMSFunctionNoBuiltinAttr(NewFD);
    AddSectionMSAllocText(NewFD);
    ModifyFnAttributesMSPragmaOptimize(NewFD);
  }

  // If this is the first declaration of an extern C variable, update
  // the map of such variables.
  if (NewFD->isFirstDecl() && !NewFD->isInvalidDecl() &&
      isIncompleteDeclExternC(*this, NewFD))
    RegisterLocallyScopedExternCDecl(NewFD, S);

  // Set this FunctionDecl's range up to the right paren.
  NewFD->setRangeEnd(D.getSourceRange().getEnd());

  if (D.isRedeclaration() && !Previous.empty()) {
    NamedDecl *Prev = Previous.getRepresentativeDecl();
    checkDLLAttributeRedeclaration(*this, Prev, NewFD,
                                   isMemberSpecialization ||
                                       isFunctionTemplateSpecialization,
                                   D.isFunctionDefinition());
  }

  if (getLangOpts().CUDA) {
    IdentifierInfo *II = NewFD->getIdentifier();
    if (II && II->isStr(CUDA().getConfigureFuncName()) &&
        !NewFD->isInvalidDecl() &&
        NewFD->getDeclContext()->getRedeclContext()->isTranslationUnit()) {
      if (!R->castAs<FunctionType>()->getReturnType()->isScalarType())
        Diag(NewFD->getLocation(), diag::err_config_scalar_return)
            << CUDA().getConfigureFuncName();
      Context.setcudaConfigureCallDecl(NewFD);
    }

    // Variadic functions, other than a *declaration* of printf, are not allowed
    // in device-side CUDA code, unless someone passed
    // -fcuda-allow-variadic-functions.
    if (!getLangOpts().CUDAAllowVariadicFunctions && NewFD->isVariadic() &&
        (NewFD->hasAttr<CUDADeviceAttr>() ||
         NewFD->hasAttr<CUDAGlobalAttr>()) &&
        !(II && II->isStr("printf") && NewFD->isExternC() &&
          !D.isFunctionDefinition())) {
      Diag(NewFD->getLocation(), diag::err_variadic_device_fn);
    }
  }

  MarkUnusedFileScopedDecl(NewFD);



  if (getLangOpts().OpenCL && NewFD->hasAttr<OpenCLKernelAttr>()) {
    // OpenCL v1.2 s6.8 static is invalid for kernel functions.
    if (SC == SC_Static) {
      Diag(D.getIdentifierLoc(), diag::err_static_kernel);
      D.setInvalidType();
    }

    // OpenCL v1.2, s6.9 -- Kernels can only have return type void.
    if (!NewFD->getReturnType()->isVoidType()) {
      SourceRange RTRange = NewFD->getReturnTypeSourceRange();
      Diag(D.getIdentifierLoc(), diag::err_expected_kernel_void_return_type)
          << (RTRange.isValid() ? FixItHint::CreateReplacement(RTRange, "void")
                                : FixItHint());
      D.setInvalidType();
    }

    llvm::SmallPtrSet<const Type *, 16> ValidTypes;
    for (auto *Param : NewFD->parameters())
      checkIsValidOpenCLKernelParameter(*this, D, Param, ValidTypes);

    if (getLangOpts().OpenCLCPlusPlus) {
      if (DC->isRecord()) {
        Diag(D.getIdentifierLoc(), diag::err_method_kernel);
        D.setInvalidType();
      }
      if (FunctionTemplate) {
        Diag(D.getIdentifierLoc(), diag::err_template_kernel);
        D.setInvalidType();
      }
    }
  }

  if (getLangOpts().CPlusPlus) {
    // Precalculate whether this is a friend function template with a constraint
    // that depends on an enclosing template, per [temp.friend]p9.
    if (isFriend && FunctionTemplate &&
        FriendConstraintsDependOnEnclosingTemplate(NewFD)) {
      NewFD->setFriendConstraintRefersToEnclosingTemplate(true);

      // C++ [temp.friend]p9:
      //    A friend function template with a constraint that depends on a
      //    template parameter from an enclosing template shall be a definition.
      if (!D.isFunctionDefinition()) {
        Diag(NewFD->getBeginLoc(),
             diag::err_friend_decl_with_enclosing_temp_constraint_must_be_def);
        NewFD->setInvalidDecl();
      }
    }

    if (FunctionTemplate) {
      if (NewFD->isInvalidDecl())
        FunctionTemplate->setInvalidDecl();
      return FunctionTemplate;
    }

    if (isMemberSpecialization && !NewFD->isInvalidDecl())
      CompleteMemberSpecialization(NewFD, Previous);
  }

  for (const ParmVarDecl *Param : NewFD->parameters()) {
    QualType PT = Param->getType();

    // OpenCL 2.0 pipe restrictions forbids pipe packet types to be non-value
    // types.
    if (getLangOpts().getOpenCLCompatibleVersion() >= 200) {
      if(const PipeType *PipeTy = PT->getAs<PipeType>()) {
        QualType ElemTy = PipeTy->getElementType();
          if (ElemTy->isReferenceType() || ElemTy->isPointerType()) {
            Diag(Param->getTypeSpecStartLoc(), diag::err_reference_pipe_type );
            D.setInvalidType();
          }
      }
    }
    // WebAssembly tables can't be used as function parameters.
    if (Context.getTargetInfo().getTriple().isWasm()) {
      if (PT->getUnqualifiedDesugaredType()->isWebAssemblyTableType()) {
        Diag(Param->getTypeSpecStartLoc(),
             diag::err_wasm_table_as_function_parameter);
        D.setInvalidType();
      }
    }
  }

  // Diagnose availability attributes. Availability cannot be used on functions
  // that are run during load/unload.
  if (const auto *attr = NewFD->getAttr<AvailabilityAttr>()) {
    if (NewFD->hasAttr<ConstructorAttr>()) {
      Diag(attr->getLocation(), diag::warn_availability_on_static_initializer)
          << 1;
      NewFD->dropAttr<AvailabilityAttr>();
    }
    if (NewFD->hasAttr<DestructorAttr>()) {
      Diag(attr->getLocation(), diag::warn_availability_on_static_initializer)
          << 2;
      NewFD->dropAttr<AvailabilityAttr>();
    }
  }

  // Diagnose no_builtin attribute on function declaration that are not a
  // definition.
  // FIXME: We should really be doing this in
  // SemaDeclAttr.cpp::handleNoBuiltinAttr, unfortunately we only have access to
  // the FunctionDecl and at this point of the code
  // FunctionDecl::isThisDeclarationADefinition() which always returns `false`
  // because Sema::ActOnStartOfFunctionDef has not been called yet.
  if (const auto *NBA = NewFD->getAttr<NoBuiltinAttr>())
    switch (D.getFunctionDefinitionKind()) {
    case FunctionDefinitionKind::Defaulted:
    case FunctionDefinitionKind::Deleted:
      Diag(NBA->getLocation(),
           diag::err_attribute_no_builtin_on_defaulted_deleted_function)
          << NBA->getSpelling();
      break;
    case FunctionDefinitionKind::Declaration:
      Diag(NBA->getLocation(), diag::err_attribute_no_builtin_on_non_definition)
          << NBA->getSpelling();
      break;
    case FunctionDefinitionKind::Definition:
      break;
    }

  // Similar to no_builtin logic above, at this point of the code
  // FunctionDecl::isThisDeclarationADefinition() always returns `false`
  // because Sema::ActOnStartOfFunctionDef has not been called yet.
  if (Context.getTargetInfo().allowDebugInfoForExternalRef() &&
      !NewFD->isInvalidDecl() &&
      D.getFunctionDefinitionKind() == FunctionDefinitionKind::Declaration)
    ExternalDeclarations.push_back(NewFD);

  return NewFD;
}

/// Return a CodeSegAttr from a containing class.  The Microsoft docs say
/// when __declspec(code_seg) "is applied to a class, all member functions of
/// the class and nested classes -- this includes compiler-generated special
/// member functions -- are put in the specified segment."
/// The actual behavior is a little more complicated. The Microsoft compiler
/// won't check outer classes if there is an active value from #pragma code_seg.
/// The CodeSeg is always applied from the direct parent but only from outer
/// classes when the #pragma code_seg stack is empty. See:
/// https://reviews.llvm.org/D22931, the Microsoft feedback page is no longer
/// available since MS has removed the page.
static Attr *getImplicitCodeSegAttrFromClass(Sema &S, const FunctionDecl *FD) {
  const auto *Method = dyn_cast<CXXMethodDecl>(FD);
  if (!Method)
    return nullptr;
  const CXXRecordDecl *Parent = Method->getParent();
  if (const auto *SAttr = Parent->getAttr<CodeSegAttr>()) {
    Attr *NewAttr = SAttr->clone(S.getASTContext());
    NewAttr->setImplicit(true);
    return NewAttr;
  }

  // The Microsoft compiler won't check outer classes for the CodeSeg
  // when the #pragma code_seg stack is active.
  if (S.CodeSegStack.CurrentValue)
   return nullptr;

  while ((Parent = dyn_cast<CXXRecordDecl>(Parent->getParent()))) {
    if (const auto *SAttr = Parent->getAttr<CodeSegAttr>()) {
      Attr *NewAttr = SAttr->clone(S.getASTContext());
      NewAttr->setImplicit(true);
      return NewAttr;
    }
  }
  return nullptr;
}

Attr *Sema::getImplicitCodeSegOrSectionAttrForFunction(const FunctionDecl *FD,
                                                       bool IsDefinition) {
  if (Attr *A = getImplicitCodeSegAttrFromClass(*this, FD))
    return A;
  if (!FD->hasAttr<SectionAttr>() && IsDefinition &&
      CodeSegStack.CurrentValue)
    return SectionAttr::CreateImplicit(
        getASTContext(), CodeSegStack.CurrentValue->getString(),
        CodeSegStack.CurrentPragmaLocation, SectionAttr::Declspec_allocate);
  return nullptr;
}

bool Sema::canFullyTypeCheckRedeclaration(ValueDecl *NewD, ValueDecl *OldD,
                                          QualType NewT, QualType OldT) {
  if (!NewD->getLexicalDeclContext()->isDependentContext())
    return true;

  // For dependently-typed local extern declarations and friends, we can't
  // perform a correct type check in general until instantiation:
  //
  //   int f();
  //   template<typename T> void g() { T f(); }
  //
  // (valid if g() is only instantiated with T = int).
  if (NewT->isDependentType() &&
      (NewD->isLocalExternDecl() || NewD->getFriendObjectKind()))
    return false;

  // Similarly, if the previous declaration was a dependent local extern
  // declaration, we don't really know its type yet.
  if (OldT->isDependentType() && OldD->isLocalExternDecl())
    return false;

  return true;
}

bool Sema::shouldLinkDependentDeclWithPrevious(Decl *D, Decl *PrevDecl) {
  if (!D->getLexicalDeclContext()->isDependentContext())
    return true;

  // Don't chain dependent friend function definitions until instantiation, to
  // permit cases like
  //
  //   void func();
  //   template<typename T> class C1 { friend void func() {} };
  //   template<typename T> class C2 { friend void func() {} };
  //
  // ... which is valid if only one of C1 and C2 is ever instantiated.
  //
  // FIXME: This need only apply to function definitions. For now, we proxy
  // this by checking for a file-scope function. We do not want this to apply
  // to friend declarations nominating member functions, because that gets in
  // the way of access checks.
  if (D->getFriendObjectKind() && D->getDeclContext()->isFileContext())
    return false;

  auto *VD = dyn_cast<ValueDecl>(D);
  auto *PrevVD = dyn_cast<ValueDecl>(PrevDecl);
  return !VD || !PrevVD ||
         canFullyTypeCheckRedeclaration(VD, PrevVD, VD->getType(),
                                        PrevVD->getType());
}

/// Check the target or target_version attribute of the function for
/// MultiVersion validity.
///
/// Returns true if there was an error, false otherwise.
static bool CheckMultiVersionValue(Sema &S, const FunctionDecl *FD) {
  const auto *TA = FD->getAttr<TargetAttr>();
  const auto *TVA = FD->getAttr<TargetVersionAttr>();
  assert(
      (TA || TVA) &&
      "MultiVersion candidate requires a target or target_version attribute");
  const TargetInfo &TargetInfo = S.Context.getTargetInfo();
  enum ErrType { Feature = 0, Architecture = 1 };

  if (TA) {
    ParsedTargetAttr ParseInfo =
        S.getASTContext().getTargetInfo().parseTargetAttr(TA->getFeaturesStr());
    if (!ParseInfo.CPU.empty() && !TargetInfo.validateCpuIs(ParseInfo.CPU)) {
      S.Diag(FD->getLocation(), diag::err_bad_multiversion_option)
          << Architecture << ParseInfo.CPU;
      return true;
    }
    for (const auto &Feat : ParseInfo.Features) {
      auto BareFeat = StringRef{Feat}.substr(1);
      if (Feat[0] == '-') {
        S.Diag(FD->getLocation(), diag::err_bad_multiversion_option)
            << Feature << ("no-" + BareFeat).str();
        return true;
      }

      if (!TargetInfo.validateCpuSupports(BareFeat) ||
          !TargetInfo.isValidFeatureName(BareFeat)) {
        S.Diag(FD->getLocation(), diag::err_bad_multiversion_option)
            << Feature << BareFeat;
        return true;
      }
    }
  }

  if (TVA) {
    llvm::SmallVector<StringRef, 8> Feats;
    TVA->getFeatures(Feats);
    for (const auto &Feat : Feats) {
      if (!TargetInfo.validateCpuSupports(Feat)) {
        S.Diag(FD->getLocation(), diag::err_bad_multiversion_option)
            << Feature << Feat;
        return true;
      }
    }
  }
  return false;
}

// Provide a white-list of attributes that are allowed to be combined with
// multiversion functions.
static bool AttrCompatibleWithMultiVersion(attr::Kind Kind,
                                           MultiVersionKind MVKind) {
  // Note: this list/diagnosis must match the list in
  // checkMultiversionAttributesAllSame.
  switch (Kind) {
  default:
    return false;
  case attr::ArmLocallyStreaming:
    return MVKind == MultiVersionKind::TargetVersion ||
           MVKind == MultiVersionKind::TargetClones;
  case attr::Used:
    return MVKind == MultiVersionKind::Target;
  case attr::NonNull:
  case attr::NoThrow:
    return true;
  }
}

static bool checkNonMultiVersionCompatAttributes(Sema &S,
                                                 const FunctionDecl *FD,
                                                 const FunctionDecl *CausedFD,
                                                 MultiVersionKind MVKind) {
  const auto Diagnose = [FD, CausedFD, MVKind](Sema &S, const Attr *A) {
    S.Diag(FD->getLocation(), diag::err_multiversion_disallowed_other_attr)
        << static_cast<unsigned>(MVKind) << A;
    if (CausedFD)
      S.Diag(CausedFD->getLocation(), diag::note_multiversioning_caused_here);
    return true;
  };

  for (const Attr *A : FD->attrs()) {
    switch (A->getKind()) {
    case attr::CPUDispatch:
    case attr::CPUSpecific:
      if (MVKind != MultiVersionKind::CPUDispatch &&
          MVKind != MultiVersionKind::CPUSpecific)
        return Diagnose(S, A);
      break;
    case attr::Target:
      if (MVKind != MultiVersionKind::Target)
        return Diagnose(S, A);
      break;
    case attr::TargetVersion:
      if (MVKind != MultiVersionKind::TargetVersion &&
          MVKind != MultiVersionKind::TargetClones)
        return Diagnose(S, A);
      break;
    case attr::TargetClones:
      if (MVKind != MultiVersionKind::TargetClones &&
          MVKind != MultiVersionKind::TargetVersion)
        return Diagnose(S, A);
      break;
    default:
      if (!AttrCompatibleWithMultiVersion(A->getKind(), MVKind))
        return Diagnose(S, A);
      break;
    }
  }
  return false;
}

bool Sema::areMultiversionVariantFunctionsCompatible(
    const FunctionDecl *OldFD, const FunctionDecl *NewFD,
    const PartialDiagnostic &NoProtoDiagID,
    const PartialDiagnosticAt &NoteCausedDiagIDAt,
    const PartialDiagnosticAt &NoSupportDiagIDAt,
    const PartialDiagnosticAt &DiffDiagIDAt, bool TemplatesSupported,
    bool ConstexprSupported, bool CLinkageMayDiffer) {
  enum DoesntSupport {
    FuncTemplates = 0,
    VirtFuncs = 1,
    DeducedReturn = 2,
    Constructors = 3,
    Destructors = 4,
    DeletedFuncs = 5,
    DefaultedFuncs = 6,
    ConstexprFuncs = 7,
    ConstevalFuncs = 8,
    Lambda = 9,
  };
  enum Different {
    CallingConv = 0,
    ReturnType = 1,
    ConstexprSpec = 2,
    InlineSpec = 3,
    Linkage = 4,
    LanguageLinkage = 5,
  };

  if (NoProtoDiagID.getDiagID() != 0 && OldFD &&
      !OldFD->getType()->getAs<FunctionProtoType>()) {
    Diag(OldFD->getLocation(), NoProtoDiagID);
    Diag(NoteCausedDiagIDAt.first, NoteCausedDiagIDAt.second);
    return true;
  }

  if (NoProtoDiagID.getDiagID() != 0 &&
      !NewFD->getType()->getAs<FunctionProtoType>())
    return Diag(NewFD->getLocation(), NoProtoDiagID);

  if (!TemplatesSupported &&
      NewFD->getTemplatedKind() == FunctionDecl::TK_FunctionTemplate)
    return Diag(NoSupportDiagIDAt.first, NoSupportDiagIDAt.second)
           << FuncTemplates;

  if (const auto *NewCXXFD = dyn_cast<CXXMethodDecl>(NewFD)) {
    if (NewCXXFD->isVirtual())
      return Diag(NoSupportDiagIDAt.first, NoSupportDiagIDAt.second)
             << VirtFuncs;

    if (isa<CXXConstructorDecl>(NewCXXFD))
      return Diag(NoSupportDiagIDAt.first, NoSupportDiagIDAt.second)
             << Constructors;

    if (isa<CXXDestructorDecl>(NewCXXFD))
      return Diag(NoSupportDiagIDAt.first, NoSupportDiagIDAt.second)
             << Destructors;
  }

  if (NewFD->isDeleted())
    return Diag(NoSupportDiagIDAt.first, NoSupportDiagIDAt.second)
           << DeletedFuncs;

  if (NewFD->isDefaulted())
    return Diag(NoSupportDiagIDAt.first, NoSupportDiagIDAt.second)
           << DefaultedFuncs;

  if (!ConstexprSupported && NewFD->isConstexpr())
    return Diag(NoSupportDiagIDAt.first, NoSupportDiagIDAt.second)
           << (NewFD->isConsteval() ? ConstevalFuncs : ConstexprFuncs);

  QualType NewQType = Context.getCanonicalType(NewFD->getType());
  const auto *NewType = cast<FunctionType>(NewQType);
  QualType NewReturnType = NewType->getReturnType();

  if (NewReturnType->isUndeducedType())
    return Diag(NoSupportDiagIDAt.first, NoSupportDiagIDAt.second)
           << DeducedReturn;

  // Ensure the return type is identical.
  if (OldFD) {
    QualType OldQType = Context.getCanonicalType(OldFD->getType());
    const auto *OldType = cast<FunctionType>(OldQType);
    FunctionType::ExtInfo OldTypeInfo = OldType->getExtInfo();
    FunctionType::ExtInfo NewTypeInfo = NewType->getExtInfo();

    const auto *OldFPT = OldFD->getType()->getAs<FunctionProtoType>();
    const auto *NewFPT = NewFD->getType()->getAs<FunctionProtoType>();

    bool ArmStreamingCCMismatched = false;
    if (OldFPT && NewFPT) {
      unsigned Diff =
          OldFPT->getAArch64SMEAttributes() ^ NewFPT->getAArch64SMEAttributes();
      // Arm-streaming, arm-streaming-compatible and non-streaming versions
      // cannot be mixed.
      if (Diff & (FunctionType::SME_PStateSMEnabledMask |
                  FunctionType::SME_PStateSMCompatibleMask))
        ArmStreamingCCMismatched = true;
    }

    if (OldTypeInfo.getCC() != NewTypeInfo.getCC() || ArmStreamingCCMismatched)
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << CallingConv;

    QualType OldReturnType = OldType->getReturnType();

    if (OldReturnType != NewReturnType)
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << ReturnType;

    if (OldFD->getConstexprKind() != NewFD->getConstexprKind())
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << ConstexprSpec;

    if (OldFD->isInlineSpecified() != NewFD->isInlineSpecified())
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << InlineSpec;

    if (OldFD->getFormalLinkage() != NewFD->getFormalLinkage())
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << Linkage;

    if (!CLinkageMayDiffer && OldFD->isExternC() != NewFD->isExternC())
      return Diag(DiffDiagIDAt.first, DiffDiagIDAt.second) << LanguageLinkage;

    if (CheckEquivalentExceptionSpec(OldFPT, OldFD->getLocation(), NewFPT,
                                     NewFD->getLocation()))
      return true;
  }
  return false;
}

static bool CheckMultiVersionAdditionalRules(Sema &S, const FunctionDecl *OldFD,
                                             const FunctionDecl *NewFD,
                                             bool CausesMV,
                                             MultiVersionKind MVKind) {
  if (!S.getASTContext().getTargetInfo().supportsMultiVersioning()) {
    S.Diag(NewFD->getLocation(), diag::err_multiversion_not_supported);
    if (OldFD)
      S.Diag(OldFD->getLocation(), diag::note_previous_declaration);
    return true;
  }

  bool IsCPUSpecificCPUDispatchMVKind =
      MVKind == MultiVersionKind::CPUDispatch ||
      MVKind == MultiVersionKind::CPUSpecific;

  if (CausesMV && OldFD &&
      checkNonMultiVersionCompatAttributes(S, OldFD, NewFD, MVKind))
    return true;

  if (checkNonMultiVersionCompatAttributes(S, NewFD, nullptr, MVKind))
    return true;

  // Only allow transition to MultiVersion if it hasn't been used.
  if (OldFD && CausesMV && OldFD->isUsed(false))
    return S.Diag(NewFD->getLocation(), diag::err_multiversion_after_used);

  return S.areMultiversionVariantFunctionsCompatible(
      OldFD, NewFD, S.PDiag(diag::err_multiversion_noproto),
      PartialDiagnosticAt(NewFD->getLocation(),
                          S.PDiag(diag::note_multiversioning_caused_here)),
      PartialDiagnosticAt(NewFD->getLocation(),
                          S.PDiag(diag::err_multiversion_doesnt_support)
                              << static_cast<unsigned>(MVKind)),
      PartialDiagnosticAt(NewFD->getLocation(),
                          S.PDiag(diag::err_multiversion_diff)),
      /*TemplatesSupported=*/false,
      /*ConstexprSupported=*/!IsCPUSpecificCPUDispatchMVKind,
      /*CLinkageMayDiffer=*/false);
}

/// Check the validity of a multiversion function declaration that is the
/// first of its kind. Also sets the multiversion'ness' of the function itself.
///
/// This sets NewFD->isInvalidDecl() to true if there was an error.
///
/// Returns true if there was an error, false otherwise.
static bool CheckMultiVersionFirstFunction(Sema &S, FunctionDecl *FD) {
  MultiVersionKind MVKind = FD->getMultiVersionKind();
  assert(MVKind != MultiVersionKind::None &&
         "Function lacks multiversion attribute");
  const auto *TA = FD->getAttr<TargetAttr>();
  const auto *TVA = FD->getAttr<TargetVersionAttr>();
  // The target attribute only causes MV if this declaration is the default,
  // otherwise it is treated as a normal function.
  if (TA && !TA->isDefaultVersion())
    return false;
  // The target_version attribute only causes Multiversioning if this
  // declaration is NOT the default version.
  if (TVA && TVA->isDefaultVersion())
    return false;

  if ((TA || TVA) && CheckMultiVersionValue(S, FD)) {
    FD->setInvalidDecl();
    return true;
  }

  if (CheckMultiVersionAdditionalRules(S, nullptr, FD, true, MVKind)) {
    FD->setInvalidDecl();
    return true;
  }

  FD->setIsMultiVersion();
  return false;
}

static bool PreviousDeclsHaveMultiVersionAttribute(const FunctionDecl *FD) {
  for (const Decl *D = FD->getPreviousDecl(); D; D = D->getPreviousDecl()) {
    if (D->getAsFunction()->getMultiVersionKind() != MultiVersionKind::None)
      return true;
  }

  return false;
}

static void patchDefaultTargetVersion(FunctionDecl *From, FunctionDecl *To) {
  if (!From->getASTContext().getTargetInfo().getTriple().isAArch64())
    return;

  MultiVersionKind MVKindFrom = From->getMultiVersionKind();
  MultiVersionKind MVKindTo = To->getMultiVersionKind();

  if (MVKindTo == MultiVersionKind::None &&
      (MVKindFrom == MultiVersionKind::TargetVersion ||
       MVKindFrom == MultiVersionKind::TargetClones))
    To->addAttr(TargetVersionAttr::CreateImplicit(
        To->getASTContext(), "default", To->getSourceRange()));
}

static bool CheckDeclarationCausesMultiVersioning(Sema &S, FunctionDecl *OldFD,
                                                  FunctionDecl *NewFD,
                                                  bool &Redeclaration,
                                                  NamedDecl *&OldDecl,
                                                  LookupResult &Previous) {
  assert(!OldFD->isMultiVersion() && "Unexpected MultiVersion");

  // The definitions should be allowed in any order. If we have discovered
  // a new target version and the preceeding was the default, then add the
  // corresponding attribute to it.
  patchDefaultTargetVersion(NewFD, OldFD);

  const auto *NewTA = NewFD->getAttr<TargetAttr>();
  const auto *NewTVA = NewFD->getAttr<TargetVersionAttr>();
  const auto *OldTA = OldFD->getAttr<TargetAttr>();

  // If the old decl is NOT MultiVersioned yet, and we don't cause that
  // to change, this is a simple redeclaration.
  if (NewTA && !NewTA->isDefaultVersion() &&
      (!OldTA || OldTA->getFeaturesStr() == NewTA->getFeaturesStr()))
    return false;

  // The target_version attribute only causes Multiversioning if this
  // declaration is NOT the default version.
  if (NewTVA && NewTVA->isDefaultVersion())
    return false;

  // Otherwise, this decl causes MultiVersioning.
  if (CheckMultiVersionAdditionalRules(S, OldFD, NewFD, true,
                                       NewTVA ? MultiVersionKind::TargetVersion
                                              : MultiVersionKind::Target)) {
    NewFD->setInvalidDecl();
    return true;
  }

  if (CheckMultiVersionValue(S, NewFD)) {
    NewFD->setInvalidDecl();
    return true;
  }

  // If this is 'default', permit the forward declaration.
  if (NewTA && NewTA->isDefaultVersion() && !OldTA) {
    Redeclaration = true;
    OldDecl = OldFD;
    OldFD->setIsMultiVersion();
    NewFD->setIsMultiVersion();
    return false;
  }

  if (CheckMultiVersionValue(S, OldFD)) {
    S.Diag(NewFD->getLocation(), diag::note_multiversioning_caused_here);
    NewFD->setInvalidDecl();
    return true;
  }

  if (NewTA) {
    ParsedTargetAttr OldParsed =
        S.getASTContext().getTargetInfo().parseTargetAttr(
            OldTA->getFeaturesStr());
    llvm::sort(OldParsed.Features);
    ParsedTargetAttr NewParsed =
        S.getASTContext().getTargetInfo().parseTargetAttr(
            NewTA->getFeaturesStr());
    // Sort order doesn't matter, it just needs to be consistent.
    llvm::sort(NewParsed.Features);
    if (OldParsed == NewParsed) {
      S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
      S.Diag(OldFD->getLocation(), diag::note_previous_declaration);
      NewFD->setInvalidDecl();
      return true;
    }
  }

  for (const auto *FD : OldFD->redecls()) {
    const auto *CurTA = FD->getAttr<TargetAttr>();
    const auto *CurTVA = FD->getAttr<TargetVersionAttr>();
    // We allow forward declarations before ANY multiversioning attributes, but
    // nothing after the fact.
    if (PreviousDeclsHaveMultiVersionAttribute(FD) &&
        ((NewTA && (!CurTA || CurTA->isInherited())) ||
         (NewTVA && (!CurTVA || CurTVA->isInherited())))) {
      S.Diag(FD->getLocation(), diag::err_multiversion_required_in_redecl)
          << (NewTA ? 0 : 2);
      S.Diag(NewFD->getLocation(), diag::note_multiversioning_caused_here);
      NewFD->setInvalidDecl();
      return true;
    }
  }

  OldFD->setIsMultiVersion();
  NewFD->setIsMultiVersion();
  Redeclaration = false;
  OldDecl = nullptr;
  Previous.clear();
  return false;
}

static bool MultiVersionTypesCompatible(FunctionDecl *Old, FunctionDecl *New) {
  MultiVersionKind OldKind = Old->getMultiVersionKind();
  MultiVersionKind NewKind = New->getMultiVersionKind();

  if (OldKind == NewKind || OldKind == MultiVersionKind::None ||
      NewKind == MultiVersionKind::None)
    return true;

  if (Old->getASTContext().getTargetInfo().getTriple().isAArch64()) {
    switch (OldKind) {
    case MultiVersionKind::TargetVersion:
      return NewKind == MultiVersionKind::TargetClones;
    case MultiVersionKind::TargetClones:
      return NewKind == MultiVersionKind::TargetVersion;
    default:
      return false;
    }
  } else {
    switch (OldKind) {
    case MultiVersionKind::CPUDispatch:
      return NewKind == MultiVersionKind::CPUSpecific;
    case MultiVersionKind::CPUSpecific:
      return NewKind == MultiVersionKind::CPUDispatch;
    default:
      return false;
    }
  }
}

/// Check the validity of a new function declaration being added to an existing
/// multiversioned declaration collection.
static bool CheckMultiVersionAdditionalDecl(
    Sema &S, FunctionDecl *OldFD, FunctionDecl *NewFD,
    const CPUDispatchAttr *NewCPUDisp, const CPUSpecificAttr *NewCPUSpec,
    const TargetClonesAttr *NewClones, bool &Redeclaration, NamedDecl *&OldDecl,
    LookupResult &Previous) {

  // Disallow mixing of multiversioning types.
  if (!MultiVersionTypesCompatible(OldFD, NewFD)) {
    S.Diag(NewFD->getLocation(), diag::err_multiversion_types_mixed);
    S.Diag(OldFD->getLocation(), diag::note_previous_declaration);
    NewFD->setInvalidDecl();
    return true;
  }

  // Add the default target_version attribute if it's missing.
  patchDefaultTargetVersion(OldFD, NewFD);
  patchDefaultTargetVersion(NewFD, OldFD);

  const auto *NewTA = NewFD->getAttr<TargetAttr>();
  const auto *NewTVA = NewFD->getAttr<TargetVersionAttr>();
  MultiVersionKind NewMVKind = NewFD->getMultiVersionKind();
  [[maybe_unused]] MultiVersionKind OldMVKind = OldFD->getMultiVersionKind();

  ParsedTargetAttr NewParsed;
  if (NewTA) {
    NewParsed = S.getASTContext().getTargetInfo().parseTargetAttr(
        NewTA->getFeaturesStr());
    llvm::sort(NewParsed.Features);
  }
  llvm::SmallVector<StringRef, 8> NewFeats;
  if (NewTVA) {
    NewTVA->getFeatures(NewFeats);
    llvm::sort(NewFeats);
  }

  bool UseMemberUsingDeclRules =
      S.CurContext->isRecord() && !NewFD->getFriendObjectKind();

  bool MayNeedOverloadableChecks =
      AllowOverloadingOfFunction(Previous, S.Context, NewFD);

  // Next, check ALL non-invalid non-overloads to see if this is a redeclaration
  // of a previous member of the MultiVersion set.
  for (NamedDecl *ND : Previous) {
    FunctionDecl *CurFD = ND->getAsFunction();
    if (!CurFD || CurFD->isInvalidDecl())
      continue;
    if (MayNeedOverloadableChecks &&
        S.IsOverload(NewFD, CurFD, UseMemberUsingDeclRules))
      continue;

    switch (NewMVKind) {
    case MultiVersionKind::None:
      assert(OldMVKind == MultiVersionKind::TargetClones &&
             "Only target_clones can be omitted in subsequent declarations");
      break;
    case MultiVersionKind::Target: {
      const auto *CurTA = CurFD->getAttr<TargetAttr>();
      if (CurTA->getFeaturesStr() == NewTA->getFeaturesStr()) {
        NewFD->setIsMultiVersion();
        Redeclaration = true;
        OldDecl = ND;
        return false;
      }

      ParsedTargetAttr CurParsed =
          S.getASTContext().getTargetInfo().parseTargetAttr(
              CurTA->getFeaturesStr());
      llvm::sort(CurParsed.Features);
      if (CurParsed == NewParsed) {
        S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
        S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
        NewFD->setInvalidDecl();
        return true;
      }
      break;
    }
    case MultiVersionKind::TargetVersion: {
      if (const auto *CurTVA = CurFD->getAttr<TargetVersionAttr>()) {
        if (CurTVA->getName() == NewTVA->getName()) {
          NewFD->setIsMultiVersion();
          Redeclaration = true;
          OldDecl = ND;
          return false;
        }
        llvm::SmallVector<StringRef, 8> CurFeats;
        CurTVA->getFeatures(CurFeats);
        llvm::sort(CurFeats);

        if (CurFeats == NewFeats) {
          S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
          S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
          NewFD->setInvalidDecl();
          return true;
        }
      } else if (const auto *CurClones = CurFD->getAttr<TargetClonesAttr>()) {
        // Default
        if (NewFeats.empty())
          break;

        for (unsigned I = 0; I < CurClones->featuresStrs_size(); ++I) {
          llvm::SmallVector<StringRef, 8> CurFeats;
          CurClones->getFeatures(CurFeats, I);
          llvm::sort(CurFeats);

          if (CurFeats == NewFeats) {
            S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
            S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
            NewFD->setInvalidDecl();
            return true;
          }
        }
      }
      break;
    }
    case MultiVersionKind::TargetClones: {
      assert(NewClones && "MultiVersionKind does not match attribute type");
      if (const auto *CurClones = CurFD->getAttr<TargetClonesAttr>()) {
        if (CurClones->featuresStrs_size() != NewClones->featuresStrs_size() ||
            !std::equal(CurClones->featuresStrs_begin(),
                        CurClones->featuresStrs_end(),
                        NewClones->featuresStrs_begin())) {
          S.Diag(NewFD->getLocation(), diag::err_target_clone_doesnt_match);
          S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
          NewFD->setInvalidDecl();
          return true;
        }
      } else if (const auto *CurTVA = CurFD->getAttr<TargetVersionAttr>()) {
        llvm::SmallVector<StringRef, 8> CurFeats;
        CurTVA->getFeatures(CurFeats);
        llvm::sort(CurFeats);

        // Default
        if (CurFeats.empty())
          break;

        for (unsigned I = 0; I < NewClones->featuresStrs_size(); ++I) {
          NewFeats.clear();
          NewClones->getFeatures(NewFeats, I);
          llvm::sort(NewFeats);

          if (CurFeats == NewFeats) {
            S.Diag(NewFD->getLocation(), diag::err_multiversion_duplicate);
            S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
            NewFD->setInvalidDecl();
            return true;
          }
        }
        break;
      }
      Redeclaration = true;
      OldDecl = CurFD;
      NewFD->setIsMultiVersion();
      return false;
    }
    case MultiVersionKind::CPUSpecific:
    case MultiVersionKind::CPUDispatch: {
      const auto *CurCPUSpec = CurFD->getAttr<CPUSpecificAttr>();
      const auto *CurCPUDisp = CurFD->getAttr<CPUDispatchAttr>();
      // Handle CPUDispatch/CPUSpecific versions.
      // Only 1 CPUDispatch function is allowed, this will make it go through
      // the redeclaration errors.
      if (NewMVKind == MultiVersionKind::CPUDispatch &&
          CurFD->hasAttr<CPUDispatchAttr>()) {
        if (CurCPUDisp->cpus_size() == NewCPUDisp->cpus_size() &&
            std::equal(
                CurCPUDisp->cpus_begin(), CurCPUDisp->cpus_end(),
                NewCPUDisp->cpus_begin(),
                [](const IdentifierInfo *Cur, const IdentifierInfo *New) {
                  return Cur->getName() == New->getName();
                })) {
          NewFD->setIsMultiVersion();
          Redeclaration = true;
          OldDecl = ND;
          return false;
        }

        // If the declarations don't match, this is an error condition.
        S.Diag(NewFD->getLocation(), diag::err_cpu_dispatch_mismatch);
        S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
        NewFD->setInvalidDecl();
        return true;
      }
      if (NewMVKind == MultiVersionKind::CPUSpecific && CurCPUSpec) {
        if (CurCPUSpec->cpus_size() == NewCPUSpec->cpus_size() &&
            std::equal(
                CurCPUSpec->cpus_begin(), CurCPUSpec->cpus_end(),
                NewCPUSpec->cpus_begin(),
                [](const IdentifierInfo *Cur, const IdentifierInfo *New) {
                  return Cur->getName() == New->getName();
                })) {
          NewFD->setIsMultiVersion();
          Redeclaration = true;
          OldDecl = ND;
          return false;
        }

        // Only 1 version of CPUSpecific is allowed for each CPU.
        for (const IdentifierInfo *CurII : CurCPUSpec->cpus()) {
          for (const IdentifierInfo *NewII : NewCPUSpec->cpus()) {
            if (CurII == NewII) {
              S.Diag(NewFD->getLocation(), diag::err_cpu_specific_multiple_defs)
                  << NewII;
              S.Diag(CurFD->getLocation(), diag::note_previous_declaration);
              NewFD->setInvalidDecl();
              return true;
            }
          }
        }
      }
      break;
    }
    }
  }

  // Else, this is simply a non-redecl case.  Checking the 'value' is only
  // necessary in the Target case, since The CPUSpecific/Dispatch cases are
  // handled in the attribute adding step.
  if ((NewMVKind == MultiVersionKind::TargetVersion ||
       NewMVKind == MultiVersionKind::Target) &&
      CheckMultiVersionValue(S, NewFD)) {
    NewFD->setInvalidDecl();
    return true;
  }

  if (CheckMultiVersionAdditionalRules(S, OldFD, NewFD,
                                       !OldFD->isMultiVersion(), NewMVKind)) {
    NewFD->setInvalidDecl();
    return true;
  }

  // Permit forward declarations in the case where these two are compatible.
  if (!OldFD->isMultiVersion()) {
    OldFD->setIsMultiVersion();
    NewFD->setIsMultiVersion();
    Redeclaration = true;
    OldDecl = OldFD;
    return false;
  }

  NewFD->setIsMultiVersion();
  Redeclaration = false;
  OldDecl = nullptr;
  Previous.clear();
  return false;
}

/// Check the validity of a mulitversion function declaration.
/// Also sets the multiversion'ness' of the function itself.
///
/// This sets NewFD->isInvalidDecl() to true if there was an error.
///
/// Returns true if there was an error, false otherwise.
static bool CheckMultiVersionFunction(Sema &S, FunctionDecl *NewFD,
                                      bool &Redeclaration, NamedDecl *&OldDecl,
                                      LookupResult &Previous) {
  const auto *NewTA = NewFD->getAttr<TargetAttr>();
  const auto *NewTVA = NewFD->getAttr<TargetVersionAttr>();
  const auto *NewCPUDisp = NewFD->getAttr<CPUDispatchAttr>();
  const auto *NewCPUSpec = NewFD->getAttr<CPUSpecificAttr>();
  const auto *NewClones = NewFD->getAttr<TargetClonesAttr>();
  MultiVersionKind MVKind = NewFD->getMultiVersionKind();

  // Main isn't allowed to become a multiversion function, however it IS
  // permitted to have 'main' be marked with the 'target' optimization hint,
  // for 'target_version' only default is allowed.
  if (NewFD->isMain()) {
    if (MVKind != MultiVersionKind::None &&
        !(MVKind == MultiVersionKind::Target && !NewTA->isDefaultVersion()) &&
        !(MVKind == MultiVersionKind::TargetVersion &&
          NewTVA->isDefaultVersion())) {
      S.Diag(NewFD->getLocation(), diag::err_multiversion_not_allowed_on_main);
      NewFD->setInvalidDecl();
      return true;
    }
    return false;
  }

  const llvm::Triple &T = S.getASTContext().getTargetInfo().getTriple();

  // Target attribute on AArch64 is not used for multiversioning
  if (NewTA && T.isAArch64())
    return false;

  // Target attribute on RISCV is not used for multiversioning
  if (NewTA && T.isRISCV())
    return false;

  if (!OldDecl || !OldDecl->getAsFunction() ||
      !OldDecl->getDeclContext()->getRedeclContext()->Equals(
          NewFD->getDeclContext()->getRedeclContext())) {
    // If there's no previous declaration, AND this isn't attempting to cause
    // multiversioning, this isn't an error condition.
    if (MVKind == MultiVersionKind::None)
      return false;
    return CheckMultiVersionFirstFunction(S, NewFD);
  }

  FunctionDecl *OldFD = OldDecl->getAsFunction();

  if (!OldFD->isMultiVersion() && MVKind == MultiVersionKind::None)
    return false;

  // Multiversioned redeclarations aren't allowed to omit the attribute, except
  // for target_clones and target_version.
  if (OldFD->isMultiVersion() && MVKind == MultiVersionKind::None &&
      OldFD->getMultiVersionKind() != MultiVersionKind::TargetClones &&
      OldFD->getMultiVersionKind() != MultiVersionKind::TargetVersion) {
    S.Diag(NewFD->getLocation(), diag::err_multiversion_required_in_redecl)
        << (OldFD->getMultiVersionKind() != MultiVersionKind::Target);
    NewFD->setInvalidDecl();
    return true;
  }

  if (!OldFD->isMultiVersion()) {
    switch (MVKind) {
    case MultiVersionKind::Target:
    case MultiVersionKind::TargetVersion:
      return CheckDeclarationCausesMultiVersioning(
          S, OldFD, NewFD, Redeclaration, OldDecl, Previous);
    case MultiVersionKind::TargetClones:
      if (OldFD->isUsed(false)) {
        NewFD->setInvalidDecl();
        return S.Diag(NewFD->getLocation(), diag::err_multiversion_after_used);
      }
      OldFD->setIsMultiVersion();
      break;

    case MultiVersionKind::CPUDispatch:
    case MultiVersionKind::CPUSpecific:
    case MultiVersionKind::None:
      break;
    }
  }

  // At this point, we have a multiversion function decl (in OldFD) AND an
  // appropriate attribute in the current function decl.  Resolve that these are
  // still compatible with previous declarations.
  return CheckMultiVersionAdditionalDecl(S, OldFD, NewFD, NewCPUDisp,
                                         NewCPUSpec, NewClones, Redeclaration,
                                         OldDecl, Previous);
}

static void CheckConstPureAttributesUsage(Sema &S, FunctionDecl *NewFD) {
  bool IsPure = NewFD->hasAttr<PureAttr>();
  bool IsConst = NewFD->hasAttr<ConstAttr>();

  // If there are no pure or const attributes, there's nothing to check.
  if (!IsPure && !IsConst)
    return;

  // If the function is marked both pure and const, we retain the const
  // attribute because it makes stronger guarantees than the pure attribute, and
  // we drop the pure attribute explicitly to prevent later confusion about
  // semantics.
  if (IsPure && IsConst) {
    S.Diag(NewFD->getLocation(), diag::warn_const_attr_with_pure_attr);
    NewFD->dropAttrs<PureAttr>();
  }

  // Constructors and destructors are functions which return void, so are
  // handled here as well.
  if (NewFD->getReturnType()->isVoidType()) {
    S.Diag(NewFD->getLocation(), diag::warn_pure_function_returns_void)
        << IsConst;
    NewFD->dropAttrs<PureAttr, ConstAttr>();
  }
}

bool Sema::CheckFunctionDeclaration(Scope *S, FunctionDecl *NewFD,
                                    LookupResult &Previous,
                                    bool IsMemberSpecialization,
                                    bool DeclIsDefn) {
  assert(!NewFD->getReturnType()->isVariablyModifiedType() &&
         "Variably modified return types are not handled here");

  // Determine whether the type of this function should be merged with
  // a previous visible declaration. This never happens for functions in C++,
  // and always happens in C if the previous declaration was visible.
  bool MergeTypeWithPrevious = !getLangOpts().CPlusPlus &&
                               !Previous.isShadowed();

  bool Redeclaration = false;
  NamedDecl *OldDecl = nullptr;
  bool MayNeedOverloadableChecks = false;

  // Merge or overload the declaration with an existing declaration of
  // the same name, if appropriate.
  if (!Previous.empty()) {
    // Determine whether NewFD is an overload of PrevDecl or
    // a declaration that requires merging. If it's an overload,
    // there's no more work to do here; we'll just add the new
    // function to the scope.
    if (!AllowOverloadingOfFunction(Previous, Context, NewFD)) {
      NamedDecl *Candidate = Previous.getRepresentativeDecl();
      if (shouldLinkPossiblyHiddenDecl(Candidate, NewFD)) {
        Redeclaration = true;
        OldDecl = Candidate;
      }
    } else {
      MayNeedOverloadableChecks = true;
      switch (CheckOverload(S, NewFD, Previous, OldDecl,
                            /*NewIsUsingDecl*/ false)) {
      case Ovl_Match:
        Redeclaration = true;
        break;

      case Ovl_NonFunction:
        Redeclaration = true;
        break;

      case Ovl_Overload:
        Redeclaration = false;
        break;
      }
    }
  }

  // Check for a previous extern "C" declaration with this name.
  if (!Redeclaration &&
      checkForConflictWithNonVisibleExternC(*this, NewFD, Previous)) {
    if (!Previous.empty()) {
      // This is an extern "C" declaration with the same name as a previous
      // declaration, and thus redeclares that entity...
      Redeclaration = true;
      OldDecl = Previous.getFoundDecl();
      MergeTypeWithPrevious = false;

      // ... except in the presence of __attribute__((overloadable)).
      if (OldDecl->hasAttr<OverloadableAttr>() ||
          NewFD->hasAttr<OverloadableAttr>()) {
        if (IsOverload(NewFD, cast<FunctionDecl>(OldDecl), false)) {
          MayNeedOverloadableChecks = true;
          Redeclaration = false;
          OldDecl = nullptr;
        }
      }
    }
  }

  if (CheckMultiVersionFunction(*this, NewFD, Redeclaration, OldDecl, Previous))
    return Redeclaration;

  // PPC MMA non-pointer types are not allowed as function return types.
  if (Context.getTargetInfo().getTriple().isPPC64() &&
      PPC().CheckPPCMMAType(NewFD->getReturnType(), NewFD->getLocation())) {
    NewFD->setInvalidDecl();
  }

  CheckConstPureAttributesUsage(*this, NewFD);

  // C++ [dcl.spec.auto.general]p12:
  //   Return type deduction for a templated function with a placeholder in its
  //   declared type occurs when the definition is instantiated even if the
  //   function body contains a return statement with a non-type-dependent
  //   operand.
  //
  // C++ [temp.dep.expr]p3:
  //   An id-expression is type-dependent if it is a template-id that is not a
  //   concept-id and is dependent; or if its terminal name is:
  //   - [...]
  //   - associated by name lookup with one or more declarations of member
  //     functions of a class that is the current instantiation declared with a
  //     return type that contains a placeholder type,
  //   - [...]
  //
  // If this is a templated function with a placeholder in its return type,
  // make the placeholder type dependent since it won't be deduced until the
  // definition is instantiated. We do this here because it needs to happen
  // for implicitly instantiated member functions/member function templates.
  if (getLangOpts().CPlusPlus14 &&
      (NewFD->isDependentContext() &&
       NewFD->getReturnType()->isUndeducedType())) {
    const FunctionProtoType *FPT =
        NewFD->getType()->castAs<FunctionProtoType>();
    QualType NewReturnType = SubstAutoTypeDependent(FPT->getReturnType());
    NewFD->setType(Context.getFunctionType(NewReturnType, FPT->getParamTypes(),
                                           FPT->getExtProtoInfo()));
  }

  // C++11 [dcl.constexpr]p8:
  //   A constexpr specifier for a non-static member function that is not
  //   a constructor declares that member function to be const.
  //
  // This needs to be delayed until we know whether this is an out-of-line
  // definition of a static member function.
  //
  // This rule is not present in C++1y, so we produce a backwards
  // compatibility warning whenever it happens in C++11.
  CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(NewFD);
  if (!getLangOpts().CPlusPlus14 && MD && MD->isConstexpr() &&
      !MD->isStatic() && !isa<CXXConstructorDecl>(MD) &&
      !isa<CXXDestructorDecl>(MD) && !MD->getMethodQualifiers().hasConst()) {
    CXXMethodDecl *OldMD = nullptr;
    if (OldDecl)
      OldMD = dyn_cast_or_null<CXXMethodDecl>(OldDecl->getAsFunction());
    if (!OldMD || !OldMD->isStatic()) {
      const FunctionProtoType *FPT =
        MD->getType()->castAs<FunctionProtoType>();
      FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
      EPI.TypeQuals.addConst();
      MD->setType(Context.getFunctionType(FPT->getReturnType(),
                                          FPT->getParamTypes(), EPI));

      // Warn that we did this, if we're not performing template instantiation.
      // In that case, we'll have warned already when the template was defined.
      if (!inTemplateInstantiation()) {
        SourceLocation AddConstLoc;
        if (FunctionTypeLoc FTL = MD->getTypeSourceInfo()->getTypeLoc()
                .IgnoreParens().getAs<FunctionTypeLoc>())
          AddConstLoc = getLocForEndOfToken(FTL.getRParenLoc());

        Diag(MD->getLocation(), diag::warn_cxx14_compat_constexpr_not_const)
          << FixItHint::CreateInsertion(AddConstLoc, " const");
      }
    }
  }

  if (Redeclaration) {
    // NewFD and OldDecl represent declarations that need to be
    // merged.
    if (MergeFunctionDecl(NewFD, OldDecl, S, MergeTypeWithPrevious,
                          DeclIsDefn)) {
      NewFD->setInvalidDecl();
      return Redeclaration;
    }

    Previous.clear();
    Previous.addDecl(OldDecl);

    if (FunctionTemplateDecl *OldTemplateDecl =
            dyn_cast<FunctionTemplateDecl>(OldDecl)) {
      auto *OldFD = OldTemplateDecl->getTemplatedDecl();
      FunctionTemplateDecl *NewTemplateDecl
        = NewFD->getDescribedFunctionTemplate();
      assert(NewTemplateDecl && "Template/non-template mismatch");

      // The call to MergeFunctionDecl above may have created some state in
      // NewTemplateDecl that needs to be merged with OldTemplateDecl before we
      // can add it as a redeclaration.
      NewTemplateDecl->mergePrevDecl(OldTemplateDecl);

      NewFD->setPreviousDeclaration(OldFD);
      if (NewFD->isCXXClassMember()) {
        NewFD->setAccess(OldTemplateDecl->getAccess());
        NewTemplateDecl->setAccess(OldTemplateDecl->getAccess());
      }

      // If this is an explicit specialization of a member that is a function
      // template, mark it as a member specialization.
      if (IsMemberSpecialization &&
          NewTemplateDecl->getInstantiatedFromMemberTemplate()) {
        NewTemplateDecl->setMemberSpecialization();
        assert(OldTemplateDecl->isMemberSpecialization());
        // Explicit specializations of a member template do not inherit deleted
        // status from the parent member template that they are specializing.
        if (OldFD->isDeleted()) {
          // FIXME: This assert will not hold in the presence of modules.
          assert(OldFD->getCanonicalDecl() == OldFD);
          // FIXME: We need an update record for this AST mutation.
          OldFD->setDeletedAsWritten(false);
        }
      }

    } else {
      if (shouldLinkDependentDeclWithPrevious(NewFD, OldDecl)) {
        auto *OldFD = cast<FunctionDecl>(OldDecl);
        // This needs to happen first so that 'inline' propagates.
        NewFD->setPreviousDeclaration(OldFD);
        if (NewFD->isCXXClassMember())
          NewFD->setAccess(OldFD->getAccess());
      }
    }
  } else if (!getLangOpts().CPlusPlus && MayNeedOverloadableChecks &&
             !NewFD->getAttr<OverloadableAttr>()) {
    assert((Previous.empty() ||
            llvm::any_of(Previous,
                         [](const NamedDecl *ND) {
                           return ND->hasAttr<OverloadableAttr>();
                         })) &&
           "Non-redecls shouldn't happen without overloadable present");

    auto OtherUnmarkedIter = llvm::find_if(Previous, [](const NamedDecl *ND) {
      const auto *FD = dyn_cast<FunctionDecl>(ND);
      return FD && !FD->hasAttr<OverloadableAttr>();
    });

    if (OtherUnmarkedIter != Previous.end()) {
      Diag(NewFD->getLocation(),
           diag::err_attribute_overloadable_multiple_unmarked_overloads);
      Diag((*OtherUnmarkedIter)->getLocation(),
           diag::note_attribute_overloadable_prev_overload)
          << false;

      NewFD->addAttr(OverloadableAttr::CreateImplicit(Context));
    }
  }

  if (LangOpts.OpenMP)
    OpenMP().ActOnFinishedFunctionDefinitionInOpenMPAssumeScope(NewFD);

  // Semantic checking for this function declaration (in isolation).

  if (getLangOpts().CPlusPlus) {
    // C++-specific checks.
    if (CXXConstructorDecl *Constructor = dyn_cast<CXXConstructorDecl>(NewFD)) {
      CheckConstructor(Constructor);
    } else if (CXXDestructorDecl *Destructor =
                   dyn_cast<CXXDestructorDecl>(NewFD)) {
      // We check here for invalid destructor names.
      // If we have a friend destructor declaration that is dependent, we can't
      // diagnose right away because cases like this are still valid:
      // template <class T> struct A { friend T::X::~Y(); };
      // struct B { struct Y { ~Y(); }; using X = Y; };
      // template struct A<B>;
      if (NewFD->getFriendObjectKind() == Decl::FriendObjectKind::FOK_None ||
          !Destructor->getFunctionObjectParameterType()->isDependentType()) {
        CXXRecordDecl *Record = Destructor->getParent();
        QualType ClassType = Context.getTypeDeclType(Record);

        DeclarationName Name = Context.DeclarationNames.getCXXDestructorName(
            Context.getCanonicalType(ClassType));
        if (NewFD->getDeclName() != Name) {
          Diag(NewFD->getLocation(), diag::err_destructor_name);
          NewFD->setInvalidDecl();
          return Redeclaration;
        }
      }
    } else if (auto *Guide = dyn_cast<CXXDeductionGuideDecl>(NewFD)) {
      if (auto *TD = Guide->getDescribedFunctionTemplate())
        CheckDeductionGuideTemplate(TD);

      // A deduction guide is not on the list of entities that can be
      // explicitly specialized.
      if (Guide->getTemplateSpecializationKind() == TSK_ExplicitSpecialization)
        Diag(Guide->getBeginLoc(), diag::err_deduction_guide_specialized)
            << /*explicit specialization*/ 1;
    }

    // Find any virtual functions that this function overrides.
    if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(NewFD)) {
      if (!Method->isFunctionTemplateSpecialization() &&
          !Method->getDescribedFunctionTemplate() &&
          Method->isCanonicalDecl()) {
        AddOverriddenMethods(Method->getParent(), Method);
      }
      if (Method->isVirtual() && NewFD->getTrailingRequiresClause())
        // C++2a [class.virtual]p6
        // A virtual method shall not have a requires-clause.
        Diag(NewFD->getTrailingRequiresClause()->getBeginLoc(),
             diag::err_constrained_virtual_method);

      if (Method->isStatic())
        checkThisInStaticMemberFunctionType(Method);
    }

    if (CXXConversionDecl *Conversion = dyn_cast<CXXConversionDecl>(NewFD))
      ActOnConversionDeclarator(Conversion);

    // Extra checking for C++ overloaded operators (C++ [over.oper]).
    if (NewFD->isOverloadedOperator() &&
        CheckOverloadedOperatorDeclaration(NewFD)) {
      NewFD->setInvalidDecl();
      return Redeclaration;
    }

    // Extra checking for C++0x literal operators (C++0x [over.literal]).
    if (NewFD->getLiteralIdentifier() &&
        CheckLiteralOperatorDeclaration(NewFD)) {
      NewFD->setInvalidDecl();
      return Redeclaration;
    }

    // In C++, check default arguments now that we have merged decls. Unless
    // the lexical context is the class, because in this case this is done
    // during delayed parsing anyway.
    if (!CurContext->isRecord())
      CheckCXXDefaultArguments(NewFD);

    // If this function is declared as being extern "C", then check to see if
    // the function returns a UDT (class, struct, or union type) that is not C
    // compatible, and if it does, warn the user.
    // But, issue any diagnostic on the first declaration only.
    if (Previous.empty() && NewFD->isExternC()) {
      QualType R = NewFD->getReturnType();
      if (R->isIncompleteType() && !R->isVoidType())
        Diag(NewFD->getLocation(), diag::warn_return_value_udt_incomplete)
            << NewFD << R;
      else if (!R.isPODType(Context) && !R->isVoidType() &&
               !R->isObjCObjectPointerType())
        Diag(NewFD->getLocation(), diag::warn_return_value_udt) << NewFD << R;
    }

    // C++1z [dcl.fct]p6:
    //   [...] whether the function has a non-throwing exception-specification
    //   [is] part of the function type
    //
    // This results in an ABI break between C++14 and C++17 for functions whose
    // declared type includes an exception-specification in a parameter or
    // return type. (Exception specifications on the function itself are OK in
    // most cases, and exception specifications are not permitted in most other
    // contexts where they could make it into a mangling.)
    if (!getLangOpts().CPlusPlus17 && !NewFD->getPrimaryTemplate()) {
      auto HasNoexcept = [&](QualType T) -> bool {
        // Strip off declarator chunks that could be between us and a function
        // type. We don't need to look far, exception specifications are very
        // restricted prior to C++17.
        if (auto *RT = T->getAs<ReferenceType>())
          T = RT->getPointeeType();
        else if (T->isAnyPointerType())
          T = T->getPointeeType();
        else if (auto *MPT = T->getAs<MemberPointerType>())
          T = MPT->getPointeeType();
        if (auto *FPT = T->getAs<FunctionProtoType>())
          if (FPT->isNothrow())
            return true;
        return false;
      };

      auto *FPT = NewFD->getType()->castAs<FunctionProtoType>();
      bool AnyNoexcept = HasNoexcept(FPT->getReturnType());
      for (QualType T : FPT->param_types())
        AnyNoexcept |= HasNoexcept(T);
      if (AnyNoexcept)
        Diag(NewFD->getLocation(),
             diag::warn_cxx17_compat_exception_spec_in_signature)
            << NewFD;
    }

    if (!Redeclaration && LangOpts.CUDA)
      CUDA().checkTargetOverload(NewFD, Previous);
  }

  // Check if the function definition uses any AArch64 SME features without
  // having the '+sme' feature enabled and warn user if sme locally streaming
  // function returns or uses arguments with VL-based types.
  if (DeclIsDefn) {
    const auto *Attr = NewFD->getAttr<ArmNewAttr>();
    bool UsesSM = NewFD->hasAttr<ArmLocallyStreamingAttr>();
    bool UsesZA = Attr && Attr->isNewZA();
    bool UsesZT0 = Attr && Attr->isNewZT0();

    if (NewFD->hasAttr<ArmLocallyStreamingAttr>()) {
      if (NewFD->getReturnType()->isSizelessVectorType())
        Diag(NewFD->getLocation(),
             diag::warn_sme_locally_streaming_has_vl_args_returns)
            << /*IsArg=*/false;
      if (llvm::any_of(NewFD->parameters(), [](ParmVarDecl *P) {
            return P->getOriginalType()->isSizelessVectorType();
          }))
        Diag(NewFD->getLocation(),
             diag::warn_sme_locally_streaming_has_vl_args_returns)
            << /*IsArg=*/true;
    }
    if (const auto *FPT = NewFD->getType()->getAs<FunctionProtoType>()) {
      FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
      UsesSM |=
          EPI.AArch64SMEAttributes & FunctionType::SME_PStateSMEnabledMask;
      UsesZA |= FunctionType::getArmZAState(EPI.AArch64SMEAttributes) !=
                FunctionType::ARM_None;
      UsesZT0 |= FunctionType::getArmZT0State(EPI.AArch64SMEAttributes) !=
                 FunctionType::ARM_None;
    }

    if (UsesSM || UsesZA) {
      llvm::StringMap<bool> FeatureMap;
      Context.getFunctionFeatureMap(FeatureMap, NewFD);
      if (!FeatureMap.contains("sme")) {
        if (UsesSM)
          Diag(NewFD->getLocation(),
               diag::err_sme_definition_using_sm_in_non_sme_target);
        else
          Diag(NewFD->getLocation(),
               diag::err_sme_definition_using_za_in_non_sme_target);
      }
    }
    if (UsesZT0) {
      llvm::StringMap<bool> FeatureMap;
      Context.getFunctionFeatureMap(FeatureMap, NewFD);
      if (!FeatureMap.contains("sme2")) {
        Diag(NewFD->getLocation(),
             diag::err_sme_definition_using_zt0_in_non_sme2_target);
      }
    }
  }

  return Redeclaration;
}

void Sema::CheckMain(FunctionDecl* FD, const DeclSpec& DS) {
  // C++11 [basic.start.main]p3:
  //   A program that [...] declares main to be inline, static or
  //   constexpr is ill-formed.
  // C11 6.7.4p4:  In a hosted environment, no function specifier(s) shall
  //   appear in a declaration of main.
  // static main is not an error under C99, but we should warn about it.
  // We accept _Noreturn main as an extension.
  if (FD->getStorageClass() == SC_Static)
    Diag(DS.getStorageClassSpecLoc(), getLangOpts().CPlusPlus
         ? diag::err_static_main : diag::warn_static_main)
      << FixItHint::CreateRemoval(DS.getStorageClassSpecLoc());
  if (FD->isInlineSpecified())
    Diag(DS.getInlineSpecLoc(), diag::err_inline_main)
      << FixItHint::CreateRemoval(DS.getInlineSpecLoc());
  if (DS.isNoreturnSpecified()) {
    SourceLocation NoreturnLoc = DS.getNoreturnSpecLoc();
    SourceRange NoreturnRange(NoreturnLoc, getLocForEndOfToken(NoreturnLoc));
    Diag(NoreturnLoc, diag::ext_noreturn_main);
    Diag(NoreturnLoc, diag::note_main_remove_noreturn)
      << FixItHint::CreateRemoval(NoreturnRange);
  }
  if (FD->isConstexpr()) {
    Diag(DS.getConstexprSpecLoc(), diag::err_constexpr_main)
        << FD->isConsteval()
        << FixItHint::CreateRemoval(DS.getConstexprSpecLoc());
    FD->setConstexprKind(ConstexprSpecKind::Unspecified);
  }

  if (getLangOpts().OpenCL) {
    Diag(FD->getLocation(), diag::err_opencl_no_main)
        << FD->hasAttr<OpenCLKernelAttr>();
    FD->setInvalidDecl();
    return;
  }

  // Functions named main in hlsl are default entries, but don't have specific
  // signatures they are required to conform to.
  if (getLangOpts().HLSL)
    return;

  QualType T = FD->getType();
  assert(T->isFunctionType() && "function decl is not of function type");
  const FunctionType* FT = T->castAs<FunctionType>();

  // Set default calling convention for main()
  if (FT->getCallConv() != CC_C) {
    FT = Context.adjustFunctionType(FT, FT->getExtInfo().withCallingConv(CC_C));
    FD->setType(QualType(FT, 0));
    T = Context.getCanonicalType(FD->getType());
  }

  if (getLangOpts().GNUMode && !getLangOpts().CPlusPlus) {
    // In C with GNU extensions we allow main() to have non-integer return
    // type, but we should warn about the extension, and we disable the
    // implicit-return-zero rule.

    // GCC in C mode accepts qualified 'int'.
    if (Context.hasSameUnqualifiedType(FT->getReturnType(), Context.IntTy))
      FD->setHasImplicitReturnZero(true);
    else {
      Diag(FD->getTypeSpecStartLoc(), diag::ext_main_returns_nonint);
      SourceRange RTRange = FD->getReturnTypeSourceRange();
      if (RTRange.isValid())
        Diag(RTRange.getBegin(), diag::note_main_change_return_type)
            << FixItHint::CreateReplacement(RTRange, "int");
    }
  } else {
    // In C and C++, main magically returns 0 if you fall off the end;
    // set the flag which tells us that.
    // This is C++ [basic.start.main]p5 and C99 5.1.2.2.3.

    // All the standards say that main() should return 'int'.
    if (Context.hasSameType(FT->getReturnType(), Context.IntTy))
      FD->setHasImplicitReturnZero(true);
    else {
      // Otherwise, this is just a flat-out error.
      SourceRange RTRange = FD->getReturnTypeSourceRange();
      Diag(FD->getTypeSpecStartLoc(), diag::err_main_returns_nonint)
          << (RTRange.isValid() ? FixItHint::CreateReplacement(RTRange, "int")
                                : FixItHint());
      FD->setInvalidDecl(true);
    }
  }

  // Treat protoless main() as nullary.
  if (isa<FunctionNoProtoType>(FT)) return;

  const FunctionProtoType* FTP = cast<const FunctionProtoType>(FT);
  unsigned nparams = FTP->getNumParams();
  assert(FD->getNumParams() == nparams);

  bool HasExtraParameters = (nparams > 3);

  if (FTP->isVariadic()) {
    Diag(FD->getLocation(), diag::ext_variadic_main);
    // FIXME: if we had information about the location of the ellipsis, we
    // could add a FixIt hint to remove it as a parameter.
  }

  // Darwin passes an undocumented fourth argument of type char**.  If
  // other platforms start sprouting these, the logic below will start
  // getting shifty.
  if (nparams == 4 && Context.getTargetInfo().getTriple().isOSDarwin())
    HasExtraParameters = false;

  if (HasExtraParameters) {
    Diag(FD->getLocation(), diag::err_main_surplus_args) << nparams;
    FD->setInvalidDecl(true);
    nparams = 3;
  }

  // FIXME: a lot of the following diagnostics would be improved
  // if we had some location information about types.

  QualType CharPP =
    Context.getPointerType(Context.getPointerType(Context.CharTy));
  QualType Expected[] = { Context.IntTy, CharPP, CharPP, CharPP };

  for (unsigned i = 0; i < nparams; ++i) {
    QualType AT = FTP->getParamType(i);

    bool mismatch = true;

    if (Context.hasSameUnqualifiedType(AT, Expected[i]))
      mismatch = false;
    else if (Expected[i] == CharPP) {
      // As an extension, the following forms are okay:
      //   char const **
      //   char const * const *
      //   char * const *

      QualifierCollector qs;
      const PointerType* PT;
      if ((PT = qs.strip(AT)->getAs<PointerType>()) &&
          (PT = qs.strip(PT->getPointeeType())->getAs<PointerType>()) &&
          Context.hasSameType(QualType(qs.strip(PT->getPointeeType()), 0),
                              Context.CharTy)) {
        qs.removeConst();
        mismatch = !qs.empty();
      }
    }

    if (mismatch) {
      Diag(FD->getLocation(), diag::err_main_arg_wrong) << i << Expected[i];
      // TODO: suggest replacing given type with expected type
      FD->setInvalidDecl(true);
    }
  }

  if (nparams == 1 && !FD->isInvalidDecl()) {
    Diag(FD->getLocation(), diag::warn_main_one_arg);
  }

  if (!FD->isInvalidDecl() && FD->getDescribedFunctionTemplate()) {
    Diag(FD->getLocation(), diag::err_mainlike_template_decl) << FD;
    FD->setInvalidDecl();
  }
}

static bool isDefaultStdCall(FunctionDecl *FD, Sema &S) {

  // Default calling convention for main and wmain is __cdecl
  if (FD->getName() == "main" || FD->getName() == "wmain")
    return false;

  // Default calling convention for MinGW is __cdecl
  const llvm::Triple &T = S.Context.getTargetInfo().getTriple();
  if (T.isWindowsGNUEnvironment())
    return false;

  // Default calling convention for WinMain, wWinMain and DllMain
  // is __stdcall on 32 bit Windows
  if (T.isOSWindows() && T.getArch() == llvm::Triple::x86)
    return true;

  return false;
}

void Sema::CheckMSVCRTEntryPoint(FunctionDecl *FD) {
  QualType T = FD->getType();
  assert(T->isFunctionType() && "function decl is not of function type");
  const FunctionType *FT = T->castAs<FunctionType>();

  // Set an implicit return of 'zero' if the function can return some integral,
  // enumeration, pointer or nullptr type.
  if (FT->getReturnType()->isIntegralOrEnumerationType() ||
      FT->getReturnType()->isAnyPointerType() ||
      FT->getReturnType()->isNullPtrType())
    // DllMain is exempt because a return value of zero means it failed.
    if (FD->getName() != "DllMain")
      FD->setHasImplicitReturnZero(true);

  // Explicitly specified calling conventions are applied to MSVC entry points
  if (!hasExplicitCallingConv(T)) {
    if (isDefaultStdCall(FD, *this)) {
      if (FT->getCallConv() != CC_X86StdCall) {
        FT = Context.adjustFunctionType(
            FT, FT->getExtInfo().withCallingConv(CC_X86StdCall));
        FD->setType(QualType(FT, 0));
      }
    } else if (FT->getCallConv() != CC_C) {
      FT = Context.adjustFunctionType(FT,
                                      FT->getExtInfo().withCallingConv(CC_C));
      FD->setType(QualType(FT, 0));
    }
  }

  if (!FD->isInvalidDecl() && FD->getDescribedFunctionTemplate()) {
    Diag(FD->getLocation(), diag::err_mainlike_template_decl) << FD;
    FD->setInvalidDecl();
  }
}

bool Sema::CheckForConstantInitializer(Expr *Init, unsigned DiagID) {
  // FIXME: Need strict checking.  In C89, we need to check for
  // any assignment, increment, decrement, function-calls, or
  // commas outside of a sizeof.  In C99, it's the same list,
  // except that the aforementioned are allowed in unevaluated
  // expressions.  Everything else falls under the
  // "may accept other forms of constant expressions" exception.
  //
  // Regular C++ code will not end up here (exceptions: language extensions,
  // OpenCL C++ etc), so the constant expression rules there don't matter.
  if (Init->isValueDependent()) {
    assert(Init->containsErrors() &&
           "Dependent code should only occur in error-recovery path.");
    return true;
  }
  const Expr *Culprit;
  if (Init->isConstantInitializer(Context, false, &Culprit))
    return false;
  Diag(Culprit->getExprLoc(), DiagID) << Culprit->getSourceRange();
  return true;
}

namespace {
  // Visits an initialization expression to see if OrigDecl is evaluated in
  // its own initialization and throws a warning if it does.
  class SelfReferenceChecker
      : public EvaluatedExprVisitor<SelfReferenceChecker> {
    Sema &S;
    Decl *OrigDecl;
    bool isRecordType;
    bool isPODType;
    bool isReferenceType;

    bool isInitList;
    llvm::SmallVector<unsigned, 4> InitFieldIndex;

  public:
    typedef EvaluatedExprVisitor<SelfReferenceChecker> Inherited;

    SelfReferenceChecker(Sema &S, Decl *OrigDecl) : Inherited(S.Context),
                                                    S(S), OrigDecl(OrigDecl) {
      isPODType = false;
      isRecordType = false;
      isReferenceType = false;
      isInitList = false;
      if (ValueDecl *VD = dyn_cast<ValueDecl>(OrigDecl)) {
        isPODType = VD->getType().isPODType(S.Context);
        isRecordType = VD->getType()->isRecordType();
        isReferenceType = VD->getType()->isReferenceType();
      }
    }

    // For most expressions, just call the visitor.  For initializer lists,
    // track the index of the field being initialized since fields are
    // initialized in order allowing use of previously initialized fields.
    void CheckExpr(Expr *E) {
      InitListExpr *InitList = dyn_cast<InitListExpr>(E);
      if (!InitList) {
        Visit(E);
        return;
      }

      // Track and increment the index here.
      isInitList = true;
      InitFieldIndex.push_back(0);
      for (auto *Child : InitList->children()) {
        CheckExpr(cast<Expr>(Child));
        ++InitFieldIndex.back();
      }
      InitFieldIndex.pop_back();
    }

    // Returns true if MemberExpr is checked and no further checking is needed.
    // Returns false if additional checking is required.
    bool CheckInitListMemberExpr(MemberExpr *E, bool CheckReference) {
      llvm::SmallVector<FieldDecl*, 4> Fields;
      Expr *Base = E;
      bool ReferenceField = false;

      // Get the field members used.
      while (MemberExpr *ME = dyn_cast<MemberExpr>(Base)) {
        FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
        if (!FD)
          return false;
        Fields.push_back(FD);
        if (FD->getType()->isReferenceType())
          ReferenceField = true;
        Base = ME->getBase()->IgnoreParenImpCasts();
      }

      // Keep checking only if the base Decl is the same.
      DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Base);
      if (!DRE || DRE->getDecl() != OrigDecl)
        return false;

      // A reference field can be bound to an unininitialized field.
      if (CheckReference && !ReferenceField)
        return true;

      // Convert FieldDecls to their index number.
      llvm::SmallVector<unsigned, 4> UsedFieldIndex;
      for (const FieldDecl *I : llvm::reverse(Fields))
        UsedFieldIndex.push_back(I->getFieldIndex());

      // See if a warning is needed by checking the first difference in index
      // numbers.  If field being used has index less than the field being
      // initialized, then the use is safe.
      for (auto UsedIter = UsedFieldIndex.begin(),
                UsedEnd = UsedFieldIndex.end(),
                OrigIter = InitFieldIndex.begin(),
                OrigEnd = InitFieldIndex.end();
           UsedIter != UsedEnd && OrigIter != OrigEnd; ++UsedIter, ++OrigIter) {
        if (*UsedIter < *OrigIter)
          return true;
        if (*UsedIter > *OrigIter)
          break;
      }

      // TODO: Add a different warning which will print the field names.
      HandleDeclRefExpr(DRE);
      return true;
    }

    // For most expressions, the cast is directly above the DeclRefExpr.
    // For conditional operators, the cast can be outside the conditional
    // operator if both expressions are DeclRefExpr's.
    void HandleValue(Expr *E) {
      E = E->IgnoreParens();
      if (DeclRefExpr* DRE = dyn_cast<DeclRefExpr>(E)) {
        HandleDeclRefExpr(DRE);
        return;
      }

      if (ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
        Visit(CO->getCond());
        HandleValue(CO->getTrueExpr());
        HandleValue(CO->getFalseExpr());
        return;
      }

      if (BinaryConditionalOperator *BCO =
              dyn_cast<BinaryConditionalOperator>(E)) {
        Visit(BCO->getCond());
        HandleValue(BCO->getFalseExpr());
        return;
      }

      if (OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(E)) {
        if (Expr *SE = OVE->getSourceExpr())
          HandleValue(SE);
        return;
      }

      if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
        if (BO->getOpcode() == BO_Comma) {
          Visit(BO->getLHS());
          HandleValue(BO->getRHS());
          return;
        }
      }

      if (isa<MemberExpr>(E)) {
        if (isInitList) {
          if (CheckInitListMemberExpr(cast<MemberExpr>(E),
                                      false /*CheckReference*/))
            return;
        }

        Expr *Base = E->IgnoreParenImpCasts();
        while (MemberExpr *ME = dyn_cast<MemberExpr>(Base)) {
          // Check for static member variables and don't warn on them.
          if (!isa<FieldDecl>(ME->getMemberDecl()))
            return;
          Base = ME->getBase()->IgnoreParenImpCasts();
        }
        if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Base))
          HandleDeclRefExpr(DRE);
        return;
      }

      Visit(E);
    }

    // Reference types not handled in HandleValue are handled here since all
    // uses of references are bad, not just r-value uses.
    void VisitDeclRefExpr(DeclRefExpr *E) {
      if (isReferenceType)
        HandleDeclRefExpr(E);
    }

    void VisitImplicitCastExpr(ImplicitCastExpr *E) {
      if (E->getCastKind() == CK_LValueToRValue) {
        HandleValue(E->getSubExpr());
        return;
      }

      Inherited::VisitImplicitCastExpr(E);
    }

    void VisitMemberExpr(MemberExpr *E) {
      if (isInitList) {
        if (CheckInitListMemberExpr(E, true /*CheckReference*/))
          return;
      }

      // Don't warn on arrays since they can be treated as pointers.
      if (E->getType()->canDecayToPointerType()) return;

      // Warn when a non-static method call is followed by non-static member
      // field accesses, which is followed by a DeclRefExpr.
      CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(E->getMemberDecl());
      bool Warn = (MD && !MD->isStatic());
      Expr *Base = E->getBase()->IgnoreParenImpCasts();
      while (MemberExpr *ME = dyn_cast<MemberExpr>(Base)) {
        if (!isa<FieldDecl>(ME->getMemberDecl()))
          Warn = false;
        Base = ME->getBase()->IgnoreParenImpCasts();
      }

      if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Base)) {
        if (Warn)
          HandleDeclRefExpr(DRE);
        return;
      }

      // The base of a MemberExpr is not a MemberExpr or a DeclRefExpr.
      // Visit that expression.
      Visit(Base);
    }

    void VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
      Expr *Callee = E->getCallee();

      if (isa<UnresolvedLookupExpr>(Callee))
        return Inherited::VisitCXXOperatorCallExpr(E);

      Visit(Callee);
      for (auto Arg: E->arguments())
        HandleValue(Arg->IgnoreParenImpCasts());
    }

    void VisitUnaryOperator(UnaryOperator *E) {
      // For POD record types, addresses of its own members are well-defined.
      if (E->getOpcode() == UO_AddrOf && isRecordType &&
          isa<MemberExpr>(E->getSubExpr()->IgnoreParens())) {
        if (!isPODType)
          HandleValue(E->getSubExpr());
        return;
      }

      if (E->isIncrementDecrementOp()) {
        HandleValue(E->getSubExpr());
        return;
      }

      Inherited::VisitUnaryOperator(E);
    }

    void VisitObjCMessageExpr(ObjCMessageExpr *E) {}

    void VisitCXXConstructExpr(CXXConstructExpr *E) {
      if (E->getConstructor()->isCopyConstructor()) {
        Expr *ArgExpr = E->getArg(0);
        if (InitListExpr *ILE = dyn_cast<InitListExpr>(ArgExpr))
          if (ILE->getNumInits() == 1)
            ArgExpr = ILE->getInit(0);
        if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(ArgExpr))
          if (ICE->getCastKind() == CK_NoOp)
            ArgExpr = ICE->getSubExpr();
        HandleValue(ArgExpr);
        return;
      }
      Inherited::VisitCXXConstructExpr(E);
    }

    void VisitCallExpr(CallExpr *E) {
      // Treat std::move as a use.
      if (E->isCallToStdMove()) {
        HandleValue(E->getArg(0));
        return;
      }

      Inherited::VisitCallExpr(E);
    }

    void VisitBinaryOperator(BinaryOperator *E) {
      if (E->isCompoundAssignmentOp()) {
        HandleValue(E->getLHS());
        Visit(E->getRHS());
        return;
      }

      Inherited::VisitBinaryOperator(E);
    }

    // A custom visitor for BinaryConditionalOperator is needed because the
    // regular visitor would check the condition and true expression separately
    // but both point to the same place giving duplicate diagnostics.
    void VisitBinaryConditionalOperator(BinaryConditionalOperator *E) {
      Visit(E->getCond());
      Visit(E->getFalseExpr());
    }

    void HandleDeclRefExpr(DeclRefExpr *DRE) {
      Decl* ReferenceDecl = DRE->getDecl();
      if (OrigDecl != ReferenceDecl) return;
      unsigned diag;
      if (isReferenceType) {
        diag = diag::warn_uninit_self_reference_in_reference_init;
      } else if (cast<VarDecl>(OrigDecl)->isStaticLocal()) {
        diag = diag::warn_static_self_reference_in_init;
      } else if (isa<TranslationUnitDecl>(OrigDecl->getDeclContext()) ||
                 isa<NamespaceDecl>(OrigDecl->getDeclContext()) ||
                 DRE->getDecl()->getType()->isRecordType()) {
        diag = diag::warn_uninit_self_reference_in_init;
      } else {
        // Local variables will be handled by the CFG analysis.
        return;
      }

      S.DiagRuntimeBehavior(DRE->getBeginLoc(), DRE,
                            S.PDiag(diag)
                                << DRE->getDecl() << OrigDecl->getLocation()
                                << DRE->getSourceRange());
    }
  };

  /// CheckSelfReference - Warns if OrigDecl is used in expression E.
  static void CheckSelfReference(Sema &S, Decl* OrigDecl, Expr *E,
                                 bool DirectInit) {
    // Parameters arguments are occassionially constructed with itself,
    // for instance, in recursive functions.  Skip them.
    if (isa<ParmVarDecl>(OrigDecl))
      return;

    E = E->IgnoreParens();

    // Skip checking T a = a where T is not a record or reference type.
    // Doing so is a way to silence uninitialized warnings.
    if (!DirectInit && !cast<VarDecl>(OrigDecl)->getType()->isRecordType())
      if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E))
        if (ICE->getCastKind() == CK_LValueToRValue)
          if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(ICE->getSubExpr()))
            if (DRE->getDecl() == OrigDecl)
              return;

    SelfReferenceChecker(S, OrigDecl).CheckExpr(E);
  }
} // end anonymous namespace

namespace {
  // Simple wrapper to add the name of a variable or (if no variable is
  // available) a DeclarationName into a diagnostic.
  struct VarDeclOrName {
    VarDecl *VDecl;
    DeclarationName Name;

    friend const Sema::SemaDiagnosticBuilder &
    operator<<(const Sema::SemaDiagnosticBuilder &Diag, VarDeclOrName VN) {
      return VN.VDecl ? Diag << VN.VDecl : Diag << VN.Name;
    }
  };
} // end anonymous namespace

QualType Sema::deduceVarTypeFromInitializer(VarDecl *VDecl,
                                            DeclarationName Name, QualType Type,
                                            TypeSourceInfo *TSI,
                                            SourceRange Range, bool DirectInit,
                                            Expr *Init) {
  bool IsInitCapture = !VDecl;
  assert((!VDecl || !VDecl->isInitCapture()) &&
         "init captures are expected to be deduced prior to initialization");

  VarDeclOrName VN{VDecl, Name};

  DeducedType *Deduced = Type->getContainedDeducedType();
  assert(Deduced && "deduceVarTypeFromInitializer for non-deduced type");

  // Diagnose auto array declarations in C23, unless it's a supported extension.
  if (getLangOpts().C23 && Type->isArrayType() &&
      !isa_and_present<StringLiteral, InitListExpr>(Init)) {
      Diag(Range.getBegin(), diag::err_auto_not_allowed)
          << (int)Deduced->getContainedAutoType()->getKeyword()
          << /*in array decl*/ 23 << Range;
    return QualType();
  }

  // C++11 [dcl.spec.auto]p3
  if (!Init) {
    assert(VDecl && "no init for init capture deduction?");

    // Except for class argument deduction, and then for an initializing
    // declaration only, i.e. no static at class scope or extern.
    if (!isa<DeducedTemplateSpecializationType>(Deduced) ||
        VDecl->hasExternalStorage() ||
        VDecl->isStaticDataMember()) {
      Diag(VDecl->getLocation(), diag::err_auto_var_requires_init)
        << VDecl->getDeclName() << Type;
      return QualType();
    }
  }

  ArrayRef<Expr*> DeduceInits;
  if (Init)
    DeduceInits = Init;

  auto *PL = dyn_cast_if_present<ParenListExpr>(Init);
  if (DirectInit && PL)
    DeduceInits = PL->exprs();

  if (isa<DeducedTemplateSpecializationType>(Deduced)) {
    assert(VDecl && "non-auto type for init capture deduction?");
    InitializedEntity Entity = InitializedEntity::InitializeVariable(VDecl);
    InitializationKind Kind = InitializationKind::CreateForInit(
        VDecl->getLocation(), DirectInit, Init);
    // FIXME: Initialization should not be taking a mutable list of inits.
    SmallVector<Expr*, 8> InitsCopy(DeduceInits.begin(), DeduceInits.end());
    return DeduceTemplateSpecializationFromInitializer(TSI, Entity, Kind,
                                                       InitsCopy);
  }

  if (DirectInit) {
    if (auto *IL = dyn_cast<InitListExpr>(Init))
      DeduceInits = IL->inits();
  }

  // Deduction only works if we have exactly one source expression.
  if (DeduceInits.empty()) {
    // It isn't possible to write this directly, but it is possible to
    // end up in this situation with "auto x(some_pack...);"
    Diag(Init->getBeginLoc(), IsInitCapture
                                  ? diag::err_init_capture_no_expression
                                  : diag::err_auto_var_init_no_expression)
        << VN << Type << Range;
    return QualType();
  }

  if (DeduceInits.size() > 1) {
    Diag(DeduceInits[1]->getBeginLoc(),
         IsInitCapture ? diag::err_init_capture_multiple_expressions
                       : diag::err_auto_var_init_multiple_expressions)
        << VN << Type << Range;
    return QualType();
  }

  Expr *DeduceInit = DeduceInits[0];
  if (DirectInit && isa<InitListExpr>(DeduceInit)) {
    Diag(Init->getBeginLoc(), IsInitCapture
                                  ? diag::err_init_capture_paren_braces
                                  : diag::err_auto_var_init_paren_braces)
        << isa<InitListExpr>(Init) << VN << Type << Range;
    return QualType();
  }

  // Expressions default to 'id' when we're in a debugger.
  bool DefaultedAnyToId = false;
  if (getLangOpts().DebuggerCastResultToId &&
      Init->getType() == Context.UnknownAnyTy && !IsInitCapture) {
    ExprResult Result = forceUnknownAnyToType(Init, Context.getObjCIdType());
    if (Result.isInvalid()) {
      return QualType();
    }
    Init = Result.get();
    DefaultedAnyToId = true;
  }

  // C++ [dcl.decomp]p1:
  //   If the assignment-expression [...] has array type A and no ref-qualifier
  //   is present, e has type cv A
  if (VDecl && isa<DecompositionDecl>(VDecl) &&
      Context.hasSameUnqualifiedType(Type, Context.getAutoDeductType()) &&
      DeduceInit->getType()->isConstantArrayType())
    return Context.getQualifiedType(DeduceInit->getType(),
                                    Type.getQualifiers());

  QualType DeducedType;
  TemplateDeductionInfo Info(DeduceInit->getExprLoc());
  TemplateDeductionResult Result =
      DeduceAutoType(TSI->getTypeLoc(), DeduceInit, DeducedType, Info);
  if (Result != TemplateDeductionResult::Success &&
      Result != TemplateDeductionResult::AlreadyDiagnosed) {
    if (!IsInitCapture)
      DiagnoseAutoDeductionFailure(VDecl, DeduceInit);
    else if (isa<InitListExpr>(Init))
      Diag(Range.getBegin(),
           diag::err_init_capture_deduction_failure_from_init_list)
          << VN
          << (DeduceInit->getType().isNull() ? TSI->getType()
                                             : DeduceInit->getType())
          << DeduceInit->getSourceRange();
    else
      Diag(Range.getBegin(), diag::err_init_capture_deduction_failure)
          << VN << TSI->getType()
          << (DeduceInit->getType().isNull() ? TSI->getType()
                                             : DeduceInit->getType())
          << DeduceInit->getSourceRange();
  }

  // Warn if we deduced 'id'. 'auto' usually implies type-safety, but using
  // 'id' instead of a specific object type prevents most of our usual
  // checks.
  // We only want to warn outside of template instantiations, though:
  // inside a template, the 'id' could have come from a parameter.
  if (!inTemplateInstantiation() && !DefaultedAnyToId && !IsInitCapture &&
      !DeducedType.isNull() && DeducedType->isObjCIdType()) {
    SourceLocation Loc = TSI->getTypeLoc().getBeginLoc();
    Diag(Loc, diag::warn_auto_var_is_id) << VN << Range;
  }

  return DeducedType;
}

bool Sema::DeduceVariableDeclarationType(VarDecl *VDecl, bool DirectInit,
                                         Expr *Init) {
  assert(!Init || !Init->containsErrors());
  QualType DeducedType = deduceVarTypeFromInitializer(
      VDecl, VDecl->getDeclName(), VDecl->getType(), VDecl->getTypeSourceInfo(),
      VDecl->getSourceRange(), DirectInit, Init);
  if (DeducedType.isNull()) {
    VDecl->setInvalidDecl();
    return true;
  }

  VDecl->setType(DeducedType);
  assert(VDecl->isLinkageValid());

  // In ARC, infer lifetime.
  if (getLangOpts().ObjCAutoRefCount && ObjC().inferObjCARCLifetime(VDecl))
    VDecl->setInvalidDecl();

  if (getLangOpts().OpenCL)
    deduceOpenCLAddressSpace(VDecl);

  // If this is a redeclaration, check that the type we just deduced matches
  // the previously declared type.
  if (VarDecl *Old = VDecl->getPreviousDecl()) {
    // We never need to merge the type, because we cannot form an incomplete
    // array of auto, nor deduce such a type.
    MergeVarDeclTypes(VDecl, Old, /*MergeTypeWithPrevious*/ false);
  }

  // Check the deduced type is valid for a variable declaration.
  CheckVariableDeclarationType(VDecl);
  return VDecl->isInvalidDecl();
}

void Sema::checkNonTrivialCUnionInInitializer(const Expr *Init,
                                              SourceLocation Loc) {
  if (auto *EWC = dyn_cast<ExprWithCleanups>(Init))
    Init = EWC->getSubExpr();

  if (auto *CE = dyn_cast<ConstantExpr>(Init))
    Init = CE->getSubExpr();

  QualType InitType = Init->getType();
  assert((InitType.hasNonTrivialToPrimitiveDefaultInitializeCUnion() ||
          InitType.hasNonTrivialToPrimitiveCopyCUnion()) &&
         "shouldn't be called if type doesn't have a non-trivial C struct");
  if (auto *ILE = dyn_cast<InitListExpr>(Init)) {
    for (auto *I : ILE->inits()) {
      if (!I->getType().hasNonTrivialToPrimitiveDefaultInitializeCUnion() &&
          !I->getType().hasNonTrivialToPrimitiveCopyCUnion())
        continue;
      SourceLocation SL = I->getExprLoc();
      checkNonTrivialCUnionInInitializer(I, SL.isValid() ? SL : Loc);
    }
    return;
  }

  if (isa<ImplicitValueInitExpr>(Init)) {
    if (InitType.hasNonTrivialToPrimitiveDefaultInitializeCUnion())
      checkNonTrivialCUnion(InitType, Loc, NTCUC_DefaultInitializedObject,
                            NTCUK_Init);
  } else {
    // Assume all other explicit initializers involving copying some existing
    // object.
    // TODO: ignore any explicit initializers where we can guarantee
    // copy-elision.
    if (InitType.hasNonTrivialToPrimitiveCopyCUnion())
      checkNonTrivialCUnion(InitType, Loc, NTCUC_CopyInit, NTCUK_Copy);
  }
}

namespace {

bool shouldIgnoreForRecordTriviality(const FieldDecl *FD) {
  // Ignore unavailable fields. A field can be marked as unavailable explicitly
  // in the source code or implicitly by the compiler if it is in a union
  // defined in a system header and has non-trivial ObjC ownership
  // qualifications. We don't want those fields to participate in determining
  // whether the containing union is non-trivial.
  return FD->hasAttr<UnavailableAttr>();
}

struct DiagNonTrivalCUnionDefaultInitializeVisitor
    : DefaultInitializedTypeVisitor<DiagNonTrivalCUnionDefaultInitializeVisitor,
                                    void> {
  using Super =
      DefaultInitializedTypeVisitor<DiagNonTrivalCUnionDefaultInitializeVisitor,
                                    void>;

  DiagNonTrivalCUnionDefaultInitializeVisitor(
      QualType OrigTy, SourceLocation OrigLoc,
      Sema::NonTrivialCUnionContext UseContext, Sema &S)
      : OrigTy(OrigTy), OrigLoc(OrigLoc), UseContext(UseContext), S(S) {}

  void visitWithKind(QualType::PrimitiveDefaultInitializeKind PDIK, QualType QT,
                     const FieldDecl *FD, bool InNonTrivialUnion) {
    if (const auto *AT = S.Context.getAsArrayType(QT))
      return this->asDerived().visit(S.Context.getBaseElementType(AT), FD,
                                     InNonTrivialUnion);
    return Super::visitWithKind(PDIK, QT, FD, InNonTrivialUnion);
  }

  void visitARCStrong(QualType QT, const FieldDecl *FD,
                      bool InNonTrivialUnion) {
    if (InNonTrivialUnion)
      S.Diag(FD->getLocation(), diag::note_non_trivial_c_union)
          << 1 << 0 << QT << FD->getName();
  }

  void visitARCWeak(QualType QT, const FieldDecl *FD, bool InNonTrivialUnion) {
    if (InNonTrivialUnion)
      S.Diag(FD->getLocation(), diag::note_non_trivial_c_union)
          << 1 << 0 << QT << FD->getName();
  }

  void visitStruct(QualType QT, const FieldDecl *FD, bool InNonTrivialUnion) {
    const RecordDecl *RD = QT->castAs<RecordType>()->getDecl();
    if (RD->isUnion()) {
      if (OrigLoc.isValid()) {
        bool IsUnion = false;
        if (auto *OrigRD = OrigTy->getAsRecordDecl())
          IsUnion = OrigRD->isUnion();
        S.Diag(OrigLoc, diag::err_non_trivial_c_union_in_invalid_context)
            << 0 << OrigTy << IsUnion << UseContext;
        // Reset OrigLoc so that this diagnostic is emitted only once.
        OrigLoc = SourceLocation();
      }
      InNonTrivialUnion = true;
    }

    if (InNonTrivialUnion)
      S.Diag(RD->getLocation(), diag::note_non_trivial_c_union)
          << 0 << 0 << QT.getUnqualifiedType() << "";

    for (const FieldDecl *FD : RD->fields())
      if (!shouldIgnoreForRecordTriviality(FD))
        asDerived().visit(FD->getType(), FD, InNonTrivialUnion);
  }

  void visitTrivial(QualType QT, const FieldDecl *FD, bool InNonTrivialUnion) {}

  // The non-trivial C union type or the struct/union type that contains a
  // non-trivial C union.
  QualType OrigTy;
  SourceLocation OrigLoc;
  Sema::NonTrivialCUnionContext UseContext;
  Sema &S;
};

struct DiagNonTrivalCUnionDestructedTypeVisitor
    : DestructedTypeVisitor<DiagNonTrivalCUnionDestructedTypeVisitor, void> {
  using Super =
      DestructedTypeVisitor<DiagNonTrivalCUnionDestructedTypeVisitor, void>;

  DiagNonTrivalCUnionDestructedTypeVisitor(
      QualType OrigTy, SourceLocation OrigLoc,
      Sema::NonTrivialCUnionContext UseContext, Sema &S)
      : OrigTy(OrigTy), OrigLoc(OrigLoc), UseContext(UseContext), S(S) {}

  void visitWithKind(QualType::DestructionKind DK, QualType QT,
                     const FieldDecl *FD, bool InNonTrivialUnion) {
    if (const auto *AT = S.Context.getAsArrayType(QT))
      return this->asDerived().visit(S.Context.getBaseElementType(AT), FD,
                                     InNonTrivialUnion);
    return Super::visitWithKind(DK, QT, FD, InNonTrivialUnion);
  }

  void visitARCStrong(QualType QT, const FieldDecl *FD,
                      bool InNonTrivialUnion) {
    if (InNonTrivialUnion)
      S.Diag(FD->getLocation(), diag::note_non_trivial_c_union)
          << 1 << 1 << QT << FD->getName();
  }

  void visitARCWeak(QualType QT, const FieldDecl *FD, bool InNonTrivialUnion) {
    if (InNonTrivialUnion)
      S.Diag(FD->getLocation(), diag::note_non_trivial_c_union)
          << 1 << 1 << QT << FD->getName();
  }

  void visitStruct(QualType QT, const FieldDecl *FD, bool InNonTrivialUnion) {
    const RecordDecl *RD = QT->castAs<RecordType>()->getDecl();
    if (RD->isUnion()) {
      if (OrigLoc.isValid()) {
        bool IsUnion = false;
        if (auto *OrigRD = OrigTy->getAsRecordDecl())
          IsUnion = OrigRD->isUnion();
        S.Diag(OrigLoc, diag::err_non_trivial_c_union_in_invalid_context)
            << 1 << OrigTy << IsUnion << UseContext;
        // Reset OrigLoc so that this diagnostic is emitted only once.
        OrigLoc = SourceLocation();
      }
      InNonTrivialUnion = true;
    }

    if (InNonTrivialUnion)
      S.Diag(RD->getLocation(), diag::note_non_trivial_c_union)
          << 0 << 1 << QT.getUnqualifiedType() << "";

    for (const FieldDecl *FD : RD->fields())
      if (!shouldIgnoreForRecordTriviality(FD))
        asDerived().visit(FD->getType(), FD, InNonTrivialUnion);
  }

  void visitTrivial(QualType QT, const FieldDecl *FD, bool InNonTrivialUnion) {}
  void visitCXXDestructor(QualType QT, const FieldDecl *FD,
                          bool InNonTrivialUnion) {}

  // The non-trivial C union type or the struct/union type that contains a
  // non-trivial C union.
  QualType OrigTy;
  SourceLocation OrigLoc;
  Sema::NonTrivialCUnionContext UseContext;
  Sema &S;
};

struct DiagNonTrivalCUnionCopyVisitor
    : CopiedTypeVisitor<DiagNonTrivalCUnionCopyVisitor, false, void> {
  using Super = CopiedTypeVisitor<DiagNonTrivalCUnionCopyVisitor, false, void>;

  DiagNonTrivalCUnionCopyVisitor(QualType OrigTy, SourceLocation OrigLoc,
                                 Sema::NonTrivialCUnionContext UseContext,
                                 Sema &S)
      : OrigTy(OrigTy), OrigLoc(OrigLoc), UseContext(UseContext), S(S) {}

  void visitWithKind(QualType::PrimitiveCopyKind PCK, QualType QT,
                     const FieldDecl *FD, bool InNonTrivialUnion) {
    if (const auto *AT = S.Context.getAsArrayType(QT))
      return this->asDerived().visit(S.Context.getBaseElementType(AT), FD,
                                     InNonTrivialUnion);
    return Super::visitWithKind(PCK, QT, FD, InNonTrivialUnion);
  }

  void visitARCStrong(QualType QT, const FieldDecl *FD,
                      bool InNonTrivialUnion) {
    if (InNonTrivialUnion)
      S.Diag(FD->getLocation(), diag::note_non_trivial_c_union)
          << 1 << 2 << QT << FD->getName();
  }

  void visitARCWeak(QualType QT, const FieldDecl *FD, bool InNonTrivialUnion) {
    if (InNonTrivialUnion)
      S.Diag(FD->getLocation(), diag::note_non_trivial_c_union)
          << 1 << 2 << QT << FD->getName();
  }

  void visitStruct(QualType QT, const FieldDecl *FD, bool InNonTrivialUnion) {
    const RecordDecl *RD = QT->castAs<RecordType>()->getDecl();
    if (RD->isUnion()) {
      if (OrigLoc.isValid()) {
        bool IsUnion = false;
        if (auto *OrigRD = OrigTy->getAsRecordDecl())
          IsUnion = OrigRD->isUnion();
        S.Diag(OrigLoc, diag::err_non_trivial_c_union_in_invalid_context)
            << 2 << OrigTy << IsUnion << UseContext;
        // Reset OrigLoc so that this diagnostic is emitted only once.
        OrigLoc = SourceLocation();
      }
      InNonTrivialUnion = true;
    }

    if (InNonTrivialUnion)
      S.Diag(RD->getLocation(), diag::note_non_trivial_c_union)
          << 0 << 2 << QT.getUnqualifiedType() << "";

    for (const FieldDecl *FD : RD->fields())
      if (!shouldIgnoreForRecordTriviality(FD))
        asDerived().visit(FD->getType(), FD, InNonTrivialUnion);
  }

  void preVisit(QualType::PrimitiveCopyKind PCK, QualType QT,
                const FieldDecl *FD, bool InNonTrivialUnion) {}
  void visitTrivial(QualType QT, const FieldDecl *FD, bool InNonTrivialUnion) {}
  void visitVolatileTrivial(QualType QT, const FieldDecl *FD,
                            bool InNonTrivialUnion) {}

  // The non-trivial C union type or the struct/union type that contains a
  // non-trivial C union.
  QualType OrigTy;
  SourceLocation OrigLoc;
  Sema::NonTrivialCUnionContext UseContext;
  Sema &S;
};

} // namespace

void Sema::checkNonTrivialCUnion(QualType QT, SourceLocation Loc,
                                 NonTrivialCUnionContext UseContext,
                                 unsigned NonTrivialKind) {
  assert((QT.hasNonTrivialToPrimitiveDefaultInitializeCUnion() ||
          QT.hasNonTrivialToPrimitiveDestructCUnion() ||
          QT.hasNonTrivialToPrimitiveCopyCUnion()) &&
         "shouldn't be called if type doesn't have a non-trivial C union");

  if ((NonTrivialKind & NTCUK_Init) &&
      QT.hasNonTrivialToPrimitiveDefaultInitializeCUnion())
    DiagNonTrivalCUnionDefaultInitializeVisitor(QT, Loc, UseContext, *this)
        .visit(QT, nullptr, false);
  if ((NonTrivialKind & NTCUK_Destruct) &&
      QT.hasNonTrivialToPrimitiveDestructCUnion())
    DiagNonTrivalCUnionDestructedTypeVisitor(QT, Loc, UseContext, *this)
        .visit(QT, nullptr, false);
  if ((NonTrivialKind & NTCUK_Copy) && QT.hasNonTrivialToPrimitiveCopyCUnion())
    DiagNonTrivalCUnionCopyVisitor(QT, Loc, UseContext, *this)
        .visit(QT, nullptr, false);
}

void Sema::AddInitializerToDecl(Decl *RealDecl, Expr *Init, bool DirectInit) {
  // If there is no declaration, there was an error parsing it.  Just ignore
  // the initializer.
  if (!RealDecl) {
    CorrectDelayedTyposInExpr(Init, dyn_cast_or_null<VarDecl>(RealDecl));
    return;
  }

  if (auto *Method = dyn_cast<CXXMethodDecl>(RealDecl)) {
    if (!Method->isInvalidDecl()) {
      // Pure-specifiers are handled in ActOnPureSpecifier.
      Diag(Method->getLocation(), diag::err_member_function_initialization)
          << Method->getDeclName() << Init->getSourceRange();
      Method->setInvalidDecl();
    }
    return;
  }

  VarDecl *VDecl = dyn_cast<VarDecl>(RealDecl);
  if (!VDecl) {
    assert(!isa<FieldDecl>(RealDecl) && "field init shouldn't get here");
    Diag(RealDecl->getLocation(), diag::err_illegal_initializer);
    RealDecl->setInvalidDecl();
    return;
  }

  if (VDecl->isInvalidDecl()) {
    ExprResult Res = CorrectDelayedTyposInExpr(Init, VDecl);
    SmallVector<Expr *> SubExprs;
    if (Res.isUsable())
      SubExprs.push_back(Res.get());
    ExprResult Recovery =
        CreateRecoveryExpr(Init->getBeginLoc(), Init->getEndLoc(), SubExprs);
    if (Expr *E = Recovery.get())
      VDecl->setInit(E);
    return;
  }

  // WebAssembly tables can't be used to initialise a variable.
  if (Init && !Init->getType().isNull() &&
      Init->getType()->isWebAssemblyTableType()) {
    Diag(Init->getExprLoc(), diag::err_wasm_table_art) << 0;
    VDecl->setInvalidDecl();
    return;
  }

  // C++11 [decl.spec.auto]p6. Deduce the type which 'auto' stands in for.
  if (VDecl->getType()->isUndeducedType()) {
    // Attempt typo correction early so that the type of the init expression can
    // be deduced based on the chosen correction if the original init contains a
    // TypoExpr.
    ExprResult Res = CorrectDelayedTyposInExpr(Init, VDecl);
    if (!Res.isUsable()) {
      // There are unresolved typos in Init, just drop them.
      // FIXME: improve the recovery strategy to preserve the Init.
      RealDecl->setInvalidDecl();
      return;
    }
    if (Res.get()->containsErrors()) {
      // Invalidate the decl as we don't know the type for recovery-expr yet.
      RealDecl->setInvalidDecl();
      VDecl->setInit(Res.get());
      return;
    }
    Init = Res.get();

    if (DeduceVariableDeclarationType(VDecl, DirectInit, Init))
      return;
  }

  // dllimport cannot be used on variable definitions.
  if (VDecl->hasAttr<DLLImportAttr>() && !VDecl->isStaticDataMember()) {
    Diag(VDecl->getLocation(), diag::err_attribute_dllimport_data_definition);
    VDecl->setInvalidDecl();
    return;
  }

  // C99 6.7.8p5. If the declaration of an identifier has block scope, and
  // the identifier has external or internal linkage, the declaration shall
  // have no initializer for the identifier.
  // C++14 [dcl.init]p5 is the same restriction for C++.
  if (VDecl->isLocalVarDecl() && VDecl->hasExternalStorage()) {
    Diag(VDecl->getLocation(), diag::err_block_extern_cant_init);
    VDecl->setInvalidDecl();
    return;
  }

  if (!VDecl->getType()->isDependentType()) {
    // A definition must end up with a complete type, which means it must be
    // complete with the restriction that an array type might be completed by
    // the initializer; note that later code assumes this restriction.
    QualType BaseDeclType = VDecl->getType();
    if (const ArrayType *Array = Context.getAsIncompleteArrayType(BaseDeclType))
      BaseDeclType = Array->getElementType();
    if (RequireCompleteType(VDecl->getLocation(), BaseDeclType,
                            diag::err_typecheck_decl_incomplete_type)) {
      RealDecl->setInvalidDecl();
      return;
    }

    // The variable can not have an abstract class type.
    if (RequireNonAbstractType(VDecl->getLocation(), VDecl->getType(),
                               diag::err_abstract_type_in_decl,
                               AbstractVariableType))
      VDecl->setInvalidDecl();
  }

  // C++ [module.import/6] external definitions are not permitted in header
  // units.
  if (getLangOpts().CPlusPlusModules && currentModuleIsHeaderUnit() &&
      !VDecl->isInvalidDecl() && VDecl->isThisDeclarationADefinition() &&
      VDecl->getFormalLinkage() == Linkage::External && !VDecl->isInline() &&
      !VDecl->isTemplated() && !isa<VarTemplateSpecializationDecl>(VDecl) &&
      !VDecl->getInstantiatedFromStaticDataMember()) {
    Diag(VDecl->getLocation(), diag::err_extern_def_in_header_unit);
    VDecl->setInvalidDecl();
  }

  // If adding the initializer will turn this declaration into a definition,
  // and we already have a definition for this variable, diagnose or otherwise
  // handle the situation.
  if (VarDecl *Def = VDecl->getDefinition())
    if (Def != VDecl &&
        (!VDecl->isStaticDataMember() || VDecl->isOutOfLine()) &&
        !VDecl->isThisDeclarationADemotedDefinition() &&
        checkVarDeclRedefinition(Def, VDecl))
      return;

  if (getLangOpts().CPlusPlus) {
    // C++ [class.static.data]p4
    //   If a static data member is of const integral or const
    //   enumeration type, its declaration in the class definition can
    //   specify a constant-initializer which shall be an integral
    //   constant expression (5.19). In that case, the member can appear
    //   in integral constant expressions. The member shall still be
    //   defined in a namespace scope if it is used in the program and the
    //   namespace scope definition shall not contain an initializer.
    //
    // We already performed a redefinition check above, but for static
    // data members we also need to check whether there was an in-class
    // declaration with an initializer.
    if (VDecl->isStaticDataMember() && VDecl->getCanonicalDecl()->hasInit()) {
      Diag(Init->getExprLoc(), diag::err_static_data_member_reinitialization)
          << VDecl->getDeclName();
      Diag(VDecl->getCanonicalDecl()->getInit()->getExprLoc(),
           diag::note_previous_initializer)
          << 0;
      return;
    }

    if (VDecl->hasLocalStorage())
      setFunctionHasBranchProtectedScope();

    if (DiagnoseUnexpandedParameterPack(Init, UPPC_Initializer)) {
      VDecl->setInvalidDecl();
      return;
    }
  }

  // OpenCL 1.1 6.5.2: "Variables allocated in the __local address space inside
  // a kernel function cannot be initialized."
  if (VDecl->getType().getAddressSpace() == LangAS::opencl_local) {
    Diag(VDecl->getLocation(), diag::err_local_cant_init);
    VDecl->setInvalidDecl();
    return;
  }

  // The LoaderUninitialized attribute acts as a definition (of undef).
  if (VDecl->hasAttr<LoaderUninitializedAttr>()) {
    Diag(VDecl->getLocation(), diag::err_loader_uninitialized_cant_init);
    VDecl->setInvalidDecl();
    return;
  }

  // Get the decls type and save a reference for later, since
  // CheckInitializerTypes may change it.
  QualType DclT = VDecl->getType(), SavT = DclT;

  // Expressions default to 'id' when we're in a debugger
  // and we are assigning it to a variable of Objective-C pointer type.
  if (getLangOpts().DebuggerCastResultToId && DclT->isObjCObjectPointerType() &&
      Init->getType() == Context.UnknownAnyTy) {
    ExprResult Result = forceUnknownAnyToType(Init, Context.getObjCIdType());
    if (Result.isInvalid()) {
      VDecl->setInvalidDecl();
      return;
    }
    Init = Result.get();
  }

  // Perform the initialization.
  ParenListExpr *CXXDirectInit = dyn_cast<ParenListExpr>(Init);
  bool IsParenListInit = false;
  if (!VDecl->isInvalidDecl()) {
    InitializedEntity Entity = InitializedEntity::InitializeVariable(VDecl);
    InitializationKind Kind = InitializationKind::CreateForInit(
        VDecl->getLocation(), DirectInit, Init);

    MultiExprArg Args = Init;
    if (CXXDirectInit)
      Args = MultiExprArg(CXXDirectInit->getExprs(),
                          CXXDirectInit->getNumExprs());

    // Try to correct any TypoExprs in the initialization arguments.
    for (size_t Idx = 0; Idx < Args.size(); ++Idx) {
      ExprResult Res = CorrectDelayedTyposInExpr(
          Args[Idx], VDecl, /*RecoverUncorrectedTypos=*/true,
          [this, Entity, Kind](Expr *E) {
            InitializationSequence Init(*this, Entity, Kind, MultiExprArg(E));
            return Init.Failed() ? ExprError() : E;
          });
      if (Res.isInvalid()) {
        VDecl->setInvalidDecl();
      } else if (Res.get() != Args[Idx]) {
        Args[Idx] = Res.get();
      }
    }
    if (VDecl->isInvalidDecl())
      return;

    InitializationSequence InitSeq(*this, Entity, Kind, Args,
                                   /*TopLevelOfInitList=*/false,
                                   /*TreatUnavailableAsInvalid=*/false);
    ExprResult Result = InitSeq.Perform(*this, Entity, Kind, Args, &DclT);
    if (Result.isInvalid()) {
      // If the provided initializer fails to initialize the var decl,
      // we attach a recovery expr for better recovery.
      auto RecoveryExpr =
          CreateRecoveryExpr(Init->getBeginLoc(), Init->getEndLoc(), Args);
      if (RecoveryExpr.get())
        VDecl->setInit(RecoveryExpr.get());
      // In general, for error recovery purposes, the initializer doesn't play
      // part in the valid bit of the declaration. There are a few exceptions:
      //  1) if the var decl has a deduced auto type, and the type cannot be
      //     deduced by an invalid initializer;
      //  2) if the var decl is a decomposition decl with a non-deduced type,
      //      and the initialization fails (e.g. `int [a] = {1, 2};`);
      // Case 1) was already handled elsewhere.
      if (isa<DecompositionDecl>(VDecl)) // Case 2)
        VDecl->setInvalidDecl();
      return;
    }

    Init = Result.getAs<Expr>();
    IsParenListInit = !InitSeq.steps().empty() &&
                      InitSeq.step_begin()->Kind ==
                          InitializationSequence::SK_ParenthesizedListInit;
    QualType VDeclType = VDecl->getType();
    if (Init && !Init->getType().isNull() &&
        !Init->getType()->isDependentType() && !VDeclType->isDependentType() &&
        Context.getAsIncompleteArrayType(VDeclType) &&
        Context.getAsIncompleteArrayType(Init->getType())) {
      // Bail out if it is not possible to deduce array size from the
      // initializer.
      Diag(VDecl->getLocation(), diag::err_typecheck_decl_incomplete_type)
          << VDeclType;
      VDecl->setInvalidDecl();
      return;
    }
  }

  // Check for self-references within variable initializers.
  // Variables declared within a function/method body (except for references)
  // are handled by a dataflow analysis.
  // This is undefined behavior in C++, but valid in C.
  if (getLangOpts().CPlusPlus)
    if (!VDecl->hasLocalStorage() || VDecl->getType()->isRecordType() ||
        VDecl->getType()->isReferenceType())
      CheckSelfReference(*this, RealDecl, Init, DirectInit);

  // If the type changed, it means we had an incomplete type that was
  // completed by the initializer. For example:
  //   int ary[] = { 1, 3, 5 };
  // "ary" transitions from an IncompleteArrayType to a ConstantArrayType.
  if (!VDecl->isInvalidDecl() && (DclT != SavT))
    VDecl->setType(DclT);

  if (!VDecl->isInvalidDecl()) {
    checkUnsafeAssigns(VDecl->getLocation(), VDecl->getType(), Init);

    if (VDecl->hasAttr<BlocksAttr>())
      ObjC().checkRetainCycles(VDecl, Init);

    // It is safe to assign a weak reference into a strong variable.
    // Although this code can still have problems:
    //   id x = self.weakProp;
    //   id y = self.weakProp;
    // we do not warn to warn spuriously when 'x' and 'y' are on separate
    // paths through the function. This should be revisited if
    // -Wrepeated-use-of-weak is made flow-sensitive.
    if (FunctionScopeInfo *FSI = getCurFunction())
      if ((VDecl->getType().getObjCLifetime() == Qualifiers::OCL_Strong ||
           VDecl->getType().isNonWeakInMRRWithObjCWeak(Context)) &&
          !Diags.isIgnored(diag::warn_arc_repeated_use_of_weak,
                           Init->getBeginLoc()))
        FSI->markSafeWeakUse(Init);
  }

  // The initialization is usually a full-expression.
  //
  // FIXME: If this is a braced initialization of an aggregate, it is not
  // an expression, and each individual field initializer is a separate
  // full-expression. For instance, in:
  //
  //   struct Temp { ~Temp(); };
  //   struct S { S(Temp); };
  //   struct T { S a, b; } t = { Temp(), Temp() }
  //
  // we should destroy the first Temp before constructing the second.
  ExprResult Result =
      ActOnFinishFullExpr(Init, VDecl->getLocation(),
                          /*DiscardedValue*/ false, VDecl->isConstexpr());
  if (Result.isInvalid()) {
    VDecl->setInvalidDecl();
    return;
  }
  Init = Result.get();

  // Attach the initializer to the decl.
  VDecl->setInit(Init);

  if (VDecl->isLocalVarDecl()) {
    // Don't check the initializer if the declaration is malformed.
    if (VDecl->isInvalidDecl()) {
      // do nothing

    // OpenCL v1.2 s6.5.3: __constant locals must be constant-initialized.
    // This is true even in C++ for OpenCL.
    } else if (VDecl->getType().getAddressSpace() == LangAS::opencl_constant) {
      CheckForConstantInitializer(Init);

      // Otherwise, C++ does not restrict the initializer.
    } else if (getLangOpts().CPlusPlus) {
      // do nothing

    // C99 6.7.8p4: All the expressions in an initializer for an object that has
    // static storage duration shall be constant expressions or string literals.
    } else if (VDecl->getStorageClass() == SC_Static) {
      CheckForConstantInitializer(Init);

      // C89 is stricter than C99 for aggregate initializers.
      // C89 6.5.7p3: All the expressions [...] in an initializer list
      // for an object that has aggregate or union type shall be
      // constant expressions.
    } else if (!getLangOpts().C99 && VDecl->getType()->isAggregateType() &&
               isa<InitListExpr>(Init)) {
      CheckForConstantInitializer(Init, diag::ext_aggregate_init_not_constant);
    }

    if (auto *E = dyn_cast<ExprWithCleanups>(Init))
      if (auto *BE = dyn_cast<BlockExpr>(E->getSubExpr()->IgnoreParens()))
        if (VDecl->hasLocalStorage())
          BE->getBlockDecl()->setCanAvoidCopyToHeap();
  } else if (VDecl->isStaticDataMember() && !VDecl->isInline() &&
             VDecl->getLexicalDeclContext()->isRecord()) {
    // This is an in-class initialization for a static data member, e.g.,
    //
    // struct S {
    //   static const int value = 17;
    // };

    // C++ [class.mem]p4:
    //   A member-declarator can contain a constant-initializer only
    //   if it declares a static member (9.4) of const integral or
    //   const enumeration type, see 9.4.2.
    //
    // C++11 [class.static.data]p3:
    //   If a non-volatile non-inline const static data member is of integral
    //   or enumeration type, its declaration in the class definition can
    //   specify a brace-or-equal-initializer in which every initializer-clause
    //   that is an assignment-expression is a constant expression. A static
    //   data member of literal type can be declared in the class definition
    //   with the constexpr specifier; if so, its declaration shall specify a
    //   brace-or-equal-initializer in which every initializer-clause that is
    //   an assignment-expression is a constant expression.

    // Do nothing on dependent types.
    if (DclT->isDependentType()) {

    // Allow any 'static constexpr' members, whether or not they are of literal
    // type. We separately check that every constexpr variable is of literal
    // type.
    } else if (VDecl->isConstexpr()) {

    // Require constness.
    } else if (!DclT.isConstQualified()) {
      Diag(VDecl->getLocation(), diag::err_in_class_initializer_non_const)
        << Init->getSourceRange();
      VDecl->setInvalidDecl();

    // We allow integer constant expressions in all cases.
    } else if (DclT->isIntegralOrEnumerationType()) {
      // Check whether the expression is a constant expression.
      SourceLocation Loc;
      if (getLangOpts().CPlusPlus11 && DclT.isVolatileQualified())
        // In C++11, a non-constexpr const static data member with an
        // in-class initializer cannot be volatile.
        Diag(VDecl->getLocation(), diag::err_in_class_initializer_volatile);
      else if (Init->isValueDependent())
        ; // Nothing to check.
      else if (Init->isIntegerConstantExpr(Context, &Loc))
        ; // Ok, it's an ICE!
      else if (Init->getType()->isScopedEnumeralType() &&
               Init->isCXX11ConstantExpr(Context))
        ; // Ok, it is a scoped-enum constant expression.
      else if (Init->isEvaluatable(Context)) {
        // If we can constant fold the initializer through heroics, accept it,
        // but report this as a use of an extension for -pedantic.
        Diag(Loc, diag::ext_in_class_initializer_non_constant)
          << Init->getSourceRange();
      } else {
        // Otherwise, this is some crazy unknown case.  Report the issue at the
        // location provided by the isIntegerConstantExpr failed check.
        Diag(Loc, diag::err_in_class_initializer_non_constant)
          << Init->getSourceRange();
        VDecl->setInvalidDecl();
      }

    // We allow foldable floating-point constants as an extension.
    } else if (DclT->isFloatingType()) { // also permits complex, which is ok
      // In C++98, this is a GNU extension. In C++11, it is not, but we support
      // it anyway and provide a fixit to add the 'constexpr'.
      if (getLangOpts().CPlusPlus11) {
        Diag(VDecl->getLocation(),
             diag::ext_in_class_initializer_float_type_cxx11)
            << DclT << Init->getSourceRange();
        Diag(VDecl->getBeginLoc(),
             diag::note_in_class_initializer_float_type_cxx11)
            << FixItHint::CreateInsertion(VDecl->getBeginLoc(), "constexpr ");
      } else {
        Diag(VDecl->getLocation(), diag::ext_in_class_initializer_float_type)
          << DclT << Init->getSourceRange();

        if (!Init->isValueDependent() && !Init->isEvaluatable(Context)) {
          Diag(Init->getExprLoc(), diag::err_in_class_initializer_non_constant)
            << Init->getSourceRange();
          VDecl->setInvalidDecl();
        }
      }

    // Suggest adding 'constexpr' in C++11 for literal types.
    } else if (getLangOpts().CPlusPlus11 && DclT->isLiteralType(Context)) {
      Diag(VDecl->getLocation(), diag::err_in_class_initializer_literal_type)
          << DclT << Init->getSourceRange()
          << FixItHint::CreateInsertion(VDecl->getBeginLoc(), "constexpr ");
      VDecl->setConstexpr(true);

    } else {
      Diag(VDecl->getLocation(), diag::err_in_class_initializer_bad_type)
        << DclT << Init->getSourceRange();
      VDecl->setInvalidDecl();
    }
  } else if (VDecl->isFileVarDecl()) {
    // In C, extern is typically used to avoid tentative definitions when
    // declaring variables in headers, but adding an initializer makes it a
    // definition. This is somewhat confusing, so GCC and Clang both warn on it.
    // In C++, extern is often used to give implicitly static const variables
    // external linkage, so don't warn in that case. If selectany is present,
    // this might be header code intended for C and C++ inclusion, so apply the
    // C++ rules.
    if (VDecl->getStorageClass() == SC_Extern &&
        ((!getLangOpts().CPlusPlus && !VDecl->hasAttr<SelectAnyAttr>()) ||
         !Context.getBaseElementType(VDecl->getType()).isConstQualified()) &&
        !(getLangOpts().CPlusPlus && VDecl->isExternC()) &&
        !isTemplateInstantiation(VDecl->getTemplateSpecializationKind()))
      Diag(VDecl->getLocation(), diag::warn_extern_init);

    // In Microsoft C++ mode, a const variable defined in namespace scope has
    // external linkage by default if the variable is declared with
    // __declspec(dllexport).
    if (Context.getTargetInfo().getCXXABI().isMicrosoft() &&
        getLangOpts().CPlusPlus && VDecl->getType().isConstQualified() &&
        VDecl->hasAttr<DLLExportAttr>() && VDecl->getDefinition())
      VDecl->setStorageClass(SC_Extern);

    // C99 6.7.8p4. All file scoped initializers need to be constant.
    // Avoid duplicate diagnostics for constexpr variables.
    if (!getLangOpts().CPlusPlus && !VDecl->isInvalidDecl() &&
        !VDecl->isConstexpr())
      CheckForConstantInitializer(Init);
  }

  QualType InitType = Init->getType();
  if (!InitType.isNull() &&
      (InitType.hasNonTrivialToPrimitiveDefaultInitializeCUnion() ||
       InitType.hasNonTrivialToPrimitiveCopyCUnion()))
    checkNonTrivialCUnionInInitializer(Init, Init->getExprLoc());

  // We will represent direct-initialization similarly to copy-initialization:
  //    int x(1);  -as-> int x = 1;
  //    ClassType x(a,b,c); -as-> ClassType x = ClassType(a,b,c);
  //
  // Clients that want to distinguish between the two forms, can check for
  // direct initializer using VarDecl::getInitStyle().
  // A major benefit is that clients that don't particularly care about which
  // exactly form was it (like the CodeGen) can handle both cases without
  // special case code.

  // C++ 8.5p11:
  // The form of initialization (using parentheses or '=') is generally
  // insignificant, but does matter when the entity being initialized has a
  // class type.
  if (CXXDirectInit) {
    assert(DirectInit && "Call-style initializer must be direct init.");
    VDecl->setInitStyle(IsParenListInit ? VarDecl::ParenListInit
                                        : VarDecl::CallInit);
  } else if (DirectInit) {
    // This must be list-initialization. No other way is direct-initialization.
    VDecl->setInitStyle(VarDecl::ListInit);
  }

  if (LangOpts.OpenMP &&
      (LangOpts.OpenMPIsTargetDevice || !LangOpts.OMPTargetTriples.empty()) &&
      VDecl->isFileVarDecl())
    DeclsToCheckForDeferredDiags.insert(VDecl);
  CheckCompleteVariableDeclaration(VDecl);
}

void Sema::ActOnInitializerError(Decl *D) {
  // Our main concern here is re-establishing invariants like "a
  // variable's type is either dependent or complete".
  if (!D || D->isInvalidDecl()) return;

  VarDecl *VD = dyn_cast<VarDecl>(D);
  if (!VD) return;

  // Bindings are not usable if we can't make sense of the initializer.
  if (auto *DD = dyn_cast<DecompositionDecl>(D))
    for (auto *BD : DD->bindings())
      BD->setInvalidDecl();

  // Auto types are meaningless if we can't make sense of the initializer.
  if (VD->getType()->isUndeducedType()) {
    D->setInvalidDecl();
    return;
  }

  QualType Ty = VD->getType();
  if (Ty->isDependentType()) return;

  // Require a complete type.
  if (RequireCompleteType(VD->getLocation(),
                          Context.getBaseElementType(Ty),
                          diag::err_typecheck_decl_incomplete_type)) {
    VD->setInvalidDecl();
    return;
  }

  // Require a non-abstract type.
  if (RequireNonAbstractType(VD->getLocation(), Ty,
                             diag::err_abstract_type_in_decl,
                             AbstractVariableType)) {
    VD->setInvalidDecl();
    return;
  }

  // Don't bother complaining about constructors or destructors,
  // though.
}

void Sema::ActOnUninitializedDecl(Decl *RealDecl) {
  // If there is no declaration, there was an error parsing it. Just ignore it.
  if (!RealDecl)
    return;

  if (VarDecl *Var = dyn_cast<VarDecl>(RealDecl)) {
    QualType Type = Var->getType();

    // C++1z [dcl.dcl]p1 grammar implies that an initializer is mandatory.
    if (isa<DecompositionDecl>(RealDecl)) {
      Diag(Var->getLocation(), diag::err_decomp_decl_requires_init) << Var;
      Var->setInvalidDecl();
      return;
    }

    if (Type->isUndeducedType() &&
        DeduceVariableDeclarationType(Var, false, nullptr))
      return;

    // C++11 [class.static.data]p3: A static data member can be declared with
    // the constexpr specifier; if so, its declaration shall specify
    // a brace-or-equal-initializer.
    // C++11 [dcl.constexpr]p1: The constexpr specifier shall be applied only to
    // the definition of a variable [...] or the declaration of a static data
    // member.
    if (Var->isConstexpr() && !Var->isThisDeclarationADefinition() &&
        !Var->isThisDeclarationADemotedDefinition()) {
      if (Var->isStaticDataMember()) {
        // C++1z removes the relevant rule; the in-class declaration is always
        // a definition there.
        if (!getLangOpts().CPlusPlus17 &&
            !Context.getTargetInfo().getCXXABI().isMicrosoft()) {
          Diag(Var->getLocation(),
               diag::err_constexpr_static_mem_var_requires_init)
              << Var;
          Var->setInvalidDecl();
          return;
        }
      } else {
        Diag(Var->getLocation(), diag::err_invalid_constexpr_var_decl);
        Var->setInvalidDecl();
        return;
      }
    }

    // OpenCL v1.1 s6.5.3: variables declared in the constant address space must
    // be initialized.
    if (!Var->isInvalidDecl() &&
        Var->getType().getAddressSpace() == LangAS::opencl_constant &&
        Var->getStorageClass() != SC_Extern && !Var->getInit()) {
      bool HasConstExprDefaultConstructor = false;
      if (CXXRecordDecl *RD = Var->getType()->getAsCXXRecordDecl()) {
        for (auto *Ctor : RD->ctors()) {
          if (Ctor->isConstexpr() && Ctor->getNumParams() == 0 &&
              Ctor->getMethodQualifiers().getAddressSpace() ==
                  LangAS::opencl_constant) {
            HasConstExprDefaultConstructor = true;
          }
        }
      }
      if (!HasConstExprDefaultConstructor) {
        Diag(Var->getLocation(), diag::err_opencl_constant_no_init);
        Var->setInvalidDecl();
        return;
      }
    }

    if (!Var->isInvalidDecl() && RealDecl->hasAttr<LoaderUninitializedAttr>()) {
      if (Var->getStorageClass() == SC_Extern) {
        Diag(Var->getLocation(), diag::err_loader_uninitialized_extern_decl)
            << Var;
        Var->setInvalidDecl();
        return;
      }
      if (RequireCompleteType(Var->getLocation(), Var->getType(),
                              diag::err_typecheck_decl_incomplete_type)) {
        Var->setInvalidDecl();
        return;
      }
      if (CXXRecordDecl *RD = Var->getType()->getAsCXXRecordDecl()) {
        if (!RD->hasTrivialDefaultConstructor()) {
          Diag(Var->getLocation(), diag::err_loader_uninitialized_trivial_ctor);
          Var->setInvalidDecl();
          return;
        }
      }
      // The declaration is uninitialized, no need for further checks.
      return;
    }

    VarDecl::DefinitionKind DefKind = Var->isThisDeclarationADefinition();
    if (!Var->isInvalidDecl() && DefKind != VarDecl::DeclarationOnly &&
        Var->getType().hasNonTrivialToPrimitiveDefaultInitializeCUnion())
      checkNonTrivialCUnion(Var->getType(), Var->getLocation(),
                            NTCUC_DefaultInitializedObject, NTCUK_Init);


    switch (DefKind) {
    case VarDecl::Definition:
      if (!Var->isStaticDataMember() || !Var->getAnyInitializer())
        break;

      // We have an out-of-line definition of a static data member
      // that has an in-class initializer, so we type-check this like
      // a declaration.
      //
      [[fallthrough]];

    case VarDecl::DeclarationOnly:
      // It's only a declaration.

      // Block scope. C99 6.7p7: If an identifier for an object is
      // declared with no linkage (C99 6.2.2p6), the type for the
      // object shall be complete.
      if (!Type->isDependentType() && Var->isLocalVarDecl() &&
          !Var->hasLinkage() && !Var->isInvalidDecl() &&
          RequireCompleteType(Var->getLocation(), Type,
                              diag::err_typecheck_decl_incomplete_type))
        Var->setInvalidDecl();

      // Make sure that the type is not abstract.
      if (!Type->isDependentType() && !Var->isInvalidDecl() &&
          RequireNonAbstractType(Var->getLocation(), Type,
                                 diag::err_abstract_type_in_decl,
                                 AbstractVariableType))
        Var->setInvalidDecl();
      if (!Type->isDependentType() && !Var->isInvalidDecl() &&
          Var->getStorageClass() == SC_PrivateExtern) {
        Diag(Var->getLocation(), diag::warn_private_extern);
        Diag(Var->getLocation(), diag::note_private_extern);
      }

      if (Context.getTargetInfo().allowDebugInfoForExternalRef() &&
          !Var->isInvalidDecl())
        ExternalDeclarations.push_back(Var);

      return;

    case VarDecl::TentativeDefinition:
      // File scope. C99 6.9.2p2: A declaration of an identifier for an
      // object that has file scope without an initializer, and without a
      // storage-class specifier or with the storage-class specifier "static",
      // constitutes a tentative definition. Note: A tentative definition with
      // external linkage is valid (C99 6.2.2p5).
      if (!Var->isInvalidDecl()) {
        if (const IncompleteArrayType *ArrayT
                                    = Context.getAsIncompleteArrayType(Type)) {
          if (RequireCompleteSizedType(
                  Var->getLocation(), ArrayT->getElementType(),
                  diag::err_array_incomplete_or_sizeless_type))
            Var->setInvalidDecl();
        } else if (Var->getStorageClass() == SC_Static) {
          // C99 6.9.2p3: If the declaration of an identifier for an object is
          // a tentative definition and has internal linkage (C99 6.2.2p3), the
          // declared type shall not be an incomplete type.
          // NOTE: code such as the following
          //     static struct s;
          //     struct s { int a; };
          // is accepted by gcc. Hence here we issue a warning instead of
          // an error and we do not invalidate the static declaration.
          // NOTE: to avoid multiple warnings, only check the first declaration.
          if (Var->isFirstDecl())
            RequireCompleteType(Var->getLocation(), Type,
                                diag::ext_typecheck_decl_incomplete_type);
        }
      }

      // Record the tentative definition; we're done.
      if (!Var->isInvalidDecl())
        TentativeDefinitions.push_back(Var);
      return;
    }

    // Provide a specific diagnostic for uninitialized variable
    // definitions with incomplete array type.
    if (Type->isIncompleteArrayType()) {
      if (Var->isConstexpr())
        Diag(Var->getLocation(), diag::err_constexpr_var_requires_const_init)
            << Var;
      else
        Diag(Var->getLocation(),
             diag::err_typecheck_incomplete_array_needs_initializer);
      Var->setInvalidDecl();
      return;
    }

    // Provide a specific diagnostic for uninitialized variable
    // definitions with reference type.
    if (Type->isReferenceType()) {
      Diag(Var->getLocation(), diag::err_reference_var_requires_init)
          << Var << SourceRange(Var->getLocation(), Var->getLocation());
      return;
    }

    // Do not attempt to type-check the default initializer for a
    // variable with dependent type.
    if (Type->isDependentType())
      return;

    if (Var->isInvalidDecl())
      return;

    if (!Var->hasAttr<AliasAttr>()) {
      if (RequireCompleteType(Var->getLocation(),
                              Context.getBaseElementType(Type),
                              diag::err_typecheck_decl_incomplete_type)) {
        Var->setInvalidDecl();
        return;
      }
    } else {
      return;
    }

    // The variable can not have an abstract class type.
    if (RequireNonAbstractType(Var->getLocation(), Type,
                               diag::err_abstract_type_in_decl,
                               AbstractVariableType)) {
      Var->setInvalidDecl();
      return;
    }

    // Check for jumps past the implicit initializer.  C++0x
    // clarifies that this applies to a "variable with automatic
    // storage duration", not a "local variable".
    // C++11 [stmt.dcl]p3
    //   A program that jumps from a point where a variable with automatic
    //   storage duration is not in scope to a point where it is in scope is
    //   ill-formed unless the variable has scalar type, class type with a
    //   trivial default constructor and a trivial destructor, a cv-qualified
    //   version of one of these types, or an array of one of the preceding
    //   types and is declared without an initializer.
    if (getLangOpts().CPlusPlus && Var->hasLocalStorage()) {
      if (const RecordType *Record
            = Context.getBaseElementType(Type)->getAs<RecordType>()) {
        CXXRecordDecl *CXXRecord = cast<CXXRecordDecl>(Record->getDecl());
        // Mark the function (if we're in one) for further checking even if the
        // looser rules of C++11 do not require such checks, so that we can
        // diagnose incompatibilities with C++98.
        if (!CXXRecord->isPOD())
          setFunctionHasBranchProtectedScope();
      }
    }
    // In OpenCL, we can't initialize objects in the __local address space,
    // even implicitly, so don't synthesize an implicit initializer.
    if (getLangOpts().OpenCL &&
        Var->getType().getAddressSpace() == LangAS::opencl_local)
      return;
    // C++03 [dcl.init]p9:
    //   If no initializer is specified for an object, and the
    //   object is of (possibly cv-qualified) non-POD class type (or
    //   array thereof), the object shall be default-initialized; if
    //   the object is of const-qualified type, the underlying class
    //   type shall have a user-declared default
    //   constructor. Otherwise, if no initializer is specified for
    //   a non- static object, the object and its subobjects, if
    //   any, have an indeterminate initial value); if the object
    //   or any of its subobjects are of const-qualified type, the
    //   program is ill-formed.
    // C++0x [dcl.init]p11:
    //   If no initializer is specified for an object, the object is
    //   default-initialized; [...].
    InitializedEntity Entity = InitializedEntity::InitializeVariable(Var);
    InitializationKind Kind
      = InitializationKind::CreateDefault(Var->getLocation());

    InitializationSequence InitSeq(*this, Entity, Kind, std::nullopt);
    ExprResult Init = InitSeq.Perform(*this, Entity, Kind, std::nullopt);

    if (Init.get()) {
      Var->setInit(MaybeCreateExprWithCleanups(Init.get()));
      // This is important for template substitution.
      Var->setInitStyle(VarDecl::CallInit);
    } else if (Init.isInvalid()) {
      // If default-init fails, attach a recovery-expr initializer to track
      // that initialization was attempted and failed.
      auto RecoveryExpr =
          CreateRecoveryExpr(Var->getLocation(), Var->getLocation(), {});
      if (RecoveryExpr.get())
        Var->setInit(RecoveryExpr.get());
    }

    CheckCompleteVariableDeclaration(Var);
  }
}

void Sema::ActOnCXXForRangeDecl(Decl *D) {
  // If there is no declaration, there was an error parsing it. Ignore it.
  if (!D)
    return;

  VarDecl *VD = dyn_cast<VarDecl>(D);
  if (!VD) {
    Diag(D->getLocation(), diag::err_for_range_decl_must_be_var);
    D->setInvalidDecl();
    return;
  }

  VD->setCXXForRangeDecl(true);

  // for-range-declaration cannot be given a storage class specifier.
  int Error = -1;
  switch (VD->getStorageClass()) {
  case SC_None:
    break;
  case SC_Extern:
    Error = 0;
    break;
  case SC_Static:
    Error = 1;
    break;
  case SC_PrivateExtern:
    Error = 2;
    break;
  case SC_Auto:
    Error = 3;
    break;
  case SC_Register:
    Error = 4;
    break;
  }

  // for-range-declaration cannot be given a storage class specifier con't.
  switch (VD->getTSCSpec()) {
  case TSCS_thread_local:
    Error = 6;
    break;
  case TSCS___thread:
  case TSCS__Thread_local:
  case TSCS_unspecified:
    break;
  }

  if (Error != -1) {
    Diag(VD->getOuterLocStart(), diag::err_for_range_storage_class)
        << VD << Error;
    D->setInvalidDecl();
  }
}

StmtResult Sema::ActOnCXXForRangeIdentifier(Scope *S, SourceLocation IdentLoc,
                                            IdentifierInfo *Ident,
                                            ParsedAttributes &Attrs) {
  // C++1y [stmt.iter]p1:
  //   A range-based for statement of the form
  //      for ( for-range-identifier : for-range-initializer ) statement
  //   is equivalent to
  //      for ( auto&& for-range-identifier : for-range-initializer ) statement
  DeclSpec DS(Attrs.getPool().getFactory());

  const char *PrevSpec;
  unsigned DiagID;
  DS.SetTypeSpecType(DeclSpec::TST_auto, IdentLoc, PrevSpec, DiagID,
                     getPrintingPolicy());

  Declarator D(DS, ParsedAttributesView::none(), DeclaratorContext::ForInit);
  D.SetIdentifier(Ident, IdentLoc);
  D.takeAttributes(Attrs);

  D.AddTypeInfo(DeclaratorChunk::getReference(0, IdentLoc, /*lvalue*/ false),
                IdentLoc);
  Decl *Var = ActOnDeclarator(S, D);
  cast<VarDecl>(Var)->setCXXForRangeDecl(true);
  FinalizeDeclaration(Var);
  return ActOnDeclStmt(FinalizeDeclaratorGroup(S, DS, Var), IdentLoc,
                       Attrs.Range.getEnd().isValid() ? Attrs.Range.getEnd()
                                                      : IdentLoc);
}

void Sema::CheckCompleteVariableDeclaration(VarDecl *var) {
  if (var->isInvalidDecl()) return;

  CUDA().MaybeAddConstantAttr(var);

  if (getLangOpts().OpenCL) {
    // OpenCL v2.0 s6.12.5 - Every block variable declaration must have an
    // initialiser
    if (var->getTypeSourceInfo()->getType()->isBlockPointerType() &&
        !var->hasInit()) {
      Diag(var->getLocation(), diag::err_opencl_invalid_block_declaration)
          << 1 /*Init*/;
      var->setInvalidDecl();
      return;
    }
  }

  // In Objective-C, don't allow jumps past the implicit initialization of a
  // local retaining variable.
  if (getLangOpts().ObjC &&
      var->hasLocalStorage()) {
    switch (var->getType().getObjCLifetime()) {
    case Qualifiers::OCL_None:
    case Qualifiers::OCL_ExplicitNone:
    case Qualifiers::OCL_Autoreleasing:
      break;

    case Qualifiers::OCL_Weak:
    case Qualifiers::OCL_Strong:
      setFunctionHasBranchProtectedScope();
      break;
    }
  }

  if (var->hasLocalStorage() &&
      var->getType().isDestructedType() == QualType::DK_nontrivial_c_struct)
    setFunctionHasBranchProtectedScope();

  // Warn about externally-visible variables being defined without a
  // prior declaration.  We only want to do this for global
  // declarations, but we also specifically need to avoid doing it for
  // class members because the linkage of an anonymous class can
  // change if it's later given a typedef name.
  if (var->isThisDeclarationADefinition() &&
      var->getDeclContext()->getRedeclContext()->isFileContext() &&
      var->isExternallyVisible() && var->hasLinkage() &&
      !var->isInline() && !var->getDescribedVarTemplate() &&
      var->getStorageClass() != SC_Register &&
      !isa<VarTemplatePartialSpecializationDecl>(var) &&
      !isTemplateInstantiation(var->getTemplateSpecializationKind()) &&
      !getDiagnostics().isIgnored(diag::warn_missing_variable_declarations,
                                  var->getLocation())) {
    // Find a previous declaration that's not a definition.
    VarDecl *prev = var->getPreviousDecl();
    while (prev && prev->isThisDeclarationADefinition())
      prev = prev->getPreviousDecl();

    if (!prev) {
      Diag(var->getLocation(), diag::warn_missing_variable_declarations) << var;
      Diag(var->getTypeSpecStartLoc(), diag::note_static_for_internal_linkage)
          << /* variable */ 0;
    }
  }

  // Cache the result of checking for constant initialization.
  std::optional<bool> CacheHasConstInit;
  const Expr *CacheCulprit = nullptr;
  auto checkConstInit = [&]() mutable {
    if (!CacheHasConstInit)
      CacheHasConstInit = var->getInit()->isConstantInitializer(
            Context, var->getType()->isReferenceType(), &CacheCulprit);
    return *CacheHasConstInit;
  };

  if (var->getTLSKind() == VarDecl::TLS_Static) {
    if (var->getType().isDestructedType()) {
      // GNU C++98 edits for __thread, [basic.start.term]p3:
      //   The type of an object with thread storage duration shall not
      //   have a non-trivial destructor.
      Diag(var->getLocation(), diag::err_thread_nontrivial_dtor);
      if (getLangOpts().CPlusPlus11)
        Diag(var->getLocation(), diag::note_use_thread_local);
    } else if (getLangOpts().CPlusPlus && var->hasInit()) {
      if (!checkConstInit()) {
        // GNU C++98 edits for __thread, [basic.start.init]p4:
        //   An object of thread storage duration shall not require dynamic
        //   initialization.
        // FIXME: Need strict checking here.
        Diag(CacheCulprit->getExprLoc(), diag::err_thread_dynamic_init)
          << CacheCulprit->getSourceRange();
        if (getLangOpts().CPlusPlus11)
          Diag(var->getLocation(), diag::note_use_thread_local);
      }
    }
  }


  if (!var->getType()->isStructureType() && var->hasInit() &&
      isa<InitListExpr>(var->getInit())) {
    const auto *ILE = cast<InitListExpr>(var->getInit());
    unsigned NumInits = ILE->getNumInits();
    if (NumInits > 2)
      for (unsigned I = 0; I < NumInits; ++I) {
        const auto *Init = ILE->getInit(I);
        if (!Init)
          break;
        const auto *SL = dyn_cast<StringLiteral>(Init->IgnoreImpCasts());
        if (!SL)
          break;

        unsigned NumConcat = SL->getNumConcatenated();
        // Diagnose missing comma in string array initialization.
        // Do not warn when all the elements in the initializer are concatenated
        // together. Do not warn for macros too.
        if (NumConcat == 2 && !SL->getBeginLoc().isMacroID()) {
          bool OnlyOneMissingComma = true;
          for (unsigned J = I + 1; J < NumInits; ++J) {
            const auto *Init = ILE->getInit(J);
            if (!Init)
              break;
            const auto *SLJ = dyn_cast<StringLiteral>(Init->IgnoreImpCasts());
            if (!SLJ || SLJ->getNumConcatenated() > 1) {
              OnlyOneMissingComma = false;
              break;
            }
          }

          if (OnlyOneMissingComma) {
            SmallVector<FixItHint, 1> Hints;
            for (unsigned i = 0; i < NumConcat - 1; ++i)
              Hints.push_back(FixItHint::CreateInsertion(
                  PP.getLocForEndOfToken(SL->getStrTokenLoc(i)), ","));

            Diag(SL->getStrTokenLoc(1),
                 diag::warn_concatenated_literal_array_init)
                << Hints;
            Diag(SL->getBeginLoc(),
                 diag::note_concatenated_string_literal_silence);
          }
          // In any case, stop now.
          break;
        }
      }
  }


  QualType type = var->getType();

  if (var->hasAttr<BlocksAttr>())
    getCurFunction()->addByrefBlockVar(var);

  Expr *Init = var->getInit();
  bool GlobalStorage = var->hasGlobalStorage();
  bool IsGlobal = GlobalStorage && !var->isStaticLocal();
  QualType baseType = Context.getBaseElementType(type);
  bool HasConstInit = true;

  if (getLangOpts().C23 && var->isConstexpr() && !Init)
    Diag(var->getLocation(), diag::err_constexpr_var_requires_const_init)
        << var;

  // Check whether the initializer is sufficiently constant.
  if ((getLangOpts().CPlusPlus || (getLangOpts().C23 && var->isConstexpr())) &&
      !type->isDependentType() && Init && !Init->isValueDependent() &&
      (GlobalStorage || var->isConstexpr() ||
       var->mightBeUsableInConstantExpressions(Context))) {
    // If this variable might have a constant initializer or might be usable in
    // constant expressions, check whether or not it actually is now.  We can't
    // do this lazily, because the result might depend on things that change
    // later, such as which constexpr functions happen to be defined.
    SmallVector<PartialDiagnosticAt, 8> Notes;
    if (!getLangOpts().CPlusPlus11 && !getLangOpts().C23) {
      // Prior to C++11, in contexts where a constant initializer is required,
      // the set of valid constant initializers is described by syntactic rules
      // in [expr.const]p2-6.
      // FIXME: Stricter checking for these rules would be useful for constinit /
      // -Wglobal-constructors.
      HasConstInit = checkConstInit();

      // Compute and cache the constant value, and remember that we have a
      // constant initializer.
      if (HasConstInit) {
        (void)var->checkForConstantInitialization(Notes);
        Notes.clear();
      } else if (CacheCulprit) {
        Notes.emplace_back(CacheCulprit->getExprLoc(),
                           PDiag(diag::note_invalid_subexpr_in_const_expr));
        Notes.back().second << CacheCulprit->getSourceRange();
      }
    } else {
      // Evaluate the initializer to see if it's a constant initializer.
      HasConstInit = var->checkForConstantInitialization(Notes);
    }

    if (HasConstInit) {
      // FIXME: Consider replacing the initializer with a ConstantExpr.
    } else if (var->isConstexpr()) {
      SourceLocation DiagLoc = var->getLocation();
      // If the note doesn't add any useful information other than a source
      // location, fold it into the primary diagnostic.
      if (Notes.size() == 1 && Notes[0].second.getDiagID() ==
                                   diag::note_invalid_subexpr_in_const_expr) {
        DiagLoc = Notes[0].first;
        Notes.clear();
      }
      Diag(DiagLoc, diag::err_constexpr_var_requires_const_init)
          << var << Init->getSourceRange();
      for (unsigned I = 0, N = Notes.size(); I != N; ++I)
        Diag(Notes[I].first, Notes[I].second);
    } else if (GlobalStorage && var->hasAttr<ConstInitAttr>()) {
      auto *Attr = var->getAttr<ConstInitAttr>();
      Diag(var->getLocation(), diag::err_require_constant_init_failed)
          << Init->getSourceRange();
      Diag(Attr->getLocation(), diag::note_declared_required_constant_init_here)
          << Attr->getRange() << Attr->isConstinit();
      for (auto &it : Notes)
        Diag(it.first, it.second);
    } else if (IsGlobal &&
               !getDiagnostics().isIgnored(diag::warn_global_constructor,
                                           var->getLocation())) {
      // Warn about globals which don't have a constant initializer.  Don't
      // warn about globals with a non-trivial destructor because we already
      // warned about them.
      CXXRecordDecl *RD = baseType->getAsCXXRecordDecl();
      if (!(RD && !RD->hasTrivialDestructor())) {
        // checkConstInit() here permits trivial default initialization even in
        // C++11 onwards, where such an initializer is not a constant initializer
        // but nonetheless doesn't require a global constructor.
        if (!checkConstInit())
          Diag(var->getLocation(), diag::warn_global_constructor)
              << Init->getSourceRange();
      }
    }
  }

  // Apply section attributes and pragmas to global variables.
  if (GlobalStorage && var->isThisDeclarationADefinition() &&
      !inTemplateInstantiation()) {
    PragmaStack<StringLiteral *> *Stack = nullptr;
    int SectionFlags = ASTContext::PSF_Read;
    bool MSVCEnv =
        Context.getTargetInfo().getTriple().isWindowsMSVCEnvironment();
    std::optional<QualType::NonConstantStorageReason> Reason;
    if (HasConstInit &&
        !(Reason = var->getType().isNonConstantStorage(Context, true, false))) {
      Stack = &ConstSegStack;
    } else {
      SectionFlags |= ASTContext::PSF_Write;
      Stack = var->hasInit() && HasConstInit ? &DataSegStack : &BSSSegStack;
    }
    if (const SectionAttr *SA = var->getAttr<SectionAttr>()) {
      if (SA->getSyntax() == AttributeCommonInfo::AS_Declspec)
        SectionFlags |= ASTContext::PSF_Implicit;
      UnifySection(SA->getName(), SectionFlags, var);
    } else if (Stack->CurrentValue) {
      if (Stack != &ConstSegStack && MSVCEnv &&
          ConstSegStack.CurrentValue != ConstSegStack.DefaultValue &&
          var->getType().isConstQualified()) {
        assert((!Reason || Reason != QualType::NonConstantStorageReason::
                                         NonConstNonReferenceType) &&
               "This case should've already been handled elsewhere");
        Diag(var->getLocation(), diag::warn_section_msvc_compat)
                << var << ConstSegStack.CurrentValue << (int)(!HasConstInit
            ? QualType::NonConstantStorageReason::NonTrivialCtor
            : *Reason);
      }
      SectionFlags |= ASTContext::PSF_Implicit;
      auto SectionName = Stack->CurrentValue->getString();
      var->addAttr(SectionAttr::CreateImplicit(Context, SectionName,
                                               Stack->CurrentPragmaLocation,
                                               SectionAttr::Declspec_allocate));
      if (UnifySection(SectionName, SectionFlags, var))
        var->dropAttr<SectionAttr>();
    }

    // Apply the init_seg attribute if this has an initializer.  If the
    // initializer turns out to not be dynamic, we'll end up ignoring this
    // attribute.
    if (CurInitSeg && var->getInit())
      var->addAttr(InitSegAttr::CreateImplicit(Context, CurInitSeg->getString(),
                                               CurInitSegLoc));
  }

  // All the following checks are C++ only.
  if (!getLangOpts().CPlusPlus) {
    // If this variable must be emitted, add it as an initializer for the
    // current module.
    if (Context.DeclMustBeEmitted(var) && !ModuleScopes.empty())
      Context.addModuleInitializer(ModuleScopes.back().Module, var);
    return;
  }

  // Require the destructor.
  if (!type->isDependentType())
    if (const RecordType *recordType = baseType->getAs<RecordType>())
      FinalizeVarWithDestructor(var, recordType);

  // If this variable must be emitted, add it as an initializer for the current
  // module.
  if (Context.DeclMustBeEmitted(var) && !ModuleScopes.empty())
    Context.addModuleInitializer(ModuleScopes.back().Module, var);

  // Build the bindings if this is a structured binding declaration.
  if (auto *DD = dyn_cast<DecompositionDecl>(var))
    CheckCompleteDecompositionDeclaration(DD);
}

void Sema::CheckStaticLocalForDllExport(VarDecl *VD) {
  assert(VD->isStaticLocal());

  auto *FD = dyn_cast_or_null<FunctionDecl>(VD->getParentFunctionOrMethod());

  // Find outermost function when VD is in lambda function.
  while (FD && !getDLLAttr(FD) &&
         !FD->hasAttr<DLLExportStaticLocalAttr>() &&
         !FD->hasAttr<DLLImportStaticLocalAttr>()) {
    FD = dyn_cast_or_null<FunctionDecl>(FD->getParentFunctionOrMethod());
  }

  if (!FD)
    return;

  // Static locals inherit dll attributes from their function.
  if (Attr *A = getDLLAttr(FD)) {
    auto *NewAttr = cast<InheritableAttr>(A->clone(getASTContext()));
    NewAttr->setInherited(true);
    VD->addAttr(NewAttr);
  } else if (Attr *A = FD->getAttr<DLLExportStaticLocalAttr>()) {
    auto *NewAttr = DLLExportAttr::CreateImplicit(getASTContext(), *A);
    NewAttr->setInherited(true);
    VD->addAttr(NewAttr);

    // Export this function to enforce exporting this static variable even
    // if it is not used in this compilation unit.
    if (!FD->hasAttr<DLLExportAttr>())
      FD->addAttr(NewAttr);

  } else if (Attr *A = FD->getAttr<DLLImportStaticLocalAttr>()) {
    auto *NewAttr = DLLImportAttr::CreateImplicit(getASTContext(), *A);
    NewAttr->setInherited(true);
    VD->addAttr(NewAttr);
  }
}

void Sema::CheckThreadLocalForLargeAlignment(VarDecl *VD) {
  assert(VD->getTLSKind());

  // Perform TLS alignment check here after attributes attached to the variable
  // which may affect the alignment have been processed. Only perform the check
  // if the target has a maximum TLS alignment (zero means no constraints).
  if (unsigned MaxAlign = Context.getTargetInfo().getMaxTLSAlign()) {
    // Protect the check so that it's not performed on dependent types and
    // dependent alignments (we can't determine the alignment in that case).
    if (!VD->hasDependentAlignment()) {
      CharUnits MaxAlignChars = Context.toCharUnitsFromBits(MaxAlign);
      if (Context.getDeclAlign(VD) > MaxAlignChars) {
        Diag(VD->getLocation(), diag::err_tls_var_aligned_over_maximum)
            << (unsigned)Context.getDeclAlign(VD).getQuantity() << VD
            << (unsigned)MaxAlignChars.getQuantity();
      }
    }
  }
}

void Sema::FinalizeDeclaration(Decl *ThisDecl) {
  // Note that we are no longer parsing the initializer for this declaration.
  ParsingInitForAutoVars.erase(ThisDecl);

  VarDecl *VD = dyn_cast_or_null<VarDecl>(ThisDecl);
  if (!VD)
    return;

  // Apply an implicit SectionAttr if '#pragma clang section bss|data|rodata' is active
  if (VD->hasGlobalStorage() && VD->isThisDeclarationADefinition() &&
      !inTemplateInstantiation() && !VD->hasAttr<SectionAttr>()) {
    if (PragmaClangBSSSection.Valid)
      VD->addAttr(PragmaClangBSSSectionAttr::CreateImplicit(
          Context, PragmaClangBSSSection.SectionName,
          PragmaClangBSSSection.PragmaLocation));
    if (PragmaClangDataSection.Valid)
      VD->addAttr(PragmaClangDataSectionAttr::CreateImplicit(
          Context, PragmaClangDataSection.SectionName,
          PragmaClangDataSection.PragmaLocation));
    if (PragmaClangRodataSection.Valid)
      VD->addAttr(PragmaClangRodataSectionAttr::CreateImplicit(
          Context, PragmaClangRodataSection.SectionName,
          PragmaClangRodataSection.PragmaLocation));
    if (PragmaClangRelroSection.Valid)
      VD->addAttr(PragmaClangRelroSectionAttr::CreateImplicit(
          Context, PragmaClangRelroSection.SectionName,
          PragmaClangRelroSection.PragmaLocation));
  }

  if (auto *DD = dyn_cast<DecompositionDecl>(ThisDecl)) {
    for (auto *BD : DD->bindings()) {
      FinalizeDeclaration(BD);
    }
  }

  checkAttributesAfterMerging(*this, *VD);

  if (VD->isStaticLocal())
    CheckStaticLocalForDllExport(VD);

  if (VD->getTLSKind())
    CheckThreadLocalForLargeAlignment(VD);

  // Perform check for initializers of device-side global variables.
  // CUDA allows empty constructors as initializers (see E.2.3.1, CUDA
  // 7.5). We must also apply the same checks to all __shared__
  // variables whether they are local or not. CUDA also allows
  // constant initializers for __constant__ and __device__ variables.
  if (getLangOpts().CUDA)
    CUDA().checkAllowedInitializer(VD);

  // Grab the dllimport or dllexport attribute off of the VarDecl.
  const InheritableAttr *DLLAttr = getDLLAttr(VD);

  // Imported static data members cannot be defined out-of-line.
  if (const auto *IA = dyn_cast_or_null<DLLImportAttr>(DLLAttr)) {
    if (VD->isStaticDataMember() && VD->isOutOfLine() &&
        VD->isThisDeclarationADefinition()) {
      // We allow definitions of dllimport class template static data members
      // with a warning.
      CXXRecordDecl *Context =
        cast<CXXRecordDecl>(VD->getFirstDecl()->getDeclContext());
      bool IsClassTemplateMember =
          isa<ClassTemplatePartialSpecializationDecl>(Context) ||
          Context->getDescribedClassTemplate();

      Diag(VD->getLocation(),
           IsClassTemplateMember
               ? diag::warn_attribute_dllimport_static_field_definition
               : diag::err_attribute_dllimport_static_field_definition);
      Diag(IA->getLocation(), diag::note_attribute);
      if (!IsClassTemplateMember)
        VD->setInvalidDecl();
    }
  }

  // dllimport/dllexport variables cannot be thread local, their TLS index
  // isn't exported with the variable.
  if (DLLAttr && VD->getTLSKind()) {
    auto *F = dyn_cast_or_null<FunctionDecl>(VD->getParentFunctionOrMethod());
    if (F && getDLLAttr(F)) {
      assert(VD->isStaticLocal());
      // But if this is a static local in a dlimport/dllexport function, the
      // function will never be inlined, which means the var would never be
      // imported, so having it marked import/export is safe.
    } else {
      Diag(VD->getLocation(), diag::err_attribute_dll_thread_local) << VD
                                                                    << DLLAttr;
      VD->setInvalidDecl();
    }
  }

  if (UsedAttr *Attr = VD->getAttr<UsedAttr>()) {
    if (!Attr->isInherited() && !VD->isThisDeclarationADefinition()) {
      Diag(Attr->getLocation(), diag::warn_attribute_ignored_on_non_definition)
          << Attr;
      VD->dropAttr<UsedAttr>();
    }
  }
  if (RetainAttr *Attr = VD->getAttr<RetainAttr>()) {
    if (!Attr->isInherited() && !VD->isThisDeclarationADefinition()) {
      Diag(Attr->getLocation(), diag::warn_attribute_ignored_on_non_definition)
          << Attr;
      VD->dropAttr<RetainAttr>();
    }
  }

  const DeclContext *DC = VD->getDeclContext();
  // If there's a #pragma GCC visibility in scope, and this isn't a class
  // member, set the visibility of this variable.
  if (DC->getRedeclContext()->isFileContext() && VD->isExternallyVisible())
    AddPushedVisibilityAttribute(VD);

  // FIXME: Warn on unused var template partial specializations.
  if (VD->isFileVarDecl() && !isa<VarTemplatePartialSpecializationDecl>(VD))
    MarkUnusedFileScopedDecl(VD);

  // Now we have parsed the initializer and can update the table of magic
  // tag values.
  if (!VD->hasAttr<TypeTagForDatatypeAttr>() ||
      !VD->getType()->isIntegralOrEnumerationType())
    return;

  for (const auto *I : ThisDecl->specific_attrs<TypeTagForDatatypeAttr>()) {
    const Expr *MagicValueExpr = VD->getInit();
    if (!MagicValueExpr) {
      continue;
    }
    std::optional<llvm::APSInt> MagicValueInt;
    if (!(MagicValueInt = MagicValueExpr->getIntegerConstantExpr(Context))) {
      Diag(I->getRange().getBegin(),
           diag::err_type_tag_for_datatype_not_ice)
        << LangOpts.CPlusPlus << MagicValueExpr->getSourceRange();
      continue;
    }
    if (MagicValueInt->getActiveBits() > 64) {
      Diag(I->getRange().getBegin(),
           diag::err_type_tag_for_datatype_too_large)
        << LangOpts.CPlusPlus << MagicValueExpr->getSourceRange();
      continue;
    }
    uint64_t MagicValue = MagicValueInt->getZExtValue();
    RegisterTypeTagForDatatype(I->getArgumentKind(),
                               MagicValue,
                               I->getMatchingCType(),
                               I->getLayoutCompatible(),
                               I->getMustBeNull());
  }
}

static bool hasDeducedAuto(DeclaratorDecl *DD) {
  auto *VD = dyn_cast<VarDecl>(DD);
  return VD && !VD->getType()->hasAutoForTrailingReturnType();
}

Sema::DeclGroupPtrTy Sema::FinalizeDeclaratorGroup(Scope *S, const DeclSpec &DS,
                                                   ArrayRef<Decl *> Group) {
  SmallVector<Decl*, 8> Decls;

  if (DS.isTypeSpecOwned())
    Decls.push_back(DS.getRepAsDecl());

  DeclaratorDecl *FirstDeclaratorInGroup = nullptr;
  DecompositionDecl *FirstDecompDeclaratorInGroup = nullptr;
  bool DiagnosedMultipleDecomps = false;
  DeclaratorDecl *FirstNonDeducedAutoInGroup = nullptr;
  bool DiagnosedNonDeducedAuto = false;

  for (Decl *D : Group) {
    if (!D)
      continue;
    // Check if the Decl has been declared in '#pragma omp declare target'
    // directive and has static storage duration.
    if (auto *VD = dyn_cast<VarDecl>(D);
        LangOpts.OpenMP && VD && VD->hasAttr<OMPDeclareTargetDeclAttr>() &&
        VD->hasGlobalStorage())
      OpenMP().ActOnOpenMPDeclareTargetInitializer(D);
    // For declarators, there are some additional syntactic-ish checks we need
    // to perform.
    if (auto *DD = dyn_cast<DeclaratorDecl>(D)) {
      if (!FirstDeclaratorInGroup)
        FirstDeclaratorInGroup = DD;
      if (!FirstDecompDeclaratorInGroup)
        FirstDecompDeclaratorInGroup = dyn_cast<DecompositionDecl>(D);
      if (!FirstNonDeducedAutoInGroup && DS.hasAutoTypeSpec() &&
          !hasDeducedAuto(DD))
        FirstNonDeducedAutoInGroup = DD;

      if (FirstDeclaratorInGroup != DD) {
        // A decomposition declaration cannot be combined with any other
        // declaration in the same group.
        if (FirstDecompDeclaratorInGroup && !DiagnosedMultipleDecomps) {
          Diag(FirstDecompDeclaratorInGroup->getLocation(),
               diag::err_decomp_decl_not_alone)
              << FirstDeclaratorInGroup->getSourceRange()
              << DD->getSourceRange();
          DiagnosedMultipleDecomps = true;
        }

        // A declarator that uses 'auto' in any way other than to declare a
        // variable with a deduced type cannot be combined with any other
        // declarator in the same group.
        if (FirstNonDeducedAutoInGroup && !DiagnosedNonDeducedAuto) {
          Diag(FirstNonDeducedAutoInGroup->getLocation(),
               diag::err_auto_non_deduced_not_alone)
              << FirstNonDeducedAutoInGroup->getType()
                     ->hasAutoForTrailingReturnType()
              << FirstDeclaratorInGroup->getSourceRange()
              << DD->getSourceRange();
          DiagnosedNonDeducedAuto = true;
        }
      }
    }

    Decls.push_back(D);
  }

  if (DeclSpec::isDeclRep(DS.getTypeSpecType())) {
    if (TagDecl *Tag = dyn_cast_or_null<TagDecl>(DS.getRepAsDecl())) {
      handleTagNumbering(Tag, S);
      if (FirstDeclaratorInGroup && !Tag->hasNameForLinkage() &&
          getLangOpts().CPlusPlus)
        Context.addDeclaratorForUnnamedTagDecl(Tag, FirstDeclaratorInGroup);
    }
  }

  return BuildDeclaratorGroup(Decls);
}

Sema::DeclGroupPtrTy
Sema::BuildDeclaratorGroup(MutableArrayRef<Decl *> Group) {
  // C++14 [dcl.spec.auto]p7: (DR1347)
  //   If the type that replaces the placeholder type is not the same in each
  //   deduction, the program is ill-formed.
  if (Group.size() > 1) {
    QualType Deduced;
    VarDecl *DeducedDecl = nullptr;
    for (unsigned i = 0, e = Group.size(); i != e; ++i) {
      VarDecl *D = dyn_cast<VarDecl>(Group[i]);
      if (!D || D->isInvalidDecl())
        break;
      DeducedType *DT = D->getType()->getContainedDeducedType();
      if (!DT || DT->getDeducedType().isNull())
        continue;
      if (Deduced.isNull()) {
        Deduced = DT->getDeducedType();
        DeducedDecl = D;
      } else if (!Context.hasSameType(DT->getDeducedType(), Deduced)) {
        auto *AT = dyn_cast<AutoType>(DT);
        auto Dia = Diag(D->getTypeSourceInfo()->getTypeLoc().getBeginLoc(),
                        diag::err_auto_different_deductions)
                   << (AT ? (unsigned)AT->getKeyword() : 3) << Deduced
                   << DeducedDecl->getDeclName() << DT->getDeducedType()
                   << D->getDeclName();
        if (DeducedDecl->hasInit())
          Dia << DeducedDecl->getInit()->getSourceRange();
        if (D->getInit())
          Dia << D->getInit()->getSourceRange();
        D->setInvalidDecl();
        break;
      }
    }
  }

  ActOnDocumentableDecls(Group);

  return DeclGroupPtrTy::make(
      DeclGroupRef::Create(Context, Group.data(), Group.size()));
}

void Sema::ActOnDocumentableDecl(Decl *D) {
  ActOnDocumentableDecls(D);
}

void Sema::ActOnDocumentableDecls(ArrayRef<Decl *> Group) {
  // Don't parse the comment if Doxygen diagnostics are ignored.
  if (Group.empty() || !Group[0])
    return;

  if (Diags.isIgnored(diag::warn_doc_param_not_found,
                      Group[0]->getLocation()) &&
      Diags.isIgnored(diag::warn_unknown_comment_command_name,
                      Group[0]->getLocation()))
    return;

  if (Group.size() >= 2) {
    // This is a decl group.  Normally it will contain only declarations
    // produced from declarator list.  But in case we have any definitions or
    // additional declaration references:
    //   'typedef struct S {} S;'
    //   'typedef struct S *S;'
    //   'struct S *pS;'
    // FinalizeDeclaratorGroup adds these as separate declarations.
    Decl *MaybeTagDecl = Group[0];
    if (MaybeTagDecl && isa<TagDecl>(MaybeTagDecl)) {
      Group = Group.slice(1);
    }
  }

  // FIMXE: We assume every Decl in the group is in the same file.
  // This is false when preprocessor constructs the group from decls in
  // different files (e. g. macros or #include).
  Context.attachCommentsToJustParsedDecls(Group, &getPreprocessor());
}

void Sema::CheckFunctionOrTemplateParamDeclarator(Scope *S, Declarator &D) {
  // Check that there are no default arguments inside the type of this
  // parameter.
  if (getLangOpts().CPlusPlus)
    CheckExtraCXXDefaultArguments(D);

  // Parameter declarators cannot be qualified (C++ [dcl.meaning]p1).
  if (D.getCXXScopeSpec().isSet()) {
    Diag(D.getIdentifierLoc(), diag::err_qualified_param_declarator)
      << D.getCXXScopeSpec().getRange();
  }

  // [dcl.meaning]p1: An unqualified-id occurring in a declarator-id shall be a
  // simple identifier except [...irrelevant cases...].
  switch (D.getName().getKind()) {
  case UnqualifiedIdKind::IK_Identifier:
    break;

  case UnqualifiedIdKind::IK_OperatorFunctionId:
  case UnqualifiedIdKind::IK_ConversionFunctionId:
  case UnqualifiedIdKind::IK_LiteralOperatorId:
  case UnqualifiedIdKind::IK_ConstructorName:
  case UnqualifiedIdKind::IK_DestructorName:
  case UnqualifiedIdKind::IK_ImplicitSelfParam:
  case UnqualifiedIdKind::IK_DeductionGuideName:
    Diag(D.getIdentifierLoc(), diag::err_bad_parameter_name)
      << GetNameForDeclarator(D).getName();
    break;

  case UnqualifiedIdKind::IK_TemplateId:
  case UnqualifiedIdKind::IK_ConstructorTemplateId:
    // GetNameForDeclarator would not produce a useful name in this case.
    Diag(D.getIdentifierLoc(), diag::err_bad_parameter_name_template_id);
    break;
  }
}

static void CheckExplicitObjectParameter(Sema &S, ParmVarDecl *P,
                                         SourceLocation ExplicitThisLoc) {
  if (!ExplicitThisLoc.isValid())
    return;
  assert(S.getLangOpts().CPlusPlus &&
         "explicit parameter in non-cplusplus mode");
  if (!S.getLangOpts().CPlusPlus23)
    S.Diag(ExplicitThisLoc, diag::err_cxx20_deducing_this)
        << P->getSourceRange();

  // C++2b [dcl.fct/7] An explicit object parameter shall not be a function
  // parameter pack.
  if (P->isParameterPack()) {
    S.Diag(P->getBeginLoc(), diag::err_explicit_object_parameter_pack)
        << P->getSourceRange();
    return;
  }
  P->setExplicitObjectParameterLoc(ExplicitThisLoc);
  if (LambdaScopeInfo *LSI = S.getCurLambda())
    LSI->ExplicitObjectParameter = P;
}

Decl *Sema::ActOnParamDeclarator(Scope *S, Declarator &D,
                                 SourceLocation ExplicitThisLoc) {
  const DeclSpec &DS = D.getDeclSpec();

  // Verify C99 6.7.5.3p2: The only SCS allowed is 'register'.

  // C++03 [dcl.stc]p2 also permits 'auto'.
  StorageClass SC = SC_None;
  if (DS.getStorageClassSpec() == DeclSpec::SCS_register) {
    SC = SC_Register;
    // In C++11, the 'register' storage class specifier is deprecated.
    // In C++17, it is not allowed, but we tolerate it as an extension.
    if (getLangOpts().CPlusPlus11) {
      Diag(DS.getStorageClassSpecLoc(),
           getLangOpts().CPlusPlus17 ? diag::ext_register_storage_class
                                     : diag::warn_deprecated_register)
        << FixItHint::CreateRemoval(DS.getStorageClassSpecLoc());
    }
  } else if (getLangOpts().CPlusPlus &&
             DS.getStorageClassSpec() == DeclSpec::SCS_auto) {
    SC = SC_Auto;
  } else if (DS.getStorageClassSpec() != DeclSpec::SCS_unspecified) {
    Diag(DS.getStorageClassSpecLoc(),
         diag::err_invalid_storage_class_in_func_decl);
    D.getMutableDeclSpec().ClearStorageClassSpecs();
  }

  if (DeclSpec::TSCS TSCS = DS.getThreadStorageClassSpec())
    Diag(DS.getThreadStorageClassSpecLoc(), diag::err_invalid_thread)
      << DeclSpec::getSpecifierName(TSCS);
  if (DS.isInlineSpecified())
    Diag(DS.getInlineSpecLoc(), diag::err_inline_non_function)
        << getLangOpts().CPlusPlus17;
  if (DS.hasConstexprSpecifier())
    Diag(DS.getConstexprSpecLoc(), diag::err_invalid_constexpr)
        << 0 << static_cast<int>(D.getDeclSpec().getConstexprSpecifier());

  DiagnoseFunctionSpecifiers(DS);

  CheckFunctionOrTemplateParamDeclarator(S, D);

  TypeSourceInfo *TInfo = GetTypeForDeclarator(D);
  QualType parmDeclType = TInfo->getType();

  // Check for redeclaration of parameters, e.g. int foo(int x, int x);
  const IdentifierInfo *II = D.getIdentifier();
  if (II) {
    LookupResult R(*this, II, D.getIdentifierLoc(), LookupOrdinaryName,
                   RedeclarationKind::ForVisibleRedeclaration);
    LookupName(R, S);
    if (!R.empty()) {
      NamedDecl *PrevDecl = *R.begin();
      if (R.isSingleResult() && PrevDecl->isTemplateParameter()) {
        // Maybe we will complain about the shadowed template parameter.
        DiagnoseTemplateParameterShadow(D.getIdentifierLoc(), PrevDecl);
        // Just pretend that we didn't see the previous declaration.
        PrevDecl = nullptr;
      }
      if (PrevDecl && S->isDeclScope(PrevDecl)) {
        Diag(D.getIdentifierLoc(), diag::err_param_redefinition) << II;
        Diag(PrevDecl->getLocation(), diag::note_previous_declaration);
        // Recover by removing the name
        II = nullptr;
        D.SetIdentifier(nullptr, D.getIdentifierLoc());
        D.setInvalidType(true);
      }
    }
  }

  // Temporarily put parameter variables in the translation unit, not
  // the enclosing context.  This prevents them from accidentally
  // looking like class members in C++.
  ParmVarDecl *New =
      CheckParameter(Context.getTranslationUnitDecl(), D.getBeginLoc(),
                     D.getIdentifierLoc(), II, parmDeclType, TInfo, SC);

  if (D.isInvalidType())
    New->setInvalidDecl();

  CheckExplicitObjectParameter(*this, New, ExplicitThisLoc);

  assert(S->isFunctionPrototypeScope());
  assert(S->getFunctionPrototypeDepth() >= 1);
  New->setScopeInfo(S->getFunctionPrototypeDepth() - 1,
                    S->getNextFunctionPrototypeIndex());

  // Add the parameter declaration into this scope.
  S->AddDecl(New);
  if (II)
    IdResolver.AddDecl(New);

  ProcessDeclAttributes(S, New, D);

  if (D.getDeclSpec().isModulePrivateSpecified())
    Diag(New->getLocation(), diag::err_module_private_local)
        << 1 << New << SourceRange(D.getDeclSpec().getModulePrivateSpecLoc())
        << FixItHint::CreateRemoval(D.getDeclSpec().getModulePrivateSpecLoc());

  if (New->hasAttr<BlocksAttr>()) {
    Diag(New->getLocation(), diag::err_block_on_nonlocal);
  }

  if (getLangOpts().OpenCL)
    deduceOpenCLAddressSpace(New);

  return New;
}

ParmVarDecl *Sema::BuildParmVarDeclForTypedef(DeclContext *DC,
                                              SourceLocation Loc,
                                              QualType T) {
  /* FIXME: setting StartLoc == Loc.
     Would it be worth to modify callers so as to provide proper source
     location for the unnamed parameters, embedding the parameter's type? */
  ParmVarDecl *Param = ParmVarDecl::Create(Context, DC, Loc, Loc, nullptr,
                                T, Context.getTrivialTypeSourceInfo(T, Loc),
                                           SC_None, nullptr);
  Param->setImplicit();
  return Param;
}

void Sema::DiagnoseUnusedParameters(ArrayRef<ParmVarDecl *> Parameters) {
  // Don't diagnose unused-parameter errors in template instantiations; we
  // will already have done so in the template itself.
  if (inTemplateInstantiation())
    return;

  for (const ParmVarDecl *Parameter : Parameters) {
    if (!Parameter->isReferenced() && Parameter->getDeclName() &&
        !Parameter->hasAttr<UnusedAttr>() &&
        !Parameter->getIdentifier()->isPlaceholder()) {
      Diag(Parameter->getLocation(), diag::warn_unused_parameter)
        << Parameter->getDeclName();
    }
  }
}

void Sema::DiagnoseSizeOfParametersAndReturnValue(
    ArrayRef<ParmVarDecl *> Parameters, QualType ReturnTy, NamedDecl *D) {
  if (LangOpts.NumLargeByValueCopy == 0) // No check.
    return;

  // Warn if the return value is pass-by-value and larger than the specified
  // threshold.
  if (!ReturnTy->isDependentType() && ReturnTy.isPODType(Context)) {
    unsigned Size = Context.getTypeSizeInChars(ReturnTy).getQuantity();
    if (Size > LangOpts.NumLargeByValueCopy)
      Diag(D->getLocation(), diag::warn_return_value_size) << D << Size;
  }

  // Warn if any parameter is pass-by-value and larger than the specified
  // threshold.
  for (const ParmVarDecl *Parameter : Parameters) {
    QualType T = Parameter->getType();
    if (T->isDependentType() || !T.isPODType(Context))
      continue;
    unsigned Size = Context.getTypeSizeInChars(T).getQuantity();
    if (Size > LangOpts.NumLargeByValueCopy)
      Diag(Parameter->getLocation(), diag::warn_parameter_size)
          << Parameter << Size;
  }
}

ParmVarDecl *Sema::CheckParameter(DeclContext *DC, SourceLocation StartLoc,
                                  SourceLocation NameLoc,
                                  const IdentifierInfo *Name, QualType T,
                                  TypeSourceInfo *TSInfo, StorageClass SC) {
  // In ARC, infer a lifetime qualifier for appropriate parameter types.
  if (getLangOpts().ObjCAutoRefCount &&
      T.getObjCLifetime() == Qualifiers::OCL_None &&
      T->isObjCLifetimeType()) {

    Qualifiers::ObjCLifetime lifetime;

    // Special cases for arrays:
    //   - if it's const, use __unsafe_unretained
    //   - otherwise, it's an error
    if (T->isArrayType()) {
      if (!T.isConstQualified()) {
        if (DelayedDiagnostics.shouldDelayDiagnostics())
          DelayedDiagnostics.add(
              sema::DelayedDiagnostic::makeForbiddenType(
              NameLoc, diag::err_arc_array_param_no_ownership, T, false));
        else
          Diag(NameLoc, diag::err_arc_array_param_no_ownership)
              << TSInfo->getTypeLoc().getSourceRange();
      }
      lifetime = Qualifiers::OCL_ExplicitNone;
    } else {
      lifetime = T->getObjCARCImplicitLifetime();
    }
    T = Context.getLifetimeQualifiedType(T, lifetime);
  }

  ParmVarDecl *New = ParmVarDecl::Create(Context, DC, StartLoc, NameLoc, Name,
                                         Context.getAdjustedParameterType(T),
                                         TSInfo, SC, nullptr);

  // Make a note if we created a new pack in the scope of a lambda, so that
  // we know that references to that pack must also be expanded within the
  // lambda scope.
  if (New->isParameterPack())
    if (auto *LSI = getEnclosingLambda())
      LSI->LocalPacks.push_back(New);

  if (New->getType().hasNonTrivialToPrimitiveDestructCUnion() ||
      New->getType().hasNonTrivialToPrimitiveCopyCUnion())
    checkNonTrivialCUnion(New->getType(), New->getLocation(),
                          NTCUC_FunctionParam, NTCUK_Destruct|NTCUK_Copy);

  // Parameter declarators cannot be interface types. All ObjC objects are
  // passed by reference.
  if (T->isObjCObjectType()) {
    SourceLocation TypeEndLoc =
        getLocForEndOfToken(TSInfo->getTypeLoc().getEndLoc());
    Diag(NameLoc,
         diag::err_object_cannot_be_passed_returned_by_value) << 1 << T
      << FixItHint::CreateInsertion(TypeEndLoc, "*");
    T = Context.getObjCObjectPointerType(T);
    New->setType(T);
  }

  // ISO/IEC TR 18037 S6.7.3: "The type of an object with automatic storage
  // duration shall not be qualified by an address-space qualifier."
  // Since all parameters have automatic store duration, they can not have
  // an address space.
  if (T.getAddressSpace() != LangAS::Default &&
      // OpenCL allows function arguments declared to be an array of a type
      // to be qualified with an address space.
      !(getLangOpts().OpenCL &&
        (T->isArrayType() || T.getAddressSpace() == LangAS::opencl_private)) &&
      // WebAssembly allows reference types as parameters. Funcref in particular
      // lives in a different address space.
      !(T->isFunctionPointerType() &&
        T.getAddressSpace() == LangAS::wasm_funcref)) {
    Diag(NameLoc, diag::err_arg_with_address_space);
    New->setInvalidDecl();
  }

  // PPC MMA non-pointer types are not allowed as function argument types.
  if (Context.getTargetInfo().getTriple().isPPC64() &&
      PPC().CheckPPCMMAType(New->getOriginalType(), New->getLocation())) {
    New->setInvalidDecl();
  }

  return New;
}

void Sema::ActOnFinishKNRParamDeclarations(Scope *S, Declarator &D,
                                           SourceLocation LocAfterDecls) {
  DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();

  // C99 6.9.1p6 "If a declarator includes an identifier list, each declaration
  // in the declaration list shall have at least one declarator, those
  // declarators shall only declare identifiers from the identifier list, and
  // every identifier in the identifier list shall be declared.
  //
  // C89 3.7.1p5 "If a declarator includes an identifier list, only the
  // identifiers it names shall be declared in the declaration list."
  //
  // This is why we only diagnose in C99 and later. Note, the other conditions
  // listed are checked elsewhere.
  if (!FTI.hasPrototype) {
    for (int i = FTI.NumParams; i != 0; /* decrement in loop */) {
      --i;
      if (FTI.Params[i].Param == nullptr) {
        if (getLangOpts().C99) {
          SmallString<256> Code;
          llvm::raw_svector_ostream(Code)
              << "  int " << FTI.Params[i].Ident->getName() << ";\n";
          Diag(FTI.Params[i].IdentLoc, diag::ext_param_not_declared)
              << FTI.Params[i].Ident
              << FixItHint::CreateInsertion(LocAfterDecls, Code);
        }

        // Implicitly declare the argument as type 'int' for lack of a better
        // type.
        AttributeFactory attrs;
        DeclSpec DS(attrs);
        const char* PrevSpec; // unused
        unsigned DiagID; // unused
        DS.SetTypeSpecType(DeclSpec::TST_int, FTI.Params[i].IdentLoc, PrevSpec,
                           DiagID, Context.getPrintingPolicy());
        // Use the identifier location for the type source range.
        DS.SetRangeStart(FTI.Params[i].IdentLoc);
        DS.SetRangeEnd(FTI.Params[i].IdentLoc);
        Declarator ParamD(DS, ParsedAttributesView::none(),
                          DeclaratorContext::KNRTypeList);
        ParamD.SetIdentifier(FTI.Params[i].Ident, FTI.Params[i].IdentLoc);
        FTI.Params[i].Param = ActOnParamDeclarator(S, ParamD);
      }
    }
  }
}

Decl *
Sema::ActOnStartOfFunctionDef(Scope *FnBodyScope, Declarator &D,
                              MultiTemplateParamsArg TemplateParameterLists,
                              SkipBodyInfo *SkipBody, FnBodyKind BodyKind) {
  assert(getCurFunctionDecl() == nullptr && "Function parsing confused");
  assert(D.isFunctionDeclarator() && "Not a function declarator!");
  Scope *ParentScope = FnBodyScope->getParent();

  // Check if we are in an `omp begin/end declare variant` scope. If we are, and
  // we define a non-templated function definition, we will create a declaration
  // instead (=BaseFD), and emit the definition with a mangled name afterwards.
  // The base function declaration will have the equivalent of an `omp declare
  // variant` annotation which specifies the mangled definition as a
  // specialization function under the OpenMP context defined as part of the
  // `omp begin declare variant`.
  SmallVector<FunctionDecl *, 4> Bases;
  if (LangOpts.OpenMP && OpenMP().isInOpenMPDeclareVariantScope())
    OpenMP().ActOnStartOfFunctionDefinitionInOpenMPDeclareVariantScope(
        ParentScope, D, TemplateParameterLists, Bases);

  D.setFunctionDefinitionKind(FunctionDefinitionKind::Definition);
  Decl *DP = HandleDeclarator(ParentScope, D, TemplateParameterLists);
  Decl *Dcl = ActOnStartOfFunctionDef(FnBodyScope, DP, SkipBody, BodyKind);

  if (!Bases.empty())
    OpenMP().ActOnFinishedFunctionDefinitionInOpenMPDeclareVariantScope(Dcl,
                                                                        Bases);

  return Dcl;
}

void Sema::ActOnFinishInlineFunctionDef(FunctionDecl *D) {
  Consumer.HandleInlineFunctionDefinition(D);
}

static bool FindPossiblePrototype(const FunctionDecl *FD,
                                  const FunctionDecl *&PossiblePrototype) {
  for (const FunctionDecl *Prev = FD->getPreviousDecl(); Prev;
       Prev = Prev->getPreviousDecl()) {
    // Ignore any declarations that occur in function or method
    // scope, because they aren't visible from the header.
    if (Prev->getLexicalDeclContext()->isFunctionOrMethod())
      continue;

    PossiblePrototype = Prev;
    return Prev->getType()->isFunctionProtoType();
  }
  return false;
}

static bool
ShouldWarnAboutMissingPrototype(const FunctionDecl *FD,
                                const FunctionDecl *&PossiblePrototype) {
  // Don't warn about invalid declarations.
  if (FD->isInvalidDecl())
    return false;

  // Or declarations that aren't global.
  if (!FD->isGlobal())
    return false;

  // Don't warn about C++ member functions.
  if (isa<CXXMethodDecl>(FD))
    return false;

  // Don't warn about 'main'.
  if (isa<TranslationUnitDecl>(FD->getDeclContext()->getRedeclContext()))
    if (IdentifierInfo *II = FD->getIdentifier())
      if (II->isStr("main") || II->isStr("efi_main"))
        return false;

  if (FD->isMSVCRTEntryPoint())
    return false;

  // Don't warn about inline functions.
  if (FD->isInlined())
    return false;

  // Don't warn about function templates.
  if (FD->getDescribedFunctionTemplate())
    return false;

  // Don't warn about function template specializations.
  if (FD->isFunctionTemplateSpecialization())
    return false;

  // Don't warn for OpenCL kernels.
  if (FD->hasAttr<OpenCLKernelAttr>())
    return false;

  // Don't warn on explicitly deleted functions.
  if (FD->isDeleted())
    return false;

  // Don't warn on implicitly local functions (such as having local-typed
  // parameters).
  if (!FD->isExternallyVisible())
    return false;

  // If we were able to find a potential prototype, don't warn.
  if (FindPossiblePrototype(FD, PossiblePrototype))
    return false;

  return true;
}

void
Sema::CheckForFunctionRedefinition(FunctionDecl *FD,
                                   const FunctionDecl *EffectiveDefinition,
                                   SkipBodyInfo *SkipBody) {
  const FunctionDecl *Definition = EffectiveDefinition;
  if (!Definition &&
      !FD->isDefined(Definition, /*CheckForPendingFriendDefinition*/ true))
    return;

  if (Definition->getFriendObjectKind() != Decl::FOK_None) {
    if (FunctionDecl *OrigDef = Definition->getInstantiatedFromMemberFunction()) {
      if (FunctionDecl *OrigFD = FD->getInstantiatedFromMemberFunction()) {
        // A merged copy of the same function, instantiated as a member of
        // the same class, is OK.
        if (declaresSameEntity(OrigFD, OrigDef) &&
            declaresSameEntity(cast<Decl>(Definition->getLexicalDeclContext()),
                               cast<Decl>(FD->getLexicalDeclContext())))
          return;
      }
    }
  }

  if (canRedefineFunction(Definition, getLangOpts()))
    return;

  // Don't emit an error when this is redefinition of a typo-corrected
  // definition.
  if (TypoCorrectedFunctionDefinitions.count(Definition))
    return;

  // If we don't have a visible definition of the function, and it's inline or
  // a template, skip the new definition.
  if (SkipBody && !hasVisibleDefinition(Definition) &&
      (Definition->getFormalLinkage() == Linkage::Internal ||
       Definition->isInlined() || Definition->getDescribedFunctionTemplate() ||
       Definition->getNumTemplateParameterLists())) {
    SkipBody->ShouldSkip = true;
    SkipBody->Previous = const_cast<FunctionDecl*>(Definition);
    if (auto *TD = Definition->getDescribedFunctionTemplate())
      makeMergedDefinitionVisible(TD);
    makeMergedDefinitionVisible(const_cast<FunctionDecl*>(Definition));
    return;
  }

  if (getLangOpts().GNUMode && Definition->isInlineSpecified() &&
      Definition->getStorageClass() == SC_Extern)
    Diag(FD->getLocation(), diag::err_redefinition_extern_inline)
        << FD << getLangOpts().CPlusPlus;
  else
    Diag(FD->getLocation(), diag::err_redefinition) << FD;

  Diag(Definition->getLocation(), diag::note_previous_definition);
  FD->setInvalidDecl();
}

LambdaScopeInfo *Sema::RebuildLambdaScopeInfo(CXXMethodDecl *CallOperator) {
  CXXRecordDecl *LambdaClass = CallOperator->getParent();

  LambdaScopeInfo *LSI = PushLambdaScope();
  LSI->CallOperator = CallOperator;
  LSI->Lambda = LambdaClass;
  LSI->ReturnType = CallOperator->getReturnType();
  // This function in calls in situation where the context of the call operator
  // is not entered, so we set AfterParameterList to false, so that
  // `tryCaptureVariable` finds explicit captures in the appropriate context.
  LSI->AfterParameterList = false;
  const LambdaCaptureDefault LCD = LambdaClass->getLambdaCaptureDefault();

  if (LCD == LCD_None)
    LSI->ImpCaptureStyle = CapturingScopeInfo::ImpCap_None;
  else if (LCD == LCD_ByCopy)
    LSI->ImpCaptureStyle = CapturingScopeInfo::ImpCap_LambdaByval;
  else if (LCD == LCD_ByRef)
    LSI->ImpCaptureStyle = CapturingScopeInfo::ImpCap_LambdaByref;
  DeclarationNameInfo DNI = CallOperator->getNameInfo();

  LSI->IntroducerRange = DNI.getCXXOperatorNameRange();
  LSI->Mutable = !CallOperator->isConst();
  if (CallOperator->isExplicitObjectMemberFunction())
    LSI->ExplicitObjectParameter = CallOperator->getParamDecl(0);

  // Add the captures to the LSI so they can be noted as already
  // captured within tryCaptureVar.
  auto I = LambdaClass->field_begin();
  for (const auto &C : LambdaClass->captures()) {
    if (C.capturesVariable()) {
      ValueDecl *VD = C.getCapturedVar();
      if (VD->isInitCapture())
        CurrentInstantiationScope->InstantiatedLocal(VD, VD);
      const bool ByRef = C.getCaptureKind() == LCK_ByRef;
      LSI->addCapture(VD, /*IsBlock*/false, ByRef,
          /*RefersToEnclosingVariableOrCapture*/true, C.getLocation(),
          /*EllipsisLoc*/C.isPackExpansion()
                         ? C.getEllipsisLoc() : SourceLocation(),
          I->getType(), /*Invalid*/false);

    } else if (C.capturesThis()) {
      LSI->addThisCapture(/*Nested*/ false, C.getLocation(), I->getType(),
                          C.getCaptureKind() == LCK_StarThis);
    } else {
      LSI->addVLATypeCapture(C.getLocation(), I->getCapturedVLAType(),
                             I->getType());
    }
    ++I;
  }
  return LSI;
}

Decl *Sema::ActOnStartOfFunctionDef(Scope *FnBodyScope, Decl *D,
                                    SkipBodyInfo *SkipBody,
                                    FnBodyKind BodyKind) {
  if (!D) {
    // Parsing the function declaration failed in some way. Push on a fake scope
    // anyway so we can try to parse the function body.
    PushFunctionScope();
    PushExpressionEvaluationContext(ExprEvalContexts.back().Context);
    return D;
  }

  FunctionDecl *FD = nullptr;

  if (FunctionTemplateDecl *FunTmpl = dyn_cast<FunctionTemplateDecl>(D))
    FD = FunTmpl->getTemplatedDecl();
  else
    FD = cast<FunctionDecl>(D);

  // Do not push if it is a lambda because one is already pushed when building
  // the lambda in ActOnStartOfLambdaDefinition().
  if (!isLambdaCallOperator(FD))
    // [expr.const]/p14.1
    // An expression or conversion is in an immediate function context if it is
    // potentially evaluated and either: its innermost enclosing non-block scope
    // is a function parameter scope of an immediate function.
    PushExpressionEvaluationContext(
        FD->isConsteval() ? ExpressionEvaluationContext::ImmediateFunctionContext
                          : ExprEvalContexts.back().Context);

  // Each ExpressionEvaluationContextRecord also keeps track of whether the
  // context is nested in an immediate function context, so smaller contexts
  // that appear inside immediate functions (like variable initializers) are
  // considered to be inside an immediate function context even though by
  // themselves they are not immediate function contexts. But when a new
  // function is entered, we need to reset this tracking, since the entered
  // function might be not an immediate function.
  ExprEvalContexts.back().InImmediateFunctionContext = FD->isConsteval();
  ExprEvalContexts.back().InImmediateEscalatingFunctionContext =
      getLangOpts().CPlusPlus20 && FD->isImmediateEscalating();

  // Check for defining attributes before the check for redefinition.
  if (const auto *Attr = FD->getAttr<AliasAttr>()) {
    Diag(Attr->getLocation(), diag::err_alias_is_definition) << FD << 0;
    FD->dropAttr<AliasAttr>();
    FD->setInvalidDecl();
  }
  if (const auto *Attr = FD->getAttr<IFuncAttr>()) {
    Diag(Attr->getLocation(), diag::err_alias_is_definition) << FD << 1;
    FD->dropAttr<IFuncAttr>();
    FD->setInvalidDecl();
  }
  if (const auto *Attr = FD->getAttr<TargetVersionAttr>()) {
    if (!Context.getTargetInfo().hasFeature("fmv") &&
        !Attr->isDefaultVersion()) {
      // If function multi versioning disabled skip parsing function body
      // defined with non-default target_version attribute
      if (SkipBody)
        SkipBody->ShouldSkip = true;
      return nullptr;
    }
  }

  if (auto *Ctor = dyn_cast<CXXConstructorDecl>(FD)) {
    if (Ctor->getTemplateSpecializationKind() == TSK_ExplicitSpecialization &&
        Ctor->isDefaultConstructor() &&
        Context.getTargetInfo().getCXXABI().isMicrosoft()) {
      // If this is an MS ABI dllexport default constructor, instantiate any
      // default arguments.
      InstantiateDefaultCtorDefaultArgs(Ctor);
    }
  }

  // See if this is a redefinition. If 'will have body' (or similar) is already
  // set, then these checks were already performed when it was set.
  if (!FD->willHaveBody() && !FD->isLateTemplateParsed() &&
      !FD->isThisDeclarationInstantiatedFromAFriendDefinition()) {
    CheckForFunctionRedefinition(FD, nullptr, SkipBody);

    // If we're skipping the body, we're done. Don't enter the scope.
    if (SkipBody && SkipBody->ShouldSkip)
      return D;
  }

  // Mark this function as "will have a body eventually".  This lets users to
  // call e.g. isInlineDefinitionExternallyVisible while we're still parsing
  // this function.
  FD->setWillHaveBody();

  // If we are instantiating a generic lambda call operator, push
  // a LambdaScopeInfo onto the function stack.  But use the information
  // that's already been calculated (ActOnLambdaExpr) to prime the current
  // LambdaScopeInfo.
  // When the template operator is being specialized, the LambdaScopeInfo,
  // has to be properly restored so that tryCaptureVariable doesn't try
  // and capture any new variables. In addition when calculating potential
  // captures during transformation of nested lambdas, it is necessary to
  // have the LSI properly restored.
  if (isGenericLambdaCallOperatorSpecialization(FD)) {
    // C++2c 7.5.5.2p17 A member of a closure type shall not be explicitly
    // instantiated, explicitly specialized.
    if (FD->getTemplateSpecializationInfo()
            ->isExplicitInstantiationOrSpecialization()) {
      Diag(FD->getLocation(), diag::err_lambda_explicit_spec);
      FD->setInvalidDecl();
      PushFunctionScope();
    } else {
      assert(inTemplateInstantiation() &&
             "There should be an active template instantiation on the stack "
             "when instantiating a generic lambda!");
      RebuildLambdaScopeInfo(cast<CXXMethodDecl>(D));
    }
  } else {
    // Enter a new function scope
    PushFunctionScope();
  }

  // Builtin functions cannot be defined.
  if (unsigned BuiltinID = FD->getBuiltinID()) {
    if (!Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID) &&
        !Context.BuiltinInfo.isPredefinedRuntimeFunction(BuiltinID)) {
      Diag(FD->getLocation(), diag::err_builtin_definition) << FD;
      FD->setInvalidDecl();
    }
  }

  // The return type of a function definition must be complete (C99 6.9.1p3).
  // C++23 [dcl.fct.def.general]/p2
  // The type of [...] the return for a function definition
  // shall not be a (possibly cv-qualified) class type that is incomplete
  // or abstract within the function body unless the function is deleted.
  QualType ResultType = FD->getReturnType();
  if (!ResultType->isDependentType() && !ResultType->isVoidType() &&
      !FD->isInvalidDecl() && BodyKind != FnBodyKind::Delete &&
      (RequireCompleteType(FD->getLocation(), ResultType,
                           diag::err_func_def_incomplete_result) ||
       RequireNonAbstractType(FD->getLocation(), FD->getReturnType(),
                              diag::err_abstract_type_in_decl,
                              AbstractReturnType)))
    FD->setInvalidDecl();

  if (FnBodyScope)
    PushDeclContext(FnBodyScope, FD);

  // Check the validity of our function parameters
  if (BodyKind != FnBodyKind::Delete)
    CheckParmsForFunctionDef(FD->parameters(),
                             /*CheckParameterNames=*/true);

  // Add non-parameter declarations already in the function to the current
  // scope.
  if (FnBodyScope) {
    for (Decl *NPD : FD->decls()) {
      auto *NonParmDecl = dyn_cast<NamedDecl>(NPD);
      if (!NonParmDecl)
        continue;
      assert(!isa<ParmVarDecl>(NonParmDecl) &&
             "parameters should not be in newly created FD yet");

      // If the decl has a name, make it accessible in the current scope.
      if (NonParmDecl->getDeclName())
        PushOnScopeChains(NonParmDecl, FnBodyScope, /*AddToContext=*/false);

      // Similarly, dive into enums and fish their constants out, making them
      // accessible in this scope.
      if (auto *ED = dyn_cast<EnumDecl>(NonParmDecl)) {
        for (auto *EI : ED->enumerators())
          PushOnScopeChains(EI, FnBodyScope, /*AddToContext=*/false);
      }
    }
  }

  // Introduce our parameters into the function scope
  for (auto *Param : FD->parameters()) {
    Param->setOwningFunction(FD);

    // If this has an identifier, add it to the scope stack.
    if (Param->getIdentifier() && FnBodyScope) {
      CheckShadow(FnBodyScope, Param);

      PushOnScopeChains(Param, FnBodyScope);
    }
  }

  // C++ [module.import/6] external definitions are not permitted in header
  // units.  Deleted and Defaulted functions are implicitly inline (but the
  // inline state is not set at this point, so check the BodyKind explicitly).
  // FIXME: Consider an alternate location for the test where the inlined()
  // state is complete.
  if (getLangOpts().CPlusPlusModules && currentModuleIsHeaderUnit() &&
      !FD->isInvalidDecl() && !FD->isInlined() &&
      BodyKind != FnBodyKind::Delete && BodyKind != FnBodyKind::Default &&
      FD->getFormalLinkage() == Linkage::External && !FD->isTemplated() &&
      !FD->isTemplateInstantiation()) {
    assert(FD->isThisDeclarationADefinition());
    Diag(FD->getLocation(), diag::err_extern_def_in_header_unit);
    FD->setInvalidDecl();
  }

  // Ensure that the function's exception specification is instantiated.
  if (const FunctionProtoType *FPT = FD->getType()->getAs<FunctionProtoType>())
    ResolveExceptionSpec(D->getLocation(), FPT);

  // dllimport cannot be applied to non-inline function definitions.
  if (FD->hasAttr<DLLImportAttr>() && !FD->isInlined() &&
      !FD->isTemplateInstantiation()) {
    assert(!FD->hasAttr<DLLExportAttr>());
    Diag(FD->getLocation(), diag::err_attribute_dllimport_function_definition);
    FD->setInvalidDecl();
    return D;
  }

  // Some function attributes (like OptimizeNoneAttr) need actions before
  // parsing body started.
  applyFunctionAttributesBeforeParsingBody(D);

  // We want to attach documentation to original Decl (which might be
  // a function template).
  ActOnDocumentableDecl(D);
  if (getCurLexicalContext()->isObjCContainer() &&
      getCurLexicalContext()->getDeclKind() != Decl::ObjCCategoryImpl &&
      getCurLexicalContext()->getDeclKind() != Decl::ObjCImplementation)
    Diag(FD->getLocation(), diag::warn_function_def_in_objc_container);

  return D;
}

void Sema::applyFunctionAttributesBeforeParsingBody(Decl *FD) {
  if (!FD || FD->isInvalidDecl())
    return;
  if (auto *TD = dyn_cast<FunctionTemplateDecl>(FD))
    FD = TD->getTemplatedDecl();
  if (FD && FD->hasAttr<OptimizeNoneAttr>()) {
    FPOptionsOverride FPO;
    FPO.setDisallowOptimizations();
    CurFPFeatures.applyChanges(FPO);
    FpPragmaStack.CurrentValue =
        CurFPFeatures.getChangesFrom(FPOptions(LangOpts));
  }
}

void Sema::computeNRVO(Stmt *Body, FunctionScopeInfo *Scope) {
  ReturnStmt **Returns = Scope->Returns.data();

  for (unsigned I = 0, E = Scope->Returns.size(); I != E; ++I) {
    if (const VarDecl *NRVOCandidate = Returns[I]->getNRVOCandidate()) {
      if (!NRVOCandidate->isNRVOVariable())
        Returns[I]->setNRVOCandidate(nullptr);
    }
  }
}

bool Sema::canDelayFunctionBody(const Declarator &D) {
  // We can't delay parsing the body of a constexpr function template (yet).
  if (D.getDeclSpec().hasConstexprSpecifier())
    return false;

  // We can't delay parsing the body of a function template with a deduced
  // return type (yet).
  if (D.getDeclSpec().hasAutoTypeSpec()) {
    // If the placeholder introduces a non-deduced trailing return type,
    // we can still delay parsing it.
    if (D.getNumTypeObjects()) {
      const auto &Outer = D.getTypeObject(D.getNumTypeObjects() - 1);
      if (Outer.Kind == DeclaratorChunk::Function &&
          Outer.Fun.hasTrailingReturnType()) {
        QualType Ty = GetTypeFromParser(Outer.Fun.getTrailingReturnType());
        return Ty.isNull() || !Ty->isUndeducedType();
      }
    }
    return false;
  }

  return true;
}

bool Sema::canSkipFunctionBody(Decl *D) {
  // We cannot skip the body of a function (or function template) which is
  // constexpr, since we may need to evaluate its body in order to parse the
  // rest of the file.
  // We cannot skip the body of a function with an undeduced return type,
  // because any callers of that function need to know the type.
  if (const FunctionDecl *FD = D->getAsFunction()) {
    if (FD->isConstexpr())
      return false;
    // We can't simply call Type::isUndeducedType here, because inside template
    // auto can be deduced to a dependent type, which is not considered
    // "undeduced".
    if (FD->getReturnType()->getContainedDeducedType())
      return false;
  }
  return Consumer.shouldSkipFunctionBody(D);
}

Decl *Sema::ActOnSkippedFunctionBody(Decl *Decl) {
  if (!Decl)
    return nullptr;
  if (FunctionDecl *FD = Decl->getAsFunction())
    FD->setHasSkippedBody();
  else if (ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(Decl))
    MD->setHasSkippedBody();
  return Decl;
}

Decl *Sema::ActOnFinishFunctionBody(Decl *D, Stmt *BodyArg) {
  return ActOnFinishFunctionBody(D, BodyArg, /*IsInstantiation=*/false);
}

/// RAII object that pops an ExpressionEvaluationContext when exiting a function
/// body.
class ExitFunctionBodyRAII {
public:
  ExitFunctionBodyRAII(Sema &S, bool IsLambda) : S(S), IsLambda(IsLambda) {}
  ~ExitFunctionBodyRAII() {
    if (!IsLambda)
      S.PopExpressionEvaluationContext();
  }

private:
  Sema &S;
  bool IsLambda = false;
};

static void diagnoseImplicitlyRetainedSelf(Sema &S) {
  llvm::DenseMap<const BlockDecl *, bool> EscapeInfo;

  auto IsOrNestedInEscapingBlock = [&](const BlockDecl *BD) {
    if (EscapeInfo.count(BD))
      return EscapeInfo[BD];

    bool R = false;
    const BlockDecl *CurBD = BD;

    do {
      R = !CurBD->doesNotEscape();
      if (R)
        break;
      CurBD = CurBD->getParent()->getInnermostBlockDecl();
    } while (CurBD);

    return EscapeInfo[BD] = R;
  };

  // If the location where 'self' is implicitly retained is inside a escaping
  // block, emit a diagnostic.
  for (const std::pair<SourceLocation, const BlockDecl *> &P :
       S.ImplicitlyRetainedSelfLocs)
    if (IsOrNestedInEscapingBlock(P.second))
      S.Diag(P.first, diag::warn_implicitly_retains_self)
          << FixItHint::CreateInsertion(P.first, "self->");
}

static bool methodHasName(const FunctionDecl *FD, StringRef Name) {
  return isa<CXXMethodDecl>(FD) && FD->param_empty() &&
         FD->getDeclName().isIdentifier() && FD->getName() == Name;
}

bool Sema::CanBeGetReturnObject(const FunctionDecl *FD) {
  return methodHasName(FD, "get_return_object");
}

bool Sema::CanBeGetReturnTypeOnAllocFailure(const FunctionDecl *FD) {
  return FD->isStatic() &&
         methodHasName(FD, "get_return_object_on_allocation_failure");
}

void Sema::CheckCoroutineWrapper(FunctionDecl *FD) {
  RecordDecl *RD = FD->getReturnType()->getAsRecordDecl();
  if (!RD || !RD->getUnderlyingDecl()->hasAttr<CoroReturnTypeAttr>())
    return;
  // Allow some_promise_type::get_return_object().
  if (CanBeGetReturnObject(FD) || CanBeGetReturnTypeOnAllocFailure(FD))
    return;
  if (!FD->hasAttr<CoroWrapperAttr>())
    Diag(FD->getLocation(), diag::err_coroutine_return_type) << RD;
}

Decl *Sema::ActOnFinishFunctionBody(Decl *dcl, Stmt *Body,
                                    bool IsInstantiation) {
  FunctionScopeInfo *FSI = getCurFunction();
  FunctionDecl *FD = dcl ? dcl->getAsFunction() : nullptr;

  if (FSI->UsesFPIntrin && FD && !FD->hasAttr<StrictFPAttr>())
    FD->addAttr(StrictFPAttr::CreateImplicit(Context));

  sema::AnalysisBasedWarnings::Policy WP = AnalysisWarnings.getDefaultPolicy();
  sema::AnalysisBasedWarnings::Policy *ActivePolicy = nullptr;

  // If we skip function body, we can't tell if a function is a coroutine.
  if (getLangOpts().Coroutines && FD && !FD->hasSkippedBody()) {
    if (FSI->isCoroutine())
      CheckCompletedCoroutineBody(FD, Body);
    else
      CheckCoroutineWrapper(FD);
  }

  {
    // Do not call PopExpressionEvaluationContext() if it is a lambda because
    // one is already popped when finishing the lambda in BuildLambdaExpr().
    // This is meant to pop the context added in ActOnStartOfFunctionDef().
    ExitFunctionBodyRAII ExitRAII(*this, isLambdaCallOperator(FD));
    if (FD) {
      // If this is called by Parser::ParseFunctionDefinition() after marking
      // the declaration as deleted, and if the deleted-function-body contains
      // a message (C++26), then a DefaultedOrDeletedInfo will have already been
      // added to store that message; do not overwrite it in that case.
      //
      // Since this would always set the body to 'nullptr' in that case anyway,
      // which is already done when the function decl is initially created,
      // always skipping this irrespective of whether there is a delete message
      // should not be a problem.
      if (!FD->isDeletedAsWritten())
        FD->setBody(Body);
      FD->setWillHaveBody(false);
      CheckImmediateEscalatingFunctionDefinition(FD, FSI);

      if (getLangOpts().CPlusPlus14) {
        if (!FD->isInvalidDecl() && Body && !FD->isDependentContext() &&
            FD->getReturnType()->isUndeducedType()) {
          // For a function with a deduced result type to return void,
          // the result type as written must be 'auto' or 'decltype(auto)',
          // possibly cv-qualified or constrained, but not ref-qualified.
          if (!FD->getReturnType()->getAs<AutoType>()) {
            Diag(dcl->getLocation(), diag::err_auto_fn_no_return_but_not_auto)
                << FD->getReturnType();
            FD->setInvalidDecl();
          } else {
            // Falling off the end of the function is the same as 'return;'.
            Expr *Dummy = nullptr;
            if (DeduceFunctionTypeFromReturnExpr(
                    FD, dcl->getLocation(), Dummy,
                    FD->getReturnType()->getAs<AutoType>()))
              FD->setInvalidDecl();
          }
        }
      } else if (getLangOpts().CPlusPlus && isLambdaCallOperator(FD)) {
        // In C++11, we don't use 'auto' deduction rules for lambda call
        // operators because we don't support return type deduction.
        auto *LSI = getCurLambda();
        if (LSI->HasImplicitReturnType) {
          deduceClosureReturnType(*LSI);

          // C++11 [expr.prim.lambda]p4:
          //   [...] if there are no return statements in the compound-statement
          //   [the deduced type is] the type void
          QualType RetType =
              LSI->ReturnType.isNull() ? Context.VoidTy : LSI->ReturnType;

          // Update the return type to the deduced type.
          const auto *Proto = FD->getType()->castAs<FunctionProtoType>();
          FD->setType(Context.getFunctionType(RetType, Proto->getParamTypes(),
                                              Proto->getExtProtoInfo()));
        }
      }

      // If the function implicitly returns zero (like 'main') or is naked,
      // don't complain about missing return statements.
      if (FD->hasImplicitReturnZero() || FD->hasAttr<NakedAttr>())
        WP.disableCheckFallThrough();

      // MSVC permits the use of pure specifier (=0) on function definition,
      // defined at class scope, warn about this non-standard construct.
      if (getLangOpts().MicrosoftExt && FD->isPureVirtual() &&
          !FD->isOutOfLine())
        Diag(FD->getLocation(), diag::ext_pure_function_definition);

      if (!FD->isInvalidDecl()) {
        // Don't diagnose unused parameters of defaulted, deleted or naked
        // functions.
        if (!FD->isDeleted() && !FD->isDefaulted() && !FD->hasSkippedBody() &&
            !FD->hasAttr<NakedAttr>())
          DiagnoseUnusedParameters(FD->parameters());
        DiagnoseSizeOfParametersAndReturnValue(FD->parameters(),
                                               FD->getReturnType(), FD);

        // If this is a structor, we need a vtable.
        if (CXXConstructorDecl *Constructor = dyn_cast<CXXConstructorDecl>(FD))
          MarkVTableUsed(FD->getLocation(), Constructor->getParent());
        else if (CXXDestructorDecl *Destructor =
                     dyn_cast<CXXDestructorDecl>(FD))
          MarkVTableUsed(FD->getLocation(), Destructor->getParent());

        // Try to apply the named return value optimization. We have to check
        // if we can do this here because lambdas keep return statements around
        // to deduce an implicit return type.
        if (FD->getReturnType()->isRecordType() &&
            (!getLangOpts().CPlusPlus || !FD->isDependentContext()))
          computeNRVO(Body, FSI);
      }

      // GNU warning -Wmissing-prototypes:
      //   Warn if a global function is defined without a previous
      //   prototype declaration. This warning is issued even if the
      //   definition itself provides a prototype. The aim is to detect
      //   global functions that fail to be declared in header files.
      const FunctionDecl *PossiblePrototype = nullptr;
      if (ShouldWarnAboutMissingPrototype(FD, PossiblePrototype)) {
        Diag(FD->getLocation(), diag::warn_missing_prototype) << FD;

        if (PossiblePrototype) {
          // We found a declaration that is not a prototype,
          // but that could be a zero-parameter prototype
          if (TypeSourceInfo *TI = PossiblePrototype->getTypeSourceInfo()) {
            TypeLoc TL = TI->getTypeLoc();
            if (FunctionNoProtoTypeLoc FTL = TL.getAs<FunctionNoProtoTypeLoc>())
              Diag(PossiblePrototype->getLocation(),
                   diag::note_declaration_not_a_prototype)
                  << (FD->getNumParams() != 0)
                  << (FD->getNumParams() == 0 ? FixItHint::CreateInsertion(
                                                    FTL.getRParenLoc(), "void")
                                              : FixItHint{});
          }
        } else {
          // Returns true if the token beginning at this Loc is `const`.
          auto isLocAtConst = [&](SourceLocation Loc, const SourceManager &SM,
                                  const LangOptions &LangOpts) {
            std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(Loc);
            if (LocInfo.first.isInvalid())
              return false;

            bool Invalid = false;
            StringRef Buffer = SM.getBufferData(LocInfo.first, &Invalid);
            if (Invalid)
              return false;

            if (LocInfo.second > Buffer.size())
              return false;

            const char *LexStart = Buffer.data() + LocInfo.second;
            StringRef StartTok(LexStart, Buffer.size() - LocInfo.second);

            return StartTok.consume_front("const") &&
                   (StartTok.empty() || isWhitespace(StartTok[0]) ||
                    StartTok.starts_with("/*") || StartTok.starts_with("//"));
          };

          auto findBeginLoc = [&]() {
            // If the return type has `const` qualifier, we want to insert
            // `static` before `const` (and not before the typename).
            if ((FD->getReturnType()->isAnyPointerType() &&
                 FD->getReturnType()->getPointeeType().isConstQualified()) ||
                FD->getReturnType().isConstQualified()) {
              // But only do this if we can determine where the `const` is.

              if (isLocAtConst(FD->getBeginLoc(), getSourceManager(),
                               getLangOpts()))

                return FD->getBeginLoc();
            }
            return FD->getTypeSpecStartLoc();
          };
          Diag(FD->getTypeSpecStartLoc(),
               diag::note_static_for_internal_linkage)
              << /* function */ 1
              << (FD->getStorageClass() == SC_None
                      ? FixItHint::CreateInsertion(findBeginLoc(), "static ")
                      : FixItHint{});
        }
      }

      // We might not have found a prototype because we didn't wish to warn on
      // the lack of a missing prototype. Try again without the checks for
      // whether we want to warn on the missing prototype.
      if (!PossiblePrototype)
        (void)FindPossiblePrototype(FD, PossiblePrototype);

      // If the function being defined does not have a prototype, then we may
      // need to diagnose it as changing behavior in C23 because we now know
      // whether the function accepts arguments or not. This only handles the
      // case where the definition has no prototype but does have parameters
      // and either there is no previous potential prototype, or the previous
      // potential prototype also has no actual prototype. This handles cases
      // like:
      //   void f(); void f(a) int a; {}
      //   void g(a) int a; {}
      // See MergeFunctionDecl() for other cases of the behavior change
      // diagnostic. See GetFullTypeForDeclarator() for handling of a function
      // type without a prototype.
      if (!FD->hasWrittenPrototype() && FD->getNumParams() != 0 &&
          (!PossiblePrototype || (!PossiblePrototype->hasWrittenPrototype() &&
                                  !PossiblePrototype->isImplicit()))) {
        // The function definition has parameters, so this will change behavior
        // in C23. If there is a possible prototype, it comes before the
        // function definition.
        // FIXME: The declaration may have already been diagnosed as being
        // deprecated in GetFullTypeForDeclarator() if it had no arguments, but
        // there's no way to test for the "changes behavior" condition in
        // SemaType.cpp when forming the declaration's function type. So, we do
        // this awkward dance instead.
        //
        // If we have a possible prototype and it declares a function with a
        // prototype, we don't want to diagnose it; if we have a possible
        // prototype and it has no prototype, it may have already been
        // diagnosed in SemaType.cpp as deprecated depending on whether
        // -Wstrict-prototypes is enabled. If we already warned about it being
        // deprecated, add a note that it also changes behavior. If we didn't
        // warn about it being deprecated (because the diagnostic is not
        // enabled), warn now that it is deprecated and changes behavior.

        // This K&R C function definition definitely changes behavior in C23,
        // so diagnose it.
        Diag(FD->getLocation(), diag::warn_non_prototype_changes_behavior)
            << /*definition*/ 1 << /* not supported in C23 */ 0;

        // If we have a possible prototype for the function which is a user-
        // visible declaration, we already tested that it has no prototype.
        // This will change behavior in C23. This gets a warning rather than a
        // note because it's the same behavior-changing problem as with the
        // definition.
        if (PossiblePrototype)
          Diag(PossiblePrototype->getLocation(),
               diag::warn_non_prototype_changes_behavior)
              << /*declaration*/ 0 << /* conflicting */ 1 << /*subsequent*/ 1
              << /*definition*/ 1;
      }

      // Warn on CPUDispatch with an actual body.
      if (FD->isMultiVersion() && FD->hasAttr<CPUDispatchAttr>() && Body)
        if (const auto *CmpndBody = dyn_cast<CompoundStmt>(Body))
          if (!CmpndBody->body_empty())
            Diag(CmpndBody->body_front()->getBeginLoc(),
                 diag::warn_dispatch_body_ignored);

      if (auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
        const CXXMethodDecl *KeyFunction;
        if (MD->isOutOfLine() && (MD = MD->getCanonicalDecl()) &&
            MD->isVirtual() &&
            (KeyFunction = Context.getCurrentKeyFunction(MD->getParent())) &&
            MD == KeyFunction->getCanonicalDecl()) {
          // Update the key-function state if necessary for this ABI.
          if (FD->isInlined() &&
              !Context.getTargetInfo().getCXXABI().canKeyFunctionBeInline()) {
            Context.setNonKeyFunction(MD);

            // If the newly-chosen key function is already defined, then we
            // need to mark the vtable as used retroactively.
            KeyFunction = Context.getCurrentKeyFunction(MD->getParent());
            const FunctionDecl *Definition;
            if (KeyFunction && KeyFunction->isDefined(Definition))
              MarkVTableUsed(Definition->getLocation(), MD->getParent(), true);
          } else {
            // We just defined they key function; mark the vtable as used.
            MarkVTableUsed(FD->getLocation(), MD->getParent(), true);
          }
        }
      }

      assert((FD == getCurFunctionDecl(/*AllowLambdas=*/true)) &&
             "Function parsing confused");
    } else if (ObjCMethodDecl *MD = dyn_cast_or_null<ObjCMethodDecl>(dcl)) {
      assert(MD == getCurMethodDecl() && "Method parsing confused");
      MD->setBody(Body);
      if (!MD->isInvalidDecl()) {
        DiagnoseSizeOfParametersAndReturnValue(MD->parameters(),
                                               MD->getReturnType(), MD);

        if (Body)
          computeNRVO(Body, FSI);
      }
      if (FSI->ObjCShouldCallSuper) {
        Diag(MD->getEndLoc(), diag::warn_objc_missing_super_call)
            << MD->getSelector().getAsString();
        FSI->ObjCShouldCallSuper = false;
      }
      if (FSI->ObjCWarnForNoDesignatedInitChain) {
        const ObjCMethodDecl *InitMethod = nullptr;
        bool isDesignated =
            MD->isDesignatedInitializerForTheInterface(&InitMethod);
        assert(isDesignated && InitMethod);
        (void)isDesignated;

        auto superIsNSObject = [&](const ObjCMethodDecl *MD) {
          auto IFace = MD->getClassInterface();
          if (!IFace)
            return false;
          auto SuperD = IFace->getSuperClass();
          if (!SuperD)
            return false;
          return SuperD->getIdentifier() ==
                 ObjC().NSAPIObj->getNSClassId(NSAPI::ClassId_NSObject);
        };
        // Don't issue this warning for unavailable inits or direct subclasses
        // of NSObject.
        if (!MD->isUnavailable() && !superIsNSObject(MD)) {
          Diag(MD->getLocation(),
               diag::warn_objc_designated_init_missing_super_call);
          Diag(InitMethod->getLocation(),
               diag::note_objc_designated_init_marked_here);
        }
        FSI->ObjCWarnForNoDesignatedInitChain = false;
      }
      if (FSI->ObjCWarnForNoInitDelegation) {
        // Don't issue this warning for unavailable inits.
        if (!MD->isUnavailable())
          Diag(MD->getLocation(),
               diag::warn_objc_secondary_init_missing_init_call);
        FSI->ObjCWarnForNoInitDelegation = false;
      }

      diagnoseImplicitlyRetainedSelf(*this);
    } else {
      // Parsing the function declaration failed in some way. Pop the fake scope
      // we pushed on.
      PopFunctionScopeInfo(ActivePolicy, dcl);
      return nullptr;
    }

    if (Body && FSI->HasPotentialAvailabilityViolations)
      DiagnoseUnguardedAvailabilityViolations(dcl);

    assert(!FSI->ObjCShouldCallSuper &&
           "This should only be set for ObjC methods, which should have been "
           "handled in the block above.");

    // Verify and clean out per-function state.
    if (Body && (!FD || !FD->isDefaulted())) {
      // C++ constructors that have function-try-blocks can't have return
      // statements in the handlers of that block. (C++ [except.handle]p14)
      // Verify this.
      if (FD && isa<CXXConstructorDecl>(FD) && isa<CXXTryStmt>(Body))
        DiagnoseReturnInConstructorExceptionHandler(cast<CXXTryStmt>(Body));

      // Verify that gotos and switch cases don't jump into scopes illegally.
      if (FSI->NeedsScopeChecking() && !PP.isCodeCompletionEnabled())
        DiagnoseInvalidJumps(Body);

      if (CXXDestructorDecl *Destructor = dyn_cast<CXXDestructorDecl>(dcl)) {
        if (!Destructor->getParent()->isDependentType())
          CheckDestructor(Destructor);

        MarkBaseAndMemberDestructorsReferenced(Destructor->getLocation(),
                                               Destructor->getParent());
      }

      // If any errors have occurred, clear out any temporaries that may have
      // been leftover. This ensures that these temporaries won't be picked up
      // for deletion in some later function.
      if (hasUncompilableErrorOccurred() ||
          hasAnyUnrecoverableErrorsInThisFunction() ||
          getDiagnostics().getSuppressAllDiagnostics()) {
        DiscardCleanupsInEvaluationContext();
      }
      if (!hasUncompilableErrorOccurred() && !isa<FunctionTemplateDecl>(dcl)) {
        // Since the body is valid, issue any analysis-based warnings that are
        // enabled.
        ActivePolicy = &WP;
      }

      if (!IsInstantiation && FD &&
          (FD->isConstexpr() || FD->hasAttr<MSConstexprAttr>()) &&
          !FD->isInvalidDecl() &&
          !CheckConstexprFunctionDefinition(FD, CheckConstexprKind::Diagnose))
        FD->setInvalidDecl();

      if (FD && FD->hasAttr<NakedAttr>()) {
        for (const Stmt *S : Body->children()) {
          // Allow local register variables without initializer as they don't
          // require prologue.
          bool RegisterVariables = false;
          if (auto *DS = dyn_cast<DeclStmt>(S)) {
            for (const auto *Decl : DS->decls()) {
              if (const auto *Var = dyn_cast<VarDecl>(Decl)) {
                RegisterVariables =
                    Var->hasAttr<AsmLabelAttr>() && !Var->hasInit();
                if (!RegisterVariables)
                  break;
              }
            }
          }
          if (RegisterVariables)
            continue;
          if (!isa<AsmStmt>(S) && !isa<NullStmt>(S)) {
            Diag(S->getBeginLoc(), diag::err_non_asm_stmt_in_naked_function);
            Diag(FD->getAttr<NakedAttr>()->getLocation(), diag::note_attribute);
            FD->setInvalidDecl();
            break;
          }
        }
      }

      assert(ExprCleanupObjects.size() ==
                 ExprEvalContexts.back().NumCleanupObjects &&
             "Leftover temporaries in function");
      assert(!Cleanup.exprNeedsCleanups() &&
             "Unaccounted cleanups in function");
      assert(MaybeODRUseExprs.empty() &&
             "Leftover expressions for odr-use checking");
    }
  } // Pops the ExitFunctionBodyRAII scope, which needs to happen before we pop
    // the declaration context below. Otherwise, we're unable to transform
    // 'this' expressions when transforming immediate context functions.

  if (!IsInstantiation)
    PopDeclContext();

  PopFunctionScopeInfo(ActivePolicy, dcl);
  // If any errors have occurred, clear out any temporaries that may have
  // been leftover. This ensures that these temporaries won't be picked up for
  // deletion in some later function.
  if (hasUncompilableErrorOccurred()) {
    DiscardCleanupsInEvaluationContext();
  }

  if (FD && ((LangOpts.OpenMP && (LangOpts.OpenMPIsTargetDevice ||
                                  !LangOpts.OMPTargetTriples.empty())) ||
             LangOpts.CUDA || LangOpts.SYCLIsDevice)) {
    auto ES = getEmissionStatus(FD);
    if (ES == Sema::FunctionEmissionStatus::Emitted ||
        ES == Sema::FunctionEmissionStatus::Unknown)
      DeclsToCheckForDeferredDiags.insert(FD);
  }

  if (FD && !FD->isDeleted())
    checkTypeSupport(FD->getType(), FD->getLocation(), FD);

  return dcl;
}

/// When we finish delayed parsing of an attribute, we must attach it to the
/// relevant Decl.
void Sema::ActOnFinishDelayedAttribute(Scope *S, Decl *D,
                                       ParsedAttributes &Attrs) {
  // Always attach attributes to the underlying decl.
  if (TemplateDecl *TD = dyn_cast<TemplateDecl>(D))
    D = TD->getTemplatedDecl();
  ProcessDeclAttributeList(S, D, Attrs);
  ProcessAPINotes(D);

  if (CXXMethodDecl *Method = dyn_cast_or_null<CXXMethodDecl>(D))
    if (Method->isStatic())
      checkThisInStaticMemberFunctionAttributes(Method);
}

NamedDecl *Sema::ImplicitlyDefineFunction(SourceLocation Loc,
                                          IdentifierInfo &II, Scope *S) {
  // It is not valid to implicitly define a function in C23.
  assert(LangOpts.implicitFunctionsAllowed() &&
         "Implicit function declarations aren't allowed in this language mode");

  // Find the scope in which the identifier is injected and the corresponding
  // DeclContext.
  // FIXME: C89 does not say what happens if there is no enclosing block scope.
  // In that case, we inject the declaration into the translation unit scope
  // instead.
  Scope *BlockScope = S;
  while (!BlockScope->isCompoundStmtScope() && BlockScope->getParent())
    BlockScope = BlockScope->getParent();

  // Loop until we find a DeclContext that is either a function/method or the
  // translation unit, which are the only two valid places to implicitly define
  // a function. This avoids accidentally defining the function within a tag
  // declaration, for example.
  Scope *ContextScope = BlockScope;
  while (!ContextScope->getEntity() ||
         (!ContextScope->getEntity()->isFunctionOrMethod() &&
          !ContextScope->getEntity()->isTranslationUnit()))
    ContextScope = ContextScope->getParent();
  ContextRAII SavedContext(*this, ContextScope->getEntity());

  // Before we produce a declaration for an implicitly defined
  // function, see whether there was a locally-scoped declaration of
  // this name as a function or variable. If so, use that
  // (non-visible) declaration, and complain about it.
  NamedDecl *ExternCPrev = findLocallyScopedExternCDecl(&II);
  if (ExternCPrev) {
    // We still need to inject the function into the enclosing block scope so
    // that later (non-call) uses can see it.
    PushOnScopeChains(ExternCPrev, BlockScope, /*AddToContext*/false);

    // C89 footnote 38:
    //   If in fact it is not defined as having type "function returning int",
    //   the behavior is undefined.
    if (!isa<FunctionDecl>(ExternCPrev) ||
        !Context.typesAreCompatible(
            cast<FunctionDecl>(ExternCPrev)->getType(),
            Context.getFunctionNoProtoType(Context.IntTy))) {
      Diag(Loc, diag::ext_use_out_of_scope_declaration)
          << ExternCPrev << !getLangOpts().C99;
      Diag(ExternCPrev->getLocation(), diag::note_previous_declaration);
      return ExternCPrev;
    }
  }

  // Extension in C99 (defaults to error). Legal in C89, but warn about it.
  unsigned diag_id;
  if (II.getName().starts_with("__builtin_"))
    diag_id = diag::warn_builtin_unknown;
  // OpenCL v2.0 s6.9.u - Implicit function declaration is not supported.
  else if (getLangOpts().C99)
    diag_id = diag::ext_implicit_function_decl_c99;
  else
    diag_id = diag::warn_implicit_function_decl;

  TypoCorrection Corrected;
  // Because typo correction is expensive, only do it if the implicit
  // function declaration is going to be treated as an error.
  //
  // Perform the correction before issuing the main diagnostic, as some
  // consumers use typo-correction callbacks to enhance the main diagnostic.
  if (S && !ExternCPrev &&
      (Diags.getDiagnosticLevel(diag_id, Loc) >= DiagnosticsEngine::Error)) {
    DeclFilterCCC<FunctionDecl> CCC{};
    Corrected = CorrectTypo(DeclarationNameInfo(&II, Loc), LookupOrdinaryName,
                            S, nullptr, CCC, CTK_NonError);
  }

  Diag(Loc, diag_id) << &II;
  if (Corrected) {
    // If the correction is going to suggest an implicitly defined function,
    // skip the correction as not being a particularly good idea.
    bool Diagnose = true;
    if (const auto *D = Corrected.getCorrectionDecl())
      Diagnose = !D->isImplicit();
    if (Diagnose)
      diagnoseTypo(Corrected, PDiag(diag::note_function_suggestion),
                   /*ErrorRecovery*/ false);
  }

  // If we found a prior declaration of this function, don't bother building
  // another one. We've already pushed that one into scope, so there's nothing
  // more to do.
  if (ExternCPrev)
    return ExternCPrev;

  // Set a Declarator for the implicit definition: int foo();
  const char *Dummy;
  AttributeFactory attrFactory;
  DeclSpec DS(attrFactory);
  unsigned DiagID;
  bool Error = DS.SetTypeSpecType(DeclSpec::TST_int, Loc, Dummy, DiagID,
                                  Context.getPrintingPolicy());
  (void)Error; // Silence warning.
  assert(!Error && "Error setting up implicit decl!");
  SourceLocation NoLoc;
  Declarator D(DS, ParsedAttributesView::none(), DeclaratorContext::Block);
  D.AddTypeInfo(DeclaratorChunk::getFunction(/*HasProto=*/false,
                                             /*IsAmbiguous=*/false,
                                             /*LParenLoc=*/NoLoc,
                                             /*Params=*/nullptr,
                                             /*NumParams=*/0,
                                             /*EllipsisLoc=*/NoLoc,
                                             /*RParenLoc=*/NoLoc,
                                             /*RefQualifierIsLvalueRef=*/true,
                                             /*RefQualifierLoc=*/NoLoc,
                                             /*MutableLoc=*/NoLoc, EST_None,
                                             /*ESpecRange=*/SourceRange(),
                                             /*Exceptions=*/nullptr,
                                             /*ExceptionRanges=*/nullptr,
                                             /*NumExceptions=*/0,
                                             /*NoexceptExpr=*/nullptr,
                                             /*ExceptionSpecTokens=*/nullptr,
                                             /*DeclsInPrototype=*/std::nullopt,
                                             Loc, Loc, D),
                std::move(DS.getAttributes()), SourceLocation());
  D.SetIdentifier(&II, Loc);

  // Insert this function into the enclosing block scope.
  FunctionDecl *FD = cast<FunctionDecl>(ActOnDeclarator(BlockScope, D));
  FD->setImplicit();

  AddKnownFunctionAttributes(FD);

  return FD;
}

void Sema::AddKnownFunctionAttributesForReplaceableGlobalAllocationFunction(
    FunctionDecl *FD) {
  if (FD->isInvalidDecl())
    return;

  if (FD->getDeclName().getCXXOverloadedOperator() != OO_New &&
      FD->getDeclName().getCXXOverloadedOperator() != OO_Array_New)
    return;

  std::optional<unsigned> AlignmentParam;
  bool IsNothrow = false;
  if (!FD->isReplaceableGlobalAllocationFunction(&AlignmentParam, &IsNothrow))
    return;

  // C++2a [basic.stc.dynamic.allocation]p4:
  //   An allocation function that has a non-throwing exception specification
  //   indicates failure by returning a null pointer value. Any other allocation
  //   function never returns a null pointer value and indicates failure only by
  //   throwing an exception [...]
  //
  // However, -fcheck-new invalidates this possible assumption, so don't add
  // NonNull when that is enabled.
  if (!IsNothrow && !FD->hasAttr<ReturnsNonNullAttr>() &&
      !getLangOpts().CheckNew)
    FD->addAttr(ReturnsNonNullAttr::CreateImplicit(Context, FD->getLocation()));

  // C++2a [basic.stc.dynamic.allocation]p2:
  //   An allocation function attempts to allocate the requested amount of
  //   storage. [...] If the request succeeds, the value returned by a
  //   replaceable allocation function is a [...] pointer value p0 different
  //   from any previously returned value p1 [...]
  //
  // However, this particular information is being added in codegen,
  // because there is an opt-out switch for it (-fno-assume-sane-operator-new)

  // C++2a [basic.stc.dynamic.allocation]p2:
  //   An allocation function attempts to allocate the requested amount of
  //   storage. If it is successful, it returns the address of the start of a
  //   block of storage whose length in bytes is at least as large as the
  //   requested size.
  if (!FD->hasAttr<AllocSizeAttr>()) {
    FD->addAttr(AllocSizeAttr::CreateImplicit(
        Context, /*ElemSizeParam=*/ParamIdx(1, FD),
        /*NumElemsParam=*/ParamIdx(), FD->getLocation()));
  }

  // C++2a [basic.stc.dynamic.allocation]p3:
  //   For an allocation function [...], the pointer returned on a successful
  //   call shall represent the address of storage that is aligned as follows:
  //   (3.1) If the allocation function takes an argument of type
  //         std​::​align_­val_­t, the storage will have the alignment
  //         specified by the value of this argument.
  if (AlignmentParam && !FD->hasAttr<AllocAlignAttr>()) {
    FD->addAttr(AllocAlignAttr::CreateImplicit(
        Context, ParamIdx(*AlignmentParam, FD), FD->getLocation()));
  }

  // FIXME:
  // C++2a [basic.stc.dynamic.allocation]p3:
  //   For an allocation function [...], the pointer returned on a successful
  //   call shall represent the address of storage that is aligned as follows:
  //   (3.2) Otherwise, if the allocation function is named operator new[],
  //         the storage is aligned for any object that does not have
  //         new-extended alignment ([basic.align]) and is no larger than the
  //         requested size.
  //   (3.3) Otherwise, the storage is aligned for any object that does not
  //         have new-extended alignment and is of the requested size.
}

void Sema::AddKnownFunctionAttributes(FunctionDecl *FD) {
  if (FD->isInvalidDecl())
    return;

  // If this is a built-in function, map its builtin attributes to
  // actual attributes.
  if (unsigned BuiltinID = FD->getBuiltinID()) {
    // Handle printf-formatting attributes.
    unsigned FormatIdx;
    bool HasVAListArg;
    if (Context.BuiltinInfo.isPrintfLike(BuiltinID, FormatIdx, HasVAListArg)) {
      if (!FD->hasAttr<FormatAttr>()) {
        const char *fmt = "printf";
        unsigned int NumParams = FD->getNumParams();
        if (FormatIdx < NumParams && // NumParams may be 0 (e.g. vfprintf)
            FD->getParamDecl(FormatIdx)->getType()->isObjCObjectPointerType())
          fmt = "NSString";
        FD->addAttr(FormatAttr::CreateImplicit(Context,
                                               &Context.Idents.get(fmt),
                                               FormatIdx+1,
                                               HasVAListArg ? 0 : FormatIdx+2,
                                               FD->getLocation()));
      }
    }
    if (Context.BuiltinInfo.isScanfLike(BuiltinID, FormatIdx,
                                             HasVAListArg)) {
     if (!FD->hasAttr<FormatAttr>())
       FD->addAttr(FormatAttr::CreateImplicit(Context,
                                              &Context.Idents.get("scanf"),
                                              FormatIdx+1,
                                              HasVAListArg ? 0 : FormatIdx+2,
                                              FD->getLocation()));
    }

    // Handle automatically recognized callbacks.
    SmallVector<int, 4> Encoding;
    if (!FD->hasAttr<CallbackAttr>() &&
        Context.BuiltinInfo.performsCallback(BuiltinID, Encoding))
      FD->addAttr(CallbackAttr::CreateImplicit(
          Context, Encoding.data(), Encoding.size(), FD->getLocation()));

    // Mark const if we don't care about errno and/or floating point exceptions
    // that are the only thing preventing the function from being const. This
    // allows IRgen to use LLVM intrinsics for such functions.
    bool NoExceptions =
        getLangOpts().getDefaultExceptionMode() == LangOptions::FPE_Ignore;
    bool ConstWithoutErrnoAndExceptions =
        Context.BuiltinInfo.isConstWithoutErrnoAndExceptions(BuiltinID);
    bool ConstWithoutExceptions =
        Context.BuiltinInfo.isConstWithoutExceptions(BuiltinID);
    if (!FD->hasAttr<ConstAttr>() &&
        (ConstWithoutErrnoAndExceptions || ConstWithoutExceptions) &&
        (!ConstWithoutErrnoAndExceptions ||
         (!getLangOpts().MathErrno && NoExceptions)) &&
        (!ConstWithoutExceptions || NoExceptions))
      FD->addAttr(ConstAttr::CreateImplicit(Context, FD->getLocation()));

    // We make "fma" on GNU or Windows const because we know it does not set
    // errno in those environments even though it could set errno based on the
    // C standard.
    const llvm::Triple &Trip = Context.getTargetInfo().getTriple();
    if ((Trip.isGNUEnvironment() || Trip.isOSMSVCRT()) &&
        !FD->hasAttr<ConstAttr>()) {
      switch (BuiltinID) {
      case Builtin::BI__builtin_fma:
      case Builtin::BI__builtin_fmaf:
      case Builtin::BI__builtin_fmal:
      case Builtin::BIfma:
      case Builtin::BIfmaf:
      case Builtin::BIfmal:
        FD->addAttr(ConstAttr::CreateImplicit(Context, FD->getLocation()));
        break;
      default:
        break;
      }
    }

    if (Context.BuiltinInfo.isReturnsTwice(BuiltinID) &&
        !FD->hasAttr<ReturnsTwiceAttr>())
      FD->addAttr(ReturnsTwiceAttr::CreateImplicit(Context,
                                         FD->getLocation()));
    if (Context.BuiltinInfo.isNoThrow(BuiltinID) && !FD->hasAttr<NoThrowAttr>())
      FD->addAttr(NoThrowAttr::CreateImplicit(Context, FD->getLocation()));
    if (Context.BuiltinInfo.isPure(BuiltinID) && !FD->hasAttr<PureAttr>())
      FD->addAttr(PureAttr::CreateImplicit(Context, FD->getLocation()));
    if (Context.BuiltinInfo.isConst(BuiltinID) && !FD->hasAttr<ConstAttr>())
      FD->addAttr(ConstAttr::CreateImplicit(Context, FD->getLocation()));
    if (getLangOpts().CUDA && Context.BuiltinInfo.isTSBuiltin(BuiltinID) &&
        !FD->hasAttr<CUDADeviceAttr>() && !FD->hasAttr<CUDAHostAttr>()) {
      // Add the appropriate attribute, depending on the CUDA compilation mode
      // and which target the builtin belongs to. For example, during host
      // compilation, aux builtins are __device__, while the rest are __host__.
      if (getLangOpts().CUDAIsDevice !=
          Context.BuiltinInfo.isAuxBuiltinID(BuiltinID))
        FD->addAttr(CUDADeviceAttr::CreateImplicit(Context, FD->getLocation()));
      else
        FD->addAttr(CUDAHostAttr::CreateImplicit(Context, FD->getLocation()));
    }

    // Add known guaranteed alignment for allocation functions.
    switch (BuiltinID) {
    case Builtin::BImemalign:
    case Builtin::BIaligned_alloc:
      if (!FD->hasAttr<AllocAlignAttr>())
        FD->addAttr(AllocAlignAttr::CreateImplicit(Context, ParamIdx(1, FD),
                                                   FD->getLocation()));
      break;
    default:
      break;
    }

    // Add allocsize attribute for allocation functions.
    switch (BuiltinID) {
    case Builtin::BIcalloc:
      FD->addAttr(AllocSizeAttr::CreateImplicit(
          Context, ParamIdx(1, FD), ParamIdx(2, FD), FD->getLocation()));
      break;
    case Builtin::BImemalign:
    case Builtin::BIaligned_alloc:
    case Builtin::BIrealloc:
      FD->addAttr(AllocSizeAttr::CreateImplicit(Context, ParamIdx(2, FD),
                                                ParamIdx(), FD->getLocation()));
      break;
    case Builtin::BImalloc:
      FD->addAttr(AllocSizeAttr::CreateImplicit(Context, ParamIdx(1, FD),
                                                ParamIdx(), FD->getLocation()));
      break;
    default:
      break;
    }

    // Add lifetime attribute to std::move, std::fowrard et al.
    switch (BuiltinID) {
    case Builtin::BIaddressof:
    case Builtin::BI__addressof:
    case Builtin::BI__builtin_addressof:
    case Builtin::BIas_const:
    case Builtin::BIforward:
    case Builtin::BIforward_like:
    case Builtin::BImove:
    case Builtin::BImove_if_noexcept:
      if (ParmVarDecl *P = FD->getParamDecl(0u);
          !P->hasAttr<LifetimeBoundAttr>())
        P->addAttr(
            LifetimeBoundAttr::CreateImplicit(Context, FD->getLocation()));
      break;
    default:
      break;
    }
  }

  AddKnownFunctionAttributesForReplaceableGlobalAllocationFunction(FD);

  // If C++ exceptions are enabled but we are told extern "C" functions cannot
  // throw, add an implicit nothrow attribute to any extern "C" function we come
  // across.
  if (getLangOpts().CXXExceptions && getLangOpts().ExternCNoUnwind &&
      FD->isExternC() && !FD->hasAttr<NoThrowAttr>()) {
    const auto *FPT = FD->getType()->getAs<FunctionProtoType>();
    if (!FPT || FPT->getExceptionSpecType() == EST_None)
      FD->addAttr(NoThrowAttr::CreateImplicit(Context, FD->getLocation()));
  }

  IdentifierInfo *Name = FD->getIdentifier();
  if (!Name)
    return;
  if ((!getLangOpts().CPlusPlus && FD->getDeclContext()->isTranslationUnit()) ||
      (isa<LinkageSpecDecl>(FD->getDeclContext()) &&
       cast<LinkageSpecDecl>(FD->getDeclContext())->getLanguage() ==
           LinkageSpecLanguageIDs::C)) {
    // Okay: this could be a libc/libm/Objective-C function we know
    // about.
  } else
    return;

  if (Name->isStr("asprintf") || Name->isStr("vasprintf")) {
    // FIXME: asprintf and vasprintf aren't C99 functions. Should they be
    // target-specific builtins, perhaps?
    if (!FD->hasAttr<FormatAttr>())
      FD->addAttr(FormatAttr::CreateImplicit(Context,
                                             &Context.Idents.get("printf"), 2,
                                             Name->isStr("vasprintf") ? 0 : 3,
                                             FD->getLocation()));
  }

  if (Name->isStr("__CFStringMakeConstantString")) {
    // We already have a __builtin___CFStringMakeConstantString,
    // but builds that use -fno-constant-cfstrings don't go through that.
    if (!FD->hasAttr<FormatArgAttr>())
      FD->addAttr(FormatArgAttr::CreateImplicit(Context, ParamIdx(1, FD),
                                                FD->getLocation()));
  }
}

TypedefDecl *Sema::ParseTypedefDecl(Scope *S, Declarator &D, QualType T,
                                    TypeSourceInfo *TInfo) {
  assert(D.getIdentifier() && "Wrong callback for declspec without declarator");
  assert(!T.isNull() && "GetTypeForDeclarator() returned null type");

  if (!TInfo) {
    assert(D.isInvalidType() && "no declarator info for valid type");
    TInfo = Context.getTrivialTypeSourceInfo(T);
  }

  // Scope manipulation handled by caller.
  TypedefDecl *NewTD =
      TypedefDecl::Create(Context, CurContext, D.getBeginLoc(),
                          D.getIdentifierLoc(), D.getIdentifier(), TInfo);

  // Bail out immediately if we have an invalid declaration.
  if (D.isInvalidType()) {
    NewTD->setInvalidDecl();
    return NewTD;
  }

  if (D.getDeclSpec().isModulePrivateSpecified()) {
    if (CurContext->isFunctionOrMethod())
      Diag(NewTD->getLocation(), diag::err_module_private_local)
          << 2 << NewTD
          << SourceRange(D.getDeclSpec().getModulePrivateSpecLoc())
          << FixItHint::CreateRemoval(
                 D.getDeclSpec().getModulePrivateSpecLoc());
    else
      NewTD->setModulePrivate();
  }

  // C++ [dcl.typedef]p8:
  //   If the typedef declaration defines an unnamed class (or
  //   enum), the first typedef-name declared by the declaration
  //   to be that class type (or enum type) is used to denote the
  //   class type (or enum type) for linkage purposes only.
  // We need to check whether the type was declared in the declaration.
  switch (D.getDeclSpec().getTypeSpecType()) {
  case TST_enum:
  case TST_struct:
  case TST_interface:
  case TST_union:
  case TST_class: {
    TagDecl *tagFromDeclSpec = cast<TagDecl>(D.getDeclSpec().getRepAsDecl());
    setTagNameForLinkagePurposes(tagFromDeclSpec, NewTD);
    break;
  }

  default:
    break;
  }

  return NewTD;
}

bool Sema::CheckEnumUnderlyingType(TypeSourceInfo *TI) {
  SourceLocation UnderlyingLoc = TI->getTypeLoc().getBeginLoc();
  QualType T = TI->getType();

  if (T->isDependentType())
    return false;

  // This doesn't use 'isIntegralType' despite the error message mentioning
  // integral type because isIntegralType would also allow enum types in C.
  if (const BuiltinType *BT = T->getAs<BuiltinType>())
    if (BT->isInteger())
      return false;

  return Diag(UnderlyingLoc, diag::err_enum_invalid_underlying)
         << T << T->isBitIntType();
}

bool Sema::CheckEnumRedeclaration(SourceLocation EnumLoc, bool IsScoped,
                                  QualType EnumUnderlyingTy, bool IsFixed,
                                  const EnumDecl *Prev) {
  if (IsScoped != Prev->isScoped()) {
    Diag(EnumLoc, diag::err_enum_redeclare_scoped_mismatch)
      << Prev->isScoped();
    Diag(Prev->getLocation(), diag::note_previous_declaration);
    return true;
  }

  if (IsFixed && Prev->isFixed()) {
    if (!EnumUnderlyingTy->isDependentType() &&
        !Prev->getIntegerType()->isDependentType() &&
        !Context.hasSameUnqualifiedType(EnumUnderlyingTy,
                                        Prev->getIntegerType())) {
      // TODO: Highlight the underlying type of the redeclaration.
      Diag(EnumLoc, diag::err_enum_redeclare_type_mismatch)
        << EnumUnderlyingTy << Prev->getIntegerType();
      Diag(Prev->getLocation(), diag::note_previous_declaration)
          << Prev->getIntegerTypeRange();
      return true;
    }
  } else if (IsFixed != Prev->isFixed()) {
    Diag(EnumLoc, diag::err_enum_redeclare_fixed_mismatch)
      << Prev->isFixed();
    Diag(Prev->getLocation(), diag::note_previous_declaration);
    return true;
  }

  return false;
}

/// Get diagnostic %select index for tag kind for
/// redeclaration diagnostic message.
/// WARNING: Indexes apply to particular diagnostics only!
///
/// \returns diagnostic %select index.
static unsigned getRedeclDiagFromTagKind(TagTypeKind Tag) {
  switch (Tag) {
  case TagTypeKind::Struct:
    return 0;
  case TagTypeKind::Interface:
    return 1;
  case TagTypeKind::Class:
    return 2;
  default: llvm_unreachable("Invalid tag kind for redecl diagnostic!");
  }
}

/// Determine if tag kind is a class-key compatible with
/// class for redeclaration (class, struct, or __interface).
///
/// \returns true iff the tag kind is compatible.
static bool isClassCompatTagKind(TagTypeKind Tag)
{
  return Tag == TagTypeKind::Struct || Tag == TagTypeKind::Class ||
         Tag == TagTypeKind::Interface;
}

Sema::NonTagKind Sema::getNonTagTypeDeclKind(const Decl *PrevDecl,
                                             TagTypeKind TTK) {
  if (isa<TypedefDecl>(PrevDecl))
    return NTK_Typedef;
  else if (isa<TypeAliasDecl>(PrevDecl))
    return NTK_TypeAlias;
  else if (isa<ClassTemplateDecl>(PrevDecl))
    return NTK_Template;
  else if (isa<TypeAliasTemplateDecl>(PrevDecl))
    return NTK_TypeAliasTemplate;
  else if (isa<TemplateTemplateParmDecl>(PrevDecl))
    return NTK_TemplateTemplateArgument;
  switch (TTK) {
  case TagTypeKind::Struct:
  case TagTypeKind::Interface:
  case TagTypeKind::Class:
    return getLangOpts().CPlusPlus ? NTK_NonClass : NTK_NonStruct;
  case TagTypeKind::Union:
    return NTK_NonUnion;
  case TagTypeKind::Enum:
    return NTK_NonEnum;
  }
  llvm_unreachable("invalid TTK");
}

bool Sema::isAcceptableTagRedeclaration(const TagDecl *Previous,
                                        TagTypeKind NewTag, bool isDefinition,
                                        SourceLocation NewTagLoc,
                                        const IdentifierInfo *Name) {
  // C++ [dcl.type.elab]p3:
  //   The class-key or enum keyword present in the
  //   elaborated-type-specifier shall agree in kind with the
  //   declaration to which the name in the elaborated-type-specifier
  //   refers. This rule also applies to the form of
  //   elaborated-type-specifier that declares a class-name or
  //   friend class since it can be construed as referring to the
  //   definition of the class. Thus, in any
  //   elaborated-type-specifier, the enum keyword shall be used to
  //   refer to an enumeration (7.2), the union class-key shall be
  //   used to refer to a union (clause 9), and either the class or
  //   struct class-key shall be used to refer to a class (clause 9)
  //   declared using the class or struct class-key.
  TagTypeKind OldTag = Previous->getTagKind();
  if (OldTag != NewTag &&
      !(isClassCompatTagKind(OldTag) && isClassCompatTagKind(NewTag)))
    return false;

  // Tags are compatible, but we might still want to warn on mismatched tags.
  // Non-class tags can't be mismatched at this point.
  if (!isClassCompatTagKind(NewTag))
    return true;

  // Declarations for which -Wmismatched-tags is disabled are entirely ignored
  // by our warning analysis. We don't want to warn about mismatches with (eg)
  // declarations in system headers that are designed to be specialized, but if
  // a user asks us to warn, we should warn if their code contains mismatched
  // declarations.
  auto IsIgnoredLoc = [&](SourceLocation Loc) {
    return getDiagnostics().isIgnored(diag::warn_struct_class_tag_mismatch,
                                      Loc);
  };
  if (IsIgnoredLoc(NewTagLoc))
    return true;

  auto IsIgnored = [&](const TagDecl *Tag) {
    return IsIgnoredLoc(Tag->getLocation());
  };
  while (IsIgnored(Previous)) {
    Previous = Previous->getPreviousDecl();
    if (!Previous)
      return true;
    OldTag = Previous->getTagKind();
  }

  bool isTemplate = false;
  if (const CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(Previous))
    isTemplate = Record->getDescribedClassTemplate();

  if (inTemplateInstantiation()) {
    if (OldTag != NewTag) {
      // In a template instantiation, do not offer fix-its for tag mismatches
      // since they usually mess up the template instead of fixing the problem.
      Diag(NewTagLoc, diag::warn_struct_class_tag_mismatch)
        << getRedeclDiagFromTagKind(NewTag) << isTemplate << Name
        << getRedeclDiagFromTagKind(OldTag);
      // FIXME: Note previous location?
    }
    return true;
  }

  if (isDefinition) {
    // On definitions, check all previous tags and issue a fix-it for each
    // one that doesn't match the current tag.
    if (Previous->getDefinition()) {
      // Don't suggest fix-its for redefinitions.
      return true;
    }

    bool previousMismatch = false;
    for (const TagDecl *I : Previous->redecls()) {
      if (I->getTagKind() != NewTag) {
        // Ignore previous declarations for which the warning was disabled.
        if (IsIgnored(I))
          continue;

        if (!previousMismatch) {
          previousMismatch = true;
          Diag(NewTagLoc, diag::warn_struct_class_previous_tag_mismatch)
            << getRedeclDiagFromTagKind(NewTag) << isTemplate << Name
            << getRedeclDiagFromTagKind(I->getTagKind());
        }
        Diag(I->getInnerLocStart(), diag::note_struct_class_suggestion)
          << getRedeclDiagFromTagKind(NewTag)
          << FixItHint::CreateReplacement(I->getInnerLocStart(),
               TypeWithKeyword::getTagTypeKindName(NewTag));
      }
    }
    return true;
  }

  // Identify the prevailing tag kind: this is the kind of the definition (if
  // there is a non-ignored definition), or otherwise the kind of the prior
  // (non-ignored) declaration.
  const TagDecl *PrevDef = Previous->getDefinition();
  if (PrevDef && IsIgnored(PrevDef))
    PrevDef = nullptr;
  const TagDecl *Redecl = PrevDef ? PrevDef : Previous;
  if (Redecl->getTagKind() != NewTag) {
    Diag(NewTagLoc, diag::warn_struct_class_tag_mismatch)
      << getRedeclDiagFromTagKind(NewTag) << isTemplate << Name
      << getRedeclDiagFromTagKind(OldTag);
    Diag(Redecl->getLocation(), diag::note_previous_use);

    // If there is a previous definition, suggest a fix-it.
    if (PrevDef) {
      Diag(NewTagLoc, diag::note_struct_class_suggestion)
        << getRedeclDiagFromTagKind(Redecl->getTagKind())
        << FixItHint::CreateReplacement(SourceRange(NewTagLoc),
             TypeWithKeyword::getTagTypeKindName(Redecl->getTagKind()));
    }
  }

  return true;
}

/// Add a minimal nested name specifier fixit hint to allow lookup of a tag name
/// from an outer enclosing namespace or file scope inside a friend declaration.
/// This should provide the commented out code in the following snippet:
///   namespace N {
///     struct X;
///     namespace M {
///       struct Y { friend struct /*N::*/ X; };
///     }
///   }
static FixItHint createFriendTagNNSFixIt(Sema &SemaRef, NamedDecl *ND, Scope *S,
                                         SourceLocation NameLoc) {
  // While the decl is in a namespace, do repeated lookup of that name and see
  // if we get the same namespace back.  If we do not, continue until
  // translation unit scope, at which point we have a fully qualified NNS.
  SmallVector<IdentifierInfo *, 4> Namespaces;
  DeclContext *DC = ND->getDeclContext()->getRedeclContext();
  for (; !DC->isTranslationUnit(); DC = DC->getParent()) {
    // This tag should be declared in a namespace, which can only be enclosed by
    // other namespaces.  Bail if there's an anonymous namespace in the chain.
    NamespaceDecl *Namespace = dyn_cast<NamespaceDecl>(DC);
    if (!Namespace || Namespace->isAnonymousNamespace())
      return FixItHint();
    IdentifierInfo *II = Namespace->getIdentifier();
    Namespaces.push_back(II);
    NamedDecl *Lookup = SemaRef.LookupSingleName(
        S, II, NameLoc, Sema::LookupNestedNameSpecifierName);
    if (Lookup == Namespace)
      break;
  }

  // Once we have all the namespaces, reverse them to go outermost first, and
  // build an NNS.
  SmallString<64> Insertion;
  llvm::raw_svector_ostream OS(Insertion);
  if (DC->isTranslationUnit())
    OS << "::";
  std::reverse(Namespaces.begin(), Namespaces.end());
  for (auto *II : Namespaces)
    OS << II->getName() << "::";
  return FixItHint::CreateInsertion(NameLoc, Insertion);
}

/// Determine whether a tag originally declared in context \p OldDC can
/// be redeclared with an unqualified name in \p NewDC (assuming name lookup
/// found a declaration in \p OldDC as a previous decl, perhaps through a
/// using-declaration).
static bool isAcceptableTagRedeclContext(Sema &S, DeclContext *OldDC,
                                         DeclContext *NewDC) {
  OldDC = OldDC->getRedeclContext();
  NewDC = NewDC->getRedeclContext();

  if (OldDC->Equals(NewDC))
    return true;

  // In MSVC mode, we allow a redeclaration if the contexts are related (either
  // encloses the other).
  if (S.getLangOpts().MSVCCompat &&
      (OldDC->Encloses(NewDC) || NewDC->Encloses(OldDC)))
    return true;

  return false;
}

DeclResult
Sema::ActOnTag(Scope *S, unsigned TagSpec, TagUseKind TUK, SourceLocation KWLoc,
               CXXScopeSpec &SS, IdentifierInfo *Name, SourceLocation NameLoc,
               const ParsedAttributesView &Attrs, AccessSpecifier AS,
               SourceLocation ModulePrivateLoc,
               MultiTemplateParamsArg TemplateParameterLists, bool &OwnedDecl,
               bool &IsDependent, SourceLocation ScopedEnumKWLoc,
               bool ScopedEnumUsesClassTag, TypeResult UnderlyingType,
               bool IsTypeSpecifier, bool IsTemplateParamOrArg,
               OffsetOfKind OOK, SkipBodyInfo *SkipBody) {
  // If this is not a definition, it must have a name.
  IdentifierInfo *OrigName = Name;
  assert((Name != nullptr || TUK == TagUseKind::Definition) &&
         "Nameless record must be a definition!");
  assert(TemplateParameterLists.size() == 0 || TUK != TagUseKind::Reference);

  OwnedDecl = false;
  TagTypeKind Kind = TypeWithKeyword::getTagTypeKindForTypeSpec(TagSpec);
  bool ScopedEnum = ScopedEnumKWLoc.isValid();

  // FIXME: Check member specializations more carefully.
  bool isMemberSpecialization = false;
  bool Invalid = false;

  // We only need to do this matching if we have template parameters
  // or a scope specifier, which also conveniently avoids this work
  // for non-C++ cases.
  if (TemplateParameterLists.size() > 0 ||
      (SS.isNotEmpty() && TUK != TagUseKind::Reference)) {
    TemplateParameterList *TemplateParams =
        MatchTemplateParametersToScopeSpecifier(
            KWLoc, NameLoc, SS, nullptr, TemplateParameterLists,
            TUK == TagUseKind::Friend, isMemberSpecialization, Invalid);

    // C++23 [dcl.type.elab] p2:
    //   If an elaborated-type-specifier is the sole constituent of a
    //   declaration, the declaration is ill-formed unless it is an explicit
    //   specialization, an explicit instantiation or it has one of the
    //   following forms: [...]
    // C++23 [dcl.enum] p1:
    //   If the enum-head-name of an opaque-enum-declaration contains a
    //   nested-name-specifier, the declaration shall be an explicit
    //   specialization.
    //
    // FIXME: Class template partial specializations can be forward declared
    // per CWG2213, but the resolution failed to allow qualified forward
    // declarations. This is almost certainly unintentional, so we allow them.
    if (TUK == TagUseKind::Declaration && SS.isNotEmpty() &&
        !isMemberSpecialization)
      Diag(SS.getBeginLoc(), diag::err_standalone_class_nested_name_specifier)
          << TypeWithKeyword::getTagTypeKindName(Kind) << SS.getRange();

    if (TemplateParams) {
      if (Kind == TagTypeKind::Enum) {
        Diag(KWLoc, diag::err_enum_template);
        return true;
      }

      if (TemplateParams->size() > 0) {
        // This is a declaration or definition of a class template (which may
        // be a member of another template).

        if (Invalid)
          return true;

        OwnedDecl = false;
        DeclResult Result = CheckClassTemplate(
            S, TagSpec, TUK, KWLoc, SS, Name, NameLoc, Attrs, TemplateParams,
            AS, ModulePrivateLoc,
            /*FriendLoc*/ SourceLocation(), TemplateParameterLists.size() - 1,
            TemplateParameterLists.data(), SkipBody);
        return Result.get();
      } else {
        // The "template<>" header is extraneous.
        Diag(TemplateParams->getTemplateLoc(), diag::err_template_tag_noparams)
          << TypeWithKeyword::getTagTypeKindName(Kind) << Name;
        isMemberSpecialization = true;
      }
    }

    if (!TemplateParameterLists.empty() && isMemberSpecialization &&
        CheckTemplateDeclScope(S, TemplateParameterLists.back()))
      return true;
  }

  if (TUK == TagUseKind::Friend && Kind == TagTypeKind::Enum) {
    // C++23 [dcl.type.elab]p4:
    //   If an elaborated-type-specifier appears with the friend specifier as
    //   an entire member-declaration, the member-declaration shall have one
    //   of the following forms:
    //     friend class-key nested-name-specifier(opt) identifier ;
    //     friend class-key simple-template-id ;
    //     friend class-key nested-name-specifier template(opt)
    //       simple-template-id ;
    //
    // Since enum is not a class-key, so declarations like "friend enum E;"
    // are ill-formed. Although CWG2363 reaffirms that such declarations are
    // invalid, most implementations accept so we issue a pedantic warning.
    Diag(KWLoc, diag::ext_enum_friend) << FixItHint::CreateRemoval(
        ScopedEnum ? SourceRange(KWLoc, ScopedEnumKWLoc) : KWLoc);
    assert(ScopedEnum || !ScopedEnumUsesClassTag);
    Diag(KWLoc, diag::note_enum_friend)
        << (ScopedEnum + ScopedEnumUsesClassTag);
  }

  // Figure out the underlying type if this a enum declaration. We need to do
  // this early, because it's needed to detect if this is an incompatible
  // redeclaration.
  llvm::PointerUnion<const Type*, TypeSourceInfo*> EnumUnderlying;
  bool IsFixed = !UnderlyingType.isUnset() || ScopedEnum;

  if (Kind == TagTypeKind::Enum) {
    if (UnderlyingType.isInvalid() || (!UnderlyingType.get() && ScopedEnum)) {
      // No underlying type explicitly specified, or we failed to parse the
      // type, default to int.
      EnumUnderlying = Context.IntTy.getTypePtr();
    } else if (UnderlyingType.get()) {
      // C++0x 7.2p2: The type-specifier-seq of an enum-base shall name an
      // integral type; any cv-qualification is ignored.
      TypeSourceInfo *TI = nullptr;
      GetTypeFromParser(UnderlyingType.get(), &TI);
      EnumUnderlying = TI;

      if (CheckEnumUnderlyingType(TI))
        // Recover by falling back to int.
        EnumUnderlying = Context.IntTy.getTypePtr();

      if (DiagnoseUnexpandedParameterPack(TI->getTypeLoc().getBeginLoc(), TI,
                                          UPPC_FixedUnderlyingType))
        EnumUnderlying = Context.IntTy.getTypePtr();

    } else if (Context.getTargetInfo().getTriple().isWindowsMSVCEnvironment()) {
      // For MSVC ABI compatibility, unfixed enums must use an underlying type
      // of 'int'. However, if this is an unfixed forward declaration, don't set
      // the underlying type unless the user enables -fms-compatibility. This
      // makes unfixed forward declared enums incomplete and is more conforming.
      if (TUK == TagUseKind::Definition || getLangOpts().MSVCCompat)
        EnumUnderlying = Context.IntTy.getTypePtr();
    }
  }

  DeclContext *SearchDC = CurContext;
  DeclContext *DC = CurContext;
  bool isStdBadAlloc = false;
  bool isStdAlignValT = false;

  RedeclarationKind Redecl = forRedeclarationInCurContext();
  if (TUK == TagUseKind::Friend || TUK == TagUseKind::Reference)
    Redecl = RedeclarationKind::NotForRedeclaration;

  /// Create a new tag decl in C/ObjC. Since the ODR-like semantics for ObjC/C
  /// implemented asks for structural equivalence checking, the returned decl
  /// here is passed back to the parser, allowing the tag body to be parsed.
  auto createTagFromNewDecl = [&]() -> TagDecl * {
    assert(!getLangOpts().CPlusPlus && "not meant for C++ usage");
    // If there is an identifier, use the location of the identifier as the
    // location of the decl, otherwise use the location of the struct/union
    // keyword.
    SourceLocation Loc = NameLoc.isValid() ? NameLoc : KWLoc;
    TagDecl *New = nullptr;

    if (Kind == TagTypeKind::Enum) {
      New = EnumDecl::Create(Context, SearchDC, KWLoc, Loc, Name, nullptr,
                             ScopedEnum, ScopedEnumUsesClassTag, IsFixed);
      // If this is an undefined enum, bail.
      if (TUK != TagUseKind::Definition && !Invalid)
        return nullptr;
      if (EnumUnderlying) {
        EnumDecl *ED = cast<EnumDecl>(New);
        if (TypeSourceInfo *TI = EnumUnderlying.dyn_cast<TypeSourceInfo *>())
          ED->setIntegerTypeSourceInfo(TI);
        else
          ED->setIntegerType(QualType(EnumUnderlying.get<const Type *>(), 0));
        QualType EnumTy = ED->getIntegerType();
        ED->setPromotionType(Context.isPromotableIntegerType(EnumTy)
                                 ? Context.getPromotedIntegerType(EnumTy)
                                 : EnumTy);
      }
    } else { // struct/union
      New = RecordDecl::Create(Context, Kind, SearchDC, KWLoc, Loc, Name,
                               nullptr);
    }

    if (RecordDecl *RD = dyn_cast<RecordDecl>(New)) {
      // Add alignment attributes if necessary; these attributes are checked
      // when the ASTContext lays out the structure.
      //
      // It is important for implementing the correct semantics that this
      // happen here (in ActOnTag). The #pragma pack stack is
      // maintained as a result of parser callbacks which can occur at
      // many points during the parsing of a struct declaration (because
      // the #pragma tokens are effectively skipped over during the
      // parsing of the struct).
      if (TUK == TagUseKind::Definition &&
          (!SkipBody || !SkipBody->ShouldSkip)) {
        AddAlignmentAttributesForRecord(RD);
        AddMsStructLayoutForRecord(RD);
      }
    }
    New->setLexicalDeclContext(CurContext);
    return New;
  };

  LookupResult Previous(*this, Name, NameLoc, LookupTagName, Redecl);
  if (Name && SS.isNotEmpty()) {
    // We have a nested-name tag ('struct foo::bar').

    // Check for invalid 'foo::'.
    if (SS.isInvalid()) {
      Name = nullptr;
      goto CreateNewDecl;
    }

    // If this is a friend or a reference to a class in a dependent
    // context, don't try to make a decl for it.
    if (TUK == TagUseKind::Friend || TUK == TagUseKind::Reference) {
      DC = computeDeclContext(SS, false);
      if (!DC) {
        IsDependent = true;
        return true;
      }
    } else {
      DC = computeDeclContext(SS, true);
      if (!DC) {
        Diag(SS.getRange().getBegin(), diag::err_dependent_nested_name_spec)
          << SS.getRange();
        return true;
      }
    }

    if (RequireCompleteDeclContext(SS, DC))
      return true;

    SearchDC = DC;
    // Look-up name inside 'foo::'.
    LookupQualifiedName(Previous, DC);

    if (Previous.isAmbiguous())
      return true;

    if (Previous.empty()) {
      // Name lookup did not find anything. However, if the
      // nested-name-specifier refers to the current instantiation,
      // and that current instantiation has any dependent base
      // classes, we might find something at instantiation time: treat
      // this as a dependent elaborated-type-specifier.
      // But this only makes any sense for reference-like lookups.
      if (Previous.wasNotFoundInCurrentInstantiation() &&
          (TUK == TagUseKind::Reference || TUK == TagUseKind::Friend)) {
        IsDependent = true;
        return true;
      }

      // A tag 'foo::bar' must already exist.
      Diag(NameLoc, diag::err_not_tag_in_scope)
          << llvm::to_underlying(Kind) << Name << DC << SS.getRange();
      Name = nullptr;
      Invalid = true;
      goto CreateNewDecl;
    }
  } else if (Name) {
    // C++14 [class.mem]p14:
    //   If T is the name of a class, then each of the following shall have a
    //   name different from T:
    //    -- every member of class T that is itself a type
    if (TUK != TagUseKind::Reference && TUK != TagUseKind::Friend &&
        DiagnoseClassNameShadow(SearchDC, DeclarationNameInfo(Name, NameLoc)))
      return true;

    // If this is a named struct, check to see if there was a previous forward
    // declaration or definition.
    // FIXME: We're looking into outer scopes here, even when we
    // shouldn't be. Doing so can result in ambiguities that we
    // shouldn't be diagnosing.
    LookupName(Previous, S);

    // When declaring or defining a tag, ignore ambiguities introduced
    // by types using'ed into this scope.
    if (Previous.isAmbiguous() &&
        (TUK == TagUseKind::Definition || TUK == TagUseKind::Declaration)) {
      LookupResult::Filter F = Previous.makeFilter();
      while (F.hasNext()) {
        NamedDecl *ND = F.next();
        if (!ND->getDeclContext()->getRedeclContext()->Equals(
                SearchDC->getRedeclContext()))
          F.erase();
      }
      F.done();
    }

    // C++11 [namespace.memdef]p3:
    //   If the name in a friend declaration is neither qualified nor
    //   a template-id and the declaration is a function or an
    //   elaborated-type-specifier, the lookup to determine whether
    //   the entity has been previously declared shall not consider
    //   any scopes outside the innermost enclosing namespace.
    //
    // MSVC doesn't implement the above rule for types, so a friend tag
    // declaration may be a redeclaration of a type declared in an enclosing
    // scope.  They do implement this rule for friend functions.
    //
    // Does it matter that this should be by scope instead of by
    // semantic context?
    if (!Previous.empty() && TUK == TagUseKind::Friend) {
      DeclContext *EnclosingNS = SearchDC->getEnclosingNamespaceContext();
      LookupResult::Filter F = Previous.makeFilter();
      bool FriendSawTagOutsideEnclosingNamespace = false;
      while (F.hasNext()) {
        NamedDecl *ND = F.next();
        DeclContext *DC = ND->getDeclContext()->getRedeclContext();
        if (DC->isFileContext() &&
            !EnclosingNS->Encloses(ND->getDeclContext())) {
          if (getLangOpts().MSVCCompat)
            FriendSawTagOutsideEnclosingNamespace = true;
          else
            F.erase();
        }
      }
      F.done();

      // Diagnose this MSVC extension in the easy case where lookup would have
      // unambiguously found something outside the enclosing namespace.
      if (Previous.isSingleResult() && FriendSawTagOutsideEnclosingNamespace) {
        NamedDecl *ND = Previous.getFoundDecl();
        Diag(NameLoc, diag::ext_friend_tag_redecl_outside_namespace)
            << createFriendTagNNSFixIt(*this, ND, S, NameLoc);
      }
    }

    // Note:  there used to be some attempt at recovery here.
    if (Previous.isAmbiguous())
      return true;

    if (!getLangOpts().CPlusPlus && TUK != TagUseKind::Reference) {
      // FIXME: This makes sure that we ignore the contexts associated
      // with C structs, unions, and enums when looking for a matching
      // tag declaration or definition. See the similar lookup tweak
      // in Sema::LookupName; is there a better way to deal with this?
      while (isa<RecordDecl, EnumDecl, ObjCContainerDecl>(SearchDC))
        SearchDC = SearchDC->getParent();
    } else if (getLangOpts().CPlusPlus) {
      // Inside ObjCContainer want to keep it as a lexical decl context but go
      // past it (most often to TranslationUnit) to find the semantic decl
      // context.
      while (isa<ObjCContainerDecl>(SearchDC))
        SearchDC = SearchDC->getParent();
    }
  } else if (getLangOpts().CPlusPlus) {
    // Don't use ObjCContainerDecl as the semantic decl context for anonymous
    // TagDecl the same way as we skip it for named TagDecl.
    while (isa<ObjCContainerDecl>(SearchDC))
      SearchDC = SearchDC->getParent();
  }

  if (Previous.isSingleResult() &&
      Previous.getFoundDecl()->isTemplateParameter()) {
    // Maybe we will complain about the shadowed template parameter.
    DiagnoseTemplateParameterShadow(NameLoc, Previous.getFoundDecl());
    // Just pretend that we didn't see the previous declaration.
    Previous.clear();
  }

  if (getLangOpts().CPlusPlus && Name && DC && StdNamespace &&
      DC->Equals(getStdNamespace())) {
    if (Name->isStr("bad_alloc")) {
      // This is a declaration of or a reference to "std::bad_alloc".
      isStdBadAlloc = true;

      // If std::bad_alloc has been implicitly declared (but made invisible to
      // name lookup), fill in this implicit declaration as the previous
      // declaration, so that the declarations get chained appropriately.
      if (Previous.empty() && StdBadAlloc)
        Previous.addDecl(getStdBadAlloc());
    } else if (Name->isStr("align_val_t")) {
      isStdAlignValT = true;
      if (Previous.empty() && StdAlignValT)
        Previous.addDecl(getStdAlignValT());
    }
  }

  // If we didn't find a previous declaration, and this is a reference
  // (or friend reference), move to the correct scope.  In C++, we
  // also need to do a redeclaration lookup there, just in case
  // there's a shadow friend decl.
  if (Name && Previous.empty() &&
      (TUK == TagUseKind::Reference || TUK == TagUseKind::Friend ||
       IsTemplateParamOrArg)) {
    if (Invalid) goto CreateNewDecl;
    assert(SS.isEmpty());

    if (TUK == TagUseKind::Reference || IsTemplateParamOrArg) {
      // C++ [basic.scope.pdecl]p5:
      //   -- for an elaborated-type-specifier of the form
      //
      //          class-key identifier
      //
      //      if the elaborated-type-specifier is used in the
      //      decl-specifier-seq or parameter-declaration-clause of a
      //      function defined in namespace scope, the identifier is
      //      declared as a class-name in the namespace that contains
      //      the declaration; otherwise, except as a friend
      //      declaration, the identifier is declared in the smallest
      //      non-class, non-function-prototype scope that contains the
      //      declaration.
      //
      // C99 6.7.2.3p8 has a similar (but not identical!) provision for
      // C structs and unions.
      //
      // It is an error in C++ to declare (rather than define) an enum
      // type, including via an elaborated type specifier.  We'll
      // diagnose that later; for now, declare the enum in the same
      // scope as we would have picked for any other tag type.
      //
      // GNU C also supports this behavior as part of its incomplete
      // enum types extension, while GNU C++ does not.
      //
      // Find the context where we'll be declaring the tag.
      // FIXME: We would like to maintain the current DeclContext as the
      // lexical context,
      SearchDC = getTagInjectionContext(SearchDC);

      // Find the scope where we'll be declaring the tag.
      S = getTagInjectionScope(S, getLangOpts());
    } else {
      assert(TUK == TagUseKind::Friend);
      CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(SearchDC);

      // C++ [namespace.memdef]p3:
      //   If a friend declaration in a non-local class first declares a
      //   class or function, the friend class or function is a member of
      //   the innermost enclosing namespace.
      SearchDC = RD->isLocalClass() ? RD->isLocalClass()
                                    : SearchDC->getEnclosingNamespaceContext();
    }

    // In C++, we need to do a redeclaration lookup to properly
    // diagnose some problems.
    // FIXME: redeclaration lookup is also used (with and without C++) to find a
    // hidden declaration so that we don't get ambiguity errors when using a
    // type declared by an elaborated-type-specifier.  In C that is not correct
    // and we should instead merge compatible types found by lookup.
    if (getLangOpts().CPlusPlus) {
      // FIXME: This can perform qualified lookups into function contexts,
      // which are meaningless.
      Previous.setRedeclarationKind(forRedeclarationInCurContext());
      LookupQualifiedName(Previous, SearchDC);
    } else {
      Previous.setRedeclarationKind(forRedeclarationInCurContext());
      LookupName(Previous, S);
    }
  }

  // If we have a known previous declaration to use, then use it.
  if (Previous.empty() && SkipBody && SkipBody->Previous)
    Previous.addDecl(SkipBody->Previous);

  if (!Previous.empty()) {
    NamedDecl *PrevDecl = Previous.getFoundDecl();
    NamedDecl *DirectPrevDecl = Previous.getRepresentativeDecl();

    // It's okay to have a tag decl in the same scope as a typedef
    // which hides a tag decl in the same scope.  Finding this
    // with a redeclaration lookup can only actually happen in C++.
    //
    // This is also okay for elaborated-type-specifiers, which is
    // technically forbidden by the current standard but which is
    // okay according to the likely resolution of an open issue;
    // see http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_active.html#407
    if (getLangOpts().CPlusPlus) {
      if (TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(PrevDecl)) {
        if (const TagType *TT = TD->getUnderlyingType()->getAs<TagType>()) {
          TagDecl *Tag = TT->getDecl();
          if (Tag->getDeclName() == Name &&
              Tag->getDeclContext()->getRedeclContext()
                          ->Equals(TD->getDeclContext()->getRedeclContext())) {
            PrevDecl = Tag;
            Previous.clear();
            Previous.addDecl(Tag);
            Previous.resolveKind();
          }
        }
      }
    }

    // If this is a redeclaration of a using shadow declaration, it must
    // declare a tag in the same context. In MSVC mode, we allow a
    // redefinition if either context is within the other.
    if (auto *Shadow = dyn_cast<UsingShadowDecl>(DirectPrevDecl)) {
      auto *OldTag = dyn_cast<TagDecl>(PrevDecl);
      if (SS.isEmpty() && TUK != TagUseKind::Reference &&
          TUK != TagUseKind::Friend &&
          isDeclInScope(Shadow, SearchDC, S, isMemberSpecialization) &&
          !(OldTag && isAcceptableTagRedeclContext(
                          *this, OldTag->getDeclContext(), SearchDC))) {
        Diag(KWLoc, diag::err_using_decl_conflict_reverse);
        Diag(Shadow->getTargetDecl()->getLocation(),
             diag::note_using_decl_target);
        Diag(Shadow->getIntroducer()->getLocation(), diag::note_using_decl)
            << 0;
        // Recover by ignoring the old declaration.
        Previous.clear();
        goto CreateNewDecl;
      }
    }

    if (TagDecl *PrevTagDecl = dyn_cast<TagDecl>(PrevDecl)) {
      // If this is a use of a previous tag, or if the tag is already declared
      // in the same scope (so that the definition/declaration completes or
      // rementions the tag), reuse the decl.
      if (TUK == TagUseKind::Reference || TUK == TagUseKind::Friend ||
          isDeclInScope(DirectPrevDecl, SearchDC, S,
                        SS.isNotEmpty() || isMemberSpecialization)) {
        // Make sure that this wasn't declared as an enum and now used as a
        // struct or something similar.
        if (!isAcceptableTagRedeclaration(PrevTagDecl, Kind,
                                          TUK == TagUseKind::Definition, KWLoc,
                                          Name)) {
          bool SafeToContinue =
              (PrevTagDecl->getTagKind() != TagTypeKind::Enum &&
               Kind != TagTypeKind::Enum);
          if (SafeToContinue)
            Diag(KWLoc, diag::err_use_with_wrong_tag)
              << Name
              << FixItHint::CreateReplacement(SourceRange(KWLoc),
                                              PrevTagDecl->getKindName());
          else
            Diag(KWLoc, diag::err_use_with_wrong_tag) << Name;
          Diag(PrevTagDecl->getLocation(), diag::note_previous_use);

          if (SafeToContinue)
            Kind = PrevTagDecl->getTagKind();
          else {
            // Recover by making this an anonymous redefinition.
            Name = nullptr;
            Previous.clear();
            Invalid = true;
          }
        }

        if (Kind == TagTypeKind::Enum &&
            PrevTagDecl->getTagKind() == TagTypeKind::Enum) {
          const EnumDecl *PrevEnum = cast<EnumDecl>(PrevTagDecl);
          if (TUK == TagUseKind::Reference || TUK == TagUseKind::Friend)
            return PrevTagDecl;

          QualType EnumUnderlyingTy;
          if (TypeSourceInfo *TI = EnumUnderlying.dyn_cast<TypeSourceInfo*>())
            EnumUnderlyingTy = TI->getType().getUnqualifiedType();
          else if (const Type *T = EnumUnderlying.dyn_cast<const Type*>())
            EnumUnderlyingTy = QualType(T, 0);

          // All conflicts with previous declarations are recovered by
          // returning the previous declaration, unless this is a definition,
          // in which case we want the caller to bail out.
          if (CheckEnumRedeclaration(NameLoc.isValid() ? NameLoc : KWLoc,
                                     ScopedEnum, EnumUnderlyingTy,
                                     IsFixed, PrevEnum))
            return TUK == TagUseKind::Declaration ? PrevTagDecl : nullptr;
        }

        // C++11 [class.mem]p1:
        //   A member shall not be declared twice in the member-specification,
        //   except that a nested class or member class template can be declared
        //   and then later defined.
        if (TUK == TagUseKind::Declaration && PrevDecl->isCXXClassMember() &&
            S->isDeclScope(PrevDecl)) {
          Diag(NameLoc, diag::ext_member_redeclared);
          Diag(PrevTagDecl->getLocation(), diag::note_previous_declaration);
        }

        if (!Invalid) {
          // If this is a use, just return the declaration we found, unless
          // we have attributes.
          if (TUK == TagUseKind::Reference || TUK == TagUseKind::Friend) {
            if (!Attrs.empty()) {
              // FIXME: Diagnose these attributes. For now, we create a new
              // declaration to hold them.
            } else if (TUK == TagUseKind::Reference &&
                       (PrevTagDecl->getFriendObjectKind() ==
                            Decl::FOK_Undeclared ||
                        PrevDecl->getOwningModule() != getCurrentModule()) &&
                       SS.isEmpty()) {
              // This declaration is a reference to an existing entity, but
              // has different visibility from that entity: it either makes
              // a friend visible or it makes a type visible in a new module.
              // In either case, create a new declaration. We only do this if
              // the declaration would have meant the same thing if no prior
              // declaration were found, that is, if it was found in the same
              // scope where we would have injected a declaration.
              if (!getTagInjectionContext(CurContext)->getRedeclContext()
                       ->Equals(PrevDecl->getDeclContext()->getRedeclContext()))
                return PrevTagDecl;
              // This is in the injected scope, create a new declaration in
              // that scope.
              S = getTagInjectionScope(S, getLangOpts());
            } else {
              return PrevTagDecl;
            }
          }

          // Diagnose attempts to redefine a tag.
          if (TUK == TagUseKind::Definition) {
            if (NamedDecl *Def = PrevTagDecl->getDefinition()) {
              // If we're defining a specialization and the previous definition
              // is from an implicit instantiation, don't emit an error
              // here; we'll catch this in the general case below.
              bool IsExplicitSpecializationAfterInstantiation = false;
              if (isMemberSpecialization) {
                if (CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(Def))
                  IsExplicitSpecializationAfterInstantiation =
                    RD->getTemplateSpecializationKind() !=
                    TSK_ExplicitSpecialization;
                else if (EnumDecl *ED = dyn_cast<EnumDecl>(Def))
                  IsExplicitSpecializationAfterInstantiation =
                    ED->getTemplateSpecializationKind() !=
                    TSK_ExplicitSpecialization;
              }

              // Note that clang allows ODR-like semantics for ObjC/C, i.e., do
              // not keep more that one definition around (merge them). However,
              // ensure the decl passes the structural compatibility check in
              // C11 6.2.7/1 (or 6.1.2.6/1 in C89).
              NamedDecl *Hidden = nullptr;
              if (SkipBody && !hasVisibleDefinition(Def, &Hidden)) {
                // There is a definition of this tag, but it is not visible. We
                // explicitly make use of C++'s one definition rule here, and
                // assume that this definition is identical to the hidden one
                // we already have. Make the existing definition visible and
                // use it in place of this one.
                if (!getLangOpts().CPlusPlus) {
                  // Postpone making the old definition visible until after we
                  // complete parsing the new one and do the structural
                  // comparison.
                  SkipBody->CheckSameAsPrevious = true;
                  SkipBody->New = createTagFromNewDecl();
                  SkipBody->Previous = Def;
                  return Def;
                } else {
                  SkipBody->ShouldSkip = true;
                  SkipBody->Previous = Def;
                  makeMergedDefinitionVisible(Hidden);
                  // Carry on and handle it like a normal definition. We'll
                  // skip starting the definition later.
                }
              } else if (!IsExplicitSpecializationAfterInstantiation) {
                // A redeclaration in function prototype scope in C isn't
                // visible elsewhere, so merely issue a warning.
                if (!getLangOpts().CPlusPlus && S->containedInPrototypeScope())
                  Diag(NameLoc, diag::warn_redefinition_in_param_list) << Name;
                else
                  Diag(NameLoc, diag::err_redefinition) << Name;
                notePreviousDefinition(Def,
                                       NameLoc.isValid() ? NameLoc : KWLoc);
                // If this is a redefinition, recover by making this
                // struct be anonymous, which will make any later
                // references get the previous definition.
                Name = nullptr;
                Previous.clear();
                Invalid = true;
              }
            } else {
              // If the type is currently being defined, complain
              // about a nested redefinition.
              auto *TD = Context.getTagDeclType(PrevTagDecl)->getAsTagDecl();
              if (TD->isBeingDefined()) {
                Diag(NameLoc, diag::err_nested_redefinition) << Name;
                Diag(PrevTagDecl->getLocation(),
                     diag::note_previous_definition);
                Name = nullptr;
                Previous.clear();
                Invalid = true;
              }
            }

            // Okay, this is definition of a previously declared or referenced
            // tag. We're going to create a new Decl for it.
          }

          // Okay, we're going to make a redeclaration.  If this is some kind
          // of reference, make sure we build the redeclaration in the same DC
          // as the original, and ignore the current access specifier.
          if (TUK == TagUseKind::Friend || TUK == TagUseKind::Reference) {
            SearchDC = PrevTagDecl->getDeclContext();
            AS = AS_none;
          }
        }
        // If we get here we have (another) forward declaration or we
        // have a definition.  Just create a new decl.

      } else {
        // If we get here, this is a definition of a new tag type in a nested
        // scope, e.g. "struct foo; void bar() { struct foo; }", just create a
        // new decl/type.  We set PrevDecl to NULL so that the entities
        // have distinct types.
        Previous.clear();
      }
      // If we get here, we're going to create a new Decl. If PrevDecl
      // is non-NULL, it's a definition of the tag declared by
      // PrevDecl. If it's NULL, we have a new definition.

    // Otherwise, PrevDecl is not a tag, but was found with tag
    // lookup.  This is only actually possible in C++, where a few
    // things like templates still live in the tag namespace.
    } else {
      // Use a better diagnostic if an elaborated-type-specifier
      // found the wrong kind of type on the first
      // (non-redeclaration) lookup.
      if ((TUK == TagUseKind::Reference || TUK == TagUseKind::Friend) &&
          !Previous.isForRedeclaration()) {
        NonTagKind NTK = getNonTagTypeDeclKind(PrevDecl, Kind);
        Diag(NameLoc, diag::err_tag_reference_non_tag)
            << PrevDecl << NTK << llvm::to_underlying(Kind);
        Diag(PrevDecl->getLocation(), diag::note_declared_at);
        Invalid = true;

      // Otherwise, only diagnose if the declaration is in scope.
      } else if (!isDeclInScope(DirectPrevDecl, SearchDC, S,
                                SS.isNotEmpty() || isMemberSpecialization)) {
        // do nothing

      // Diagnose implicit declarations introduced by elaborated types.
      } else if (TUK == TagUseKind::Reference || TUK == TagUseKind::Friend) {
        NonTagKind NTK = getNonTagTypeDeclKind(PrevDecl, Kind);
        Diag(NameLoc, diag::err_tag_reference_conflict) << NTK;
        Diag(PrevDecl->getLocation(), diag::note_previous_decl) << PrevDecl;
        Invalid = true;

      // Otherwise it's a declaration.  Call out a particularly common
      // case here.
      } else if (TypedefNameDecl *TND = dyn_cast<TypedefNameDecl>(PrevDecl)) {
        unsigned Kind = 0;
        if (isa<TypeAliasDecl>(PrevDecl)) Kind = 1;
        Diag(NameLoc, diag::err_tag_definition_of_typedef)
          << Name << Kind << TND->getUnderlyingType();
        Diag(PrevDecl->getLocation(), diag::note_previous_decl) << PrevDecl;
        Invalid = true;

      // Otherwise, diagnose.
      } else {
        // The tag name clashes with something else in the target scope,
        // issue an error and recover by making this tag be anonymous.
        Diag(NameLoc, diag::err_redefinition_different_kind) << Name;
        notePreviousDefinition(PrevDecl, NameLoc);
        Name = nullptr;
        Invalid = true;
      }

      // The existing declaration isn't relevant to us; we're in a
      // new scope, so clear out the previous declaration.
      Previous.clear();
    }
  }

CreateNewDecl:

  TagDecl *PrevDecl = nullptr;
  if (Previous.isSingleResult())
    PrevDecl = cast<TagDecl>(Previous.getFoundDecl());

  // If there is an identifier, use the location of the identifier as the
  // location of the decl, otherwise use the location of the struct/union
  // keyword.
  SourceLocation Loc = NameLoc.isValid() ? NameLoc : KWLoc;

  // Otherwise, create a new declaration. If there is a previous
  // declaration of the same entity, the two will be linked via
  // PrevDecl.
  TagDecl *New;

  if (Kind == TagTypeKind::Enum) {
    // FIXME: Tag decls should be chained to any simultaneous vardecls, e.g.:
    // enum X { A, B, C } D;    D should chain to X.
    New = EnumDecl::Create(Context, SearchDC, KWLoc, Loc, Name,
                           cast_or_null<EnumDecl>(PrevDecl), ScopedEnum,
                           ScopedEnumUsesClassTag, IsFixed);

    if (isStdAlignValT && (!StdAlignValT || getStdAlignValT()->isImplicit()))
      StdAlignValT = cast<EnumDecl>(New);

    // If this is an undefined enum, warn.
    if (TUK != TagUseKind::Definition && !Invalid) {
      TagDecl *Def;
      if (IsFixed && cast<EnumDecl>(New)->isFixed()) {
        // C++0x: 7.2p2: opaque-enum-declaration.
        // Conflicts are diagnosed above. Do nothing.
      }
      else if (PrevDecl && (Def = cast<EnumDecl>(PrevDecl)->getDefinition())) {
        Diag(Loc, diag::ext_forward_ref_enum_def)
          << New;
        Diag(Def->getLocation(), diag::note_previous_definition);
      } else {
        unsigned DiagID = diag::ext_forward_ref_enum;
        if (getLangOpts().MSVCCompat)
          DiagID = diag::ext_ms_forward_ref_enum;
        else if (getLangOpts().CPlusPlus)
          DiagID = diag::err_forward_ref_enum;
        Diag(Loc, DiagID);
      }
    }

    if (EnumUnderlying) {
      EnumDecl *ED = cast<EnumDecl>(New);
      if (TypeSourceInfo *TI = EnumUnderlying.dyn_cast<TypeSourceInfo*>())
        ED->setIntegerTypeSourceInfo(TI);
      else
        ED->setIntegerType(QualType(EnumUnderlying.get<const Type *>(), 0));
      QualType EnumTy = ED->getIntegerType();
      ED->setPromotionType(Context.isPromotableIntegerType(EnumTy)
                               ? Context.getPromotedIntegerType(EnumTy)
                               : EnumTy);
      assert(ED->isComplete() && "enum with type should be complete");
    }
  } else {
    // struct/union/class

    // FIXME: Tag decls should be chained to any simultaneous vardecls, e.g.:
    // struct X { int A; } D;    D should chain to X.
    if (getLangOpts().CPlusPlus) {
      // FIXME: Look for a way to use RecordDecl for simple structs.
      New = CXXRecordDecl::Create(Context, Kind, SearchDC, KWLoc, Loc, Name,
                                  cast_or_null<CXXRecordDecl>(PrevDecl));

      if (isStdBadAlloc && (!StdBadAlloc || getStdBadAlloc()->isImplicit()))
        StdBadAlloc = cast<CXXRecordDecl>(New);
    } else
      New = RecordDecl::Create(Context, Kind, SearchDC, KWLoc, Loc, Name,
                               cast_or_null<RecordDecl>(PrevDecl));
  }

  // Only C23 and later allow defining new types in 'offsetof()'.
  if (OOK != OOK_Outside && TUK == TagUseKind::Definition &&
      !getLangOpts().CPlusPlus && !getLangOpts().C23)
    Diag(New->getLocation(), diag::ext_type_defined_in_offsetof)
        << (OOK == OOK_Macro) << New->getSourceRange();

  // C++11 [dcl.type]p3:
  //   A type-specifier-seq shall not define a class or enumeration [...].
  if (!Invalid && getLangOpts().CPlusPlus &&
      (IsTypeSpecifier || IsTemplateParamOrArg) &&
      TUK == TagUseKind::Definition) {
    Diag(New->getLocation(), diag::err_type_defined_in_type_specifier)
      << Context.getTagDeclType(New);
    Invalid = true;
  }

  if (!Invalid && getLangOpts().CPlusPlus && TUK == TagUseKind::Definition &&
      DC->getDeclKind() == Decl::Enum) {
    Diag(New->getLocation(), diag::err_type_defined_in_enum)
      << Context.getTagDeclType(New);
    Invalid = true;
  }

  // Maybe add qualifier info.
  if (SS.isNotEmpty()) {
    if (SS.isSet()) {
      // If this is either a declaration or a definition, check the
      // nested-name-specifier against the current context.
      if ((TUK == TagUseKind::Definition || TUK == TagUseKind::Declaration) &&
          diagnoseQualifiedDeclaration(SS, DC, OrigName, Loc,
                                       /*TemplateId=*/nullptr,
                                       isMemberSpecialization))
        Invalid = true;

      New->setQualifierInfo(SS.getWithLocInContext(Context));
      if (TemplateParameterLists.size() > 0) {
        New->setTemplateParameterListsInfo(Context, TemplateParameterLists);
      }
    }
    else
      Invalid = true;
  }

  if (RecordDecl *RD = dyn_cast<RecordDecl>(New)) {
    // Add alignment attributes if necessary; these attributes are checked when
    // the ASTContext lays out the structure.
    //
    // It is important for implementing the correct semantics that this
    // happen here (in ActOnTag). The #pragma pack stack is
    // maintained as a result of parser callbacks which can occur at
    // many points during the parsing of a struct declaration (because
    // the #pragma tokens are effectively skipped over during the
    // parsing of the struct).
    if (TUK == TagUseKind::Definition && (!SkipBody || !SkipBody->ShouldSkip)) {
      AddAlignmentAttributesForRecord(RD);
      AddMsStructLayoutForRecord(RD);
    }
  }

  if (ModulePrivateLoc.isValid()) {
    if (isMemberSpecialization)
      Diag(New->getLocation(), diag::err_module_private_specialization)
        << 2
        << FixItHint::CreateRemoval(ModulePrivateLoc);
    // __module_private__ does not apply to local classes. However, we only
    // diagnose this as an error when the declaration specifiers are
    // freestanding. Here, we just ignore the __module_private__.
    else if (!SearchDC->isFunctionOrMethod())
      New->setModulePrivate();
  }

  // If this is a specialization of a member class (of a class template),
  // check the specialization.
  if (isMemberSpecialization && CheckMemberSpecialization(New, Previous))
    Invalid = true;

  // If we're declaring or defining a tag in function prototype scope in C,
  // note that this type can only be used within the function and add it to
  // the list of decls to inject into the function definition scope.
  if ((Name || Kind == TagTypeKind::Enum) &&
      getNonFieldDeclScope(S)->isFunctionPrototypeScope()) {
    if (getLangOpts().CPlusPlus) {
      // C++ [dcl.fct]p6:
      //   Types shall not be defined in return or parameter types.
      if (TUK == TagUseKind::Definition && !IsTypeSpecifier) {
        Diag(Loc, diag::err_type_defined_in_param_type)
            << Name;
        Invalid = true;
      }
    } else if (!PrevDecl) {
      Diag(Loc, diag::warn_decl_in_param_list) << Context.getTagDeclType(New);
    }
  }

  if (Invalid)
    New->setInvalidDecl();

  // Set the lexical context. If the tag has a C++ scope specifier, the
  // lexical context will be different from the semantic context.
  New->setLexicalDeclContext(CurContext);

  // Mark this as a friend decl if applicable.
  // In Microsoft mode, a friend declaration also acts as a forward
  // declaration so we always pass true to setObjectOfFriendDecl to make
  // the tag name visible.
  if (TUK == TagUseKind::Friend)
    New->setObjectOfFriendDecl(getLangOpts().MSVCCompat);

  // Set the access specifier.
  if (!Invalid && SearchDC->isRecord())
    SetMemberAccessSpecifier(New, PrevDecl, AS);

  if (PrevDecl)
    CheckRedeclarationInModule(New, PrevDecl);

  if (TUK == TagUseKind::Definition && (!SkipBody || !SkipBody->ShouldSkip))
    New->startDefinition();

  ProcessDeclAttributeList(S, New, Attrs);
  AddPragmaAttributes(S, New);

  // If this has an identifier, add it to the scope stack.
  if (TUK == TagUseKind::Friend) {
    // We might be replacing an existing declaration in the lookup tables;
    // if so, borrow its access specifier.
    if (PrevDecl)
      New->setAccess(PrevDecl->getAccess());

    DeclContext *DC = New->getDeclContext()->getRedeclContext();
    DC->makeDeclVisibleInContext(New);
    if (Name) // can be null along some error paths
      if (Scope *EnclosingScope = getScopeForDeclContext(S, DC))
        PushOnScopeChains(New, EnclosingScope, /* AddToContext = */ false);
  } else if (Name) {
    S = getNonFieldDeclScope(S);
    PushOnScopeChains(New, S, true);
  } else {
    CurContext->addDecl(New);
  }

  // If this is the C FILE type, notify the AST context.
  if (IdentifierInfo *II = New->getIdentifier())
    if (!New->isInvalidDecl() &&
        New->getDeclContext()->getRedeclContext()->isTranslationUnit() &&
        II->isStr("FILE"))
      Context.setFILEDecl(New);

  if (PrevDecl)
    mergeDeclAttributes(New, PrevDecl);

  if (auto *CXXRD = dyn_cast<CXXRecordDecl>(New)) {
    inferGslOwnerPointerAttribute(CXXRD);
    inferNullableClassAttribute(CXXRD);
  }

  // If there's a #pragma GCC visibility in scope, set the visibility of this
  // record.
  AddPushedVisibilityAttribute(New);

  if (isMemberSpecialization && !New->isInvalidDecl())
    CompleteMemberSpecialization(New, Previous);

  OwnedDecl = true;
  // In C++, don't return an invalid declaration. We can't recover well from
  // the cases where we make the type anonymous.
  if (Invalid && getLangOpts().CPlusPlus) {
    if (New->isBeingDefined())
      if (auto RD = dyn_cast<RecordDecl>(New))
        RD->completeDefinition();
    return true;
  } else if (SkipBody && SkipBody->ShouldSkip) {
    return SkipBody->Previous;
  } else {
    return New;
  }
}

void Sema::ActOnTagStartDefinition(Scope *S, Decl *TagD) {
  AdjustDeclIfTemplate(TagD);
  TagDecl *Tag = cast<TagDecl>(TagD);

  // Enter the tag context.
  PushDeclContext(S, Tag);

  ActOnDocumentableDecl(TagD);

  // If there's a #pragma GCC visibility in scope, set the visibility of this
  // record.
  AddPushedVisibilityAttribute(Tag);
}

bool Sema::ActOnDuplicateDefinition(Decl *Prev, SkipBodyInfo &SkipBody) {
  if (!hasStructuralCompatLayout(Prev, SkipBody.New))
    return false;

  // Make the previous decl visible.
  makeMergedDefinitionVisible(SkipBody.Previous);
  return true;
}

void Sema::ActOnStartCXXMemberDeclarations(Scope *S, Decl *TagD,
                                           SourceLocation FinalLoc,
                                           bool IsFinalSpelledSealed,
                                           bool IsAbstract,
                                           SourceLocation LBraceLoc) {
  AdjustDeclIfTemplate(TagD);
  CXXRecordDecl *Record = cast<CXXRecordDecl>(TagD);

  FieldCollector->StartClass();

  if (!Record->getIdentifier())
    return;

  if (IsAbstract)
    Record->markAbstract();

  if (FinalLoc.isValid()) {
    Record->addAttr(FinalAttr::Create(Context, FinalLoc,
                                      IsFinalSpelledSealed
                                          ? FinalAttr::Keyword_sealed
                                          : FinalAttr::Keyword_final));
  }
  // C++ [class]p2:
  //   [...] The class-name is also inserted into the scope of the
  //   class itself; this is known as the injected-class-name. For
  //   purposes of access checking, the injected-class-name is treated
  //   as if it were a public member name.
  CXXRecordDecl *InjectedClassName = CXXRecordDecl::Create(
      Context, Record->getTagKind(), CurContext, Record->getBeginLoc(),
      Record->getLocation(), Record->getIdentifier(),
      /*PrevDecl=*/nullptr,
      /*DelayTypeCreation=*/true);
  Context.getTypeDeclType(InjectedClassName, Record);
  InjectedClassName->setImplicit();
  InjectedClassName->setAccess(AS_public);
  if (ClassTemplateDecl *Template = Record->getDescribedClassTemplate())
      InjectedClassName->setDescribedClassTemplate(Template);
  PushOnScopeChains(InjectedClassName, S);
  assert(InjectedClassName->isInjectedClassName() &&
         "Broken injected-class-name");
}

void Sema::ActOnTagFinishDefinition(Scope *S, Decl *TagD,
                                    SourceRange BraceRange) {
  AdjustDeclIfTemplate(TagD);
  TagDecl *Tag = cast<TagDecl>(TagD);
  Tag->setBraceRange(BraceRange);

  // Make sure we "complete" the definition even it is invalid.
  if (Tag->isBeingDefined()) {
    assert(Tag->isInvalidDecl() && "We should already have completed it");
    if (RecordDecl *RD = dyn_cast<RecordDecl>(Tag))
      RD->completeDefinition();
  }

  if (auto *RD = dyn_cast<CXXRecordDecl>(Tag)) {
    FieldCollector->FinishClass();
    if (RD->hasAttr<SYCLSpecialClassAttr>()) {
      auto *Def = RD->getDefinition();
      assert(Def && "The record is expected to have a completed definition");
      unsigned NumInitMethods = 0;
      for (auto *Method : Def->methods()) {
        if (!Method->getIdentifier())
            continue;
        if (Method->getName() == "__init")
          NumInitMethods++;
      }
      if (NumInitMethods > 1 || !Def->hasInitMethod())
        Diag(RD->getLocation(), diag::err_sycl_special_type_num_init_method);
    }

    // If we're defining a dynamic class in a module interface unit, we always
    // need to produce the vtable for it, even if the vtable is not used in the
    // current TU.
    //
    // The case where the current class is not dynamic is handled in
    // MarkVTableUsed.
    if (getCurrentModule() && getCurrentModule()->isInterfaceOrPartition())
      MarkVTableUsed(RD->getLocation(), RD, /*DefinitionRequired=*/true);
  }

  // Exit this scope of this tag's definition.
  PopDeclContext();

  if (getCurLexicalContext()->isObjCContainer() &&
      Tag->getDeclContext()->isFileContext())
    Tag->setTopLevelDeclInObjCContainer();

  // Notify the consumer that we've defined a tag.
  if (!Tag->isInvalidDecl())
    Consumer.HandleTagDeclDefinition(Tag);

  // Clangs implementation of #pragma align(packed) differs in bitfield layout
  // from XLs and instead matches the XL #pragma pack(1) behavior.
  if (Context.getTargetInfo().getTriple().isOSAIX() &&
      AlignPackStack.hasValue()) {
    AlignPackInfo APInfo = AlignPackStack.CurrentValue;
    // Only diagnose #pragma align(packed).
    if (!APInfo.IsAlignAttr() || APInfo.getAlignMode() != AlignPackInfo::Packed)
      return;
    const RecordDecl *RD = dyn_cast<RecordDecl>(Tag);
    if (!RD)
      return;
    // Only warn if there is at least 1 bitfield member.
    if (llvm::any_of(RD->fields(),
                     [](const FieldDecl *FD) { return FD->isBitField(); }))
      Diag(BraceRange.getBegin(), diag::warn_pragma_align_not_xl_compatible);
  }
}

void Sema::ActOnTagDefinitionError(Scope *S, Decl *TagD) {
  AdjustDeclIfTemplate(TagD);
  TagDecl *Tag = cast<TagDecl>(TagD);
  Tag->setInvalidDecl();

  // Make sure we "complete" the definition even it is invalid.
  if (Tag->isBeingDefined()) {
    if (RecordDecl *RD = dyn_cast<RecordDecl>(Tag))
      RD->completeDefinition();
  }

  // We're undoing ActOnTagStartDefinition here, not
  // ActOnStartCXXMemberDeclarations, so we don't have to mess with
  // the FieldCollector.

  PopDeclContext();
}

// Note that FieldName may be null for anonymous bitfields.
ExprResult Sema::VerifyBitField(SourceLocation FieldLoc,
                                const IdentifierInfo *FieldName,
                                QualType FieldTy, bool IsMsStruct,
                                Expr *BitWidth) {
  assert(BitWidth);
  if (BitWidth->containsErrors())
    return ExprError();

  // C99 6.7.2.1p4 - verify the field type.
  // C++ 9.6p3: A bit-field shall have integral or enumeration type.
  if (!FieldTy->isDependentType() && !FieldTy->isIntegralOrEnumerationType()) {
    // Handle incomplete and sizeless types with a specific error.
    if (RequireCompleteSizedType(FieldLoc, FieldTy,
                                 diag::err_field_incomplete_or_sizeless))
      return ExprError();
    if (FieldName)
      return Diag(FieldLoc, diag::err_not_integral_type_bitfield)
        << FieldName << FieldTy << BitWidth->getSourceRange();
    return Diag(FieldLoc, diag::err_not_integral_type_anon_bitfield)
      << FieldTy << BitWidth->getSourceRange();
  } else if (DiagnoseUnexpandedParameterPack(const_cast<Expr *>(BitWidth),
                                             UPPC_BitFieldWidth))
    return ExprError();

  // If the bit-width is type- or value-dependent, don't try to check
  // it now.
  if (BitWidth->isValueDependent() || BitWidth->isTypeDependent())
    return BitWidth;

  llvm::APSInt Value;
  ExprResult ICE = VerifyIntegerConstantExpression(BitWidth, &Value, AllowFold);
  if (ICE.isInvalid())
    return ICE;
  BitWidth = ICE.get();

  // Zero-width bitfield is ok for anonymous field.
  if (Value == 0 && FieldName)
    return Diag(FieldLoc, diag::err_bitfield_has_zero_width)
           << FieldName << BitWidth->getSourceRange();

  if (Value.isSigned() && Value.isNegative()) {
    if (FieldName)
      return Diag(FieldLoc, diag::err_bitfield_has_negative_width)
               << FieldName << toString(Value, 10);
    return Diag(FieldLoc, diag::err_anon_bitfield_has_negative_width)
      << toString(Value, 10);
  }

  // The size of the bit-field must not exceed our maximum permitted object
  // size.
  if (Value.getActiveBits() > ConstantArrayType::getMaxSizeBits(Context)) {
    return Diag(FieldLoc, diag::err_bitfield_too_wide)
           << !FieldName << FieldName << toString(Value, 10);
  }

  if (!FieldTy->isDependentType()) {
    uint64_t TypeStorageSize = Context.getTypeSize(FieldTy);
    uint64_t TypeWidth = Context.getIntWidth(FieldTy);
    bool BitfieldIsOverwide = Value.ugt(TypeWidth);

    // Over-wide bitfields are an error in C or when using the MSVC bitfield
    // ABI.
    bool CStdConstraintViolation =
        BitfieldIsOverwide && !getLangOpts().CPlusPlus;
    bool MSBitfieldViolation =
        Value.ugt(TypeStorageSize) &&
        (IsMsStruct || Context.getTargetInfo().getCXXABI().isMicrosoft());
    if (CStdConstraintViolation || MSBitfieldViolation) {
      unsigned DiagWidth =
          CStdConstraintViolation ? TypeWidth : TypeStorageSize;
      return Diag(FieldLoc, diag::err_bitfield_width_exceeds_type_width)
             << (bool)FieldName << FieldName << toString(Value, 10)
             << !CStdConstraintViolation << DiagWidth;
    }

    // Warn on types where the user might conceivably expect to get all
    // specified bits as value bits: that's all integral types other than
    // 'bool'.
    if (BitfieldIsOverwide && !FieldTy->isBooleanType() && FieldName) {
      Diag(FieldLoc, diag::warn_bitfield_width_exceeds_type_width)
          << FieldName << toString(Value, 10)
          << (unsigned)TypeWidth;
    }
  }

  return BitWidth;
}

Decl *Sema::ActOnField(Scope *S, Decl *TagD, SourceLocation DeclStart,
                       Declarator &D, Expr *BitfieldWidth) {
  FieldDecl *Res = HandleField(S, cast_if_present<RecordDecl>(TagD), DeclStart,
                               D, BitfieldWidth,
                               /*InitStyle=*/ICIS_NoInit, AS_public);
  return Res;
}

FieldDecl *Sema::HandleField(Scope *S, RecordDecl *Record,
                             SourceLocation DeclStart,
                             Declarator &D, Expr *BitWidth,
                             InClassInitStyle InitStyle,
                             AccessSpecifier AS) {
  if (D.isDecompositionDeclarator()) {
    const DecompositionDeclarator &Decomp = D.getDecompositionDeclarator();
    Diag(Decomp.getLSquareLoc(), diag::err_decomp_decl_context)
      << Decomp.getSourceRange();
    return nullptr;
  }

  const IdentifierInfo *II = D.getIdentifier();
  SourceLocation Loc = DeclStart;
  if (II) Loc = D.getIdentifierLoc();

  TypeSourceInfo *TInfo = GetTypeForDeclarator(D);
  QualType T = TInfo->getType();
  if (getLangOpts().CPlusPlus) {
    CheckExtraCXXDefaultArguments(D);

    if (DiagnoseUnexpandedParameterPack(D.getIdentifierLoc(), TInfo,
                                        UPPC_DataMemberType)) {
      D.setInvalidType();
      T = Context.IntTy;
      TInfo = Context.getTrivialTypeSourceInfo(T, Loc);
    }
  }

  DiagnoseFunctionSpecifiers(D.getDeclSpec());

  if (D.getDeclSpec().isInlineSpecified())
    Diag(D.getDeclSpec().getInlineSpecLoc(), diag::err_inline_non_function)
        << getLangOpts().CPlusPlus17;
  if (DeclSpec::TSCS TSCS = D.getDeclSpec().getThreadStorageClassSpec())
    Diag(D.getDeclSpec().getThreadStorageClassSpecLoc(),
         diag::err_invalid_thread)
      << DeclSpec::getSpecifierName(TSCS);

  // Check to see if this name was declared as a member previously
  NamedDecl *PrevDecl = nullptr;
  LookupResult Previous(*this, II, Loc, LookupMemberName,
                        RedeclarationKind::ForVisibleRedeclaration);
  LookupName(Previous, S);
  switch (Previous.getResultKind()) {
    case LookupResult::Found:
    case LookupResult::FoundUnresolvedValue:
      PrevDecl = Previous.getAsSingle<NamedDecl>();
      break;

    case LookupResult::FoundOverloaded:
      PrevDecl = Previous.getRepresentativeDecl();
      break;

    case LookupResult::NotFound:
    case LookupResult::NotFoundInCurrentInstantiation:
    case LookupResult::Ambiguous:
      break;
  }
  Previous.suppressDiagnostics();

  if (PrevDecl && PrevDecl->isTemplateParameter()) {
    // Maybe we will complain about the shadowed template parameter.
    DiagnoseTemplateParameterShadow(D.getIdentifierLoc(), PrevDecl);
    // Just pretend that we didn't see the previous declaration.
    PrevDecl = nullptr;
  }

  if (PrevDecl && !isDeclInScope(PrevDecl, Record, S))
    PrevDecl = nullptr;

  bool Mutable
    = (D.getDeclSpec().getStorageClassSpec() == DeclSpec::SCS_mutable);
  SourceLocation TSSL = D.getBeginLoc();
  FieldDecl *NewFD
    = CheckFieldDecl(II, T, TInfo, Record, Loc, Mutable, BitWidth, InitStyle,
                     TSSL, AS, PrevDecl, &D);

  if (NewFD->isInvalidDecl())
    Record->setInvalidDecl();

  if (D.getDeclSpec().isModulePrivateSpecified())
    NewFD->setModulePrivate();

  if (NewFD->isInvalidDecl() && PrevDecl) {
    // Don't introduce NewFD into scope; there's already something
    // with the same name in the same scope.
  } else if (II) {
    PushOnScopeChains(NewFD, S);
  } else
    Record->addDecl(NewFD);

  return NewFD;
}

FieldDecl *Sema::CheckFieldDecl(DeclarationName Name, QualType T,
                                TypeSourceInfo *TInfo,
                                RecordDecl *Record, SourceLocation Loc,
                                bool Mutable, Expr *BitWidth,
                                InClassInitStyle InitStyle,
                                SourceLocation TSSL,
                                AccessSpecifier AS, NamedDecl *PrevDecl,
                                Declarator *D) {
  const IdentifierInfo *II = Name.getAsIdentifierInfo();
  bool InvalidDecl = false;
  if (D) InvalidDecl = D->isInvalidType();

  // If we receive a broken type, recover by assuming 'int' and
  // marking this declaration as invalid.
  if (T.isNull() || T->containsErrors()) {
    InvalidDecl = true;
    T = Context.IntTy;
  }

  QualType EltTy = Context.getBaseElementType(T);
  if (!EltTy->isDependentType() && !EltTy->containsErrors()) {
    if (RequireCompleteSizedType(Loc, EltTy,
                                 diag::err_field_incomplete_or_sizeless)) {
      // Fields of incomplete type force their record to be invalid.
      Record->setInvalidDecl();
      InvalidDecl = true;
    } else {
      NamedDecl *Def;
      EltTy->isIncompleteType(&Def);
      if (Def && Def->isInvalidDecl()) {
        Record->setInvalidDecl();
        InvalidDecl = true;
      }
    }
  }

  // TR 18037 does not allow fields to be declared with address space
  if (T.hasAddressSpace() || T->isDependentAddressSpaceType() ||
      T->getBaseElementTypeUnsafe()->isDependentAddressSpaceType()) {
    Diag(Loc, diag::err_field_with_address_space);
    Record->setInvalidDecl();
    InvalidDecl = true;
  }

  if (LangOpts.OpenCL) {
    // OpenCL v1.2 s6.9b,r & OpenCL v2.0 s6.12.5 - The following types cannot be
    // used as structure or union field: image, sampler, event or block types.
    if (T->isEventT() || T->isImageType() || T->isSamplerT() ||
        T->isBlockPointerType()) {
      Diag(Loc, diag::err_opencl_type_struct_or_union_field) << T;
      Record->setInvalidDecl();
      InvalidDecl = true;
    }
    // OpenCL v1.2 s6.9.c: bitfields are not supported, unless Clang extension
    // is enabled.
    if (BitWidth && !getOpenCLOptions().isAvailableOption(
                        "__cl_clang_bitfields", LangOpts)) {
      Diag(Loc, diag::err_opencl_bitfields);
      InvalidDecl = true;
    }
  }

  // Anonymous bit-fields cannot be cv-qualified (CWG 2229).
  if (!InvalidDecl && getLangOpts().CPlusPlus && !II && BitWidth &&
      T.hasQualifiers()) {
    InvalidDecl = true;
    Diag(Loc, diag::err_anon_bitfield_qualifiers);
  }

  // C99 6.7.2.1p8: A member of a structure or union may have any type other
  // than a variably modified type.
  if (!InvalidDecl && T->isVariablyModifiedType()) {
    if (!tryToFixVariablyModifiedVarType(
            TInfo, T, Loc, diag::err_typecheck_field_variable_size))
      InvalidDecl = true;
  }

  // Fields can not have abstract class types
  if (!InvalidDecl && RequireNonAbstractType(Loc, T,
                                             diag::err_abstract_type_in_decl,
                                             AbstractFieldType))
    InvalidDecl = true;

  if (InvalidDecl)
    BitWidth = nullptr;
  // If this is declared as a bit-field, check the bit-field.
  if (BitWidth) {
    BitWidth =
        VerifyBitField(Loc, II, T, Record->isMsStruct(Context), BitWidth).get();
    if (!BitWidth) {
      InvalidDecl = true;
      BitWidth = nullptr;
    }
  }

  // Check that 'mutable' is consistent with the type of the declaration.
  if (!InvalidDecl && Mutable) {
    unsigned DiagID = 0;
    if (T->isReferenceType())
      DiagID = getLangOpts().MSVCCompat ? diag::ext_mutable_reference
                                        : diag::err_mutable_reference;
    else if (T.isConstQualified())
      DiagID = diag::err_mutable_const;

    if (DiagID) {
      SourceLocation ErrLoc = Loc;
      if (D && D->getDeclSpec().getStorageClassSpecLoc().isValid())
        ErrLoc = D->getDeclSpec().getStorageClassSpecLoc();
      Diag(ErrLoc, DiagID);
      if (DiagID != diag::ext_mutable_reference) {
        Mutable = false;
        InvalidDecl = true;
      }
    }
  }

  // C++11 [class.union]p8 (DR1460):
  //   At most one variant member of a union may have a
  //   brace-or-equal-initializer.
  if (InitStyle != ICIS_NoInit)
    checkDuplicateDefaultInit(*this, cast<CXXRecordDecl>(Record), Loc);

  FieldDecl *NewFD = FieldDecl::Create(Context, Record, TSSL, Loc, II, T, TInfo,
                                       BitWidth, Mutable, InitStyle);
  if (InvalidDecl)
    NewFD->setInvalidDecl();

  if (PrevDecl && !isa<TagDecl>(PrevDecl) &&
      !PrevDecl->isPlaceholderVar(getLangOpts())) {
    Diag(Loc, diag::err_duplicate_member) << II;
    Diag(PrevDecl->getLocation(), diag::note_previous_declaration);
    NewFD->setInvalidDecl();
  }

  if (!InvalidDecl && getLangOpts().CPlusPlus) {
    if (Record->isUnion()) {
      if (const RecordType *RT = EltTy->getAs<RecordType>()) {
        CXXRecordDecl* RDecl = cast<CXXRecordDecl>(RT->getDecl());
        if (RDecl->getDefinition()) {
          // C++ [class.union]p1: An object of a class with a non-trivial
          // constructor, a non-trivial copy constructor, a non-trivial
          // destructor, or a non-trivial copy assignment operator
          // cannot be a member of a union, nor can an array of such
          // objects.
          if (CheckNontrivialField(NewFD))
            NewFD->setInvalidDecl();
        }
      }

      // C++ [class.union]p1: If a union contains a member of reference type,
      // the program is ill-formed, except when compiling with MSVC extensions
      // enabled.
      if (EltTy->isReferenceType()) {
        Diag(NewFD->getLocation(), getLangOpts().MicrosoftExt ?
                                    diag::ext_union_member_of_reference_type :
                                    diag::err_union_member_of_reference_type)
          << NewFD->getDeclName() << EltTy;
        if (!getLangOpts().MicrosoftExt)
          NewFD->setInvalidDecl();
      }
    }
  }

  // FIXME: We need to pass in the attributes given an AST
  // representation, not a parser representation.
  if (D) {
    // FIXME: The current scope is almost... but not entirely... correct here.
    ProcessDeclAttributes(getCurScope(), NewFD, *D);

    if (NewFD->hasAttrs())
      CheckAlignasUnderalignment(NewFD);
  }

  // In auto-retain/release, infer strong retension for fields of
  // retainable type.
  if (getLangOpts().ObjCAutoRefCount && ObjC().inferObjCARCLifetime(NewFD))
    NewFD->setInvalidDecl();

  if (T.isObjCGCWeak())
    Diag(Loc, diag::warn_attribute_weak_on_field);

  // PPC MMA non-pointer types are not allowed as field types.
  if (Context.getTargetInfo().getTriple().isPPC64() &&
      PPC().CheckPPCMMAType(T, NewFD->getLocation()))
    NewFD->setInvalidDecl();

  NewFD->setAccess(AS);
  return NewFD;
}

bool Sema::CheckNontrivialField(FieldDecl *FD) {
  assert(FD);
  assert(getLangOpts().CPlusPlus && "valid check only for C++");

  if (FD->isInvalidDecl() || FD->getType()->isDependentType())
    return false;

  QualType EltTy = Context.getBaseElementType(FD->getType());
  if (const RecordType *RT = EltTy->getAs<RecordType>()) {
    CXXRecordDecl *RDecl = cast<CXXRecordDecl>(RT->getDecl());
    if (RDecl->getDefinition()) {
      // We check for copy constructors before constructors
      // because otherwise we'll never get complaints about
      // copy constructors.

      CXXSpecialMemberKind member = CXXSpecialMemberKind::Invalid;
      // We're required to check for any non-trivial constructors. Since the
      // implicit default constructor is suppressed if there are any
      // user-declared constructors, we just need to check that there is a
      // trivial default constructor and a trivial copy constructor. (We don't
      // worry about move constructors here, since this is a C++98 check.)
      if (RDecl->hasNonTrivialCopyConstructor())
        member = CXXSpecialMemberKind::CopyConstructor;
      else if (!RDecl->hasTrivialDefaultConstructor())
        member = CXXSpecialMemberKind::DefaultConstructor;
      else if (RDecl->hasNonTrivialCopyAssignment())
        member = CXXSpecialMemberKind::CopyAssignment;
      else if (RDecl->hasNonTrivialDestructor())
        member = CXXSpecialMemberKind::Destructor;

      if (member != CXXSpecialMemberKind::Invalid) {
        if (!getLangOpts().CPlusPlus11 &&
            getLangOpts().ObjCAutoRefCount && RDecl->hasObjectMember()) {
          // Objective-C++ ARC: it is an error to have a non-trivial field of
          // a union. However, system headers in Objective-C programs
          // occasionally have Objective-C lifetime objects within unions,
          // and rather than cause the program to fail, we make those
          // members unavailable.
          SourceLocation Loc = FD->getLocation();
          if (getSourceManager().isInSystemHeader(Loc)) {
            if (!FD->hasAttr<UnavailableAttr>())
              FD->addAttr(UnavailableAttr::CreateImplicit(Context, "",
                            UnavailableAttr::IR_ARCFieldWithOwnership, Loc));
            return false;
          }
        }

        Diag(
            FD->getLocation(),
            getLangOpts().CPlusPlus11
                ? diag::warn_cxx98_compat_nontrivial_union_or_anon_struct_member
                : diag::err_illegal_union_or_anon_struct_member)
            << FD->getParent()->isUnion() << FD->getDeclName()
            << llvm::to_underlying(member);
        DiagnoseNontrivial(RDecl, member);
        return !getLangOpts().CPlusPlus11;
      }
    }
  }

  return false;
}

void Sema::ActOnLastBitfield(SourceLocation DeclLoc,
                             SmallVectorImpl<Decl *> &AllIvarDecls) {
  if (LangOpts.ObjCRuntime.isFragile() || AllIvarDecls.empty())
    return;

  Decl *ivarDecl = AllIvarDecls[AllIvarDecls.size()-1];
  ObjCIvarDecl *Ivar = cast<ObjCIvarDecl>(ivarDecl);

  if (!Ivar->isBitField() || Ivar->isZeroLengthBitField(Context))
    return;
  ObjCInterfaceDecl *ID = dyn_cast<ObjCInterfaceDecl>(CurContext);
  if (!ID) {
    if (ObjCCategoryDecl *CD = dyn_cast<ObjCCategoryDecl>(CurContext)) {
      if (!CD->IsClassExtension())
        return;
    }
    // No need to add this to end of @implementation.
    else
      return;
  }
  // All conditions are met. Add a new bitfield to the tail end of ivars.
  llvm::APInt Zero(Context.getTypeSize(Context.IntTy), 0);
  Expr * BW = IntegerLiteral::Create(Context, Zero, Context.IntTy, DeclLoc);

  Ivar = ObjCIvarDecl::Create(Context, cast<ObjCContainerDecl>(CurContext),
                              DeclLoc, DeclLoc, nullptr,
                              Context.CharTy,
                              Context.getTrivialTypeSourceInfo(Context.CharTy,
                                                               DeclLoc),
                              ObjCIvarDecl::Private, BW,
                              true);
  AllIvarDecls.push_back(Ivar);
}

/// [class.dtor]p4:
///   At the end of the definition of a class, overload resolution is
///   performed among the prospective destructors declared in that class with
///   an empty argument list to select the destructor for the class, also
///   known as the selected destructor.
///
/// We do the overload resolution here, then mark the selected constructor in the AST.
/// Later CXXRecordDecl::getDestructor() will return the selected constructor.
static void ComputeSelectedDestructor(Sema &S, CXXRecordDecl *Record) {
  if (!Record->hasUserDeclaredDestructor()) {
    return;
  }

  SourceLocation Loc = Record->getLocation();
  OverloadCandidateSet OCS(Loc, OverloadCandidateSet::CSK_Normal);

  for (auto *Decl : Record->decls()) {
    if (auto *DD = dyn_cast<CXXDestructorDecl>(Decl)) {
      if (DD->isInvalidDecl())
        continue;
      S.AddOverloadCandidate(DD, DeclAccessPair::make(DD, DD->getAccess()), {},
                             OCS);
      assert(DD->isIneligibleOrNotSelected() && "Selecting a destructor but a destructor was already selected.");
    }
  }

  if (OCS.empty()) {
    return;
  }
  OverloadCandidateSet::iterator Best;
  unsigned Msg = 0;
  OverloadCandidateDisplayKind DisplayKind;

  switch (OCS.BestViableFunction(S, Loc, Best)) {
  case OR_Success:
  case OR_Deleted:
    Record->addedSelectedDestructor(dyn_cast<CXXDestructorDecl>(Best->Function));
    break;

  case OR_Ambiguous:
    Msg = diag::err_ambiguous_destructor;
    DisplayKind = OCD_AmbiguousCandidates;
    break;

  case OR_No_Viable_Function:
    Msg = diag::err_no_viable_destructor;
    DisplayKind = OCD_AllCandidates;
    break;
  }

  if (Msg) {
    // OpenCL have got their own thing going with destructors. It's slightly broken,
    // but we allow it.
    if (!S.LangOpts.OpenCL) {
      PartialDiagnostic Diag = S.PDiag(Msg) << Record;
      OCS.NoteCandidates(PartialDiagnosticAt(Loc, Diag), S, DisplayKind, {});
      Record->setInvalidDecl();
    }
    // It's a bit hacky: At this point we've raised an error but we want the
    // rest of the compiler to continue somehow working. However almost
    // everything we'll try to do with the class will depend on there being a
    // destructor. So let's pretend the first one is selected and hope for the
    // best.
    Record->addedSelectedDestructor(dyn_cast<CXXDestructorDecl>(OCS.begin()->Function));
  }
}

/// [class.mem.special]p5
/// Two special member functions are of the same kind if:
/// - they are both default constructors,
/// - they are both copy or move constructors with the same first parameter
///   type, or
/// - they are both copy or move assignment operators with the same first
///   parameter type and the same cv-qualifiers and ref-qualifier, if any.
static bool AreSpecialMemberFunctionsSameKind(ASTContext &Context,
                                              CXXMethodDecl *M1,
                                              CXXMethodDecl *M2,
                                              CXXSpecialMemberKind CSM) {
  // We don't want to compare templates to non-templates: See
  // https://github.com/llvm/llvm-project/issues/59206
  if (CSM == CXXSpecialMemberKind::DefaultConstructor)
    return bool(M1->getDescribedFunctionTemplate()) ==
           bool(M2->getDescribedFunctionTemplate());
  // FIXME: better resolve CWG
  // https://cplusplus.github.io/CWG/issues/2787.html
  if (!Context.hasSameType(M1->getNonObjectParameter(0)->getType(),
                           M2->getNonObjectParameter(0)->getType()))
    return false;
  if (!Context.hasSameType(M1->getFunctionObjectParameterReferenceType(),
                           M2->getFunctionObjectParameterReferenceType()))
    return false;

  return true;
}

/// [class.mem.special]p6:
/// An eligible special member function is a special member function for which:
/// - the function is not deleted,
/// - the associated constraints, if any, are satisfied, and
/// - no special member function of the same kind whose associated constraints
///   [CWG2595], if any, are satisfied is more constrained.
static void SetEligibleMethods(Sema &S, CXXRecordDecl *Record,
                               ArrayRef<CXXMethodDecl *> Methods,
                               CXXSpecialMemberKind CSM) {
  SmallVector<bool, 4> SatisfactionStatus;

  for (CXXMethodDecl *Method : Methods) {
    const Expr *Constraints = Method->getTrailingRequiresClause();
    if (!Constraints)
      SatisfactionStatus.push_back(true);
    else {
      ConstraintSatisfaction Satisfaction;
      if (S.CheckFunctionConstraints(Method, Satisfaction))
        SatisfactionStatus.push_back(false);
      else
        SatisfactionStatus.push_back(Satisfaction.IsSatisfied);
    }
  }

  for (size_t i = 0; i < Methods.size(); i++) {
    if (!SatisfactionStatus[i])
      continue;
    CXXMethodDecl *Method = Methods[i];
    CXXMethodDecl *OrigMethod = Method;
    if (FunctionDecl *MF = OrigMethod->getInstantiatedFromMemberFunction())
      OrigMethod = cast<CXXMethodDecl>(MF);

    const Expr *Constraints = OrigMethod->getTrailingRequiresClause();
    bool AnotherMethodIsMoreConstrained = false;
    for (size_t j = 0; j < Methods.size(); j++) {
      if (i == j || !SatisfactionStatus[j])
        continue;
      CXXMethodDecl *OtherMethod = Methods[j];
      if (FunctionDecl *MF = OtherMethod->getInstantiatedFromMemberFunction())
        OtherMethod = cast<CXXMethodDecl>(MF);

      if (!AreSpecialMemberFunctionsSameKind(S.Context, OrigMethod, OtherMethod,
                                             CSM))
        continue;

      const Expr *OtherConstraints = OtherMethod->getTrailingRequiresClause();
      if (!OtherConstraints)
        continue;
      if (!Constraints) {
        AnotherMethodIsMoreConstrained = true;
        break;
      }
      if (S.IsAtLeastAsConstrained(OtherMethod, {OtherConstraints}, OrigMethod,
                                   {Constraints},
                                   AnotherMethodIsMoreConstrained)) {
        // There was an error with the constraints comparison. Exit the loop
        // and don't consider this function eligible.
        AnotherMethodIsMoreConstrained = true;
      }
      if (AnotherMethodIsMoreConstrained)
        break;
    }
    // FIXME: Do not consider deleted methods as eligible after implementing
    // DR1734 and DR1496.
    if (!AnotherMethodIsMoreConstrained) {
      Method->setIneligibleOrNotSelected(false);
      Record->addedEligibleSpecialMemberFunction(Method,
                                                 1 << llvm::to_underlying(CSM));
    }
  }
}

static void ComputeSpecialMemberFunctionsEligiblity(Sema &S,
                                                    CXXRecordDecl *Record) {
  SmallVector<CXXMethodDecl *, 4> DefaultConstructors;
  SmallVector<CXXMethodDecl *, 4> CopyConstructors;
  SmallVector<CXXMethodDecl *, 4> MoveConstructors;
  SmallVector<CXXMethodDecl *, 4> CopyAssignmentOperators;
  SmallVector<CXXMethodDecl *, 4> MoveAssignmentOperators;

  for (auto *Decl : Record->decls()) {
    auto *MD = dyn_cast<CXXMethodDecl>(Decl);
    if (!MD) {
      auto *FTD = dyn_cast<FunctionTemplateDecl>(Decl);
      if (FTD)
        MD = dyn_cast<CXXMethodDecl>(FTD->getTemplatedDecl());
    }
    if (!MD)
      continue;
    if (auto *CD = dyn_cast<CXXConstructorDecl>(MD)) {
      if (CD->isInvalidDecl())
        continue;
      if (CD->isDefaultConstructor())
        DefaultConstructors.push_back(MD);
      else if (CD->isCopyConstructor())
        CopyConstructors.push_back(MD);
      else if (CD->isMoveConstructor())
        MoveConstructors.push_back(MD);
    } else if (MD->isCopyAssignmentOperator()) {
      CopyAssignmentOperators.push_back(MD);
    } else if (MD->isMoveAssignmentOperator()) {
      MoveAssignmentOperators.push_back(MD);
    }
  }

  SetEligibleMethods(S, Record, DefaultConstructors,
                     CXXSpecialMemberKind::DefaultConstructor);
  SetEligibleMethods(S, Record, CopyConstructors,
                     CXXSpecialMemberKind::CopyConstructor);
  SetEligibleMethods(S, Record, MoveConstructors,
                     CXXSpecialMemberKind::MoveConstructor);
  SetEligibleMethods(S, Record, CopyAssignmentOperators,
                     CXXSpecialMemberKind::CopyAssignment);
  SetEligibleMethods(S, Record, MoveAssignmentOperators,
                     CXXSpecialMemberKind::MoveAssignment);
}

void Sema::ActOnFields(Scope *S, SourceLocation RecLoc, Decl *EnclosingDecl,
                       ArrayRef<Decl *> Fields, SourceLocation LBrac,
                       SourceLocation RBrac,
                       const ParsedAttributesView &Attrs) {
  assert(EnclosingDecl && "missing record or interface decl");

  // If this is an Objective-C @implementation or category and we have
  // new fields here we should reset the layout of the interface since
  // it will now change.
  if (!Fields.empty() && isa<ObjCContainerDecl>(EnclosingDecl)) {
    ObjCContainerDecl *DC = cast<ObjCContainerDecl>(EnclosingDecl);
    switch (DC->getKind()) {
    default: break;
    case Decl::ObjCCategory:
      Context.ResetObjCLayout(cast<ObjCCategoryDecl>(DC)->getClassInterface());
      break;
    case Decl::ObjCImplementation:
      Context.
        ResetObjCLayout(cast<ObjCImplementationDecl>(DC)->getClassInterface());
      break;
    }
  }

  RecordDecl *Record = dyn_cast<RecordDecl>(EnclosingDecl);
  CXXRecordDecl *CXXRecord = dyn_cast<CXXRecordDecl>(EnclosingDecl);

  // Start counting up the number of named members; make sure to include
  // members of anonymous structs and unions in the total.
  unsigned NumNamedMembers = 0;
  if (Record) {
    for (const auto *I : Record->decls()) {
      if (const auto *IFD = dyn_cast<IndirectFieldDecl>(I))
        if (IFD->getDeclName())
          ++NumNamedMembers;
    }
  }

  // Verify that all the fields are okay.
  SmallVector<FieldDecl*, 32> RecFields;

  for (ArrayRef<Decl *>::iterator i = Fields.begin(), end = Fields.end();
       i != end; ++i) {
    FieldDecl *FD = cast<FieldDecl>(*i);

    // Get the type for the field.
    const Type *FDTy = FD->getType().getTypePtr();

    if (!FD->isAnonymousStructOrUnion()) {
      // Remember all fields written by the user.
      RecFields.push_back(FD);
    }

    // If the field is already invalid for some reason, don't emit more
    // diagnostics about it.
    if (FD->isInvalidDecl()) {
      EnclosingDecl->setInvalidDecl();
      continue;
    }

    // C99 6.7.2.1p2:
    //   A structure or union shall not contain a member with
    //   incomplete or function type (hence, a structure shall not
    //   contain an instance of itself, but may contain a pointer to
    //   an instance of itself), except that the last member of a
    //   structure with more than one named member may have incomplete
    //   array type; such a structure (and any union containing,
    //   possibly recursively, a member that is such a structure)
    //   shall not be a member of a structure or an element of an
    //   array.
    bool IsLastField = (i + 1 == Fields.end());
    if (FDTy->isFunctionType()) {
      // Field declared as a function.
      Diag(FD->getLocation(), diag::err_field_declared_as_function)
        << FD->getDeclName();
      FD->setInvalidDecl();
      EnclosingDecl->setInvalidDecl();
      continue;
    } else if (FDTy->isIncompleteArrayType() &&
               (Record || isa<ObjCContainerDecl>(EnclosingDecl))) {
      if (Record) {
        // Flexible array member.
        // Microsoft and g++ is more permissive regarding flexible array.
        // It will accept flexible array in union and also
        // as the sole element of a struct/class.
        unsigned DiagID = 0;
        if (!Record->isUnion() && !IsLastField) {
          Diag(FD->getLocation(), diag::err_flexible_array_not_at_end)
              << FD->getDeclName() << FD->getType()
              << llvm::to_underlying(Record->getTagKind());
          Diag((*(i + 1))->getLocation(), diag::note_next_field_declaration);
          FD->setInvalidDecl();
          EnclosingDecl->setInvalidDecl();
          continue;
        } else if (Record->isUnion())
          DiagID = getLangOpts().MicrosoftExt
                       ? diag::ext_flexible_array_union_ms
                       : diag::ext_flexible_array_union_gnu;
        else if (NumNamedMembers < 1)
          DiagID = getLangOpts().MicrosoftExt
                       ? diag::ext_flexible_array_empty_aggregate_ms
                       : diag::ext_flexible_array_empty_aggregate_gnu;

        if (DiagID)
          Diag(FD->getLocation(), DiagID)
              << FD->getDeclName() << llvm::to_underlying(Record->getTagKind());
        // While the layout of types that contain virtual bases is not specified
        // by the C++ standard, both the Itanium and Microsoft C++ ABIs place
        // virtual bases after the derived members.  This would make a flexible
        // array member declared at the end of an object not adjacent to the end
        // of the type.
        if (CXXRecord && CXXRecord->getNumVBases() != 0)
          Diag(FD->getLocation(), diag::err_flexible_array_virtual_base)
              << FD->getDeclName() << llvm::to_underlying(Record->getTagKind());
        if (!getLangOpts().C99)
          Diag(FD->getLocation(), diag::ext_c99_flexible_array_member)
              << FD->getDeclName() << llvm::to_underlying(Record->getTagKind());

        // If the element type has a non-trivial destructor, we would not
        // implicitly destroy the elements, so disallow it for now.
        //
        // FIXME: GCC allows this. We should probably either implicitly delete
        // the destructor of the containing class, or just allow this.
        QualType BaseElem = Context.getBaseElementType(FD->getType());
        if (!BaseElem->isDependentType() && BaseElem.isDestructedType()) {
          Diag(FD->getLocation(), diag::err_flexible_array_has_nontrivial_dtor)
            << FD->getDeclName() << FD->getType();
          FD->setInvalidDecl();
          EnclosingDecl->setInvalidDecl();
          continue;
        }
        // Okay, we have a legal flexible array member at the end of the struct.
        Record->setHasFlexibleArrayMember(true);
      } else {
        // In ObjCContainerDecl ivars with incomplete array type are accepted,
        // unless they are followed by another ivar. That check is done
        // elsewhere, after synthesized ivars are known.
      }
    } else if (!FDTy->isDependentType() &&
               RequireCompleteSizedType(
                   FD->getLocation(), FD->getType(),
                   diag::err_field_incomplete_or_sizeless)) {
      // Incomplete type
      FD->setInvalidDecl();
      EnclosingDecl->setInvalidDecl();
      continue;
    } else if (const RecordType *FDTTy = FDTy->getAs<RecordType>()) {
      if (Record && FDTTy->getDecl()->hasFlexibleArrayMember()) {
        // A type which contains a flexible array member is considered to be a
        // flexible array member.
        Record->setHasFlexibleArrayMember(true);
        if (!Record->isUnion()) {
          // If this is a struct/class and this is not the last element, reject
          // it.  Note that GCC supports variable sized arrays in the middle of
          // structures.
          if (!IsLastField)
            Diag(FD->getLocation(), diag::ext_variable_sized_type_in_struct)
              << FD->getDeclName() << FD->getType();
          else {
            // We support flexible arrays at the end of structs in
            // other structs as an extension.
            Diag(FD->getLocation(), diag::ext_flexible_array_in_struct)
              << FD->getDeclName();
          }
        }
      }
      if (isa<ObjCContainerDecl>(EnclosingDecl) &&
          RequireNonAbstractType(FD->getLocation(), FD->getType(),
                                 diag::err_abstract_type_in_decl,
                                 AbstractIvarType)) {
        // Ivars can not have abstract class types
        FD->setInvalidDecl();
      }
      if (Record && FDTTy->getDecl()->hasObjectMember())
        Record->setHasObjectMember(true);
      if (Record && FDTTy->getDecl()->hasVolatileMember())
        Record->setHasVolatileMember(true);
    } else if (FDTy->isObjCObjectType()) {
      /// A field cannot be an Objective-c object
      Diag(FD->getLocation(), diag::err_statically_allocated_object)
        << FixItHint::CreateInsertion(FD->getLocation(), "*");
      QualType T = Context.getObjCObjectPointerType(FD->getType());
      FD->setType(T);
    } else if (Record && Record->isUnion() &&
               FD->getType().hasNonTrivialObjCLifetime() &&
               getSourceManager().isInSystemHeader(FD->getLocation()) &&
               !getLangOpts().CPlusPlus && !FD->hasAttr<UnavailableAttr>() &&
               (FD->getType().getObjCLifetime() != Qualifiers::OCL_Strong ||
                !Context.hasDirectOwnershipQualifier(FD->getType()))) {
      // For backward compatibility, fields of C unions declared in system
      // headers that have non-trivial ObjC ownership qualifications are marked
      // as unavailable unless the qualifier is explicit and __strong. This can
      // break ABI compatibility between programs compiled with ARC and MRR, but
      // is a better option than rejecting programs using those unions under
      // ARC.
      FD->addAttr(UnavailableAttr::CreateImplicit(
          Context, "", UnavailableAttr::IR_ARCFieldWithOwnership,
          FD->getLocation()));
    } else if (getLangOpts().ObjC &&
               getLangOpts().getGC() != LangOptions::NonGC && Record &&
               !Record->hasObjectMember()) {
      if (FD->getType()->isObjCObjectPointerType() ||
          FD->getType().isObjCGCStrong())
        Record->setHasObjectMember(true);
      else if (Context.getAsArrayType(FD->getType())) {
        QualType BaseType = Context.getBaseElementType(FD->getType());
        if (BaseType->isRecordType() &&
            BaseType->castAs<RecordType>()->getDecl()->hasObjectMember())
          Record->setHasObjectMember(true);
        else if (BaseType->isObjCObjectPointerType() ||
                 BaseType.isObjCGCStrong())
               Record->setHasObjectMember(true);
      }
    }

    if (Record && !getLangOpts().CPlusPlus &&
        !shouldIgnoreForRecordTriviality(FD)) {
      QualType FT = FD->getType();
      if (FT.isNonTrivialToPrimitiveDefaultInitialize()) {
        Record->setNonTrivialToPrimitiveDefaultInitialize(true);
        if (FT.hasNonTrivialToPrimitiveDefaultInitializeCUnion() ||
            Record->isUnion())
          Record->setHasNonTrivialToPrimitiveDefaultInitializeCUnion(true);
      }
      QualType::PrimitiveCopyKind PCK = FT.isNonTrivialToPrimitiveCopy();
      if (PCK != QualType::PCK_Trivial && PCK != QualType::PCK_VolatileTrivial) {
        Record->setNonTrivialToPrimitiveCopy(true);
        if (FT.hasNonTrivialToPrimitiveCopyCUnion() || Record->isUnion())
          Record->setHasNonTrivialToPrimitiveCopyCUnion(true);
      }
      if (FT.isDestructedType()) {
        Record->setNonTrivialToPrimitiveDestroy(true);
        Record->setParamDestroyedInCallee(true);
        if (FT.hasNonTrivialToPrimitiveDestructCUnion() || Record->isUnion())
          Record->setHasNonTrivialToPrimitiveDestructCUnion(true);
      }

      if (const auto *RT = FT->getAs<RecordType>()) {
        if (RT->getDecl()->getArgPassingRestrictions() ==
            RecordArgPassingKind::CanNeverPassInRegs)
          Record->setArgPassingRestrictions(
              RecordArgPassingKind::CanNeverPassInRegs);
      } else if (FT.getQualifiers().getObjCLifetime() == Qualifiers::OCL_Weak)
        Record->setArgPassingRestrictions(
            RecordArgPassingKind::CanNeverPassInRegs);
    }

    if (Record && FD->getType().isVolatileQualified())
      Record->setHasVolatileMember(true);
    // Keep track of the number of named members.
    if (FD->getIdentifier())
      ++NumNamedMembers;
  }

  // Okay, we successfully defined 'Record'.
  if (Record) {
    bool Completed = false;
    if (S) {
      Scope *Parent = S->getParent();
      if (Parent && Parent->isTypeAliasScope() &&
          Parent->isTemplateParamScope())
        Record->setInvalidDecl();
    }

    if (CXXRecord) {
      if (!CXXRecord->isInvalidDecl()) {
        // Set access bits correctly on the directly-declared conversions.
        for (CXXRecordDecl::conversion_iterator
               I = CXXRecord->conversion_begin(),
               E = CXXRecord->conversion_end(); I != E; ++I)
          I.setAccess((*I)->getAccess());
      }

      // Add any implicitly-declared members to this class.
      AddImplicitlyDeclaredMembersToClass(CXXRecord);

      if (!CXXRecord->isDependentType()) {
        if (!CXXRecord->isInvalidDecl()) {
          // If we have virtual base classes, we may end up finding multiple
          // final overriders for a given virtual function. Check for this
          // problem now.
          if (CXXRecord->getNumVBases()) {
            CXXFinalOverriderMap FinalOverriders;
            CXXRecord->getFinalOverriders(FinalOverriders);

            for (CXXFinalOverriderMap::iterator M = FinalOverriders.begin(),
                                             MEnd = FinalOverriders.end();
                 M != MEnd; ++M) {
              for (OverridingMethods::iterator SO = M->second.begin(),
                                            SOEnd = M->second.end();
                   SO != SOEnd; ++SO) {
                assert(SO->second.size() > 0 &&
                       "Virtual function without overriding functions?");
                if (SO->second.size() == 1)
                  continue;

                // C++ [class.virtual]p2:
                //   In a derived class, if a virtual member function of a base
                //   class subobject has more than one final overrider the
                //   program is ill-formed.
                Diag(Record->getLocation(), diag::err_multiple_final_overriders)
                  << (const NamedDecl *)M->first << Record;
                Diag(M->first->getLocation(),
                     diag::note_overridden_virtual_function);
                for (OverridingMethods::overriding_iterator
                          OM = SO->second.begin(),
                       OMEnd = SO->second.end();
                     OM != OMEnd; ++OM)
                  Diag(OM->Method->getLocation(), diag::note_final_overrider)
                    << (const NamedDecl *)M->first << OM->Method->getParent();

                Record->setInvalidDecl();
              }
            }
            CXXRecord->completeDefinition(&FinalOverriders);
            Completed = true;
          }
        }
        ComputeSelectedDestructor(*this, CXXRecord);
        ComputeSpecialMemberFunctionsEligiblity(*this, CXXRecord);
      }
    }

    if (!Completed)
      Record->completeDefinition();

    // Handle attributes before checking the layout.
    ProcessDeclAttributeList(S, Record, Attrs);

    // Check to see if a FieldDecl is a pointer to a function.
    auto IsFunctionPointerOrForwardDecl = [&](const Decl *D) {
      const FieldDecl *FD = dyn_cast<FieldDecl>(D);
      if (!FD) {
        // Check whether this is a forward declaration that was inserted by
        // Clang. This happens when a non-forward declared / defined type is
        // used, e.g.:
        //
        //   struct foo {
        //     struct bar *(*f)();
        //     struct bar *(*g)();
        //   };
        //
        // "struct bar" shows up in the decl AST as a "RecordDecl" with an
        // incomplete definition.
        if (const auto *TD = dyn_cast<TagDecl>(D))
          return !TD->isCompleteDefinition();
        return false;
      }
      QualType FieldType = FD->getType().getDesugaredType(Context);
      if (isa<PointerType>(FieldType)) {
        QualType PointeeType = cast<PointerType>(FieldType)->getPointeeType();
        return PointeeType.getDesugaredType(Context)->isFunctionType();
      }
      return false;
    };

    // Maybe randomize the record's decls. We automatically randomize a record
    // of function pointers, unless it has the "no_randomize_layout" attribute.
    if (!getLangOpts().CPlusPlus &&
        (Record->hasAttr<RandomizeLayoutAttr>() ||
         (!Record->hasAttr<NoRandomizeLayoutAttr>() &&
          llvm::all_of(Record->decls(), IsFunctionPointerOrForwardDecl))) &&
        !Record->isUnion() && !getLangOpts().RandstructSeed.empty() &&
        !Record->isRandomized()) {
      SmallVector<Decl *, 32> NewDeclOrdering;
      if (randstruct::randomizeStructureLayout(Context, Record,
                                               NewDeclOrdering))
        Record->reorderDecls(NewDeclOrdering);
    }

    // We may have deferred checking for a deleted destructor. Check now.
    if (CXXRecord) {
      auto *Dtor = CXXRecord->getDestructor();
      if (Dtor && Dtor->isImplicit() &&
          ShouldDeleteSpecialMember(Dtor, CXXSpecialMemberKind::Destructor)) {
        CXXRecord->setImplicitDestructorIsDeleted();
        SetDeclDeleted(Dtor, CXXRecord->getLocation());
      }
    }

    if (Record->hasAttrs()) {
      CheckAlignasUnderalignment(Record);

      if (const MSInheritanceAttr *IA = Record->getAttr<MSInheritanceAttr>())
        checkMSInheritanceAttrOnDefinition(cast<CXXRecordDecl>(Record),
                                           IA->getRange(), IA->getBestCase(),
                                           IA->getInheritanceModel());
    }

    // Check if the structure/union declaration is a type that can have zero
    // size in C. For C this is a language extension, for C++ it may cause
    // compatibility problems.
    bool CheckForZeroSize;
    if (!getLangOpts().CPlusPlus) {
      CheckForZeroSize = true;
    } else {
      // For C++ filter out types that cannot be referenced in C code.
      CXXRecordDecl *CXXRecord = cast<CXXRecordDecl>(Record);
      CheckForZeroSize =
          CXXRecord->getLexicalDeclContext()->isExternCContext() &&
          !CXXRecord->isDependentType() && !inTemplateInstantiation() &&
          CXXRecord->isCLike();
    }
    if (CheckForZeroSize) {
      bool ZeroSize = true;
      bool IsEmpty = true;
      unsigned NonBitFields = 0;
      for (RecordDecl::field_iterator I = Record->field_begin(),
                                      E = Record->field_end();
           (NonBitFields == 0 || ZeroSize) && I != E; ++I) {
        IsEmpty = false;
        if (I->isUnnamedBitField()) {
          if (!I->isZeroLengthBitField(Context))
            ZeroSize = false;
        } else {
          ++NonBitFields;
          QualType FieldType = I->getType();
          if (FieldType->isIncompleteType() ||
              !Context.getTypeSizeInChars(FieldType).isZero())
            ZeroSize = false;
        }
      }

      // Empty structs are an extension in C (C99 6.7.2.1p7). They are
      // allowed in C++, but warn if its declaration is inside
      // extern "C" block.
      if (ZeroSize) {
        Diag(RecLoc, getLangOpts().CPlusPlus ?
                         diag::warn_zero_size_struct_union_in_extern_c :
                         diag::warn_zero_size_struct_union_compat)
          << IsEmpty << Record->isUnion() << (NonBitFields > 1);
      }

      // Structs without named members are extension in C (C99 6.7.2.1p7),
      // but are accepted by GCC.
      if (NonBitFields == 0 && !getLangOpts().CPlusPlus) {
        Diag(RecLoc, IsEmpty ? diag::ext_empty_struct_union :
                               diag::ext_no_named_members_in_struct_union)
          << Record->isUnion();
      }
    }
  } else {
    ObjCIvarDecl **ClsFields =
      reinterpret_cast<ObjCIvarDecl**>(RecFields.data());
    if (ObjCInterfaceDecl *ID = dyn_cast<ObjCInterfaceDecl>(EnclosingDecl)) {
      ID->setEndOfDefinitionLoc(RBrac);
      // Add ivar's to class's DeclContext.
      for (unsigned i = 0, e = RecFields.size(); i != e; ++i) {
        ClsFields[i]->setLexicalDeclContext(ID);
        ID->addDecl(ClsFields[i]);
      }
      // Must enforce the rule that ivars in the base classes may not be
      // duplicates.
      if (ID->getSuperClass())
        ObjC().DiagnoseDuplicateIvars(ID, ID->getSuperClass());
    } else if (ObjCImplementationDecl *IMPDecl =
                  dyn_cast<ObjCImplementationDecl>(EnclosingDecl)) {
      assert(IMPDecl && "ActOnFields - missing ObjCImplementationDecl");
      for (unsigned I = 0, N = RecFields.size(); I != N; ++I)
        // Ivar declared in @implementation never belongs to the implementation.
        // Only it is in implementation's lexical context.
        ClsFields[I]->setLexicalDeclContext(IMPDecl);
      ObjC().CheckImplementationIvars(IMPDecl, ClsFields, RecFields.size(),
                                      RBrac);
      IMPDecl->setIvarLBraceLoc(LBrac);
      IMPDecl->setIvarRBraceLoc(RBrac);
    } else if (ObjCCategoryDecl *CDecl =
                dyn_cast<ObjCCategoryDecl>(EnclosingDecl)) {
      // case of ivars in class extension; all other cases have been
      // reported as errors elsewhere.
      // FIXME. Class extension does not have a LocEnd field.
      // CDecl->setLocEnd(RBrac);
      // Add ivar's to class extension's DeclContext.
      // Diagnose redeclaration of private ivars.
      ObjCInterfaceDecl *IDecl = CDecl->getClassInterface();
      for (unsigned i = 0, e = RecFields.size(); i != e; ++i) {
        if (IDecl) {
          if (const ObjCIvarDecl *ClsIvar =
              IDecl->getIvarDecl(ClsFields[i]->getIdentifier())) {
            Diag(ClsFields[i]->getLocation(),
                 diag::err_duplicate_ivar_declaration);
            Diag(ClsIvar->getLocation(), diag::note_previous_definition);
            continue;
          }
          for (const auto *Ext : IDecl->known_extensions()) {
            if (const ObjCIvarDecl *ClsExtIvar
                  = Ext->getIvarDecl(ClsFields[i]->getIdentifier())) {
              Diag(ClsFields[i]->getLocation(),
                   diag::err_duplicate_ivar_declaration);
              Diag(ClsExtIvar->getLocation(), diag::note_previous_definition);
              continue;
            }
          }
        }
        ClsFields[i]->setLexicalDeclContext(CDecl);
        CDecl->addDecl(ClsFields[i]);
      }
      CDecl->setIvarLBraceLoc(LBrac);
      CDecl->setIvarRBraceLoc(RBrac);
    }
  }
  ProcessAPINotes(Record);
}

/// Determine whether the given integral value is representable within
/// the given type T.
static bool isRepresentableIntegerValue(ASTContext &Context,
                                        llvm::APSInt &Value,
                                        QualType T) {
  assert((T->isIntegralType(Context) || T->isEnumeralType()) &&
         "Integral type required!");
  unsigned BitWidth = Context.getIntWidth(T);

  if (Value.isUnsigned() || Value.isNonNegative()) {
    if (T->isSignedIntegerOrEnumerationType())
      --BitWidth;
    return Value.getActiveBits() <= BitWidth;
  }
  return Value.getSignificantBits() <= BitWidth;
}

// Given an integral type, return the next larger integral type
// (or a NULL type of no such type exists).
static QualType getNextLargerIntegralType(ASTContext &Context, QualType T) {
  // FIXME: Int128/UInt128 support, which also needs to be introduced into
  // enum checking below.
  assert((T->isIntegralType(Context) ||
         T->isEnumeralType()) && "Integral type required!");
  const unsigned NumTypes = 4;
  QualType SignedIntegralTypes[NumTypes] = {
    Context.ShortTy, Context.IntTy, Context.LongTy, Context.LongLongTy
  };
  QualType UnsignedIntegralTypes[NumTypes] = {
    Context.UnsignedShortTy, Context.UnsignedIntTy, Context.UnsignedLongTy,
    Context.UnsignedLongLongTy
  };

  unsigned BitWidth = Context.getTypeSize(T);
  QualType *Types = T->isSignedIntegerOrEnumerationType()? SignedIntegralTypes
                                                        : UnsignedIntegralTypes;
  for (unsigned I = 0; I != NumTypes; ++I)
    if (Context.getTypeSize(Types[I]) > BitWidth)
      return Types[I];

  return QualType();
}

EnumConstantDecl *Sema::CheckEnumConstant(EnumDecl *Enum,
                                          EnumConstantDecl *LastEnumConst,
                                          SourceLocation IdLoc,
                                          IdentifierInfo *Id,
                                          Expr *Val) {
  unsigned IntWidth = Context.getTargetInfo().getIntWidth();
  llvm::APSInt EnumVal(IntWidth);
  QualType EltTy;

  if (Val && DiagnoseUnexpandedParameterPack(Val, UPPC_EnumeratorValue))
    Val = nullptr;

  if (Val)
    Val = DefaultLvalueConversion(Val).get();

  if (Val) {
    if (Enum->isDependentType() || Val->isTypeDependent() ||
        Val->containsErrors())
      EltTy = Context.DependentTy;
    else {
      // FIXME: We don't allow folding in C++11 mode for an enum with a fixed
      // underlying type, but do allow it in all other contexts.
      if (getLangOpts().CPlusPlus11 && Enum->isFixed()) {
        // C++11 [dcl.enum]p5: If the underlying type is fixed, [...] the
        // constant-expression in the enumerator-definition shall be a converted
        // constant expression of the underlying type.
        EltTy = Enum->getIntegerType();
        ExprResult Converted =
          CheckConvertedConstantExpression(Val, EltTy, EnumVal,
                                           CCEK_Enumerator);
        if (Converted.isInvalid())
          Val = nullptr;
        else
          Val = Converted.get();
      } else if (!Val->isValueDependent() &&
                 !(Val =
                       VerifyIntegerConstantExpression(Val, &EnumVal, AllowFold)
                           .get())) {
        // C99 6.7.2.2p2: Make sure we have an integer constant expression.
      } else {
        if (Enum->isComplete()) {
          EltTy = Enum->getIntegerType();

          // In Obj-C and Microsoft mode, require the enumeration value to be
          // representable in the underlying type of the enumeration. In C++11,
          // we perform a non-narrowing conversion as part of converted constant
          // expression checking.
          if (!isRepresentableIntegerValue(Context, EnumVal, EltTy)) {
            if (Context.getTargetInfo()
                    .getTriple()
                    .isWindowsMSVCEnvironment()) {
              Diag(IdLoc, diag::ext_enumerator_too_large) << EltTy;
            } else {
              Diag(IdLoc, diag::err_enumerator_too_large) << EltTy;
            }
          }

          // Cast to the underlying type.
          Val = ImpCastExprToType(Val, EltTy,
                                  EltTy->isBooleanType() ? CK_IntegralToBoolean
                                                         : CK_IntegralCast)
                    .get();
        } else if (getLangOpts().CPlusPlus) {
          // C++11 [dcl.enum]p5:
          //   If the underlying type is not fixed, the type of each enumerator
          //   is the type of its initializing value:
          //     - If an initializer is specified for an enumerator, the
          //       initializing value has the same type as the expression.
          EltTy = Val->getType();
        } else {
          // C99 6.7.2.2p2:
          //   The expression that defines the value of an enumeration constant
          //   shall be an integer constant expression that has a value
          //   representable as an int.

          // Complain if the value is not representable in an int.
          if (!isRepresentableIntegerValue(Context, EnumVal, Context.IntTy))
            Diag(IdLoc, diag::ext_enum_value_not_int)
              << toString(EnumVal, 10) << Val->getSourceRange()
              << (EnumVal.isUnsigned() || EnumVal.isNonNegative());
          else if (!Context.hasSameType(Val->getType(), Context.IntTy)) {
            // Force the type of the expression to 'int'.
            Val = ImpCastExprToType(Val, Context.IntTy, CK_IntegralCast).get();
          }
          EltTy = Val->getType();
        }
      }
    }
  }

  if (!Val) {
    if (Enum->isDependentType())
      EltTy = Context.DependentTy;
    else if (!LastEnumConst) {
      // C++0x [dcl.enum]p5:
      //   If the underlying type is not fixed, the type of each enumerator
      //   is the type of its initializing value:
      //     - If no initializer is specified for the first enumerator, the
      //       initializing value has an unspecified integral type.
      //
      // GCC uses 'int' for its unspecified integral type, as does
      // C99 6.7.2.2p3.
      if (Enum->isFixed()) {
        EltTy = Enum->getIntegerType();
      }
      else {
        EltTy = Context.IntTy;
      }
    } else {
      // Assign the last value + 1.
      EnumVal = LastEnumConst->getInitVal();
      ++EnumVal;
      EltTy = LastEnumConst->getType();

      // Check for overflow on increment.
      if (EnumVal < LastEnumConst->getInitVal()) {
        // C++0x [dcl.enum]p5:
        //   If the underlying type is not fixed, the type of each enumerator
        //   is the type of its initializing value:
        //
        //     - Otherwise the type of the initializing value is the same as
        //       the type of the initializing value of the preceding enumerator
        //       unless the incremented value is not representable in that type,
        //       in which case the type is an unspecified integral type
        //       sufficient to contain the incremented value. If no such type
        //       exists, the program is ill-formed.
        QualType T = getNextLargerIntegralType(Context, EltTy);
        if (T.isNull() || Enum->isFixed()) {
          // There is no integral type larger enough to represent this
          // value. Complain, then allow the value to wrap around.
          EnumVal = LastEnumConst->getInitVal();
          EnumVal = EnumVal.zext(EnumVal.getBitWidth() * 2);
          ++EnumVal;
          if (Enum->isFixed())
            // When the underlying type is fixed, this is ill-formed.
            Diag(IdLoc, diag::err_enumerator_wrapped)
              << toString(EnumVal, 10)
              << EltTy;
          else
            Diag(IdLoc, diag::ext_enumerator_increment_too_large)
              << toString(EnumVal, 10);
        } else {
          EltTy = T;
        }

        // Retrieve the last enumerator's value, extent that type to the
        // type that is supposed to be large enough to represent the incremented
        // value, then increment.
        EnumVal = LastEnumConst->getInitVal();
        EnumVal.setIsSigned(EltTy->isSignedIntegerOrEnumerationType());
        EnumVal = EnumVal.zextOrTrunc(Context.getIntWidth(EltTy));
        ++EnumVal;

        // If we're not in C++, diagnose the overflow of enumerator values,
        // which in C99 means that the enumerator value is not representable in
        // an int (C99 6.7.2.2p2). However, we support GCC's extension that
        // permits enumerator values that are representable in some larger
        // integral type.
        if (!getLangOpts().CPlusPlus && !T.isNull())
          Diag(IdLoc, diag::warn_enum_value_overflow);
      } else if (!getLangOpts().CPlusPlus &&
                 !EltTy->isDependentType() &&
                 !isRepresentableIntegerValue(Context, EnumVal, EltTy)) {
        // Enforce C99 6.7.2.2p2 even when we compute the next value.
        Diag(IdLoc, diag::ext_enum_value_not_int)
          << toString(EnumVal, 10) << 1;
      }
    }
  }

  if (!EltTy->isDependentType()) {
    // Make the enumerator value match the signedness and size of the
    // enumerator's type.
    EnumVal = EnumVal.extOrTrunc(Context.getIntWidth(EltTy));
    EnumVal.setIsSigned(EltTy->isSignedIntegerOrEnumerationType());
  }

  return EnumConstantDecl::Create(Context, Enum, IdLoc, Id, EltTy,
                                  Val, EnumVal);
}

SkipBodyInfo Sema::shouldSkipAnonEnumBody(Scope *S, IdentifierInfo *II,
                                                SourceLocation IILoc) {
  if (!(getLangOpts().Modules || getLangOpts().ModulesLocalVisibility) ||
      !getLangOpts().CPlusPlus)
    return SkipBodyInfo();

  // We have an anonymous enum definition. Look up the first enumerator to
  // determine if we should merge the definition with an existing one and
  // skip the body.
  NamedDecl *PrevDecl = LookupSingleName(S, II, IILoc, LookupOrdinaryName,
                                         forRedeclarationInCurContext());
  auto *PrevECD = dyn_cast_or_null<EnumConstantDecl>(PrevDecl);
  if (!PrevECD)
    return SkipBodyInfo();

  EnumDecl *PrevED = cast<EnumDecl>(PrevECD->getDeclContext());
  NamedDecl *Hidden;
  if (!PrevED->getDeclName() && !hasVisibleDefinition(PrevED, &Hidden)) {
    SkipBodyInfo Skip;
    Skip.Previous = Hidden;
    return Skip;
  }

  return SkipBodyInfo();
}

Decl *Sema::ActOnEnumConstant(Scope *S, Decl *theEnumDecl, Decl *lastEnumConst,
                              SourceLocation IdLoc, IdentifierInfo *Id,
                              const ParsedAttributesView &Attrs,
                              SourceLocation EqualLoc, Expr *Val) {
  EnumDecl *TheEnumDecl = cast<EnumDecl>(theEnumDecl);
  EnumConstantDecl *LastEnumConst =
    cast_or_null<EnumConstantDecl>(lastEnumConst);

  // The scope passed in may not be a decl scope.  Zip up the scope tree until
  // we find one that is.
  S = getNonFieldDeclScope(S);

  // Verify that there isn't already something declared with this name in this
  // scope.
  LookupResult R(*this, Id, IdLoc, LookupOrdinaryName,
                 RedeclarationKind::ForVisibleRedeclaration);
  LookupName(R, S);
  NamedDecl *PrevDecl = R.getAsSingle<NamedDecl>();

  if (PrevDecl && PrevDecl->isTemplateParameter()) {
    // Maybe we will complain about the shadowed template parameter.
    DiagnoseTemplateParameterShadow(IdLoc, PrevDecl);
    // Just pretend that we didn't see the previous declaration.
    PrevDecl = nullptr;
  }

  // C++ [class.mem]p15:
  // If T is the name of a class, then each of the following shall have a name
  // different from T:
  // - every enumerator of every member of class T that is an unscoped
  // enumerated type
  if (getLangOpts().CPlusPlus && !TheEnumDecl->isScoped())
    DiagnoseClassNameShadow(TheEnumDecl->getDeclContext(),
                            DeclarationNameInfo(Id, IdLoc));

  EnumConstantDecl *New =
    CheckEnumConstant(TheEnumDecl, LastEnumConst, IdLoc, Id, Val);
  if (!New)
    return nullptr;

  if (PrevDecl) {
    if (!TheEnumDecl->isScoped() && isa<ValueDecl>(PrevDecl)) {
      // Check for other kinds of shadowing not already handled.
      CheckShadow(New, PrevDecl, R);
    }

    // When in C++, we may get a TagDecl with the same name; in this case the
    // enum constant will 'hide' the tag.
    assert((getLangOpts().CPlusPlus || !isa<TagDecl>(PrevDecl)) &&
           "Received TagDecl when not in C++!");
    if (!isa<TagDecl>(PrevDecl) && isDeclInScope(PrevDecl, CurContext, S)) {
      if (isa<EnumConstantDecl>(PrevDecl))
        Diag(IdLoc, diag::err_redefinition_of_enumerator) << Id;
      else
        Diag(IdLoc, diag::err_redefinition) << Id;
      notePreviousDefinition(PrevDecl, IdLoc);
      return nullptr;
    }
  }

  // Process attributes.
  ProcessDeclAttributeList(S, New, Attrs);
  AddPragmaAttributes(S, New);
  ProcessAPINotes(New);

  // Register this decl in the current scope stack.
  New->setAccess(TheEnumDecl->getAccess());
  PushOnScopeChains(New, S);

  ActOnDocumentableDecl(New);

  return New;
}

// Returns true when the enum initial expression does not trigger the
// duplicate enum warning.  A few common cases are exempted as follows:
// Element2 = Element1
// Element2 = Element1 + 1
// Element2 = Element1 - 1
// Where Element2 and Element1 are from the same enum.
static bool ValidDuplicateEnum(EnumConstantDecl *ECD, EnumDecl *Enum) {
  Expr *InitExpr = ECD->getInitExpr();
  if (!InitExpr)
    return true;
  InitExpr = InitExpr->IgnoreImpCasts();

  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(InitExpr)) {
    if (!BO->isAdditiveOp())
      return true;
    IntegerLiteral *IL = dyn_cast<IntegerLiteral>(BO->getRHS());
    if (!IL)
      return true;
    if (IL->getValue() != 1)
      return true;

    InitExpr = BO->getLHS();
  }

  // This checks if the elements are from the same enum.
  DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(InitExpr);
  if (!DRE)
    return true;

  EnumConstantDecl *EnumConstant = dyn_cast<EnumConstantDecl>(DRE->getDecl());
  if (!EnumConstant)
    return true;

  if (cast<EnumDecl>(TagDecl::castFromDeclContext(ECD->getDeclContext())) !=
      Enum)
    return true;

  return false;
}

// Emits a warning when an element is implicitly set a value that
// a previous element has already been set to.
static void CheckForDuplicateEnumValues(Sema &S, ArrayRef<Decl *> Elements,
                                        EnumDecl *Enum, QualType EnumType) {
  // Avoid anonymous enums
  if (!Enum->getIdentifier())
    return;

  // Only check for small enums.
  if (Enum->getNumPositiveBits() > 63 || Enum->getNumNegativeBits() > 64)
    return;

  if (S.Diags.isIgnored(diag::warn_duplicate_enum_values, Enum->getLocation()))
    return;

  typedef SmallVector<EnumConstantDecl *, 3> ECDVector;
  typedef SmallVector<std::unique_ptr<ECDVector>, 3> DuplicatesVector;

  typedef llvm::PointerUnion<EnumConstantDecl*, ECDVector*> DeclOrVector;

  // DenseMaps cannot contain the all ones int64_t value, so use unordered_map.
  typedef std::unordered_map<int64_t, DeclOrVector> ValueToVectorMap;

  // Use int64_t as a key to avoid needing special handling for map keys.
  auto EnumConstantToKey = [](const EnumConstantDecl *D) {
    llvm::APSInt Val = D->getInitVal();
    return Val.isSigned() ? Val.getSExtValue() : Val.getZExtValue();
  };

  DuplicatesVector DupVector;
  ValueToVectorMap EnumMap;

  // Populate the EnumMap with all values represented by enum constants without
  // an initializer.
  for (auto *Element : Elements) {
    EnumConstantDecl *ECD = cast_or_null<EnumConstantDecl>(Element);

    // Null EnumConstantDecl means a previous diagnostic has been emitted for
    // this constant.  Skip this enum since it may be ill-formed.
    if (!ECD) {
      return;
    }

    // Constants with initializers are handled in the next loop.
    if (ECD->getInitExpr())
      continue;

    // Duplicate values are handled in the next loop.
    EnumMap.insert({EnumConstantToKey(ECD), ECD});
  }

  if (EnumMap.size() == 0)
    return;

  // Create vectors for any values that has duplicates.
  for (auto *Element : Elements) {
    // The last loop returned if any constant was null.
    EnumConstantDecl *ECD = cast<EnumConstantDecl>(Element);
    if (!ValidDuplicateEnum(ECD, Enum))
      continue;

    auto Iter = EnumMap.find(EnumConstantToKey(ECD));
    if (Iter == EnumMap.end())
      continue;

    DeclOrVector& Entry = Iter->second;
    if (EnumConstantDecl *D = Entry.dyn_cast<EnumConstantDecl*>()) {
      // Ensure constants are different.
      if (D == ECD)
        continue;

      // Create new vector and push values onto it.
      auto Vec = std::make_unique<ECDVector>();
      Vec->push_back(D);
      Vec->push_back(ECD);

      // Update entry to point to the duplicates vector.
      Entry = Vec.get();

      // Store the vector somewhere we can consult later for quick emission of
      // diagnostics.
      DupVector.emplace_back(std::move(Vec));
      continue;
    }

    ECDVector *Vec = Entry.get<ECDVector*>();
    // Make sure constants are not added more than once.
    if (*Vec->begin() == ECD)
      continue;

    Vec->push_back(ECD);
  }

  // Emit diagnostics.
  for (const auto &Vec : DupVector) {
    assert(Vec->size() > 1 && "ECDVector should have at least 2 elements.");

    // Emit warning for one enum constant.
    auto *FirstECD = Vec->front();
    S.Diag(FirstECD->getLocation(), diag::warn_duplicate_enum_values)
      << FirstECD << toString(FirstECD->getInitVal(), 10)
      << FirstECD->getSourceRange();

    // Emit one note for each of the remaining enum constants with
    // the same value.
    for (auto *ECD : llvm::drop_begin(*Vec))
      S.Diag(ECD->getLocation(), diag::note_duplicate_element)
        << ECD << toString(ECD->getInitVal(), 10)
        << ECD->getSourceRange();
  }
}

bool Sema::IsValueInFlagEnum(const EnumDecl *ED, const llvm::APInt &Val,
                             bool AllowMask) const {
  assert(ED->isClosedFlag() && "looking for value in non-flag or open enum");
  assert(ED->isCompleteDefinition() && "expected enum definition");

  auto R = FlagBitsCache.insert(std::make_pair(ED, llvm::APInt()));
  llvm::APInt &FlagBits = R.first->second;

  if (R.second) {
    for (auto *E : ED->enumerators()) {
      const auto &EVal = E->getInitVal();
      // Only single-bit enumerators introduce new flag values.
      if (EVal.isPowerOf2())
        FlagBits = FlagBits.zext(EVal.getBitWidth()) | EVal;
    }
  }

  // A value is in a flag enum if either its bits are a subset of the enum's
  // flag bits (the first condition) or we are allowing masks and the same is
  // true of its complement (the second condition). When masks are allowed, we
  // allow the common idiom of ~(enum1 | enum2) to be a valid enum value.
  //
  // While it's true that any value could be used as a mask, the assumption is
  // that a mask will have all of the insignificant bits set. Anything else is
  // likely a logic error.
  llvm::APInt FlagMask = ~FlagBits.zextOrTrunc(Val.getBitWidth());
  return !(FlagMask & Val) || (AllowMask && !(FlagMask & ~Val));
}

void Sema::ActOnEnumBody(SourceLocation EnumLoc, SourceRange BraceRange,
                         Decl *EnumDeclX, ArrayRef<Decl *> Elements, Scope *S,
                         const ParsedAttributesView &Attrs) {
  EnumDecl *Enum = cast<EnumDecl>(EnumDeclX);
  QualType EnumType = Context.getTypeDeclType(Enum);

  ProcessDeclAttributeList(S, Enum, Attrs);
  ProcessAPINotes(Enum);

  if (Enum->isDependentType()) {
    for (unsigned i = 0, e = Elements.size(); i != e; ++i) {
      EnumConstantDecl *ECD =
        cast_or_null<EnumConstantDecl>(Elements[i]);
      if (!ECD) continue;

      ECD->setType(EnumType);
    }

    Enum->completeDefinition(Context.DependentTy, Context.DependentTy, 0, 0);
    return;
  }

  // TODO: If the result value doesn't fit in an int, it must be a long or long
  // long value.  ISO C does not support this, but GCC does as an extension,
  // emit a warning.
  unsigned IntWidth = Context.getTargetInfo().getIntWidth();
  unsigned CharWidth = Context.getTargetInfo().getCharWidth();
  unsigned ShortWidth = Context.getTargetInfo().getShortWidth();

  // Verify that all the values are okay, compute the size of the values, and
  // reverse the list.
  unsigned NumNegativeBits = 0;
  unsigned NumPositiveBits = 0;

  for (unsigned i = 0, e = Elements.size(); i != e; ++i) {
    EnumConstantDecl *ECD =
      cast_or_null<EnumConstantDecl>(Elements[i]);
    if (!ECD) continue;  // Already issued a diagnostic.

    const llvm::APSInt &InitVal = ECD->getInitVal();

    // Keep track of the size of positive and negative values.
    if (InitVal.isUnsigned() || InitVal.isNonNegative()) {
      // If the enumerator is zero that should still be counted as a positive
      // bit since we need a bit to store the value zero.
      unsigned ActiveBits = InitVal.getActiveBits();
      NumPositiveBits = std::max({NumPositiveBits, ActiveBits, 1u});
    } else {
      NumNegativeBits =
          std::max(NumNegativeBits, (unsigned)InitVal.getSignificantBits());
    }
  }

  // If we have an empty set of enumerators we still need one bit.
  // From [dcl.enum]p8
  // If the enumerator-list is empty, the values of the enumeration are as if
  // the enumeration had a single enumerator with value 0
  if (!NumPositiveBits && !NumNegativeBits)
    NumPositiveBits = 1;

  // Figure out the type that should be used for this enum.
  QualType BestType;
  unsigned BestWidth;

  // C++0x N3000 [conv.prom]p3:
  //   An rvalue of an unscoped enumeration type whose underlying
  //   type is not fixed can be converted to an rvalue of the first
  //   of the following types that can represent all the values of
  //   the enumeration: int, unsigned int, long int, unsigned long
  //   int, long long int, or unsigned long long int.
  // C99 6.4.4.3p2:
  //   An identifier declared as an enumeration constant has type int.
  // The C99 rule is modified by a gcc extension
  QualType BestPromotionType;

  bool Packed = Enum->hasAttr<PackedAttr>();
  // -fshort-enums is the equivalent to specifying the packed attribute on all
  // enum definitions.
  if (LangOpts.ShortEnums)
    Packed = true;

  // If the enum already has a type because it is fixed or dictated by the
  // target, promote that type instead of analyzing the enumerators.
  if (Enum->isComplete()) {
    BestType = Enum->getIntegerType();
    if (Context.isPromotableIntegerType(BestType))
      BestPromotionType = Context.getPromotedIntegerType(BestType);
    else
      BestPromotionType = BestType;

    BestWidth = Context.getIntWidth(BestType);
  }
  else if (NumNegativeBits) {
    // If there is a negative value, figure out the smallest integer type (of
    // int/long/longlong) that fits.
    // If it's packed, check also if it fits a char or a short.
    if (Packed && NumNegativeBits <= CharWidth && NumPositiveBits < CharWidth) {
      BestType = Context.SignedCharTy;
      BestWidth = CharWidth;
    } else if (Packed && NumNegativeBits <= ShortWidth &&
               NumPositiveBits < ShortWidth) {
      BestType = Context.ShortTy;
      BestWidth = ShortWidth;
    } else if (NumNegativeBits <= IntWidth && NumPositiveBits < IntWidth) {
      BestType = Context.IntTy;
      BestWidth = IntWidth;
    } else {
      BestWidth = Context.getTargetInfo().getLongWidth();

      if (NumNegativeBits <= BestWidth && NumPositiveBits < BestWidth) {
        BestType = Context.LongTy;
      } else {
        BestWidth = Context.getTargetInfo().getLongLongWidth();

        if (NumNegativeBits > BestWidth || NumPositiveBits >= BestWidth)
          Diag(Enum->getLocation(), diag::ext_enum_too_large);
        BestType = Context.LongLongTy;
      }
    }
    BestPromotionType = (BestWidth <= IntWidth ? Context.IntTy : BestType);
  } else {
    // If there is no negative value, figure out the smallest type that fits
    // all of the enumerator values.
    // If it's packed, check also if it fits a char or a short.
    if (Packed && NumPositiveBits <= CharWidth) {
      BestType = Context.UnsignedCharTy;
      BestPromotionType = Context.IntTy;
      BestWidth = CharWidth;
    } else if (Packed && NumPositiveBits <= ShortWidth) {
      BestType = Context.UnsignedShortTy;
      BestPromotionType = Context.IntTy;
      BestWidth = ShortWidth;
    } else if (NumPositiveBits <= IntWidth) {
      BestType = Context.UnsignedIntTy;
      BestWidth = IntWidth;
      BestPromotionType
        = (NumPositiveBits == BestWidth || !getLangOpts().CPlusPlus)
                           ? Context.UnsignedIntTy : Context.IntTy;
    } else if (NumPositiveBits <=
               (BestWidth = Context.getTargetInfo().getLongWidth())) {
      BestType = Context.UnsignedLongTy;
      BestPromotionType
        = (NumPositiveBits == BestWidth || !getLangOpts().CPlusPlus)
                           ? Context.UnsignedLongTy : Context.LongTy;
    } else {
      BestWidth = Context.getTargetInfo().getLongLongWidth();
      if (NumPositiveBits > BestWidth) {
        // This can happen with bit-precise integer types, but those are not
        // allowed as the type for an enumerator per C23 6.7.2.2p4 and p12.
        // FIXME: GCC uses __int128_t and __uint128_t for cases that fit within
        // a 128-bit integer, we should consider doing the same.
        Diag(Enum->getLocation(), diag::ext_enum_too_large);
      }
      BestType = Context.UnsignedLongLongTy;
      BestPromotionType
        = (NumPositiveBits == BestWidth || !getLangOpts().CPlusPlus)
                           ? Context.UnsignedLongLongTy : Context.LongLongTy;
    }
  }

  // Loop over all of the enumerator constants, changing their types to match
  // the type of the enum if needed.
  for (auto *D : Elements) {
    auto *ECD = cast_or_null<EnumConstantDecl>(D);
    if (!ECD) continue;  // Already issued a diagnostic.

    // Standard C says the enumerators have int type, but we allow, as an
    // extension, the enumerators to be larger than int size.  If each
    // enumerator value fits in an int, type it as an int, otherwise type it the
    // same as the enumerator decl itself.  This means that in "enum { X = 1U }"
    // that X has type 'int', not 'unsigned'.

    // Determine whether the value fits into an int.
    llvm::APSInt InitVal = ECD->getInitVal();

    // If it fits into an integer type, force it.  Otherwise force it to match
    // the enum decl type.
    QualType NewTy;
    unsigned NewWidth;
    bool NewSign;
    if (!getLangOpts().CPlusPlus &&
        !Enum->isFixed() &&
        isRepresentableIntegerValue(Context, InitVal, Context.IntTy)) {
      NewTy = Context.IntTy;
      NewWidth = IntWidth;
      NewSign = true;
    } else if (ECD->getType() == BestType) {
      // Already the right type!
      if (getLangOpts().CPlusPlus)
        // C++ [dcl.enum]p4: Following the closing brace of an
        // enum-specifier, each enumerator has the type of its
        // enumeration.
        ECD->setType(EnumType);
      continue;
    } else {
      NewTy = BestType;
      NewWidth = BestWidth;
      NewSign = BestType->isSignedIntegerOrEnumerationType();
    }

    // Adjust the APSInt value.
    InitVal = InitVal.extOrTrunc(NewWidth);
    InitVal.setIsSigned(NewSign);
    ECD->setInitVal(Context, InitVal);

    // Adjust the Expr initializer and type.
    if (ECD->getInitExpr() &&
        !Context.hasSameType(NewTy, ECD->getInitExpr()->getType()))
      ECD->setInitExpr(ImplicitCastExpr::Create(
          Context, NewTy, CK_IntegralCast, ECD->getInitExpr(),
          /*base paths*/ nullptr, VK_PRValue, FPOptionsOverride()));
    if (getLangOpts().CPlusPlus)
      // C++ [dcl.enum]p4: Following the closing brace of an
      // enum-specifier, each enumerator has the type of its
      // enumeration.
      ECD->setType(EnumType);
    else
      ECD->setType(NewTy);
  }

  Enum->completeDefinition(BestType, BestPromotionType,
                           NumPositiveBits, NumNegativeBits);

  CheckForDuplicateEnumValues(*this, Elements, Enum, EnumType);

  if (Enum->isClosedFlag()) {
    for (Decl *D : Elements) {
      EnumConstantDecl *ECD = cast_or_null<EnumConstantDecl>(D);
      if (!ECD) continue;  // Already issued a diagnostic.

      llvm::APSInt InitVal = ECD->getInitVal();
      if (InitVal != 0 && !InitVal.isPowerOf2() &&
          !IsValueInFlagEnum(Enum, InitVal, true))
        Diag(ECD->getLocation(), diag::warn_flag_enum_constant_out_of_range)
          << ECD << Enum;
    }
  }

  // Now that the enum type is defined, ensure it's not been underaligned.
  if (Enum->hasAttrs())
    CheckAlignasUnderalignment(Enum);
}

Decl *Sema::ActOnFileScopeAsmDecl(Expr *expr,
                                  SourceLocation StartLoc,
                                  SourceLocation EndLoc) {
  StringLiteral *AsmString = cast<StringLiteral>(expr);

  FileScopeAsmDecl *New = FileScopeAsmDecl::Create(Context, CurContext,
                                                   AsmString, StartLoc,
                                                   EndLoc);
  CurContext->addDecl(New);
  return New;
}

TopLevelStmtDecl *Sema::ActOnStartTopLevelStmtDecl(Scope *S) {
  auto *New = TopLevelStmtDecl::Create(Context, /*Statement=*/nullptr);
  CurContext->addDecl(New);
  PushDeclContext(S, New);
  PushFunctionScope();
  PushCompoundScope(false);
  return New;
}

void Sema::ActOnFinishTopLevelStmtDecl(TopLevelStmtDecl *D, Stmt *Statement) {
  D->setStmt(Statement);
  PopCompoundScope();
  PopFunctionScopeInfo();
  PopDeclContext();
}

void Sema::ActOnPragmaRedefineExtname(IdentifierInfo* Name,
                                      IdentifierInfo* AliasName,
                                      SourceLocation PragmaLoc,
                                      SourceLocation NameLoc,
                                      SourceLocation AliasNameLoc) {
  NamedDecl *PrevDecl = LookupSingleName(TUScope, Name, NameLoc,
                                         LookupOrdinaryName);
  AttributeCommonInfo Info(AliasName, SourceRange(AliasNameLoc),
                           AttributeCommonInfo::Form::Pragma());
  AsmLabelAttr *Attr = AsmLabelAttr::CreateImplicit(
      Context, AliasName->getName(), /*IsLiteralLabel=*/true, Info);

  // If a declaration that:
  // 1) declares a function or a variable
  // 2) has external linkage
  // already exists, add a label attribute to it.
  if (PrevDecl && (isa<FunctionDecl>(PrevDecl) || isa<VarDecl>(PrevDecl))) {
    if (isDeclExternC(PrevDecl))
      PrevDecl->addAttr(Attr);
    else
      Diag(PrevDecl->getLocation(), diag::warn_redefine_extname_not_applied)
          << /*Variable*/(isa<FunctionDecl>(PrevDecl) ? 0 : 1) << PrevDecl;
    // Otherwise, add a label attribute to ExtnameUndeclaredIdentifiers.
  } else
    (void)ExtnameUndeclaredIdentifiers.insert(std::make_pair(Name, Attr));
}

void Sema::ActOnPragmaWeakID(IdentifierInfo* Name,
                             SourceLocation PragmaLoc,
                             SourceLocation NameLoc) {
  Decl *PrevDecl = LookupSingleName(TUScope, Name, NameLoc, LookupOrdinaryName);

  if (PrevDecl) {
    PrevDecl->addAttr(WeakAttr::CreateImplicit(Context, PragmaLoc));
  } else {
    (void)WeakUndeclaredIdentifiers[Name].insert(WeakInfo(nullptr, NameLoc));
  }
}

void Sema::ActOnPragmaWeakAlias(IdentifierInfo* Name,
                                IdentifierInfo* AliasName,
                                SourceLocation PragmaLoc,
                                SourceLocation NameLoc,
                                SourceLocation AliasNameLoc) {
  Decl *PrevDecl = LookupSingleName(TUScope, AliasName, AliasNameLoc,
                                    LookupOrdinaryName);
  WeakInfo W = WeakInfo(Name, NameLoc);

  if (PrevDecl && (isa<FunctionDecl>(PrevDecl) || isa<VarDecl>(PrevDecl))) {
    if (!PrevDecl->hasAttr<AliasAttr>())
      if (NamedDecl *ND = dyn_cast<NamedDecl>(PrevDecl))
        DeclApplyPragmaWeak(TUScope, ND, W);
  } else {
    (void)WeakUndeclaredIdentifiers[AliasName].insert(W);
  }
}

Sema::FunctionEmissionStatus Sema::getEmissionStatus(const FunctionDecl *FD,
                                                     bool Final) {
  assert(FD && "Expected non-null FunctionDecl");

  // SYCL functions can be template, so we check if they have appropriate
  // attribute prior to checking if it is a template.
  if (LangOpts.SYCLIsDevice && FD->hasAttr<SYCLKernelAttr>())
    return FunctionEmissionStatus::Emitted;

  // Templates are emitted when they're instantiated.
  if (FD->isDependentContext())
    return FunctionEmissionStatus::TemplateDiscarded;

  // Check whether this function is an externally visible definition.
  auto IsEmittedForExternalSymbol = [this, FD]() {
    // We have to check the GVA linkage of the function's *definition* -- if we
    // only have a declaration, we don't know whether or not the function will
    // be emitted, because (say) the definition could include "inline".
    const FunctionDecl *Def = FD->getDefinition();

    return Def && !isDiscardableGVALinkage(
                      getASTContext().GetGVALinkageForFunction(Def));
  };

  if (LangOpts.OpenMPIsTargetDevice) {
    // In OpenMP device mode we will not emit host only functions, or functions
    // we don't need due to their linkage.
    std::optional<OMPDeclareTargetDeclAttr::DevTypeTy> DevTy =
        OMPDeclareTargetDeclAttr::getDeviceType(FD->getCanonicalDecl());
    // DevTy may be changed later by
    //  #pragma omp declare target to(*) device_type(*).
    // Therefore DevTy having no value does not imply host. The emission status
    // will be checked again at the end of compilation unit with Final = true.
    if (DevTy)
      if (*DevTy == OMPDeclareTargetDeclAttr::DT_Host)
        return FunctionEmissionStatus::OMPDiscarded;
    // If we have an explicit value for the device type, or we are in a target
    // declare context, we need to emit all extern and used symbols.
    if (OpenMP().isInOpenMPDeclareTargetContext() || DevTy)
      if (IsEmittedForExternalSymbol())
        return FunctionEmissionStatus::Emitted;
    // Device mode only emits what it must, if it wasn't tagged yet and needed,
    // we'll omit it.
    if (Final)
      return FunctionEmissionStatus::OMPDiscarded;
  } else if (LangOpts.OpenMP > 45) {
    // In OpenMP host compilation prior to 5.0 everything was an emitted host
    // function. In 5.0, no_host was introduced which might cause a function to
    // be omitted.
    std::optional<OMPDeclareTargetDeclAttr::DevTypeTy> DevTy =
        OMPDeclareTargetDeclAttr::getDeviceType(FD->getCanonicalDecl());
    if (DevTy)
      if (*DevTy == OMPDeclareTargetDeclAttr::DT_NoHost)
        return FunctionEmissionStatus::OMPDiscarded;
  }

  if (Final && LangOpts.OpenMP && !LangOpts.CUDA)
    return FunctionEmissionStatus::Emitted;

  if (LangOpts.CUDA) {
    // When compiling for device, host functions are never emitted.  Similarly,
    // when compiling for host, device and global functions are never emitted.
    // (Technically, we do emit a host-side stub for global functions, but this
    // doesn't count for our purposes here.)
    CUDAFunctionTarget T = CUDA().IdentifyTarget(FD);
    if (LangOpts.CUDAIsDevice && T == CUDAFunctionTarget::Host)
      return FunctionEmissionStatus::CUDADiscarded;
    if (!LangOpts.CUDAIsDevice &&
        (T == CUDAFunctionTarget::Device || T == CUDAFunctionTarget::Global))
      return FunctionEmissionStatus::CUDADiscarded;

    if (IsEmittedForExternalSymbol())
      return FunctionEmissionStatus::Emitted;
  }

  // Otherwise, the function is known-emitted if it's in our set of
  // known-emitted functions.
  return FunctionEmissionStatus::Unknown;
}

bool Sema::shouldIgnoreInHostDeviceCheck(FunctionDecl *Callee) {
  // Host-side references to a __global__ function refer to the stub, so the
  // function itself is never emitted and therefore should not be marked.
  // If we have host fn calls kernel fn calls host+device, the HD function
  // does not get instantiated on the host. We model this by omitting at the
  // call to the kernel from the callgraph. This ensures that, when compiling
  // for host, only HD functions actually called from the host get marked as
  // known-emitted.
  return LangOpts.CUDA && !LangOpts.CUDAIsDevice &&
         CUDA().IdentifyTarget(Callee) == CUDAFunctionTarget::Global;
}

void Sema::diagnoseFunctionEffectMergeConflicts(
    const FunctionEffectSet::Conflicts &Errs, SourceLocation NewLoc,
    SourceLocation OldLoc) {
  for (const FunctionEffectSet::Conflict &Conflict : Errs) {
    Diag(NewLoc, diag::warn_conflicting_func_effects)
        << Conflict.Kept.description() << Conflict.Rejected.description();
    Diag(OldLoc, diag::note_previous_declaration);
  }
}

bool Sema::diagnoseConflictingFunctionEffect(
    const FunctionEffectsRef &FX, const FunctionEffectWithCondition &NewEC,
    SourceLocation NewAttrLoc) {
  // If the new effect has a condition, we can't detect conflicts until the
  // condition is resolved.
  if (NewEC.Cond.getCondition() != nullptr)
    return false;

  // Diagnose the new attribute as incompatible with a previous one.
  auto Incompatible = [&](const FunctionEffectWithCondition &PrevEC) {
    Diag(NewAttrLoc, diag::err_attributes_are_not_compatible)
        << ("'" + NewEC.description() + "'")
        << ("'" + PrevEC.description() + "'") << false;
    // We don't necessarily have the location of the previous attribute,
    // so no note.
    return true;
  };

  // Compare against previous attributes.
  FunctionEffect::Kind NewKind = NewEC.Effect.kind();

  for (const FunctionEffectWithCondition &PrevEC : FX) {
    // Again, can't check yet when the effect is conditional.
    if (PrevEC.Cond.getCondition() != nullptr)
      continue;

    FunctionEffect::Kind PrevKind = PrevEC.Effect.kind();
    // Note that we allow PrevKind == NewKind; it's redundant and ignored.

    if (PrevEC.Effect.oppositeKind() == NewKind)
      return Incompatible(PrevEC);

    // A new allocating is incompatible with a previous nonblocking.
    if (PrevKind == FunctionEffect::Kind::NonBlocking &&
        NewKind == FunctionEffect::Kind::Allocating)
      return Incompatible(PrevEC);

    // A new nonblocking is incompatible with a previous allocating.
    if (PrevKind == FunctionEffect::Kind::Allocating &&
        NewKind == FunctionEffect::Kind::NonBlocking)
      return Incompatible(PrevEC);
  }

  return false;
}
