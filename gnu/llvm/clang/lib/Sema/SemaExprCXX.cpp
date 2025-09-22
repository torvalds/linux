//===--- SemaExprCXX.cpp - Semantic Analysis for Expressions --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Implements semantic analysis for C++ expressions.
///
//===----------------------------------------------------------------------===//

#include "TreeTransform.h"
#include "TypeLocBuilder.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/AlignedAllocation.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Basic/TypeTraits.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaLambda.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaPPC.h"
#include "clang/Sema/Template.h"
#include "clang/Sema/TemplateDeduction.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TypeSize.h"
#include <optional>
using namespace clang;
using namespace sema;

ParsedType Sema::getInheritingConstructorName(CXXScopeSpec &SS,
                                              SourceLocation NameLoc,
                                              const IdentifierInfo &Name) {
  NestedNameSpecifier *NNS = SS.getScopeRep();

  // Convert the nested-name-specifier into a type.
  QualType Type;
  switch (NNS->getKind()) {
  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    Type = QualType(NNS->getAsType(), 0);
    break;

  case NestedNameSpecifier::Identifier:
    // Strip off the last layer of the nested-name-specifier and build a
    // typename type for it.
    assert(NNS->getAsIdentifier() == &Name && "not a constructor name");
    Type = Context.getDependentNameType(
        ElaboratedTypeKeyword::None, NNS->getPrefix(), NNS->getAsIdentifier());
    break;

  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Super:
  case NestedNameSpecifier::Namespace:
  case NestedNameSpecifier::NamespaceAlias:
    llvm_unreachable("Nested name specifier is not a type for inheriting ctor");
  }

  // This reference to the type is located entirely at the location of the
  // final identifier in the qualified-id.
  return CreateParsedType(Type,
                          Context.getTrivialTypeSourceInfo(Type, NameLoc));
}

ParsedType Sema::getConstructorName(const IdentifierInfo &II,
                                    SourceLocation NameLoc, Scope *S,
                                    CXXScopeSpec &SS, bool EnteringContext) {
  CXXRecordDecl *CurClass = getCurrentClass(S, &SS);
  assert(CurClass && &II == CurClass->getIdentifier() &&
         "not a constructor name");

  // When naming a constructor as a member of a dependent context (eg, in a
  // friend declaration or an inherited constructor declaration), form an
  // unresolved "typename" type.
  if (CurClass->isDependentContext() && !EnteringContext && SS.getScopeRep()) {
    QualType T = Context.getDependentNameType(ElaboratedTypeKeyword::None,
                                              SS.getScopeRep(), &II);
    return ParsedType::make(T);
  }

  if (SS.isNotEmpty() && RequireCompleteDeclContext(SS, CurClass))
    return ParsedType();

  // Find the injected-class-name declaration. Note that we make no attempt to
  // diagnose cases where the injected-class-name is shadowed: the only
  // declaration that can validly shadow the injected-class-name is a
  // non-static data member, and if the class contains both a non-static data
  // member and a constructor then it is ill-formed (we check that in
  // CheckCompletedCXXClass).
  CXXRecordDecl *InjectedClassName = nullptr;
  for (NamedDecl *ND : CurClass->lookup(&II)) {
    auto *RD = dyn_cast<CXXRecordDecl>(ND);
    if (RD && RD->isInjectedClassName()) {
      InjectedClassName = RD;
      break;
    }
  }
  if (!InjectedClassName) {
    if (!CurClass->isInvalidDecl()) {
      // FIXME: RequireCompleteDeclContext doesn't check dependent contexts
      // properly. Work around it here for now.
      Diag(SS.getLastQualifierNameLoc(),
           diag::err_incomplete_nested_name_spec) << CurClass << SS.getRange();
    }
    return ParsedType();
  }

  QualType T = Context.getTypeDeclType(InjectedClassName);
  DiagnoseUseOfDecl(InjectedClassName, NameLoc);
  MarkAnyDeclReferenced(NameLoc, InjectedClassName, /*OdrUse=*/false);

  return ParsedType::make(T);
}

ParsedType Sema::getDestructorName(const IdentifierInfo &II,
                                   SourceLocation NameLoc, Scope *S,
                                   CXXScopeSpec &SS, ParsedType ObjectTypePtr,
                                   bool EnteringContext) {
  // Determine where to perform name lookup.

  // FIXME: This area of the standard is very messy, and the current
  // wording is rather unclear about which scopes we search for the
  // destructor name; see core issues 399 and 555. Issue 399 in
  // particular shows where the current description of destructor name
  // lookup is completely out of line with existing practice, e.g.,
  // this appears to be ill-formed:
  //
  //   namespace N {
  //     template <typename T> struct S {
  //       ~S();
  //     };
  //   }
  //
  //   void f(N::S<int>* s) {
  //     s->N::S<int>::~S();
  //   }
  //
  // See also PR6358 and PR6359.
  //
  // For now, we accept all the cases in which the name given could plausibly
  // be interpreted as a correct destructor name, issuing off-by-default
  // extension diagnostics on the cases that don't strictly conform to the
  // C++20 rules. This basically means we always consider looking in the
  // nested-name-specifier prefix, the complete nested-name-specifier, and
  // the scope, and accept if we find the expected type in any of the three
  // places.

  if (SS.isInvalid())
    return nullptr;

  // Whether we've failed with a diagnostic already.
  bool Failed = false;

  llvm::SmallVector<NamedDecl*, 8> FoundDecls;
  llvm::SmallPtrSet<CanonicalDeclPtr<Decl>, 8> FoundDeclSet;

  // If we have an object type, it's because we are in a
  // pseudo-destructor-expression or a member access expression, and
  // we know what type we're looking for.
  QualType SearchType =
      ObjectTypePtr ? GetTypeFromParser(ObjectTypePtr) : QualType();

  auto CheckLookupResult = [&](LookupResult &Found) -> ParsedType {
    auto IsAcceptableResult = [&](NamedDecl *D) -> bool {
      auto *Type = dyn_cast<TypeDecl>(D->getUnderlyingDecl());
      if (!Type)
        return false;

      if (SearchType.isNull() || SearchType->isDependentType())
        return true;

      QualType T = Context.getTypeDeclType(Type);
      return Context.hasSameUnqualifiedType(T, SearchType);
    };

    unsigned NumAcceptableResults = 0;
    for (NamedDecl *D : Found) {
      if (IsAcceptableResult(D))
        ++NumAcceptableResults;

      // Don't list a class twice in the lookup failure diagnostic if it's
      // found by both its injected-class-name and by the name in the enclosing
      // scope.
      if (auto *RD = dyn_cast<CXXRecordDecl>(D))
        if (RD->isInjectedClassName())
          D = cast<NamedDecl>(RD->getParent());

      if (FoundDeclSet.insert(D).second)
        FoundDecls.push_back(D);
    }

    // As an extension, attempt to "fix" an ambiguity by erasing all non-type
    // results, and all non-matching results if we have a search type. It's not
    // clear what the right behavior is if destructor lookup hits an ambiguity,
    // but other compilers do generally accept at least some kinds of
    // ambiguity.
    if (Found.isAmbiguous() && NumAcceptableResults == 1) {
      Diag(NameLoc, diag::ext_dtor_name_ambiguous);
      LookupResult::Filter F = Found.makeFilter();
      while (F.hasNext()) {
        NamedDecl *D = F.next();
        if (auto *TD = dyn_cast<TypeDecl>(D->getUnderlyingDecl()))
          Diag(D->getLocation(), diag::note_destructor_type_here)
              << Context.getTypeDeclType(TD);
        else
          Diag(D->getLocation(), diag::note_destructor_nontype_here);

        if (!IsAcceptableResult(D))
          F.erase();
      }
      F.done();
    }

    if (Found.isAmbiguous())
      Failed = true;

    if (TypeDecl *Type = Found.getAsSingle<TypeDecl>()) {
      if (IsAcceptableResult(Type)) {
        QualType T = Context.getTypeDeclType(Type);
        MarkAnyDeclReferenced(Type->getLocation(), Type, /*OdrUse=*/false);
        return CreateParsedType(
            Context.getElaboratedType(ElaboratedTypeKeyword::None, nullptr, T),
            Context.getTrivialTypeSourceInfo(T, NameLoc));
      }
    }

    return nullptr;
  };

  bool IsDependent = false;

  auto LookupInObjectType = [&]() -> ParsedType {
    if (Failed || SearchType.isNull())
      return nullptr;

    IsDependent |= SearchType->isDependentType();

    LookupResult Found(*this, &II, NameLoc, LookupDestructorName);
    DeclContext *LookupCtx = computeDeclContext(SearchType);
    if (!LookupCtx)
      return nullptr;
    LookupQualifiedName(Found, LookupCtx);
    return CheckLookupResult(Found);
  };

  auto LookupInNestedNameSpec = [&](CXXScopeSpec &LookupSS) -> ParsedType {
    if (Failed)
      return nullptr;

    IsDependent |= isDependentScopeSpecifier(LookupSS);
    DeclContext *LookupCtx = computeDeclContext(LookupSS, EnteringContext);
    if (!LookupCtx)
      return nullptr;

    LookupResult Found(*this, &II, NameLoc, LookupDestructorName);
    if (RequireCompleteDeclContext(LookupSS, LookupCtx)) {
      Failed = true;
      return nullptr;
    }
    LookupQualifiedName(Found, LookupCtx);
    return CheckLookupResult(Found);
  };

  auto LookupInScope = [&]() -> ParsedType {
    if (Failed || !S)
      return nullptr;

    LookupResult Found(*this, &II, NameLoc, LookupDestructorName);
    LookupName(Found, S);
    return CheckLookupResult(Found);
  };

  // C++2a [basic.lookup.qual]p6:
  //   In a qualified-id of the form
  //
  //     nested-name-specifier[opt] type-name :: ~ type-name
  //
  //   the second type-name is looked up in the same scope as the first.
  //
  // We interpret this as meaning that if you do a dual-scope lookup for the
  // first name, you also do a dual-scope lookup for the second name, per
  // C++ [basic.lookup.classref]p4:
  //
  //   If the id-expression in a class member access is a qualified-id of the
  //   form
  //
  //     class-name-or-namespace-name :: ...
  //
  //   the class-name-or-namespace-name following the . or -> is first looked
  //   up in the class of the object expression and the name, if found, is used.
  //   Otherwise, it is looked up in the context of the entire
  //   postfix-expression.
  //
  // This looks in the same scopes as for an unqualified destructor name:
  //
  // C++ [basic.lookup.classref]p3:
  //   If the unqualified-id is ~ type-name, the type-name is looked up
  //   in the context of the entire postfix-expression. If the type T
  //   of the object expression is of a class type C, the type-name is
  //   also looked up in the scope of class C. At least one of the
  //   lookups shall find a name that refers to cv T.
  //
  // FIXME: The intent is unclear here. Should type-name::~type-name look in
  // the scope anyway if it finds a non-matching name declared in the class?
  // If both lookups succeed and find a dependent result, which result should
  // we retain? (Same question for p->~type-name().)

  if (NestedNameSpecifier *Prefix =
      SS.isSet() ? SS.getScopeRep()->getPrefix() : nullptr) {
    // This is
    //
    //   nested-name-specifier type-name :: ~ type-name
    //
    // Look for the second type-name in the nested-name-specifier.
    CXXScopeSpec PrefixSS;
    PrefixSS.Adopt(NestedNameSpecifierLoc(Prefix, SS.location_data()));
    if (ParsedType T = LookupInNestedNameSpec(PrefixSS))
      return T;
  } else {
    // This is one of
    //
    //   type-name :: ~ type-name
    //   ~ type-name
    //
    // Look in the scope and (if any) the object type.
    if (ParsedType T = LookupInScope())
      return T;
    if (ParsedType T = LookupInObjectType())
      return T;
  }

  if (Failed)
    return nullptr;

  if (IsDependent) {
    // We didn't find our type, but that's OK: it's dependent anyway.

    // FIXME: What if we have no nested-name-specifier?
    QualType T =
        CheckTypenameType(ElaboratedTypeKeyword::None, SourceLocation(),
                          SS.getWithLocInContext(Context), II, NameLoc);
    return ParsedType::make(T);
  }

  // The remaining cases are all non-standard extensions imitating the behavior
  // of various other compilers.
  unsigned NumNonExtensionDecls = FoundDecls.size();

  if (SS.isSet()) {
    // For compatibility with older broken C++ rules and existing code,
    //
    //   nested-name-specifier :: ~ type-name
    //
    // also looks for type-name within the nested-name-specifier.
    if (ParsedType T = LookupInNestedNameSpec(SS)) {
      Diag(SS.getEndLoc(), diag::ext_dtor_named_in_wrong_scope)
          << SS.getRange()
          << FixItHint::CreateInsertion(SS.getEndLoc(),
                                        ("::" + II.getName()).str());
      return T;
    }

    // For compatibility with other compilers and older versions of Clang,
    //
    //   nested-name-specifier type-name :: ~ type-name
    //
    // also looks for type-name in the scope. Unfortunately, we can't
    // reasonably apply this fallback for dependent nested-name-specifiers.
    if (SS.isValid() && SS.getScopeRep()->getPrefix()) {
      if (ParsedType T = LookupInScope()) {
        Diag(SS.getEndLoc(), diag::ext_qualified_dtor_named_in_lexical_scope)
            << FixItHint::CreateRemoval(SS.getRange());
        Diag(FoundDecls.back()->getLocation(), diag::note_destructor_type_here)
            << GetTypeFromParser(T);
        return T;
      }
    }
  }

  // We didn't find anything matching; tell the user what we did find (if
  // anything).

  // Don't tell the user about declarations we shouldn't have found.
  FoundDecls.resize(NumNonExtensionDecls);

  // List types before non-types.
  std::stable_sort(FoundDecls.begin(), FoundDecls.end(),
                   [](NamedDecl *A, NamedDecl *B) {
                     return isa<TypeDecl>(A->getUnderlyingDecl()) >
                            isa<TypeDecl>(B->getUnderlyingDecl());
                   });

  // Suggest a fixit to properly name the destroyed type.
  auto MakeFixItHint = [&]{
    const CXXRecordDecl *Destroyed = nullptr;
    // FIXME: If we have a scope specifier, suggest its last component?
    if (!SearchType.isNull())
      Destroyed = SearchType->getAsCXXRecordDecl();
    else if (S)
      Destroyed = dyn_cast_or_null<CXXRecordDecl>(S->getEntity());
    if (Destroyed)
      return FixItHint::CreateReplacement(SourceRange(NameLoc),
                                          Destroyed->getNameAsString());
    return FixItHint();
  };

  if (FoundDecls.empty()) {
    // FIXME: Attempt typo-correction?
    Diag(NameLoc, diag::err_undeclared_destructor_name)
      << &II << MakeFixItHint();
  } else if (!SearchType.isNull() && FoundDecls.size() == 1) {
    if (auto *TD = dyn_cast<TypeDecl>(FoundDecls[0]->getUnderlyingDecl())) {
      assert(!SearchType.isNull() &&
             "should only reject a type result if we have a search type");
      QualType T = Context.getTypeDeclType(TD);
      Diag(NameLoc, diag::err_destructor_expr_type_mismatch)
          << T << SearchType << MakeFixItHint();
    } else {
      Diag(NameLoc, diag::err_destructor_expr_nontype)
          << &II << MakeFixItHint();
    }
  } else {
    Diag(NameLoc, SearchType.isNull() ? diag::err_destructor_name_nontype
                                      : diag::err_destructor_expr_mismatch)
        << &II << SearchType << MakeFixItHint();
  }

  for (NamedDecl *FoundD : FoundDecls) {
    if (auto *TD = dyn_cast<TypeDecl>(FoundD->getUnderlyingDecl()))
      Diag(FoundD->getLocation(), diag::note_destructor_type_here)
          << Context.getTypeDeclType(TD);
    else
      Diag(FoundD->getLocation(), diag::note_destructor_nontype_here)
          << FoundD;
  }

  return nullptr;
}

ParsedType Sema::getDestructorTypeForDecltype(const DeclSpec &DS,
                                              ParsedType ObjectType) {
  if (DS.getTypeSpecType() == DeclSpec::TST_error)
    return nullptr;

  if (DS.getTypeSpecType() == DeclSpec::TST_decltype_auto) {
    Diag(DS.getTypeSpecTypeLoc(), diag::err_decltype_auto_invalid);
    return nullptr;
  }

  assert(DS.getTypeSpecType() == DeclSpec::TST_decltype &&
         "unexpected type in getDestructorType");
  QualType T = BuildDecltypeType(DS.getRepAsExpr());

  // If we know the type of the object, check that the correct destructor
  // type was named now; we can give better diagnostics this way.
  QualType SearchType = GetTypeFromParser(ObjectType);
  if (!SearchType.isNull() && !SearchType->isDependentType() &&
      !Context.hasSameUnqualifiedType(T, SearchType)) {
    Diag(DS.getTypeSpecTypeLoc(), diag::err_destructor_expr_type_mismatch)
      << T << SearchType;
    return nullptr;
  }

  return ParsedType::make(T);
}

bool Sema::checkLiteralOperatorId(const CXXScopeSpec &SS,
                                  const UnqualifiedId &Name, bool IsUDSuffix) {
  assert(Name.getKind() == UnqualifiedIdKind::IK_LiteralOperatorId);
  if (!IsUDSuffix) {
    // [over.literal] p8
    //
    // double operator""_Bq(long double);  // OK: not a reserved identifier
    // double operator"" _Bq(long double); // ill-formed, no diagnostic required
    const IdentifierInfo *II = Name.Identifier;
    ReservedIdentifierStatus Status = II->isReserved(PP.getLangOpts());
    SourceLocation Loc = Name.getEndLoc();
    if (!PP.getSourceManager().isInSystemHeader(Loc)) {
      if (auto Hint = FixItHint::CreateReplacement(
              Name.getSourceRange(),
              (StringRef("operator\"\"") + II->getName()).str());
          isReservedInAllContexts(Status)) {
        Diag(Loc, diag::warn_reserved_extern_symbol)
            << II << static_cast<int>(Status) << Hint;
      } else {
        Diag(Loc, diag::warn_deprecated_literal_operator_id) << II << Hint;
      }
    }
  }

  if (!SS.isValid())
    return false;

  switch (SS.getScopeRep()->getKind()) {
  case NestedNameSpecifier::Identifier:
  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    // Per C++11 [over.literal]p2, literal operators can only be declared at
    // namespace scope. Therefore, this unqualified-id cannot name anything.
    // Reject it early, because we have no AST representation for this in the
    // case where the scope is dependent.
    Diag(Name.getBeginLoc(), diag::err_literal_operator_id_outside_namespace)
        << SS.getScopeRep();
    return true;

  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Super:
  case NestedNameSpecifier::Namespace:
  case NestedNameSpecifier::NamespaceAlias:
    return false;
  }

  llvm_unreachable("unknown nested name specifier kind");
}

ExprResult Sema::BuildCXXTypeId(QualType TypeInfoType,
                                SourceLocation TypeidLoc,
                                TypeSourceInfo *Operand,
                                SourceLocation RParenLoc) {
  // C++ [expr.typeid]p4:
  //   The top-level cv-qualifiers of the lvalue expression or the type-id
  //   that is the operand of typeid are always ignored.
  //   If the type of the type-id is a class type or a reference to a class
  //   type, the class shall be completely-defined.
  Qualifiers Quals;
  QualType T
    = Context.getUnqualifiedArrayType(Operand->getType().getNonReferenceType(),
                                      Quals);
  if (T->getAs<RecordType>() &&
      RequireCompleteType(TypeidLoc, T, diag::err_incomplete_typeid))
    return ExprError();

  if (T->isVariablyModifiedType())
    return ExprError(Diag(TypeidLoc, diag::err_variably_modified_typeid) << T);

  if (CheckQualifiedFunctionForTypeId(T, TypeidLoc))
    return ExprError();

  return new (Context) CXXTypeidExpr(TypeInfoType.withConst(), Operand,
                                     SourceRange(TypeidLoc, RParenLoc));
}

ExprResult Sema::BuildCXXTypeId(QualType TypeInfoType,
                                SourceLocation TypeidLoc,
                                Expr *E,
                                SourceLocation RParenLoc) {
  bool WasEvaluated = false;
  if (E && !E->isTypeDependent()) {
    if (E->hasPlaceholderType()) {
      ExprResult result = CheckPlaceholderExpr(E);
      if (result.isInvalid()) return ExprError();
      E = result.get();
    }

    QualType T = E->getType();
    if (const RecordType *RecordT = T->getAs<RecordType>()) {
      CXXRecordDecl *RecordD = cast<CXXRecordDecl>(RecordT->getDecl());
      // C++ [expr.typeid]p3:
      //   [...] If the type of the expression is a class type, the class
      //   shall be completely-defined.
      if (RequireCompleteType(TypeidLoc, T, diag::err_incomplete_typeid))
        return ExprError();

      // C++ [expr.typeid]p3:
      //   When typeid is applied to an expression other than an glvalue of a
      //   polymorphic class type [...] [the] expression is an unevaluated
      //   operand. [...]
      if (RecordD->isPolymorphic() && E->isGLValue()) {
        if (isUnevaluatedContext()) {
          // The operand was processed in unevaluated context, switch the
          // context and recheck the subexpression.
          ExprResult Result = TransformToPotentiallyEvaluated(E);
          if (Result.isInvalid())
            return ExprError();
          E = Result.get();
        }

        // We require a vtable to query the type at run time.
        MarkVTableUsed(TypeidLoc, RecordD);
        WasEvaluated = true;
      }
    }

    ExprResult Result = CheckUnevaluatedOperand(E);
    if (Result.isInvalid())
      return ExprError();
    E = Result.get();

    // C++ [expr.typeid]p4:
    //   [...] If the type of the type-id is a reference to a possibly
    //   cv-qualified type, the result of the typeid expression refers to a
    //   std::type_info object representing the cv-unqualified referenced
    //   type.
    Qualifiers Quals;
    QualType UnqualT = Context.getUnqualifiedArrayType(T, Quals);
    if (!Context.hasSameType(T, UnqualT)) {
      T = UnqualT;
      E = ImpCastExprToType(E, UnqualT, CK_NoOp, E->getValueKind()).get();
    }
  }

  if (E->getType()->isVariablyModifiedType())
    return ExprError(Diag(TypeidLoc, diag::err_variably_modified_typeid)
                     << E->getType());
  else if (!inTemplateInstantiation() &&
           E->HasSideEffects(Context, WasEvaluated)) {
    // The expression operand for typeid is in an unevaluated expression
    // context, so side effects could result in unintended consequences.
    Diag(E->getExprLoc(), WasEvaluated
                              ? diag::warn_side_effects_typeid
                              : diag::warn_side_effects_unevaluated_context);
  }

  return new (Context) CXXTypeidExpr(TypeInfoType.withConst(), E,
                                     SourceRange(TypeidLoc, RParenLoc));
}

/// ActOnCXXTypeidOfType - Parse typeid( type-id ) or typeid (expression);
ExprResult
Sema::ActOnCXXTypeid(SourceLocation OpLoc, SourceLocation LParenLoc,
                     bool isType, void *TyOrExpr, SourceLocation RParenLoc) {
  // typeid is not supported in OpenCL.
  if (getLangOpts().OpenCLCPlusPlus) {
    return ExprError(Diag(OpLoc, diag::err_openclcxx_not_supported)
                     << "typeid");
  }

  // Find the std::type_info type.
  if (!getStdNamespace())
    return ExprError(Diag(OpLoc, diag::err_need_header_before_typeid));

  if (!CXXTypeInfoDecl) {
    IdentifierInfo *TypeInfoII = &PP.getIdentifierTable().get("type_info");
    LookupResult R(*this, TypeInfoII, SourceLocation(), LookupTagName);
    LookupQualifiedName(R, getStdNamespace());
    CXXTypeInfoDecl = R.getAsSingle<RecordDecl>();
    // Microsoft's typeinfo doesn't have type_info in std but in the global
    // namespace if _HAS_EXCEPTIONS is defined to 0. See PR13153.
    if (!CXXTypeInfoDecl && LangOpts.MSVCCompat) {
      LookupQualifiedName(R, Context.getTranslationUnitDecl());
      CXXTypeInfoDecl = R.getAsSingle<RecordDecl>();
    }
    if (!CXXTypeInfoDecl)
      return ExprError(Diag(OpLoc, diag::err_need_header_before_typeid));
  }

  if (!getLangOpts().RTTI) {
    return ExprError(Diag(OpLoc, diag::err_no_typeid_with_fno_rtti));
  }

  QualType TypeInfoType = Context.getTypeDeclType(CXXTypeInfoDecl);

  if (isType) {
    // The operand is a type; handle it as such.
    TypeSourceInfo *TInfo = nullptr;
    QualType T = GetTypeFromParser(ParsedType::getFromOpaquePtr(TyOrExpr),
                                   &TInfo);
    if (T.isNull())
      return ExprError();

    if (!TInfo)
      TInfo = Context.getTrivialTypeSourceInfo(T, OpLoc);

    return BuildCXXTypeId(TypeInfoType, OpLoc, TInfo, RParenLoc);
  }

  // The operand is an expression.
  ExprResult Result =
      BuildCXXTypeId(TypeInfoType, OpLoc, (Expr *)TyOrExpr, RParenLoc);

  if (!getLangOpts().RTTIData && !Result.isInvalid())
    if (auto *CTE = dyn_cast<CXXTypeidExpr>(Result.get()))
      if (CTE->isPotentiallyEvaluated() && !CTE->isMostDerived(Context))
        Diag(OpLoc, diag::warn_no_typeid_with_rtti_disabled)
            << (getDiagnostics().getDiagnosticOptions().getFormat() ==
                DiagnosticOptions::MSVC);
  return Result;
}

/// Grabs __declspec(uuid()) off a type, or returns 0 if we cannot resolve to
/// a single GUID.
static void
getUuidAttrOfType(Sema &SemaRef, QualType QT,
                  llvm::SmallSetVector<const UuidAttr *, 1> &UuidAttrs) {
  // Optionally remove one level of pointer, reference or array indirection.
  const Type *Ty = QT.getTypePtr();
  if (QT->isPointerType() || QT->isReferenceType())
    Ty = QT->getPointeeType().getTypePtr();
  else if (QT->isArrayType())
    Ty = Ty->getBaseElementTypeUnsafe();

  const auto *TD = Ty->getAsTagDecl();
  if (!TD)
    return;

  if (const auto *Uuid = TD->getMostRecentDecl()->getAttr<UuidAttr>()) {
    UuidAttrs.insert(Uuid);
    return;
  }

  // __uuidof can grab UUIDs from template arguments.
  if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(TD)) {
    const TemplateArgumentList &TAL = CTSD->getTemplateArgs();
    for (const TemplateArgument &TA : TAL.asArray()) {
      const UuidAttr *UuidForTA = nullptr;
      if (TA.getKind() == TemplateArgument::Type)
        getUuidAttrOfType(SemaRef, TA.getAsType(), UuidAttrs);
      else if (TA.getKind() == TemplateArgument::Declaration)
        getUuidAttrOfType(SemaRef, TA.getAsDecl()->getType(), UuidAttrs);

      if (UuidForTA)
        UuidAttrs.insert(UuidForTA);
    }
  }
}

ExprResult Sema::BuildCXXUuidof(QualType Type,
                                SourceLocation TypeidLoc,
                                TypeSourceInfo *Operand,
                                SourceLocation RParenLoc) {
  MSGuidDecl *Guid = nullptr;
  if (!Operand->getType()->isDependentType()) {
    llvm::SmallSetVector<const UuidAttr *, 1> UuidAttrs;
    getUuidAttrOfType(*this, Operand->getType(), UuidAttrs);
    if (UuidAttrs.empty())
      return ExprError(Diag(TypeidLoc, diag::err_uuidof_without_guid));
    if (UuidAttrs.size() > 1)
      return ExprError(Diag(TypeidLoc, diag::err_uuidof_with_multiple_guids));
    Guid = UuidAttrs.back()->getGuidDecl();
  }

  return new (Context)
      CXXUuidofExpr(Type, Operand, Guid, SourceRange(TypeidLoc, RParenLoc));
}

ExprResult Sema::BuildCXXUuidof(QualType Type, SourceLocation TypeidLoc,
                                Expr *E, SourceLocation RParenLoc) {
  MSGuidDecl *Guid = nullptr;
  if (!E->getType()->isDependentType()) {
    if (E->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNull)) {
      // A null pointer results in {00000000-0000-0000-0000-000000000000}.
      Guid = Context.getMSGuidDecl(MSGuidDecl::Parts{});
    } else {
      llvm::SmallSetVector<const UuidAttr *, 1> UuidAttrs;
      getUuidAttrOfType(*this, E->getType(), UuidAttrs);
      if (UuidAttrs.empty())
        return ExprError(Diag(TypeidLoc, diag::err_uuidof_without_guid));
      if (UuidAttrs.size() > 1)
        return ExprError(Diag(TypeidLoc, diag::err_uuidof_with_multiple_guids));
      Guid = UuidAttrs.back()->getGuidDecl();
    }
  }

  return new (Context)
      CXXUuidofExpr(Type, E, Guid, SourceRange(TypeidLoc, RParenLoc));
}

/// ActOnCXXUuidof - Parse __uuidof( type-id ) or __uuidof (expression);
ExprResult
Sema::ActOnCXXUuidof(SourceLocation OpLoc, SourceLocation LParenLoc,
                     bool isType, void *TyOrExpr, SourceLocation RParenLoc) {
  QualType GuidType = Context.getMSGuidType();
  GuidType.addConst();

  if (isType) {
    // The operand is a type; handle it as such.
    TypeSourceInfo *TInfo = nullptr;
    QualType T = GetTypeFromParser(ParsedType::getFromOpaquePtr(TyOrExpr),
                                   &TInfo);
    if (T.isNull())
      return ExprError();

    if (!TInfo)
      TInfo = Context.getTrivialTypeSourceInfo(T, OpLoc);

    return BuildCXXUuidof(GuidType, OpLoc, TInfo, RParenLoc);
  }

  // The operand is an expression.
  return BuildCXXUuidof(GuidType, OpLoc, (Expr*)TyOrExpr, RParenLoc);
}

ExprResult
Sema::ActOnCXXBoolLiteral(SourceLocation OpLoc, tok::TokenKind Kind) {
  assert((Kind == tok::kw_true || Kind == tok::kw_false) &&
         "Unknown C++ Boolean value!");
  return new (Context)
      CXXBoolLiteralExpr(Kind == tok::kw_true, Context.BoolTy, OpLoc);
}

ExprResult
Sema::ActOnCXXNullPtrLiteral(SourceLocation Loc) {
  return new (Context) CXXNullPtrLiteralExpr(Context.NullPtrTy, Loc);
}

ExprResult
Sema::ActOnCXXThrow(Scope *S, SourceLocation OpLoc, Expr *Ex) {
  bool IsThrownVarInScope = false;
  if (Ex) {
    // C++0x [class.copymove]p31:
    //   When certain criteria are met, an implementation is allowed to omit the
    //   copy/move construction of a class object [...]
    //
    //     - in a throw-expression, when the operand is the name of a
    //       non-volatile automatic object (other than a function or catch-
    //       clause parameter) whose scope does not extend beyond the end of the
    //       innermost enclosing try-block (if there is one), the copy/move
    //       operation from the operand to the exception object (15.1) can be
    //       omitted by constructing the automatic object directly into the
    //       exception object
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Ex->IgnoreParens()))
      if (const auto *Var = dyn_cast<VarDecl>(DRE->getDecl());
          Var && Var->hasLocalStorage() &&
          !Var->getType().isVolatileQualified()) {
        for (; S; S = S->getParent()) {
          if (S->isDeclScope(Var)) {
            IsThrownVarInScope = true;
            break;
          }

          // FIXME: Many of the scope checks here seem incorrect.
          if (S->getFlags() &
              (Scope::FnScope | Scope::ClassScope | Scope::BlockScope |
               Scope::ObjCMethodScope | Scope::TryScope))
            break;
        }
      }
  }

  return BuildCXXThrow(OpLoc, Ex, IsThrownVarInScope);
}

ExprResult Sema::BuildCXXThrow(SourceLocation OpLoc, Expr *Ex,
                               bool IsThrownVarInScope) {
  const llvm::Triple &T = Context.getTargetInfo().getTriple();
  const bool IsOpenMPGPUTarget =
      getLangOpts().OpenMPIsTargetDevice && (T.isNVPTX() || T.isAMDGCN());
  // Don't report an error if 'throw' is used in system headers or in an OpenMP
  // target region compiled for a GPU architecture.
  if (!IsOpenMPGPUTarget && !getLangOpts().CXXExceptions &&
      !getSourceManager().isInSystemHeader(OpLoc) && !getLangOpts().CUDA) {
    // Delay error emission for the OpenMP device code.
    targetDiag(OpLoc, diag::err_exceptions_disabled) << "throw";
  }

  // In OpenMP target regions, we replace 'throw' with a trap on GPU targets.
  if (IsOpenMPGPUTarget)
    targetDiag(OpLoc, diag::warn_throw_not_valid_on_target) << T.str();

  // Exceptions aren't allowed in CUDA device code.
  if (getLangOpts().CUDA)
    CUDA().DiagIfDeviceCode(OpLoc, diag::err_cuda_device_exceptions)
        << "throw" << llvm::to_underlying(CUDA().CurrentTarget());

  if (getCurScope() && getCurScope()->isOpenMPSimdDirectiveScope())
    Diag(OpLoc, diag::err_omp_simd_region_cannot_use_stmt) << "throw";

  // Exceptions that escape a compute construct are ill-formed.
  if (getLangOpts().OpenACC && getCurScope() &&
      getCurScope()->isInOpenACCComputeConstructScope(Scope::TryScope))
    Diag(OpLoc, diag::err_acc_branch_in_out_compute_construct)
        << /*throw*/ 2 << /*out of*/ 0;

  if (Ex && !Ex->isTypeDependent()) {
    // Initialize the exception result.  This implicitly weeds out
    // abstract types or types with inaccessible copy constructors.

    // C++0x [class.copymove]p31:
    //   When certain criteria are met, an implementation is allowed to omit the
    //   copy/move construction of a class object [...]
    //
    //     - in a throw-expression, when the operand is the name of a
    //       non-volatile automatic object (other than a function or
    //       catch-clause
    //       parameter) whose scope does not extend beyond the end of the
    //       innermost enclosing try-block (if there is one), the copy/move
    //       operation from the operand to the exception object (15.1) can be
    //       omitted by constructing the automatic object directly into the
    //       exception object
    NamedReturnInfo NRInfo =
        IsThrownVarInScope ? getNamedReturnInfo(Ex) : NamedReturnInfo();

    QualType ExceptionObjectTy = Context.getExceptionObjectType(Ex->getType());
    if (CheckCXXThrowOperand(OpLoc, ExceptionObjectTy, Ex))
      return ExprError();

    InitializedEntity Entity =
        InitializedEntity::InitializeException(OpLoc, ExceptionObjectTy);
    ExprResult Res = PerformMoveOrCopyInitialization(Entity, NRInfo, Ex);
    if (Res.isInvalid())
      return ExprError();
    Ex = Res.get();
  }

  // PPC MMA non-pointer types are not allowed as throw expr types.
  if (Ex && Context.getTargetInfo().getTriple().isPPC64())
    PPC().CheckPPCMMAType(Ex->getType(), Ex->getBeginLoc());

  return new (Context)
      CXXThrowExpr(Ex, Context.VoidTy, OpLoc, IsThrownVarInScope);
}

static void
collectPublicBases(CXXRecordDecl *RD,
                   llvm::DenseMap<CXXRecordDecl *, unsigned> &SubobjectsSeen,
                   llvm::SmallPtrSetImpl<CXXRecordDecl *> &VBases,
                   llvm::SetVector<CXXRecordDecl *> &PublicSubobjectsSeen,
                   bool ParentIsPublic) {
  for (const CXXBaseSpecifier &BS : RD->bases()) {
    CXXRecordDecl *BaseDecl = BS.getType()->getAsCXXRecordDecl();
    bool NewSubobject;
    // Virtual bases constitute the same subobject.  Non-virtual bases are
    // always distinct subobjects.
    if (BS.isVirtual())
      NewSubobject = VBases.insert(BaseDecl).second;
    else
      NewSubobject = true;

    if (NewSubobject)
      ++SubobjectsSeen[BaseDecl];

    // Only add subobjects which have public access throughout the entire chain.
    bool PublicPath = ParentIsPublic && BS.getAccessSpecifier() == AS_public;
    if (PublicPath)
      PublicSubobjectsSeen.insert(BaseDecl);

    // Recurse on to each base subobject.
    collectPublicBases(BaseDecl, SubobjectsSeen, VBases, PublicSubobjectsSeen,
                       PublicPath);
  }
}

static void getUnambiguousPublicSubobjects(
    CXXRecordDecl *RD, llvm::SmallVectorImpl<CXXRecordDecl *> &Objects) {
  llvm::DenseMap<CXXRecordDecl *, unsigned> SubobjectsSeen;
  llvm::SmallSet<CXXRecordDecl *, 2> VBases;
  llvm::SetVector<CXXRecordDecl *> PublicSubobjectsSeen;
  SubobjectsSeen[RD] = 1;
  PublicSubobjectsSeen.insert(RD);
  collectPublicBases(RD, SubobjectsSeen, VBases, PublicSubobjectsSeen,
                     /*ParentIsPublic=*/true);

  for (CXXRecordDecl *PublicSubobject : PublicSubobjectsSeen) {
    // Skip ambiguous objects.
    if (SubobjectsSeen[PublicSubobject] > 1)
      continue;

    Objects.push_back(PublicSubobject);
  }
}

bool Sema::CheckCXXThrowOperand(SourceLocation ThrowLoc,
                                QualType ExceptionObjectTy, Expr *E) {
  //   If the type of the exception would be an incomplete type or a pointer
  //   to an incomplete type other than (cv) void the program is ill-formed.
  QualType Ty = ExceptionObjectTy;
  bool isPointer = false;
  if (const PointerType* Ptr = Ty->getAs<PointerType>()) {
    Ty = Ptr->getPointeeType();
    isPointer = true;
  }

  // Cannot throw WebAssembly reference type.
  if (Ty.isWebAssemblyReferenceType()) {
    Diag(ThrowLoc, diag::err_wasm_reftype_tc) << 0 << E->getSourceRange();
    return true;
  }

  // Cannot throw WebAssembly table.
  if (isPointer && Ty.isWebAssemblyReferenceType()) {
    Diag(ThrowLoc, diag::err_wasm_table_art) << 2 << E->getSourceRange();
    return true;
  }

  if (!isPointer || !Ty->isVoidType()) {
    if (RequireCompleteType(ThrowLoc, Ty,
                            isPointer ? diag::err_throw_incomplete_ptr
                                      : diag::err_throw_incomplete,
                            E->getSourceRange()))
      return true;

    if (!isPointer && Ty->isSizelessType()) {
      Diag(ThrowLoc, diag::err_throw_sizeless) << Ty << E->getSourceRange();
      return true;
    }

    if (RequireNonAbstractType(ThrowLoc, ExceptionObjectTy,
                               diag::err_throw_abstract_type, E))
      return true;
  }

  // If the exception has class type, we need additional handling.
  CXXRecordDecl *RD = Ty->getAsCXXRecordDecl();
  if (!RD)
    return false;

  // If we are throwing a polymorphic class type or pointer thereof,
  // exception handling will make use of the vtable.
  MarkVTableUsed(ThrowLoc, RD);

  // If a pointer is thrown, the referenced object will not be destroyed.
  if (isPointer)
    return false;

  // If the class has a destructor, we must be able to call it.
  if (!RD->hasIrrelevantDestructor()) {
    if (CXXDestructorDecl *Destructor = LookupDestructor(RD)) {
      MarkFunctionReferenced(E->getExprLoc(), Destructor);
      CheckDestructorAccess(E->getExprLoc(), Destructor,
                            PDiag(diag::err_access_dtor_exception) << Ty);
      if (DiagnoseUseOfDecl(Destructor, E->getExprLoc()))
        return true;
    }
  }

  // The MSVC ABI creates a list of all types which can catch the exception
  // object.  This list also references the appropriate copy constructor to call
  // if the object is caught by value and has a non-trivial copy constructor.
  if (Context.getTargetInfo().getCXXABI().isMicrosoft()) {
    // We are only interested in the public, unambiguous bases contained within
    // the exception object.  Bases which are ambiguous or otherwise
    // inaccessible are not catchable types.
    llvm::SmallVector<CXXRecordDecl *, 2> UnambiguousPublicSubobjects;
    getUnambiguousPublicSubobjects(RD, UnambiguousPublicSubobjects);

    for (CXXRecordDecl *Subobject : UnambiguousPublicSubobjects) {
      // Attempt to lookup the copy constructor.  Various pieces of machinery
      // will spring into action, like template instantiation, which means this
      // cannot be a simple walk of the class's decls.  Instead, we must perform
      // lookup and overload resolution.
      CXXConstructorDecl *CD = LookupCopyingConstructor(Subobject, 0);
      if (!CD || CD->isDeleted())
        continue;

      // Mark the constructor referenced as it is used by this throw expression.
      MarkFunctionReferenced(E->getExprLoc(), CD);

      // Skip this copy constructor if it is trivial, we don't need to record it
      // in the catchable type data.
      if (CD->isTrivial())
        continue;

      // The copy constructor is non-trivial, create a mapping from this class
      // type to this constructor.
      // N.B.  The selection of copy constructor is not sensitive to this
      // particular throw-site.  Lookup will be performed at the catch-site to
      // ensure that the copy constructor is, in fact, accessible (via
      // friendship or any other means).
      Context.addCopyConstructorForExceptionObject(Subobject, CD);

      // We don't keep the instantiated default argument expressions around so
      // we must rebuild them here.
      for (unsigned I = 1, E = CD->getNumParams(); I != E; ++I) {
        if (CheckCXXDefaultArgExpr(ThrowLoc, CD, CD->getParamDecl(I)))
          return true;
      }
    }
  }

  // Under the Itanium C++ ABI, memory for the exception object is allocated by
  // the runtime with no ability for the compiler to request additional
  // alignment. Warn if the exception type requires alignment beyond the minimum
  // guaranteed by the target C++ runtime.
  if (Context.getTargetInfo().getCXXABI().isItaniumFamily()) {
    CharUnits TypeAlign = Context.getTypeAlignInChars(Ty);
    CharUnits ExnObjAlign = Context.getExnObjectAlignment();
    if (ExnObjAlign < TypeAlign) {
      Diag(ThrowLoc, diag::warn_throw_underaligned_obj);
      Diag(ThrowLoc, diag::note_throw_underaligned_obj)
          << Ty << (unsigned)TypeAlign.getQuantity()
          << (unsigned)ExnObjAlign.getQuantity();
    }
  }
  if (!isPointer && getLangOpts().AssumeNothrowExceptionDtor) {
    if (CXXDestructorDecl *Dtor = RD->getDestructor()) {
      auto Ty = Dtor->getType();
      if (auto *FT = Ty.getTypePtr()->getAs<FunctionProtoType>()) {
        if (!isUnresolvedExceptionSpec(FT->getExceptionSpecType()) &&
            !FT->isNothrow())
          Diag(ThrowLoc, diag::err_throw_object_throwing_dtor) << RD;
      }
    }
  }

  return false;
}

static QualType adjustCVQualifiersForCXXThisWithinLambda(
    ArrayRef<FunctionScopeInfo *> FunctionScopes, QualType ThisTy,
    DeclContext *CurSemaContext, ASTContext &ASTCtx) {

  QualType ClassType = ThisTy->getPointeeType();
  LambdaScopeInfo *CurLSI = nullptr;
  DeclContext *CurDC = CurSemaContext;

  // Iterate through the stack of lambdas starting from the innermost lambda to
  // the outermost lambda, checking if '*this' is ever captured by copy - since
  // that could change the cv-qualifiers of the '*this' object.
  // The object referred to by '*this' starts out with the cv-qualifiers of its
  // member function.  We then start with the innermost lambda and iterate
  // outward checking to see if any lambda performs a by-copy capture of '*this'
  // - and if so, any nested lambda must respect the 'constness' of that
  // capturing lamdbda's call operator.
  //

  // Since the FunctionScopeInfo stack is representative of the lexical
  // nesting of the lambda expressions during initial parsing (and is the best
  // place for querying information about captures about lambdas that are
  // partially processed) and perhaps during instantiation of function templates
  // that contain lambda expressions that need to be transformed BUT not
  // necessarily during instantiation of a nested generic lambda's function call
  // operator (which might even be instantiated at the end of the TU) - at which
  // time the DeclContext tree is mature enough to query capture information
  // reliably - we use a two pronged approach to walk through all the lexically
  // enclosing lambda expressions:
  //
  //  1) Climb down the FunctionScopeInfo stack as long as each item represents
  //  a Lambda (i.e. LambdaScopeInfo) AND each LSI's 'closure-type' is lexically
  //  enclosed by the call-operator of the LSI below it on the stack (while
  //  tracking the enclosing DC for step 2 if needed).  Note the topmost LSI on
  //  the stack represents the innermost lambda.
  //
  //  2) If we run out of enclosing LSI's, check if the enclosing DeclContext
  //  represents a lambda's call operator.  If it does, we must be instantiating
  //  a generic lambda's call operator (represented by the Current LSI, and
  //  should be the only scenario where an inconsistency between the LSI and the
  //  DeclContext should occur), so climb out the DeclContexts if they
  //  represent lambdas, while querying the corresponding closure types
  //  regarding capture information.

  // 1) Climb down the function scope info stack.
  for (int I = FunctionScopes.size();
       I-- && isa<LambdaScopeInfo>(FunctionScopes[I]) &&
       (!CurLSI || !CurLSI->Lambda || CurLSI->Lambda->getDeclContext() ==
                       cast<LambdaScopeInfo>(FunctionScopes[I])->CallOperator);
       CurDC = getLambdaAwareParentOfDeclContext(CurDC)) {
    CurLSI = cast<LambdaScopeInfo>(FunctionScopes[I]);

    if (!CurLSI->isCXXThisCaptured())
        continue;

    auto C = CurLSI->getCXXThisCapture();

    if (C.isCopyCapture()) {
      if (CurLSI->lambdaCaptureShouldBeConst())
        ClassType.addConst();
      return ASTCtx.getPointerType(ClassType);
    }
  }

  // 2) We've run out of ScopeInfos but check 1. if CurDC is a lambda (which
  //    can happen during instantiation of its nested generic lambda call
  //    operator); 2. if we're in a lambda scope (lambda body).
  if (CurLSI && isLambdaCallOperator(CurDC)) {
    assert(isGenericLambdaCallOperatorSpecialization(CurLSI->CallOperator) &&
           "While computing 'this' capture-type for a generic lambda, when we "
           "run out of enclosing LSI's, yet the enclosing DC is a "
           "lambda-call-operator we must be (i.e. Current LSI) in a generic "
           "lambda call oeprator");
    assert(CurDC == getLambdaAwareParentOfDeclContext(CurLSI->CallOperator));

    auto IsThisCaptured =
        [](CXXRecordDecl *Closure, bool &IsByCopy, bool &IsConst) {
      IsConst = false;
      IsByCopy = false;
      for (auto &&C : Closure->captures()) {
        if (C.capturesThis()) {
          if (C.getCaptureKind() == LCK_StarThis)
            IsByCopy = true;
          if (Closure->getLambdaCallOperator()->isConst())
            IsConst = true;
          return true;
        }
      }
      return false;
    };

    bool IsByCopyCapture = false;
    bool IsConstCapture = false;
    CXXRecordDecl *Closure = cast<CXXRecordDecl>(CurDC->getParent());
    while (Closure &&
           IsThisCaptured(Closure, IsByCopyCapture, IsConstCapture)) {
      if (IsByCopyCapture) {
        if (IsConstCapture)
          ClassType.addConst();
        return ASTCtx.getPointerType(ClassType);
      }
      Closure = isLambdaCallOperator(Closure->getParent())
                    ? cast<CXXRecordDecl>(Closure->getParent()->getParent())
                    : nullptr;
    }
  }
  return ThisTy;
}

QualType Sema::getCurrentThisType() {
  DeclContext *DC = getFunctionLevelDeclContext();
  QualType ThisTy = CXXThisTypeOverride;

  if (CXXMethodDecl *method = dyn_cast<CXXMethodDecl>(DC)) {
    if (method && method->isImplicitObjectMemberFunction())
      ThisTy = method->getThisType().getNonReferenceType();
  }

  if (ThisTy.isNull() && isLambdaCallWithImplicitObjectParameter(CurContext) &&
      inTemplateInstantiation() && isa<CXXRecordDecl>(DC)) {

    // This is a lambda call operator that is being instantiated as a default
    // initializer. DC must point to the enclosing class type, so we can recover
    // the 'this' type from it.
    QualType ClassTy = Context.getTypeDeclType(cast<CXXRecordDecl>(DC));
    // There are no cv-qualifiers for 'this' within default initializers,
    // per [expr.prim.general]p4.
    ThisTy = Context.getPointerType(ClassTy);
  }

  // If we are within a lambda's call operator, the cv-qualifiers of 'this'
  // might need to be adjusted if the lambda or any of its enclosing lambda's
  // captures '*this' by copy.
  if (!ThisTy.isNull() && isLambdaCallOperator(CurContext))
    return adjustCVQualifiersForCXXThisWithinLambda(FunctionScopes, ThisTy,
                                                    CurContext, Context);
  return ThisTy;
}

Sema::CXXThisScopeRAII::CXXThisScopeRAII(Sema &S,
                                         Decl *ContextDecl,
                                         Qualifiers CXXThisTypeQuals,
                                         bool Enabled)
  : S(S), OldCXXThisTypeOverride(S.CXXThisTypeOverride), Enabled(false)
{
  if (!Enabled || !ContextDecl)
    return;

  CXXRecordDecl *Record = nullptr;
  if (ClassTemplateDecl *Template = dyn_cast<ClassTemplateDecl>(ContextDecl))
    Record = Template->getTemplatedDecl();
  else
    Record = cast<CXXRecordDecl>(ContextDecl);

  QualType T = S.Context.getRecordType(Record);
  T = S.getASTContext().getQualifiedType(T, CXXThisTypeQuals);

  S.CXXThisTypeOverride =
      S.Context.getLangOpts().HLSL ? T : S.Context.getPointerType(T);

  this->Enabled = true;
}


Sema::CXXThisScopeRAII::~CXXThisScopeRAII() {
  if (Enabled) {
    S.CXXThisTypeOverride = OldCXXThisTypeOverride;
  }
}

static void buildLambdaThisCaptureFixit(Sema &Sema, LambdaScopeInfo *LSI) {
  SourceLocation DiagLoc = LSI->IntroducerRange.getEnd();
  assert(!LSI->isCXXThisCaptured());
  //  [=, this] {};   // until C++20: Error: this when = is the default
  if (LSI->ImpCaptureStyle == CapturingScopeInfo::ImpCap_LambdaByval &&
      !Sema.getLangOpts().CPlusPlus20)
    return;
  Sema.Diag(DiagLoc, diag::note_lambda_this_capture_fixit)
      << FixItHint::CreateInsertion(
             DiagLoc, LSI->NumExplicitCaptures > 0 ? ", this" : "this");
}

bool Sema::CheckCXXThisCapture(SourceLocation Loc, const bool Explicit,
    bool BuildAndDiagnose, const unsigned *const FunctionScopeIndexToStopAt,
    const bool ByCopy) {
  // We don't need to capture this in an unevaluated context.
  if (isUnevaluatedContext() && !Explicit)
    return true;

  assert((!ByCopy || Explicit) && "cannot implicitly capture *this by value");

  const int MaxFunctionScopesIndex = FunctionScopeIndexToStopAt
                                         ? *FunctionScopeIndexToStopAt
                                         : FunctionScopes.size() - 1;

  // Check that we can capture the *enclosing object* (referred to by '*this')
  // by the capturing-entity/closure (lambda/block/etc) at
  // MaxFunctionScopesIndex-deep on the FunctionScopes stack.

  // Note: The *enclosing object* can only be captured by-value by a
  // closure that is a lambda, using the explicit notation:
  //    [*this] { ... }.
  // Every other capture of the *enclosing object* results in its by-reference
  // capture.

  // For a closure 'L' (at MaxFunctionScopesIndex in the FunctionScopes
  // stack), we can capture the *enclosing object* only if:
  // - 'L' has an explicit byref or byval capture of the *enclosing object*
  // -  or, 'L' has an implicit capture.
  // AND
  //   -- there is no enclosing closure
  //   -- or, there is some enclosing closure 'E' that has already captured the
  //      *enclosing object*, and every intervening closure (if any) between 'E'
  //      and 'L' can implicitly capture the *enclosing object*.
  //   -- or, every enclosing closure can implicitly capture the
  //      *enclosing object*


  unsigned NumCapturingClosures = 0;
  for (int idx = MaxFunctionScopesIndex; idx >= 0; idx--) {
    if (CapturingScopeInfo *CSI =
            dyn_cast<CapturingScopeInfo>(FunctionScopes[idx])) {
      if (CSI->CXXThisCaptureIndex != 0) {
        // 'this' is already being captured; there isn't anything more to do.
        CSI->Captures[CSI->CXXThisCaptureIndex - 1].markUsed(BuildAndDiagnose);
        break;
      }
      LambdaScopeInfo *LSI = dyn_cast<LambdaScopeInfo>(CSI);
      if (LSI && isGenericLambdaCallOperatorSpecialization(LSI->CallOperator)) {
        // This context can't implicitly capture 'this'; fail out.
        if (BuildAndDiagnose) {
          LSI->CallOperator->setInvalidDecl();
          Diag(Loc, diag::err_this_capture)
              << (Explicit && idx == MaxFunctionScopesIndex);
          if (!Explicit)
            buildLambdaThisCaptureFixit(*this, LSI);
        }
        return true;
      }
      if (CSI->ImpCaptureStyle == CapturingScopeInfo::ImpCap_LambdaByref ||
          CSI->ImpCaptureStyle == CapturingScopeInfo::ImpCap_LambdaByval ||
          CSI->ImpCaptureStyle == CapturingScopeInfo::ImpCap_Block ||
          CSI->ImpCaptureStyle == CapturingScopeInfo::ImpCap_CapturedRegion ||
          (Explicit && idx == MaxFunctionScopesIndex)) {
        // Regarding (Explicit && idx == MaxFunctionScopesIndex): only the first
        // iteration through can be an explicit capture, all enclosing closures,
        // if any, must perform implicit captures.

        // This closure can capture 'this'; continue looking upwards.
        NumCapturingClosures++;
        continue;
      }
      // This context can't implicitly capture 'this'; fail out.
      if (BuildAndDiagnose) {
        LSI->CallOperator->setInvalidDecl();
        Diag(Loc, diag::err_this_capture)
            << (Explicit && idx == MaxFunctionScopesIndex);
      }
      if (!Explicit)
        buildLambdaThisCaptureFixit(*this, LSI);
      return true;
    }
    break;
  }
  if (!BuildAndDiagnose) return false;

  // If we got here, then the closure at MaxFunctionScopesIndex on the
  // FunctionScopes stack, can capture the *enclosing object*, so capture it
  // (including implicit by-reference captures in any enclosing closures).

  // In the loop below, respect the ByCopy flag only for the closure requesting
  // the capture (i.e. first iteration through the loop below).  Ignore it for
  // all enclosing closure's up to NumCapturingClosures (since they must be
  // implicitly capturing the *enclosing  object* by reference (see loop
  // above)).
  assert((!ByCopy ||
          isa<LambdaScopeInfo>(FunctionScopes[MaxFunctionScopesIndex])) &&
         "Only a lambda can capture the enclosing object (referred to by "
         "*this) by copy");
  QualType ThisTy = getCurrentThisType();
  for (int idx = MaxFunctionScopesIndex; NumCapturingClosures;
       --idx, --NumCapturingClosures) {
    CapturingScopeInfo *CSI = cast<CapturingScopeInfo>(FunctionScopes[idx]);

    // The type of the corresponding data member (not a 'this' pointer if 'by
    // copy').
    QualType CaptureType = ByCopy ? ThisTy->getPointeeType() : ThisTy;

    bool isNested = NumCapturingClosures > 1;
    CSI->addThisCapture(isNested, Loc, CaptureType, ByCopy);
  }
  return false;
}

ExprResult Sema::ActOnCXXThis(SourceLocation Loc) {
  // C++20 [expr.prim.this]p1:
  //   The keyword this names a pointer to the object for which an
  //   implicit object member function is invoked or a non-static
  //   data member's initializer is evaluated.
  QualType ThisTy = getCurrentThisType();

  if (CheckCXXThisType(Loc, ThisTy))
    return ExprError();

  return BuildCXXThisExpr(Loc, ThisTy, /*IsImplicit=*/false);
}

bool Sema::CheckCXXThisType(SourceLocation Loc, QualType Type) {
  if (!Type.isNull())
    return false;

  // C++20 [expr.prim.this]p3:
  //   If a declaration declares a member function or member function template
  //   of a class X, the expression this is a prvalue of type
  //   "pointer to cv-qualifier-seq X" wherever X is the current class between
  //   the optional cv-qualifier-seq and the end of the function-definition,
  //   member-declarator, or declarator. It shall not appear within the
  //   declaration of either a static member function or an explicit object
  //   member function of the current class (although its type and value
  //   category are defined within such member functions as they are within
  //   an implicit object member function).
  DeclContext *DC = getFunctionLevelDeclContext();
  const auto *Method = dyn_cast<CXXMethodDecl>(DC);
  if (Method && Method->isExplicitObjectMemberFunction()) {
    Diag(Loc, diag::err_invalid_this_use) << 1;
  } else if (Method && isLambdaCallWithExplicitObjectParameter(CurContext)) {
    Diag(Loc, diag::err_invalid_this_use) << 1;
  } else {
    Diag(Loc, diag::err_invalid_this_use) << 0;
  }
  return true;
}

Expr *Sema::BuildCXXThisExpr(SourceLocation Loc, QualType Type,
                             bool IsImplicit) {
  auto *This = CXXThisExpr::Create(Context, Loc, Type, IsImplicit);
  MarkThisReferenced(This);
  return This;
}

void Sema::MarkThisReferenced(CXXThisExpr *This) {
  CheckCXXThisCapture(This->getExprLoc());
  if (This->isTypeDependent())
    return;

  // Check if 'this' is captured by value in a lambda with a dependent explicit
  // object parameter, and mark it as type-dependent as well if so.
  auto IsDependent = [&]() {
    for (auto *Scope : llvm::reverse(FunctionScopes)) {
      auto *LSI = dyn_cast<sema::LambdaScopeInfo>(Scope);
      if (!LSI)
        continue;

      if (LSI->Lambda && !LSI->Lambda->Encloses(CurContext) &&
          LSI->AfterParameterList)
        return false;

      // If this lambda captures 'this' by value, then 'this' is dependent iff
      // this lambda has a dependent explicit object parameter. If we can't
      // determine whether it does (e.g. because the CXXMethodDecl's type is
      // null), assume it doesn't.
      if (LSI->isCXXThisCaptured()) {
        if (!LSI->getCXXThisCapture().isCopyCapture())
          continue;

        const auto *MD = LSI->CallOperator;
        if (MD->getType().isNull())
          return false;

        const auto *Ty = MD->getType()->getAs<FunctionProtoType>();
        return Ty && MD->isExplicitObjectMemberFunction() &&
               Ty->getParamType(0)->isDependentType();
      }
    }
    return false;
  }();

  This->setCapturedByCopyInLambdaWithExplicitObjectParameter(IsDependent);
}

bool Sema::isThisOutsideMemberFunctionBody(QualType BaseType) {
  // If we're outside the body of a member function, then we'll have a specified
  // type for 'this'.
  if (CXXThisTypeOverride.isNull())
    return false;

  // Determine whether we're looking into a class that's currently being
  // defined.
  CXXRecordDecl *Class = BaseType->getAsCXXRecordDecl();
  return Class && Class->isBeingDefined();
}

ExprResult
Sema::ActOnCXXTypeConstructExpr(ParsedType TypeRep,
                                SourceLocation LParenOrBraceLoc,
                                MultiExprArg exprs,
                                SourceLocation RParenOrBraceLoc,
                                bool ListInitialization) {
  if (!TypeRep)
    return ExprError();

  TypeSourceInfo *TInfo;
  QualType Ty = GetTypeFromParser(TypeRep, &TInfo);
  if (!TInfo)
    TInfo = Context.getTrivialTypeSourceInfo(Ty, SourceLocation());

  auto Result = BuildCXXTypeConstructExpr(TInfo, LParenOrBraceLoc, exprs,
                                          RParenOrBraceLoc, ListInitialization);
  // Avoid creating a non-type-dependent expression that contains typos.
  // Non-type-dependent expressions are liable to be discarded without
  // checking for embedded typos.
  if (!Result.isInvalid() && Result.get()->isInstantiationDependent() &&
      !Result.get()->isTypeDependent())
    Result = CorrectDelayedTyposInExpr(Result.get());
  else if (Result.isInvalid())
    Result = CreateRecoveryExpr(TInfo->getTypeLoc().getBeginLoc(),
                                RParenOrBraceLoc, exprs, Ty);
  return Result;
}

ExprResult
Sema::BuildCXXTypeConstructExpr(TypeSourceInfo *TInfo,
                                SourceLocation LParenOrBraceLoc,
                                MultiExprArg Exprs,
                                SourceLocation RParenOrBraceLoc,
                                bool ListInitialization) {
  QualType Ty = TInfo->getType();
  SourceLocation TyBeginLoc = TInfo->getTypeLoc().getBeginLoc();

  assert((!ListInitialization || Exprs.size() == 1) &&
         "List initialization must have exactly one expression.");
  SourceRange FullRange = SourceRange(TyBeginLoc, RParenOrBraceLoc);

  InitializedEntity Entity =
      InitializedEntity::InitializeTemporary(Context, TInfo);
  InitializationKind Kind =
      Exprs.size()
          ? ListInitialization
                ? InitializationKind::CreateDirectList(
                      TyBeginLoc, LParenOrBraceLoc, RParenOrBraceLoc)
                : InitializationKind::CreateDirect(TyBeginLoc, LParenOrBraceLoc,
                                                   RParenOrBraceLoc)
          : InitializationKind::CreateValue(TyBeginLoc, LParenOrBraceLoc,
                                            RParenOrBraceLoc);

  // C++17 [expr.type.conv]p1:
  //   If the type is a placeholder for a deduced class type, [...perform class
  //   template argument deduction...]
  // C++23:
  //   Otherwise, if the type contains a placeholder type, it is replaced by the
  //   type determined by placeholder type deduction.
  DeducedType *Deduced = Ty->getContainedDeducedType();
  if (Deduced && !Deduced->isDeduced() &&
      isa<DeducedTemplateSpecializationType>(Deduced)) {
    Ty = DeduceTemplateSpecializationFromInitializer(TInfo, Entity,
                                                     Kind, Exprs);
    if (Ty.isNull())
      return ExprError();
    Entity = InitializedEntity::InitializeTemporary(TInfo, Ty);
  } else if (Deduced && !Deduced->isDeduced()) {
    MultiExprArg Inits = Exprs;
    if (ListInitialization) {
      auto *ILE = cast<InitListExpr>(Exprs[0]);
      Inits = MultiExprArg(ILE->getInits(), ILE->getNumInits());
    }

    if (Inits.empty())
      return ExprError(Diag(TyBeginLoc, diag::err_auto_expr_init_no_expression)
                       << Ty << FullRange);
    if (Inits.size() > 1) {
      Expr *FirstBad = Inits[1];
      return ExprError(Diag(FirstBad->getBeginLoc(),
                            diag::err_auto_expr_init_multiple_expressions)
                       << Ty << FullRange);
    }
    if (getLangOpts().CPlusPlus23) {
      if (Ty->getAs<AutoType>())
        Diag(TyBeginLoc, diag::warn_cxx20_compat_auto_expr) << FullRange;
    }
    Expr *Deduce = Inits[0];
    if (isa<InitListExpr>(Deduce))
      return ExprError(
          Diag(Deduce->getBeginLoc(), diag::err_auto_expr_init_paren_braces)
          << ListInitialization << Ty << FullRange);
    QualType DeducedType;
    TemplateDeductionInfo Info(Deduce->getExprLoc());
    TemplateDeductionResult Result =
        DeduceAutoType(TInfo->getTypeLoc(), Deduce, DeducedType, Info);
    if (Result != TemplateDeductionResult::Success &&
        Result != TemplateDeductionResult::AlreadyDiagnosed)
      return ExprError(Diag(TyBeginLoc, diag::err_auto_expr_deduction_failure)
                       << Ty << Deduce->getType() << FullRange
                       << Deduce->getSourceRange());
    if (DeducedType.isNull()) {
      assert(Result == TemplateDeductionResult::AlreadyDiagnosed);
      return ExprError();
    }

    Ty = DeducedType;
    Entity = InitializedEntity::InitializeTemporary(TInfo, Ty);
  }

  if (Ty->isDependentType() || CallExpr::hasAnyTypeDependentArguments(Exprs))
    return CXXUnresolvedConstructExpr::Create(
        Context, Ty.getNonReferenceType(), TInfo, LParenOrBraceLoc, Exprs,
        RParenOrBraceLoc, ListInitialization);

  // C++ [expr.type.conv]p1:
  // If the expression list is a parenthesized single expression, the type
  // conversion expression is equivalent (in definedness, and if defined in
  // meaning) to the corresponding cast expression.
  if (Exprs.size() == 1 && !ListInitialization &&
      !isa<InitListExpr>(Exprs[0])) {
    Expr *Arg = Exprs[0];
    return BuildCXXFunctionalCastExpr(TInfo, Ty, LParenOrBraceLoc, Arg,
                                      RParenOrBraceLoc);
  }

  //   For an expression of the form T(), T shall not be an array type.
  QualType ElemTy = Ty;
  if (Ty->isArrayType()) {
    if (!ListInitialization)
      return ExprError(Diag(TyBeginLoc, diag::err_value_init_for_array_type)
                         << FullRange);
    ElemTy = Context.getBaseElementType(Ty);
  }

  // Only construct objects with object types.
  // The standard doesn't explicitly forbid function types here, but that's an
  // obvious oversight, as there's no way to dynamically construct a function
  // in general.
  if (Ty->isFunctionType())
    return ExprError(Diag(TyBeginLoc, diag::err_init_for_function_type)
                       << Ty << FullRange);

  // C++17 [expr.type.conv]p2:
  //   If the type is cv void and the initializer is (), the expression is a
  //   prvalue of the specified type that performs no initialization.
  if (!Ty->isVoidType() &&
      RequireCompleteType(TyBeginLoc, ElemTy,
                          diag::err_invalid_incomplete_type_use, FullRange))
    return ExprError();

  //   Otherwise, the expression is a prvalue of the specified type whose
  //   result object is direct-initialized (11.6) with the initializer.
  InitializationSequence InitSeq(*this, Entity, Kind, Exprs);
  ExprResult Result = InitSeq.Perform(*this, Entity, Kind, Exprs);

  if (Result.isInvalid())
    return Result;

  Expr *Inner = Result.get();
  if (CXXBindTemporaryExpr *BTE = dyn_cast_or_null<CXXBindTemporaryExpr>(Inner))
    Inner = BTE->getSubExpr();
  if (auto *CE = dyn_cast<ConstantExpr>(Inner);
      CE && CE->isImmediateInvocation())
    Inner = CE->getSubExpr();
  if (!isa<CXXTemporaryObjectExpr>(Inner) &&
      !isa<CXXScalarValueInitExpr>(Inner)) {
    // If we created a CXXTemporaryObjectExpr, that node also represents the
    // functional cast. Otherwise, create an explicit cast to represent
    // the syntactic form of a functional-style cast that was used here.
    //
    // FIXME: Creating a CXXFunctionalCastExpr around a CXXConstructExpr
    // would give a more consistent AST representation than using a
    // CXXTemporaryObjectExpr. It's also weird that the functional cast
    // is sometimes handled by initialization and sometimes not.
    QualType ResultType = Result.get()->getType();
    SourceRange Locs = ListInitialization
                           ? SourceRange()
                           : SourceRange(LParenOrBraceLoc, RParenOrBraceLoc);
    Result = CXXFunctionalCastExpr::Create(
        Context, ResultType, Expr::getValueKindForType(Ty), TInfo, CK_NoOp,
        Result.get(), /*Path=*/nullptr, CurFPFeatureOverrides(),
        Locs.getBegin(), Locs.getEnd());
  }

  return Result;
}

bool Sema::isUsualDeallocationFunction(const CXXMethodDecl *Method) {
  // [CUDA] Ignore this function, if we can't call it.
  const FunctionDecl *Caller = getCurFunctionDecl(/*AllowLambda=*/true);
  if (getLangOpts().CUDA) {
    auto CallPreference = CUDA().IdentifyPreference(Caller, Method);
    // If it's not callable at all, it's not the right function.
    if (CallPreference < SemaCUDA::CFP_WrongSide)
      return false;
    if (CallPreference == SemaCUDA::CFP_WrongSide) {
      // Maybe. We have to check if there are better alternatives.
      DeclContext::lookup_result R =
          Method->getDeclContext()->lookup(Method->getDeclName());
      for (const auto *D : R) {
        if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
          if (CUDA().IdentifyPreference(Caller, FD) > SemaCUDA::CFP_WrongSide)
            return false;
        }
      }
      // We've found no better variants.
    }
  }

  SmallVector<const FunctionDecl*, 4> PreventedBy;
  bool Result = Method->isUsualDeallocationFunction(PreventedBy);

  if (Result || !getLangOpts().CUDA || PreventedBy.empty())
    return Result;

  // In case of CUDA, return true if none of the 1-argument deallocator
  // functions are actually callable.
  return llvm::none_of(PreventedBy, [&](const FunctionDecl *FD) {
    assert(FD->getNumParams() == 1 &&
           "Only single-operand functions should be in PreventedBy");
    return CUDA().IdentifyPreference(Caller, FD) >= SemaCUDA::CFP_HostDevice;
  });
}

/// Determine whether the given function is a non-placement
/// deallocation function.
static bool isNonPlacementDeallocationFunction(Sema &S, FunctionDecl *FD) {
  if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(FD))
    return S.isUsualDeallocationFunction(Method);

  if (FD->getOverloadedOperator() != OO_Delete &&
      FD->getOverloadedOperator() != OO_Array_Delete)
    return false;

  unsigned UsualParams = 1;

  if (S.getLangOpts().SizedDeallocation && UsualParams < FD->getNumParams() &&
      S.Context.hasSameUnqualifiedType(
          FD->getParamDecl(UsualParams)->getType(),
          S.Context.getSizeType()))
    ++UsualParams;

  if (S.getLangOpts().AlignedAllocation && UsualParams < FD->getNumParams() &&
      S.Context.hasSameUnqualifiedType(
          FD->getParamDecl(UsualParams)->getType(),
          S.Context.getTypeDeclType(S.getStdAlignValT())))
    ++UsualParams;

  return UsualParams == FD->getNumParams();
}

namespace {
  struct UsualDeallocFnInfo {
    UsualDeallocFnInfo() : Found(), FD(nullptr) {}
    UsualDeallocFnInfo(Sema &S, DeclAccessPair Found)
        : Found(Found), FD(dyn_cast<FunctionDecl>(Found->getUnderlyingDecl())),
          Destroying(false), HasSizeT(false), HasAlignValT(false),
          CUDAPref(SemaCUDA::CFP_Native) {
      // A function template declaration is never a usual deallocation function.
      if (!FD)
        return;
      unsigned NumBaseParams = 1;
      if (FD->isDestroyingOperatorDelete()) {
        Destroying = true;
        ++NumBaseParams;
      }

      if (NumBaseParams < FD->getNumParams() &&
          S.Context.hasSameUnqualifiedType(
              FD->getParamDecl(NumBaseParams)->getType(),
              S.Context.getSizeType())) {
        ++NumBaseParams;
        HasSizeT = true;
      }

      if (NumBaseParams < FD->getNumParams() &&
          FD->getParamDecl(NumBaseParams)->getType()->isAlignValT()) {
        ++NumBaseParams;
        HasAlignValT = true;
      }

      // In CUDA, determine how much we'd like / dislike to call this.
      if (S.getLangOpts().CUDA)
        CUDAPref = S.CUDA().IdentifyPreference(
            S.getCurFunctionDecl(/*AllowLambda=*/true), FD);
    }

    explicit operator bool() const { return FD; }

    bool isBetterThan(const UsualDeallocFnInfo &Other, bool WantSize,
                      bool WantAlign) const {
      // C++ P0722:
      //   A destroying operator delete is preferred over a non-destroying
      //   operator delete.
      if (Destroying != Other.Destroying)
        return Destroying;

      // C++17 [expr.delete]p10:
      //   If the type has new-extended alignment, a function with a parameter
      //   of type std::align_val_t is preferred; otherwise a function without
      //   such a parameter is preferred
      if (HasAlignValT != Other.HasAlignValT)
        return HasAlignValT == WantAlign;

      if (HasSizeT != Other.HasSizeT)
        return HasSizeT == WantSize;

      // Use CUDA call preference as a tiebreaker.
      return CUDAPref > Other.CUDAPref;
    }

    DeclAccessPair Found;
    FunctionDecl *FD;
    bool Destroying, HasSizeT, HasAlignValT;
    SemaCUDA::CUDAFunctionPreference CUDAPref;
  };
}

/// Determine whether a type has new-extended alignment. This may be called when
/// the type is incomplete (for a delete-expression with an incomplete pointee
/// type), in which case it will conservatively return false if the alignment is
/// not known.
static bool hasNewExtendedAlignment(Sema &S, QualType AllocType) {
  return S.getLangOpts().AlignedAllocation &&
         S.getASTContext().getTypeAlignIfKnown(AllocType) >
             S.getASTContext().getTargetInfo().getNewAlign();
}

/// Select the correct "usual" deallocation function to use from a selection of
/// deallocation functions (either global or class-scope).
static UsualDeallocFnInfo resolveDeallocationOverload(
    Sema &S, LookupResult &R, bool WantSize, bool WantAlign,
    llvm::SmallVectorImpl<UsualDeallocFnInfo> *BestFns = nullptr) {
  UsualDeallocFnInfo Best;

  for (auto I = R.begin(), E = R.end(); I != E; ++I) {
    UsualDeallocFnInfo Info(S, I.getPair());
    if (!Info || !isNonPlacementDeallocationFunction(S, Info.FD) ||
        Info.CUDAPref == SemaCUDA::CFP_Never)
      continue;

    if (!Best) {
      Best = Info;
      if (BestFns)
        BestFns->push_back(Info);
      continue;
    }

    if (Best.isBetterThan(Info, WantSize, WantAlign))
      continue;

    //   If more than one preferred function is found, all non-preferred
    //   functions are eliminated from further consideration.
    if (BestFns && Info.isBetterThan(Best, WantSize, WantAlign))
      BestFns->clear();

    Best = Info;
    if (BestFns)
      BestFns->push_back(Info);
  }

  return Best;
}

/// Determine whether a given type is a class for which 'delete[]' would call
/// a member 'operator delete[]' with a 'size_t' parameter. This implies that
/// we need to store the array size (even if the type is
/// trivially-destructible).
static bool doesUsualArrayDeleteWantSize(Sema &S, SourceLocation loc,
                                         QualType allocType) {
  const RecordType *record =
    allocType->getBaseElementTypeUnsafe()->getAs<RecordType>();
  if (!record) return false;

  // Try to find an operator delete[] in class scope.

  DeclarationName deleteName =
    S.Context.DeclarationNames.getCXXOperatorName(OO_Array_Delete);
  LookupResult ops(S, deleteName, loc, Sema::LookupOrdinaryName);
  S.LookupQualifiedName(ops, record->getDecl());

  // We're just doing this for information.
  ops.suppressDiagnostics();

  // Very likely: there's no operator delete[].
  if (ops.empty()) return false;

  // If it's ambiguous, it should be illegal to call operator delete[]
  // on this thing, so it doesn't matter if we allocate extra space or not.
  if (ops.isAmbiguous()) return false;

  // C++17 [expr.delete]p10:
  //   If the deallocation functions have class scope, the one without a
  //   parameter of type std::size_t is selected.
  auto Best = resolveDeallocationOverload(
      S, ops, /*WantSize*/false,
      /*WantAlign*/hasNewExtendedAlignment(S, allocType));
  return Best && Best.HasSizeT;
}

ExprResult
Sema::ActOnCXXNew(SourceLocation StartLoc, bool UseGlobal,
                  SourceLocation PlacementLParen, MultiExprArg PlacementArgs,
                  SourceLocation PlacementRParen, SourceRange TypeIdParens,
                  Declarator &D, Expr *Initializer) {
  std::optional<Expr *> ArraySize;
  // If the specified type is an array, unwrap it and save the expression.
  if (D.getNumTypeObjects() > 0 &&
      D.getTypeObject(0).Kind == DeclaratorChunk::Array) {
    DeclaratorChunk &Chunk = D.getTypeObject(0);
    if (D.getDeclSpec().hasAutoTypeSpec())
      return ExprError(Diag(Chunk.Loc, diag::err_new_array_of_auto)
        << D.getSourceRange());
    if (Chunk.Arr.hasStatic)
      return ExprError(Diag(Chunk.Loc, diag::err_static_illegal_in_new)
        << D.getSourceRange());
    if (!Chunk.Arr.NumElts && !Initializer)
      return ExprError(Diag(Chunk.Loc, diag::err_array_new_needs_size)
        << D.getSourceRange());

    ArraySize = static_cast<Expr*>(Chunk.Arr.NumElts);
    D.DropFirstTypeObject();
  }

  // Every dimension shall be of constant size.
  if (ArraySize) {
    for (unsigned I = 0, N = D.getNumTypeObjects(); I < N; ++I) {
      if (D.getTypeObject(I).Kind != DeclaratorChunk::Array)
        break;

      DeclaratorChunk::ArrayTypeInfo &Array = D.getTypeObject(I).Arr;
      if (Expr *NumElts = (Expr *)Array.NumElts) {
        if (!NumElts->isTypeDependent() && !NumElts->isValueDependent()) {
          // FIXME: GCC permits constant folding here. We should either do so consistently
          // or not do so at all, rather than changing behavior in C++14 onwards.
          if (getLangOpts().CPlusPlus14) {
            // C++1y [expr.new]p6: Every constant-expression in a noptr-new-declarator
            //   shall be a converted constant expression (5.19) of type std::size_t
            //   and shall evaluate to a strictly positive value.
            llvm::APSInt Value(Context.getIntWidth(Context.getSizeType()));
            Array.NumElts
             = CheckConvertedConstantExpression(NumElts, Context.getSizeType(), Value,
                                                CCEK_ArrayBound)
                 .get();
          } else {
            Array.NumElts =
                VerifyIntegerConstantExpression(
                    NumElts, nullptr, diag::err_new_array_nonconst, AllowFold)
                    .get();
          }
          if (!Array.NumElts)
            return ExprError();
        }
      }
    }
  }

  TypeSourceInfo *TInfo = GetTypeForDeclarator(D);
  QualType AllocType = TInfo->getType();
  if (D.isInvalidType())
    return ExprError();

  SourceRange DirectInitRange;
  if (ParenListExpr *List = dyn_cast_or_null<ParenListExpr>(Initializer))
    DirectInitRange = List->getSourceRange();

  return BuildCXXNew(SourceRange(StartLoc, D.getEndLoc()), UseGlobal,
                     PlacementLParen, PlacementArgs, PlacementRParen,
                     TypeIdParens, AllocType, TInfo, ArraySize, DirectInitRange,
                     Initializer);
}

static bool isLegalArrayNewInitializer(CXXNewInitializationStyle Style,
                                       Expr *Init, bool IsCPlusPlus20) {
  if (!Init)
    return true;
  if (ParenListExpr *PLE = dyn_cast<ParenListExpr>(Init))
    return IsCPlusPlus20 || PLE->getNumExprs() == 0;
  if (isa<ImplicitValueInitExpr>(Init))
    return true;
  else if (CXXConstructExpr *CCE = dyn_cast<CXXConstructExpr>(Init))
    return !CCE->isListInitialization() &&
           CCE->getConstructor()->isDefaultConstructor();
  else if (Style == CXXNewInitializationStyle::Braces) {
    assert(isa<InitListExpr>(Init) &&
           "Shouldn't create list CXXConstructExprs for arrays.");
    return true;
  }
  return false;
}

bool
Sema::isUnavailableAlignedAllocationFunction(const FunctionDecl &FD) const {
  if (!getLangOpts().AlignedAllocationUnavailable)
    return false;
  if (FD.isDefined())
    return false;
  std::optional<unsigned> AlignmentParam;
  if (FD.isReplaceableGlobalAllocationFunction(&AlignmentParam) &&
      AlignmentParam)
    return true;
  return false;
}

// Emit a diagnostic if an aligned allocation/deallocation function that is not
// implemented in the standard library is selected.
void Sema::diagnoseUnavailableAlignedAllocation(const FunctionDecl &FD,
                                                SourceLocation Loc) {
  if (isUnavailableAlignedAllocationFunction(FD)) {
    const llvm::Triple &T = getASTContext().getTargetInfo().getTriple();
    StringRef OSName = AvailabilityAttr::getPlatformNameSourceSpelling(
        getASTContext().getTargetInfo().getPlatformName());
    VersionTuple OSVersion = alignedAllocMinVersion(T.getOS());

    OverloadedOperatorKind Kind = FD.getDeclName().getCXXOverloadedOperator();
    bool IsDelete = Kind == OO_Delete || Kind == OO_Array_Delete;
    Diag(Loc, diag::err_aligned_allocation_unavailable)
        << IsDelete << FD.getType().getAsString() << OSName
        << OSVersion.getAsString() << OSVersion.empty();
    Diag(Loc, diag::note_silence_aligned_allocation_unavailable);
  }
}

ExprResult Sema::BuildCXXNew(SourceRange Range, bool UseGlobal,
                             SourceLocation PlacementLParen,
                             MultiExprArg PlacementArgs,
                             SourceLocation PlacementRParen,
                             SourceRange TypeIdParens, QualType AllocType,
                             TypeSourceInfo *AllocTypeInfo,
                             std::optional<Expr *> ArraySize,
                             SourceRange DirectInitRange, Expr *Initializer) {
  SourceRange TypeRange = AllocTypeInfo->getTypeLoc().getSourceRange();
  SourceLocation StartLoc = Range.getBegin();

  CXXNewInitializationStyle InitStyle;
  if (DirectInitRange.isValid()) {
    assert(Initializer && "Have parens but no initializer.");
    InitStyle = CXXNewInitializationStyle::Parens;
  } else if (isa_and_nonnull<InitListExpr>(Initializer))
    InitStyle = CXXNewInitializationStyle::Braces;
  else {
    assert((!Initializer || isa<ImplicitValueInitExpr>(Initializer) ||
            isa<CXXConstructExpr>(Initializer)) &&
           "Initializer expression that cannot have been implicitly created.");
    InitStyle = CXXNewInitializationStyle::None;
  }

  MultiExprArg Exprs(&Initializer, Initializer ? 1 : 0);
  if (ParenListExpr *List = dyn_cast_or_null<ParenListExpr>(Initializer)) {
    assert(InitStyle == CXXNewInitializationStyle::Parens &&
           "paren init for non-call init");
    Exprs = MultiExprArg(List->getExprs(), List->getNumExprs());
  }

  // C++11 [expr.new]p15:
  //   A new-expression that creates an object of type T initializes that
  //   object as follows:
  InitializationKind Kind = [&] {
    switch (InitStyle) {
    //     - If the new-initializer is omitted, the object is default-
    //       initialized (8.5); if no initialization is performed,
    //       the object has indeterminate value
    case CXXNewInitializationStyle::None:
      return InitializationKind::CreateDefault(TypeRange.getBegin());
    //     - Otherwise, the new-initializer is interpreted according to the
    //       initialization rules of 8.5 for direct-initialization.
    case CXXNewInitializationStyle::Parens:
      return InitializationKind::CreateDirect(TypeRange.getBegin(),
                                              DirectInitRange.getBegin(),
                                              DirectInitRange.getEnd());
    case CXXNewInitializationStyle::Braces:
      return InitializationKind::CreateDirectList(TypeRange.getBegin(),
                                                  Initializer->getBeginLoc(),
                                                  Initializer->getEndLoc());
    }
    llvm_unreachable("Unknown initialization kind");
  }();

  // C++11 [dcl.spec.auto]p6. Deduce the type which 'auto' stands in for.
  auto *Deduced = AllocType->getContainedDeducedType();
  if (Deduced && !Deduced->isDeduced() &&
      isa<DeducedTemplateSpecializationType>(Deduced)) {
    if (ArraySize)
      return ExprError(
          Diag(*ArraySize ? (*ArraySize)->getExprLoc() : TypeRange.getBegin(),
               diag::err_deduced_class_template_compound_type)
          << /*array*/ 2
          << (*ArraySize ? (*ArraySize)->getSourceRange() : TypeRange));

    InitializedEntity Entity
      = InitializedEntity::InitializeNew(StartLoc, AllocType);
    AllocType = DeduceTemplateSpecializationFromInitializer(
        AllocTypeInfo, Entity, Kind, Exprs);
    if (AllocType.isNull())
      return ExprError();
  } else if (Deduced && !Deduced->isDeduced()) {
    MultiExprArg Inits = Exprs;
    bool Braced = (InitStyle == CXXNewInitializationStyle::Braces);
    if (Braced) {
      auto *ILE = cast<InitListExpr>(Exprs[0]);
      Inits = MultiExprArg(ILE->getInits(), ILE->getNumInits());
    }

    if (InitStyle == CXXNewInitializationStyle::None || Inits.empty())
      return ExprError(Diag(StartLoc, diag::err_auto_new_requires_ctor_arg)
                       << AllocType << TypeRange);
    if (Inits.size() > 1) {
      Expr *FirstBad = Inits[1];
      return ExprError(Diag(FirstBad->getBeginLoc(),
                            diag::err_auto_new_ctor_multiple_expressions)
                       << AllocType << TypeRange);
    }
    if (Braced && !getLangOpts().CPlusPlus17)
      Diag(Initializer->getBeginLoc(), diag::ext_auto_new_list_init)
          << AllocType << TypeRange;
    Expr *Deduce = Inits[0];
    if (isa<InitListExpr>(Deduce))
      return ExprError(
          Diag(Deduce->getBeginLoc(), diag::err_auto_expr_init_paren_braces)
          << Braced << AllocType << TypeRange);
    QualType DeducedType;
    TemplateDeductionInfo Info(Deduce->getExprLoc());
    TemplateDeductionResult Result =
        DeduceAutoType(AllocTypeInfo->getTypeLoc(), Deduce, DeducedType, Info);
    if (Result != TemplateDeductionResult::Success &&
        Result != TemplateDeductionResult::AlreadyDiagnosed)
      return ExprError(Diag(StartLoc, diag::err_auto_new_deduction_failure)
                       << AllocType << Deduce->getType() << TypeRange
                       << Deduce->getSourceRange());
    if (DeducedType.isNull()) {
      assert(Result == TemplateDeductionResult::AlreadyDiagnosed);
      return ExprError();
    }
    AllocType = DeducedType;
  }

  // Per C++0x [expr.new]p5, the type being constructed may be a
  // typedef of an array type.
  if (!ArraySize) {
    if (const ConstantArrayType *Array
                              = Context.getAsConstantArrayType(AllocType)) {
      ArraySize = IntegerLiteral::Create(Context, Array->getSize(),
                                         Context.getSizeType(),
                                         TypeRange.getEnd());
      AllocType = Array->getElementType();
    }
  }

  if (CheckAllocatedType(AllocType, TypeRange.getBegin(), TypeRange))
    return ExprError();

  if (ArraySize && !checkArrayElementAlignment(AllocType, TypeRange.getBegin()))
    return ExprError();

  // In ARC, infer 'retaining' for the allocated
  if (getLangOpts().ObjCAutoRefCount &&
      AllocType.getObjCLifetime() == Qualifiers::OCL_None &&
      AllocType->isObjCLifetimeType()) {
    AllocType = Context.getLifetimeQualifiedType(AllocType,
                                    AllocType->getObjCARCImplicitLifetime());
  }

  QualType ResultType = Context.getPointerType(AllocType);

  if (ArraySize && *ArraySize &&
      (*ArraySize)->getType()->isNonOverloadPlaceholderType()) {
    ExprResult result = CheckPlaceholderExpr(*ArraySize);
    if (result.isInvalid()) return ExprError();
    ArraySize = result.get();
  }
  // C++98 5.3.4p6: "The expression in a direct-new-declarator shall have
  //   integral or enumeration type with a non-negative value."
  // C++11 [expr.new]p6: The expression [...] shall be of integral or unscoped
  //   enumeration type, or a class type for which a single non-explicit
  //   conversion function to integral or unscoped enumeration type exists.
  // C++1y [expr.new]p6: The expression [...] is implicitly converted to
  //   std::size_t.
  std::optional<uint64_t> KnownArraySize;
  if (ArraySize && *ArraySize && !(*ArraySize)->isTypeDependent()) {
    ExprResult ConvertedSize;
    if (getLangOpts().CPlusPlus14) {
      assert(Context.getTargetInfo().getIntWidth() && "Builtin type of size 0?");

      ConvertedSize = PerformImplicitConversion(*ArraySize, Context.getSizeType(),
                                                AA_Converting);

      if (!ConvertedSize.isInvalid() &&
          (*ArraySize)->getType()->getAs<RecordType>())
        // Diagnose the compatibility of this conversion.
        Diag(StartLoc, diag::warn_cxx98_compat_array_size_conversion)
          << (*ArraySize)->getType() << 0 << "'size_t'";
    } else {
      class SizeConvertDiagnoser : public ICEConvertDiagnoser {
      protected:
        Expr *ArraySize;

      public:
        SizeConvertDiagnoser(Expr *ArraySize)
            : ICEConvertDiagnoser(/*AllowScopedEnumerations*/false, false, false),
              ArraySize(ArraySize) {}

        SemaDiagnosticBuilder diagnoseNotInt(Sema &S, SourceLocation Loc,
                                             QualType T) override {
          return S.Diag(Loc, diag::err_array_size_not_integral)
                   << S.getLangOpts().CPlusPlus11 << T;
        }

        SemaDiagnosticBuilder diagnoseIncomplete(
            Sema &S, SourceLocation Loc, QualType T) override {
          return S.Diag(Loc, diag::err_array_size_incomplete_type)
                   << T << ArraySize->getSourceRange();
        }

        SemaDiagnosticBuilder diagnoseExplicitConv(
            Sema &S, SourceLocation Loc, QualType T, QualType ConvTy) override {
          return S.Diag(Loc, diag::err_array_size_explicit_conversion) << T << ConvTy;
        }

        SemaDiagnosticBuilder noteExplicitConv(
            Sema &S, CXXConversionDecl *Conv, QualType ConvTy) override {
          return S.Diag(Conv->getLocation(), diag::note_array_size_conversion)
                   << ConvTy->isEnumeralType() << ConvTy;
        }

        SemaDiagnosticBuilder diagnoseAmbiguous(
            Sema &S, SourceLocation Loc, QualType T) override {
          return S.Diag(Loc, diag::err_array_size_ambiguous_conversion) << T;
        }

        SemaDiagnosticBuilder noteAmbiguous(
            Sema &S, CXXConversionDecl *Conv, QualType ConvTy) override {
          return S.Diag(Conv->getLocation(), diag::note_array_size_conversion)
                   << ConvTy->isEnumeralType() << ConvTy;
        }

        SemaDiagnosticBuilder diagnoseConversion(Sema &S, SourceLocation Loc,
                                                 QualType T,
                                                 QualType ConvTy) override {
          return S.Diag(Loc,
                        S.getLangOpts().CPlusPlus11
                          ? diag::warn_cxx98_compat_array_size_conversion
                          : diag::ext_array_size_conversion)
                   << T << ConvTy->isEnumeralType() << ConvTy;
        }
      } SizeDiagnoser(*ArraySize);

      ConvertedSize = PerformContextualImplicitConversion(StartLoc, *ArraySize,
                                                          SizeDiagnoser);
    }
    if (ConvertedSize.isInvalid())
      return ExprError();

    ArraySize = ConvertedSize.get();
    QualType SizeType = (*ArraySize)->getType();

    if (!SizeType->isIntegralOrUnscopedEnumerationType())
      return ExprError();

    // C++98 [expr.new]p7:
    //   The expression in a direct-new-declarator shall have integral type
    //   with a non-negative value.
    //
    // Let's see if this is a constant < 0. If so, we reject it out of hand,
    // per CWG1464. Otherwise, if it's not a constant, we must have an
    // unparenthesized array type.

    // We've already performed any required implicit conversion to integer or
    // unscoped enumeration type.
    // FIXME: Per CWG1464, we are required to check the value prior to
    // converting to size_t. This will never find a negative array size in
    // C++14 onwards, because Value is always unsigned here!
    if (std::optional<llvm::APSInt> Value =
            (*ArraySize)->getIntegerConstantExpr(Context)) {
      if (Value->isSigned() && Value->isNegative()) {
        return ExprError(Diag((*ArraySize)->getBeginLoc(),
                              diag::err_typecheck_negative_array_size)
                         << (*ArraySize)->getSourceRange());
      }

      if (!AllocType->isDependentType()) {
        unsigned ActiveSizeBits =
            ConstantArrayType::getNumAddressingBits(Context, AllocType, *Value);
        if (ActiveSizeBits > ConstantArrayType::getMaxSizeBits(Context))
          return ExprError(
              Diag((*ArraySize)->getBeginLoc(), diag::err_array_too_large)
              << toString(*Value, 10) << (*ArraySize)->getSourceRange());
      }

      KnownArraySize = Value->getZExtValue();
    } else if (TypeIdParens.isValid()) {
      // Can't have dynamic array size when the type-id is in parentheses.
      Diag((*ArraySize)->getBeginLoc(), diag::ext_new_paren_array_nonconst)
          << (*ArraySize)->getSourceRange()
          << FixItHint::CreateRemoval(TypeIdParens.getBegin())
          << FixItHint::CreateRemoval(TypeIdParens.getEnd());

      TypeIdParens = SourceRange();
    }

    // Note that we do *not* convert the argument in any way.  It can
    // be signed, larger than size_t, whatever.
  }

  FunctionDecl *OperatorNew = nullptr;
  FunctionDecl *OperatorDelete = nullptr;
  unsigned Alignment =
      AllocType->isDependentType() ? 0 : Context.getTypeAlign(AllocType);
  unsigned NewAlignment = Context.getTargetInfo().getNewAlign();
  bool PassAlignment = getLangOpts().AlignedAllocation &&
                       Alignment > NewAlignment;

  if (CheckArgsForPlaceholders(PlacementArgs))
    return ExprError();

  AllocationFunctionScope Scope = UseGlobal ? AFS_Global : AFS_Both;
  if (!AllocType->isDependentType() &&
      !Expr::hasAnyTypeDependentArguments(PlacementArgs) &&
      FindAllocationFunctions(
          StartLoc, SourceRange(PlacementLParen, PlacementRParen), Scope, Scope,
          AllocType, ArraySize.has_value(), PassAlignment, PlacementArgs,
          OperatorNew, OperatorDelete))
    return ExprError();

  // If this is an array allocation, compute whether the usual array
  // deallocation function for the type has a size_t parameter.
  bool UsualArrayDeleteWantsSize = false;
  if (ArraySize && !AllocType->isDependentType())
    UsualArrayDeleteWantsSize =
        doesUsualArrayDeleteWantSize(*this, StartLoc, AllocType);

  SmallVector<Expr *, 8> AllPlaceArgs;
  if (OperatorNew) {
    auto *Proto = OperatorNew->getType()->castAs<FunctionProtoType>();
    VariadicCallType CallType = Proto->isVariadic() ? VariadicFunction
                                                    : VariadicDoesNotApply;

    // We've already converted the placement args, just fill in any default
    // arguments. Skip the first parameter because we don't have a corresponding
    // argument. Skip the second parameter too if we're passing in the
    // alignment; we've already filled it in.
    unsigned NumImplicitArgs = PassAlignment ? 2 : 1;
    if (GatherArgumentsForCall(PlacementLParen, OperatorNew, Proto,
                               NumImplicitArgs, PlacementArgs, AllPlaceArgs,
                               CallType))
      return ExprError();

    if (!AllPlaceArgs.empty())
      PlacementArgs = AllPlaceArgs;

    // We would like to perform some checking on the given `operator new` call,
    // but the PlacementArgs does not contain the implicit arguments,
    // namely allocation size and maybe allocation alignment,
    // so we need to conjure them.

    QualType SizeTy = Context.getSizeType();
    unsigned SizeTyWidth = Context.getTypeSize(SizeTy);

    llvm::APInt SingleEltSize(
        SizeTyWidth, Context.getTypeSizeInChars(AllocType).getQuantity());

    // How many bytes do we want to allocate here?
    std::optional<llvm::APInt> AllocationSize;
    if (!ArraySize && !AllocType->isDependentType()) {
      // For non-array operator new, we only want to allocate one element.
      AllocationSize = SingleEltSize;
    } else if (KnownArraySize && !AllocType->isDependentType()) {
      // For array operator new, only deal with static array size case.
      bool Overflow;
      AllocationSize = llvm::APInt(SizeTyWidth, *KnownArraySize)
                           .umul_ov(SingleEltSize, Overflow);
      (void)Overflow;
      assert(
          !Overflow &&
          "Expected that all the overflows would have been handled already.");
    }

    IntegerLiteral AllocationSizeLiteral(
        Context, AllocationSize.value_or(llvm::APInt::getZero(SizeTyWidth)),
        SizeTy, SourceLocation());
    // Otherwise, if we failed to constant-fold the allocation size, we'll
    // just give up and pass-in something opaque, that isn't a null pointer.
    OpaqueValueExpr OpaqueAllocationSize(SourceLocation(), SizeTy, VK_PRValue,
                                         OK_Ordinary, /*SourceExpr=*/nullptr);

    // Let's synthesize the alignment argument in case we will need it.
    // Since we *really* want to allocate these on stack, this is slightly ugly
    // because there might not be a `std::align_val_t` type.
    EnumDecl *StdAlignValT = getStdAlignValT();
    QualType AlignValT =
        StdAlignValT ? Context.getTypeDeclType(StdAlignValT) : SizeTy;
    IntegerLiteral AlignmentLiteral(
        Context,
        llvm::APInt(Context.getTypeSize(SizeTy),
                    Alignment / Context.getCharWidth()),
        SizeTy, SourceLocation());
    ImplicitCastExpr DesiredAlignment(ImplicitCastExpr::OnStack, AlignValT,
                                      CK_IntegralCast, &AlignmentLiteral,
                                      VK_PRValue, FPOptionsOverride());

    // Adjust placement args by prepending conjured size and alignment exprs.
    llvm::SmallVector<Expr *, 8> CallArgs;
    CallArgs.reserve(NumImplicitArgs + PlacementArgs.size());
    CallArgs.emplace_back(AllocationSize
                              ? static_cast<Expr *>(&AllocationSizeLiteral)
                              : &OpaqueAllocationSize);
    if (PassAlignment)
      CallArgs.emplace_back(&DesiredAlignment);
    CallArgs.insert(CallArgs.end(), PlacementArgs.begin(), PlacementArgs.end());

    DiagnoseSentinelCalls(OperatorNew, PlacementLParen, CallArgs);

    checkCall(OperatorNew, Proto, /*ThisArg=*/nullptr, CallArgs,
              /*IsMemberFunction=*/false, StartLoc, Range, CallType);

    // Warn if the type is over-aligned and is being allocated by (unaligned)
    // global operator new.
    if (PlacementArgs.empty() && !PassAlignment &&
        (OperatorNew->isImplicit() ||
         (OperatorNew->getBeginLoc().isValid() &&
          getSourceManager().isInSystemHeader(OperatorNew->getBeginLoc())))) {
      if (Alignment > NewAlignment)
        Diag(StartLoc, diag::warn_overaligned_type)
            << AllocType
            << unsigned(Alignment / Context.getCharWidth())
            << unsigned(NewAlignment / Context.getCharWidth());
    }
  }

  // Array 'new' can't have any initializers except empty parentheses.
  // Initializer lists are also allowed, in C++11. Rely on the parser for the
  // dialect distinction.
  if (ArraySize && !isLegalArrayNewInitializer(InitStyle, Initializer,
                                               getLangOpts().CPlusPlus20)) {
    SourceRange InitRange(Exprs.front()->getBeginLoc(),
                          Exprs.back()->getEndLoc());
    Diag(StartLoc, diag::err_new_array_init_args) << InitRange;
    return ExprError();
  }

  // If we can perform the initialization, and we've not already done so,
  // do it now.
  if (!AllocType->isDependentType() &&
      !Expr::hasAnyTypeDependentArguments(Exprs)) {
    // The type we initialize is the complete type, including the array bound.
    QualType InitType;
    if (KnownArraySize)
      InitType = Context.getConstantArrayType(
          AllocType,
          llvm::APInt(Context.getTypeSize(Context.getSizeType()),
                      *KnownArraySize),
          *ArraySize, ArraySizeModifier::Normal, 0);
    else if (ArraySize)
      InitType = Context.getIncompleteArrayType(AllocType,
                                                ArraySizeModifier::Normal, 0);
    else
      InitType = AllocType;

    InitializedEntity Entity
      = InitializedEntity::InitializeNew(StartLoc, InitType);
    InitializationSequence InitSeq(*this, Entity, Kind, Exprs);
    ExprResult FullInit = InitSeq.Perform(*this, Entity, Kind, Exprs);
    if (FullInit.isInvalid())
      return ExprError();

    // FullInit is our initializer; strip off CXXBindTemporaryExprs, because
    // we don't want the initialized object to be destructed.
    // FIXME: We should not create these in the first place.
    if (CXXBindTemporaryExpr *Binder =
            dyn_cast_or_null<CXXBindTemporaryExpr>(FullInit.get()))
      FullInit = Binder->getSubExpr();

    Initializer = FullInit.get();

    // FIXME: If we have a KnownArraySize, check that the array bound of the
    // initializer is no greater than that constant value.

    if (ArraySize && !*ArraySize) {
      auto *CAT = Context.getAsConstantArrayType(Initializer->getType());
      if (CAT) {
        // FIXME: Track that the array size was inferred rather than explicitly
        // specified.
        ArraySize = IntegerLiteral::Create(
            Context, CAT->getSize(), Context.getSizeType(), TypeRange.getEnd());
      } else {
        Diag(TypeRange.getEnd(), diag::err_new_array_size_unknown_from_init)
            << Initializer->getSourceRange();
      }
    }
  }

  // Mark the new and delete operators as referenced.
  if (OperatorNew) {
    if (DiagnoseUseOfDecl(OperatorNew, StartLoc))
      return ExprError();
    MarkFunctionReferenced(StartLoc, OperatorNew);
  }
  if (OperatorDelete) {
    if (DiagnoseUseOfDecl(OperatorDelete, StartLoc))
      return ExprError();
    MarkFunctionReferenced(StartLoc, OperatorDelete);
  }

  return CXXNewExpr::Create(Context, UseGlobal, OperatorNew, OperatorDelete,
                            PassAlignment, UsualArrayDeleteWantsSize,
                            PlacementArgs, TypeIdParens, ArraySize, InitStyle,
                            Initializer, ResultType, AllocTypeInfo, Range,
                            DirectInitRange);
}

bool Sema::CheckAllocatedType(QualType AllocType, SourceLocation Loc,
                              SourceRange R) {
  // C++ 5.3.4p1: "[The] type shall be a complete object type, but not an
  //   abstract class type or array thereof.
  if (AllocType->isFunctionType())
    return Diag(Loc, diag::err_bad_new_type)
      << AllocType << 0 << R;
  else if (AllocType->isReferenceType())
    return Diag(Loc, diag::err_bad_new_type)
      << AllocType << 1 << R;
  else if (!AllocType->isDependentType() &&
           RequireCompleteSizedType(
               Loc, AllocType, diag::err_new_incomplete_or_sizeless_type, R))
    return true;
  else if (RequireNonAbstractType(Loc, AllocType,
                                  diag::err_allocation_of_abstract_type))
    return true;
  else if (AllocType->isVariablyModifiedType())
    return Diag(Loc, diag::err_variably_modified_new_type)
             << AllocType;
  else if (AllocType.getAddressSpace() != LangAS::Default &&
           !getLangOpts().OpenCLCPlusPlus)
    return Diag(Loc, diag::err_address_space_qualified_new)
      << AllocType.getUnqualifiedType()
      << AllocType.getQualifiers().getAddressSpaceAttributePrintValue();
  else if (getLangOpts().ObjCAutoRefCount) {
    if (const ArrayType *AT = Context.getAsArrayType(AllocType)) {
      QualType BaseAllocType = Context.getBaseElementType(AT);
      if (BaseAllocType.getObjCLifetime() == Qualifiers::OCL_None &&
          BaseAllocType->isObjCLifetimeType())
        return Diag(Loc, diag::err_arc_new_array_without_ownership)
          << BaseAllocType;
    }
  }

  return false;
}

static bool resolveAllocationOverload(
    Sema &S, LookupResult &R, SourceRange Range, SmallVectorImpl<Expr *> &Args,
    bool &PassAlignment, FunctionDecl *&Operator,
    OverloadCandidateSet *AlignedCandidates, Expr *AlignArg, bool Diagnose) {
  OverloadCandidateSet Candidates(R.getNameLoc(),
                                  OverloadCandidateSet::CSK_Normal);
  for (LookupResult::iterator Alloc = R.begin(), AllocEnd = R.end();
       Alloc != AllocEnd; ++Alloc) {
    // Even member operator new/delete are implicitly treated as
    // static, so don't use AddMemberCandidate.
    NamedDecl *D = (*Alloc)->getUnderlyingDecl();

    if (FunctionTemplateDecl *FnTemplate = dyn_cast<FunctionTemplateDecl>(D)) {
      S.AddTemplateOverloadCandidate(FnTemplate, Alloc.getPair(),
                                     /*ExplicitTemplateArgs=*/nullptr, Args,
                                     Candidates,
                                     /*SuppressUserConversions=*/false);
      continue;
    }

    FunctionDecl *Fn = cast<FunctionDecl>(D);
    S.AddOverloadCandidate(Fn, Alloc.getPair(), Args, Candidates,
                           /*SuppressUserConversions=*/false);
  }

  // Do the resolution.
  OverloadCandidateSet::iterator Best;
  switch (Candidates.BestViableFunction(S, R.getNameLoc(), Best)) {
  case OR_Success: {
    // Got one!
    FunctionDecl *FnDecl = Best->Function;
    if (S.CheckAllocationAccess(R.getNameLoc(), Range, R.getNamingClass(),
                                Best->FoundDecl) == Sema::AR_inaccessible)
      return true;

    Operator = FnDecl;
    return false;
  }

  case OR_No_Viable_Function:
    // C++17 [expr.new]p13:
    //   If no matching function is found and the allocated object type has
    //   new-extended alignment, the alignment argument is removed from the
    //   argument list, and overload resolution is performed again.
    if (PassAlignment) {
      PassAlignment = false;
      AlignArg = Args[1];
      Args.erase(Args.begin() + 1);
      return resolveAllocationOverload(S, R, Range, Args, PassAlignment,
                                       Operator, &Candidates, AlignArg,
                                       Diagnose);
    }

    // MSVC will fall back on trying to find a matching global operator new
    // if operator new[] cannot be found.  Also, MSVC will leak by not
    // generating a call to operator delete or operator delete[], but we
    // will not replicate that bug.
    // FIXME: Find out how this interacts with the std::align_val_t fallback
    // once MSVC implements it.
    if (R.getLookupName().getCXXOverloadedOperator() == OO_Array_New &&
        S.Context.getLangOpts().MSVCCompat) {
      R.clear();
      R.setLookupName(S.Context.DeclarationNames.getCXXOperatorName(OO_New));
      S.LookupQualifiedName(R, S.Context.getTranslationUnitDecl());
      // FIXME: This will give bad diagnostics pointing at the wrong functions.
      return resolveAllocationOverload(S, R, Range, Args, PassAlignment,
                                       Operator, /*Candidates=*/nullptr,
                                       /*AlignArg=*/nullptr, Diagnose);
    }

    if (Diagnose) {
      // If this is an allocation of the form 'new (p) X' for some object
      // pointer p (or an expression that will decay to such a pointer),
      // diagnose the missing inclusion of <new>.
      if (!R.isClassLookup() && Args.size() == 2 &&
          (Args[1]->getType()->isObjectPointerType() ||
           Args[1]->getType()->isArrayType())) {
        S.Diag(R.getNameLoc(), diag::err_need_header_before_placement_new)
            << R.getLookupName() << Range;
        // Listing the candidates is unlikely to be useful; skip it.
        return true;
      }

      // Finish checking all candidates before we note any. This checking can
      // produce additional diagnostics so can't be interleaved with our
      // emission of notes.
      //
      // For an aligned allocation, separately check the aligned and unaligned
      // candidates with their respective argument lists.
      SmallVector<OverloadCandidate*, 32> Cands;
      SmallVector<OverloadCandidate*, 32> AlignedCands;
      llvm::SmallVector<Expr*, 4> AlignedArgs;
      if (AlignedCandidates) {
        auto IsAligned = [](OverloadCandidate &C) {
          return C.Function->getNumParams() > 1 &&
                 C.Function->getParamDecl(1)->getType()->isAlignValT();
        };
        auto IsUnaligned = [&](OverloadCandidate &C) { return !IsAligned(C); };

        AlignedArgs.reserve(Args.size() + 1);
        AlignedArgs.push_back(Args[0]);
        AlignedArgs.push_back(AlignArg);
        AlignedArgs.append(Args.begin() + 1, Args.end());
        AlignedCands = AlignedCandidates->CompleteCandidates(
            S, OCD_AllCandidates, AlignedArgs, R.getNameLoc(), IsAligned);

        Cands = Candidates.CompleteCandidates(S, OCD_AllCandidates, Args,
                                              R.getNameLoc(), IsUnaligned);
      } else {
        Cands = Candidates.CompleteCandidates(S, OCD_AllCandidates, Args,
                                              R.getNameLoc());
      }

      S.Diag(R.getNameLoc(), diag::err_ovl_no_viable_function_in_call)
          << R.getLookupName() << Range;
      if (AlignedCandidates)
        AlignedCandidates->NoteCandidates(S, AlignedArgs, AlignedCands, "",
                                          R.getNameLoc());
      Candidates.NoteCandidates(S, Args, Cands, "", R.getNameLoc());
    }
    return true;

  case OR_Ambiguous:
    if (Diagnose) {
      Candidates.NoteCandidates(
          PartialDiagnosticAt(R.getNameLoc(),
                              S.PDiag(diag::err_ovl_ambiguous_call)
                                  << R.getLookupName() << Range),
          S, OCD_AmbiguousCandidates, Args);
    }
    return true;

  case OR_Deleted: {
    if (Diagnose)
      S.DiagnoseUseOfDeletedFunction(R.getNameLoc(), Range, R.getLookupName(),
                                     Candidates, Best->Function, Args);
    return true;
  }
  }
  llvm_unreachable("Unreachable, bad result from BestViableFunction");
}

bool Sema::FindAllocationFunctions(SourceLocation StartLoc, SourceRange Range,
                                   AllocationFunctionScope NewScope,
                                   AllocationFunctionScope DeleteScope,
                                   QualType AllocType, bool IsArray,
                                   bool &PassAlignment, MultiExprArg PlaceArgs,
                                   FunctionDecl *&OperatorNew,
                                   FunctionDecl *&OperatorDelete,
                                   bool Diagnose) {
  // --- Choosing an allocation function ---
  // C++ 5.3.4p8 - 14 & 18
  // 1) If looking in AFS_Global scope for allocation functions, only look in
  //    the global scope. Else, if AFS_Class, only look in the scope of the
  //    allocated class. If AFS_Both, look in both.
  // 2) If an array size is given, look for operator new[], else look for
  //   operator new.
  // 3) The first argument is always size_t. Append the arguments from the
  //   placement form.

  SmallVector<Expr*, 8> AllocArgs;
  AllocArgs.reserve((PassAlignment ? 2 : 1) + PlaceArgs.size());

  // We don't care about the actual value of these arguments.
  // FIXME: Should the Sema create the expression and embed it in the syntax
  // tree? Or should the consumer just recalculate the value?
  // FIXME: Using a dummy value will interact poorly with attribute enable_if.
  QualType SizeTy = Context.getSizeType();
  unsigned SizeTyWidth = Context.getTypeSize(SizeTy);
  IntegerLiteral Size(Context, llvm::APInt::getZero(SizeTyWidth), SizeTy,
                      SourceLocation());
  AllocArgs.push_back(&Size);

  QualType AlignValT = Context.VoidTy;
  if (PassAlignment) {
    DeclareGlobalNewDelete();
    AlignValT = Context.getTypeDeclType(getStdAlignValT());
  }
  CXXScalarValueInitExpr Align(AlignValT, nullptr, SourceLocation());
  if (PassAlignment)
    AllocArgs.push_back(&Align);

  AllocArgs.insert(AllocArgs.end(), PlaceArgs.begin(), PlaceArgs.end());

  // C++ [expr.new]p8:
  //   If the allocated type is a non-array type, the allocation
  //   function's name is operator new and the deallocation function's
  //   name is operator delete. If the allocated type is an array
  //   type, the allocation function's name is operator new[] and the
  //   deallocation function's name is operator delete[].
  DeclarationName NewName = Context.DeclarationNames.getCXXOperatorName(
      IsArray ? OO_Array_New : OO_New);

  QualType AllocElemType = Context.getBaseElementType(AllocType);

  // Find the allocation function.
  {
    LookupResult R(*this, NewName, StartLoc, LookupOrdinaryName);

    // C++1z [expr.new]p9:
    //   If the new-expression begins with a unary :: operator, the allocation
    //   function's name is looked up in the global scope. Otherwise, if the
    //   allocated type is a class type T or array thereof, the allocation
    //   function's name is looked up in the scope of T.
    if (AllocElemType->isRecordType() && NewScope != AFS_Global)
      LookupQualifiedName(R, AllocElemType->getAsCXXRecordDecl());

    // We can see ambiguity here if the allocation function is found in
    // multiple base classes.
    if (R.isAmbiguous())
      return true;

    //   If this lookup fails to find the name, or if the allocated type is not
    //   a class type, the allocation function's name is looked up in the
    //   global scope.
    if (R.empty()) {
      if (NewScope == AFS_Class)
        return true;

      LookupQualifiedName(R, Context.getTranslationUnitDecl());
    }

    if (getLangOpts().OpenCLCPlusPlus && R.empty()) {
      if (PlaceArgs.empty()) {
        Diag(StartLoc, diag::err_openclcxx_not_supported) << "default new";
      } else {
        Diag(StartLoc, diag::err_openclcxx_placement_new);
      }
      return true;
    }

    assert(!R.empty() && "implicitly declared allocation functions not found");
    assert(!R.isAmbiguous() && "global allocation functions are ambiguous");

    // We do our own custom access checks below.
    R.suppressDiagnostics();

    if (resolveAllocationOverload(*this, R, Range, AllocArgs, PassAlignment,
                                  OperatorNew, /*Candidates=*/nullptr,
                                  /*AlignArg=*/nullptr, Diagnose))
      return true;
  }

  // We don't need an operator delete if we're running under -fno-exceptions.
  if (!getLangOpts().Exceptions) {
    OperatorDelete = nullptr;
    return false;
  }

  // Note, the name of OperatorNew might have been changed from array to
  // non-array by resolveAllocationOverload.
  DeclarationName DeleteName = Context.DeclarationNames.getCXXOperatorName(
      OperatorNew->getDeclName().getCXXOverloadedOperator() == OO_Array_New
          ? OO_Array_Delete
          : OO_Delete);

  // C++ [expr.new]p19:
  //
  //   If the new-expression begins with a unary :: operator, the
  //   deallocation function's name is looked up in the global
  //   scope. Otherwise, if the allocated type is a class type T or an
  //   array thereof, the deallocation function's name is looked up in
  //   the scope of T. If this lookup fails to find the name, or if
  //   the allocated type is not a class type or array thereof, the
  //   deallocation function's name is looked up in the global scope.
  LookupResult FoundDelete(*this, DeleteName, StartLoc, LookupOrdinaryName);
  if (AllocElemType->isRecordType() && DeleteScope != AFS_Global) {
    auto *RD =
        cast<CXXRecordDecl>(AllocElemType->castAs<RecordType>()->getDecl());
    LookupQualifiedName(FoundDelete, RD);
  }
  if (FoundDelete.isAmbiguous())
    return true; // FIXME: clean up expressions?

  // Filter out any destroying operator deletes. We can't possibly call such a
  // function in this context, because we're handling the case where the object
  // was not successfully constructed.
  // FIXME: This is not covered by the language rules yet.
  {
    LookupResult::Filter Filter = FoundDelete.makeFilter();
    while (Filter.hasNext()) {
      auto *FD = dyn_cast<FunctionDecl>(Filter.next()->getUnderlyingDecl());
      if (FD && FD->isDestroyingOperatorDelete())
        Filter.erase();
    }
    Filter.done();
  }

  bool FoundGlobalDelete = FoundDelete.empty();
  if (FoundDelete.empty()) {
    FoundDelete.clear(LookupOrdinaryName);

    if (DeleteScope == AFS_Class)
      return true;

    DeclareGlobalNewDelete();
    LookupQualifiedName(FoundDelete, Context.getTranslationUnitDecl());
  }

  FoundDelete.suppressDiagnostics();

  SmallVector<std::pair<DeclAccessPair,FunctionDecl*>, 2> Matches;

  // Whether we're looking for a placement operator delete is dictated
  // by whether we selected a placement operator new, not by whether
  // we had explicit placement arguments.  This matters for things like
  //   struct A { void *operator new(size_t, int = 0); ... };
  //   A *a = new A()
  //
  // We don't have any definition for what a "placement allocation function"
  // is, but we assume it's any allocation function whose
  // parameter-declaration-clause is anything other than (size_t).
  //
  // FIXME: Should (size_t, std::align_val_t) also be considered non-placement?
  // This affects whether an exception from the constructor of an overaligned
  // type uses the sized or non-sized form of aligned operator delete.
  bool isPlacementNew = !PlaceArgs.empty() || OperatorNew->param_size() != 1 ||
                        OperatorNew->isVariadic();

  if (isPlacementNew) {
    // C++ [expr.new]p20:
    //   A declaration of a placement deallocation function matches the
    //   declaration of a placement allocation function if it has the
    //   same number of parameters and, after parameter transformations
    //   (8.3.5), all parameter types except the first are
    //   identical. [...]
    //
    // To perform this comparison, we compute the function type that
    // the deallocation function should have, and use that type both
    // for template argument deduction and for comparison purposes.
    QualType ExpectedFunctionType;
    {
      auto *Proto = OperatorNew->getType()->castAs<FunctionProtoType>();

      SmallVector<QualType, 4> ArgTypes;
      ArgTypes.push_back(Context.VoidPtrTy);
      for (unsigned I = 1, N = Proto->getNumParams(); I < N; ++I)
        ArgTypes.push_back(Proto->getParamType(I));

      FunctionProtoType::ExtProtoInfo EPI;
      // FIXME: This is not part of the standard's rule.
      EPI.Variadic = Proto->isVariadic();

      ExpectedFunctionType
        = Context.getFunctionType(Context.VoidTy, ArgTypes, EPI);
    }

    for (LookupResult::iterator D = FoundDelete.begin(),
                             DEnd = FoundDelete.end();
         D != DEnd; ++D) {
      FunctionDecl *Fn = nullptr;
      if (FunctionTemplateDecl *FnTmpl =
              dyn_cast<FunctionTemplateDecl>((*D)->getUnderlyingDecl())) {
        // Perform template argument deduction to try to match the
        // expected function type.
        TemplateDeductionInfo Info(StartLoc);
        if (DeduceTemplateArguments(FnTmpl, nullptr, ExpectedFunctionType, Fn,
                                    Info) != TemplateDeductionResult::Success)
          continue;
      } else
        Fn = cast<FunctionDecl>((*D)->getUnderlyingDecl());

      if (Context.hasSameType(adjustCCAndNoReturn(Fn->getType(),
                                                  ExpectedFunctionType,
                                                  /*AdjustExcpetionSpec*/true),
                              ExpectedFunctionType))
        Matches.push_back(std::make_pair(D.getPair(), Fn));
    }

    if (getLangOpts().CUDA)
      CUDA().EraseUnwantedMatches(getCurFunctionDecl(/*AllowLambda=*/true),
                                  Matches);
  } else {
    // C++1y [expr.new]p22:
    //   For a non-placement allocation function, the normal deallocation
    //   function lookup is used
    //
    // Per [expr.delete]p10, this lookup prefers a member operator delete
    // without a size_t argument, but prefers a non-member operator delete
    // with a size_t where possible (which it always is in this case).
    llvm::SmallVector<UsualDeallocFnInfo, 4> BestDeallocFns;
    UsualDeallocFnInfo Selected = resolveDeallocationOverload(
        *this, FoundDelete, /*WantSize*/ FoundGlobalDelete,
        /*WantAlign*/ hasNewExtendedAlignment(*this, AllocElemType),
        &BestDeallocFns);
    if (Selected)
      Matches.push_back(std::make_pair(Selected.Found, Selected.FD));
    else {
      // If we failed to select an operator, all remaining functions are viable
      // but ambiguous.
      for (auto Fn : BestDeallocFns)
        Matches.push_back(std::make_pair(Fn.Found, Fn.FD));
    }
  }

  // C++ [expr.new]p20:
  //   [...] If the lookup finds a single matching deallocation
  //   function, that function will be called; otherwise, no
  //   deallocation function will be called.
  if (Matches.size() == 1) {
    OperatorDelete = Matches[0].second;

    // C++1z [expr.new]p23:
    //   If the lookup finds a usual deallocation function (3.7.4.2)
    //   with a parameter of type std::size_t and that function, considered
    //   as a placement deallocation function, would have been
    //   selected as a match for the allocation function, the program
    //   is ill-formed.
    if (getLangOpts().CPlusPlus11 && isPlacementNew &&
        isNonPlacementDeallocationFunction(*this, OperatorDelete)) {
      UsualDeallocFnInfo Info(*this,
                              DeclAccessPair::make(OperatorDelete, AS_public));
      // Core issue, per mail to core reflector, 2016-10-09:
      //   If this is a member operator delete, and there is a corresponding
      //   non-sized member operator delete, this isn't /really/ a sized
      //   deallocation function, it just happens to have a size_t parameter.
      bool IsSizedDelete = Info.HasSizeT;
      if (IsSizedDelete && !FoundGlobalDelete) {
        auto NonSizedDelete =
            resolveDeallocationOverload(*this, FoundDelete, /*WantSize*/false,
                                        /*WantAlign*/Info.HasAlignValT);
        if (NonSizedDelete && !NonSizedDelete.HasSizeT &&
            NonSizedDelete.HasAlignValT == Info.HasAlignValT)
          IsSizedDelete = false;
      }

      if (IsSizedDelete) {
        SourceRange R = PlaceArgs.empty()
                            ? SourceRange()
                            : SourceRange(PlaceArgs.front()->getBeginLoc(),
                                          PlaceArgs.back()->getEndLoc());
        Diag(StartLoc, diag::err_placement_new_non_placement_delete) << R;
        if (!OperatorDelete->isImplicit())
          Diag(OperatorDelete->getLocation(), diag::note_previous_decl)
              << DeleteName;
      }
    }

    CheckAllocationAccess(StartLoc, Range, FoundDelete.getNamingClass(),
                          Matches[0].first);
  } else if (!Matches.empty()) {
    // We found multiple suitable operators. Per [expr.new]p20, that means we
    // call no 'operator delete' function, but we should at least warn the user.
    // FIXME: Suppress this warning if the construction cannot throw.
    Diag(StartLoc, diag::warn_ambiguous_suitable_delete_function_found)
      << DeleteName << AllocElemType;

    for (auto &Match : Matches)
      Diag(Match.second->getLocation(),
           diag::note_member_declared_here) << DeleteName;
  }

  return false;
}

void Sema::DeclareGlobalNewDelete() {
  if (GlobalNewDeleteDeclared)
    return;

  // The implicitly declared new and delete operators
  // are not supported in OpenCL.
  if (getLangOpts().OpenCLCPlusPlus)
    return;

  // C++ [basic.stc.dynamic.general]p2:
  //   The library provides default definitions for the global allocation
  //   and deallocation functions. Some global allocation and deallocation
  //   functions are replaceable ([new.delete]); these are attached to the
  //   global module ([module.unit]).
  if (getLangOpts().CPlusPlusModules && getCurrentModule())
    PushGlobalModuleFragment(SourceLocation());

  // C++ [basic.std.dynamic]p2:
  //   [...] The following allocation and deallocation functions (18.4) are
  //   implicitly declared in global scope in each translation unit of a
  //   program
  //
  //     C++03:
  //     void* operator new(std::size_t) throw(std::bad_alloc);
  //     void* operator new[](std::size_t) throw(std::bad_alloc);
  //     void  operator delete(void*) throw();
  //     void  operator delete[](void*) throw();
  //     C++11:
  //     void* operator new(std::size_t);
  //     void* operator new[](std::size_t);
  //     void  operator delete(void*) noexcept;
  //     void  operator delete[](void*) noexcept;
  //     C++1y:
  //     void* operator new(std::size_t);
  //     void* operator new[](std::size_t);
  //     void  operator delete(void*) noexcept;
  //     void  operator delete[](void*) noexcept;
  //     void  operator delete(void*, std::size_t) noexcept;
  //     void  operator delete[](void*, std::size_t) noexcept;
  //
  //   These implicit declarations introduce only the function names operator
  //   new, operator new[], operator delete, operator delete[].
  //
  // Here, we need to refer to std::bad_alloc, so we will implicitly declare
  // "std" or "bad_alloc" as necessary to form the exception specification.
  // However, we do not make these implicit declarations visible to name
  // lookup.
  if (!StdBadAlloc && !getLangOpts().CPlusPlus11) {
    // The "std::bad_alloc" class has not yet been declared, so build it
    // implicitly.
    StdBadAlloc = CXXRecordDecl::Create(
        Context, TagTypeKind::Class, getOrCreateStdNamespace(),
        SourceLocation(), SourceLocation(),
        &PP.getIdentifierTable().get("bad_alloc"), nullptr);
    getStdBadAlloc()->setImplicit(true);

    // The implicitly declared "std::bad_alloc" should live in global module
    // fragment.
    if (TheGlobalModuleFragment) {
      getStdBadAlloc()->setModuleOwnershipKind(
          Decl::ModuleOwnershipKind::ReachableWhenImported);
      getStdBadAlloc()->setLocalOwningModule(TheGlobalModuleFragment);
    }
  }
  if (!StdAlignValT && getLangOpts().AlignedAllocation) {
    // The "std::align_val_t" enum class has not yet been declared, so build it
    // implicitly.
    auto *AlignValT = EnumDecl::Create(
        Context, getOrCreateStdNamespace(), SourceLocation(), SourceLocation(),
        &PP.getIdentifierTable().get("align_val_t"), nullptr, true, true, true);

    // The implicitly declared "std::align_val_t" should live in global module
    // fragment.
    if (TheGlobalModuleFragment) {
      AlignValT->setModuleOwnershipKind(
          Decl::ModuleOwnershipKind::ReachableWhenImported);
      AlignValT->setLocalOwningModule(TheGlobalModuleFragment);
    }

    AlignValT->setIntegerType(Context.getSizeType());
    AlignValT->setPromotionType(Context.getSizeType());
    AlignValT->setImplicit(true);

    StdAlignValT = AlignValT;
  }

  GlobalNewDeleteDeclared = true;

  QualType VoidPtr = Context.getPointerType(Context.VoidTy);
  QualType SizeT = Context.getSizeType();

  auto DeclareGlobalAllocationFunctions = [&](OverloadedOperatorKind Kind,
                                              QualType Return, QualType Param) {
    llvm::SmallVector<QualType, 3> Params;
    Params.push_back(Param);

    // Create up to four variants of the function (sized/aligned).
    bool HasSizedVariant = getLangOpts().SizedDeallocation &&
                           (Kind == OO_Delete || Kind == OO_Array_Delete);
    bool HasAlignedVariant = getLangOpts().AlignedAllocation;

    int NumSizeVariants = (HasSizedVariant ? 2 : 1);
    int NumAlignVariants = (HasAlignedVariant ? 2 : 1);
    for (int Sized = 0; Sized < NumSizeVariants; ++Sized) {
      if (Sized)
        Params.push_back(SizeT);

      for (int Aligned = 0; Aligned < NumAlignVariants; ++Aligned) {
        if (Aligned)
          Params.push_back(Context.getTypeDeclType(getStdAlignValT()));

        DeclareGlobalAllocationFunction(
            Context.DeclarationNames.getCXXOperatorName(Kind), Return, Params);

        if (Aligned)
          Params.pop_back();
      }
    }
  };

  DeclareGlobalAllocationFunctions(OO_New, VoidPtr, SizeT);
  DeclareGlobalAllocationFunctions(OO_Array_New, VoidPtr, SizeT);
  DeclareGlobalAllocationFunctions(OO_Delete, Context.VoidTy, VoidPtr);
  DeclareGlobalAllocationFunctions(OO_Array_Delete, Context.VoidTy, VoidPtr);

  if (getLangOpts().CPlusPlusModules && getCurrentModule())
    PopGlobalModuleFragment();
}

/// DeclareGlobalAllocationFunction - Declares a single implicit global
/// allocation function if it doesn't already exist.
void Sema::DeclareGlobalAllocationFunction(DeclarationName Name,
                                           QualType Return,
                                           ArrayRef<QualType> Params) {
  DeclContext *GlobalCtx = Context.getTranslationUnitDecl();

  // Check if this function is already declared.
  DeclContext::lookup_result R = GlobalCtx->lookup(Name);
  for (DeclContext::lookup_iterator Alloc = R.begin(), AllocEnd = R.end();
       Alloc != AllocEnd; ++Alloc) {
    // Only look at non-template functions, as it is the predefined,
    // non-templated allocation function we are trying to declare here.
    if (FunctionDecl *Func = dyn_cast<FunctionDecl>(*Alloc)) {
      if (Func->getNumParams() == Params.size()) {
        llvm::SmallVector<QualType, 3> FuncParams;
        for (auto *P : Func->parameters())
          FuncParams.push_back(
              Context.getCanonicalType(P->getType().getUnqualifiedType()));
        if (llvm::ArrayRef(FuncParams) == Params) {
          // Make the function visible to name lookup, even if we found it in
          // an unimported module. It either is an implicitly-declared global
          // allocation function, or is suppressing that function.
          Func->setVisibleDespiteOwningModule();
          return;
        }
      }
    }
  }

  FunctionProtoType::ExtProtoInfo EPI(Context.getDefaultCallingConvention(
      /*IsVariadic=*/false, /*IsCXXMethod=*/false, /*IsBuiltin=*/true));

  QualType BadAllocType;
  bool HasBadAllocExceptionSpec
    = (Name.getCXXOverloadedOperator() == OO_New ||
       Name.getCXXOverloadedOperator() == OO_Array_New);
  if (HasBadAllocExceptionSpec) {
    if (!getLangOpts().CPlusPlus11) {
      BadAllocType = Context.getTypeDeclType(getStdBadAlloc());
      assert(StdBadAlloc && "Must have std::bad_alloc declared");
      EPI.ExceptionSpec.Type = EST_Dynamic;
      EPI.ExceptionSpec.Exceptions = llvm::ArrayRef(BadAllocType);
    }
    if (getLangOpts().NewInfallible) {
      EPI.ExceptionSpec.Type = EST_DynamicNone;
    }
  } else {
    EPI.ExceptionSpec =
        getLangOpts().CPlusPlus11 ? EST_BasicNoexcept : EST_DynamicNone;
  }

  auto CreateAllocationFunctionDecl = [&](Attr *ExtraAttr) {
    QualType FnType = Context.getFunctionType(Return, Params, EPI);
    FunctionDecl *Alloc = FunctionDecl::Create(
        Context, GlobalCtx, SourceLocation(), SourceLocation(), Name, FnType,
        /*TInfo=*/nullptr, SC_None, getCurFPFeatures().isFPConstrained(), false,
        true);
    Alloc->setImplicit();
    // Global allocation functions should always be visible.
    Alloc->setVisibleDespiteOwningModule();

    if (HasBadAllocExceptionSpec && getLangOpts().NewInfallible &&
        !getLangOpts().CheckNew)
      Alloc->addAttr(
          ReturnsNonNullAttr::CreateImplicit(Context, Alloc->getLocation()));

    // C++ [basic.stc.dynamic.general]p2:
    //   The library provides default definitions for the global allocation
    //   and deallocation functions. Some global allocation and deallocation
    //   functions are replaceable ([new.delete]); these are attached to the
    //   global module ([module.unit]).
    //
    // In the language wording, these functions are attched to the global
    // module all the time. But in the implementation, the global module
    // is only meaningful when we're in a module unit. So here we attach
    // these allocation functions to global module conditionally.
    if (TheGlobalModuleFragment) {
      Alloc->setModuleOwnershipKind(
          Decl::ModuleOwnershipKind::ReachableWhenImported);
      Alloc->setLocalOwningModule(TheGlobalModuleFragment);
    }

    if (LangOpts.hasGlobalAllocationFunctionVisibility())
      Alloc->addAttr(VisibilityAttr::CreateImplicit(
          Context, LangOpts.hasHiddenGlobalAllocationFunctionVisibility()
                       ? VisibilityAttr::Hidden
                   : LangOpts.hasProtectedGlobalAllocationFunctionVisibility()
                       ? VisibilityAttr::Protected
                       : VisibilityAttr::Default));

    llvm::SmallVector<ParmVarDecl *, 3> ParamDecls;
    for (QualType T : Params) {
      ParamDecls.push_back(ParmVarDecl::Create(
          Context, Alloc, SourceLocation(), SourceLocation(), nullptr, T,
          /*TInfo=*/nullptr, SC_None, nullptr));
      ParamDecls.back()->setImplicit();
    }
    Alloc->setParams(ParamDecls);
    if (ExtraAttr)
      Alloc->addAttr(ExtraAttr);
    AddKnownFunctionAttributesForReplaceableGlobalAllocationFunction(Alloc);
    Context.getTranslationUnitDecl()->addDecl(Alloc);
    IdResolver.tryAddTopLevelDecl(Alloc, Name);
  };

  if (!LangOpts.CUDA)
    CreateAllocationFunctionDecl(nullptr);
  else {
    // Host and device get their own declaration so each can be
    // defined or re-declared independently.
    CreateAllocationFunctionDecl(CUDAHostAttr::CreateImplicit(Context));
    CreateAllocationFunctionDecl(CUDADeviceAttr::CreateImplicit(Context));
  }
}

FunctionDecl *Sema::FindUsualDeallocationFunction(SourceLocation StartLoc,
                                                  bool CanProvideSize,
                                                  bool Overaligned,
                                                  DeclarationName Name) {
  DeclareGlobalNewDelete();

  LookupResult FoundDelete(*this, Name, StartLoc, LookupOrdinaryName);
  LookupQualifiedName(FoundDelete, Context.getTranslationUnitDecl());

  // FIXME: It's possible for this to result in ambiguity, through a
  // user-declared variadic operator delete or the enable_if attribute. We
  // should probably not consider those cases to be usual deallocation
  // functions. But for now we just make an arbitrary choice in that case.
  auto Result = resolveDeallocationOverload(*this, FoundDelete, CanProvideSize,
                                            Overaligned);
  assert(Result.FD && "operator delete missing from global scope?");
  return Result.FD;
}

FunctionDecl *Sema::FindDeallocationFunctionForDestructor(SourceLocation Loc,
                                                          CXXRecordDecl *RD) {
  DeclarationName Name = Context.DeclarationNames.getCXXOperatorName(OO_Delete);

  FunctionDecl *OperatorDelete = nullptr;
  if (FindDeallocationFunction(Loc, RD, Name, OperatorDelete))
    return nullptr;
  if (OperatorDelete)
    return OperatorDelete;

  // If there's no class-specific operator delete, look up the global
  // non-array delete.
  return FindUsualDeallocationFunction(
      Loc, true, hasNewExtendedAlignment(*this, Context.getRecordType(RD)),
      Name);
}

bool Sema::FindDeallocationFunction(SourceLocation StartLoc, CXXRecordDecl *RD,
                                    DeclarationName Name,
                                    FunctionDecl *&Operator, bool Diagnose,
                                    bool WantSize, bool WantAligned) {
  LookupResult Found(*this, Name, StartLoc, LookupOrdinaryName);
  // Try to find operator delete/operator delete[] in class scope.
  LookupQualifiedName(Found, RD);

  if (Found.isAmbiguous())
    return true;

  Found.suppressDiagnostics();

  bool Overaligned =
      WantAligned || hasNewExtendedAlignment(*this, Context.getRecordType(RD));

  // C++17 [expr.delete]p10:
  //   If the deallocation functions have class scope, the one without a
  //   parameter of type std::size_t is selected.
  llvm::SmallVector<UsualDeallocFnInfo, 4> Matches;
  resolveDeallocationOverload(*this, Found, /*WantSize*/ WantSize,
                              /*WantAlign*/ Overaligned, &Matches);

  // If we could find an overload, use it.
  if (Matches.size() == 1) {
    Operator = cast<CXXMethodDecl>(Matches[0].FD);

    // FIXME: DiagnoseUseOfDecl?
    if (Operator->isDeleted()) {
      if (Diagnose) {
        StringLiteral *Msg = Operator->getDeletedMessage();
        Diag(StartLoc, diag::err_deleted_function_use)
            << (Msg != nullptr) << (Msg ? Msg->getString() : StringRef());
        NoteDeletedFunction(Operator);
      }
      return true;
    }

    if (CheckAllocationAccess(StartLoc, SourceRange(), Found.getNamingClass(),
                              Matches[0].Found, Diagnose) == AR_inaccessible)
      return true;

    return false;
  }

  // We found multiple suitable operators; complain about the ambiguity.
  // FIXME: The standard doesn't say to do this; it appears that the intent
  // is that this should never happen.
  if (!Matches.empty()) {
    if (Diagnose) {
      Diag(StartLoc, diag::err_ambiguous_suitable_delete_member_function_found)
        << Name << RD;
      for (auto &Match : Matches)
        Diag(Match.FD->getLocation(), diag::note_member_declared_here) << Name;
    }
    return true;
  }

  // We did find operator delete/operator delete[] declarations, but
  // none of them were suitable.
  if (!Found.empty()) {
    if (Diagnose) {
      Diag(StartLoc, diag::err_no_suitable_delete_member_function_found)
        << Name << RD;

      for (NamedDecl *D : Found)
        Diag(D->getUnderlyingDecl()->getLocation(),
             diag::note_member_declared_here) << Name;
    }
    return true;
  }

  Operator = nullptr;
  return false;
}

namespace {
/// Checks whether delete-expression, and new-expression used for
///  initializing deletee have the same array form.
class MismatchingNewDeleteDetector {
public:
  enum MismatchResult {
    /// Indicates that there is no mismatch or a mismatch cannot be proven.
    NoMismatch,
    /// Indicates that variable is initialized with mismatching form of \a new.
    VarInitMismatches,
    /// Indicates that member is initialized with mismatching form of \a new.
    MemberInitMismatches,
    /// Indicates that 1 or more constructors' definitions could not been
    /// analyzed, and they will be checked again at the end of translation unit.
    AnalyzeLater
  };

  /// \param EndOfTU True, if this is the final analysis at the end of
  /// translation unit. False, if this is the initial analysis at the point
  /// delete-expression was encountered.
  explicit MismatchingNewDeleteDetector(bool EndOfTU)
      : Field(nullptr), IsArrayForm(false), EndOfTU(EndOfTU),
        HasUndefinedConstructors(false) {}

  /// Checks whether pointee of a delete-expression is initialized with
  /// matching form of new-expression.
  ///
  /// If return value is \c VarInitMismatches or \c MemberInitMismatches at the
  /// point where delete-expression is encountered, then a warning will be
  /// issued immediately. If return value is \c AnalyzeLater at the point where
  /// delete-expression is seen, then member will be analyzed at the end of
  /// translation unit. \c AnalyzeLater is returned iff at least one constructor
  /// couldn't be analyzed. If at least one constructor initializes the member
  /// with matching type of new, the return value is \c NoMismatch.
  MismatchResult analyzeDeleteExpr(const CXXDeleteExpr *DE);
  /// Analyzes a class member.
  /// \param Field Class member to analyze.
  /// \param DeleteWasArrayForm Array form-ness of the delete-expression used
  /// for deleting the \p Field.
  MismatchResult analyzeField(FieldDecl *Field, bool DeleteWasArrayForm);
  FieldDecl *Field;
  /// List of mismatching new-expressions used for initialization of the pointee
  llvm::SmallVector<const CXXNewExpr *, 4> NewExprs;
  /// Indicates whether delete-expression was in array form.
  bool IsArrayForm;

private:
  const bool EndOfTU;
  /// Indicates that there is at least one constructor without body.
  bool HasUndefinedConstructors;
  /// Returns \c CXXNewExpr from given initialization expression.
  /// \param E Expression used for initializing pointee in delete-expression.
  /// E can be a single-element \c InitListExpr consisting of new-expression.
  const CXXNewExpr *getNewExprFromInitListOrExpr(const Expr *E);
  /// Returns whether member is initialized with mismatching form of
  /// \c new either by the member initializer or in-class initialization.
  ///
  /// If bodies of all constructors are not visible at the end of translation
  /// unit or at least one constructor initializes member with the matching
  /// form of \c new, mismatch cannot be proven, and this function will return
  /// \c NoMismatch.
  MismatchResult analyzeMemberExpr(const MemberExpr *ME);
  /// Returns whether variable is initialized with mismatching form of
  /// \c new.
  ///
  /// If variable is initialized with matching form of \c new or variable is not
  /// initialized with a \c new expression, this function will return true.
  /// If variable is initialized with mismatching form of \c new, returns false.
  /// \param D Variable to analyze.
  bool hasMatchingVarInit(const DeclRefExpr *D);
  /// Checks whether the constructor initializes pointee with mismatching
  /// form of \c new.
  ///
  /// Returns true, if member is initialized with matching form of \c new in
  /// member initializer list. Returns false, if member is initialized with the
  /// matching form of \c new in this constructor's initializer or given
  /// constructor isn't defined at the point where delete-expression is seen, or
  /// member isn't initialized by the constructor.
  bool hasMatchingNewInCtor(const CXXConstructorDecl *CD);
  /// Checks whether member is initialized with matching form of
  /// \c new in member initializer list.
  bool hasMatchingNewInCtorInit(const CXXCtorInitializer *CI);
  /// Checks whether member is initialized with mismatching form of \c new by
  /// in-class initializer.
  MismatchResult analyzeInClassInitializer();
};
}

MismatchingNewDeleteDetector::MismatchResult
MismatchingNewDeleteDetector::analyzeDeleteExpr(const CXXDeleteExpr *DE) {
  NewExprs.clear();
  assert(DE && "Expected delete-expression");
  IsArrayForm = DE->isArrayForm();
  const Expr *E = DE->getArgument()->IgnoreParenImpCasts();
  if (const MemberExpr *ME = dyn_cast<const MemberExpr>(E)) {
    return analyzeMemberExpr(ME);
  } else if (const DeclRefExpr *D = dyn_cast<const DeclRefExpr>(E)) {
    if (!hasMatchingVarInit(D))
      return VarInitMismatches;
  }
  return NoMismatch;
}

const CXXNewExpr *
MismatchingNewDeleteDetector::getNewExprFromInitListOrExpr(const Expr *E) {
  assert(E != nullptr && "Expected a valid initializer expression");
  E = E->IgnoreParenImpCasts();
  if (const InitListExpr *ILE = dyn_cast<const InitListExpr>(E)) {
    if (ILE->getNumInits() == 1)
      E = dyn_cast<const CXXNewExpr>(ILE->getInit(0)->IgnoreParenImpCasts());
  }

  return dyn_cast_or_null<const CXXNewExpr>(E);
}

bool MismatchingNewDeleteDetector::hasMatchingNewInCtorInit(
    const CXXCtorInitializer *CI) {
  const CXXNewExpr *NE = nullptr;
  if (Field == CI->getMember() &&
      (NE = getNewExprFromInitListOrExpr(CI->getInit()))) {
    if (NE->isArray() == IsArrayForm)
      return true;
    else
      NewExprs.push_back(NE);
  }
  return false;
}

bool MismatchingNewDeleteDetector::hasMatchingNewInCtor(
    const CXXConstructorDecl *CD) {
  if (CD->isImplicit())
    return false;
  const FunctionDecl *Definition = CD;
  if (!CD->isThisDeclarationADefinition() && !CD->isDefined(Definition)) {
    HasUndefinedConstructors = true;
    return EndOfTU;
  }
  for (const auto *CI : cast<const CXXConstructorDecl>(Definition)->inits()) {
    if (hasMatchingNewInCtorInit(CI))
      return true;
  }
  return false;
}

MismatchingNewDeleteDetector::MismatchResult
MismatchingNewDeleteDetector::analyzeInClassInitializer() {
  assert(Field != nullptr && "This should be called only for members");
  const Expr *InitExpr = Field->getInClassInitializer();
  if (!InitExpr)
    return EndOfTU ? NoMismatch : AnalyzeLater;
  if (const CXXNewExpr *NE = getNewExprFromInitListOrExpr(InitExpr)) {
    if (NE->isArray() != IsArrayForm) {
      NewExprs.push_back(NE);
      return MemberInitMismatches;
    }
  }
  return NoMismatch;
}

MismatchingNewDeleteDetector::MismatchResult
MismatchingNewDeleteDetector::analyzeField(FieldDecl *Field,
                                           bool DeleteWasArrayForm) {
  assert(Field != nullptr && "Analysis requires a valid class member.");
  this->Field = Field;
  IsArrayForm = DeleteWasArrayForm;
  const CXXRecordDecl *RD = cast<const CXXRecordDecl>(Field->getParent());
  for (const auto *CD : RD->ctors()) {
    if (hasMatchingNewInCtor(CD))
      return NoMismatch;
  }
  if (HasUndefinedConstructors)
    return EndOfTU ? NoMismatch : AnalyzeLater;
  if (!NewExprs.empty())
    return MemberInitMismatches;
  return Field->hasInClassInitializer() ? analyzeInClassInitializer()
                                        : NoMismatch;
}

MismatchingNewDeleteDetector::MismatchResult
MismatchingNewDeleteDetector::analyzeMemberExpr(const MemberExpr *ME) {
  assert(ME != nullptr && "Expected a member expression");
  if (FieldDecl *F = dyn_cast<FieldDecl>(ME->getMemberDecl()))
    return analyzeField(F, IsArrayForm);
  return NoMismatch;
}

bool MismatchingNewDeleteDetector::hasMatchingVarInit(const DeclRefExpr *D) {
  const CXXNewExpr *NE = nullptr;
  if (const VarDecl *VD = dyn_cast<const VarDecl>(D->getDecl())) {
    if (VD->hasInit() && (NE = getNewExprFromInitListOrExpr(VD->getInit())) &&
        NE->isArray() != IsArrayForm) {
      NewExprs.push_back(NE);
    }
  }
  return NewExprs.empty();
}

static void
DiagnoseMismatchedNewDelete(Sema &SemaRef, SourceLocation DeleteLoc,
                            const MismatchingNewDeleteDetector &Detector) {
  SourceLocation EndOfDelete = SemaRef.getLocForEndOfToken(DeleteLoc);
  FixItHint H;
  if (!Detector.IsArrayForm)
    H = FixItHint::CreateInsertion(EndOfDelete, "[]");
  else {
    SourceLocation RSquare = Lexer::findLocationAfterToken(
        DeleteLoc, tok::l_square, SemaRef.getSourceManager(),
        SemaRef.getLangOpts(), true);
    if (RSquare.isValid())
      H = FixItHint::CreateRemoval(SourceRange(EndOfDelete, RSquare));
  }
  SemaRef.Diag(DeleteLoc, diag::warn_mismatched_delete_new)
      << Detector.IsArrayForm << H;

  for (const auto *NE : Detector.NewExprs)
    SemaRef.Diag(NE->getExprLoc(), diag::note_allocated_here)
        << Detector.IsArrayForm;
}

void Sema::AnalyzeDeleteExprMismatch(const CXXDeleteExpr *DE) {
  if (Diags.isIgnored(diag::warn_mismatched_delete_new, SourceLocation()))
    return;
  MismatchingNewDeleteDetector Detector(/*EndOfTU=*/false);
  switch (Detector.analyzeDeleteExpr(DE)) {
  case MismatchingNewDeleteDetector::VarInitMismatches:
  case MismatchingNewDeleteDetector::MemberInitMismatches: {
    DiagnoseMismatchedNewDelete(*this, DE->getBeginLoc(), Detector);
    break;
  }
  case MismatchingNewDeleteDetector::AnalyzeLater: {
    DeleteExprs[Detector.Field].push_back(
        std::make_pair(DE->getBeginLoc(), DE->isArrayForm()));
    break;
  }
  case MismatchingNewDeleteDetector::NoMismatch:
    break;
  }
}

void Sema::AnalyzeDeleteExprMismatch(FieldDecl *Field, SourceLocation DeleteLoc,
                                     bool DeleteWasArrayForm) {
  MismatchingNewDeleteDetector Detector(/*EndOfTU=*/true);
  switch (Detector.analyzeField(Field, DeleteWasArrayForm)) {
  case MismatchingNewDeleteDetector::VarInitMismatches:
    llvm_unreachable("This analysis should have been done for class members.");
  case MismatchingNewDeleteDetector::AnalyzeLater:
    llvm_unreachable("Analysis cannot be postponed any point beyond end of "
                     "translation unit.");
  case MismatchingNewDeleteDetector::MemberInitMismatches:
    DiagnoseMismatchedNewDelete(*this, DeleteLoc, Detector);
    break;
  case MismatchingNewDeleteDetector::NoMismatch:
    break;
  }
}

ExprResult
Sema::ActOnCXXDelete(SourceLocation StartLoc, bool UseGlobal,
                     bool ArrayForm, Expr *ExE) {
  // C++ [expr.delete]p1:
  //   The operand shall have a pointer type, or a class type having a single
  //   non-explicit conversion function to a pointer type. The result has type
  //   void.
  //
  // DR599 amends "pointer type" to "pointer to object type" in both cases.

  ExprResult Ex = ExE;
  FunctionDecl *OperatorDelete = nullptr;
  bool ArrayFormAsWritten = ArrayForm;
  bool UsualArrayDeleteWantsSize = false;

  if (!Ex.get()->isTypeDependent()) {
    // Perform lvalue-to-rvalue cast, if needed.
    Ex = DefaultLvalueConversion(Ex.get());
    if (Ex.isInvalid())
      return ExprError();

    QualType Type = Ex.get()->getType();

    class DeleteConverter : public ContextualImplicitConverter {
    public:
      DeleteConverter() : ContextualImplicitConverter(false, true) {}

      bool match(QualType ConvType) override {
        // FIXME: If we have an operator T* and an operator void*, we must pick
        // the operator T*.
        if (const PointerType *ConvPtrType = ConvType->getAs<PointerType>())
          if (ConvPtrType->getPointeeType()->isIncompleteOrObjectType())
            return true;
        return false;
      }

      SemaDiagnosticBuilder diagnoseNoMatch(Sema &S, SourceLocation Loc,
                                            QualType T) override {
        return S.Diag(Loc, diag::err_delete_operand) << T;
      }

      SemaDiagnosticBuilder diagnoseIncomplete(Sema &S, SourceLocation Loc,
                                               QualType T) override {
        return S.Diag(Loc, diag::err_delete_incomplete_class_type) << T;
      }

      SemaDiagnosticBuilder diagnoseExplicitConv(Sema &S, SourceLocation Loc,
                                                 QualType T,
                                                 QualType ConvTy) override {
        return S.Diag(Loc, diag::err_delete_explicit_conversion) << T << ConvTy;
      }

      SemaDiagnosticBuilder noteExplicitConv(Sema &S, CXXConversionDecl *Conv,
                                             QualType ConvTy) override {
        return S.Diag(Conv->getLocation(), diag::note_delete_conversion)
          << ConvTy;
      }

      SemaDiagnosticBuilder diagnoseAmbiguous(Sema &S, SourceLocation Loc,
                                              QualType T) override {
        return S.Diag(Loc, diag::err_ambiguous_delete_operand) << T;
      }

      SemaDiagnosticBuilder noteAmbiguous(Sema &S, CXXConversionDecl *Conv,
                                          QualType ConvTy) override {
        return S.Diag(Conv->getLocation(), diag::note_delete_conversion)
          << ConvTy;
      }

      SemaDiagnosticBuilder diagnoseConversion(Sema &S, SourceLocation Loc,
                                               QualType T,
                                               QualType ConvTy) override {
        llvm_unreachable("conversion functions are permitted");
      }
    } Converter;

    Ex = PerformContextualImplicitConversion(StartLoc, Ex.get(), Converter);
    if (Ex.isInvalid())
      return ExprError();
    Type = Ex.get()->getType();
    if (!Converter.match(Type))
      // FIXME: PerformContextualImplicitConversion should return ExprError
      //        itself in this case.
      return ExprError();

    QualType Pointee = Type->castAs<PointerType>()->getPointeeType();
    QualType PointeeElem = Context.getBaseElementType(Pointee);

    if (Pointee.getAddressSpace() != LangAS::Default &&
        !getLangOpts().OpenCLCPlusPlus)
      return Diag(Ex.get()->getBeginLoc(),
                  diag::err_address_space_qualified_delete)
             << Pointee.getUnqualifiedType()
             << Pointee.getQualifiers().getAddressSpaceAttributePrintValue();

    CXXRecordDecl *PointeeRD = nullptr;
    if (Pointee->isVoidType() && !isSFINAEContext()) {
      // The C++ standard bans deleting a pointer to a non-object type, which
      // effectively bans deletion of "void*". However, most compilers support
      // this, so we treat it as a warning unless we're in a SFINAE context.
      // But we still prohibit this since C++26.
      Diag(StartLoc, LangOpts.CPlusPlus26 ? diag::err_delete_incomplete
                                          : diag::ext_delete_void_ptr_operand)
          << (LangOpts.CPlusPlus26 ? Pointee : Type)
          << Ex.get()->getSourceRange();
    } else if (Pointee->isFunctionType() || Pointee->isVoidType() ||
               Pointee->isSizelessType()) {
      return ExprError(Diag(StartLoc, diag::err_delete_operand)
        << Type << Ex.get()->getSourceRange());
    } else if (!Pointee->isDependentType()) {
      // FIXME: This can result in errors if the definition was imported from a
      // module but is hidden.
      if (!RequireCompleteType(StartLoc, Pointee,
                               LangOpts.CPlusPlus26
                                   ? diag::err_delete_incomplete
                                   : diag::warn_delete_incomplete,
                               Ex.get())) {
        if (const RecordType *RT = PointeeElem->getAs<RecordType>())
          PointeeRD = cast<CXXRecordDecl>(RT->getDecl());
      }
    }

    if (Pointee->isArrayType() && !ArrayForm) {
      Diag(StartLoc, diag::warn_delete_array_type)
          << Type << Ex.get()->getSourceRange()
          << FixItHint::CreateInsertion(getLocForEndOfToken(StartLoc), "[]");
      ArrayForm = true;
    }

    DeclarationName DeleteName = Context.DeclarationNames.getCXXOperatorName(
                                      ArrayForm ? OO_Array_Delete : OO_Delete);

    if (PointeeRD) {
      if (!UseGlobal &&
          FindDeallocationFunction(StartLoc, PointeeRD, DeleteName,
                                   OperatorDelete))
        return ExprError();

      // If we're allocating an array of records, check whether the
      // usual operator delete[] has a size_t parameter.
      if (ArrayForm) {
        // If the user specifically asked to use the global allocator,
        // we'll need to do the lookup into the class.
        if (UseGlobal)
          UsualArrayDeleteWantsSize =
            doesUsualArrayDeleteWantSize(*this, StartLoc, PointeeElem);

        // Otherwise, the usual operator delete[] should be the
        // function we just found.
        else if (isa_and_nonnull<CXXMethodDecl>(OperatorDelete))
          UsualArrayDeleteWantsSize =
            UsualDeallocFnInfo(*this,
                               DeclAccessPair::make(OperatorDelete, AS_public))
              .HasSizeT;
      }

      if (!PointeeRD->hasIrrelevantDestructor())
        if (CXXDestructorDecl *Dtor = LookupDestructor(PointeeRD)) {
          MarkFunctionReferenced(StartLoc,
                                    const_cast<CXXDestructorDecl*>(Dtor));
          if (DiagnoseUseOfDecl(Dtor, StartLoc))
            return ExprError();
        }

      CheckVirtualDtorCall(PointeeRD->getDestructor(), StartLoc,
                           /*IsDelete=*/true, /*CallCanBeVirtual=*/true,
                           /*WarnOnNonAbstractTypes=*/!ArrayForm,
                           SourceLocation());
    }

    if (!OperatorDelete) {
      if (getLangOpts().OpenCLCPlusPlus) {
        Diag(StartLoc, diag::err_openclcxx_not_supported) << "default delete";
        return ExprError();
      }

      bool IsComplete = isCompleteType(StartLoc, Pointee);
      bool CanProvideSize =
          IsComplete && (!ArrayForm || UsualArrayDeleteWantsSize ||
                         Pointee.isDestructedType());
      bool Overaligned = hasNewExtendedAlignment(*this, Pointee);

      // Look for a global declaration.
      OperatorDelete = FindUsualDeallocationFunction(StartLoc, CanProvideSize,
                                                     Overaligned, DeleteName);
    }

    MarkFunctionReferenced(StartLoc, OperatorDelete);

    // Check access and ambiguity of destructor if we're going to call it.
    // Note that this is required even for a virtual delete.
    bool IsVirtualDelete = false;
    if (PointeeRD) {
      if (CXXDestructorDecl *Dtor = LookupDestructor(PointeeRD)) {
        CheckDestructorAccess(Ex.get()->getExprLoc(), Dtor,
                              PDiag(diag::err_access_dtor) << PointeeElem);
        IsVirtualDelete = Dtor->isVirtual();
      }
    }

    DiagnoseUseOfDecl(OperatorDelete, StartLoc);

    // Convert the operand to the type of the first parameter of operator
    // delete. This is only necessary if we selected a destroying operator
    // delete that we are going to call (non-virtually); converting to void*
    // is trivial and left to AST consumers to handle.
    QualType ParamType = OperatorDelete->getParamDecl(0)->getType();
    if (!IsVirtualDelete && !ParamType->getPointeeType()->isVoidType()) {
      Qualifiers Qs = Pointee.getQualifiers();
      if (Qs.hasCVRQualifiers()) {
        // Qualifiers are irrelevant to this conversion; we're only looking
        // for access and ambiguity.
        Qs.removeCVRQualifiers();
        QualType Unqual = Context.getPointerType(
            Context.getQualifiedType(Pointee.getUnqualifiedType(), Qs));
        Ex = ImpCastExprToType(Ex.get(), Unqual, CK_NoOp);
      }
      Ex = PerformImplicitConversion(Ex.get(), ParamType, AA_Passing);
      if (Ex.isInvalid())
        return ExprError();
    }
  }

  CXXDeleteExpr *Result = new (Context) CXXDeleteExpr(
      Context.VoidTy, UseGlobal, ArrayForm, ArrayFormAsWritten,
      UsualArrayDeleteWantsSize, OperatorDelete, Ex.get(), StartLoc);
  AnalyzeDeleteExprMismatch(Result);
  return Result;
}

static bool resolveBuiltinNewDeleteOverload(Sema &S, CallExpr *TheCall,
                                            bool IsDelete,
                                            FunctionDecl *&Operator) {

  DeclarationName NewName = S.Context.DeclarationNames.getCXXOperatorName(
      IsDelete ? OO_Delete : OO_New);

  LookupResult R(S, NewName, TheCall->getBeginLoc(), Sema::LookupOrdinaryName);
  S.LookupQualifiedName(R, S.Context.getTranslationUnitDecl());
  assert(!R.empty() && "implicitly declared allocation functions not found");
  assert(!R.isAmbiguous() && "global allocation functions are ambiguous");

  // We do our own custom access checks below.
  R.suppressDiagnostics();

  SmallVector<Expr *, 8> Args(TheCall->arguments());
  OverloadCandidateSet Candidates(R.getNameLoc(),
                                  OverloadCandidateSet::CSK_Normal);
  for (LookupResult::iterator FnOvl = R.begin(), FnOvlEnd = R.end();
       FnOvl != FnOvlEnd; ++FnOvl) {
    // Even member operator new/delete are implicitly treated as
    // static, so don't use AddMemberCandidate.
    NamedDecl *D = (*FnOvl)->getUnderlyingDecl();

    if (FunctionTemplateDecl *FnTemplate = dyn_cast<FunctionTemplateDecl>(D)) {
      S.AddTemplateOverloadCandidate(FnTemplate, FnOvl.getPair(),
                                     /*ExplicitTemplateArgs=*/nullptr, Args,
                                     Candidates,
                                     /*SuppressUserConversions=*/false);
      continue;
    }

    FunctionDecl *Fn = cast<FunctionDecl>(D);
    S.AddOverloadCandidate(Fn, FnOvl.getPair(), Args, Candidates,
                           /*SuppressUserConversions=*/false);
  }

  SourceRange Range = TheCall->getSourceRange();

  // Do the resolution.
  OverloadCandidateSet::iterator Best;
  switch (Candidates.BestViableFunction(S, R.getNameLoc(), Best)) {
  case OR_Success: {
    // Got one!
    FunctionDecl *FnDecl = Best->Function;
    assert(R.getNamingClass() == nullptr &&
           "class members should not be considered");

    if (!FnDecl->isReplaceableGlobalAllocationFunction()) {
      S.Diag(R.getNameLoc(), diag::err_builtin_operator_new_delete_not_usual)
          << (IsDelete ? 1 : 0) << Range;
      S.Diag(FnDecl->getLocation(), diag::note_non_usual_function_declared_here)
          << R.getLookupName() << FnDecl->getSourceRange();
      return true;
    }

    Operator = FnDecl;
    return false;
  }

  case OR_No_Viable_Function:
    Candidates.NoteCandidates(
        PartialDiagnosticAt(R.getNameLoc(),
                            S.PDiag(diag::err_ovl_no_viable_function_in_call)
                                << R.getLookupName() << Range),
        S, OCD_AllCandidates, Args);
    return true;

  case OR_Ambiguous:
    Candidates.NoteCandidates(
        PartialDiagnosticAt(R.getNameLoc(),
                            S.PDiag(diag::err_ovl_ambiguous_call)
                                << R.getLookupName() << Range),
        S, OCD_AmbiguousCandidates, Args);
    return true;

  case OR_Deleted:
    S.DiagnoseUseOfDeletedFunction(R.getNameLoc(), Range, R.getLookupName(),
                                   Candidates, Best->Function, Args);
    return true;
  }
  llvm_unreachable("Unreachable, bad result from BestViableFunction");
}

ExprResult Sema::BuiltinOperatorNewDeleteOverloaded(ExprResult TheCallResult,
                                                    bool IsDelete) {
  CallExpr *TheCall = cast<CallExpr>(TheCallResult.get());
  if (!getLangOpts().CPlusPlus) {
    Diag(TheCall->getExprLoc(), diag::err_builtin_requires_language)
        << (IsDelete ? "__builtin_operator_delete" : "__builtin_operator_new")
        << "C++";
    return ExprError();
  }
  // CodeGen assumes it can find the global new and delete to call,
  // so ensure that they are declared.
  DeclareGlobalNewDelete();

  FunctionDecl *OperatorNewOrDelete = nullptr;
  if (resolveBuiltinNewDeleteOverload(*this, TheCall, IsDelete,
                                      OperatorNewOrDelete))
    return ExprError();
  assert(OperatorNewOrDelete && "should be found");

  DiagnoseUseOfDecl(OperatorNewOrDelete, TheCall->getExprLoc());
  MarkFunctionReferenced(TheCall->getExprLoc(), OperatorNewOrDelete);

  TheCall->setType(OperatorNewOrDelete->getReturnType());
  for (unsigned i = 0; i != TheCall->getNumArgs(); ++i) {
    QualType ParamTy = OperatorNewOrDelete->getParamDecl(i)->getType();
    InitializedEntity Entity =
        InitializedEntity::InitializeParameter(Context, ParamTy, false);
    ExprResult Arg = PerformCopyInitialization(
        Entity, TheCall->getArg(i)->getBeginLoc(), TheCall->getArg(i));
    if (Arg.isInvalid())
      return ExprError();
    TheCall->setArg(i, Arg.get());
  }
  auto Callee = dyn_cast<ImplicitCastExpr>(TheCall->getCallee());
  assert(Callee && Callee->getCastKind() == CK_BuiltinFnToFnPtr &&
         "Callee expected to be implicit cast to a builtin function pointer");
  Callee->setType(OperatorNewOrDelete->getType());

  return TheCallResult;
}

void Sema::CheckVirtualDtorCall(CXXDestructorDecl *dtor, SourceLocation Loc,
                                bool IsDelete, bool CallCanBeVirtual,
                                bool WarnOnNonAbstractTypes,
                                SourceLocation DtorLoc) {
  if (!dtor || dtor->isVirtual() || !CallCanBeVirtual || isUnevaluatedContext())
    return;

  // C++ [expr.delete]p3:
  //   In the first alternative (delete object), if the static type of the
  //   object to be deleted is different from its dynamic type, the static
  //   type shall be a base class of the dynamic type of the object to be
  //   deleted and the static type shall have a virtual destructor or the
  //   behavior is undefined.
  //
  const CXXRecordDecl *PointeeRD = dtor->getParent();
  // Note: a final class cannot be derived from, no issue there
  if (!PointeeRD->isPolymorphic() || PointeeRD->hasAttr<FinalAttr>())
    return;

  // If the superclass is in a system header, there's nothing that can be done.
  // The `delete` (where we emit the warning) can be in a system header,
  // what matters for this warning is where the deleted type is defined.
  if (getSourceManager().isInSystemHeader(PointeeRD->getLocation()))
    return;

  QualType ClassType = dtor->getFunctionObjectParameterType();
  if (PointeeRD->isAbstract()) {
    // If the class is abstract, we warn by default, because we're
    // sure the code has undefined behavior.
    Diag(Loc, diag::warn_delete_abstract_non_virtual_dtor) << (IsDelete ? 0 : 1)
                                                           << ClassType;
  } else if (WarnOnNonAbstractTypes) {
    // Otherwise, if this is not an array delete, it's a bit suspect,
    // but not necessarily wrong.
    Diag(Loc, diag::warn_delete_non_virtual_dtor) << (IsDelete ? 0 : 1)
                                                  << ClassType;
  }
  if (!IsDelete) {
    std::string TypeStr;
    ClassType.getAsStringInternal(TypeStr, getPrintingPolicy());
    Diag(DtorLoc, diag::note_delete_non_virtual)
        << FixItHint::CreateInsertion(DtorLoc, TypeStr + "::");
  }
}

Sema::ConditionResult Sema::ActOnConditionVariable(Decl *ConditionVar,
                                                   SourceLocation StmtLoc,
                                                   ConditionKind CK) {
  ExprResult E =
      CheckConditionVariable(cast<VarDecl>(ConditionVar), StmtLoc, CK);
  if (E.isInvalid())
    return ConditionError();
  return ConditionResult(*this, ConditionVar, MakeFullExpr(E.get(), StmtLoc),
                         CK == ConditionKind::ConstexprIf);
}

ExprResult Sema::CheckConditionVariable(VarDecl *ConditionVar,
                                        SourceLocation StmtLoc,
                                        ConditionKind CK) {
  if (ConditionVar->isInvalidDecl())
    return ExprError();

  QualType T = ConditionVar->getType();

  // C++ [stmt.select]p2:
  //   The declarator shall not specify a function or an array.
  if (T->isFunctionType())
    return ExprError(Diag(ConditionVar->getLocation(),
                          diag::err_invalid_use_of_function_type)
                       << ConditionVar->getSourceRange());
  else if (T->isArrayType())
    return ExprError(Diag(ConditionVar->getLocation(),
                          diag::err_invalid_use_of_array_type)
                     << ConditionVar->getSourceRange());

  ExprResult Condition = BuildDeclRefExpr(
      ConditionVar, ConditionVar->getType().getNonReferenceType(), VK_LValue,
      ConditionVar->getLocation());

  switch (CK) {
  case ConditionKind::Boolean:
    return CheckBooleanCondition(StmtLoc, Condition.get());

  case ConditionKind::ConstexprIf:
    return CheckBooleanCondition(StmtLoc, Condition.get(), true);

  case ConditionKind::Switch:
    return CheckSwitchCondition(StmtLoc, Condition.get());
  }

  llvm_unreachable("unexpected condition kind");
}

ExprResult Sema::CheckCXXBooleanCondition(Expr *CondExpr, bool IsConstexpr) {
  // C++11 6.4p4:
  // The value of a condition that is an initialized declaration in a statement
  // other than a switch statement is the value of the declared variable
  // implicitly converted to type bool. If that conversion is ill-formed, the
  // program is ill-formed.
  // The value of a condition that is an expression is the value of the
  // expression, implicitly converted to bool.
  //
  // C++23 8.5.2p2
  // If the if statement is of the form if constexpr, the value of the condition
  // is contextually converted to bool and the converted expression shall be
  // a constant expression.
  //

  ExprResult E = PerformContextuallyConvertToBool(CondExpr);
  if (!IsConstexpr || E.isInvalid() || E.get()->isValueDependent())
    return E;

  // FIXME: Return this value to the caller so they don't need to recompute it.
  llvm::APSInt Cond;
  E = VerifyIntegerConstantExpression(
      E.get(), &Cond,
      diag::err_constexpr_if_condition_expression_is_not_constant);
  return E;
}

bool
Sema::IsStringLiteralToNonConstPointerConversion(Expr *From, QualType ToType) {
  // Look inside the implicit cast, if it exists.
  if (ImplicitCastExpr *Cast = dyn_cast<ImplicitCastExpr>(From))
    From = Cast->getSubExpr();

  // A string literal (2.13.4) that is not a wide string literal can
  // be converted to an rvalue of type "pointer to char"; a wide
  // string literal can be converted to an rvalue of type "pointer
  // to wchar_t" (C++ 4.2p2).
  if (StringLiteral *StrLit = dyn_cast<StringLiteral>(From->IgnoreParens()))
    if (const PointerType *ToPtrType = ToType->getAs<PointerType>())
      if (const BuiltinType *ToPointeeType
          = ToPtrType->getPointeeType()->getAs<BuiltinType>()) {
        // This conversion is considered only when there is an
        // explicit appropriate pointer target type (C++ 4.2p2).
        if (!ToPtrType->getPointeeType().hasQualifiers()) {
          switch (StrLit->getKind()) {
          case StringLiteralKind::UTF8:
          case StringLiteralKind::UTF16:
          case StringLiteralKind::UTF32:
            // We don't allow UTF literals to be implicitly converted
            break;
          case StringLiteralKind::Ordinary:
            return (ToPointeeType->getKind() == BuiltinType::Char_U ||
                    ToPointeeType->getKind() == BuiltinType::Char_S);
          case StringLiteralKind::Wide:
            return Context.typesAreCompatible(Context.getWideCharType(),
                                              QualType(ToPointeeType, 0));
          case StringLiteralKind::Unevaluated:
            assert(false && "Unevaluated string literal in expression");
            break;
          }
        }
      }

  return false;
}

static ExprResult BuildCXXCastArgument(Sema &S,
                                       SourceLocation CastLoc,
                                       QualType Ty,
                                       CastKind Kind,
                                       CXXMethodDecl *Method,
                                       DeclAccessPair FoundDecl,
                                       bool HadMultipleCandidates,
                                       Expr *From) {
  switch (Kind) {
  default: llvm_unreachable("Unhandled cast kind!");
  case CK_ConstructorConversion: {
    CXXConstructorDecl *Constructor = cast<CXXConstructorDecl>(Method);
    SmallVector<Expr*, 8> ConstructorArgs;

    if (S.RequireNonAbstractType(CastLoc, Ty,
                                 diag::err_allocation_of_abstract_type))
      return ExprError();

    if (S.CompleteConstructorCall(Constructor, Ty, From, CastLoc,
                                  ConstructorArgs))
      return ExprError();

    S.CheckConstructorAccess(CastLoc, Constructor, FoundDecl,
                             InitializedEntity::InitializeTemporary(Ty));
    if (S.DiagnoseUseOfDecl(Method, CastLoc))
      return ExprError();

    ExprResult Result = S.BuildCXXConstructExpr(
        CastLoc, Ty, FoundDecl, cast<CXXConstructorDecl>(Method),
        ConstructorArgs, HadMultipleCandidates,
        /*ListInit*/ false, /*StdInitListInit*/ false, /*ZeroInit*/ false,
        CXXConstructionKind::Complete, SourceRange());
    if (Result.isInvalid())
      return ExprError();

    return S.MaybeBindToTemporary(Result.getAs<Expr>());
  }

  case CK_UserDefinedConversion: {
    assert(!From->getType()->isPointerType() && "Arg can't have pointer type!");

    S.CheckMemberOperatorAccess(CastLoc, From, /*arg*/ nullptr, FoundDecl);
    if (S.DiagnoseUseOfDecl(Method, CastLoc))
      return ExprError();

    // Create an implicit call expr that calls it.
    CXXConversionDecl *Conv = cast<CXXConversionDecl>(Method);
    ExprResult Result = S.BuildCXXMemberCallExpr(From, FoundDecl, Conv,
                                                 HadMultipleCandidates);
    if (Result.isInvalid())
      return ExprError();
    // Record usage of conversion in an implicit cast.
    Result = ImplicitCastExpr::Create(S.Context, Result.get()->getType(),
                                      CK_UserDefinedConversion, Result.get(),
                                      nullptr, Result.get()->getValueKind(),
                                      S.CurFPFeatureOverrides());

    return S.MaybeBindToTemporary(Result.get());
  }
  }
}

ExprResult
Sema::PerformImplicitConversion(Expr *From, QualType ToType,
                                const ImplicitConversionSequence &ICS,
                                AssignmentAction Action,
                                CheckedConversionKind CCK) {
  // C++ [over.match.oper]p7: [...] operands of class type are converted [...]
  if (CCK == CheckedConversionKind::ForBuiltinOverloadedOp &&
      !From->getType()->isRecordType())
    return From;

  switch (ICS.getKind()) {
  case ImplicitConversionSequence::StandardConversion: {
    ExprResult Res = PerformImplicitConversion(From, ToType, ICS.Standard,
                                               Action, CCK);
    if (Res.isInvalid())
      return ExprError();
    From = Res.get();
    break;
  }

  case ImplicitConversionSequence::UserDefinedConversion: {

      FunctionDecl *FD = ICS.UserDefined.ConversionFunction;
      CastKind CastKind;
      QualType BeforeToType;
      assert(FD && "no conversion function for user-defined conversion seq");
      if (const CXXConversionDecl *Conv = dyn_cast<CXXConversionDecl>(FD)) {
        CastKind = CK_UserDefinedConversion;

        // If the user-defined conversion is specified by a conversion function,
        // the initial standard conversion sequence converts the source type to
        // the implicit object parameter of the conversion function.
        BeforeToType = Context.getTagDeclType(Conv->getParent());
      } else {
        const CXXConstructorDecl *Ctor = cast<CXXConstructorDecl>(FD);
        CastKind = CK_ConstructorConversion;
        // Do no conversion if dealing with ... for the first conversion.
        if (!ICS.UserDefined.EllipsisConversion) {
          // If the user-defined conversion is specified by a constructor, the
          // initial standard conversion sequence converts the source type to
          // the type required by the argument of the constructor
          BeforeToType = Ctor->getParamDecl(0)->getType().getNonReferenceType();
        }
      }
      // Watch out for ellipsis conversion.
      if (!ICS.UserDefined.EllipsisConversion) {
        ExprResult Res =
          PerformImplicitConversion(From, BeforeToType,
                                    ICS.UserDefined.Before, AA_Converting,
                                    CCK);
        if (Res.isInvalid())
          return ExprError();
        From = Res.get();
      }

      ExprResult CastArg = BuildCXXCastArgument(
          *this, From->getBeginLoc(), ToType.getNonReferenceType(), CastKind,
          cast<CXXMethodDecl>(FD), ICS.UserDefined.FoundConversionFunction,
          ICS.UserDefined.HadMultipleCandidates, From);

      if (CastArg.isInvalid())
        return ExprError();

      From = CastArg.get();

      // C++ [over.match.oper]p7:
      //   [...] the second standard conversion sequence of a user-defined
      //   conversion sequence is not applied.
      if (CCK == CheckedConversionKind::ForBuiltinOverloadedOp)
        return From;

      return PerformImplicitConversion(From, ToType, ICS.UserDefined.After,
                                       AA_Converting, CCK);
  }

  case ImplicitConversionSequence::AmbiguousConversion:
    ICS.DiagnoseAmbiguousConversion(*this, From->getExprLoc(),
                          PDiag(diag::err_typecheck_ambiguous_condition)
                            << From->getSourceRange());
    return ExprError();

  case ImplicitConversionSequence::EllipsisConversion:
  case ImplicitConversionSequence::StaticObjectArgumentConversion:
    llvm_unreachable("bad conversion");

  case ImplicitConversionSequence::BadConversion:
    Sema::AssignConvertType ConvTy =
        CheckAssignmentConstraints(From->getExprLoc(), ToType, From->getType());
    bool Diagnosed = DiagnoseAssignmentResult(
        ConvTy == Compatible ? Incompatible : ConvTy, From->getExprLoc(),
        ToType, From->getType(), From, Action);
    assert(Diagnosed && "failed to diagnose bad conversion"); (void)Diagnosed;
    return ExprError();
  }

  // Everything went well.
  return From;
}

// adjustVectorType - Compute the intermediate cast type casting elements of the
// from type to the elements of the to type without resizing the vector.
static QualType adjustVectorType(ASTContext &Context, QualType FromTy,
                                 QualType ToType, QualType *ElTy = nullptr) {
  auto *ToVec = ToType->castAs<VectorType>();
  QualType ElType = ToVec->getElementType();
  if (ElTy)
    *ElTy = ElType;
  if (!FromTy->isVectorType())
    return ElType;
  auto *FromVec = FromTy->castAs<VectorType>();
  return Context.getExtVectorType(ElType, FromVec->getNumElements());
}

ExprResult
Sema::PerformImplicitConversion(Expr *From, QualType ToType,
                                const StandardConversionSequence& SCS,
                                AssignmentAction Action,
                                CheckedConversionKind CCK) {
  bool CStyle = (CCK == CheckedConversionKind::CStyleCast ||
                 CCK == CheckedConversionKind::FunctionalCast);

  // Overall FIXME: we are recomputing too many types here and doing far too
  // much extra work. What this means is that we need to keep track of more
  // information that is computed when we try the implicit conversion initially,
  // so that we don't need to recompute anything here.
  QualType FromType = From->getType();

  if (SCS.CopyConstructor) {
    // FIXME: When can ToType be a reference type?
    assert(!ToType->isReferenceType());
    if (SCS.Second == ICK_Derived_To_Base) {
      SmallVector<Expr*, 8> ConstructorArgs;
      if (CompleteConstructorCall(
              cast<CXXConstructorDecl>(SCS.CopyConstructor), ToType, From,
              /*FIXME:ConstructLoc*/ SourceLocation(), ConstructorArgs))
        return ExprError();
      return BuildCXXConstructExpr(
          /*FIXME:ConstructLoc*/ SourceLocation(), ToType,
          SCS.FoundCopyConstructor, SCS.CopyConstructor, ConstructorArgs,
          /*HadMultipleCandidates*/ false,
          /*ListInit*/ false, /*StdInitListInit*/ false, /*ZeroInit*/ false,
          CXXConstructionKind::Complete, SourceRange());
    }
    return BuildCXXConstructExpr(
        /*FIXME:ConstructLoc*/ SourceLocation(), ToType,
        SCS.FoundCopyConstructor, SCS.CopyConstructor, From,
        /*HadMultipleCandidates*/ false,
        /*ListInit*/ false, /*StdInitListInit*/ false, /*ZeroInit*/ false,
        CXXConstructionKind::Complete, SourceRange());
  }

  // Resolve overloaded function references.
  if (Context.hasSameType(FromType, Context.OverloadTy)) {
    DeclAccessPair Found;
    FunctionDecl *Fn = ResolveAddressOfOverloadedFunction(From, ToType,
                                                          true, Found);
    if (!Fn)
      return ExprError();

    if (DiagnoseUseOfDecl(Fn, From->getBeginLoc()))
      return ExprError();

    ExprResult Res = FixOverloadedFunctionReference(From, Found, Fn);
    if (Res.isInvalid())
      return ExprError();

    // We might get back another placeholder expression if we resolved to a
    // builtin.
    Res = CheckPlaceholderExpr(Res.get());
    if (Res.isInvalid())
      return ExprError();

    From = Res.get();
    FromType = From->getType();
  }

  // If we're converting to an atomic type, first convert to the corresponding
  // non-atomic type.
  QualType ToAtomicType;
  if (const AtomicType *ToAtomic = ToType->getAs<AtomicType>()) {
    ToAtomicType = ToType;
    ToType = ToAtomic->getValueType();
  }

  QualType InitialFromType = FromType;
  // Perform the first implicit conversion.
  switch (SCS.First) {
  case ICK_Identity:
    if (const AtomicType *FromAtomic = FromType->getAs<AtomicType>()) {
      FromType = FromAtomic->getValueType().getUnqualifiedType();
      From = ImplicitCastExpr::Create(Context, FromType, CK_AtomicToNonAtomic,
                                      From, /*BasePath=*/nullptr, VK_PRValue,
                                      FPOptionsOverride());
    }
    break;

  case ICK_Lvalue_To_Rvalue: {
    assert(From->getObjectKind() != OK_ObjCProperty);
    ExprResult FromRes = DefaultLvalueConversion(From);
    if (FromRes.isInvalid())
      return ExprError();

    From = FromRes.get();
    FromType = From->getType();
    break;
  }

  case ICK_Array_To_Pointer:
    FromType = Context.getArrayDecayedType(FromType);
    From = ImpCastExprToType(From, FromType, CK_ArrayToPointerDecay, VK_PRValue,
                             /*BasePath=*/nullptr, CCK)
               .get();
    break;

  case ICK_HLSL_Array_RValue:
    FromType = Context.getArrayParameterType(FromType);
    From = ImpCastExprToType(From, FromType, CK_HLSLArrayRValue, VK_PRValue,
                             /*BasePath=*/nullptr, CCK)
               .get();
    break;

  case ICK_Function_To_Pointer:
    FromType = Context.getPointerType(FromType);
    From = ImpCastExprToType(From, FromType, CK_FunctionToPointerDecay,
                             VK_PRValue, /*BasePath=*/nullptr, CCK)
               .get();
    break;

  default:
    llvm_unreachable("Improper first standard conversion");
  }

  // Perform the second implicit conversion
  switch (SCS.Second) {
  case ICK_Identity:
    // C++ [except.spec]p5:
    //   [For] assignment to and initialization of pointers to functions,
    //   pointers to member functions, and references to functions: the
    //   target entity shall allow at least the exceptions allowed by the
    //   source value in the assignment or initialization.
    switch (Action) {
    case AA_Assigning:
    case AA_Initializing:
      // Note, function argument passing and returning are initialization.
    case AA_Passing:
    case AA_Returning:
    case AA_Sending:
    case AA_Passing_CFAudited:
      if (CheckExceptionSpecCompatibility(From, ToType))
        return ExprError();
      break;

    case AA_Casting:
    case AA_Converting:
      // Casts and implicit conversions are not initialization, so are not
      // checked for exception specification mismatches.
      break;
    }
    // Nothing else to do.
    break;

  case ICK_Integral_Promotion:
  case ICK_Integral_Conversion: {
    QualType ElTy = ToType;
    QualType StepTy = ToType;
    if (ToType->isVectorType())
      StepTy = adjustVectorType(Context, FromType, ToType, &ElTy);
    if (ElTy->isBooleanType()) {
      assert(FromType->castAs<EnumType>()->getDecl()->isFixed() &&
             SCS.Second == ICK_Integral_Promotion &&
             "only enums with fixed underlying type can promote to bool");
      From = ImpCastExprToType(From, StepTy, CK_IntegralToBoolean, VK_PRValue,
                               /*BasePath=*/nullptr, CCK)
                 .get();
    } else {
      From = ImpCastExprToType(From, StepTy, CK_IntegralCast, VK_PRValue,
                               /*BasePath=*/nullptr, CCK)
                 .get();
    }
    break;
  }

  case ICK_Floating_Promotion:
  case ICK_Floating_Conversion: {
    QualType StepTy = ToType;
    if (ToType->isVectorType())
      StepTy = adjustVectorType(Context, FromType, ToType);
    From = ImpCastExprToType(From, StepTy, CK_FloatingCast, VK_PRValue,
                             /*BasePath=*/nullptr, CCK)
               .get();
    break;
  }

  case ICK_Complex_Promotion:
  case ICK_Complex_Conversion: {
    QualType FromEl = From->getType()->castAs<ComplexType>()->getElementType();
    QualType ToEl = ToType->castAs<ComplexType>()->getElementType();
    CastKind CK;
    if (FromEl->isRealFloatingType()) {
      if (ToEl->isRealFloatingType())
        CK = CK_FloatingComplexCast;
      else
        CK = CK_FloatingComplexToIntegralComplex;
    } else if (ToEl->isRealFloatingType()) {
      CK = CK_IntegralComplexToFloatingComplex;
    } else {
      CK = CK_IntegralComplexCast;
    }
    From = ImpCastExprToType(From, ToType, CK, VK_PRValue, /*BasePath=*/nullptr,
                             CCK)
               .get();
    break;
  }

  case ICK_Floating_Integral: {
    QualType ElTy = ToType;
    QualType StepTy = ToType;
    if (ToType->isVectorType())
      StepTy = adjustVectorType(Context, FromType, ToType, &ElTy);
    if (ElTy->isRealFloatingType())
      From = ImpCastExprToType(From, StepTy, CK_IntegralToFloating, VK_PRValue,
                               /*BasePath=*/nullptr, CCK)
                 .get();
    else
      From = ImpCastExprToType(From, StepTy, CK_FloatingToIntegral, VK_PRValue,
                               /*BasePath=*/nullptr, CCK)
                 .get();
    break;
  }

  case ICK_Fixed_Point_Conversion:
    assert((FromType->isFixedPointType() || ToType->isFixedPointType()) &&
           "Attempting implicit fixed point conversion without a fixed "
           "point operand");
    if (FromType->isFloatingType())
      From = ImpCastExprToType(From, ToType, CK_FloatingToFixedPoint,
                               VK_PRValue,
                               /*BasePath=*/nullptr, CCK).get();
    else if (ToType->isFloatingType())
      From = ImpCastExprToType(From, ToType, CK_FixedPointToFloating,
                               VK_PRValue,
                               /*BasePath=*/nullptr, CCK).get();
    else if (FromType->isIntegralType(Context))
      From = ImpCastExprToType(From, ToType, CK_IntegralToFixedPoint,
                               VK_PRValue,
                               /*BasePath=*/nullptr, CCK).get();
    else if (ToType->isIntegralType(Context))
      From = ImpCastExprToType(From, ToType, CK_FixedPointToIntegral,
                               VK_PRValue,
                               /*BasePath=*/nullptr, CCK).get();
    else if (ToType->isBooleanType())
      From = ImpCastExprToType(From, ToType, CK_FixedPointToBoolean,
                               VK_PRValue,
                               /*BasePath=*/nullptr, CCK).get();
    else
      From = ImpCastExprToType(From, ToType, CK_FixedPointCast,
                               VK_PRValue,
                               /*BasePath=*/nullptr, CCK).get();
    break;

  case ICK_Compatible_Conversion:
    From = ImpCastExprToType(From, ToType, CK_NoOp, From->getValueKind(),
                             /*BasePath=*/nullptr, CCK).get();
    break;

  case ICK_Writeback_Conversion:
  case ICK_Pointer_Conversion: {
    if (SCS.IncompatibleObjC && Action != AA_Casting) {
      // Diagnose incompatible Objective-C conversions
      if (Action == AA_Initializing || Action == AA_Assigning)
        Diag(From->getBeginLoc(),
             diag::ext_typecheck_convert_incompatible_pointer)
            << ToType << From->getType() << Action << From->getSourceRange()
            << 0;
      else
        Diag(From->getBeginLoc(),
             diag::ext_typecheck_convert_incompatible_pointer)
            << From->getType() << ToType << Action << From->getSourceRange()
            << 0;

      if (From->getType()->isObjCObjectPointerType() &&
          ToType->isObjCObjectPointerType())
        ObjC().EmitRelatedResultTypeNote(From);
    } else if (getLangOpts().allowsNonTrivialObjCLifetimeQualifiers() &&
               !ObjC().CheckObjCARCUnavailableWeakConversion(ToType,
                                                             From->getType())) {
      if (Action == AA_Initializing)
        Diag(From->getBeginLoc(), diag::err_arc_weak_unavailable_assign);
      else
        Diag(From->getBeginLoc(), diag::err_arc_convesion_of_weak_unavailable)
            << (Action == AA_Casting) << From->getType() << ToType
            << From->getSourceRange();
    }

    // Defer address space conversion to the third conversion.
    QualType FromPteeType = From->getType()->getPointeeType();
    QualType ToPteeType = ToType->getPointeeType();
    QualType NewToType = ToType;
    if (!FromPteeType.isNull() && !ToPteeType.isNull() &&
        FromPteeType.getAddressSpace() != ToPteeType.getAddressSpace()) {
      NewToType = Context.removeAddrSpaceQualType(ToPteeType);
      NewToType = Context.getAddrSpaceQualType(NewToType,
                                               FromPteeType.getAddressSpace());
      if (ToType->isObjCObjectPointerType())
        NewToType = Context.getObjCObjectPointerType(NewToType);
      else if (ToType->isBlockPointerType())
        NewToType = Context.getBlockPointerType(NewToType);
      else
        NewToType = Context.getPointerType(NewToType);
    }

    CastKind Kind;
    CXXCastPath BasePath;
    if (CheckPointerConversion(From, NewToType, Kind, BasePath, CStyle))
      return ExprError();

    // Make sure we extend blocks if necessary.
    // FIXME: doing this here is really ugly.
    if (Kind == CK_BlockPointerToObjCPointerCast) {
      ExprResult E = From;
      (void)ObjC().PrepareCastToObjCObjectPointer(E);
      From = E.get();
    }
    if (getLangOpts().allowsNonTrivialObjCLifetimeQualifiers())
      ObjC().CheckObjCConversion(SourceRange(), NewToType, From, CCK);
    From = ImpCastExprToType(From, NewToType, Kind, VK_PRValue, &BasePath, CCK)
               .get();
    break;
  }

  case ICK_Pointer_Member: {
    CastKind Kind;
    CXXCastPath BasePath;
    if (CheckMemberPointerConversion(From, ToType, Kind, BasePath, CStyle))
      return ExprError();
    if (CheckExceptionSpecCompatibility(From, ToType))
      return ExprError();

    // We may not have been able to figure out what this member pointer resolved
    // to up until this exact point.  Attempt to lock-in it's inheritance model.
    if (Context.getTargetInfo().getCXXABI().isMicrosoft()) {
      (void)isCompleteType(From->getExprLoc(), From->getType());
      (void)isCompleteType(From->getExprLoc(), ToType);
    }

    From =
        ImpCastExprToType(From, ToType, Kind, VK_PRValue, &BasePath, CCK).get();
    break;
  }

  case ICK_Boolean_Conversion: {
    // Perform half-to-boolean conversion via float.
    if (From->getType()->isHalfType()) {
      From = ImpCastExprToType(From, Context.FloatTy, CK_FloatingCast).get();
      FromType = Context.FloatTy;
    }
    QualType ElTy = FromType;
    QualType StepTy = ToType;
    if (FromType->isVectorType()) {
      if (getLangOpts().HLSL)
        StepTy = adjustVectorType(Context, FromType, ToType);
      ElTy = FromType->castAs<VectorType>()->getElementType();
    }

    From = ImpCastExprToType(From, StepTy, ScalarTypeToBooleanCastKind(ElTy),
                             VK_PRValue,
                             /*BasePath=*/nullptr, CCK)
               .get();
    break;
  }

  case ICK_Derived_To_Base: {
    CXXCastPath BasePath;
    if (CheckDerivedToBaseConversion(
            From->getType(), ToType.getNonReferenceType(), From->getBeginLoc(),
            From->getSourceRange(), &BasePath, CStyle))
      return ExprError();

    From = ImpCastExprToType(From, ToType.getNonReferenceType(),
                      CK_DerivedToBase, From->getValueKind(),
                      &BasePath, CCK).get();
    break;
  }

  case ICK_Vector_Conversion:
    From = ImpCastExprToType(From, ToType, CK_BitCast, VK_PRValue,
                             /*BasePath=*/nullptr, CCK)
               .get();
    break;

  case ICK_SVE_Vector_Conversion:
  case ICK_RVV_Vector_Conversion:
    From = ImpCastExprToType(From, ToType, CK_BitCast, VK_PRValue,
                             /*BasePath=*/nullptr, CCK)
               .get();
    break;

  case ICK_Vector_Splat: {
    // Vector splat from any arithmetic type to a vector.
    Expr *Elem = prepareVectorSplat(ToType, From).get();
    From = ImpCastExprToType(Elem, ToType, CK_VectorSplat, VK_PRValue,
                             /*BasePath=*/nullptr, CCK)
               .get();
    break;
  }

  case ICK_Complex_Real:
    // Case 1.  x -> _Complex y
    if (const ComplexType *ToComplex = ToType->getAs<ComplexType>()) {
      QualType ElType = ToComplex->getElementType();
      bool isFloatingComplex = ElType->isRealFloatingType();

      // x -> y
      if (Context.hasSameUnqualifiedType(ElType, From->getType())) {
        // do nothing
      } else if (From->getType()->isRealFloatingType()) {
        From = ImpCastExprToType(From, ElType,
                isFloatingComplex ? CK_FloatingCast : CK_FloatingToIntegral).get();
      } else {
        assert(From->getType()->isIntegerType());
        From = ImpCastExprToType(From, ElType,
                isFloatingComplex ? CK_IntegralToFloating : CK_IntegralCast).get();
      }
      // y -> _Complex y
      From = ImpCastExprToType(From, ToType,
                   isFloatingComplex ? CK_FloatingRealToComplex
                                     : CK_IntegralRealToComplex).get();

    // Case 2.  _Complex x -> y
    } else {
      auto *FromComplex = From->getType()->castAs<ComplexType>();
      QualType ElType = FromComplex->getElementType();
      bool isFloatingComplex = ElType->isRealFloatingType();

      // _Complex x -> x
      From = ImpCastExprToType(From, ElType,
                               isFloatingComplex ? CK_FloatingComplexToReal
                                                 : CK_IntegralComplexToReal,
                               VK_PRValue, /*BasePath=*/nullptr, CCK)
                 .get();

      // x -> y
      if (Context.hasSameUnqualifiedType(ElType, ToType)) {
        // do nothing
      } else if (ToType->isRealFloatingType()) {
        From = ImpCastExprToType(From, ToType,
                                 isFloatingComplex ? CK_FloatingCast
                                                   : CK_IntegralToFloating,
                                 VK_PRValue, /*BasePath=*/nullptr, CCK)
                   .get();
      } else {
        assert(ToType->isIntegerType());
        From = ImpCastExprToType(From, ToType,
                                 isFloatingComplex ? CK_FloatingToIntegral
                                                   : CK_IntegralCast,
                                 VK_PRValue, /*BasePath=*/nullptr, CCK)
                   .get();
      }
    }
    break;

  case ICK_Block_Pointer_Conversion: {
    LangAS AddrSpaceL =
        ToType->castAs<BlockPointerType>()->getPointeeType().getAddressSpace();
    LangAS AddrSpaceR =
        FromType->castAs<BlockPointerType>()->getPointeeType().getAddressSpace();
    assert(Qualifiers::isAddressSpaceSupersetOf(AddrSpaceL, AddrSpaceR) &&
           "Invalid cast");
    CastKind Kind =
        AddrSpaceL != AddrSpaceR ? CK_AddressSpaceConversion : CK_BitCast;
    From = ImpCastExprToType(From, ToType.getUnqualifiedType(), Kind,
                             VK_PRValue, /*BasePath=*/nullptr, CCK)
               .get();
    break;
  }

  case ICK_TransparentUnionConversion: {
    ExprResult FromRes = From;
    Sema::AssignConvertType ConvTy =
      CheckTransparentUnionArgumentConstraints(ToType, FromRes);
    if (FromRes.isInvalid())
      return ExprError();
    From = FromRes.get();
    assert ((ConvTy == Sema::Compatible) &&
            "Improper transparent union conversion");
    (void)ConvTy;
    break;
  }

  case ICK_Zero_Event_Conversion:
  case ICK_Zero_Queue_Conversion:
    From = ImpCastExprToType(From, ToType,
                             CK_ZeroToOCLOpaqueType,
                             From->getValueKind()).get();
    break;

  case ICK_Lvalue_To_Rvalue:
  case ICK_Array_To_Pointer:
  case ICK_Function_To_Pointer:
  case ICK_Function_Conversion:
  case ICK_Qualification:
  case ICK_Num_Conversion_Kinds:
  case ICK_C_Only_Conversion:
  case ICK_Incompatible_Pointer_Conversion:
  case ICK_HLSL_Array_RValue:
  case ICK_HLSL_Vector_Truncation:
  case ICK_HLSL_Vector_Splat:
    llvm_unreachable("Improper second standard conversion");
  }

  if (SCS.Dimension != ICK_Identity) {
    // If SCS.Element is not ICK_Identity the To and From types must be HLSL
    // vectors or matrices.

    // TODO: Support HLSL matrices.
    assert((!From->getType()->isMatrixType() && !ToType->isMatrixType()) &&
           "Dimension conversion for matrix types is not implemented yet.");
    assert(ToType->isVectorType() &&
           "Dimension conversion is only supported for vector types.");
    switch (SCS.Dimension) {
    case ICK_HLSL_Vector_Splat: {
      // Vector splat from any arithmetic type to a vector.
      Expr *Elem = prepareVectorSplat(ToType, From).get();
      From = ImpCastExprToType(Elem, ToType, CK_VectorSplat, VK_PRValue,
                               /*BasePath=*/nullptr, CCK)
                 .get();
      break;
    }
    case ICK_HLSL_Vector_Truncation: {
      // Note: HLSL built-in vectors are ExtVectors. Since this truncates a
      // vector to a smaller vector, this can only operate on arguments where
      // the source and destination types are ExtVectors.
      assert(From->getType()->isExtVectorType() && ToType->isExtVectorType() &&
             "HLSL vector truncation should only apply to ExtVectors");
      auto *FromVec = From->getType()->castAs<VectorType>();
      auto *ToVec = ToType->castAs<VectorType>();
      QualType ElType = FromVec->getElementType();
      QualType TruncTy =
          Context.getExtVectorType(ElType, ToVec->getNumElements());
      From = ImpCastExprToType(From, TruncTy, CK_HLSLVectorTruncation,
                               From->getValueKind())
                 .get();
      break;
    }
    case ICK_Identity:
    default:
      llvm_unreachable("Improper element standard conversion");
    }
  }

  switch (SCS.Third) {
  case ICK_Identity:
    // Nothing to do.
    break;

  case ICK_Function_Conversion:
    // If both sides are functions (or pointers/references to them), there could
    // be incompatible exception declarations.
    if (CheckExceptionSpecCompatibility(From, ToType))
      return ExprError();

    From = ImpCastExprToType(From, ToType, CK_NoOp, VK_PRValue,
                             /*BasePath=*/nullptr, CCK)
               .get();
    break;

  case ICK_Qualification: {
    ExprValueKind VK = From->getValueKind();
    CastKind CK = CK_NoOp;

    if (ToType->isReferenceType() &&
        ToType->getPointeeType().getAddressSpace() !=
            From->getType().getAddressSpace())
      CK = CK_AddressSpaceConversion;

    if (ToType->isPointerType() &&
        ToType->getPointeeType().getAddressSpace() !=
            From->getType()->getPointeeType().getAddressSpace())
      CK = CK_AddressSpaceConversion;

    if (!isCast(CCK) &&
        !ToType->getPointeeType().getQualifiers().hasUnaligned() &&
        From->getType()->getPointeeType().getQualifiers().hasUnaligned()) {
      Diag(From->getBeginLoc(), diag::warn_imp_cast_drops_unaligned)
          << InitialFromType << ToType;
    }

    From = ImpCastExprToType(From, ToType.getNonLValueExprType(Context), CK, VK,
                             /*BasePath=*/nullptr, CCK)
               .get();

    if (SCS.DeprecatedStringLiteralToCharPtr &&
        !getLangOpts().WritableStrings) {
      Diag(From->getBeginLoc(),
           getLangOpts().CPlusPlus11
               ? diag::ext_deprecated_string_literal_conversion
               : diag::warn_deprecated_string_literal_conversion)
          << ToType.getNonReferenceType();
    }

    break;
  }

  default:
    llvm_unreachable("Improper third standard conversion");
  }

  // If this conversion sequence involved a scalar -> atomic conversion, perform
  // that conversion now.
  if (!ToAtomicType.isNull()) {
    assert(Context.hasSameType(
        ToAtomicType->castAs<AtomicType>()->getValueType(), From->getType()));
    From = ImpCastExprToType(From, ToAtomicType, CK_NonAtomicToAtomic,
                             VK_PRValue, nullptr, CCK)
               .get();
  }

  // Materialize a temporary if we're implicitly converting to a reference
  // type. This is not required by the C++ rules but is necessary to maintain
  // AST invariants.
  if (ToType->isReferenceType() && From->isPRValue()) {
    ExprResult Res = TemporaryMaterializationConversion(From);
    if (Res.isInvalid())
      return ExprError();
    From = Res.get();
  }

  // If this conversion sequence succeeded and involved implicitly converting a
  // _Nullable type to a _Nonnull one, complain.
  if (!isCast(CCK))
    diagnoseNullableToNonnullConversion(ToType, InitialFromType,
                                        From->getBeginLoc());

  return From;
}

/// Checks that type T is not a VLA.
///
/// @returns @c true if @p T is VLA and a diagnostic was emitted,
/// @c false otherwise.
static bool DiagnoseVLAInCXXTypeTrait(Sema &S, const TypeSourceInfo *T,
                                      clang::tok::TokenKind TypeTraitID) {
  if (!T->getType()->isVariableArrayType())
    return false;

  S.Diag(T->getTypeLoc().getBeginLoc(), diag::err_vla_unsupported)
      << 1 << TypeTraitID;
  return true;
}

/// Check the completeness of a type in a unary type trait.
///
/// If the particular type trait requires a complete type, tries to complete
/// it. If completing the type fails, a diagnostic is emitted and false
/// returned. If completing the type succeeds or no completion was required,
/// returns true.
static bool CheckUnaryTypeTraitTypeCompleteness(Sema &S, TypeTrait UTT,
                                                SourceLocation Loc,
                                                QualType ArgTy) {
  // C++0x [meta.unary.prop]p3:
  //   For all of the class templates X declared in this Clause, instantiating
  //   that template with a template argument that is a class template
  //   specialization may result in the implicit instantiation of the template
  //   argument if and only if the semantics of X require that the argument
  //   must be a complete type.
  // We apply this rule to all the type trait expressions used to implement
  // these class templates. We also try to follow any GCC documented behavior
  // in these expressions to ensure portability of standard libraries.
  switch (UTT) {
  default: llvm_unreachable("not a UTT");
    // is_complete_type somewhat obviously cannot require a complete type.
  case UTT_IsCompleteType:
    // Fall-through

    // These traits are modeled on the type predicates in C++0x
    // [meta.unary.cat] and [meta.unary.comp]. They are not specified as
    // requiring a complete type, as whether or not they return true cannot be
    // impacted by the completeness of the type.
  case UTT_IsVoid:
  case UTT_IsIntegral:
  case UTT_IsFloatingPoint:
  case UTT_IsArray:
  case UTT_IsBoundedArray:
  case UTT_IsPointer:
  case UTT_IsNullPointer:
  case UTT_IsReferenceable:
  case UTT_IsLvalueReference:
  case UTT_IsRvalueReference:
  case UTT_IsMemberFunctionPointer:
  case UTT_IsMemberObjectPointer:
  case UTT_IsEnum:
  case UTT_IsScopedEnum:
  case UTT_IsUnion:
  case UTT_IsClass:
  case UTT_IsFunction:
  case UTT_IsReference:
  case UTT_IsArithmetic:
  case UTT_IsFundamental:
  case UTT_IsObject:
  case UTT_IsScalar:
  case UTT_IsCompound:
  case UTT_IsMemberPointer:
    // Fall-through

    // These traits are modeled on type predicates in C++0x [meta.unary.prop]
    // which requires some of its traits to have the complete type. However,
    // the completeness of the type cannot impact these traits' semantics, and
    // so they don't require it. This matches the comments on these traits in
    // Table 49.
  case UTT_IsConst:
  case UTT_IsVolatile:
  case UTT_IsSigned:
  case UTT_IsUnboundedArray:
  case UTT_IsUnsigned:

  // This type trait always returns false, checking the type is moot.
  case UTT_IsInterfaceClass:
    return true;

  // C++14 [meta.unary.prop]:
  //   If T is a non-union class type, T shall be a complete type.
  case UTT_IsEmpty:
  case UTT_IsPolymorphic:
  case UTT_IsAbstract:
    if (const auto *RD = ArgTy->getAsCXXRecordDecl())
      if (!RD->isUnion())
        return !S.RequireCompleteType(
            Loc, ArgTy, diag::err_incomplete_type_used_in_type_trait_expr);
    return true;

  // C++14 [meta.unary.prop]:
  //   If T is a class type, T shall be a complete type.
  case UTT_IsFinal:
  case UTT_IsSealed:
    if (ArgTy->getAsCXXRecordDecl())
      return !S.RequireCompleteType(
          Loc, ArgTy, diag::err_incomplete_type_used_in_type_trait_expr);
    return true;

  // LWG3823: T shall be an array type, a complete type, or cv void.
  case UTT_IsAggregate:
    if (ArgTy->isArrayType() || ArgTy->isVoidType())
      return true;

    return !S.RequireCompleteType(
        Loc, ArgTy, diag::err_incomplete_type_used_in_type_trait_expr);

  // C++1z [meta.unary.prop]:
  //   remove_all_extents_t<T> shall be a complete type or cv void.
  case UTT_IsTrivial:
  case UTT_IsTriviallyCopyable:
  case UTT_IsStandardLayout:
  case UTT_IsPOD:
  case UTT_IsLiteral:
  case UTT_IsBitwiseCloneable:
  // By analogy, is_trivially_relocatable and is_trivially_equality_comparable
  // impose the same constraints.
  case UTT_IsTriviallyRelocatable:
  case UTT_IsTriviallyEqualityComparable:
  case UTT_CanPassInRegs:
  // Per the GCC type traits documentation, T shall be a complete type, cv void,
  // or an array of unknown bound. But GCC actually imposes the same constraints
  // as above.
  case UTT_HasNothrowAssign:
  case UTT_HasNothrowMoveAssign:
  case UTT_HasNothrowConstructor:
  case UTT_HasNothrowCopy:
  case UTT_HasTrivialAssign:
  case UTT_HasTrivialMoveAssign:
  case UTT_HasTrivialDefaultConstructor:
  case UTT_HasTrivialMoveConstructor:
  case UTT_HasTrivialCopy:
  case UTT_HasTrivialDestructor:
  case UTT_HasVirtualDestructor:
  // has_unique_object_representations<T> when T is an array is defined in terms
  // of has_unique_object_representations<remove_all_extents_t<T>>, so the base
  // type needs to be complete even if the type is an incomplete array type.
  case UTT_HasUniqueObjectRepresentations:
    ArgTy = QualType(ArgTy->getBaseElementTypeUnsafe(), 0);
    [[fallthrough]];

  // C++1z [meta.unary.prop]:
  //   T shall be a complete type, cv void, or an array of unknown bound.
  case UTT_IsDestructible:
  case UTT_IsNothrowDestructible:
  case UTT_IsTriviallyDestructible:
    if (ArgTy->isIncompleteArrayType() || ArgTy->isVoidType())
      return true;

    return !S.RequireCompleteType(
        Loc, ArgTy, diag::err_incomplete_type_used_in_type_trait_expr);
  }
}

static bool HasNoThrowOperator(const RecordType *RT, OverloadedOperatorKind Op,
                               Sema &Self, SourceLocation KeyLoc, ASTContext &C,
                               bool (CXXRecordDecl::*HasTrivial)() const,
                               bool (CXXRecordDecl::*HasNonTrivial)() const,
                               bool (CXXMethodDecl::*IsDesiredOp)() const)
{
  CXXRecordDecl *RD = cast<CXXRecordDecl>(RT->getDecl());
  if ((RD->*HasTrivial)() && !(RD->*HasNonTrivial)())
    return true;

  DeclarationName Name = C.DeclarationNames.getCXXOperatorName(Op);
  DeclarationNameInfo NameInfo(Name, KeyLoc);
  LookupResult Res(Self, NameInfo, Sema::LookupOrdinaryName);
  if (Self.LookupQualifiedName(Res, RD)) {
    bool FoundOperator = false;
    Res.suppressDiagnostics();
    for (LookupResult::iterator Op = Res.begin(), OpEnd = Res.end();
         Op != OpEnd; ++Op) {
      if (isa<FunctionTemplateDecl>(*Op))
        continue;

      CXXMethodDecl *Operator = cast<CXXMethodDecl>(*Op);
      if((Operator->*IsDesiredOp)()) {
        FoundOperator = true;
        auto *CPT = Operator->getType()->castAs<FunctionProtoType>();
        CPT = Self.ResolveExceptionSpec(KeyLoc, CPT);
        if (!CPT || !CPT->isNothrow())
          return false;
      }
    }
    return FoundOperator;
  }
  return false;
}

static bool HasNonDeletedDefaultedEqualityComparison(Sema &S,
                                                     const CXXRecordDecl *Decl,
                                                     SourceLocation KeyLoc) {
  if (Decl->isUnion())
    return false;
  if (Decl->isLambda())
    return Decl->isCapturelessLambda();

  {
    EnterExpressionEvaluationContext UnevaluatedContext(
        S, Sema::ExpressionEvaluationContext::Unevaluated);
    Sema::SFINAETrap SFINAE(S, /*AccessCheckingSFINAE=*/true);
    Sema::ContextRAII TUContext(S, S.Context.getTranslationUnitDecl());

    // const ClassT& obj;
    OpaqueValueExpr Operand(
        KeyLoc,
        Decl->getTypeForDecl()->getCanonicalTypeUnqualified().withConst(),
        ExprValueKind::VK_LValue);
    UnresolvedSet<16> Functions;
    // obj == obj;
    S.LookupBinOp(S.TUScope, {}, BinaryOperatorKind::BO_EQ, Functions);

    auto Result = S.CreateOverloadedBinOp(KeyLoc, BinaryOperatorKind::BO_EQ,
                                          Functions, &Operand, &Operand);
    if (Result.isInvalid() || SFINAE.hasErrorOccurred())
      return false;

    const auto *CallExpr = dyn_cast<CXXOperatorCallExpr>(Result.get());
    if (!CallExpr)
      return false;
    const auto *Callee = CallExpr->getDirectCallee();
    auto ParamT = Callee->getParamDecl(0)->getType();
    if (!Callee->isDefaulted())
      return false;
    if (!ParamT->isReferenceType() && !Decl->isTriviallyCopyable())
      return false;
    if (ParamT.getNonReferenceType()->getUnqualifiedDesugaredType() !=
        Decl->getTypeForDecl())
      return false;
  }

  return llvm::all_of(Decl->bases(),
                      [&](const CXXBaseSpecifier &BS) {
                        if (const auto *RD = BS.getType()->getAsCXXRecordDecl())
                          return HasNonDeletedDefaultedEqualityComparison(
                              S, RD, KeyLoc);
                        return true;
                      }) &&
         llvm::all_of(Decl->fields(), [&](const FieldDecl *FD) {
           auto Type = FD->getType();
           if (Type->isArrayType())
             Type = Type->getBaseElementTypeUnsafe()
                        ->getCanonicalTypeUnqualified();

           if (Type->isReferenceType() || Type->isEnumeralType())
             return false;
           if (const auto *RD = Type->getAsCXXRecordDecl())
             return HasNonDeletedDefaultedEqualityComparison(S, RD, KeyLoc);
           return true;
         });
}

static bool isTriviallyEqualityComparableType(Sema &S, QualType Type, SourceLocation KeyLoc) {
  QualType CanonicalType = Type.getCanonicalType();
  if (CanonicalType->isIncompleteType() || CanonicalType->isDependentType() ||
      CanonicalType->isEnumeralType() || CanonicalType->isArrayType())
    return false;

  if (const auto *RD = CanonicalType->getAsCXXRecordDecl()) {
    if (!HasNonDeletedDefaultedEqualityComparison(S, RD, KeyLoc))
      return false;
  }

  return S.getASTContext().hasUniqueObjectRepresentations(
      CanonicalType, /*CheckIfTriviallyCopyable=*/false);
}

static bool EvaluateUnaryTypeTrait(Sema &Self, TypeTrait UTT,
                                   SourceLocation KeyLoc,
                                   TypeSourceInfo *TInfo) {
  QualType T = TInfo->getType();
  assert(!T->isDependentType() && "Cannot evaluate traits of dependent type");

  ASTContext &C = Self.Context;
  switch(UTT) {
  default: llvm_unreachable("not a UTT");
    // Type trait expressions corresponding to the primary type category
    // predicates in C++0x [meta.unary.cat].
  case UTT_IsVoid:
    return T->isVoidType();
  case UTT_IsIntegral:
    return T->isIntegralType(C);
  case UTT_IsFloatingPoint:
    return T->isFloatingType();
  case UTT_IsArray:
    // Zero-sized arrays aren't considered arrays in partial specializations,
    // so __is_array shouldn't consider them arrays either.
    if (const auto *CAT = C.getAsConstantArrayType(T))
      return CAT->getSize() != 0;
    return T->isArrayType();
  case UTT_IsBoundedArray:
    if (DiagnoseVLAInCXXTypeTrait(Self, TInfo, tok::kw___is_bounded_array))
      return false;
    // Zero-sized arrays aren't considered arrays in partial specializations,
    // so __is_bounded_array shouldn't consider them arrays either.
    if (const auto *CAT = C.getAsConstantArrayType(T))
      return CAT->getSize() != 0;
    return T->isArrayType() && !T->isIncompleteArrayType();
  case UTT_IsUnboundedArray:
    if (DiagnoseVLAInCXXTypeTrait(Self, TInfo, tok::kw___is_unbounded_array))
      return false;
    return T->isIncompleteArrayType();
  case UTT_IsPointer:
    return T->isAnyPointerType();
  case UTT_IsNullPointer:
    return T->isNullPtrType();
  case UTT_IsLvalueReference:
    return T->isLValueReferenceType();
  case UTT_IsRvalueReference:
    return T->isRValueReferenceType();
  case UTT_IsMemberFunctionPointer:
    return T->isMemberFunctionPointerType();
  case UTT_IsMemberObjectPointer:
    return T->isMemberDataPointerType();
  case UTT_IsEnum:
    return T->isEnumeralType();
  case UTT_IsScopedEnum:
    return T->isScopedEnumeralType();
  case UTT_IsUnion:
    return T->isUnionType();
  case UTT_IsClass:
    return T->isClassType() || T->isStructureType() || T->isInterfaceType();
  case UTT_IsFunction:
    return T->isFunctionType();

    // Type trait expressions which correspond to the convenient composition
    // predicates in C++0x [meta.unary.comp].
  case UTT_IsReference:
    return T->isReferenceType();
  case UTT_IsArithmetic:
    return T->isArithmeticType() && !T->isEnumeralType();
  case UTT_IsFundamental:
    return T->isFundamentalType();
  case UTT_IsObject:
    return T->isObjectType();
  case UTT_IsScalar:
    // Note: semantic analysis depends on Objective-C lifetime types to be
    // considered scalar types. However, such types do not actually behave
    // like scalar types at run time (since they may require retain/release
    // operations), so we report them as non-scalar.
    if (T->isObjCLifetimeType()) {
      switch (T.getObjCLifetime()) {
      case Qualifiers::OCL_None:
      case Qualifiers::OCL_ExplicitNone:
        return true;

      case Qualifiers::OCL_Strong:
      case Qualifiers::OCL_Weak:
      case Qualifiers::OCL_Autoreleasing:
        return false;
      }
    }

    return T->isScalarType();
  case UTT_IsCompound:
    return T->isCompoundType();
  case UTT_IsMemberPointer:
    return T->isMemberPointerType();

    // Type trait expressions which correspond to the type property predicates
    // in C++0x [meta.unary.prop].
  case UTT_IsConst:
    return T.isConstQualified();
  case UTT_IsVolatile:
    return T.isVolatileQualified();
  case UTT_IsTrivial:
    return T.isTrivialType(C);
  case UTT_IsTriviallyCopyable:
    return T.isTriviallyCopyableType(C);
  case UTT_IsStandardLayout:
    return T->isStandardLayoutType();
  case UTT_IsPOD:
    return T.isPODType(C);
  case UTT_IsLiteral:
    return T->isLiteralType(C);
  case UTT_IsEmpty:
    if (const CXXRecordDecl *RD = T->getAsCXXRecordDecl())
      return !RD->isUnion() && RD->isEmpty();
    return false;
  case UTT_IsPolymorphic:
    if (const CXXRecordDecl *RD = T->getAsCXXRecordDecl())
      return !RD->isUnion() && RD->isPolymorphic();
    return false;
  case UTT_IsAbstract:
    if (const CXXRecordDecl *RD = T->getAsCXXRecordDecl())
      return !RD->isUnion() && RD->isAbstract();
    return false;
  case UTT_IsAggregate:
    // Report vector extensions and complex types as aggregates because they
    // support aggregate initialization. GCC mirrors this behavior for vectors
    // but not _Complex.
    return T->isAggregateType() || T->isVectorType() || T->isExtVectorType() ||
           T->isAnyComplexType();
  // __is_interface_class only returns true when CL is invoked in /CLR mode and
  // even then only when it is used with the 'interface struct ...' syntax
  // Clang doesn't support /CLR which makes this type trait moot.
  case UTT_IsInterfaceClass:
    return false;
  case UTT_IsFinal:
  case UTT_IsSealed:
    if (const CXXRecordDecl *RD = T->getAsCXXRecordDecl())
      return RD->hasAttr<FinalAttr>();
    return false;
  case UTT_IsSigned:
    // Enum types should always return false.
    // Floating points should always return true.
    return T->isFloatingType() ||
           (T->isSignedIntegerType() && !T->isEnumeralType());
  case UTT_IsUnsigned:
    // Enum types should always return false.
    return T->isUnsignedIntegerType() && !T->isEnumeralType();

    // Type trait expressions which query classes regarding their construction,
    // destruction, and copying. Rather than being based directly on the
    // related type predicates in the standard, they are specified by both
    // GCC[1] and the Embarcadero C++ compiler[2], and Clang implements those
    // specifications.
    //
    //   1: http://gcc.gnu/.org/onlinedocs/gcc/Type-Traits.html
    //   2: http://docwiki.embarcadero.com/RADStudio/XE/en/Type_Trait_Functions_(C%2B%2B0x)_Index
    //
    // Note that these builtins do not behave as documented in g++: if a class
    // has both a trivial and a non-trivial special member of a particular kind,
    // they return false! For now, we emulate this behavior.
    // FIXME: This appears to be a g++ bug: more complex cases reveal that it
    // does not correctly compute triviality in the presence of multiple special
    // members of the same kind. Revisit this once the g++ bug is fixed.
  case UTT_HasTrivialDefaultConstructor:
    // http://gcc.gnu.org/onlinedocs/gcc/Type-Traits.html:
    //   If __is_pod (type) is true then the trait is true, else if type is
    //   a cv class or union type (or array thereof) with a trivial default
    //   constructor ([class.ctor]) then the trait is true, else it is false.
    if (T.isPODType(C))
      return true;
    if (CXXRecordDecl *RD = C.getBaseElementType(T)->getAsCXXRecordDecl())
      return RD->hasTrivialDefaultConstructor() &&
             !RD->hasNonTrivialDefaultConstructor();
    return false;
  case UTT_HasTrivialMoveConstructor:
    //  This trait is implemented by MSVC 2012 and needed to parse the
    //  standard library headers. Specifically this is used as the logic
    //  behind std::is_trivially_move_constructible (20.9.4.3).
    if (T.isPODType(C))
      return true;
    if (CXXRecordDecl *RD = C.getBaseElementType(T)->getAsCXXRecordDecl())
      return RD->hasTrivialMoveConstructor() && !RD->hasNonTrivialMoveConstructor();
    return false;
  case UTT_HasTrivialCopy:
    // http://gcc.gnu.org/onlinedocs/gcc/Type-Traits.html:
    //   If __is_pod (type) is true or type is a reference type then
    //   the trait is true, else if type is a cv class or union type
    //   with a trivial copy constructor ([class.copy]) then the trait
    //   is true, else it is false.
    if (T.isPODType(C) || T->isReferenceType())
      return true;
    if (CXXRecordDecl *RD = T->getAsCXXRecordDecl())
      return RD->hasTrivialCopyConstructor() &&
             !RD->hasNonTrivialCopyConstructor();
    return false;
  case UTT_HasTrivialMoveAssign:
    //  This trait is implemented by MSVC 2012 and needed to parse the
    //  standard library headers. Specifically it is used as the logic
    //  behind std::is_trivially_move_assignable (20.9.4.3)
    if (T.isPODType(C))
      return true;
    if (CXXRecordDecl *RD = C.getBaseElementType(T)->getAsCXXRecordDecl())
      return RD->hasTrivialMoveAssignment() && !RD->hasNonTrivialMoveAssignment();
    return false;
  case UTT_HasTrivialAssign:
    // http://gcc.gnu.org/onlinedocs/gcc/Type-Traits.html:
    //   If type is const qualified or is a reference type then the
    //   trait is false. Otherwise if __is_pod (type) is true then the
    //   trait is true, else if type is a cv class or union type with
    //   a trivial copy assignment ([class.copy]) then the trait is
    //   true, else it is false.
    // Note: the const and reference restrictions are interesting,
    // given that const and reference members don't prevent a class
    // from having a trivial copy assignment operator (but do cause
    // errors if the copy assignment operator is actually used, q.v.
    // [class.copy]p12).

    if (T.isConstQualified())
      return false;
    if (T.isPODType(C))
      return true;
    if (CXXRecordDecl *RD = T->getAsCXXRecordDecl())
      return RD->hasTrivialCopyAssignment() &&
             !RD->hasNonTrivialCopyAssignment();
    return false;
  case UTT_IsDestructible:
  case UTT_IsTriviallyDestructible:
  case UTT_IsNothrowDestructible:
    // C++14 [meta.unary.prop]:
    //   For reference types, is_destructible<T>::value is true.
    if (T->isReferenceType())
      return true;

    // Objective-C++ ARC: autorelease types don't require destruction.
    if (T->isObjCLifetimeType() &&
        T.getObjCLifetime() == Qualifiers::OCL_Autoreleasing)
      return true;

    // C++14 [meta.unary.prop]:
    //   For incomplete types and function types, is_destructible<T>::value is
    //   false.
    if (T->isIncompleteType() || T->isFunctionType())
      return false;

    // A type that requires destruction (via a non-trivial destructor or ARC
    // lifetime semantics) is not trivially-destructible.
    if (UTT == UTT_IsTriviallyDestructible && T.isDestructedType())
      return false;

    // C++14 [meta.unary.prop]:
    //   For object types and given U equal to remove_all_extents_t<T>, if the
    //   expression std::declval<U&>().~U() is well-formed when treated as an
    //   unevaluated operand (Clause 5), then is_destructible<T>::value is true
    if (auto *RD = C.getBaseElementType(T)->getAsCXXRecordDecl()) {
      CXXDestructorDecl *Destructor = Self.LookupDestructor(RD);
      if (!Destructor)
        return false;
      //  C++14 [dcl.fct.def.delete]p2:
      //    A program that refers to a deleted function implicitly or
      //    explicitly, other than to declare it, is ill-formed.
      if (Destructor->isDeleted())
        return false;
      if (C.getLangOpts().AccessControl && Destructor->getAccess() != AS_public)
        return false;
      if (UTT == UTT_IsNothrowDestructible) {
        auto *CPT = Destructor->getType()->castAs<FunctionProtoType>();
        CPT = Self.ResolveExceptionSpec(KeyLoc, CPT);
        if (!CPT || !CPT->isNothrow())
          return false;
      }
    }
    return true;

  case UTT_HasTrivialDestructor:
    // http://gcc.gnu.org/onlinedocs/gcc/Type-Traits.html
    //   If __is_pod (type) is true or type is a reference type
    //   then the trait is true, else if type is a cv class or union
    //   type (or array thereof) with a trivial destructor
    //   ([class.dtor]) then the trait is true, else it is
    //   false.
    if (T.isPODType(C) || T->isReferenceType())
      return true;

    // Objective-C++ ARC: autorelease types don't require destruction.
    if (T->isObjCLifetimeType() &&
        T.getObjCLifetime() == Qualifiers::OCL_Autoreleasing)
      return true;

    if (CXXRecordDecl *RD = C.getBaseElementType(T)->getAsCXXRecordDecl())
      return RD->hasTrivialDestructor();
    return false;
  // TODO: Propagate nothrowness for implicitly declared special members.
  case UTT_HasNothrowAssign:
    // http://gcc.gnu.org/onlinedocs/gcc/Type-Traits.html:
    //   If type is const qualified or is a reference type then the
    //   trait is false. Otherwise if __has_trivial_assign (type)
    //   is true then the trait is true, else if type is a cv class
    //   or union type with copy assignment operators that are known
    //   not to throw an exception then the trait is true, else it is
    //   false.
    if (C.getBaseElementType(T).isConstQualified())
      return false;
    if (T->isReferenceType())
      return false;
    if (T.isPODType(C) || T->isObjCLifetimeType())
      return true;

    if (const RecordType *RT = T->getAs<RecordType>())
      return HasNoThrowOperator(RT, OO_Equal, Self, KeyLoc, C,
                                &CXXRecordDecl::hasTrivialCopyAssignment,
                                &CXXRecordDecl::hasNonTrivialCopyAssignment,
                                &CXXMethodDecl::isCopyAssignmentOperator);
    return false;
  case UTT_HasNothrowMoveAssign:
    //  This trait is implemented by MSVC 2012 and needed to parse the
    //  standard library headers. Specifically this is used as the logic
    //  behind std::is_nothrow_move_assignable (20.9.4.3).
    if (T.isPODType(C))
      return true;

    if (const RecordType *RT = C.getBaseElementType(T)->getAs<RecordType>())
      return HasNoThrowOperator(RT, OO_Equal, Self, KeyLoc, C,
                                &CXXRecordDecl::hasTrivialMoveAssignment,
                                &CXXRecordDecl::hasNonTrivialMoveAssignment,
                                &CXXMethodDecl::isMoveAssignmentOperator);
    return false;
  case UTT_HasNothrowCopy:
    // http://gcc.gnu.org/onlinedocs/gcc/Type-Traits.html:
    //   If __has_trivial_copy (type) is true then the trait is true, else
    //   if type is a cv class or union type with copy constructors that are
    //   known not to throw an exception then the trait is true, else it is
    //   false.
    if (T.isPODType(C) || T->isReferenceType() || T->isObjCLifetimeType())
      return true;
    if (CXXRecordDecl *RD = T->getAsCXXRecordDecl()) {
      if (RD->hasTrivialCopyConstructor() &&
          !RD->hasNonTrivialCopyConstructor())
        return true;

      bool FoundConstructor = false;
      unsigned FoundTQs;
      for (const auto *ND : Self.LookupConstructors(RD)) {
        // A template constructor is never a copy constructor.
        // FIXME: However, it may actually be selected at the actual overload
        // resolution point.
        if (isa<FunctionTemplateDecl>(ND->getUnderlyingDecl()))
          continue;
        // UsingDecl itself is not a constructor
        if (isa<UsingDecl>(ND))
          continue;
        auto *Constructor = cast<CXXConstructorDecl>(ND->getUnderlyingDecl());
        if (Constructor->isCopyConstructor(FoundTQs)) {
          FoundConstructor = true;
          auto *CPT = Constructor->getType()->castAs<FunctionProtoType>();
          CPT = Self.ResolveExceptionSpec(KeyLoc, CPT);
          if (!CPT)
            return false;
          // TODO: check whether evaluating default arguments can throw.
          // For now, we'll be conservative and assume that they can throw.
          if (!CPT->isNothrow() || CPT->getNumParams() > 1)
            return false;
        }
      }

      return FoundConstructor;
    }
    return false;
  case UTT_HasNothrowConstructor:
    // http://gcc.gnu.org/onlinedocs/gcc/Type-Traits.html
    //   If __has_trivial_constructor (type) is true then the trait is
    //   true, else if type is a cv class or union type (or array
    //   thereof) with a default constructor that is known not to
    //   throw an exception then the trait is true, else it is false.
    if (T.isPODType(C) || T->isObjCLifetimeType())
      return true;
    if (CXXRecordDecl *RD = C.getBaseElementType(T)->getAsCXXRecordDecl()) {
      if (RD->hasTrivialDefaultConstructor() &&
          !RD->hasNonTrivialDefaultConstructor())
        return true;

      bool FoundConstructor = false;
      for (const auto *ND : Self.LookupConstructors(RD)) {
        // FIXME: In C++0x, a constructor template can be a default constructor.
        if (isa<FunctionTemplateDecl>(ND->getUnderlyingDecl()))
          continue;
        // UsingDecl itself is not a constructor
        if (isa<UsingDecl>(ND))
          continue;
        auto *Constructor = cast<CXXConstructorDecl>(ND->getUnderlyingDecl());
        if (Constructor->isDefaultConstructor()) {
          FoundConstructor = true;
          auto *CPT = Constructor->getType()->castAs<FunctionProtoType>();
          CPT = Self.ResolveExceptionSpec(KeyLoc, CPT);
          if (!CPT)
            return false;
          // FIXME: check whether evaluating default arguments can throw.
          // For now, we'll be conservative and assume that they can throw.
          if (!CPT->isNothrow() || CPT->getNumParams() > 0)
            return false;
        }
      }
      return FoundConstructor;
    }
    return false;
  case UTT_HasVirtualDestructor:
    // http://gcc.gnu.org/onlinedocs/gcc/Type-Traits.html:
    //   If type is a class type with a virtual destructor ([class.dtor])
    //   then the trait is true, else it is false.
    if (CXXRecordDecl *RD = T->getAsCXXRecordDecl())
      if (CXXDestructorDecl *Destructor = Self.LookupDestructor(RD))
        return Destructor->isVirtual();
    return false;

    // These type trait expressions are modeled on the specifications for the
    // Embarcadero C++0x type trait functions:
    //   http://docwiki.embarcadero.com/RADStudio/XE/en/Type_Trait_Functions_(C%2B%2B0x)_Index
  case UTT_IsCompleteType:
    // http://docwiki.embarcadero.com/RADStudio/XE/en/Is_complete_type_(typename_T_):
    //   Returns True if and only if T is a complete type at the point of the
    //   function call.
    return !T->isIncompleteType();
  case UTT_HasUniqueObjectRepresentations:
    return C.hasUniqueObjectRepresentations(T);
  case UTT_IsTriviallyRelocatable:
    return T.isTriviallyRelocatableType(C);
  case UTT_IsBitwiseCloneable:
    return T.isBitwiseCloneableType(C);
  case UTT_IsReferenceable:
    return T.isReferenceable();
  case UTT_CanPassInRegs:
    if (CXXRecordDecl *RD = T->getAsCXXRecordDecl(); RD && !T.hasQualifiers())
      return RD->canPassInRegisters();
    Self.Diag(KeyLoc, diag::err_builtin_pass_in_regs_non_class) << T;
    return false;
  case UTT_IsTriviallyEqualityComparable:
    return isTriviallyEqualityComparableType(Self, T, KeyLoc);
  }
}

static bool EvaluateBinaryTypeTrait(Sema &Self, TypeTrait BTT, const TypeSourceInfo *Lhs,
                                    const TypeSourceInfo *Rhs, SourceLocation KeyLoc);

static ExprResult CheckConvertibilityForTypeTraits(
    Sema &Self, const TypeSourceInfo *Lhs, const TypeSourceInfo *Rhs,
    SourceLocation KeyLoc, llvm::BumpPtrAllocator &OpaqueExprAllocator) {

  QualType LhsT = Lhs->getType();
  QualType RhsT = Rhs->getType();

  // C++0x [meta.rel]p4:
  //   Given the following function prototype:
  //
  //     template <class T>
  //       typename add_rvalue_reference<T>::type create();
  //
  //   the predicate condition for a template specialization
  //   is_convertible<From, To> shall be satisfied if and only if
  //   the return expression in the following code would be
  //   well-formed, including any implicit conversions to the return
  //   type of the function:
  //
  //     To test() {
  //       return create<From>();
  //     }
  //
  //   Access checking is performed as if in a context unrelated to To and
  //   From. Only the validity of the immediate context of the expression
  //   of the return-statement (including conversions to the return type)
  //   is considered.
  //
  // We model the initialization as a copy-initialization of a temporary
  // of the appropriate type, which for this expression is identical to the
  // return statement (since NRVO doesn't apply).

  // Functions aren't allowed to return function or array types.
  if (RhsT->isFunctionType() || RhsT->isArrayType())
    return ExprError();

  // A function definition requires a complete, non-abstract return type.
  if (!Self.isCompleteType(Rhs->getTypeLoc().getBeginLoc(), RhsT) ||
      Self.isAbstractType(Rhs->getTypeLoc().getBeginLoc(), RhsT))
    return ExprError();

  // Compute the result of add_rvalue_reference.
  if (LhsT->isObjectType() || LhsT->isFunctionType())
    LhsT = Self.Context.getRValueReferenceType(LhsT);

  // Build a fake source and destination for initialization.
  InitializedEntity To(InitializedEntity::InitializeTemporary(RhsT));
  Expr *From = new (OpaqueExprAllocator.Allocate<OpaqueValueExpr>())
      OpaqueValueExpr(KeyLoc, LhsT.getNonLValueExprType(Self.Context),
                      Expr::getValueKindForType(LhsT));
  InitializationKind Kind =
      InitializationKind::CreateCopy(KeyLoc, SourceLocation());

  // Perform the initialization in an unevaluated context within a SFINAE
  // trap at translation unit scope.
  EnterExpressionEvaluationContext Unevaluated(
      Self, Sema::ExpressionEvaluationContext::Unevaluated);
  Sema::SFINAETrap SFINAE(Self, /*AccessCheckingSFINAE=*/true);
  Sema::ContextRAII TUContext(Self, Self.Context.getTranslationUnitDecl());
  InitializationSequence Init(Self, To, Kind, From);
  if (Init.Failed())
    return ExprError();

  ExprResult Result = Init.Perform(Self, To, Kind, From);
  if (Result.isInvalid() || SFINAE.hasErrorOccurred())
    return ExprError();

  return Result;
}

static bool EvaluateBooleanTypeTrait(Sema &S, TypeTrait Kind,
                                     SourceLocation KWLoc,
                                     ArrayRef<TypeSourceInfo *> Args,
                                     SourceLocation RParenLoc,
                                     bool IsDependent) {
  if (IsDependent)
    return false;

  if (Kind <= UTT_Last)
    return EvaluateUnaryTypeTrait(S, Kind, KWLoc, Args[0]);

  // Evaluate ReferenceBindsToTemporary and ReferenceConstructsFromTemporary
  // alongside the IsConstructible traits to avoid duplication.
  if (Kind <= BTT_Last && Kind != BTT_ReferenceBindsToTemporary &&
      Kind != BTT_ReferenceConstructsFromTemporary &&
      Kind != BTT_ReferenceConvertsFromTemporary)
    return EvaluateBinaryTypeTrait(S, Kind, Args[0],
                                   Args[1], RParenLoc);

  switch (Kind) {
  case clang::BTT_ReferenceBindsToTemporary:
  case clang::BTT_ReferenceConstructsFromTemporary:
  case clang::BTT_ReferenceConvertsFromTemporary:
  case clang::TT_IsConstructible:
  case clang::TT_IsNothrowConstructible:
  case clang::TT_IsTriviallyConstructible: {
    // C++11 [meta.unary.prop]:
    //   is_trivially_constructible is defined as:
    //
    //     is_constructible<T, Args...>::value is true and the variable
    //     definition for is_constructible, as defined below, is known to call
    //     no operation that is not trivial.
    //
    //   The predicate condition for a template specialization
    //   is_constructible<T, Args...> shall be satisfied if and only if the
    //   following variable definition would be well-formed for some invented
    //   variable t:
    //
    //     T t(create<Args>()...);
    assert(!Args.empty());

    // Precondition: T and all types in the parameter pack Args shall be
    // complete types, (possibly cv-qualified) void, or arrays of
    // unknown bound.
    for (const auto *TSI : Args) {
      QualType ArgTy = TSI->getType();
      if (ArgTy->isVoidType() || ArgTy->isIncompleteArrayType())
        continue;

      if (S.RequireCompleteType(KWLoc, ArgTy,
          diag::err_incomplete_type_used_in_type_trait_expr))
        return false;
    }

    // Make sure the first argument is not incomplete nor a function type.
    QualType T = Args[0]->getType();
    if (T->isIncompleteType() || T->isFunctionType())
      return false;

    // Make sure the first argument is not an abstract type.
    CXXRecordDecl *RD = T->getAsCXXRecordDecl();
    if (RD && RD->isAbstract())
      return false;

    llvm::BumpPtrAllocator OpaqueExprAllocator;
    SmallVector<Expr *, 2> ArgExprs;
    ArgExprs.reserve(Args.size() - 1);
    for (unsigned I = 1, N = Args.size(); I != N; ++I) {
      QualType ArgTy = Args[I]->getType();
      if (ArgTy->isObjectType() || ArgTy->isFunctionType())
        ArgTy = S.Context.getRValueReferenceType(ArgTy);
      ArgExprs.push_back(
          new (OpaqueExprAllocator.Allocate<OpaqueValueExpr>())
              OpaqueValueExpr(Args[I]->getTypeLoc().getBeginLoc(),
                              ArgTy.getNonLValueExprType(S.Context),
                              Expr::getValueKindForType(ArgTy)));
    }

    // Perform the initialization in an unevaluated context within a SFINAE
    // trap at translation unit scope.
    EnterExpressionEvaluationContext Unevaluated(
        S, Sema::ExpressionEvaluationContext::Unevaluated);
    Sema::SFINAETrap SFINAE(S, /*AccessCheckingSFINAE=*/true);
    Sema::ContextRAII TUContext(S, S.Context.getTranslationUnitDecl());
    InitializedEntity To(
        InitializedEntity::InitializeTemporary(S.Context, Args[0]));
    InitializationKind InitKind(
        Kind == clang::BTT_ReferenceConvertsFromTemporary
            ? InitializationKind::CreateCopy(KWLoc, KWLoc)
            : InitializationKind::CreateDirect(KWLoc, KWLoc, RParenLoc));
    InitializationSequence Init(S, To, InitKind, ArgExprs);
    if (Init.Failed())
      return false;

    ExprResult Result = Init.Perform(S, To, InitKind, ArgExprs);
    if (Result.isInvalid() || SFINAE.hasErrorOccurred())
      return false;

    if (Kind == clang::TT_IsConstructible)
      return true;

    if (Kind == clang::BTT_ReferenceBindsToTemporary ||
        Kind == clang::BTT_ReferenceConstructsFromTemporary ||
        Kind == clang::BTT_ReferenceConvertsFromTemporary) {
      if (!T->isReferenceType())
        return false;

      if (!Init.isDirectReferenceBinding())
        return true;

      if (Kind == clang::BTT_ReferenceBindsToTemporary)
        return false;

      QualType U = Args[1]->getType();
      if (U->isReferenceType())
        return false;

      TypeSourceInfo *TPtr = S.Context.CreateTypeSourceInfo(
          S.Context.getPointerType(T.getNonReferenceType()));
      TypeSourceInfo *UPtr = S.Context.CreateTypeSourceInfo(
          S.Context.getPointerType(U.getNonReferenceType()));
      return !CheckConvertibilityForTypeTraits(S, UPtr, TPtr, RParenLoc,
                                               OpaqueExprAllocator)
                  .isInvalid();
    }

    if (Kind == clang::TT_IsNothrowConstructible)
      return S.canThrow(Result.get()) == CT_Cannot;

    if (Kind == clang::TT_IsTriviallyConstructible) {
      // Under Objective-C ARC and Weak, if the destination has non-trivial
      // Objective-C lifetime, this is a non-trivial construction.
      if (T.getNonReferenceType().hasNonTrivialObjCLifetime())
        return false;

      // The initialization succeeded; now make sure there are no non-trivial
      // calls.
      return !Result.get()->hasNonTrivialCall(S.Context);
    }

    llvm_unreachable("unhandled type trait");
    return false;
  }
    default: llvm_unreachable("not a TT");
  }

  return false;
}

namespace {
void DiagnoseBuiltinDeprecation(Sema& S, TypeTrait Kind,
                                SourceLocation KWLoc) {
  TypeTrait Replacement;
  switch (Kind) {
    case UTT_HasNothrowAssign:
    case UTT_HasNothrowMoveAssign:
      Replacement = BTT_IsNothrowAssignable;
      break;
    case UTT_HasNothrowCopy:
    case UTT_HasNothrowConstructor:
      Replacement = TT_IsNothrowConstructible;
      break;
    case UTT_HasTrivialAssign:
    case UTT_HasTrivialMoveAssign:
      Replacement = BTT_IsTriviallyAssignable;
      break;
    case UTT_HasTrivialCopy:
      Replacement = UTT_IsTriviallyCopyable;
      break;
    case UTT_HasTrivialDefaultConstructor:
    case UTT_HasTrivialMoveConstructor:
      Replacement = TT_IsTriviallyConstructible;
      break;
    case UTT_HasTrivialDestructor:
      Replacement = UTT_IsTriviallyDestructible;
      break;
    default:
      return;
  }
  S.Diag(KWLoc, diag::warn_deprecated_builtin)
    << getTraitSpelling(Kind) << getTraitSpelling(Replacement);
}
}

bool Sema::CheckTypeTraitArity(unsigned Arity, SourceLocation Loc, size_t N) {
  if (Arity && N != Arity) {
    Diag(Loc, diag::err_type_trait_arity)
        << Arity << 0 << (Arity > 1) << (int)N << SourceRange(Loc);
    return false;
  }

  if (!Arity && N == 0) {
    Diag(Loc, diag::err_type_trait_arity)
        << 1 << 1 << 1 << (int)N << SourceRange(Loc);
    return false;
  }
  return true;
}

enum class TypeTraitReturnType {
  Bool,
};

static TypeTraitReturnType GetReturnType(TypeTrait Kind) {
  return TypeTraitReturnType::Bool;
}

ExprResult Sema::BuildTypeTrait(TypeTrait Kind, SourceLocation KWLoc,
                                ArrayRef<TypeSourceInfo *> Args,
                                SourceLocation RParenLoc) {
  if (!CheckTypeTraitArity(getTypeTraitArity(Kind), KWLoc, Args.size()))
    return ExprError();

  if (Kind <= UTT_Last && !CheckUnaryTypeTraitTypeCompleteness(
                               *this, Kind, KWLoc, Args[0]->getType()))
    return ExprError();

  DiagnoseBuiltinDeprecation(*this, Kind, KWLoc);

  bool Dependent = false;
  for (unsigned I = 0, N = Args.size(); I != N; ++I) {
    if (Args[I]->getType()->isDependentType()) {
      Dependent = true;
      break;
    }
  }

  switch (GetReturnType(Kind)) {
  case TypeTraitReturnType::Bool: {
    bool Result = EvaluateBooleanTypeTrait(*this, Kind, KWLoc, Args, RParenLoc,
                                           Dependent);
    return TypeTraitExpr::Create(Context, Context.getLogicalOperationType(),
                                 KWLoc, Kind, Args, RParenLoc, Result);
  }
  }
  llvm_unreachable("unhandled type trait return type");
}

ExprResult Sema::ActOnTypeTrait(TypeTrait Kind, SourceLocation KWLoc,
                                ArrayRef<ParsedType> Args,
                                SourceLocation RParenLoc) {
  SmallVector<TypeSourceInfo *, 4> ConvertedArgs;
  ConvertedArgs.reserve(Args.size());

  for (unsigned I = 0, N = Args.size(); I != N; ++I) {
    TypeSourceInfo *TInfo;
    QualType T = GetTypeFromParser(Args[I], &TInfo);
    if (!TInfo)
      TInfo = Context.getTrivialTypeSourceInfo(T, KWLoc);

    ConvertedArgs.push_back(TInfo);
  }

  return BuildTypeTrait(Kind, KWLoc, ConvertedArgs, RParenLoc);
}

static bool EvaluateBinaryTypeTrait(Sema &Self, TypeTrait BTT, const TypeSourceInfo *Lhs,
                                    const TypeSourceInfo *Rhs, SourceLocation KeyLoc) {
  QualType LhsT = Lhs->getType();
  QualType RhsT = Rhs->getType();

  assert(!LhsT->isDependentType() && !RhsT->isDependentType() &&
         "Cannot evaluate traits of dependent types");

  switch(BTT) {
  case BTT_IsBaseOf: {
    // C++0x [meta.rel]p2
    // Base is a base class of Derived without regard to cv-qualifiers or
    // Base and Derived are not unions and name the same class type without
    // regard to cv-qualifiers.

    const RecordType *lhsRecord = LhsT->getAs<RecordType>();
    const RecordType *rhsRecord = RhsT->getAs<RecordType>();
    if (!rhsRecord || !lhsRecord) {
      const ObjCObjectType *LHSObjTy = LhsT->getAs<ObjCObjectType>();
      const ObjCObjectType *RHSObjTy = RhsT->getAs<ObjCObjectType>();
      if (!LHSObjTy || !RHSObjTy)
        return false;

      ObjCInterfaceDecl *BaseInterface = LHSObjTy->getInterface();
      ObjCInterfaceDecl *DerivedInterface = RHSObjTy->getInterface();
      if (!BaseInterface || !DerivedInterface)
        return false;

      if (Self.RequireCompleteType(
              Rhs->getTypeLoc().getBeginLoc(), RhsT,
              diag::err_incomplete_type_used_in_type_trait_expr))
        return false;

      return BaseInterface->isSuperClassOf(DerivedInterface);
    }

    assert(Self.Context.hasSameUnqualifiedType(LhsT, RhsT)
             == (lhsRecord == rhsRecord));

    // Unions are never base classes, and never have base classes.
    // It doesn't matter if they are complete or not. See PR#41843
    if (lhsRecord && lhsRecord->getDecl()->isUnion())
      return false;
    if (rhsRecord && rhsRecord->getDecl()->isUnion())
      return false;

    if (lhsRecord == rhsRecord)
      return true;

    // C++0x [meta.rel]p2:
    //   If Base and Derived are class types and are different types
    //   (ignoring possible cv-qualifiers) then Derived shall be a
    //   complete type.
    if (Self.RequireCompleteType(
            Rhs->getTypeLoc().getBeginLoc(), RhsT,
            diag::err_incomplete_type_used_in_type_trait_expr))
      return false;

    return cast<CXXRecordDecl>(rhsRecord->getDecl())
      ->isDerivedFrom(cast<CXXRecordDecl>(lhsRecord->getDecl()));
  }
  case BTT_IsSame:
    return Self.Context.hasSameType(LhsT, RhsT);
  case BTT_TypeCompatible: {
    // GCC ignores cv-qualifiers on arrays for this builtin.
    Qualifiers LhsQuals, RhsQuals;
    QualType Lhs = Self.getASTContext().getUnqualifiedArrayType(LhsT, LhsQuals);
    QualType Rhs = Self.getASTContext().getUnqualifiedArrayType(RhsT, RhsQuals);
    return Self.Context.typesAreCompatible(Lhs, Rhs);
  }
  case BTT_IsConvertible:
  case BTT_IsConvertibleTo:
  case BTT_IsNothrowConvertible: {
    if (RhsT->isVoidType())
      return LhsT->isVoidType();
    llvm::BumpPtrAllocator OpaqueExprAllocator;
    ExprResult Result = CheckConvertibilityForTypeTraits(Self, Lhs, Rhs, KeyLoc,
                                                         OpaqueExprAllocator);
    if (Result.isInvalid())
      return false;

    if (BTT != BTT_IsNothrowConvertible)
      return true;

    return Self.canThrow(Result.get()) == CT_Cannot;
  }

  case BTT_IsAssignable:
  case BTT_IsNothrowAssignable:
  case BTT_IsTriviallyAssignable: {
    // C++11 [meta.unary.prop]p3:
    //   is_trivially_assignable is defined as:
    //     is_assignable<T, U>::value is true and the assignment, as defined by
    //     is_assignable, is known to call no operation that is not trivial
    //
    //   is_assignable is defined as:
    //     The expression declval<T>() = declval<U>() is well-formed when
    //     treated as an unevaluated operand (Clause 5).
    //
    //   For both, T and U shall be complete types, (possibly cv-qualified)
    //   void, or arrays of unknown bound.
    if (!LhsT->isVoidType() && !LhsT->isIncompleteArrayType() &&
        Self.RequireCompleteType(
            Lhs->getTypeLoc().getBeginLoc(), LhsT,
            diag::err_incomplete_type_used_in_type_trait_expr))
      return false;
    if (!RhsT->isVoidType() && !RhsT->isIncompleteArrayType() &&
        Self.RequireCompleteType(
            Rhs->getTypeLoc().getBeginLoc(), RhsT,
            diag::err_incomplete_type_used_in_type_trait_expr))
      return false;

    // cv void is never assignable.
    if (LhsT->isVoidType() || RhsT->isVoidType())
      return false;

    // Build expressions that emulate the effect of declval<T>() and
    // declval<U>().
    if (LhsT->isObjectType() || LhsT->isFunctionType())
      LhsT = Self.Context.getRValueReferenceType(LhsT);
    if (RhsT->isObjectType() || RhsT->isFunctionType())
      RhsT = Self.Context.getRValueReferenceType(RhsT);
    OpaqueValueExpr Lhs(KeyLoc, LhsT.getNonLValueExprType(Self.Context),
                        Expr::getValueKindForType(LhsT));
    OpaqueValueExpr Rhs(KeyLoc, RhsT.getNonLValueExprType(Self.Context),
                        Expr::getValueKindForType(RhsT));

    // Attempt the assignment in an unevaluated context within a SFINAE
    // trap at translation unit scope.
    EnterExpressionEvaluationContext Unevaluated(
        Self, Sema::ExpressionEvaluationContext::Unevaluated);
    Sema::SFINAETrap SFINAE(Self, /*AccessCheckingSFINAE=*/true);
    Sema::ContextRAII TUContext(Self, Self.Context.getTranslationUnitDecl());
    ExprResult Result = Self.BuildBinOp(/*S=*/nullptr, KeyLoc, BO_Assign, &Lhs,
                                        &Rhs);
    if (Result.isInvalid())
      return false;

    // Treat the assignment as unused for the purpose of -Wdeprecated-volatile.
    Self.CheckUnusedVolatileAssignment(Result.get());

    if (SFINAE.hasErrorOccurred())
      return false;

    if (BTT == BTT_IsAssignable)
      return true;

    if (BTT == BTT_IsNothrowAssignable)
      return Self.canThrow(Result.get()) == CT_Cannot;

    if (BTT == BTT_IsTriviallyAssignable) {
      // Under Objective-C ARC and Weak, if the destination has non-trivial
      // Objective-C lifetime, this is a non-trivial assignment.
      if (LhsT.getNonReferenceType().hasNonTrivialObjCLifetime())
        return false;

      return !Result.get()->hasNonTrivialCall(Self.Context);
    }

    llvm_unreachable("unhandled type trait");
    return false;
  }
  case BTT_IsLayoutCompatible: {
    if (!LhsT->isVoidType() && !LhsT->isIncompleteArrayType())
      Self.RequireCompleteType(Lhs->getTypeLoc().getBeginLoc(), LhsT,
                               diag::err_incomplete_type);
    if (!RhsT->isVoidType() && !RhsT->isIncompleteArrayType())
      Self.RequireCompleteType(Rhs->getTypeLoc().getBeginLoc(), RhsT,
                               diag::err_incomplete_type);

    DiagnoseVLAInCXXTypeTrait(Self, Lhs, tok::kw___is_layout_compatible);
    DiagnoseVLAInCXXTypeTrait(Self, Rhs, tok::kw___is_layout_compatible);

    return Self.IsLayoutCompatible(LhsT, RhsT);
  }
  case BTT_IsPointerInterconvertibleBaseOf: {
    if (LhsT->isStructureOrClassType() && RhsT->isStructureOrClassType() &&
        !Self.getASTContext().hasSameUnqualifiedType(LhsT, RhsT)) {
      Self.RequireCompleteType(Rhs->getTypeLoc().getBeginLoc(), RhsT,
                               diag::err_incomplete_type);
    }

    DiagnoseVLAInCXXTypeTrait(Self, Lhs,
                              tok::kw___is_pointer_interconvertible_base_of);
    DiagnoseVLAInCXXTypeTrait(Self, Rhs,
                              tok::kw___is_pointer_interconvertible_base_of);

    return Self.IsPointerInterconvertibleBaseOf(Lhs, Rhs);
  }
  case BTT_IsDeducible: {
    const auto *TSTToBeDeduced = cast<DeducedTemplateSpecializationType>(LhsT);
    sema::TemplateDeductionInfo Info(KeyLoc);
    return Self.DeduceTemplateArgumentsFromType(
               TSTToBeDeduced->getTemplateName().getAsTemplateDecl(), RhsT,
               Info) == TemplateDeductionResult::Success;
  }
  default:
    llvm_unreachable("not a BTT");
  }
  llvm_unreachable("Unknown type trait or not implemented");
}

ExprResult Sema::ActOnArrayTypeTrait(ArrayTypeTrait ATT,
                                     SourceLocation KWLoc,
                                     ParsedType Ty,
                                     Expr* DimExpr,
                                     SourceLocation RParen) {
  TypeSourceInfo *TSInfo;
  QualType T = GetTypeFromParser(Ty, &TSInfo);
  if (!TSInfo)
    TSInfo = Context.getTrivialTypeSourceInfo(T);

  return BuildArrayTypeTrait(ATT, KWLoc, TSInfo, DimExpr, RParen);
}

static uint64_t EvaluateArrayTypeTrait(Sema &Self, ArrayTypeTrait ATT,
                                           QualType T, Expr *DimExpr,
                                           SourceLocation KeyLoc) {
  assert(!T->isDependentType() && "Cannot evaluate traits of dependent type");

  switch(ATT) {
  case ATT_ArrayRank:
    if (T->isArrayType()) {
      unsigned Dim = 0;
      while (const ArrayType *AT = Self.Context.getAsArrayType(T)) {
        ++Dim;
        T = AT->getElementType();
      }
      return Dim;
    }
    return 0;

  case ATT_ArrayExtent: {
    llvm::APSInt Value;
    uint64_t Dim;
    if (Self.VerifyIntegerConstantExpression(
                DimExpr, &Value, diag::err_dimension_expr_not_constant_integer)
            .isInvalid())
      return 0;
    if (Value.isSigned() && Value.isNegative()) {
      Self.Diag(KeyLoc, diag::err_dimension_expr_not_constant_integer)
        << DimExpr->getSourceRange();
      return 0;
    }
    Dim = Value.getLimitedValue();

    if (T->isArrayType()) {
      unsigned D = 0;
      bool Matched = false;
      while (const ArrayType *AT = Self.Context.getAsArrayType(T)) {
        if (Dim == D) {
          Matched = true;
          break;
        }
        ++D;
        T = AT->getElementType();
      }

      if (Matched && T->isArrayType()) {
        if (const ConstantArrayType *CAT = Self.Context.getAsConstantArrayType(T))
          return CAT->getLimitedSize();
      }
    }
    return 0;
  }
  }
  llvm_unreachable("Unknown type trait or not implemented");
}

ExprResult Sema::BuildArrayTypeTrait(ArrayTypeTrait ATT,
                                     SourceLocation KWLoc,
                                     TypeSourceInfo *TSInfo,
                                     Expr* DimExpr,
                                     SourceLocation RParen) {
  QualType T = TSInfo->getType();

  // FIXME: This should likely be tracked as an APInt to remove any host
  // assumptions about the width of size_t on the target.
  uint64_t Value = 0;
  if (!T->isDependentType())
    Value = EvaluateArrayTypeTrait(*this, ATT, T, DimExpr, KWLoc);

  // While the specification for these traits from the Embarcadero C++
  // compiler's documentation says the return type is 'unsigned int', Clang
  // returns 'size_t'. On Windows, the primary platform for the Embarcadero
  // compiler, there is no difference. On several other platforms this is an
  // important distinction.
  return new (Context) ArrayTypeTraitExpr(KWLoc, ATT, TSInfo, Value, DimExpr,
                                          RParen, Context.getSizeType());
}

ExprResult Sema::ActOnExpressionTrait(ExpressionTrait ET,
                                      SourceLocation KWLoc,
                                      Expr *Queried,
                                      SourceLocation RParen) {
  // If error parsing the expression, ignore.
  if (!Queried)
    return ExprError();

  ExprResult Result = BuildExpressionTrait(ET, KWLoc, Queried, RParen);

  return Result;
}

static bool EvaluateExpressionTrait(ExpressionTrait ET, Expr *E) {
  switch (ET) {
  case ET_IsLValueExpr: return E->isLValue();
  case ET_IsRValueExpr:
    return E->isPRValue();
  }
  llvm_unreachable("Expression trait not covered by switch");
}

ExprResult Sema::BuildExpressionTrait(ExpressionTrait ET,
                                      SourceLocation KWLoc,
                                      Expr *Queried,
                                      SourceLocation RParen) {
  if (Queried->isTypeDependent()) {
    // Delay type-checking for type-dependent expressions.
  } else if (Queried->hasPlaceholderType()) {
    ExprResult PE = CheckPlaceholderExpr(Queried);
    if (PE.isInvalid()) return ExprError();
    return BuildExpressionTrait(ET, KWLoc, PE.get(), RParen);
  }

  bool Value = EvaluateExpressionTrait(ET, Queried);

  return new (Context)
      ExpressionTraitExpr(KWLoc, ET, Queried, Value, RParen, Context.BoolTy);
}

QualType Sema::CheckPointerToMemberOperands(ExprResult &LHS, ExprResult &RHS,
                                            ExprValueKind &VK,
                                            SourceLocation Loc,
                                            bool isIndirect) {
  assert(!LHS.get()->hasPlaceholderType() && !RHS.get()->hasPlaceholderType() &&
         "placeholders should have been weeded out by now");

  // The LHS undergoes lvalue conversions if this is ->*, and undergoes the
  // temporary materialization conversion otherwise.
  if (isIndirect)
    LHS = DefaultLvalueConversion(LHS.get());
  else if (LHS.get()->isPRValue())
    LHS = TemporaryMaterializationConversion(LHS.get());
  if (LHS.isInvalid())
    return QualType();

  // The RHS always undergoes lvalue conversions.
  RHS = DefaultLvalueConversion(RHS.get());
  if (RHS.isInvalid()) return QualType();

  const char *OpSpelling = isIndirect ? "->*" : ".*";
  // C++ 5.5p2
  //   The binary operator .* [p3: ->*] binds its second operand, which shall
  //   be of type "pointer to member of T" (where T is a completely-defined
  //   class type) [...]
  QualType RHSType = RHS.get()->getType();
  const MemberPointerType *MemPtr = RHSType->getAs<MemberPointerType>();
  if (!MemPtr) {
    Diag(Loc, diag::err_bad_memptr_rhs)
      << OpSpelling << RHSType << RHS.get()->getSourceRange();
    return QualType();
  }

  QualType Class(MemPtr->getClass(), 0);

  // Note: C++ [expr.mptr.oper]p2-3 says that the class type into which the
  // member pointer points must be completely-defined. However, there is no
  // reason for this semantic distinction, and the rule is not enforced by
  // other compilers. Therefore, we do not check this property, as it is
  // likely to be considered a defect.

  // C++ 5.5p2
  //   [...] to its first operand, which shall be of class T or of a class of
  //   which T is an unambiguous and accessible base class. [p3: a pointer to
  //   such a class]
  QualType LHSType = LHS.get()->getType();
  if (isIndirect) {
    if (const PointerType *Ptr = LHSType->getAs<PointerType>())
      LHSType = Ptr->getPointeeType();
    else {
      Diag(Loc, diag::err_bad_memptr_lhs)
        << OpSpelling << 1 << LHSType
        << FixItHint::CreateReplacement(SourceRange(Loc), ".*");
      return QualType();
    }
  }

  if (!Context.hasSameUnqualifiedType(Class, LHSType)) {
    // If we want to check the hierarchy, we need a complete type.
    if (RequireCompleteType(Loc, LHSType, diag::err_bad_memptr_lhs,
                            OpSpelling, (int)isIndirect)) {
      return QualType();
    }

    if (!IsDerivedFrom(Loc, LHSType, Class)) {
      Diag(Loc, diag::err_bad_memptr_lhs) << OpSpelling
        << (int)isIndirect << LHS.get()->getType();
      return QualType();
    }

    CXXCastPath BasePath;
    if (CheckDerivedToBaseConversion(
            LHSType, Class, Loc,
            SourceRange(LHS.get()->getBeginLoc(), RHS.get()->getEndLoc()),
            &BasePath))
      return QualType();

    // Cast LHS to type of use.
    QualType UseType = Context.getQualifiedType(Class, LHSType.getQualifiers());
    if (isIndirect)
      UseType = Context.getPointerType(UseType);
    ExprValueKind VK = isIndirect ? VK_PRValue : LHS.get()->getValueKind();
    LHS = ImpCastExprToType(LHS.get(), UseType, CK_DerivedToBase, VK,
                            &BasePath);
  }

  if (isa<CXXScalarValueInitExpr>(RHS.get()->IgnoreParens())) {
    // Diagnose use of pointer-to-member type which when used as
    // the functional cast in a pointer-to-member expression.
    Diag(Loc, diag::err_pointer_to_member_type) << isIndirect;
     return QualType();
  }

  // C++ 5.5p2
  //   The result is an object or a function of the type specified by the
  //   second operand.
  // The cv qualifiers are the union of those in the pointer and the left side,
  // in accordance with 5.5p5 and 5.2.5.
  QualType Result = MemPtr->getPointeeType();
  Result = Context.getCVRQualifiedType(Result, LHSType.getCVRQualifiers());

  // C++0x [expr.mptr.oper]p6:
  //   In a .* expression whose object expression is an rvalue, the program is
  //   ill-formed if the second operand is a pointer to member function with
  //   ref-qualifier &. In a ->* expression or in a .* expression whose object
  //   expression is an lvalue, the program is ill-formed if the second operand
  //   is a pointer to member function with ref-qualifier &&.
  if (const FunctionProtoType *Proto = Result->getAs<FunctionProtoType>()) {
    switch (Proto->getRefQualifier()) {
    case RQ_None:
      // Do nothing
      break;

    case RQ_LValue:
      if (!isIndirect && !LHS.get()->Classify(Context).isLValue()) {
        // C++2a allows functions with ref-qualifier & if their cv-qualifier-seq
        // is (exactly) 'const'.
        if (Proto->isConst() && !Proto->isVolatile())
          Diag(Loc, getLangOpts().CPlusPlus20
                        ? diag::warn_cxx17_compat_pointer_to_const_ref_member_on_rvalue
                        : diag::ext_pointer_to_const_ref_member_on_rvalue);
        else
          Diag(Loc, diag::err_pointer_to_member_oper_value_classify)
              << RHSType << 1 << LHS.get()->getSourceRange();
      }
      break;

    case RQ_RValue:
      if (isIndirect || !LHS.get()->Classify(Context).isRValue())
        Diag(Loc, diag::err_pointer_to_member_oper_value_classify)
          << RHSType << 0 << LHS.get()->getSourceRange();
      break;
    }
  }

  // C++ [expr.mptr.oper]p6:
  //   The result of a .* expression whose second operand is a pointer
  //   to a data member is of the same value category as its
  //   first operand. The result of a .* expression whose second
  //   operand is a pointer to a member function is a prvalue. The
  //   result of an ->* expression is an lvalue if its second operand
  //   is a pointer to data member and a prvalue otherwise.
  if (Result->isFunctionType()) {
    VK = VK_PRValue;
    return Context.BoundMemberTy;
  } else if (isIndirect) {
    VK = VK_LValue;
  } else {
    VK = LHS.get()->getValueKind();
  }

  return Result;
}

/// Try to convert a type to another according to C++11 5.16p3.
///
/// This is part of the parameter validation for the ? operator. If either
/// value operand is a class type, the two operands are attempted to be
/// converted to each other. This function does the conversion in one direction.
/// It returns true if the program is ill-formed and has already been diagnosed
/// as such.
static bool TryClassUnification(Sema &Self, Expr *From, Expr *To,
                                SourceLocation QuestionLoc,
                                bool &HaveConversion,
                                QualType &ToType) {
  HaveConversion = false;
  ToType = To->getType();

  InitializationKind Kind =
      InitializationKind::CreateCopy(To->getBeginLoc(), SourceLocation());
  // C++11 5.16p3
  //   The process for determining whether an operand expression E1 of type T1
  //   can be converted to match an operand expression E2 of type T2 is defined
  //   as follows:
  //   -- If E2 is an lvalue: E1 can be converted to match E2 if E1 can be
  //      implicitly converted to type "lvalue reference to T2", subject to the
  //      constraint that in the conversion the reference must bind directly to
  //      an lvalue.
  //   -- If E2 is an xvalue: E1 can be converted to match E2 if E1 can be
  //      implicitly converted to the type "rvalue reference to R2", subject to
  //      the constraint that the reference must bind directly.
  if (To->isGLValue()) {
    QualType T = Self.Context.getReferenceQualifiedType(To);
    InitializedEntity Entity = InitializedEntity::InitializeTemporary(T);

    InitializationSequence InitSeq(Self, Entity, Kind, From);
    if (InitSeq.isDirectReferenceBinding()) {
      ToType = T;
      HaveConversion = true;
      return false;
    }

    if (InitSeq.isAmbiguous())
      return InitSeq.Diagnose(Self, Entity, Kind, From);
  }

  //   -- If E2 is an rvalue, or if the conversion above cannot be done:
  //      -- if E1 and E2 have class type, and the underlying class types are
  //         the same or one is a base class of the other:
  QualType FTy = From->getType();
  QualType TTy = To->getType();
  const RecordType *FRec = FTy->getAs<RecordType>();
  const RecordType *TRec = TTy->getAs<RecordType>();
  bool FDerivedFromT = FRec && TRec && FRec != TRec &&
                       Self.IsDerivedFrom(QuestionLoc, FTy, TTy);
  if (FRec && TRec && (FRec == TRec || FDerivedFromT ||
                       Self.IsDerivedFrom(QuestionLoc, TTy, FTy))) {
    //         E1 can be converted to match E2 if the class of T2 is the
    //         same type as, or a base class of, the class of T1, and
    //         [cv2 > cv1].
    if (FRec == TRec || FDerivedFromT) {
      if (TTy.isAtLeastAsQualifiedAs(FTy)) {
        InitializedEntity Entity = InitializedEntity::InitializeTemporary(TTy);
        InitializationSequence InitSeq(Self, Entity, Kind, From);
        if (InitSeq) {
          HaveConversion = true;
          return false;
        }

        if (InitSeq.isAmbiguous())
          return InitSeq.Diagnose(Self, Entity, Kind, From);
      }
    }

    return false;
  }

  //     -- Otherwise: E1 can be converted to match E2 if E1 can be
  //        implicitly converted to the type that expression E2 would have
  //        if E2 were converted to an rvalue (or the type it has, if E2 is
  //        an rvalue).
  //
  // This actually refers very narrowly to the lvalue-to-rvalue conversion, not
  // to the array-to-pointer or function-to-pointer conversions.
  TTy = TTy.getNonLValueExprType(Self.Context);

  InitializedEntity Entity = InitializedEntity::InitializeTemporary(TTy);
  InitializationSequence InitSeq(Self, Entity, Kind, From);
  HaveConversion = !InitSeq.Failed();
  ToType = TTy;
  if (InitSeq.isAmbiguous())
    return InitSeq.Diagnose(Self, Entity, Kind, From);

  return false;
}

/// Try to find a common type for two according to C++0x 5.16p5.
///
/// This is part of the parameter validation for the ? operator. If either
/// value operand is a class type, overload resolution is used to find a
/// conversion to a common type.
static bool FindConditionalOverload(Sema &Self, ExprResult &LHS, ExprResult &RHS,
                                    SourceLocation QuestionLoc) {
  Expr *Args[2] = { LHS.get(), RHS.get() };
  OverloadCandidateSet CandidateSet(QuestionLoc,
                                    OverloadCandidateSet::CSK_Operator);
  Self.AddBuiltinOperatorCandidates(OO_Conditional, QuestionLoc, Args,
                                    CandidateSet);

  OverloadCandidateSet::iterator Best;
  switch (CandidateSet.BestViableFunction(Self, QuestionLoc, Best)) {
    case OR_Success: {
      // We found a match. Perform the conversions on the arguments and move on.
      ExprResult LHSRes = Self.PerformImplicitConversion(
          LHS.get(), Best->BuiltinParamTypes[0], Best->Conversions[0],
          Sema::AA_Converting);
      if (LHSRes.isInvalid())
        break;
      LHS = LHSRes;

      ExprResult RHSRes = Self.PerformImplicitConversion(
          RHS.get(), Best->BuiltinParamTypes[1], Best->Conversions[1],
          Sema::AA_Converting);
      if (RHSRes.isInvalid())
        break;
      RHS = RHSRes;
      if (Best->Function)
        Self.MarkFunctionReferenced(QuestionLoc, Best->Function);
      return false;
    }

    case OR_No_Viable_Function:

      // Emit a better diagnostic if one of the expressions is a null pointer
      // constant and the other is a pointer type. In this case, the user most
      // likely forgot to take the address of the other expression.
      if (Self.DiagnoseConditionalForNull(LHS.get(), RHS.get(), QuestionLoc))
        return true;

      Self.Diag(QuestionLoc, diag::err_typecheck_cond_incompatible_operands)
        << LHS.get()->getType() << RHS.get()->getType()
        << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
      return true;

    case OR_Ambiguous:
      Self.Diag(QuestionLoc, diag::err_conditional_ambiguous_ovl)
        << LHS.get()->getType() << RHS.get()->getType()
        << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
      // FIXME: Print the possible common types by printing the return types of
      // the viable candidates.
      break;

    case OR_Deleted:
      llvm_unreachable("Conditional operator has only built-in overloads");
  }
  return true;
}

/// Perform an "extended" implicit conversion as returned by
/// TryClassUnification.
static bool ConvertForConditional(Sema &Self, ExprResult &E, QualType T) {
  InitializedEntity Entity = InitializedEntity::InitializeTemporary(T);
  InitializationKind Kind =
      InitializationKind::CreateCopy(E.get()->getBeginLoc(), SourceLocation());
  Expr *Arg = E.get();
  InitializationSequence InitSeq(Self, Entity, Kind, Arg);
  ExprResult Result = InitSeq.Perform(Self, Entity, Kind, Arg);
  if (Result.isInvalid())
    return true;

  E = Result;
  return false;
}

// Check the condition operand of ?: to see if it is valid for the GCC
// extension.
static bool isValidVectorForConditionalCondition(ASTContext &Ctx,
                                                 QualType CondTy) {
  if (!CondTy->isVectorType() && !CondTy->isExtVectorType())
    return false;
  const QualType EltTy =
      cast<VectorType>(CondTy.getCanonicalType())->getElementType();
  assert(!EltTy->isEnumeralType() && "Vectors cant be enum types");
  return EltTy->isIntegralType(Ctx);
}

static bool isValidSizelessVectorForConditionalCondition(ASTContext &Ctx,
                                                         QualType CondTy) {
  if (!CondTy->isSveVLSBuiltinType())
    return false;
  const QualType EltTy =
      cast<BuiltinType>(CondTy.getCanonicalType())->getSveEltType(Ctx);
  assert(!EltTy->isEnumeralType() && "Vectors cant be enum types");
  return EltTy->isIntegralType(Ctx);
}

QualType Sema::CheckVectorConditionalTypes(ExprResult &Cond, ExprResult &LHS,
                                           ExprResult &RHS,
                                           SourceLocation QuestionLoc) {
  LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());

  QualType CondType = Cond.get()->getType();
  const auto *CondVT = CondType->castAs<VectorType>();
  QualType CondElementTy = CondVT->getElementType();
  unsigned CondElementCount = CondVT->getNumElements();
  QualType LHSType = LHS.get()->getType();
  const auto *LHSVT = LHSType->getAs<VectorType>();
  QualType RHSType = RHS.get()->getType();
  const auto *RHSVT = RHSType->getAs<VectorType>();

  QualType ResultType;


  if (LHSVT && RHSVT) {
    if (isa<ExtVectorType>(CondVT) != isa<ExtVectorType>(LHSVT)) {
      Diag(QuestionLoc, diag::err_conditional_vector_cond_result_mismatch)
          << /*isExtVector*/ isa<ExtVectorType>(CondVT);
      return {};
    }

    // If both are vector types, they must be the same type.
    if (!Context.hasSameType(LHSType, RHSType)) {
      Diag(QuestionLoc, diag::err_conditional_vector_mismatched)
          << LHSType << RHSType;
      return {};
    }
    ResultType = Context.getCommonSugaredType(LHSType, RHSType);
  } else if (LHSVT || RHSVT) {
    ResultType = CheckVectorOperands(
        LHS, RHS, QuestionLoc, /*isCompAssign*/ false, /*AllowBothBool*/ true,
        /*AllowBoolConversions*/ false,
        /*AllowBoolOperation*/ true,
        /*ReportInvalid*/ true);
    if (ResultType.isNull())
      return {};
  } else {
    // Both are scalar.
    LHSType = LHSType.getUnqualifiedType();
    RHSType = RHSType.getUnqualifiedType();
    QualType ResultElementTy =
        Context.hasSameType(LHSType, RHSType)
            ? Context.getCommonSugaredType(LHSType, RHSType)
            : UsualArithmeticConversions(LHS, RHS, QuestionLoc,
                                         ACK_Conditional);

    if (ResultElementTy->isEnumeralType()) {
      Diag(QuestionLoc, diag::err_conditional_vector_operand_type)
          << ResultElementTy;
      return {};
    }
    if (CondType->isExtVectorType())
      ResultType =
          Context.getExtVectorType(ResultElementTy, CondVT->getNumElements());
    else
      ResultType = Context.getVectorType(
          ResultElementTy, CondVT->getNumElements(), VectorKind::Generic);

    LHS = ImpCastExprToType(LHS.get(), ResultType, CK_VectorSplat);
    RHS = ImpCastExprToType(RHS.get(), ResultType, CK_VectorSplat);
  }

  assert(!ResultType.isNull() && ResultType->isVectorType() &&
         (!CondType->isExtVectorType() || ResultType->isExtVectorType()) &&
         "Result should have been a vector type");
  auto *ResultVectorTy = ResultType->castAs<VectorType>();
  QualType ResultElementTy = ResultVectorTy->getElementType();
  unsigned ResultElementCount = ResultVectorTy->getNumElements();

  if (ResultElementCount != CondElementCount) {
    Diag(QuestionLoc, diag::err_conditional_vector_size) << CondType
                                                         << ResultType;
    return {};
  }

  if (Context.getTypeSize(ResultElementTy) !=
      Context.getTypeSize(CondElementTy)) {
    Diag(QuestionLoc, diag::err_conditional_vector_element_size) << CondType
                                                                 << ResultType;
    return {};
  }

  return ResultType;
}

QualType Sema::CheckSizelessVectorConditionalTypes(ExprResult &Cond,
                                                   ExprResult &LHS,
                                                   ExprResult &RHS,
                                                   SourceLocation QuestionLoc) {
  LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());

  QualType CondType = Cond.get()->getType();
  const auto *CondBT = CondType->castAs<BuiltinType>();
  QualType CondElementTy = CondBT->getSveEltType(Context);
  llvm::ElementCount CondElementCount =
      Context.getBuiltinVectorTypeInfo(CondBT).EC;

  QualType LHSType = LHS.get()->getType();
  const auto *LHSBT =
      LHSType->isSveVLSBuiltinType() ? LHSType->getAs<BuiltinType>() : nullptr;
  QualType RHSType = RHS.get()->getType();
  const auto *RHSBT =
      RHSType->isSveVLSBuiltinType() ? RHSType->getAs<BuiltinType>() : nullptr;

  QualType ResultType;

  if (LHSBT && RHSBT) {
    // If both are sizeless vector types, they must be the same type.
    if (!Context.hasSameType(LHSType, RHSType)) {
      Diag(QuestionLoc, diag::err_conditional_vector_mismatched)
          << LHSType << RHSType;
      return QualType();
    }
    ResultType = LHSType;
  } else if (LHSBT || RHSBT) {
    ResultType = CheckSizelessVectorOperands(
        LHS, RHS, QuestionLoc, /*IsCompAssign*/ false, ACK_Conditional);
    if (ResultType.isNull())
      return QualType();
  } else {
    // Both are scalar so splat
    QualType ResultElementTy;
    LHSType = LHSType.getCanonicalType().getUnqualifiedType();
    RHSType = RHSType.getCanonicalType().getUnqualifiedType();

    if (Context.hasSameType(LHSType, RHSType))
      ResultElementTy = LHSType;
    else
      ResultElementTy =
          UsualArithmeticConversions(LHS, RHS, QuestionLoc, ACK_Conditional);

    if (ResultElementTy->isEnumeralType()) {
      Diag(QuestionLoc, diag::err_conditional_vector_operand_type)
          << ResultElementTy;
      return QualType();
    }

    ResultType = Context.getScalableVectorType(
        ResultElementTy, CondElementCount.getKnownMinValue());

    LHS = ImpCastExprToType(LHS.get(), ResultType, CK_VectorSplat);
    RHS = ImpCastExprToType(RHS.get(), ResultType, CK_VectorSplat);
  }

  assert(!ResultType.isNull() && ResultType->isSveVLSBuiltinType() &&
         "Result should have been a vector type");
  auto *ResultBuiltinTy = ResultType->castAs<BuiltinType>();
  QualType ResultElementTy = ResultBuiltinTy->getSveEltType(Context);
  llvm::ElementCount ResultElementCount =
      Context.getBuiltinVectorTypeInfo(ResultBuiltinTy).EC;

  if (ResultElementCount != CondElementCount) {
    Diag(QuestionLoc, diag::err_conditional_vector_size)
        << CondType << ResultType;
    return QualType();
  }

  if (Context.getTypeSize(ResultElementTy) !=
      Context.getTypeSize(CondElementTy)) {
    Diag(QuestionLoc, diag::err_conditional_vector_element_size)
        << CondType << ResultType;
    return QualType();
  }

  return ResultType;
}

QualType Sema::CXXCheckConditionalOperands(ExprResult &Cond, ExprResult &LHS,
                                           ExprResult &RHS, ExprValueKind &VK,
                                           ExprObjectKind &OK,
                                           SourceLocation QuestionLoc) {
  // FIXME: Handle C99's complex types, block pointers and Obj-C++ interface
  // pointers.

  // Assume r-value.
  VK = VK_PRValue;
  OK = OK_Ordinary;
  bool IsVectorConditional =
      isValidVectorForConditionalCondition(Context, Cond.get()->getType());

  bool IsSizelessVectorConditional =
      isValidSizelessVectorForConditionalCondition(Context,
                                                   Cond.get()->getType());

  // C++11 [expr.cond]p1
  //   The first expression is contextually converted to bool.
  if (!Cond.get()->isTypeDependent()) {
    ExprResult CondRes = IsVectorConditional || IsSizelessVectorConditional
                             ? DefaultFunctionArrayLvalueConversion(Cond.get())
                             : CheckCXXBooleanCondition(Cond.get());
    if (CondRes.isInvalid())
      return QualType();
    Cond = CondRes;
  } else {
    // To implement C++, the first expression typically doesn't alter the result
    // type of the conditional, however the GCC compatible vector extension
    // changes the result type to be that of the conditional. Since we cannot
    // know if this is a vector extension here, delay the conversion of the
    // LHS/RHS below until later.
    return Context.DependentTy;
  }


  // Either of the arguments dependent?
  if (LHS.get()->isTypeDependent() || RHS.get()->isTypeDependent())
    return Context.DependentTy;

  // C++11 [expr.cond]p2
  //   If either the second or the third operand has type (cv) void, ...
  QualType LTy = LHS.get()->getType();
  QualType RTy = RHS.get()->getType();
  bool LVoid = LTy->isVoidType();
  bool RVoid = RTy->isVoidType();
  if (LVoid || RVoid) {
    //   ... one of the following shall hold:
    //   -- The second or the third operand (but not both) is a (possibly
    //      parenthesized) throw-expression; the result is of the type
    //      and value category of the other.
    bool LThrow = isa<CXXThrowExpr>(LHS.get()->IgnoreParenImpCasts());
    bool RThrow = isa<CXXThrowExpr>(RHS.get()->IgnoreParenImpCasts());

    // Void expressions aren't legal in the vector-conditional expressions.
    if (IsVectorConditional) {
      SourceRange DiagLoc =
          LVoid ? LHS.get()->getSourceRange() : RHS.get()->getSourceRange();
      bool IsThrow = LVoid ? LThrow : RThrow;
      Diag(DiagLoc.getBegin(), diag::err_conditional_vector_has_void)
          << DiagLoc << IsThrow;
      return QualType();
    }

    if (LThrow != RThrow) {
      Expr *NonThrow = LThrow ? RHS.get() : LHS.get();
      VK = NonThrow->getValueKind();
      // DR (no number yet): the result is a bit-field if the
      // non-throw-expression operand is a bit-field.
      OK = NonThrow->getObjectKind();
      return NonThrow->getType();
    }

    //   -- Both the second and third operands have type void; the result is of
    //      type void and is a prvalue.
    if (LVoid && RVoid)
      return Context.getCommonSugaredType(LTy, RTy);

    // Neither holds, error.
    Diag(QuestionLoc, diag::err_conditional_void_nonvoid)
      << (LVoid ? RTy : LTy) << (LVoid ? 0 : 1)
      << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
    return QualType();
  }

  // Neither is void.
  if (IsVectorConditional)
    return CheckVectorConditionalTypes(Cond, LHS, RHS, QuestionLoc);

  if (IsSizelessVectorConditional)
    return CheckSizelessVectorConditionalTypes(Cond, LHS, RHS, QuestionLoc);

  // WebAssembly tables are not allowed as conditional LHS or RHS.
  if (LTy->isWebAssemblyTableType() || RTy->isWebAssemblyTableType()) {
    Diag(QuestionLoc, diag::err_wasm_table_conditional_expression)
        << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
    return QualType();
  }

  // C++11 [expr.cond]p3
  //   Otherwise, if the second and third operand have different types, and
  //   either has (cv) class type [...] an attempt is made to convert each of
  //   those operands to the type of the other.
  if (!Context.hasSameType(LTy, RTy) &&
      (LTy->isRecordType() || RTy->isRecordType())) {
    // These return true if a single direction is already ambiguous.
    QualType L2RType, R2LType;
    bool HaveL2R, HaveR2L;
    if (TryClassUnification(*this, LHS.get(), RHS.get(), QuestionLoc, HaveL2R, L2RType))
      return QualType();
    if (TryClassUnification(*this, RHS.get(), LHS.get(), QuestionLoc, HaveR2L, R2LType))
      return QualType();

    //   If both can be converted, [...] the program is ill-formed.
    if (HaveL2R && HaveR2L) {
      Diag(QuestionLoc, diag::err_conditional_ambiguous)
        << LTy << RTy << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
      return QualType();
    }

    //   If exactly one conversion is possible, that conversion is applied to
    //   the chosen operand and the converted operands are used in place of the
    //   original operands for the remainder of this section.
    if (HaveL2R) {
      if (ConvertForConditional(*this, LHS, L2RType) || LHS.isInvalid())
        return QualType();
      LTy = LHS.get()->getType();
    } else if (HaveR2L) {
      if (ConvertForConditional(*this, RHS, R2LType) || RHS.isInvalid())
        return QualType();
      RTy = RHS.get()->getType();
    }
  }

  // C++11 [expr.cond]p3
  //   if both are glvalues of the same value category and the same type except
  //   for cv-qualification, an attempt is made to convert each of those
  //   operands to the type of the other.
  // FIXME:
  //   Resolving a defect in P0012R1: we extend this to cover all cases where
  //   one of the operands is reference-compatible with the other, in order
  //   to support conditionals between functions differing in noexcept. This
  //   will similarly cover difference in array bounds after P0388R4.
  // FIXME: If LTy and RTy have a composite pointer type, should we convert to
  //   that instead?
  ExprValueKind LVK = LHS.get()->getValueKind();
  ExprValueKind RVK = RHS.get()->getValueKind();
  if (!Context.hasSameType(LTy, RTy) && LVK == RVK && LVK != VK_PRValue) {
    // DerivedToBase was already handled by the class-specific case above.
    // FIXME: Should we allow ObjC conversions here?
    const ReferenceConversions AllowedConversions =
        ReferenceConversions::Qualification |
        ReferenceConversions::NestedQualification |
        ReferenceConversions::Function;

    ReferenceConversions RefConv;
    if (CompareReferenceRelationship(QuestionLoc, LTy, RTy, &RefConv) ==
            Ref_Compatible &&
        !(RefConv & ~AllowedConversions) &&
        // [...] subject to the constraint that the reference must bind
        // directly [...]
        !RHS.get()->refersToBitField() && !RHS.get()->refersToVectorElement()) {
      RHS = ImpCastExprToType(RHS.get(), LTy, CK_NoOp, RVK);
      RTy = RHS.get()->getType();
    } else if (CompareReferenceRelationship(QuestionLoc, RTy, LTy, &RefConv) ==
                   Ref_Compatible &&
               !(RefConv & ~AllowedConversions) &&
               !LHS.get()->refersToBitField() &&
               !LHS.get()->refersToVectorElement()) {
      LHS = ImpCastExprToType(LHS.get(), RTy, CK_NoOp, LVK);
      LTy = LHS.get()->getType();
    }
  }

  // C++11 [expr.cond]p4
  //   If the second and third operands are glvalues of the same value
  //   category and have the same type, the result is of that type and
  //   value category and it is a bit-field if the second or the third
  //   operand is a bit-field, or if both are bit-fields.
  // We only extend this to bitfields, not to the crazy other kinds of
  // l-values.
  bool Same = Context.hasSameType(LTy, RTy);
  if (Same && LVK == RVK && LVK != VK_PRValue &&
      LHS.get()->isOrdinaryOrBitFieldObject() &&
      RHS.get()->isOrdinaryOrBitFieldObject()) {
    VK = LHS.get()->getValueKind();
    if (LHS.get()->getObjectKind() == OK_BitField ||
        RHS.get()->getObjectKind() == OK_BitField)
      OK = OK_BitField;
    return Context.getCommonSugaredType(LTy, RTy);
  }

  // C++11 [expr.cond]p5
  //   Otherwise, the result is a prvalue. If the second and third operands
  //   do not have the same type, and either has (cv) class type, ...
  if (!Same && (LTy->isRecordType() || RTy->isRecordType())) {
    //   ... overload resolution is used to determine the conversions (if any)
    //   to be applied to the operands. If the overload resolution fails, the
    //   program is ill-formed.
    if (FindConditionalOverload(*this, LHS, RHS, QuestionLoc))
      return QualType();
  }

  // C++11 [expr.cond]p6
  //   Lvalue-to-rvalue, array-to-pointer, and function-to-pointer standard
  //   conversions are performed on the second and third operands.
  LHS = DefaultFunctionArrayLvalueConversion(LHS.get());
  RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();
  LTy = LHS.get()->getType();
  RTy = RHS.get()->getType();

  //   After those conversions, one of the following shall hold:
  //   -- The second and third operands have the same type; the result
  //      is of that type. If the operands have class type, the result
  //      is a prvalue temporary of the result type, which is
  //      copy-initialized from either the second operand or the third
  //      operand depending on the value of the first operand.
  if (Context.hasSameType(LTy, RTy)) {
    if (LTy->isRecordType()) {
      // The operands have class type. Make a temporary copy.
      ExprResult LHSCopy = PerformCopyInitialization(
          InitializedEntity::InitializeTemporary(LTy), SourceLocation(), LHS);
      if (LHSCopy.isInvalid())
        return QualType();

      ExprResult RHSCopy = PerformCopyInitialization(
          InitializedEntity::InitializeTemporary(RTy), SourceLocation(), RHS);
      if (RHSCopy.isInvalid())
        return QualType();

      LHS = LHSCopy;
      RHS = RHSCopy;
    }
    return Context.getCommonSugaredType(LTy, RTy);
  }

  // Extension: conditional operator involving vector types.
  if (LTy->isVectorType() || RTy->isVectorType())
    return CheckVectorOperands(LHS, RHS, QuestionLoc, /*isCompAssign*/ false,
                               /*AllowBothBool*/ true,
                               /*AllowBoolConversions*/ false,
                               /*AllowBoolOperation*/ false,
                               /*ReportInvalid*/ true);

  //   -- The second and third operands have arithmetic or enumeration type;
  //      the usual arithmetic conversions are performed to bring them to a
  //      common type, and the result is of that type.
  if (LTy->isArithmeticType() && RTy->isArithmeticType()) {
    QualType ResTy =
        UsualArithmeticConversions(LHS, RHS, QuestionLoc, ACK_Conditional);
    if (LHS.isInvalid() || RHS.isInvalid())
      return QualType();
    if (ResTy.isNull()) {
      Diag(QuestionLoc,
           diag::err_typecheck_cond_incompatible_operands) << LTy << RTy
        << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
      return QualType();
    }

    LHS = ImpCastExprToType(LHS.get(), ResTy, PrepareScalarCast(LHS, ResTy));
    RHS = ImpCastExprToType(RHS.get(), ResTy, PrepareScalarCast(RHS, ResTy));

    return ResTy;
  }

  //   -- The second and third operands have pointer type, or one has pointer
  //      type and the other is a null pointer constant, or both are null
  //      pointer constants, at least one of which is non-integral; pointer
  //      conversions and qualification conversions are performed to bring them
  //      to their composite pointer type. The result is of the composite
  //      pointer type.
  //   -- The second and third operands have pointer to member type, or one has
  //      pointer to member type and the other is a null pointer constant;
  //      pointer to member conversions and qualification conversions are
  //      performed to bring them to a common type, whose cv-qualification
  //      shall match the cv-qualification of either the second or the third
  //      operand. The result is of the common type.
  QualType Composite = FindCompositePointerType(QuestionLoc, LHS, RHS);
  if (!Composite.isNull())
    return Composite;

  // Similarly, attempt to find composite type of two objective-c pointers.
  Composite = ObjC().FindCompositeObjCPointerType(LHS, RHS, QuestionLoc);
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();
  if (!Composite.isNull())
    return Composite;

  // Check if we are using a null with a non-pointer type.
  if (DiagnoseConditionalForNull(LHS.get(), RHS.get(), QuestionLoc))
    return QualType();

  Diag(QuestionLoc, diag::err_typecheck_cond_incompatible_operands)
    << LHS.get()->getType() << RHS.get()->getType()
    << LHS.get()->getSourceRange() << RHS.get()->getSourceRange();
  return QualType();
}

QualType Sema::FindCompositePointerType(SourceLocation Loc,
                                        Expr *&E1, Expr *&E2,
                                        bool ConvertArgs) {
  assert(getLangOpts().CPlusPlus && "This function assumes C++");

  // C++1z [expr]p14:
  //   The composite pointer type of two operands p1 and p2 having types T1
  //   and T2
  QualType T1 = E1->getType(), T2 = E2->getType();

  //   where at least one is a pointer or pointer to member type or
  //   std::nullptr_t is:
  bool T1IsPointerLike = T1->isAnyPointerType() || T1->isMemberPointerType() ||
                         T1->isNullPtrType();
  bool T2IsPointerLike = T2->isAnyPointerType() || T2->isMemberPointerType() ||
                         T2->isNullPtrType();
  if (!T1IsPointerLike && !T2IsPointerLike)
    return QualType();

  //   - if both p1 and p2 are null pointer constants, std::nullptr_t;
  // This can't actually happen, following the standard, but we also use this
  // to implement the end of [expr.conv], which hits this case.
  //
  //   - if either p1 or p2 is a null pointer constant, T2 or T1, respectively;
  if (T1IsPointerLike &&
      E2->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNull)) {
    if (ConvertArgs)
      E2 = ImpCastExprToType(E2, T1, T1->isMemberPointerType()
                                         ? CK_NullToMemberPointer
                                         : CK_NullToPointer).get();
    return T1;
  }
  if (T2IsPointerLike &&
      E1->isNullPointerConstant(Context, Expr::NPC_ValueDependentIsNull)) {
    if (ConvertArgs)
      E1 = ImpCastExprToType(E1, T2, T2->isMemberPointerType()
                                         ? CK_NullToMemberPointer
                                         : CK_NullToPointer).get();
    return T2;
  }

  // Now both have to be pointers or member pointers.
  if (!T1IsPointerLike || !T2IsPointerLike)
    return QualType();
  assert(!T1->isNullPtrType() && !T2->isNullPtrType() &&
         "nullptr_t should be a null pointer constant");

  struct Step {
    enum Kind { Pointer, ObjCPointer, MemberPointer, Array } K;
    // Qualifiers to apply under the step kind.
    Qualifiers Quals;
    /// The class for a pointer-to-member; a constant array type with a bound
    /// (if any) for an array.
    const Type *ClassOrBound;

    Step(Kind K, const Type *ClassOrBound = nullptr)
        : K(K), ClassOrBound(ClassOrBound) {}
    QualType rebuild(ASTContext &Ctx, QualType T) const {
      T = Ctx.getQualifiedType(T, Quals);
      switch (K) {
      case Pointer:
        return Ctx.getPointerType(T);
      case MemberPointer:
        return Ctx.getMemberPointerType(T, ClassOrBound);
      case ObjCPointer:
        return Ctx.getObjCObjectPointerType(T);
      case Array:
        if (auto *CAT = cast_or_null<ConstantArrayType>(ClassOrBound))
          return Ctx.getConstantArrayType(T, CAT->getSize(), nullptr,
                                          ArraySizeModifier::Normal, 0);
        else
          return Ctx.getIncompleteArrayType(T, ArraySizeModifier::Normal, 0);
      }
      llvm_unreachable("unknown step kind");
    }
  };

  SmallVector<Step, 8> Steps;

  //  - if T1 is "pointer to cv1 C1" and T2 is "pointer to cv2 C2", where C1
  //    is reference-related to C2 or C2 is reference-related to C1 (8.6.3),
  //    the cv-combined type of T1 and T2 or the cv-combined type of T2 and T1,
  //    respectively;
  //  - if T1 is "pointer to member of C1 of type cv1 U1" and T2 is "pointer
  //    to member of C2 of type cv2 U2" for some non-function type U, where
  //    C1 is reference-related to C2 or C2 is reference-related to C1, the
  //    cv-combined type of T2 and T1 or the cv-combined type of T1 and T2,
  //    respectively;
  //  - if T1 and T2 are similar types (4.5), the cv-combined type of T1 and
  //    T2;
  //
  // Dismantle T1 and T2 to simultaneously determine whether they are similar
  // and to prepare to form the cv-combined type if so.
  QualType Composite1 = T1;
  QualType Composite2 = T2;
  unsigned NeedConstBefore = 0;
  while (true) {
    assert(!Composite1.isNull() && !Composite2.isNull());

    Qualifiers Q1, Q2;
    Composite1 = Context.getUnqualifiedArrayType(Composite1, Q1);
    Composite2 = Context.getUnqualifiedArrayType(Composite2, Q2);

    // Top-level qualifiers are ignored. Merge at all lower levels.
    if (!Steps.empty()) {
      // Find the qualifier union: (approximately) the unique minimal set of
      // qualifiers that is compatible with both types.
      Qualifiers Quals = Qualifiers::fromCVRUMask(Q1.getCVRUQualifiers() |
                                                  Q2.getCVRUQualifiers());

      // Under one level of pointer or pointer-to-member, we can change to an
      // unambiguous compatible address space.
      if (Q1.getAddressSpace() == Q2.getAddressSpace()) {
        Quals.setAddressSpace(Q1.getAddressSpace());
      } else if (Steps.size() == 1) {
        bool MaybeQ1 = Q1.isAddressSpaceSupersetOf(Q2);
        bool MaybeQ2 = Q2.isAddressSpaceSupersetOf(Q1);
        if (MaybeQ1 == MaybeQ2) {
          // Exception for ptr size address spaces. Should be able to choose
          // either address space during comparison.
          if (isPtrSizeAddressSpace(Q1.getAddressSpace()) ||
              isPtrSizeAddressSpace(Q2.getAddressSpace()))
            MaybeQ1 = true;
          else
            return QualType(); // No unique best address space.
        }
        Quals.setAddressSpace(MaybeQ1 ? Q1.getAddressSpace()
                                      : Q2.getAddressSpace());
      } else {
        return QualType();
      }

      // FIXME: In C, we merge __strong and none to __strong at the top level.
      if (Q1.getObjCGCAttr() == Q2.getObjCGCAttr())
        Quals.setObjCGCAttr(Q1.getObjCGCAttr());
      else if (T1->isVoidPointerType() || T2->isVoidPointerType())
        assert(Steps.size() == 1);
      else
        return QualType();

      // Mismatched lifetime qualifiers never compatibly include each other.
      if (Q1.getObjCLifetime() == Q2.getObjCLifetime())
        Quals.setObjCLifetime(Q1.getObjCLifetime());
      else if (T1->isVoidPointerType() || T2->isVoidPointerType())
        assert(Steps.size() == 1);
      else
        return QualType();

      Steps.back().Quals = Quals;
      if (Q1 != Quals || Q2 != Quals)
        NeedConstBefore = Steps.size() - 1;
    }

    // FIXME: Can we unify the following with UnwrapSimilarTypes?

    const ArrayType *Arr1, *Arr2;
    if ((Arr1 = Context.getAsArrayType(Composite1)) &&
        (Arr2 = Context.getAsArrayType(Composite2))) {
      auto *CAT1 = dyn_cast<ConstantArrayType>(Arr1);
      auto *CAT2 = dyn_cast<ConstantArrayType>(Arr2);
      if (CAT1 && CAT2 && CAT1->getSize() == CAT2->getSize()) {
        Composite1 = Arr1->getElementType();
        Composite2 = Arr2->getElementType();
        Steps.emplace_back(Step::Array, CAT1);
        continue;
      }
      bool IAT1 = isa<IncompleteArrayType>(Arr1);
      bool IAT2 = isa<IncompleteArrayType>(Arr2);
      if ((IAT1 && IAT2) ||
          (getLangOpts().CPlusPlus20 && (IAT1 != IAT2) &&
           ((bool)CAT1 != (bool)CAT2) &&
           (Steps.empty() || Steps.back().K != Step::Array))) {
        // In C++20 onwards, we can unify an array of N T with an array of
        // a different or unknown bound. But we can't form an array whose
        // element type is an array of unknown bound by doing so.
        Composite1 = Arr1->getElementType();
        Composite2 = Arr2->getElementType();
        Steps.emplace_back(Step::Array);
        if (CAT1 || CAT2)
          NeedConstBefore = Steps.size();
        continue;
      }
    }

    const PointerType *Ptr1, *Ptr2;
    if ((Ptr1 = Composite1->getAs<PointerType>()) &&
        (Ptr2 = Composite2->getAs<PointerType>())) {
      Composite1 = Ptr1->getPointeeType();
      Composite2 = Ptr2->getPointeeType();
      Steps.emplace_back(Step::Pointer);
      continue;
    }

    const ObjCObjectPointerType *ObjPtr1, *ObjPtr2;
    if ((ObjPtr1 = Composite1->getAs<ObjCObjectPointerType>()) &&
        (ObjPtr2 = Composite2->getAs<ObjCObjectPointerType>())) {
      Composite1 = ObjPtr1->getPointeeType();
      Composite2 = ObjPtr2->getPointeeType();
      Steps.emplace_back(Step::ObjCPointer);
      continue;
    }

    const MemberPointerType *MemPtr1, *MemPtr2;
    if ((MemPtr1 = Composite1->getAs<MemberPointerType>()) &&
        (MemPtr2 = Composite2->getAs<MemberPointerType>())) {
      Composite1 = MemPtr1->getPointeeType();
      Composite2 = MemPtr2->getPointeeType();

      // At the top level, we can perform a base-to-derived pointer-to-member
      // conversion:
      //
      //  - [...] where C1 is reference-related to C2 or C2 is
      //    reference-related to C1
      //
      // (Note that the only kinds of reference-relatedness in scope here are
      // "same type or derived from".) At any other level, the class must
      // exactly match.
      const Type *Class = nullptr;
      QualType Cls1(MemPtr1->getClass(), 0);
      QualType Cls2(MemPtr2->getClass(), 0);
      if (Context.hasSameType(Cls1, Cls2))
        Class = MemPtr1->getClass();
      else if (Steps.empty())
        Class = IsDerivedFrom(Loc, Cls1, Cls2) ? MemPtr1->getClass() :
                IsDerivedFrom(Loc, Cls2, Cls1) ? MemPtr2->getClass() : nullptr;
      if (!Class)
        return QualType();

      Steps.emplace_back(Step::MemberPointer, Class);
      continue;
    }

    // Special case: at the top level, we can decompose an Objective-C pointer
    // and a 'cv void *'. Unify the qualifiers.
    if (Steps.empty() && ((Composite1->isVoidPointerType() &&
                           Composite2->isObjCObjectPointerType()) ||
                          (Composite1->isObjCObjectPointerType() &&
                           Composite2->isVoidPointerType()))) {
      Composite1 = Composite1->getPointeeType();
      Composite2 = Composite2->getPointeeType();
      Steps.emplace_back(Step::Pointer);
      continue;
    }

    // FIXME: block pointer types?

    // Cannot unwrap any more types.
    break;
  }

  //  - if T1 or T2 is "pointer to noexcept function" and the other type is
  //    "pointer to function", where the function types are otherwise the same,
  //    "pointer to function";
  //  - if T1 or T2 is "pointer to member of C1 of type function", the other
  //    type is "pointer to member of C2 of type noexcept function", and C1
  //    is reference-related to C2 or C2 is reference-related to C1, where
  //    the function types are otherwise the same, "pointer to member of C2 of
  //    type function" or "pointer to member of C1 of type function",
  //    respectively;
  //
  // We also support 'noreturn' here, so as a Clang extension we generalize the
  // above to:
  //
  //  - [Clang] If T1 and T2 are both of type "pointer to function" or
  //    "pointer to member function" and the pointee types can be unified
  //    by a function pointer conversion, that conversion is applied
  //    before checking the following rules.
  //
  // We've already unwrapped down to the function types, and we want to merge
  // rather than just convert, so do this ourselves rather than calling
  // IsFunctionConversion.
  //
  // FIXME: In order to match the standard wording as closely as possible, we
  // currently only do this under a single level of pointers. Ideally, we would
  // allow this in general, and set NeedConstBefore to the relevant depth on
  // the side(s) where we changed anything. If we permit that, we should also
  // consider this conversion when determining type similarity and model it as
  // a qualification conversion.
  if (Steps.size() == 1) {
    if (auto *FPT1 = Composite1->getAs<FunctionProtoType>()) {
      if (auto *FPT2 = Composite2->getAs<FunctionProtoType>()) {
        FunctionProtoType::ExtProtoInfo EPI1 = FPT1->getExtProtoInfo();
        FunctionProtoType::ExtProtoInfo EPI2 = FPT2->getExtProtoInfo();

        // The result is noreturn if both operands are.
        bool Noreturn =
            EPI1.ExtInfo.getNoReturn() && EPI2.ExtInfo.getNoReturn();
        EPI1.ExtInfo = EPI1.ExtInfo.withNoReturn(Noreturn);
        EPI2.ExtInfo = EPI2.ExtInfo.withNoReturn(Noreturn);

        // The result is nothrow if both operands are.
        SmallVector<QualType, 8> ExceptionTypeStorage;
        EPI1.ExceptionSpec = EPI2.ExceptionSpec = Context.mergeExceptionSpecs(
            EPI1.ExceptionSpec, EPI2.ExceptionSpec, ExceptionTypeStorage,
            getLangOpts().CPlusPlus17);

        Composite1 = Context.getFunctionType(FPT1->getReturnType(),
                                             FPT1->getParamTypes(), EPI1);
        Composite2 = Context.getFunctionType(FPT2->getReturnType(),
                                             FPT2->getParamTypes(), EPI2);
      }
    }
  }

  // There are some more conversions we can perform under exactly one pointer.
  if (Steps.size() == 1 && Steps.front().K == Step::Pointer &&
      !Context.hasSameType(Composite1, Composite2)) {
    //  - if T1 or T2 is "pointer to cv1 void" and the other type is
    //    "pointer to cv2 T", where T is an object type or void,
    //    "pointer to cv12 void", where cv12 is the union of cv1 and cv2;
    if (Composite1->isVoidType() && Composite2->isObjectType())
      Composite2 = Composite1;
    else if (Composite2->isVoidType() && Composite1->isObjectType())
      Composite1 = Composite2;
    //  - if T1 is "pointer to cv1 C1" and T2 is "pointer to cv2 C2", where C1
    //    is reference-related to C2 or C2 is reference-related to C1 (8.6.3),
    //    the cv-combined type of T1 and T2 or the cv-combined type of T2 and
    //    T1, respectively;
    //
    // The "similar type" handling covers all of this except for the "T1 is a
    // base class of T2" case in the definition of reference-related.
    else if (IsDerivedFrom(Loc, Composite1, Composite2))
      Composite1 = Composite2;
    else if (IsDerivedFrom(Loc, Composite2, Composite1))
      Composite2 = Composite1;
  }

  // At this point, either the inner types are the same or we have failed to
  // find a composite pointer type.
  if (!Context.hasSameType(Composite1, Composite2))
    return QualType();

  // Per C++ [conv.qual]p3, add 'const' to every level before the last
  // differing qualifier.
  for (unsigned I = 0; I != NeedConstBefore; ++I)
    Steps[I].Quals.addConst();

  // Rebuild the composite type.
  QualType Composite = Context.getCommonSugaredType(Composite1, Composite2);
  for (auto &S : llvm::reverse(Steps))
    Composite = S.rebuild(Context, Composite);

  if (ConvertArgs) {
    // Convert the expressions to the composite pointer type.
    InitializedEntity Entity =
        InitializedEntity::InitializeTemporary(Composite);
    InitializationKind Kind =
        InitializationKind::CreateCopy(Loc, SourceLocation());

    InitializationSequence E1ToC(*this, Entity, Kind, E1);
    if (!E1ToC)
      return QualType();

    InitializationSequence E2ToC(*this, Entity, Kind, E2);
    if (!E2ToC)
      return QualType();

    // FIXME: Let the caller know if these fail to avoid duplicate diagnostics.
    ExprResult E1Result = E1ToC.Perform(*this, Entity, Kind, E1);
    if (E1Result.isInvalid())
      return QualType();
    E1 = E1Result.get();

    ExprResult E2Result = E2ToC.Perform(*this, Entity, Kind, E2);
    if (E2Result.isInvalid())
      return QualType();
    E2 = E2Result.get();
  }

  return Composite;
}

ExprResult Sema::MaybeBindToTemporary(Expr *E) {
  if (!E)
    return ExprError();

  assert(!isa<CXXBindTemporaryExpr>(E) && "Double-bound temporary?");

  // If the result is a glvalue, we shouldn't bind it.
  if (E->isGLValue())
    return E;

  // In ARC, calls that return a retainable type can return retained,
  // in which case we have to insert a consuming cast.
  if (getLangOpts().ObjCAutoRefCount &&
      E->getType()->isObjCRetainableType()) {

    bool ReturnsRetained;

    // For actual calls, we compute this by examining the type of the
    // called value.
    if (CallExpr *Call = dyn_cast<CallExpr>(E)) {
      Expr *Callee = Call->getCallee()->IgnoreParens();
      QualType T = Callee->getType();

      if (T == Context.BoundMemberTy) {
        // Handle pointer-to-members.
        if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(Callee))
          T = BinOp->getRHS()->getType();
        else if (MemberExpr *Mem = dyn_cast<MemberExpr>(Callee))
          T = Mem->getMemberDecl()->getType();
      }

      if (const PointerType *Ptr = T->getAs<PointerType>())
        T = Ptr->getPointeeType();
      else if (const BlockPointerType *Ptr = T->getAs<BlockPointerType>())
        T = Ptr->getPointeeType();
      else if (const MemberPointerType *MemPtr = T->getAs<MemberPointerType>())
        T = MemPtr->getPointeeType();

      auto *FTy = T->castAs<FunctionType>();
      ReturnsRetained = FTy->getExtInfo().getProducesResult();

    // ActOnStmtExpr arranges things so that StmtExprs of retainable
    // type always produce a +1 object.
    } else if (isa<StmtExpr>(E)) {
      ReturnsRetained = true;

    // We hit this case with the lambda conversion-to-block optimization;
    // we don't want any extra casts here.
    } else if (isa<CastExpr>(E) &&
               isa<BlockExpr>(cast<CastExpr>(E)->getSubExpr())) {
      return E;

    // For message sends and property references, we try to find an
    // actual method.  FIXME: we should infer retention by selector in
    // cases where we don't have an actual method.
    } else {
      ObjCMethodDecl *D = nullptr;
      if (ObjCMessageExpr *Send = dyn_cast<ObjCMessageExpr>(E)) {
        D = Send->getMethodDecl();
      } else if (ObjCBoxedExpr *BoxedExpr = dyn_cast<ObjCBoxedExpr>(E)) {
        D = BoxedExpr->getBoxingMethod();
      } else if (ObjCArrayLiteral *ArrayLit = dyn_cast<ObjCArrayLiteral>(E)) {
        // Don't do reclaims if we're using the zero-element array
        // constant.
        if (ArrayLit->getNumElements() == 0 &&
            Context.getLangOpts().ObjCRuntime.hasEmptyCollections())
          return E;

        D = ArrayLit->getArrayWithObjectsMethod();
      } else if (ObjCDictionaryLiteral *DictLit
                                        = dyn_cast<ObjCDictionaryLiteral>(E)) {
        // Don't do reclaims if we're using the zero-element dictionary
        // constant.
        if (DictLit->getNumElements() == 0 &&
            Context.getLangOpts().ObjCRuntime.hasEmptyCollections())
          return E;

        D = DictLit->getDictWithObjectsMethod();
      }

      ReturnsRetained = (D && D->hasAttr<NSReturnsRetainedAttr>());

      // Don't do reclaims on performSelector calls; despite their
      // return type, the invoked method doesn't necessarily actually
      // return an object.
      if (!ReturnsRetained &&
          D && D->getMethodFamily() == OMF_performSelector)
        return E;
    }

    // Don't reclaim an object of Class type.
    if (!ReturnsRetained && E->getType()->isObjCARCImplicitlyUnretainedType())
      return E;

    Cleanup.setExprNeedsCleanups(true);

    CastKind ck = (ReturnsRetained ? CK_ARCConsumeObject
                                   : CK_ARCReclaimReturnedObject);
    return ImplicitCastExpr::Create(Context, E->getType(), ck, E, nullptr,
                                    VK_PRValue, FPOptionsOverride());
  }

  if (E->getType().isDestructedType() == QualType::DK_nontrivial_c_struct)
    Cleanup.setExprNeedsCleanups(true);

  if (!getLangOpts().CPlusPlus)
    return E;

  // Search for the base element type (cf. ASTContext::getBaseElementType) with
  // a fast path for the common case that the type is directly a RecordType.
  const Type *T = Context.getCanonicalType(E->getType().getTypePtr());
  const RecordType *RT = nullptr;
  while (!RT) {
    switch (T->getTypeClass()) {
    case Type::Record:
      RT = cast<RecordType>(T);
      break;
    case Type::ConstantArray:
    case Type::IncompleteArray:
    case Type::VariableArray:
    case Type::DependentSizedArray:
      T = cast<ArrayType>(T)->getElementType().getTypePtr();
      break;
    default:
      return E;
    }
  }

  // That should be enough to guarantee that this type is complete, if we're
  // not processing a decltype expression.
  CXXRecordDecl *RD = cast<CXXRecordDecl>(RT->getDecl());
  if (RD->isInvalidDecl() || RD->isDependentContext())
    return E;

  bool IsDecltype = ExprEvalContexts.back().ExprContext ==
                    ExpressionEvaluationContextRecord::EK_Decltype;
  CXXDestructorDecl *Destructor = IsDecltype ? nullptr : LookupDestructor(RD);

  if (Destructor) {
    MarkFunctionReferenced(E->getExprLoc(), Destructor);
    CheckDestructorAccess(E->getExprLoc(), Destructor,
                          PDiag(diag::err_access_dtor_temp)
                            << E->getType());
    if (DiagnoseUseOfDecl(Destructor, E->getExprLoc()))
      return ExprError();

    // If destructor is trivial, we can avoid the extra copy.
    if (Destructor->isTrivial())
      return E;

    // We need a cleanup, but we don't need to remember the temporary.
    Cleanup.setExprNeedsCleanups(true);
  }

  CXXTemporary *Temp = CXXTemporary::Create(Context, Destructor);
  CXXBindTemporaryExpr *Bind = CXXBindTemporaryExpr::Create(Context, Temp, E);

  if (IsDecltype)
    ExprEvalContexts.back().DelayedDecltypeBinds.push_back(Bind);

  return Bind;
}

ExprResult
Sema::MaybeCreateExprWithCleanups(ExprResult SubExpr) {
  if (SubExpr.isInvalid())
    return ExprError();

  return MaybeCreateExprWithCleanups(SubExpr.get());
}

Expr *Sema::MaybeCreateExprWithCleanups(Expr *SubExpr) {
  assert(SubExpr && "subexpression can't be null!");

  CleanupVarDeclMarking();

  unsigned FirstCleanup = ExprEvalContexts.back().NumCleanupObjects;
  assert(ExprCleanupObjects.size() >= FirstCleanup);
  assert(Cleanup.exprNeedsCleanups() ||
         ExprCleanupObjects.size() == FirstCleanup);
  if (!Cleanup.exprNeedsCleanups())
    return SubExpr;

  auto Cleanups = llvm::ArrayRef(ExprCleanupObjects.begin() + FirstCleanup,
                                 ExprCleanupObjects.size() - FirstCleanup);

  auto *E = ExprWithCleanups::Create(
      Context, SubExpr, Cleanup.cleanupsHaveSideEffects(), Cleanups);
  DiscardCleanupsInEvaluationContext();

  return E;
}

Stmt *Sema::MaybeCreateStmtWithCleanups(Stmt *SubStmt) {
  assert(SubStmt && "sub-statement can't be null!");

  CleanupVarDeclMarking();

  if (!Cleanup.exprNeedsCleanups())
    return SubStmt;

  // FIXME: In order to attach the temporaries, wrap the statement into
  // a StmtExpr; currently this is only used for asm statements.
  // This is hacky, either create a new CXXStmtWithTemporaries statement or
  // a new AsmStmtWithTemporaries.
  CompoundStmt *CompStmt =
      CompoundStmt::Create(Context, SubStmt, FPOptionsOverride(),
                           SourceLocation(), SourceLocation());
  Expr *E = new (Context)
      StmtExpr(CompStmt, Context.VoidTy, SourceLocation(), SourceLocation(),
               /*FIXME TemplateDepth=*/0);
  return MaybeCreateExprWithCleanups(E);
}

ExprResult Sema::ActOnDecltypeExpression(Expr *E) {
  assert(ExprEvalContexts.back().ExprContext ==
             ExpressionEvaluationContextRecord::EK_Decltype &&
         "not in a decltype expression");

  ExprResult Result = CheckPlaceholderExpr(E);
  if (Result.isInvalid())
    return ExprError();
  E = Result.get();

  // C++11 [expr.call]p11:
  //   If a function call is a prvalue of object type,
  // -- if the function call is either
  //   -- the operand of a decltype-specifier, or
  //   -- the right operand of a comma operator that is the operand of a
  //      decltype-specifier,
  //   a temporary object is not introduced for the prvalue.

  // Recursively rebuild ParenExprs and comma expressions to strip out the
  // outermost CXXBindTemporaryExpr, if any.
  if (ParenExpr *PE = dyn_cast<ParenExpr>(E)) {
    ExprResult SubExpr = ActOnDecltypeExpression(PE->getSubExpr());
    if (SubExpr.isInvalid())
      return ExprError();
    if (SubExpr.get() == PE->getSubExpr())
      return E;
    return ActOnParenExpr(PE->getLParen(), PE->getRParen(), SubExpr.get());
  }
  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_Comma) {
      ExprResult RHS = ActOnDecltypeExpression(BO->getRHS());
      if (RHS.isInvalid())
        return ExprError();
      if (RHS.get() == BO->getRHS())
        return E;
      return BinaryOperator::Create(Context, BO->getLHS(), RHS.get(), BO_Comma,
                                    BO->getType(), BO->getValueKind(),
                                    BO->getObjectKind(), BO->getOperatorLoc(),
                                    BO->getFPFeatures());
    }
  }

  CXXBindTemporaryExpr *TopBind = dyn_cast<CXXBindTemporaryExpr>(E);
  CallExpr *TopCall = TopBind ? dyn_cast<CallExpr>(TopBind->getSubExpr())
                              : nullptr;
  if (TopCall)
    E = TopCall;
  else
    TopBind = nullptr;

  // Disable the special decltype handling now.
  ExprEvalContexts.back().ExprContext =
      ExpressionEvaluationContextRecord::EK_Other;

  Result = CheckUnevaluatedOperand(E);
  if (Result.isInvalid())
    return ExprError();
  E = Result.get();

  // In MS mode, don't perform any extra checking of call return types within a
  // decltype expression.
  if (getLangOpts().MSVCCompat)
    return E;

  // Perform the semantic checks we delayed until this point.
  for (unsigned I = 0, N = ExprEvalContexts.back().DelayedDecltypeCalls.size();
       I != N; ++I) {
    CallExpr *Call = ExprEvalContexts.back().DelayedDecltypeCalls[I];
    if (Call == TopCall)
      continue;

    if (CheckCallReturnType(Call->getCallReturnType(Context),
                            Call->getBeginLoc(), Call, Call->getDirectCallee()))
      return ExprError();
  }

  // Now all relevant types are complete, check the destructors are accessible
  // and non-deleted, and annotate them on the temporaries.
  for (unsigned I = 0, N = ExprEvalContexts.back().DelayedDecltypeBinds.size();
       I != N; ++I) {
    CXXBindTemporaryExpr *Bind =
      ExprEvalContexts.back().DelayedDecltypeBinds[I];
    if (Bind == TopBind)
      continue;

    CXXTemporary *Temp = Bind->getTemporary();

    CXXRecordDecl *RD =
      Bind->getType()->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
    CXXDestructorDecl *Destructor = LookupDestructor(RD);
    Temp->setDestructor(Destructor);

    MarkFunctionReferenced(Bind->getExprLoc(), Destructor);
    CheckDestructorAccess(Bind->getExprLoc(), Destructor,
                          PDiag(diag::err_access_dtor_temp)
                            << Bind->getType());
    if (DiagnoseUseOfDecl(Destructor, Bind->getExprLoc()))
      return ExprError();

    // We need a cleanup, but we don't need to remember the temporary.
    Cleanup.setExprNeedsCleanups(true);
  }

  // Possibly strip off the top CXXBindTemporaryExpr.
  return E;
}

/// Note a set of 'operator->' functions that were used for a member access.
static void noteOperatorArrows(Sema &S,
                               ArrayRef<FunctionDecl *> OperatorArrows) {
  unsigned SkipStart = OperatorArrows.size(), SkipCount = 0;
  // FIXME: Make this configurable?
  unsigned Limit = 9;
  if (OperatorArrows.size() > Limit) {
    // Produce Limit-1 normal notes and one 'skipping' note.
    SkipStart = (Limit - 1) / 2 + (Limit - 1) % 2;
    SkipCount = OperatorArrows.size() - (Limit - 1);
  }

  for (unsigned I = 0; I < OperatorArrows.size(); /**/) {
    if (I == SkipStart) {
      S.Diag(OperatorArrows[I]->getLocation(),
             diag::note_operator_arrows_suppressed)
          << SkipCount;
      I += SkipCount;
    } else {
      S.Diag(OperatorArrows[I]->getLocation(), diag::note_operator_arrow_here)
          << OperatorArrows[I]->getCallResultType();
      ++I;
    }
  }
}

ExprResult Sema::ActOnStartCXXMemberReference(Scope *S, Expr *Base,
                                              SourceLocation OpLoc,
                                              tok::TokenKind OpKind,
                                              ParsedType &ObjectType,
                                              bool &MayBePseudoDestructor) {
  // Since this might be a postfix expression, get rid of ParenListExprs.
  ExprResult Result = MaybeConvertParenListExprToParenExpr(S, Base);
  if (Result.isInvalid()) return ExprError();
  Base = Result.get();

  Result = CheckPlaceholderExpr(Base);
  if (Result.isInvalid()) return ExprError();
  Base = Result.get();

  QualType BaseType = Base->getType();
  MayBePseudoDestructor = false;
  if (BaseType->isDependentType()) {
    // If we have a pointer to a dependent type and are using the -> operator,
    // the object type is the type that the pointer points to. We might still
    // have enough information about that type to do something useful.
    if (OpKind == tok::arrow)
      if (const PointerType *Ptr = BaseType->getAs<PointerType>())
        BaseType = Ptr->getPointeeType();

    ObjectType = ParsedType::make(BaseType);
    MayBePseudoDestructor = true;
    return Base;
  }

  // C++ [over.match.oper]p8:
  //   [...] When operator->returns, the operator-> is applied  to the value
  //   returned, with the original second operand.
  if (OpKind == tok::arrow) {
    QualType StartingType = BaseType;
    bool NoArrowOperatorFound = false;
    bool FirstIteration = true;
    FunctionDecl *CurFD = dyn_cast<FunctionDecl>(CurContext);
    // The set of types we've considered so far.
    llvm::SmallPtrSet<CanQualType,8> CTypes;
    SmallVector<FunctionDecl*, 8> OperatorArrows;
    CTypes.insert(Context.getCanonicalType(BaseType));

    while (BaseType->isRecordType()) {
      if (OperatorArrows.size() >= getLangOpts().ArrowDepth) {
        Diag(OpLoc, diag::err_operator_arrow_depth_exceeded)
          << StartingType << getLangOpts().ArrowDepth << Base->getSourceRange();
        noteOperatorArrows(*this, OperatorArrows);
        Diag(OpLoc, diag::note_operator_arrow_depth)
          << getLangOpts().ArrowDepth;
        return ExprError();
      }

      Result = BuildOverloadedArrowExpr(
          S, Base, OpLoc,
          // When in a template specialization and on the first loop iteration,
          // potentially give the default diagnostic (with the fixit in a
          // separate note) instead of having the error reported back to here
          // and giving a diagnostic with a fixit attached to the error itself.
          (FirstIteration && CurFD && CurFD->isFunctionTemplateSpecialization())
              ? nullptr
              : &NoArrowOperatorFound);
      if (Result.isInvalid()) {
        if (NoArrowOperatorFound) {
          if (FirstIteration) {
            Diag(OpLoc, diag::err_typecheck_member_reference_suggestion)
              << BaseType << 1 << Base->getSourceRange()
              << FixItHint::CreateReplacement(OpLoc, ".");
            OpKind = tok::period;
            break;
          }
          Diag(OpLoc, diag::err_typecheck_member_reference_arrow)
            << BaseType << Base->getSourceRange();
          CallExpr *CE = dyn_cast<CallExpr>(Base);
          if (Decl *CD = (CE ? CE->getCalleeDecl() : nullptr)) {
            Diag(CD->getBeginLoc(),
                 diag::note_member_reference_arrow_from_operator_arrow);
          }
        }
        return ExprError();
      }
      Base = Result.get();
      if (CXXOperatorCallExpr *OpCall = dyn_cast<CXXOperatorCallExpr>(Base))
        OperatorArrows.push_back(OpCall->getDirectCallee());
      BaseType = Base->getType();
      CanQualType CBaseType = Context.getCanonicalType(BaseType);
      if (!CTypes.insert(CBaseType).second) {
        Diag(OpLoc, diag::err_operator_arrow_circular) << StartingType;
        noteOperatorArrows(*this, OperatorArrows);
        return ExprError();
      }
      FirstIteration = false;
    }

    if (OpKind == tok::arrow) {
      if (BaseType->isPointerType())
        BaseType = BaseType->getPointeeType();
      else if (auto *AT = Context.getAsArrayType(BaseType))
        BaseType = AT->getElementType();
    }
  }

  // Objective-C properties allow "." access on Objective-C pointer types,
  // so adjust the base type to the object type itself.
  if (BaseType->isObjCObjectPointerType())
    BaseType = BaseType->getPointeeType();

  // C++ [basic.lookup.classref]p2:
  //   [...] If the type of the object expression is of pointer to scalar
  //   type, the unqualified-id is looked up in the context of the complete
  //   postfix-expression.
  //
  // This also indicates that we could be parsing a pseudo-destructor-name.
  // Note that Objective-C class and object types can be pseudo-destructor
  // expressions or normal member (ivar or property) access expressions, and
  // it's legal for the type to be incomplete if this is a pseudo-destructor
  // call.  We'll do more incomplete-type checks later in the lookup process,
  // so just skip this check for ObjC types.
  if (!BaseType->isRecordType()) {
    ObjectType = ParsedType::make(BaseType);
    MayBePseudoDestructor = true;
    return Base;
  }

  // The object type must be complete (or dependent), or
  // C++11 [expr.prim.general]p3:
  //   Unlike the object expression in other contexts, *this is not required to
  //   be of complete type for purposes of class member access (5.2.5) outside
  //   the member function body.
  if (!BaseType->isDependentType() &&
      !isThisOutsideMemberFunctionBody(BaseType) &&
      RequireCompleteType(OpLoc, BaseType,
                          diag::err_incomplete_member_access)) {
    return CreateRecoveryExpr(Base->getBeginLoc(), Base->getEndLoc(), {Base});
  }

  // C++ [basic.lookup.classref]p2:
  //   If the id-expression in a class member access (5.2.5) is an
  //   unqualified-id, and the type of the object expression is of a class
  //   type C (or of pointer to a class type C), the unqualified-id is looked
  //   up in the scope of class C. [...]
  ObjectType = ParsedType::make(BaseType);
  return Base;
}

static bool CheckArrow(Sema &S, QualType &ObjectType, Expr *&Base,
                       tok::TokenKind &OpKind, SourceLocation OpLoc) {
  if (Base->hasPlaceholderType()) {
    ExprResult result = S.CheckPlaceholderExpr(Base);
    if (result.isInvalid()) return true;
    Base = result.get();
  }
  ObjectType = Base->getType();

  // C++ [expr.pseudo]p2:
  //   The left-hand side of the dot operator shall be of scalar type. The
  //   left-hand side of the arrow operator shall be of pointer to scalar type.
  //   This scalar type is the object type.
  // Note that this is rather different from the normal handling for the
  // arrow operator.
  if (OpKind == tok::arrow) {
    // The operator requires a prvalue, so perform lvalue conversions.
    // Only do this if we might plausibly end with a pointer, as otherwise
    // this was likely to be intended to be a '.'.
    if (ObjectType->isPointerType() || ObjectType->isArrayType() ||
        ObjectType->isFunctionType()) {
      ExprResult BaseResult = S.DefaultFunctionArrayLvalueConversion(Base);
      if (BaseResult.isInvalid())
        return true;
      Base = BaseResult.get();
      ObjectType = Base->getType();
    }

    if (const PointerType *Ptr = ObjectType->getAs<PointerType>()) {
      ObjectType = Ptr->getPointeeType();
    } else if (!Base->isTypeDependent()) {
      // The user wrote "p->" when they probably meant "p."; fix it.
      S.Diag(OpLoc, diag::err_typecheck_member_reference_suggestion)
        << ObjectType << true
        << FixItHint::CreateReplacement(OpLoc, ".");
      if (S.isSFINAEContext())
        return true;

      OpKind = tok::period;
    }
  }

  return false;
}

/// Check if it's ok to try and recover dot pseudo destructor calls on
/// pointer objects.
static bool
canRecoverDotPseudoDestructorCallsOnPointerObjects(Sema &SemaRef,
                                                   QualType DestructedType) {
  // If this is a record type, check if its destructor is callable.
  if (auto *RD = DestructedType->getAsCXXRecordDecl()) {
    if (RD->hasDefinition())
      if (CXXDestructorDecl *D = SemaRef.LookupDestructor(RD))
        return SemaRef.CanUseDecl(D, /*TreatUnavailableAsInvalid=*/false);
    return false;
  }

  // Otherwise, check if it's a type for which it's valid to use a pseudo-dtor.
  return DestructedType->isDependentType() || DestructedType->isScalarType() ||
         DestructedType->isVectorType();
}

ExprResult Sema::BuildPseudoDestructorExpr(Expr *Base,
                                           SourceLocation OpLoc,
                                           tok::TokenKind OpKind,
                                           const CXXScopeSpec &SS,
                                           TypeSourceInfo *ScopeTypeInfo,
                                           SourceLocation CCLoc,
                                           SourceLocation TildeLoc,
                                         PseudoDestructorTypeStorage Destructed) {
  TypeSourceInfo *DestructedTypeInfo = Destructed.getTypeSourceInfo();

  QualType ObjectType;
  if (CheckArrow(*this, ObjectType, Base, OpKind, OpLoc))
    return ExprError();

  if (!ObjectType->isDependentType() && !ObjectType->isScalarType() &&
      !ObjectType->isVectorType()) {
    if (getLangOpts().MSVCCompat && ObjectType->isVoidType())
      Diag(OpLoc, diag::ext_pseudo_dtor_on_void) << Base->getSourceRange();
    else {
      Diag(OpLoc, diag::err_pseudo_dtor_base_not_scalar)
        << ObjectType << Base->getSourceRange();
      return ExprError();
    }
  }

  // C++ [expr.pseudo]p2:
  //   [...] The cv-unqualified versions of the object type and of the type
  //   designated by the pseudo-destructor-name shall be the same type.
  if (DestructedTypeInfo) {
    QualType DestructedType = DestructedTypeInfo->getType();
    SourceLocation DestructedTypeStart =
        DestructedTypeInfo->getTypeLoc().getBeginLoc();
    if (!DestructedType->isDependentType() && !ObjectType->isDependentType()) {
      if (!Context.hasSameUnqualifiedType(DestructedType, ObjectType)) {
        // Detect dot pseudo destructor calls on pointer objects, e.g.:
        //   Foo *foo;
        //   foo.~Foo();
        if (OpKind == tok::period && ObjectType->isPointerType() &&
            Context.hasSameUnqualifiedType(DestructedType,
                                           ObjectType->getPointeeType())) {
          auto Diagnostic =
              Diag(OpLoc, diag::err_typecheck_member_reference_suggestion)
              << ObjectType << /*IsArrow=*/0 << Base->getSourceRange();

          // Issue a fixit only when the destructor is valid.
          if (canRecoverDotPseudoDestructorCallsOnPointerObjects(
                  *this, DestructedType))
            Diagnostic << FixItHint::CreateReplacement(OpLoc, "->");

          // Recover by setting the object type to the destructed type and the
          // operator to '->'.
          ObjectType = DestructedType;
          OpKind = tok::arrow;
        } else {
          Diag(DestructedTypeStart, diag::err_pseudo_dtor_type_mismatch)
              << ObjectType << DestructedType << Base->getSourceRange()
              << DestructedTypeInfo->getTypeLoc().getSourceRange();

          // Recover by setting the destructed type to the object type.
          DestructedType = ObjectType;
          DestructedTypeInfo =
              Context.getTrivialTypeSourceInfo(ObjectType, DestructedTypeStart);
          Destructed = PseudoDestructorTypeStorage(DestructedTypeInfo);
        }
      } else if (DestructedType.getObjCLifetime() !=
                                                ObjectType.getObjCLifetime()) {

        if (DestructedType.getObjCLifetime() == Qualifiers::OCL_None) {
          // Okay: just pretend that the user provided the correctly-qualified
          // type.
        } else {
          Diag(DestructedTypeStart, diag::err_arc_pseudo_dtor_inconstant_quals)
              << ObjectType << DestructedType << Base->getSourceRange()
              << DestructedTypeInfo->getTypeLoc().getSourceRange();
        }

        // Recover by setting the destructed type to the object type.
        DestructedType = ObjectType;
        DestructedTypeInfo = Context.getTrivialTypeSourceInfo(ObjectType,
                                                           DestructedTypeStart);
        Destructed = PseudoDestructorTypeStorage(DestructedTypeInfo);
      }
    }
  }

  // C++ [expr.pseudo]p2:
  //   [...] Furthermore, the two type-names in a pseudo-destructor-name of the
  //   form
  //
  //     ::[opt] nested-name-specifier[opt] type-name :: ~ type-name
  //
  //   shall designate the same scalar type.
  if (ScopeTypeInfo) {
    QualType ScopeType = ScopeTypeInfo->getType();
    if (!ScopeType->isDependentType() && !ObjectType->isDependentType() &&
        !Context.hasSameUnqualifiedType(ScopeType, ObjectType)) {

      Diag(ScopeTypeInfo->getTypeLoc().getSourceRange().getBegin(),
           diag::err_pseudo_dtor_type_mismatch)
          << ObjectType << ScopeType << Base->getSourceRange()
          << ScopeTypeInfo->getTypeLoc().getSourceRange();

      ScopeType = QualType();
      ScopeTypeInfo = nullptr;
    }
  }

  Expr *Result
    = new (Context) CXXPseudoDestructorExpr(Context, Base,
                                            OpKind == tok::arrow, OpLoc,
                                            SS.getWithLocInContext(Context),
                                            ScopeTypeInfo,
                                            CCLoc,
                                            TildeLoc,
                                            Destructed);

  return Result;
}

ExprResult Sema::ActOnPseudoDestructorExpr(Scope *S, Expr *Base,
                                           SourceLocation OpLoc,
                                           tok::TokenKind OpKind,
                                           CXXScopeSpec &SS,
                                           UnqualifiedId &FirstTypeName,
                                           SourceLocation CCLoc,
                                           SourceLocation TildeLoc,
                                           UnqualifiedId &SecondTypeName) {
  assert((FirstTypeName.getKind() == UnqualifiedIdKind::IK_TemplateId ||
          FirstTypeName.getKind() == UnqualifiedIdKind::IK_Identifier) &&
         "Invalid first type name in pseudo-destructor");
  assert((SecondTypeName.getKind() == UnqualifiedIdKind::IK_TemplateId ||
          SecondTypeName.getKind() == UnqualifiedIdKind::IK_Identifier) &&
         "Invalid second type name in pseudo-destructor");

  QualType ObjectType;
  if (CheckArrow(*this, ObjectType, Base, OpKind, OpLoc))
    return ExprError();

  // Compute the object type that we should use for name lookup purposes. Only
  // record types and dependent types matter.
  ParsedType ObjectTypePtrForLookup;
  if (!SS.isSet()) {
    if (ObjectType->isRecordType())
      ObjectTypePtrForLookup = ParsedType::make(ObjectType);
    else if (ObjectType->isDependentType())
      ObjectTypePtrForLookup = ParsedType::make(Context.DependentTy);
  }

  // Convert the name of the type being destructed (following the ~) into a
  // type (with source-location information).
  QualType DestructedType;
  TypeSourceInfo *DestructedTypeInfo = nullptr;
  PseudoDestructorTypeStorage Destructed;
  if (SecondTypeName.getKind() == UnqualifiedIdKind::IK_Identifier) {
    ParsedType T = getTypeName(*SecondTypeName.Identifier,
                               SecondTypeName.StartLocation,
                               S, &SS, true, false, ObjectTypePtrForLookup,
                               /*IsCtorOrDtorName*/true);
    if (!T &&
        ((SS.isSet() && !computeDeclContext(SS, false)) ||
         (!SS.isSet() && ObjectType->isDependentType()))) {
      // The name of the type being destroyed is a dependent name, and we
      // couldn't find anything useful in scope. Just store the identifier and
      // it's location, and we'll perform (qualified) name lookup again at
      // template instantiation time.
      Destructed = PseudoDestructorTypeStorage(SecondTypeName.Identifier,
                                               SecondTypeName.StartLocation);
    } else if (!T) {
      Diag(SecondTypeName.StartLocation,
           diag::err_pseudo_dtor_destructor_non_type)
        << SecondTypeName.Identifier << ObjectType;
      if (isSFINAEContext())
        return ExprError();

      // Recover by assuming we had the right type all along.
      DestructedType = ObjectType;
    } else
      DestructedType = GetTypeFromParser(T, &DestructedTypeInfo);
  } else {
    // Resolve the template-id to a type.
    TemplateIdAnnotation *TemplateId = SecondTypeName.TemplateId;
    ASTTemplateArgsPtr TemplateArgsPtr(TemplateId->getTemplateArgs(),
                                       TemplateId->NumArgs);
    TypeResult T = ActOnTemplateIdType(S,
                                       SS,
                                       TemplateId->TemplateKWLoc,
                                       TemplateId->Template,
                                       TemplateId->Name,
                                       TemplateId->TemplateNameLoc,
                                       TemplateId->LAngleLoc,
                                       TemplateArgsPtr,
                                       TemplateId->RAngleLoc,
                                       /*IsCtorOrDtorName*/true);
    if (T.isInvalid() || !T.get()) {
      // Recover by assuming we had the right type all along.
      DestructedType = ObjectType;
    } else
      DestructedType = GetTypeFromParser(T.get(), &DestructedTypeInfo);
  }

  // If we've performed some kind of recovery, (re-)build the type source
  // information.
  if (!DestructedType.isNull()) {
    if (!DestructedTypeInfo)
      DestructedTypeInfo = Context.getTrivialTypeSourceInfo(DestructedType,
                                                  SecondTypeName.StartLocation);
    Destructed = PseudoDestructorTypeStorage(DestructedTypeInfo);
  }

  // Convert the name of the scope type (the type prior to '::') into a type.
  TypeSourceInfo *ScopeTypeInfo = nullptr;
  QualType ScopeType;
  if (FirstTypeName.getKind() == UnqualifiedIdKind::IK_TemplateId ||
      FirstTypeName.Identifier) {
    if (FirstTypeName.getKind() == UnqualifiedIdKind::IK_Identifier) {
      ParsedType T = getTypeName(*FirstTypeName.Identifier,
                                 FirstTypeName.StartLocation,
                                 S, &SS, true, false, ObjectTypePtrForLookup,
                                 /*IsCtorOrDtorName*/true);
      if (!T) {
        Diag(FirstTypeName.StartLocation,
             diag::err_pseudo_dtor_destructor_non_type)
          << FirstTypeName.Identifier << ObjectType;

        if (isSFINAEContext())
          return ExprError();

        // Just drop this type. It's unnecessary anyway.
        ScopeType = QualType();
      } else
        ScopeType = GetTypeFromParser(T, &ScopeTypeInfo);
    } else {
      // Resolve the template-id to a type.
      TemplateIdAnnotation *TemplateId = FirstTypeName.TemplateId;
      ASTTemplateArgsPtr TemplateArgsPtr(TemplateId->getTemplateArgs(),
                                         TemplateId->NumArgs);
      TypeResult T = ActOnTemplateIdType(S,
                                         SS,
                                         TemplateId->TemplateKWLoc,
                                         TemplateId->Template,
                                         TemplateId->Name,
                                         TemplateId->TemplateNameLoc,
                                         TemplateId->LAngleLoc,
                                         TemplateArgsPtr,
                                         TemplateId->RAngleLoc,
                                         /*IsCtorOrDtorName*/true);
      if (T.isInvalid() || !T.get()) {
        // Recover by dropping this type.
        ScopeType = QualType();
      } else
        ScopeType = GetTypeFromParser(T.get(), &ScopeTypeInfo);
    }
  }

  if (!ScopeType.isNull() && !ScopeTypeInfo)
    ScopeTypeInfo = Context.getTrivialTypeSourceInfo(ScopeType,
                                                  FirstTypeName.StartLocation);


  return BuildPseudoDestructorExpr(Base, OpLoc, OpKind, SS,
                                   ScopeTypeInfo, CCLoc, TildeLoc,
                                   Destructed);
}

ExprResult Sema::ActOnPseudoDestructorExpr(Scope *S, Expr *Base,
                                           SourceLocation OpLoc,
                                           tok::TokenKind OpKind,
                                           SourceLocation TildeLoc,
                                           const DeclSpec& DS) {
  QualType ObjectType;
  QualType T;
  TypeLocBuilder TLB;
  if (CheckArrow(*this, ObjectType, Base, OpKind, OpLoc))
    return ExprError();

  switch (DS.getTypeSpecType()) {
  case DeclSpec::TST_decltype_auto: {
    Diag(DS.getTypeSpecTypeLoc(), diag::err_decltype_auto_invalid);
    return true;
  }
  case DeclSpec::TST_decltype: {
    T = BuildDecltypeType(DS.getRepAsExpr(), /*AsUnevaluated=*/false);
    DecltypeTypeLoc DecltypeTL = TLB.push<DecltypeTypeLoc>(T);
    DecltypeTL.setDecltypeLoc(DS.getTypeSpecTypeLoc());
    DecltypeTL.setRParenLoc(DS.getTypeofParensRange().getEnd());
    break;
  }
  case DeclSpec::TST_typename_pack_indexing: {
    T = ActOnPackIndexingType(DS.getRepAsType().get(), DS.getPackIndexingExpr(),
                              DS.getBeginLoc(), DS.getEllipsisLoc());
    TLB.pushTrivial(getASTContext(),
                    cast<PackIndexingType>(T.getTypePtr())->getPattern(),
                    DS.getBeginLoc());
    PackIndexingTypeLoc PITL = TLB.push<PackIndexingTypeLoc>(T);
    PITL.setEllipsisLoc(DS.getEllipsisLoc());
    break;
  }
  default:
    llvm_unreachable("Unsupported type in pseudo destructor");
  }
  TypeSourceInfo *DestructedTypeInfo = TLB.getTypeSourceInfo(Context, T);
  PseudoDestructorTypeStorage Destructed(DestructedTypeInfo);

  return BuildPseudoDestructorExpr(Base, OpLoc, OpKind, CXXScopeSpec(),
                                   nullptr, SourceLocation(), TildeLoc,
                                   Destructed);
}

ExprResult Sema::BuildCXXNoexceptExpr(SourceLocation KeyLoc, Expr *Operand,
                                      SourceLocation RParen) {
  // If the operand is an unresolved lookup expression, the expression is ill-
  // formed per [over.over]p1, because overloaded function names cannot be used
  // without arguments except in explicit contexts.
  ExprResult R = CheckPlaceholderExpr(Operand);
  if (R.isInvalid())
    return R;

  R = CheckUnevaluatedOperand(R.get());
  if (R.isInvalid())
    return ExprError();

  Operand = R.get();

  if (!inTemplateInstantiation() && !Operand->isInstantiationDependent() &&
      Operand->HasSideEffects(Context, false)) {
    // The expression operand for noexcept is in an unevaluated expression
    // context, so side effects could result in unintended consequences.
    Diag(Operand->getExprLoc(), diag::warn_side_effects_unevaluated_context);
  }

  CanThrowResult CanThrow = canThrow(Operand);
  return new (Context)
      CXXNoexceptExpr(Context.BoolTy, Operand, CanThrow, KeyLoc, RParen);
}

ExprResult Sema::ActOnNoexceptExpr(SourceLocation KeyLoc, SourceLocation,
                                   Expr *Operand, SourceLocation RParen) {
  return BuildCXXNoexceptExpr(KeyLoc, Operand, RParen);
}

static void MaybeDecrementCount(
    Expr *E, llvm::DenseMap<const VarDecl *, int> &RefsMinusAssignments) {
  DeclRefExpr *LHS = nullptr;
  bool IsCompoundAssign = false;
  bool isIncrementDecrementUnaryOp = false;
  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getLHS()->getType()->isDependentType() ||
        BO->getRHS()->getType()->isDependentType()) {
      if (BO->getOpcode() != BO_Assign)
        return;
    } else if (!BO->isAssignmentOp())
      return;
    else
      IsCompoundAssign = BO->isCompoundAssignmentOp();
    LHS = dyn_cast<DeclRefExpr>(BO->getLHS());
  } else if (CXXOperatorCallExpr *COCE = dyn_cast<CXXOperatorCallExpr>(E)) {
    if (COCE->getOperator() != OO_Equal)
      return;
    LHS = dyn_cast<DeclRefExpr>(COCE->getArg(0));
  } else if (UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (!UO->isIncrementDecrementOp())
      return;
    isIncrementDecrementUnaryOp = true;
    LHS = dyn_cast<DeclRefExpr>(UO->getSubExpr());
  }
  if (!LHS)
    return;
  VarDecl *VD = dyn_cast<VarDecl>(LHS->getDecl());
  if (!VD)
    return;
  // Don't decrement RefsMinusAssignments if volatile variable with compound
  // assignment (+=, ...) or increment/decrement unary operator to avoid
  // potential unused-but-set-variable warning.
  if ((IsCompoundAssign || isIncrementDecrementUnaryOp) &&
      VD->getType().isVolatileQualified())
    return;
  auto iter = RefsMinusAssignments.find(VD);
  if (iter == RefsMinusAssignments.end())
    return;
  iter->getSecond()--;
}

/// Perform the conversions required for an expression used in a
/// context that ignores the result.
ExprResult Sema::IgnoredValueConversions(Expr *E) {
  MaybeDecrementCount(E, RefsMinusAssignments);

  if (E->hasPlaceholderType()) {
    ExprResult result = CheckPlaceholderExpr(E);
    if (result.isInvalid()) return E;
    E = result.get();
  }

  if (getLangOpts().CPlusPlus) {
    // The C++11 standard defines the notion of a discarded-value expression;
    // normally, we don't need to do anything to handle it, but if it is a
    // volatile lvalue with a special form, we perform an lvalue-to-rvalue
    // conversion.
    if (getLangOpts().CPlusPlus11 && E->isReadIfDiscardedInCPlusPlus11()) {
      ExprResult Res = DefaultLvalueConversion(E);
      if (Res.isInvalid())
        return E;
      E = Res.get();
    } else {
      // Per C++2a [expr.ass]p5, a volatile assignment is not deprecated if
      // it occurs as a discarded-value expression.
      CheckUnusedVolatileAssignment(E);
    }

    // C++1z:
    //   If the expression is a prvalue after this optional conversion, the
    //   temporary materialization conversion is applied.
    //
    // We do not materialize temporaries by default in order to avoid creating
    // unnecessary temporary objects. If we skip this step, IR generation is
    // able to synthesize the storage for itself in the aggregate case, and
    // adding the extra node to the AST is just clutter.
    if (isInLifetimeExtendingContext() && getLangOpts().CPlusPlus17 &&
        E->isPRValue() && !E->getType()->isVoidType()) {
      ExprResult Res = TemporaryMaterializationConversion(E);
      if (Res.isInvalid())
        return E;
      E = Res.get();
    }
    return E;
  }

  // C99 6.3.2.1:
  //   [Except in specific positions,] an lvalue that does not have
  //   array type is converted to the value stored in the
  //   designated object (and is no longer an lvalue).
  if (E->isPRValue()) {
    // In C, function designators (i.e. expressions of function type)
    // are r-values, but we still want to do function-to-pointer decay
    // on them.  This is both technically correct and convenient for
    // some clients.
    if (!getLangOpts().CPlusPlus && E->getType()->isFunctionType())
      return DefaultFunctionArrayConversion(E);

    return E;
  }

  // GCC seems to also exclude expressions of incomplete enum type.
  if (const EnumType *T = E->getType()->getAs<EnumType>()) {
    if (!T->getDecl()->isComplete()) {
      // FIXME: stupid workaround for a codegen bug!
      E = ImpCastExprToType(E, Context.VoidTy, CK_ToVoid).get();
      return E;
    }
  }

  ExprResult Res = DefaultFunctionArrayLvalueConversion(E);
  if (Res.isInvalid())
    return E;
  E = Res.get();

  if (!E->getType()->isVoidType())
    RequireCompleteType(E->getExprLoc(), E->getType(),
                        diag::err_incomplete_type);
  return E;
}

ExprResult Sema::CheckUnevaluatedOperand(Expr *E) {
  // Per C++2a [expr.ass]p5, a volatile assignment is not deprecated if
  // it occurs as an unevaluated operand.
  CheckUnusedVolatileAssignment(E);

  return E;
}

// If we can unambiguously determine whether Var can never be used
// in a constant expression, return true.
//  - if the variable and its initializer are non-dependent, then
//    we can unambiguously check if the variable is a constant expression.
//  - if the initializer is not value dependent - we can determine whether
//    it can be used to initialize a constant expression.  If Init can not
//    be used to initialize a constant expression we conclude that Var can
//    never be a constant expression.
//  - FXIME: if the initializer is dependent, we can still do some analysis and
//    identify certain cases unambiguously as non-const by using a Visitor:
//      - such as those that involve odr-use of a ParmVarDecl, involve a new
//        delete, lambda-expr, dynamic-cast, reinterpret-cast etc...
static inline bool VariableCanNeverBeAConstantExpression(VarDecl *Var,
    ASTContext &Context) {
  if (isa<ParmVarDecl>(Var)) return true;
  const VarDecl *DefVD = nullptr;

  // If there is no initializer - this can not be a constant expression.
  const Expr *Init = Var->getAnyInitializer(DefVD);
  if (!Init)
    return true;
  assert(DefVD);
  if (DefVD->isWeak())
    return false;

  if (Var->getType()->isDependentType() || Init->isValueDependent()) {
    // FIXME: Teach the constant evaluator to deal with the non-dependent parts
    // of value-dependent expressions, and use it here to determine whether the
    // initializer is a potential constant expression.
    return false;
  }

  return !Var->isUsableInConstantExpressions(Context);
}

/// Check if the current lambda has any potential captures
/// that must be captured by any of its enclosing lambdas that are ready to
/// capture. If there is a lambda that can capture a nested
/// potential-capture, go ahead and do so.  Also, check to see if any
/// variables are uncaptureable or do not involve an odr-use so do not
/// need to be captured.

static void CheckIfAnyEnclosingLambdasMustCaptureAnyPotentialCaptures(
    Expr *const FE, LambdaScopeInfo *const CurrentLSI, Sema &S) {

  assert(!S.isUnevaluatedContext());
  assert(S.CurContext->isDependentContext());
#ifndef NDEBUG
  DeclContext *DC = S.CurContext;
  while (isa_and_nonnull<CapturedDecl>(DC))
    DC = DC->getParent();
  assert(
      CurrentLSI->CallOperator == DC &&
      "The current call operator must be synchronized with Sema's CurContext");
#endif // NDEBUG

  const bool IsFullExprInstantiationDependent = FE->isInstantiationDependent();

  // All the potentially captureable variables in the current nested
  // lambda (within a generic outer lambda), must be captured by an
  // outer lambda that is enclosed within a non-dependent context.
  CurrentLSI->visitPotentialCaptures([&](ValueDecl *Var, Expr *VarExpr) {
    // If the variable is clearly identified as non-odr-used and the full
    // expression is not instantiation dependent, only then do we not
    // need to check enclosing lambda's for speculative captures.
    // For e.g.:
    // Even though 'x' is not odr-used, it should be captured.
    // int test() {
    //   const int x = 10;
    //   auto L = [=](auto a) {
    //     (void) +x + a;
    //   };
    // }
    if (CurrentLSI->isVariableExprMarkedAsNonODRUsed(VarExpr) &&
        !IsFullExprInstantiationDependent)
      return;

    VarDecl *UnderlyingVar = Var->getPotentiallyDecomposedVarDecl();
    if (!UnderlyingVar)
      return;

    // If we have a capture-capable lambda for the variable, go ahead and
    // capture the variable in that lambda (and all its enclosing lambdas).
    if (const std::optional<unsigned> Index =
            getStackIndexOfNearestEnclosingCaptureCapableLambda(
                S.FunctionScopes, Var, S))
      S.MarkCaptureUsedInEnclosingContext(Var, VarExpr->getExprLoc(), *Index);
    const bool IsVarNeverAConstantExpression =
        VariableCanNeverBeAConstantExpression(UnderlyingVar, S.Context);
    if (!IsFullExprInstantiationDependent || IsVarNeverAConstantExpression) {
      // This full expression is not instantiation dependent or the variable
      // can not be used in a constant expression - which means
      // this variable must be odr-used here, so diagnose a
      // capture violation early, if the variable is un-captureable.
      // This is purely for diagnosing errors early.  Otherwise, this
      // error would get diagnosed when the lambda becomes capture ready.
      QualType CaptureType, DeclRefType;
      SourceLocation ExprLoc = VarExpr->getExprLoc();
      if (S.tryCaptureVariable(Var, ExprLoc, S.TryCapture_Implicit,
                          /*EllipsisLoc*/ SourceLocation(),
                          /*BuildAndDiagnose*/false, CaptureType,
                          DeclRefType, nullptr)) {
        // We will never be able to capture this variable, and we need
        // to be able to in any and all instantiations, so diagnose it.
        S.tryCaptureVariable(Var, ExprLoc, S.TryCapture_Implicit,
                          /*EllipsisLoc*/ SourceLocation(),
                          /*BuildAndDiagnose*/true, CaptureType,
                          DeclRefType, nullptr);
      }
    }
  });

  // Check if 'this' needs to be captured.
  if (CurrentLSI->hasPotentialThisCapture()) {
    // If we have a capture-capable lambda for 'this', go ahead and capture
    // 'this' in that lambda (and all its enclosing lambdas).
    if (const std::optional<unsigned> Index =
            getStackIndexOfNearestEnclosingCaptureCapableLambda(
                S.FunctionScopes, /*0 is 'this'*/ nullptr, S)) {
      const unsigned FunctionScopeIndexOfCapturableLambda = *Index;
      S.CheckCXXThisCapture(CurrentLSI->PotentialThisCaptureLocation,
                            /*Explicit*/ false, /*BuildAndDiagnose*/ true,
                            &FunctionScopeIndexOfCapturableLambda);
    }
  }

  // Reset all the potential captures at the end of each full-expression.
  CurrentLSI->clearPotentialCaptures();
}

static ExprResult attemptRecovery(Sema &SemaRef,
                                  const TypoCorrectionConsumer &Consumer,
                                  const TypoCorrection &TC) {
  LookupResult R(SemaRef, Consumer.getLookupResult().getLookupNameInfo(),
                 Consumer.getLookupResult().getLookupKind());
  const CXXScopeSpec *SS = Consumer.getSS();
  CXXScopeSpec NewSS;

  // Use an approprate CXXScopeSpec for building the expr.
  if (auto *NNS = TC.getCorrectionSpecifier())
    NewSS.MakeTrivial(SemaRef.Context, NNS, TC.getCorrectionRange());
  else if (SS && !TC.WillReplaceSpecifier())
    NewSS = *SS;

  if (auto *ND = TC.getFoundDecl()) {
    R.setLookupName(ND->getDeclName());
    R.addDecl(ND);
    if (ND->isCXXClassMember()) {
      // Figure out the correct naming class to add to the LookupResult.
      CXXRecordDecl *Record = nullptr;
      if (auto *NNS = TC.getCorrectionSpecifier())
        Record = NNS->getAsType()->getAsCXXRecordDecl();
      if (!Record)
        Record =
            dyn_cast<CXXRecordDecl>(ND->getDeclContext()->getRedeclContext());
      if (Record)
        R.setNamingClass(Record);

      // Detect and handle the case where the decl might be an implicit
      // member.
      if (SemaRef.isPotentialImplicitMemberAccess(
              NewSS, R, Consumer.isAddressOfOperand()))
        return SemaRef.BuildPossibleImplicitMemberExpr(
            NewSS, /*TemplateKWLoc*/ SourceLocation(), R,
            /*TemplateArgs*/ nullptr, /*S*/ nullptr);
    } else if (auto *Ivar = dyn_cast<ObjCIvarDecl>(ND)) {
      return SemaRef.ObjC().LookupInObjCMethod(R, Consumer.getScope(),
                                               Ivar->getIdentifier());
    }
  }

  return SemaRef.BuildDeclarationNameExpr(NewSS, R, /*NeedsADL*/ false,
                                          /*AcceptInvalidDecl*/ true);
}

namespace {
class FindTypoExprs : public RecursiveASTVisitor<FindTypoExprs> {
  llvm::SmallSetVector<TypoExpr *, 2> &TypoExprs;

public:
  explicit FindTypoExprs(llvm::SmallSetVector<TypoExpr *, 2> &TypoExprs)
      : TypoExprs(TypoExprs) {}
  bool VisitTypoExpr(TypoExpr *TE) {
    TypoExprs.insert(TE);
    return true;
  }
};

class TransformTypos : public TreeTransform<TransformTypos> {
  typedef TreeTransform<TransformTypos> BaseTransform;

  VarDecl *InitDecl; // A decl to avoid as a correction because it is in the
                     // process of being initialized.
  llvm::function_ref<ExprResult(Expr *)> ExprFilter;
  llvm::SmallSetVector<TypoExpr *, 2> TypoExprs, AmbiguousTypoExprs;
  llvm::SmallDenseMap<TypoExpr *, ExprResult, 2> TransformCache;
  llvm::SmallDenseMap<OverloadExpr *, Expr *, 4> OverloadResolution;

  /// Emit diagnostics for all of the TypoExprs encountered.
  ///
  /// If the TypoExprs were successfully corrected, then the diagnostics should
  /// suggest the corrections. Otherwise the diagnostics will not suggest
  /// anything (having been passed an empty TypoCorrection).
  ///
  /// If we've failed to correct due to ambiguous corrections, we need to
  /// be sure to pass empty corrections and replacements. Otherwise it's
  /// possible that the Consumer has a TypoCorrection that failed to ambiguity
  /// and we don't want to report those diagnostics.
  void EmitAllDiagnostics(bool IsAmbiguous) {
    for (TypoExpr *TE : TypoExprs) {
      auto &State = SemaRef.getTypoExprState(TE);
      if (State.DiagHandler) {
        TypoCorrection TC = IsAmbiguous
            ? TypoCorrection() : State.Consumer->getCurrentCorrection();
        ExprResult Replacement = IsAmbiguous ? ExprError() : TransformCache[TE];

        // Extract the NamedDecl from the transformed TypoExpr and add it to the
        // TypoCorrection, replacing the existing decls. This ensures the right
        // NamedDecl is used in diagnostics e.g. in the case where overload
        // resolution was used to select one from several possible decls that
        // had been stored in the TypoCorrection.
        if (auto *ND = getDeclFromExpr(
                Replacement.isInvalid() ? nullptr : Replacement.get()))
          TC.setCorrectionDecl(ND);

        State.DiagHandler(TC);
      }
      SemaRef.clearDelayedTypo(TE);
    }
  }

  /// Try to advance the typo correction state of the first unfinished TypoExpr.
  /// We allow advancement of the correction stream by removing it from the
  /// TransformCache which allows `TransformTypoExpr` to advance during the
  /// next transformation attempt.
  ///
  /// Any substitution attempts for the previous TypoExprs (which must have been
  /// finished) will need to be retried since it's possible that they will now
  /// be invalid given the latest advancement.
  ///
  /// We need to be sure that we're making progress - it's possible that the
  /// tree is so malformed that the transform never makes it to the
  /// `TransformTypoExpr`.
  ///
  /// Returns true if there are any untried correction combinations.
  bool CheckAndAdvanceTypoExprCorrectionStreams() {
    for (auto *TE : TypoExprs) {
      auto &State = SemaRef.getTypoExprState(TE);
      TransformCache.erase(TE);
      if (!State.Consumer->hasMadeAnyCorrectionProgress())
        return false;
      if (!State.Consumer->finished())
        return true;
      State.Consumer->resetCorrectionStream();
    }
    return false;
  }

  NamedDecl *getDeclFromExpr(Expr *E) {
    if (auto *OE = dyn_cast_or_null<OverloadExpr>(E))
      E = OverloadResolution[OE];

    if (!E)
      return nullptr;
    if (auto *DRE = dyn_cast<DeclRefExpr>(E))
      return DRE->getFoundDecl();
    if (auto *ME = dyn_cast<MemberExpr>(E))
      return ME->getFoundDecl();
    // FIXME: Add any other expr types that could be seen by the delayed typo
    // correction TreeTransform for which the corresponding TypoCorrection could
    // contain multiple decls.
    return nullptr;
  }

  ExprResult TryTransform(Expr *E) {
    Sema::SFINAETrap Trap(SemaRef);
    ExprResult Res = TransformExpr(E);
    if (Trap.hasErrorOccurred() || Res.isInvalid())
      return ExprError();

    return ExprFilter(Res.get());
  }

  // Since correcting typos may intoduce new TypoExprs, this function
  // checks for new TypoExprs and recurses if it finds any. Note that it will
  // only succeed if it is able to correct all typos in the given expression.
  ExprResult CheckForRecursiveTypos(ExprResult Res, bool &IsAmbiguous) {
    if (Res.isInvalid()) {
      return Res;
    }
    // Check to see if any new TypoExprs were created. If so, we need to recurse
    // to check their validity.
    Expr *FixedExpr = Res.get();

    auto SavedTypoExprs = std::move(TypoExprs);
    auto SavedAmbiguousTypoExprs = std::move(AmbiguousTypoExprs);
    TypoExprs.clear();
    AmbiguousTypoExprs.clear();

    FindTypoExprs(TypoExprs).TraverseStmt(FixedExpr);
    if (!TypoExprs.empty()) {
      // Recurse to handle newly created TypoExprs. If we're not able to
      // handle them, discard these TypoExprs.
      ExprResult RecurResult =
          RecursiveTransformLoop(FixedExpr, IsAmbiguous);
      if (RecurResult.isInvalid()) {
        Res = ExprError();
        // Recursive corrections didn't work, wipe them away and don't add
        // them to the TypoExprs set. Remove them from Sema's TypoExpr list
        // since we don't want to clear them twice. Note: it's possible the
        // TypoExprs were created recursively and thus won't be in our
        // Sema's TypoExprs - they were created in our `RecursiveTransformLoop`.
        auto &SemaTypoExprs = SemaRef.TypoExprs;
        for (auto *TE : TypoExprs) {
          TransformCache.erase(TE);
          SemaRef.clearDelayedTypo(TE);

          auto SI = find(SemaTypoExprs, TE);
          if (SI != SemaTypoExprs.end()) {
            SemaTypoExprs.erase(SI);
          }
        }
      } else {
        // TypoExpr is valid: add newly created TypoExprs since we were
        // able to correct them.
        Res = RecurResult;
        SavedTypoExprs.set_union(TypoExprs);
      }
    }

    TypoExprs = std::move(SavedTypoExprs);
    AmbiguousTypoExprs = std::move(SavedAmbiguousTypoExprs);

    return Res;
  }

  // Try to transform the given expression, looping through the correction
  // candidates with `CheckAndAdvanceTypoExprCorrectionStreams`.
  //
  // If valid ambiguous typo corrections are seen, `IsAmbiguous` is set to
  // true and this method immediately will return an `ExprError`.
  ExprResult RecursiveTransformLoop(Expr *E, bool &IsAmbiguous) {
    ExprResult Res;
    auto SavedTypoExprs = std::move(SemaRef.TypoExprs);
    SemaRef.TypoExprs.clear();

    while (true) {
      Res = CheckForRecursiveTypos(TryTransform(E), IsAmbiguous);

      // Recursion encountered an ambiguous correction. This means that our
      // correction itself is ambiguous, so stop now.
      if (IsAmbiguous)
        break;

      // If the transform is still valid after checking for any new typos,
      // it's good to go.
      if (!Res.isInvalid())
        break;

      // The transform was invalid, see if we have any TypoExprs with untried
      // correction candidates.
      if (!CheckAndAdvanceTypoExprCorrectionStreams())
        break;
    }

    // If we found a valid result, double check to make sure it's not ambiguous.
    if (!IsAmbiguous && !Res.isInvalid() && !AmbiguousTypoExprs.empty()) {
      auto SavedTransformCache =
          llvm::SmallDenseMap<TypoExpr *, ExprResult, 2>(TransformCache);

      // Ensure none of the TypoExprs have multiple typo correction candidates
      // with the same edit length that pass all the checks and filters.
      while (!AmbiguousTypoExprs.empty()) {
        auto TE  = AmbiguousTypoExprs.back();

        // TryTransform itself can create new Typos, adding them to the TypoExpr map
        // and invalidating our TypoExprState, so always fetch it instead of storing.
        SemaRef.getTypoExprState(TE).Consumer->saveCurrentPosition();

        TypoCorrection TC = SemaRef.getTypoExprState(TE).Consumer->peekNextCorrection();
        TypoCorrection Next;
        do {
          // Fetch the next correction by erasing the typo from the cache and calling
          // `TryTransform` which will iterate through corrections in
          // `TransformTypoExpr`.
          TransformCache.erase(TE);
          ExprResult AmbigRes = CheckForRecursiveTypos(TryTransform(E), IsAmbiguous);

          if (!AmbigRes.isInvalid() || IsAmbiguous) {
            SemaRef.getTypoExprState(TE).Consumer->resetCorrectionStream();
            SavedTransformCache.erase(TE);
            Res = ExprError();
            IsAmbiguous = true;
            break;
          }
        } while ((Next = SemaRef.getTypoExprState(TE).Consumer->peekNextCorrection()) &&
                 Next.getEditDistance(false) == TC.getEditDistance(false));

        if (IsAmbiguous)
          break;

        AmbiguousTypoExprs.remove(TE);
        SemaRef.getTypoExprState(TE).Consumer->restoreSavedPosition();
        TransformCache[TE] = SavedTransformCache[TE];
      }
      TransformCache = std::move(SavedTransformCache);
    }

    // Wipe away any newly created TypoExprs that we don't know about. Since we
    // clear any invalid TypoExprs in `CheckForRecursiveTypos`, this is only
    // possible if a `TypoExpr` is created during a transformation but then
    // fails before we can discover it.
    auto &SemaTypoExprs = SemaRef.TypoExprs;
    for (auto Iterator = SemaTypoExprs.begin(); Iterator != SemaTypoExprs.end();) {
      auto TE = *Iterator;
      auto FI = find(TypoExprs, TE);
      if (FI != TypoExprs.end()) {
        Iterator++;
        continue;
      }
      SemaRef.clearDelayedTypo(TE);
      Iterator = SemaTypoExprs.erase(Iterator);
    }
    SemaRef.TypoExprs = std::move(SavedTypoExprs);

    return Res;
  }

public:
  TransformTypos(Sema &SemaRef, VarDecl *InitDecl, llvm::function_ref<ExprResult(Expr *)> Filter)
      : BaseTransform(SemaRef), InitDecl(InitDecl), ExprFilter(Filter) {}

  ExprResult RebuildCallExpr(Expr *Callee, SourceLocation LParenLoc,
                                   MultiExprArg Args,
                                   SourceLocation RParenLoc,
                                   Expr *ExecConfig = nullptr) {
    auto Result = BaseTransform::RebuildCallExpr(Callee, LParenLoc, Args,
                                                 RParenLoc, ExecConfig);
    if (auto *OE = dyn_cast<OverloadExpr>(Callee)) {
      if (Result.isUsable()) {
        Expr *ResultCall = Result.get();
        if (auto *BE = dyn_cast<CXXBindTemporaryExpr>(ResultCall))
          ResultCall = BE->getSubExpr();
        if (auto *CE = dyn_cast<CallExpr>(ResultCall))
          OverloadResolution[OE] = CE->getCallee();
      }
    }
    return Result;
  }

  ExprResult TransformLambdaExpr(LambdaExpr *E) { return Owned(E); }

  ExprResult TransformBlockExpr(BlockExpr *E) { return Owned(E); }

  ExprResult Transform(Expr *E) {
    bool IsAmbiguous = false;
    ExprResult Res = RecursiveTransformLoop(E, IsAmbiguous);

    if (!Res.isUsable())
      FindTypoExprs(TypoExprs).TraverseStmt(E);

    EmitAllDiagnostics(IsAmbiguous);

    return Res;
  }

  ExprResult TransformTypoExpr(TypoExpr *E) {
    // If the TypoExpr hasn't been seen before, record it. Otherwise, return the
    // cached transformation result if there is one and the TypoExpr isn't the
    // first one that was encountered.
    auto &CacheEntry = TransformCache[E];
    if (!TypoExprs.insert(E) && !CacheEntry.isUnset()) {
      return CacheEntry;
    }

    auto &State = SemaRef.getTypoExprState(E);
    assert(State.Consumer && "Cannot transform a cleared TypoExpr");

    // For the first TypoExpr and an uncached TypoExpr, find the next likely
    // typo correction and return it.
    while (TypoCorrection TC = State.Consumer->getNextCorrection()) {
      if (InitDecl && TC.getFoundDecl() == InitDecl)
        continue;
      // FIXME: If we would typo-correct to an invalid declaration, it's
      // probably best to just suppress all errors from this typo correction.
      ExprResult NE = State.RecoveryHandler ?
          State.RecoveryHandler(SemaRef, E, TC) :
          attemptRecovery(SemaRef, *State.Consumer, TC);
      if (!NE.isInvalid()) {
        // Check whether there may be a second viable correction with the same
        // edit distance; if so, remember this TypoExpr may have an ambiguous
        // correction so it can be more thoroughly vetted later.
        TypoCorrection Next;
        if ((Next = State.Consumer->peekNextCorrection()) &&
            Next.getEditDistance(false) == TC.getEditDistance(false)) {
          AmbiguousTypoExprs.insert(E);
        } else {
          AmbiguousTypoExprs.remove(E);
        }
        assert(!NE.isUnset() &&
               "Typo was transformed into a valid-but-null ExprResult");
        return CacheEntry = NE;
      }
    }
    return CacheEntry = ExprError();
  }
};
}

ExprResult
Sema::CorrectDelayedTyposInExpr(Expr *E, VarDecl *InitDecl,
                                bool RecoverUncorrectedTypos,
                                llvm::function_ref<ExprResult(Expr *)> Filter) {
  // If the current evaluation context indicates there are uncorrected typos
  // and the current expression isn't guaranteed to not have typos, try to
  // resolve any TypoExpr nodes that might be in the expression.
  if (E && !ExprEvalContexts.empty() && ExprEvalContexts.back().NumTypos &&
      (E->isTypeDependent() || E->isValueDependent() ||
       E->isInstantiationDependent())) {
    auto TyposResolved = DelayedTypos.size();
    auto Result = TransformTypos(*this, InitDecl, Filter).Transform(E);
    TyposResolved -= DelayedTypos.size();
    if (Result.isInvalid() || Result.get() != E) {
      ExprEvalContexts.back().NumTypos -= TyposResolved;
      if (Result.isInvalid() && RecoverUncorrectedTypos) {
        struct TyposReplace : TreeTransform<TyposReplace> {
          TyposReplace(Sema &SemaRef) : TreeTransform(SemaRef) {}
          ExprResult TransformTypoExpr(clang::TypoExpr *E) {
            return this->SemaRef.CreateRecoveryExpr(E->getBeginLoc(),
                                                    E->getEndLoc(), {});
          }
        } TT(*this);
        return TT.TransformExpr(E);
      }
      return Result;
    }
    assert(TyposResolved == 0 && "Corrected typo but got same Expr back?");
  }
  return E;
}

ExprResult Sema::ActOnFinishFullExpr(Expr *FE, SourceLocation CC,
                                     bool DiscardedValue, bool IsConstexpr,
                                     bool IsTemplateArgument) {
  ExprResult FullExpr = FE;

  if (!FullExpr.get())
    return ExprError();

  if (!IsTemplateArgument && DiagnoseUnexpandedParameterPack(FullExpr.get()))
    return ExprError();

  if (DiscardedValue) {
    // Top-level expressions default to 'id' when we're in a debugger.
    if (getLangOpts().DebuggerCastResultToId &&
        FullExpr.get()->getType() == Context.UnknownAnyTy) {
      FullExpr = forceUnknownAnyToType(FullExpr.get(), Context.getObjCIdType());
      if (FullExpr.isInvalid())
        return ExprError();
    }

    FullExpr = CheckPlaceholderExpr(FullExpr.get());
    if (FullExpr.isInvalid())
      return ExprError();

    FullExpr = IgnoredValueConversions(FullExpr.get());
    if (FullExpr.isInvalid())
      return ExprError();

    DiagnoseUnusedExprResult(FullExpr.get(), diag::warn_unused_expr);
  }

  FullExpr = CorrectDelayedTyposInExpr(FullExpr.get(), /*InitDecl=*/nullptr,
                                       /*RecoverUncorrectedTypos=*/true);
  if (FullExpr.isInvalid())
    return ExprError();

  CheckCompletedExpr(FullExpr.get(), CC, IsConstexpr);

  // At the end of this full expression (which could be a deeply nested
  // lambda), if there is a potential capture within the nested lambda,
  // have the outer capture-able lambda try and capture it.
  // Consider the following code:
  // void f(int, int);
  // void f(const int&, double);
  // void foo() {
  //  const int x = 10, y = 20;
  //  auto L = [=](auto a) {
  //      auto M = [=](auto b) {
  //         f(x, b); <-- requires x to be captured by L and M
  //         f(y, a); <-- requires y to be captured by L, but not all Ms
  //      };
  //   };
  // }

  // FIXME: Also consider what happens for something like this that involves
  // the gnu-extension statement-expressions or even lambda-init-captures:
  //   void f() {
  //     const int n = 0;
  //     auto L =  [&](auto a) {
  //       +n + ({ 0; a; });
  //     };
  //   }
  //
  // Here, we see +n, and then the full-expression 0; ends, so we don't
  // capture n (and instead remove it from our list of potential captures),
  // and then the full-expression +n + ({ 0; }); ends, but it's too late
  // for us to see that we need to capture n after all.

  LambdaScopeInfo *const CurrentLSI =
      getCurLambda(/*IgnoreCapturedRegions=*/true);
  // FIXME: PR 17877 showed that getCurLambda() can return a valid pointer
  // even if CurContext is not a lambda call operator. Refer to that Bug Report
  // for an example of the code that might cause this asynchrony.
  // By ensuring we are in the context of a lambda's call operator
  // we can fix the bug (we only need to check whether we need to capture
  // if we are within a lambda's body); but per the comments in that
  // PR, a proper fix would entail :
  //   "Alternative suggestion:
  //   - Add to Sema an integer holding the smallest (outermost) scope
  //     index that we are *lexically* within, and save/restore/set to
  //     FunctionScopes.size() in InstantiatingTemplate's
  //     constructor/destructor.
  //  - Teach the handful of places that iterate over FunctionScopes to
  //    stop at the outermost enclosing lexical scope."
  DeclContext *DC = CurContext;
  while (isa_and_nonnull<CapturedDecl>(DC))
    DC = DC->getParent();
  const bool IsInLambdaDeclContext = isLambdaCallOperator(DC);
  if (IsInLambdaDeclContext && CurrentLSI &&
      CurrentLSI->hasPotentialCaptures() && !FullExpr.isInvalid())
    CheckIfAnyEnclosingLambdasMustCaptureAnyPotentialCaptures(FE, CurrentLSI,
                                                              *this);
  return MaybeCreateExprWithCleanups(FullExpr);
}

StmtResult Sema::ActOnFinishFullStmt(Stmt *FullStmt) {
  if (!FullStmt) return StmtError();

  return MaybeCreateStmtWithCleanups(FullStmt);
}

Sema::IfExistsResult
Sema::CheckMicrosoftIfExistsSymbol(Scope *S,
                                   CXXScopeSpec &SS,
                                   const DeclarationNameInfo &TargetNameInfo) {
  DeclarationName TargetName = TargetNameInfo.getName();
  if (!TargetName)
    return IER_DoesNotExist;

  // If the name itself is dependent, then the result is dependent.
  if (TargetName.isDependentName())
    return IER_Dependent;

  // Do the redeclaration lookup in the current scope.
  LookupResult R(*this, TargetNameInfo, Sema::LookupAnyName,
                 RedeclarationKind::NotForRedeclaration);
  LookupParsedName(R, S, &SS, /*ObjectType=*/QualType());
  R.suppressDiagnostics();

  switch (R.getResultKind()) {
  case LookupResult::Found:
  case LookupResult::FoundOverloaded:
  case LookupResult::FoundUnresolvedValue:
  case LookupResult::Ambiguous:
    return IER_Exists;

  case LookupResult::NotFound:
    return IER_DoesNotExist;

  case LookupResult::NotFoundInCurrentInstantiation:
    return IER_Dependent;
  }

  llvm_unreachable("Invalid LookupResult Kind!");
}

Sema::IfExistsResult
Sema::CheckMicrosoftIfExistsSymbol(Scope *S, SourceLocation KeywordLoc,
                                   bool IsIfExists, CXXScopeSpec &SS,
                                   UnqualifiedId &Name) {
  DeclarationNameInfo TargetNameInfo = GetNameFromUnqualifiedId(Name);

  // Check for an unexpanded parameter pack.
  auto UPPC = IsIfExists ? UPPC_IfExists : UPPC_IfNotExists;
  if (DiagnoseUnexpandedParameterPack(SS, UPPC) ||
      DiagnoseUnexpandedParameterPack(TargetNameInfo, UPPC))
    return IER_Error;

  return CheckMicrosoftIfExistsSymbol(S, SS, TargetNameInfo);
}

concepts::Requirement *Sema::ActOnSimpleRequirement(Expr *E) {
  return BuildExprRequirement(E, /*IsSimple=*/true,
                              /*NoexceptLoc=*/SourceLocation(),
                              /*ReturnTypeRequirement=*/{});
}

concepts::Requirement *Sema::ActOnTypeRequirement(
    SourceLocation TypenameKWLoc, CXXScopeSpec &SS, SourceLocation NameLoc,
    const IdentifierInfo *TypeName, TemplateIdAnnotation *TemplateId) {
  assert(((!TypeName && TemplateId) || (TypeName && !TemplateId)) &&
         "Exactly one of TypeName and TemplateId must be specified.");
  TypeSourceInfo *TSI = nullptr;
  if (TypeName) {
    QualType T =
        CheckTypenameType(ElaboratedTypeKeyword::Typename, TypenameKWLoc,
                          SS.getWithLocInContext(Context), *TypeName, NameLoc,
                          &TSI, /*DeducedTSTContext=*/false);
    if (T.isNull())
      return nullptr;
  } else {
    ASTTemplateArgsPtr ArgsPtr(TemplateId->getTemplateArgs(),
                               TemplateId->NumArgs);
    TypeResult T = ActOnTypenameType(CurScope, TypenameKWLoc, SS,
                                     TemplateId->TemplateKWLoc,
                                     TemplateId->Template, TemplateId->Name,
                                     TemplateId->TemplateNameLoc,
                                     TemplateId->LAngleLoc, ArgsPtr,
                                     TemplateId->RAngleLoc);
    if (T.isInvalid())
      return nullptr;
    if (GetTypeFromParser(T.get(), &TSI).isNull())
      return nullptr;
  }
  return BuildTypeRequirement(TSI);
}

concepts::Requirement *
Sema::ActOnCompoundRequirement(Expr *E, SourceLocation NoexceptLoc) {
  return BuildExprRequirement(E, /*IsSimple=*/false, NoexceptLoc,
                              /*ReturnTypeRequirement=*/{});
}

concepts::Requirement *
Sema::ActOnCompoundRequirement(
    Expr *E, SourceLocation NoexceptLoc, CXXScopeSpec &SS,
    TemplateIdAnnotation *TypeConstraint, unsigned Depth) {
  // C++2a [expr.prim.req.compound] p1.3.3
  //   [..] the expression is deduced against an invented function template
  //   F [...] F is a void function template with a single type template
  //   parameter T declared with the constrained-parameter. Form a new
  //   cv-qualifier-seq cv by taking the union of const and volatile specifiers
  //   around the constrained-parameter. F has a single parameter whose
  //   type-specifier is cv T followed by the abstract-declarator. [...]
  //
  // The cv part is done in the calling function - we get the concept with
  // arguments and the abstract declarator with the correct CV qualification and
  // have to synthesize T and the single parameter of F.
  auto &II = Context.Idents.get("expr-type");
  auto *TParam = TemplateTypeParmDecl::Create(Context, CurContext,
                                              SourceLocation(),
                                              SourceLocation(), Depth,
                                              /*Index=*/0, &II,
                                              /*Typename=*/true,
                                              /*ParameterPack=*/false,
                                              /*HasTypeConstraint=*/true);

  if (BuildTypeConstraint(SS, TypeConstraint, TParam,
                          /*EllipsisLoc=*/SourceLocation(),
                          /*AllowUnexpandedPack=*/true))
    // Just produce a requirement with no type requirements.
    return BuildExprRequirement(E, /*IsSimple=*/false, NoexceptLoc, {});

  auto *TPL = TemplateParameterList::Create(Context, SourceLocation(),
                                            SourceLocation(),
                                            ArrayRef<NamedDecl *>(TParam),
                                            SourceLocation(),
                                            /*RequiresClause=*/nullptr);
  return BuildExprRequirement(
      E, /*IsSimple=*/false, NoexceptLoc,
      concepts::ExprRequirement::ReturnTypeRequirement(TPL));
}

concepts::ExprRequirement *
Sema::BuildExprRequirement(
    Expr *E, bool IsSimple, SourceLocation NoexceptLoc,
    concepts::ExprRequirement::ReturnTypeRequirement ReturnTypeRequirement) {
  auto Status = concepts::ExprRequirement::SS_Satisfied;
  ConceptSpecializationExpr *SubstitutedConstraintExpr = nullptr;
  if (E->isInstantiationDependent() || E->getType()->isPlaceholderType() ||
      ReturnTypeRequirement.isDependent())
    Status = concepts::ExprRequirement::SS_Dependent;
  else if (NoexceptLoc.isValid() && canThrow(E) == CanThrowResult::CT_Can)
    Status = concepts::ExprRequirement::SS_NoexceptNotMet;
  else if (ReturnTypeRequirement.isSubstitutionFailure())
    Status = concepts::ExprRequirement::SS_TypeRequirementSubstitutionFailure;
  else if (ReturnTypeRequirement.isTypeConstraint()) {
    // C++2a [expr.prim.req]p1.3.3
    //     The immediately-declared constraint ([temp]) of decltype((E)) shall
    //     be satisfied.
    TemplateParameterList *TPL =
        ReturnTypeRequirement.getTypeConstraintTemplateParameterList();
    QualType MatchedType =
        Context.getReferenceQualifiedType(E).getCanonicalType();
    llvm::SmallVector<TemplateArgument, 1> Args;
    Args.push_back(TemplateArgument(MatchedType));

    auto *Param = cast<TemplateTypeParmDecl>(TPL->getParam(0));

    MultiLevelTemplateArgumentList MLTAL(Param, Args, /*Final=*/false);
    MLTAL.addOuterRetainedLevels(TPL->getDepth());
    const TypeConstraint *TC = Param->getTypeConstraint();
    assert(TC && "Type Constraint cannot be null here");
    auto *IDC = TC->getImmediatelyDeclaredConstraint();
    assert(IDC && "ImmediatelyDeclaredConstraint can't be null here.");
    ExprResult Constraint = SubstExpr(IDC, MLTAL);
    if (Constraint.isInvalid()) {
      return new (Context) concepts::ExprRequirement(
          concepts::createSubstDiagAt(*this, IDC->getExprLoc(),
                                      [&](llvm::raw_ostream &OS) {
                                        IDC->printPretty(OS, /*Helper=*/nullptr,
                                                         getPrintingPolicy());
                                      }),
          IsSimple, NoexceptLoc, ReturnTypeRequirement);
    }
    SubstitutedConstraintExpr =
        cast<ConceptSpecializationExpr>(Constraint.get());
    if (!SubstitutedConstraintExpr->isSatisfied())
      Status = concepts::ExprRequirement::SS_ConstraintsNotSatisfied;
  }
  return new (Context) concepts::ExprRequirement(E, IsSimple, NoexceptLoc,
                                                 ReturnTypeRequirement, Status,
                                                 SubstitutedConstraintExpr);
}

concepts::ExprRequirement *
Sema::BuildExprRequirement(
    concepts::Requirement::SubstitutionDiagnostic *ExprSubstitutionDiagnostic,
    bool IsSimple, SourceLocation NoexceptLoc,
    concepts::ExprRequirement::ReturnTypeRequirement ReturnTypeRequirement) {
  return new (Context) concepts::ExprRequirement(ExprSubstitutionDiagnostic,
                                                 IsSimple, NoexceptLoc,
                                                 ReturnTypeRequirement);
}

concepts::TypeRequirement *
Sema::BuildTypeRequirement(TypeSourceInfo *Type) {
  return new (Context) concepts::TypeRequirement(Type);
}

concepts::TypeRequirement *
Sema::BuildTypeRequirement(
    concepts::Requirement::SubstitutionDiagnostic *SubstDiag) {
  return new (Context) concepts::TypeRequirement(SubstDiag);
}

concepts::Requirement *Sema::ActOnNestedRequirement(Expr *Constraint) {
  return BuildNestedRequirement(Constraint);
}

concepts::NestedRequirement *
Sema::BuildNestedRequirement(Expr *Constraint) {
  ConstraintSatisfaction Satisfaction;
  if (!Constraint->isInstantiationDependent() &&
      CheckConstraintSatisfaction(nullptr, {Constraint}, /*TemplateArgs=*/{},
                                  Constraint->getSourceRange(), Satisfaction))
    return nullptr;
  return new (Context) concepts::NestedRequirement(Context, Constraint,
                                                   Satisfaction);
}

concepts::NestedRequirement *
Sema::BuildNestedRequirement(StringRef InvalidConstraintEntity,
                       const ASTConstraintSatisfaction &Satisfaction) {
  return new (Context) concepts::NestedRequirement(
      InvalidConstraintEntity,
      ASTConstraintSatisfaction::Rebuild(Context, Satisfaction));
}

RequiresExprBodyDecl *
Sema::ActOnStartRequiresExpr(SourceLocation RequiresKWLoc,
                             ArrayRef<ParmVarDecl *> LocalParameters,
                             Scope *BodyScope) {
  assert(BodyScope);

  RequiresExprBodyDecl *Body = RequiresExprBodyDecl::Create(Context, CurContext,
                                                            RequiresKWLoc);

  PushDeclContext(BodyScope, Body);

  for (ParmVarDecl *Param : LocalParameters) {
    if (Param->hasDefaultArg())
      // C++2a [expr.prim.req] p4
      //     [...] A local parameter of a requires-expression shall not have a
      //     default argument. [...]
      Diag(Param->getDefaultArgRange().getBegin(),
           diag::err_requires_expr_local_parameter_default_argument);
    // Ignore default argument and move on

    Param->setDeclContext(Body);
    // If this has an identifier, add it to the scope stack.
    if (Param->getIdentifier()) {
      CheckShadow(BodyScope, Param);
      PushOnScopeChains(Param, BodyScope);
    }
  }
  return Body;
}

void Sema::ActOnFinishRequiresExpr() {
  assert(CurContext && "DeclContext imbalance!");
  CurContext = CurContext->getLexicalParent();
  assert(CurContext && "Popped translation unit!");
}

ExprResult Sema::ActOnRequiresExpr(
    SourceLocation RequiresKWLoc, RequiresExprBodyDecl *Body,
    SourceLocation LParenLoc, ArrayRef<ParmVarDecl *> LocalParameters,
    SourceLocation RParenLoc, ArrayRef<concepts::Requirement *> Requirements,
    SourceLocation ClosingBraceLoc) {
  auto *RE = RequiresExpr::Create(Context, RequiresKWLoc, Body, LParenLoc,
                                  LocalParameters, RParenLoc, Requirements,
                                  ClosingBraceLoc);
  if (DiagnoseUnexpandedParameterPackInRequiresExpr(RE))
    return ExprError();
  return RE;
}
