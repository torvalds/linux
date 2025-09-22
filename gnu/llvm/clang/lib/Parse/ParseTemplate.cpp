//===--- ParseTemplate.cpp - Template Parsing -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements parsing of C++ templates.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "llvm/Support/TimeProfiler.h"
using namespace clang;

/// Re-enter a possible template scope, creating as many template parameter
/// scopes as necessary.
/// \return The number of template parameter scopes entered.
unsigned Parser::ReenterTemplateScopes(MultiParseScope &S, Decl *D) {
  return Actions.ActOnReenterTemplateScope(D, [&] {
    S.Enter(Scope::TemplateParamScope);
    return Actions.getCurScope();
  });
}

/// Parse a template declaration, explicit instantiation, or
/// explicit specialization.
Parser::DeclGroupPtrTy
Parser::ParseDeclarationStartingWithTemplate(DeclaratorContext Context,
                                             SourceLocation &DeclEnd,
                                             ParsedAttributes &AccessAttrs) {
  ObjCDeclContextSwitch ObjCDC(*this);

  if (Tok.is(tok::kw_template) && NextToken().isNot(tok::less)) {
    return ParseExplicitInstantiation(Context, SourceLocation(), ConsumeToken(),
                                      DeclEnd, AccessAttrs,
                                      AccessSpecifier::AS_none);
  }
  return ParseTemplateDeclarationOrSpecialization(Context, DeclEnd, AccessAttrs,
                                                  AccessSpecifier::AS_none);
}

/// Parse a template declaration or an explicit specialization.
///
/// Template declarations include one or more template parameter lists
/// and either the function or class template declaration. Explicit
/// specializations contain one or more 'template < >' prefixes
/// followed by a (possibly templated) declaration. Since the
/// syntactic form of both features is nearly identical, we parse all
/// of the template headers together and let semantic analysis sort
/// the declarations from the explicit specializations.
///
///       template-declaration: [C++ temp]
///         'export'[opt] 'template' '<' template-parameter-list '>' declaration
///
///       template-declaration: [C++2a]
///         template-head declaration
///         template-head concept-definition
///
///       TODO: requires-clause
///       template-head: [C++2a]
///         'template' '<' template-parameter-list '>'
///             requires-clause[opt]
///
///       explicit-specialization: [ C++ temp.expl.spec]
///         'template' '<' '>' declaration
Parser::DeclGroupPtrTy Parser::ParseTemplateDeclarationOrSpecialization(
    DeclaratorContext Context, SourceLocation &DeclEnd,
    ParsedAttributes &AccessAttrs, AccessSpecifier AS) {
  assert(Tok.isOneOf(tok::kw_export, tok::kw_template) &&
         "Token does not start a template declaration.");

  MultiParseScope TemplateParamScopes(*this);

  // Tell the action that names should be checked in the context of
  // the declaration to come.
  ParsingDeclRAIIObject
    ParsingTemplateParams(*this, ParsingDeclRAIIObject::NoParent);

  // Parse multiple levels of template headers within this template
  // parameter scope, e.g.,
  //
  //   template<typename T>
  //     template<typename U>
  //       class A<T>::B { ... };
  //
  // We parse multiple levels non-recursively so that we can build a
  // single data structure containing all of the template parameter
  // lists to easily differentiate between the case above and:
  //
  //   template<typename T>
  //   class A {
  //     template<typename U> class B;
  //   };
  //
  // In the first case, the action for declaring A<T>::B receives
  // both template parameter lists. In the second case, the action for
  // defining A<T>::B receives just the inner template parameter list
  // (and retrieves the outer template parameter list from its
  // context).
  bool isSpecialization = true;
  bool LastParamListWasEmpty = false;
  TemplateParameterLists ParamLists;
  TemplateParameterDepthRAII CurTemplateDepthTracker(TemplateParameterDepth);

  do {
    // Consume the 'export', if any.
    SourceLocation ExportLoc;
    TryConsumeToken(tok::kw_export, ExportLoc);

    // Consume the 'template', which should be here.
    SourceLocation TemplateLoc;
    if (!TryConsumeToken(tok::kw_template, TemplateLoc)) {
      Diag(Tok.getLocation(), diag::err_expected_template);
      return nullptr;
    }

    // Parse the '<' template-parameter-list '>'
    SourceLocation LAngleLoc, RAngleLoc;
    SmallVector<NamedDecl*, 4> TemplateParams;
    if (ParseTemplateParameters(TemplateParamScopes,
                                CurTemplateDepthTracker.getDepth(),
                                TemplateParams, LAngleLoc, RAngleLoc)) {
      // Skip until the semi-colon or a '}'.
      SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
      TryConsumeToken(tok::semi);
      return nullptr;
    }

    ExprResult OptionalRequiresClauseConstraintER;
    if (!TemplateParams.empty()) {
      isSpecialization = false;
      ++CurTemplateDepthTracker;

      if (TryConsumeToken(tok::kw_requires)) {
        OptionalRequiresClauseConstraintER =
            Actions.ActOnRequiresClause(ParseConstraintLogicalOrExpression(
                /*IsTrailingRequiresClause=*/false));
        if (!OptionalRequiresClauseConstraintER.isUsable()) {
          // Skip until the semi-colon or a '}'.
          SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
          TryConsumeToken(tok::semi);
          return nullptr;
        }
      }
    } else {
      LastParamListWasEmpty = true;
    }

    ParamLists.push_back(Actions.ActOnTemplateParameterList(
        CurTemplateDepthTracker.getDepth(), ExportLoc, TemplateLoc, LAngleLoc,
        TemplateParams, RAngleLoc, OptionalRequiresClauseConstraintER.get()));
  } while (Tok.isOneOf(tok::kw_export, tok::kw_template));

  ParsedTemplateInfo TemplateInfo(&ParamLists, isSpecialization,
                                  LastParamListWasEmpty);

  // Parse the actual template declaration.
  if (Tok.is(tok::kw_concept)) {
    Decl *ConceptDecl = ParseConceptDefinition(TemplateInfo, DeclEnd);
    // We need to explicitly pass ConceptDecl to ParsingDeclRAIIObject, so that
    // delayed diagnostics (e.g. warn_deprecated) have a Decl to work with.
    ParsingTemplateParams.complete(ConceptDecl);
    return Actions.ConvertDeclToDeclGroup(ConceptDecl);
  }

  return ParseDeclarationAfterTemplate(
      Context, TemplateInfo, ParsingTemplateParams, DeclEnd, AccessAttrs, AS);
}

/// Parse a single declaration that declares a template,
/// template specialization, or explicit instantiation of a template.
///
/// \param DeclEnd will receive the source location of the last token
/// within this declaration.
///
/// \param AS the access specifier associated with this
/// declaration. Will be AS_none for namespace-scope declarations.
///
/// \returns the new declaration.
Parser::DeclGroupPtrTy Parser::ParseDeclarationAfterTemplate(
    DeclaratorContext Context, ParsedTemplateInfo &TemplateInfo,
    ParsingDeclRAIIObject &DiagsFromTParams, SourceLocation &DeclEnd,
    ParsedAttributes &AccessAttrs, AccessSpecifier AS) {
  assert(TemplateInfo.Kind != ParsedTemplateInfo::NonTemplate &&
         "Template information required");

  if (Tok.is(tok::kw_static_assert)) {
    // A static_assert declaration may not be templated.
    Diag(Tok.getLocation(), diag::err_templated_invalid_declaration)
      << TemplateInfo.getSourceRange();
    // Parse the static_assert declaration to improve error recovery.
    return Actions.ConvertDeclToDeclGroup(
        ParseStaticAssertDeclaration(DeclEnd));
  }

  // We are parsing a member template.
  if (Context == DeclaratorContext::Member)
    return ParseCXXClassMemberDeclaration(AS, AccessAttrs, TemplateInfo,
                                          &DiagsFromTParams);

  ParsedAttributes DeclAttrs(AttrFactory);
  ParsedAttributes DeclSpecAttrs(AttrFactory);

  // GNU attributes are applied to the declaration specification while the
  // standard attributes are applied to the declaration.  We parse the two
  // attribute sets into different containters so we can apply them during
  // the regular parsing process.
  while (MaybeParseCXX11Attributes(DeclAttrs) ||
         MaybeParseGNUAttributes(DeclSpecAttrs))
    ;

  if (Tok.is(tok::kw_using))
    return ParseUsingDirectiveOrDeclaration(Context, TemplateInfo, DeclEnd,
                                            DeclAttrs);

  // Parse the declaration specifiers, stealing any diagnostics from
  // the template parameters.
  ParsingDeclSpec DS(*this, &DiagsFromTParams);
  DS.SetRangeStart(DeclSpecAttrs.Range.getBegin());
  DS.SetRangeEnd(DeclSpecAttrs.Range.getEnd());
  DS.takeAttributesFrom(DeclSpecAttrs);

  ParseDeclarationSpecifiers(DS, TemplateInfo, AS,
                             getDeclSpecContextFromDeclaratorContext(Context));

  if (Tok.is(tok::semi)) {
    ProhibitAttributes(DeclAttrs);
    DeclEnd = ConsumeToken();
    RecordDecl *AnonRecord = nullptr;
    Decl *Decl = Actions.ParsedFreeStandingDeclSpec(
        getCurScope(), AS, DS, ParsedAttributesView::none(),
        TemplateInfo.TemplateParams ? *TemplateInfo.TemplateParams
                                    : MultiTemplateParamsArg(),
        TemplateInfo.Kind == ParsedTemplateInfo::ExplicitInstantiation,
        AnonRecord);
    Actions.ActOnDefinedDeclarationSpecifier(Decl);
    assert(!AnonRecord &&
           "Anonymous unions/structs should not be valid with template");
    DS.complete(Decl);
    return Actions.ConvertDeclToDeclGroup(Decl);
  }

  if (DS.hasTagDefinition())
    Actions.ActOnDefinedDeclarationSpecifier(DS.getRepAsDecl());

  // Move the attributes from the prefix into the DS.
  if (TemplateInfo.Kind == ParsedTemplateInfo::ExplicitInstantiation)
    ProhibitAttributes(DeclAttrs);

  return ParseDeclGroup(DS, Context, DeclAttrs, TemplateInfo, &DeclEnd);
}

/// \brief Parse a single declaration that declares a concept.
///
/// \param DeclEnd will receive the source location of the last token
/// within this declaration.
///
/// \returns the new declaration.
Decl *
Parser::ParseConceptDefinition(const ParsedTemplateInfo &TemplateInfo,
                               SourceLocation &DeclEnd) {
  assert(TemplateInfo.Kind != ParsedTemplateInfo::NonTemplate &&
         "Template information required");
  assert(Tok.is(tok::kw_concept) &&
         "ParseConceptDefinition must be called when at a 'concept' keyword");

  ConsumeToken(); // Consume 'concept'

  SourceLocation BoolKWLoc;
  if (TryConsumeToken(tok::kw_bool, BoolKWLoc))
    Diag(Tok.getLocation(), diag::err_concept_legacy_bool_keyword) <<
        FixItHint::CreateRemoval(SourceLocation(BoolKWLoc));

  DiagnoseAndSkipCXX11Attributes();

  CXXScopeSpec SS;
  if (ParseOptionalCXXScopeSpecifier(
          SS, /*ObjectType=*/nullptr,
          /*ObjectHasErrors=*/false, /*EnteringContext=*/false,
          /*MayBePseudoDestructor=*/nullptr,
          /*IsTypename=*/false, /*LastII=*/nullptr, /*OnlyNamespace=*/true) ||
      SS.isInvalid()) {
    SkipUntil(tok::semi);
    return nullptr;
  }

  if (SS.isNotEmpty())
    Diag(SS.getBeginLoc(),
         diag::err_concept_definition_not_identifier);

  UnqualifiedId Result;
  if (ParseUnqualifiedId(SS, /*ObjectType=*/nullptr,
                         /*ObjectHadErrors=*/false, /*EnteringContext=*/false,
                         /*AllowDestructorName=*/false,
                         /*AllowConstructorName=*/false,
                         /*AllowDeductionGuide=*/false,
                         /*TemplateKWLoc=*/nullptr, Result)) {
    SkipUntil(tok::semi);
    return nullptr;
  }

  if (Result.getKind() != UnqualifiedIdKind::IK_Identifier) {
    Diag(Result.getBeginLoc(), diag::err_concept_definition_not_identifier);
    SkipUntil(tok::semi);
    return nullptr;
  }

  const IdentifierInfo *Id = Result.Identifier;
  SourceLocation IdLoc = Result.getBeginLoc();

  ParsedAttributes Attrs(AttrFactory);
  MaybeParseAttributes(PAKM_GNU | PAKM_CXX11, Attrs);

  if (!TryConsumeToken(tok::equal)) {
    Diag(Tok.getLocation(), diag::err_expected) << tok::equal;
    SkipUntil(tok::semi);
    return nullptr;
  }

  ExprResult ConstraintExprResult =
      Actions.CorrectDelayedTyposInExpr(ParseConstraintExpression());
  if (ConstraintExprResult.isInvalid()) {
    SkipUntil(tok::semi);
    return nullptr;
  }

  DeclEnd = Tok.getLocation();
  ExpectAndConsumeSemi(diag::err_expected_semi_declaration);
  Expr *ConstraintExpr = ConstraintExprResult.get();
  return Actions.ActOnConceptDefinition(getCurScope(),
                                        *TemplateInfo.TemplateParams, Id, IdLoc,
                                        ConstraintExpr, Attrs);
}

/// ParseTemplateParameters - Parses a template-parameter-list enclosed in
/// angle brackets. Depth is the depth of this template-parameter-list, which
/// is the number of template headers directly enclosing this template header.
/// TemplateParams is the current list of template parameters we're building.
/// The template parameter we parse will be added to this list. LAngleLoc and
/// RAngleLoc will receive the positions of the '<' and '>', respectively,
/// that enclose this template parameter list.
///
/// \returns true if an error occurred, false otherwise.
bool Parser::ParseTemplateParameters(
    MultiParseScope &TemplateScopes, unsigned Depth,
    SmallVectorImpl<NamedDecl *> &TemplateParams, SourceLocation &LAngleLoc,
    SourceLocation &RAngleLoc) {
  // Get the template parameter list.
  if (!TryConsumeToken(tok::less, LAngleLoc)) {
    Diag(Tok.getLocation(), diag::err_expected_less_after) << "template";
    return true;
  }

  // Try to parse the template parameter list.
  bool Failed = false;
  // FIXME: Missing greatergreatergreater support.
  if (!Tok.is(tok::greater) && !Tok.is(tok::greatergreater)) {
    TemplateScopes.Enter(Scope::TemplateParamScope);
    Failed = ParseTemplateParameterList(Depth, TemplateParams);
  }

  if (Tok.is(tok::greatergreater)) {
    // No diagnostic required here: a template-parameter-list can only be
    // followed by a declaration or, for a template template parameter, the
    // 'class' keyword. Therefore, the second '>' will be diagnosed later.
    // This matters for elegant diagnosis of:
    //   template<template<typename>> struct S;
    Tok.setKind(tok::greater);
    RAngleLoc = Tok.getLocation();
    Tok.setLocation(Tok.getLocation().getLocWithOffset(1));
  } else if (!TryConsumeToken(tok::greater, RAngleLoc) && Failed) {
    Diag(Tok.getLocation(), diag::err_expected) << tok::greater;
    return true;
  }
  return false;
}

/// ParseTemplateParameterList - Parse a template parameter list. If
/// the parsing fails badly (i.e., closing bracket was left out), this
/// will try to put the token stream in a reasonable position (closing
/// a statement, etc.) and return false.
///
///       template-parameter-list:    [C++ temp]
///         template-parameter
///         template-parameter-list ',' template-parameter
bool
Parser::ParseTemplateParameterList(const unsigned Depth,
                             SmallVectorImpl<NamedDecl*> &TemplateParams) {
  while (true) {

    if (NamedDecl *TmpParam
          = ParseTemplateParameter(Depth, TemplateParams.size())) {
      TemplateParams.push_back(TmpParam);
    } else {
      // If we failed to parse a template parameter, skip until we find
      // a comma or closing brace.
      SkipUntil(tok::comma, tok::greater, tok::greatergreater,
                StopAtSemi | StopBeforeMatch);
    }

    // Did we find a comma or the end of the template parameter list?
    if (Tok.is(tok::comma)) {
      ConsumeToken();
    } else if (Tok.isOneOf(tok::greater, tok::greatergreater)) {
      // Don't consume this... that's done by template parser.
      break;
    } else {
      // Somebody probably forgot to close the template. Skip ahead and
      // try to get out of the expression. This error is currently
      // subsumed by whatever goes on in ParseTemplateParameter.
      Diag(Tok.getLocation(), diag::err_expected_comma_greater);
      SkipUntil(tok::comma, tok::greater, tok::greatergreater,
                StopAtSemi | StopBeforeMatch);
      return false;
    }
  }
  return true;
}

/// Determine whether the parser is at the start of a template
/// type parameter.
Parser::TPResult Parser::isStartOfTemplateTypeParameter() {
  if (Tok.is(tok::kw_class)) {
    // "class" may be the start of an elaborated-type-specifier or a
    // type-parameter. Per C++ [temp.param]p3, we prefer the type-parameter.
    switch (NextToken().getKind()) {
    case tok::equal:
    case tok::comma:
    case tok::greater:
    case tok::greatergreater:
    case tok::ellipsis:
      return TPResult::True;

    case tok::identifier:
      // This may be either a type-parameter or an elaborated-type-specifier.
      // We have to look further.
      break;

    default:
      return TPResult::False;
    }

    switch (GetLookAheadToken(2).getKind()) {
    case tok::equal:
    case tok::comma:
    case tok::greater:
    case tok::greatergreater:
      return TPResult::True;

    default:
      return TPResult::False;
    }
  }

  if (TryAnnotateTypeConstraint())
    return TPResult::Error;

  if (isTypeConstraintAnnotation() &&
      // Next token might be 'auto' or 'decltype', indicating that this
      // type-constraint is in fact part of a placeholder-type-specifier of a
      // non-type template parameter.
      !GetLookAheadToken(Tok.is(tok::annot_cxxscope) ? 2 : 1)
           .isOneOf(tok::kw_auto, tok::kw_decltype))
    return TPResult::True;

  // 'typedef' is a reasonably-common typo/thinko for 'typename', and is
  // ill-formed otherwise.
  if (Tok.isNot(tok::kw_typename) && Tok.isNot(tok::kw_typedef))
    return TPResult::False;

  // C++ [temp.param]p2:
  //   There is no semantic difference between class and typename in a
  //   template-parameter. typename followed by an unqualified-id
  //   names a template type parameter. typename followed by a
  //   qualified-id denotes the type in a non-type
  //   parameter-declaration.
  Token Next = NextToken();

  // If we have an identifier, skip over it.
  if (Next.getKind() == tok::identifier)
    Next = GetLookAheadToken(2);

  switch (Next.getKind()) {
  case tok::equal:
  case tok::comma:
  case tok::greater:
  case tok::greatergreater:
  case tok::ellipsis:
    return TPResult::True;

  case tok::kw_typename:
  case tok::kw_typedef:
  case tok::kw_class:
    // These indicate that a comma was missed after a type parameter, not that
    // we have found a non-type parameter.
    return TPResult::True;

  default:
    return TPResult::False;
  }
}

/// ParseTemplateParameter - Parse a template-parameter (C++ [temp.param]).
///
///       template-parameter: [C++ temp.param]
///         type-parameter
///         parameter-declaration
///
///       type-parameter: (See below)
///         type-parameter-key ...[opt] identifier[opt]
///         type-parameter-key identifier[opt] = type-id
/// (C++2a) type-constraint ...[opt] identifier[opt]
/// (C++2a) type-constraint identifier[opt] = type-id
///         'template' '<' template-parameter-list '>' type-parameter-key
///               ...[opt] identifier[opt]
///         'template' '<' template-parameter-list '>' type-parameter-key
///               identifier[opt] '=' id-expression
///
///       type-parameter-key:
///         class
///         typename
///
NamedDecl *Parser::ParseTemplateParameter(unsigned Depth, unsigned Position) {

  switch (isStartOfTemplateTypeParameter()) {
  case TPResult::True:
    // Is there just a typo in the input code? ('typedef' instead of
    // 'typename')
    if (Tok.is(tok::kw_typedef)) {
      Diag(Tok.getLocation(), diag::err_expected_template_parameter);

      Diag(Tok.getLocation(), diag::note_meant_to_use_typename)
          << FixItHint::CreateReplacement(CharSourceRange::getCharRange(
                                              Tok.getLocation(),
                                              Tok.getEndLoc()),
                                          "typename");

      Tok.setKind(tok::kw_typename);
    }

    return ParseTypeParameter(Depth, Position);
  case TPResult::False:
    break;

  case TPResult::Error: {
    // We return an invalid parameter as opposed to null to avoid having bogus
    // diagnostics about an empty template parameter list.
    // FIXME: Fix ParseTemplateParameterList to better handle nullptr results
    //  from here.
    // Return a NTTP as if there was an error in a scope specifier, the user
    // probably meant to write the type of a NTTP.
    DeclSpec DS(getAttrFactory());
    DS.SetTypeSpecError();
    Declarator D(DS, ParsedAttributesView::none(),
                 DeclaratorContext::TemplateParam);
    D.SetIdentifier(nullptr, Tok.getLocation());
    D.setInvalidType(true);
    NamedDecl *ErrorParam = Actions.ActOnNonTypeTemplateParameter(
        getCurScope(), D, Depth, Position, /*EqualLoc=*/SourceLocation(),
        /*DefaultArg=*/nullptr);
    ErrorParam->setInvalidDecl(true);
    SkipUntil(tok::comma, tok::greater, tok::greatergreater,
              StopAtSemi | StopBeforeMatch);
    return ErrorParam;
  }

  case TPResult::Ambiguous:
    llvm_unreachable("template param classification can't be ambiguous");
  }

  if (Tok.is(tok::kw_template))
    return ParseTemplateTemplateParameter(Depth, Position);

  // If it's none of the above, then it must be a parameter declaration.
  // NOTE: This will pick up errors in the closure of the template parameter
  // list (e.g., template < ; Check here to implement >> style closures.
  return ParseNonTypeTemplateParameter(Depth, Position);
}

/// Check whether the current token is a template-id annotation denoting a
/// type-constraint.
bool Parser::isTypeConstraintAnnotation() {
  const Token &T = Tok.is(tok::annot_cxxscope) ? NextToken() : Tok;
  if (T.isNot(tok::annot_template_id))
    return false;
  const auto *ExistingAnnot =
      static_cast<TemplateIdAnnotation *>(T.getAnnotationValue());
  return ExistingAnnot->Kind == TNK_Concept_template;
}

/// Try parsing a type-constraint at the current location.
///
///     type-constraint:
///       nested-name-specifier[opt] concept-name
///       nested-name-specifier[opt] concept-name
///           '<' template-argument-list[opt] '>'[opt]
///
/// \returns true if an error occurred, and false otherwise.
bool Parser::TryAnnotateTypeConstraint() {
  if (!getLangOpts().CPlusPlus20)
    return false;
  CXXScopeSpec SS;
  bool WasScopeAnnotation = Tok.is(tok::annot_cxxscope);
  if (ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/nullptr,
                                     /*ObjectHasErrors=*/false,
                                     /*EnteringContext=*/false,
                                     /*MayBePseudoDestructor=*/nullptr,
                                     // If this is not a type-constraint, then
                                     // this scope-spec is part of the typename
                                     // of a non-type template parameter
                                     /*IsTypename=*/true, /*LastII=*/nullptr,
                                     // We won't find concepts in
                                     // non-namespaces anyway, so might as well
                                     // parse this correctly for possible type
                                     // names.
                                     /*OnlyNamespace=*/false))
    return true;

  if (Tok.is(tok::identifier)) {
    UnqualifiedId PossibleConceptName;
    PossibleConceptName.setIdentifier(Tok.getIdentifierInfo(),
                                      Tok.getLocation());

    TemplateTy PossibleConcept;
    bool MemberOfUnknownSpecialization = false;
    auto TNK = Actions.isTemplateName(getCurScope(), SS,
                                      /*hasTemplateKeyword=*/false,
                                      PossibleConceptName,
                                      /*ObjectType=*/ParsedType(),
                                      /*EnteringContext=*/false,
                                      PossibleConcept,
                                      MemberOfUnknownSpecialization,
                                      /*Disambiguation=*/true);
    if (MemberOfUnknownSpecialization || !PossibleConcept ||
        TNK != TNK_Concept_template) {
      if (SS.isNotEmpty())
        AnnotateScopeToken(SS, !WasScopeAnnotation);
      return false;
    }

    // At this point we're sure we're dealing with a constrained parameter. It
    // may or may not have a template parameter list following the concept
    // name.
    if (AnnotateTemplateIdToken(PossibleConcept, TNK, SS,
                                   /*TemplateKWLoc=*/SourceLocation(),
                                   PossibleConceptName,
                                   /*AllowTypeAnnotation=*/false,
                                   /*TypeConstraint=*/true))
      return true;
  }

  if (SS.isNotEmpty())
    AnnotateScopeToken(SS, !WasScopeAnnotation);
  return false;
}

/// ParseTypeParameter - Parse a template type parameter (C++ [temp.param]).
/// Other kinds of template parameters are parsed in
/// ParseTemplateTemplateParameter and ParseNonTypeTemplateParameter.
///
///       type-parameter:     [C++ temp.param]
///         'class' ...[opt][C++0x] identifier[opt]
///         'class' identifier[opt] '=' type-id
///         'typename' ...[opt][C++0x] identifier[opt]
///         'typename' identifier[opt] '=' type-id
NamedDecl *Parser::ParseTypeParameter(unsigned Depth, unsigned Position) {
  assert((Tok.isOneOf(tok::kw_class, tok::kw_typename) ||
          isTypeConstraintAnnotation()) &&
         "A type-parameter starts with 'class', 'typename' or a "
         "type-constraint");

  CXXScopeSpec TypeConstraintSS;
  TemplateIdAnnotation *TypeConstraint = nullptr;
  bool TypenameKeyword = false;
  SourceLocation KeyLoc;
  ParseOptionalCXXScopeSpecifier(TypeConstraintSS, /*ObjectType=*/nullptr,
                                 /*ObjectHasErrors=*/false,
                                 /*EnteringContext*/ false);
  if (Tok.is(tok::annot_template_id)) {
    // Consume the 'type-constraint'.
    TypeConstraint =
        static_cast<TemplateIdAnnotation *>(Tok.getAnnotationValue());
    assert(TypeConstraint->Kind == TNK_Concept_template &&
           "stray non-concept template-id annotation");
    KeyLoc = ConsumeAnnotationToken();
  } else {
    assert(TypeConstraintSS.isEmpty() &&
           "expected type constraint after scope specifier");

    // Consume the 'class' or 'typename' keyword.
    TypenameKeyword = Tok.is(tok::kw_typename);
    KeyLoc = ConsumeToken();
  }

  // Grab the ellipsis (if given).
  SourceLocation EllipsisLoc;
  if (TryConsumeToken(tok::ellipsis, EllipsisLoc)) {
    Diag(EllipsisLoc,
         getLangOpts().CPlusPlus11
           ? diag::warn_cxx98_compat_variadic_templates
           : diag::ext_variadic_templates);
  }

  // Grab the template parameter name (if given)
  SourceLocation NameLoc = Tok.getLocation();
  IdentifierInfo *ParamName = nullptr;
  if (Tok.is(tok::identifier)) {
    ParamName = Tok.getIdentifierInfo();
    ConsumeToken();
  } else if (Tok.isOneOf(tok::equal, tok::comma, tok::greater,
                         tok::greatergreater)) {
    // Unnamed template parameter. Don't have to do anything here, just
    // don't consume this token.
  } else {
    Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
    return nullptr;
  }

  // Recover from misplaced ellipsis.
  bool AlreadyHasEllipsis = EllipsisLoc.isValid();
  if (TryConsumeToken(tok::ellipsis, EllipsisLoc))
    DiagnoseMisplacedEllipsis(EllipsisLoc, NameLoc, AlreadyHasEllipsis, true);

  // Grab a default argument (if available).
  // Per C++0x [basic.scope.pdecl]p9, we parse the default argument before
  // we introduce the type parameter into the local scope.
  SourceLocation EqualLoc;
  ParsedType DefaultArg;
  std::optional<DelayTemplateIdDestructionRAII> DontDestructTemplateIds;
  if (TryConsumeToken(tok::equal, EqualLoc)) {
    // The default argument might contain a lambda declaration; avoid destroying
    // parsed template ids at the end of that declaration because they can be
    // used in a type constraint later.
    DontDestructTemplateIds.emplace(*this, /*DelayTemplateIdDestruction=*/true);
    // The default argument may declare template parameters, notably
    // if it contains a generic lambda, so we need to increase
    // the template depth as these parameters would not be instantiated
    // at the current level.
    TemplateParameterDepthRAII CurTemplateDepthTracker(TemplateParameterDepth);
    ++CurTemplateDepthTracker;
    DefaultArg =
        ParseTypeName(/*Range=*/nullptr, DeclaratorContext::TemplateTypeArg)
            .get();
  }

  NamedDecl *NewDecl = Actions.ActOnTypeParameter(getCurScope(),
                                                  TypenameKeyword, EllipsisLoc,
                                                  KeyLoc, ParamName, NameLoc,
                                                  Depth, Position, EqualLoc,
                                                  DefaultArg,
                                                  TypeConstraint != nullptr);

  if (TypeConstraint) {
    Actions.ActOnTypeConstraint(TypeConstraintSS, TypeConstraint,
                                cast<TemplateTypeParmDecl>(NewDecl),
                                EllipsisLoc);
  }

  return NewDecl;
}

/// ParseTemplateTemplateParameter - Handle the parsing of template
/// template parameters.
///
///       type-parameter:    [C++ temp.param]
///         template-head type-parameter-key ...[opt] identifier[opt]
///         template-head type-parameter-key identifier[opt] = id-expression
///       type-parameter-key:
///         'class'
///         'typename'       [C++1z]
///       template-head:     [C++2a]
///         'template' '<' template-parameter-list '>'
///             requires-clause[opt]
NamedDecl *Parser::ParseTemplateTemplateParameter(unsigned Depth,
                                                  unsigned Position) {
  assert(Tok.is(tok::kw_template) && "Expected 'template' keyword");

  // Handle the template <...> part.
  SourceLocation TemplateLoc = ConsumeToken();
  SmallVector<NamedDecl*,8> TemplateParams;
  SourceLocation LAngleLoc, RAngleLoc;
  ExprResult OptionalRequiresClauseConstraintER;
  {
    MultiParseScope TemplateParmScope(*this);
    if (ParseTemplateParameters(TemplateParmScope, Depth + 1, TemplateParams,
                                LAngleLoc, RAngleLoc)) {
      return nullptr;
    }
    if (TryConsumeToken(tok::kw_requires)) {
      OptionalRequiresClauseConstraintER =
          Actions.ActOnRequiresClause(ParseConstraintLogicalOrExpression(
              /*IsTrailingRequiresClause=*/false));
      if (!OptionalRequiresClauseConstraintER.isUsable()) {
        SkipUntil(tok::comma, tok::greater, tok::greatergreater,
                  StopAtSemi | StopBeforeMatch);
        return nullptr;
      }
    }
  }

  // Provide an ExtWarn if the C++1z feature of using 'typename' here is used.
  // Generate a meaningful error if the user forgot to put class before the
  // identifier, comma, or greater. Provide a fixit if the identifier, comma,
  // or greater appear immediately or after 'struct'. In the latter case,
  // replace the keyword with 'class'.
  bool TypenameKeyword = false;
  if (!TryConsumeToken(tok::kw_class)) {
    bool Replace = Tok.isOneOf(tok::kw_typename, tok::kw_struct);
    const Token &Next = Tok.is(tok::kw_struct) ? NextToken() : Tok;
    if (Tok.is(tok::kw_typename)) {
      TypenameKeyword = true;
      Diag(Tok.getLocation(),
           getLangOpts().CPlusPlus17
               ? diag::warn_cxx14_compat_template_template_param_typename
               : diag::ext_template_template_param_typename)
        << (!getLangOpts().CPlusPlus17
                ? FixItHint::CreateReplacement(Tok.getLocation(), "class")
                : FixItHint());
    } else if (Next.isOneOf(tok::identifier, tok::comma, tok::greater,
                            tok::greatergreater, tok::ellipsis)) {
      Diag(Tok.getLocation(), diag::err_class_on_template_template_param)
          << getLangOpts().CPlusPlus17
          << (Replace
                  ? FixItHint::CreateReplacement(Tok.getLocation(), "class")
                  : FixItHint::CreateInsertion(Tok.getLocation(), "class "));
    } else
      Diag(Tok.getLocation(), diag::err_class_on_template_template_param)
          << getLangOpts().CPlusPlus17;

    if (Replace)
      ConsumeToken();
  }

  // Parse the ellipsis, if given.
  SourceLocation EllipsisLoc;
  if (TryConsumeToken(tok::ellipsis, EllipsisLoc))
    Diag(EllipsisLoc,
         getLangOpts().CPlusPlus11
           ? diag::warn_cxx98_compat_variadic_templates
           : diag::ext_variadic_templates);

  // Get the identifier, if given.
  SourceLocation NameLoc = Tok.getLocation();
  IdentifierInfo *ParamName = nullptr;
  if (Tok.is(tok::identifier)) {
    ParamName = Tok.getIdentifierInfo();
    ConsumeToken();
  } else if (Tok.isOneOf(tok::equal, tok::comma, tok::greater,
                         tok::greatergreater)) {
    // Unnamed template parameter. Don't have to do anything here, just
    // don't consume this token.
  } else {
    Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
    return nullptr;
  }

  // Recover from misplaced ellipsis.
  bool AlreadyHasEllipsis = EllipsisLoc.isValid();
  if (TryConsumeToken(tok::ellipsis, EllipsisLoc))
    DiagnoseMisplacedEllipsis(EllipsisLoc, NameLoc, AlreadyHasEllipsis, true);

  TemplateParameterList *ParamList = Actions.ActOnTemplateParameterList(
      Depth, SourceLocation(), TemplateLoc, LAngleLoc, TemplateParams,
      RAngleLoc, OptionalRequiresClauseConstraintER.get());

  // Grab a default argument (if available).
  // Per C++0x [basic.scope.pdecl]p9, we parse the default argument before
  // we introduce the template parameter into the local scope.
  SourceLocation EqualLoc;
  ParsedTemplateArgument DefaultArg;
  if (TryConsumeToken(tok::equal, EqualLoc)) {
    DefaultArg = ParseTemplateTemplateArgument();
    if (DefaultArg.isInvalid()) {
      Diag(Tok.getLocation(),
           diag::err_default_template_template_parameter_not_template);
      SkipUntil(tok::comma, tok::greater, tok::greatergreater,
                StopAtSemi | StopBeforeMatch);
    }
  }

  return Actions.ActOnTemplateTemplateParameter(
      getCurScope(), TemplateLoc, ParamList, TypenameKeyword, EllipsisLoc,
      ParamName, NameLoc, Depth, Position, EqualLoc, DefaultArg);
}

/// ParseNonTypeTemplateParameter - Handle the parsing of non-type
/// template parameters (e.g., in "template<int Size> class array;").
///
///       template-parameter:
///         ...
///         parameter-declaration
NamedDecl *
Parser::ParseNonTypeTemplateParameter(unsigned Depth, unsigned Position) {
  // Parse the declaration-specifiers (i.e., the type).
  // FIXME: The type should probably be restricted in some way... Not all
  // declarators (parts of declarators?) are accepted for parameters.
  DeclSpec DS(AttrFactory);
  ParsedTemplateInfo TemplateInfo;
  ParseDeclarationSpecifiers(DS, TemplateInfo, AS_none,
                             DeclSpecContext::DSC_template_param);

  // Parse this as a typename.
  Declarator ParamDecl(DS, ParsedAttributesView::none(),
                       DeclaratorContext::TemplateParam);
  ParseDeclarator(ParamDecl);
  if (DS.getTypeSpecType() == DeclSpec::TST_unspecified) {
    Diag(Tok.getLocation(), diag::err_expected_template_parameter);
    return nullptr;
  }

  // Recover from misplaced ellipsis.
  SourceLocation EllipsisLoc;
  if (TryConsumeToken(tok::ellipsis, EllipsisLoc))
    DiagnoseMisplacedEllipsisInDeclarator(EllipsisLoc, ParamDecl);

  // If there is a default value, parse it.
  // Per C++0x [basic.scope.pdecl]p9, we parse the default argument before
  // we introduce the template parameter into the local scope.
  SourceLocation EqualLoc;
  ExprResult DefaultArg;
  if (TryConsumeToken(tok::equal, EqualLoc)) {
    if (Tok.is(tok::l_paren) && NextToken().is(tok::l_brace)) {
      Diag(Tok.getLocation(), diag::err_stmt_expr_in_default_arg) << 1;
      SkipUntil(tok::comma, tok::greater, StopAtSemi | StopBeforeMatch);
    } else {
      // C++ [temp.param]p15:
      //   When parsing a default template-argument for a non-type
      //   template-parameter, the first non-nested > is taken as the
      //   end of the template-parameter-list rather than a greater-than
      //   operator.
      GreaterThanIsOperatorScope G(GreaterThanIsOperator, false);

      // The default argument may declare template parameters, notably
      // if it contains a generic lambda, so we need to increase
      // the template depth as these parameters would not be instantiated
      // at the current level.
      TemplateParameterDepthRAII CurTemplateDepthTracker(
          TemplateParameterDepth);
      ++CurTemplateDepthTracker;
      EnterExpressionEvaluationContext ConstantEvaluated(
          Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);
      DefaultArg = Actions.CorrectDelayedTyposInExpr(ParseInitializer());
      if (DefaultArg.isInvalid())
        SkipUntil(tok::comma, tok::greater, StopAtSemi | StopBeforeMatch);
    }
  }

  // Create the parameter.
  return Actions.ActOnNonTypeTemplateParameter(getCurScope(), ParamDecl,
                                               Depth, Position, EqualLoc,
                                               DefaultArg.get());
}

void Parser::DiagnoseMisplacedEllipsis(SourceLocation EllipsisLoc,
                                       SourceLocation CorrectLoc,
                                       bool AlreadyHasEllipsis,
                                       bool IdentifierHasName) {
  FixItHint Insertion;
  if (!AlreadyHasEllipsis)
    Insertion = FixItHint::CreateInsertion(CorrectLoc, "...");
  Diag(EllipsisLoc, diag::err_misplaced_ellipsis_in_declaration)
      << FixItHint::CreateRemoval(EllipsisLoc) << Insertion
      << !IdentifierHasName;
}

void Parser::DiagnoseMisplacedEllipsisInDeclarator(SourceLocation EllipsisLoc,
                                                   Declarator &D) {
  assert(EllipsisLoc.isValid());
  bool AlreadyHasEllipsis = D.getEllipsisLoc().isValid();
  if (!AlreadyHasEllipsis)
    D.setEllipsisLoc(EllipsisLoc);
  DiagnoseMisplacedEllipsis(EllipsisLoc, D.getIdentifierLoc(),
                            AlreadyHasEllipsis, D.hasName());
}

/// Parses a '>' at the end of a template list.
///
/// If this function encounters '>>', '>>>', '>=', or '>>=', it tries
/// to determine if these tokens were supposed to be a '>' followed by
/// '>', '>>', '>=', or '>='. It emits an appropriate diagnostic if necessary.
///
/// \param RAngleLoc the location of the consumed '>'.
///
/// \param ConsumeLastToken if true, the '>' is consumed.
///
/// \param ObjCGenericList if true, this is the '>' closing an Objective-C
/// type parameter or type argument list, rather than a C++ template parameter
/// or argument list.
///
/// \returns true, if current token does not start with '>', false otherwise.
bool Parser::ParseGreaterThanInTemplateList(SourceLocation LAngleLoc,
                                            SourceLocation &RAngleLoc,
                                            bool ConsumeLastToken,
                                            bool ObjCGenericList) {
  // What will be left once we've consumed the '>'.
  tok::TokenKind RemainingToken;
  const char *ReplacementStr = "> >";
  bool MergeWithNextToken = false;

  switch (Tok.getKind()) {
  default:
    Diag(getEndOfPreviousToken(), diag::err_expected) << tok::greater;
    Diag(LAngleLoc, diag::note_matching) << tok::less;
    return true;

  case tok::greater:
    // Determine the location of the '>' token. Only consume this token
    // if the caller asked us to.
    RAngleLoc = Tok.getLocation();
    if (ConsumeLastToken)
      ConsumeToken();
    return false;

  case tok::greatergreater:
    RemainingToken = tok::greater;
    break;

  case tok::greatergreatergreater:
    RemainingToken = tok::greatergreater;
    break;

  case tok::greaterequal:
    RemainingToken = tok::equal;
    ReplacementStr = "> =";

    // Join two adjacent '=' tokens into one, for cases like:
    //   void (*p)() = f<int>;
    //   return f<int>==p;
    if (NextToken().is(tok::equal) &&
        areTokensAdjacent(Tok, NextToken())) {
      RemainingToken = tok::equalequal;
      MergeWithNextToken = true;
    }
    break;

  case tok::greatergreaterequal:
    RemainingToken = tok::greaterequal;
    break;
  }

  // This template-id is terminated by a token that starts with a '>'.
  // Outside C++11 and Objective-C, this is now error recovery.
  //
  // C++11 allows this when the token is '>>', and in CUDA + C++11 mode, we
  // extend that treatment to also apply to the '>>>' token.
  //
  // Objective-C allows this in its type parameter / argument lists.

  SourceLocation TokBeforeGreaterLoc = PrevTokLocation;
  SourceLocation TokLoc = Tok.getLocation();
  Token Next = NextToken();

  // Whether splitting the current token after the '>' would undesirably result
  // in the remaining token pasting with the token after it. This excludes the
  // MergeWithNextToken cases, which we've already handled.
  bool PreventMergeWithNextToken =
      (RemainingToken == tok::greater ||
       RemainingToken == tok::greatergreater) &&
      (Next.isOneOf(tok::greater, tok::greatergreater,
                    tok::greatergreatergreater, tok::equal, tok::greaterequal,
                    tok::greatergreaterequal, tok::equalequal)) &&
      areTokensAdjacent(Tok, Next);

  // Diagnose this situation as appropriate.
  if (!ObjCGenericList) {
    // The source range of the replaced token(s).
    CharSourceRange ReplacementRange = CharSourceRange::getCharRange(
        TokLoc, Lexer::AdvanceToTokenCharacter(TokLoc, 2, PP.getSourceManager(),
                                               getLangOpts()));

    // A hint to put a space between the '>>'s. In order to make the hint as
    // clear as possible, we include the characters either side of the space in
    // the replacement, rather than just inserting a space at SecondCharLoc.
    FixItHint Hint1 = FixItHint::CreateReplacement(ReplacementRange,
                                                   ReplacementStr);

    // A hint to put another space after the token, if it would otherwise be
    // lexed differently.
    FixItHint Hint2;
    if (PreventMergeWithNextToken)
      Hint2 = FixItHint::CreateInsertion(Next.getLocation(), " ");

    unsigned DiagId = diag::err_two_right_angle_brackets_need_space;
    if (getLangOpts().CPlusPlus11 &&
        (Tok.is(tok::greatergreater) || Tok.is(tok::greatergreatergreater)))
      DiagId = diag::warn_cxx98_compat_two_right_angle_brackets;
    else if (Tok.is(tok::greaterequal))
      DiagId = diag::err_right_angle_bracket_equal_needs_space;
    Diag(TokLoc, DiagId) << Hint1 << Hint2;
  }

  // Find the "length" of the resulting '>' token. This is not always 1, as it
  // can contain escaped newlines.
  unsigned GreaterLength = Lexer::getTokenPrefixLength(
      TokLoc, 1, PP.getSourceManager(), getLangOpts());

  // Annotate the source buffer to indicate that we split the token after the
  // '>'. This allows us to properly find the end of, and extract the spelling
  // of, the '>' token later.
  RAngleLoc = PP.SplitToken(TokLoc, GreaterLength);

  // Strip the initial '>' from the token.
  bool CachingTokens = PP.IsPreviousCachedToken(Tok);

  Token Greater = Tok;
  Greater.setLocation(RAngleLoc);
  Greater.setKind(tok::greater);
  Greater.setLength(GreaterLength);

  unsigned OldLength = Tok.getLength();
  if (MergeWithNextToken) {
    ConsumeToken();
    OldLength += Tok.getLength();
  }

  Tok.setKind(RemainingToken);
  Tok.setLength(OldLength - GreaterLength);

  // Split the second token if lexing it normally would lex a different token
  // (eg, the fifth token in 'A<B>>>' should re-lex as '>', not '>>').
  SourceLocation AfterGreaterLoc = TokLoc.getLocWithOffset(GreaterLength);
  if (PreventMergeWithNextToken)
    AfterGreaterLoc = PP.SplitToken(AfterGreaterLoc, Tok.getLength());
  Tok.setLocation(AfterGreaterLoc);

  // Update the token cache to match what we just did if necessary.
  if (CachingTokens) {
    // If the previous cached token is being merged, delete it.
    if (MergeWithNextToken)
      PP.ReplacePreviousCachedToken({});

    if (ConsumeLastToken)
      PP.ReplacePreviousCachedToken({Greater, Tok});
    else
      PP.ReplacePreviousCachedToken({Greater});
  }

  if (ConsumeLastToken) {
    PrevTokLocation = RAngleLoc;
  } else {
    PrevTokLocation = TokBeforeGreaterLoc;
    PP.EnterToken(Tok, /*IsReinject=*/true);
    Tok = Greater;
  }

  return false;
}

/// Parses a template-id that after the template name has
/// already been parsed.
///
/// This routine takes care of parsing the enclosed template argument
/// list ('<' template-parameter-list [opt] '>') and placing the
/// results into a form that can be transferred to semantic analysis.
///
/// \param ConsumeLastToken if true, then we will consume the last
/// token that forms the template-id. Otherwise, we will leave the
/// last token in the stream (e.g., so that it can be replaced with an
/// annotation token).
bool Parser::ParseTemplateIdAfterTemplateName(bool ConsumeLastToken,
                                              SourceLocation &LAngleLoc,
                                              TemplateArgList &TemplateArgs,
                                              SourceLocation &RAngleLoc,
                                              TemplateTy Template) {
  assert(Tok.is(tok::less) && "Must have already parsed the template-name");

  // Consume the '<'.
  LAngleLoc = ConsumeToken();

  // Parse the optional template-argument-list.
  bool Invalid = false;
  {
    GreaterThanIsOperatorScope G(GreaterThanIsOperator, false);
    if (!Tok.isOneOf(tok::greater, tok::greatergreater,
                     tok::greatergreatergreater, tok::greaterequal,
                     tok::greatergreaterequal))
      Invalid = ParseTemplateArgumentList(TemplateArgs, Template, LAngleLoc);

    if (Invalid) {
      // Try to find the closing '>'.
      if (getLangOpts().CPlusPlus11)
        SkipUntil(tok::greater, tok::greatergreater,
                  tok::greatergreatergreater, StopAtSemi | StopBeforeMatch);
      else
        SkipUntil(tok::greater, StopAtSemi | StopBeforeMatch);
    }
  }

  return ParseGreaterThanInTemplateList(LAngleLoc, RAngleLoc, ConsumeLastToken,
                                        /*ObjCGenericList=*/false) ||
         Invalid;
}

/// Replace the tokens that form a simple-template-id with an
/// annotation token containing the complete template-id.
///
/// The first token in the stream must be the name of a template that
/// is followed by a '<'. This routine will parse the complete
/// simple-template-id and replace the tokens with a single annotation
/// token with one of two different kinds: if the template-id names a
/// type (and \p AllowTypeAnnotation is true), the annotation token is
/// a type annotation that includes the optional nested-name-specifier
/// (\p SS). Otherwise, the annotation token is a template-id
/// annotation that does not include the optional
/// nested-name-specifier.
///
/// \param Template  the declaration of the template named by the first
/// token (an identifier), as returned from \c Action::isTemplateName().
///
/// \param TNK the kind of template that \p Template
/// refers to, as returned from \c Action::isTemplateName().
///
/// \param SS if non-NULL, the nested-name-specifier that precedes
/// this template name.
///
/// \param TemplateKWLoc if valid, specifies that this template-id
/// annotation was preceded by the 'template' keyword and gives the
/// location of that keyword. If invalid (the default), then this
/// template-id was not preceded by a 'template' keyword.
///
/// \param AllowTypeAnnotation if true (the default), then a
/// simple-template-id that refers to a class template, template
/// template parameter, or other template that produces a type will be
/// replaced with a type annotation token. Otherwise, the
/// simple-template-id is always replaced with a template-id
/// annotation token.
///
/// \param TypeConstraint if true, then this is actually a type-constraint,
/// meaning that the template argument list can be omitted (and the template in
/// question must be a concept).
///
/// If an unrecoverable parse error occurs and no annotation token can be
/// formed, this function returns true.
///
bool Parser::AnnotateTemplateIdToken(TemplateTy Template, TemplateNameKind TNK,
                                     CXXScopeSpec &SS,
                                     SourceLocation TemplateKWLoc,
                                     UnqualifiedId &TemplateName,
                                     bool AllowTypeAnnotation,
                                     bool TypeConstraint) {
  assert(getLangOpts().CPlusPlus && "Can only annotate template-ids in C++");
  assert((Tok.is(tok::less) || TypeConstraint) &&
         "Parser isn't at the beginning of a template-id");
  assert(!(TypeConstraint && AllowTypeAnnotation) && "type-constraint can't be "
                                                     "a type annotation");
  assert((!TypeConstraint || TNK == TNK_Concept_template) && "type-constraint "
         "must accompany a concept name");
  assert((Template || TNK == TNK_Non_template) && "missing template name");

  // Consume the template-name.
  SourceLocation TemplateNameLoc = TemplateName.getSourceRange().getBegin();

  // Parse the enclosed template argument list.
  SourceLocation LAngleLoc, RAngleLoc;
  TemplateArgList TemplateArgs;
  bool ArgsInvalid = false;
  if (!TypeConstraint || Tok.is(tok::less)) {
    ArgsInvalid = ParseTemplateIdAfterTemplateName(
        false, LAngleLoc, TemplateArgs, RAngleLoc, Template);
    // If we couldn't recover from invalid arguments, don't form an annotation
    // token -- we don't know how much to annotate.
    // FIXME: This can lead to duplicate diagnostics if we retry parsing this
    // template-id in another context. Try to annotate anyway?
    if (RAngleLoc.isInvalid())
      return true;
  }

  ASTTemplateArgsPtr TemplateArgsPtr(TemplateArgs);

  // Build the annotation token.
  if (TNK == TNK_Type_template && AllowTypeAnnotation) {
    TypeResult Type = ArgsInvalid
                          ? TypeError()
                          : Actions.ActOnTemplateIdType(
                                getCurScope(), SS, TemplateKWLoc, Template,
                                TemplateName.Identifier, TemplateNameLoc,
                                LAngleLoc, TemplateArgsPtr, RAngleLoc);

    Tok.setKind(tok::annot_typename);
    setTypeAnnotation(Tok, Type);
    if (SS.isNotEmpty())
      Tok.setLocation(SS.getBeginLoc());
    else if (TemplateKWLoc.isValid())
      Tok.setLocation(TemplateKWLoc);
    else
      Tok.setLocation(TemplateNameLoc);
  } else {
    // Build a template-id annotation token that can be processed
    // later.
    Tok.setKind(tok::annot_template_id);

    const IdentifierInfo *TemplateII =
        TemplateName.getKind() == UnqualifiedIdKind::IK_Identifier
            ? TemplateName.Identifier
            : nullptr;

    OverloadedOperatorKind OpKind =
        TemplateName.getKind() == UnqualifiedIdKind::IK_Identifier
            ? OO_None
            : TemplateName.OperatorFunctionId.Operator;

    TemplateIdAnnotation *TemplateId = TemplateIdAnnotation::Create(
        TemplateKWLoc, TemplateNameLoc, TemplateII, OpKind, Template, TNK,
        LAngleLoc, RAngleLoc, TemplateArgs, ArgsInvalid, TemplateIds);

    Tok.setAnnotationValue(TemplateId);
    if (TemplateKWLoc.isValid())
      Tok.setLocation(TemplateKWLoc);
    else
      Tok.setLocation(TemplateNameLoc);
  }

  // Common fields for the annotation token
  Tok.setAnnotationEndLoc(RAngleLoc);

  // In case the tokens were cached, have Preprocessor replace them with the
  // annotation token.
  PP.AnnotateCachedTokens(Tok);
  return false;
}

/// Replaces a template-id annotation token with a type
/// annotation token.
///
/// If there was a failure when forming the type from the template-id,
/// a type annotation token will still be created, but will have a
/// NULL type pointer to signify an error.
///
/// \param SS The scope specifier appearing before the template-id, if any.
///
/// \param AllowImplicitTypename whether this is a context where T::type
/// denotes a dependent type.
/// \param IsClassName Is this template-id appearing in a context where we
/// know it names a class, such as in an elaborated-type-specifier or
/// base-specifier? ('typename' and 'template' are unneeded and disallowed
/// in those contexts.)
void Parser::AnnotateTemplateIdTokenAsType(
    CXXScopeSpec &SS, ImplicitTypenameContext AllowImplicitTypename,
    bool IsClassName) {
  assert(Tok.is(tok::annot_template_id) && "Requires template-id tokens");

  TemplateIdAnnotation *TemplateId = takeTemplateIdAnnotation(Tok);
  assert(TemplateId->mightBeType() &&
         "Only works for type and dependent templates");

  ASTTemplateArgsPtr TemplateArgsPtr(TemplateId->getTemplateArgs(),
                                     TemplateId->NumArgs);

  TypeResult Type =
      TemplateId->isInvalid()
          ? TypeError()
          : Actions.ActOnTemplateIdType(
                getCurScope(), SS, TemplateId->TemplateKWLoc,
                TemplateId->Template, TemplateId->Name,
                TemplateId->TemplateNameLoc, TemplateId->LAngleLoc,
                TemplateArgsPtr, TemplateId->RAngleLoc,
                /*IsCtorOrDtorName=*/false, IsClassName, AllowImplicitTypename);
  // Create the new "type" annotation token.
  Tok.setKind(tok::annot_typename);
  setTypeAnnotation(Tok, Type);
  if (SS.isNotEmpty()) // it was a C++ qualified type name.
    Tok.setLocation(SS.getBeginLoc());
  // End location stays the same

  // Replace the template-id annotation token, and possible the scope-specifier
  // that precedes it, with the typename annotation token.
  PP.AnnotateCachedTokens(Tok);
}

/// Determine whether the given token can end a template argument.
static bool isEndOfTemplateArgument(Token Tok) {
  // FIXME: Handle '>>>'.
  return Tok.isOneOf(tok::comma, tok::greater, tok::greatergreater,
                     tok::greatergreatergreater);
}

/// Parse a C++ template template argument.
ParsedTemplateArgument Parser::ParseTemplateTemplateArgument() {
  if (!Tok.is(tok::identifier) && !Tok.is(tok::coloncolon) &&
      !Tok.is(tok::annot_cxxscope))
    return ParsedTemplateArgument();

  // C++0x [temp.arg.template]p1:
  //   A template-argument for a template template-parameter shall be the name
  //   of a class template or an alias template, expressed as id-expression.
  //
  // We parse an id-expression that refers to a class template or alias
  // template. The grammar we parse is:
  //
  //   nested-name-specifier[opt] template[opt] identifier ...[opt]
  //
  // followed by a token that terminates a template argument, such as ',',
  // '>', or (in some cases) '>>'.
  CXXScopeSpec SS; // nested-name-specifier, if present
  ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/nullptr,
                                 /*ObjectHasErrors=*/false,
                                 /*EnteringContext=*/false);

  ParsedTemplateArgument Result;
  SourceLocation EllipsisLoc;
  if (SS.isSet() && Tok.is(tok::kw_template)) {
    // Parse the optional 'template' keyword following the
    // nested-name-specifier.
    SourceLocation TemplateKWLoc = ConsumeToken();

    if (Tok.is(tok::identifier)) {
      // We appear to have a dependent template name.
      UnqualifiedId Name;
      Name.setIdentifier(Tok.getIdentifierInfo(), Tok.getLocation());
      ConsumeToken(); // the identifier

      TryConsumeToken(tok::ellipsis, EllipsisLoc);

      // If the next token signals the end of a template argument, then we have
      // a (possibly-dependent) template name that could be a template template
      // argument.
      TemplateTy Template;
      if (isEndOfTemplateArgument(Tok) &&
          Actions.ActOnTemplateName(getCurScope(), SS, TemplateKWLoc, Name,
                                    /*ObjectType=*/nullptr,
                                    /*EnteringContext=*/false, Template))
        Result = ParsedTemplateArgument(SS, Template, Name.StartLocation);
    }
  } else if (Tok.is(tok::identifier)) {
    // We may have a (non-dependent) template name.
    TemplateTy Template;
    UnqualifiedId Name;
    Name.setIdentifier(Tok.getIdentifierInfo(), Tok.getLocation());
    ConsumeToken(); // the identifier

    TryConsumeToken(tok::ellipsis, EllipsisLoc);

    if (isEndOfTemplateArgument(Tok)) {
      bool MemberOfUnknownSpecialization;
      TemplateNameKind TNK = Actions.isTemplateName(
          getCurScope(), SS,
          /*hasTemplateKeyword=*/false, Name,
          /*ObjectType=*/nullptr,
          /*EnteringContext=*/false, Template, MemberOfUnknownSpecialization);
      if (TNK == TNK_Dependent_template_name || TNK == TNK_Type_template) {
        // We have an id-expression that refers to a class template or
        // (C++0x) alias template.
        Result = ParsedTemplateArgument(SS, Template, Name.StartLocation);
      }
    }
  }

  // If this is a pack expansion, build it as such.
  if (EllipsisLoc.isValid() && !Result.isInvalid())
    Result = Actions.ActOnPackExpansion(Result, EllipsisLoc);

  return Result;
}

/// ParseTemplateArgument - Parse a C++ template argument (C++ [temp.names]).
///
///       template-argument: [C++ 14.2]
///         constant-expression
///         type-id
///         id-expression
///         braced-init-list  [C++26, DR]
///
ParsedTemplateArgument Parser::ParseTemplateArgument() {
  // C++ [temp.arg]p2:
  //   In a template-argument, an ambiguity between a type-id and an
  //   expression is resolved to a type-id, regardless of the form of
  //   the corresponding template-parameter.
  //
  // Therefore, we initially try to parse a type-id - and isCXXTypeId might look
  // up and annotate an identifier as an id-expression during disambiguation,
  // so enter the appropriate context for a constant expression template
  // argument before trying to disambiguate.

  EnterExpressionEvaluationContext EnterConstantEvaluated(
    Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated,
    /*LambdaContextDecl=*/nullptr,
    /*ExprContext=*/Sema::ExpressionEvaluationContextRecord::EK_TemplateArgument);
  if (isCXXTypeId(TypeIdAsTemplateArgument)) {
    TypeResult TypeArg = ParseTypeName(
        /*Range=*/nullptr, DeclaratorContext::TemplateArg);
    return Actions.ActOnTemplateTypeArgument(TypeArg);
  }

  // Try to parse a template template argument.
  {
    TentativeParsingAction TPA(*this);

    ParsedTemplateArgument TemplateTemplateArgument
      = ParseTemplateTemplateArgument();
    if (!TemplateTemplateArgument.isInvalid()) {
      TPA.Commit();
      return TemplateTemplateArgument;
    }

    // Revert this tentative parse to parse a non-type template argument.
    TPA.Revert();
  }

  // Parse a non-type template argument.
  ExprResult ExprArg;
  SourceLocation Loc = Tok.getLocation();
  if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace))
    ExprArg = ParseBraceInitializer();
  else
    ExprArg = ParseConstantExpressionInExprEvalContext(MaybeTypeCast);
  if (ExprArg.isInvalid() || !ExprArg.get()) {
    return ParsedTemplateArgument();
  }

  return ParsedTemplateArgument(ParsedTemplateArgument::NonType,
                                ExprArg.get(), Loc);
}

/// ParseTemplateArgumentList - Parse a C++ template-argument-list
/// (C++ [temp.names]). Returns true if there was an error.
///
///       template-argument-list: [C++ 14.2]
///         template-argument
///         template-argument-list ',' template-argument
///
/// \param Template is only used for code completion, and may be null.
bool Parser::ParseTemplateArgumentList(TemplateArgList &TemplateArgs,
                                       TemplateTy Template,
                                       SourceLocation OpenLoc) {

  ColonProtectionRAIIObject ColonProtection(*this, false);

  auto RunSignatureHelp = [&] {
    if (!Template)
      return QualType();
    CalledSignatureHelp = true;
    return Actions.CodeCompletion().ProduceTemplateArgumentSignatureHelp(
        Template, TemplateArgs, OpenLoc);
  };

  do {
    PreferredType.enterFunctionArgument(Tok.getLocation(), RunSignatureHelp);
    ParsedTemplateArgument Arg = ParseTemplateArgument();
    SourceLocation EllipsisLoc;
    if (TryConsumeToken(tok::ellipsis, EllipsisLoc))
      Arg = Actions.ActOnPackExpansion(Arg, EllipsisLoc);

    if (Arg.isInvalid()) {
      if (PP.isCodeCompletionReached() && !CalledSignatureHelp)
        RunSignatureHelp();
      return true;
    }

    // Save this template argument.
    TemplateArgs.push_back(Arg);

    // If the next token is a comma, consume it and keep reading
    // arguments.
  } while (TryConsumeToken(tok::comma));

  return false;
}

/// Parse a C++ explicit template instantiation
/// (C++ [temp.explicit]).
///
///       explicit-instantiation:
///         'extern' [opt] 'template' declaration
///
/// Note that the 'extern' is a GNU extension and C++11 feature.
Parser::DeclGroupPtrTy Parser::ParseExplicitInstantiation(
    DeclaratorContext Context, SourceLocation ExternLoc,
    SourceLocation TemplateLoc, SourceLocation &DeclEnd,
    ParsedAttributes &AccessAttrs, AccessSpecifier AS) {
  // This isn't really required here.
  ParsingDeclRAIIObject
    ParsingTemplateParams(*this, ParsingDeclRAIIObject::NoParent);
  ParsedTemplateInfo TemplateInfo(ExternLoc, TemplateLoc);
  return ParseDeclarationAfterTemplate(
      Context, TemplateInfo, ParsingTemplateParams, DeclEnd, AccessAttrs, AS);
}

SourceRange Parser::ParsedTemplateInfo::getSourceRange() const {
  if (TemplateParams)
    return getTemplateParamsRange(TemplateParams->data(),
                                  TemplateParams->size());

  SourceRange R(TemplateLoc);
  if (ExternLoc.isValid())
    R.setBegin(ExternLoc);
  return R;
}

void Parser::LateTemplateParserCallback(void *P, LateParsedTemplate &LPT) {
  ((Parser *)P)->ParseLateTemplatedFuncDef(LPT);
}

/// Late parse a C++ function template in Microsoft mode.
void Parser::ParseLateTemplatedFuncDef(LateParsedTemplate &LPT) {
  if (!LPT.D)
     return;

  // Destroy TemplateIdAnnotations when we're done, if possible.
  DestroyTemplateIdAnnotationsRAIIObj CleanupRAII(*this);

  // Get the FunctionDecl.
  FunctionDecl *FunD = LPT.D->getAsFunction();
  // Track template parameter depth.
  TemplateParameterDepthRAII CurTemplateDepthTracker(TemplateParameterDepth);

  // To restore the context after late parsing.
  Sema::ContextRAII GlobalSavedContext(
      Actions, Actions.Context.getTranslationUnitDecl());

  MultiParseScope Scopes(*this);

  // Get the list of DeclContexts to reenter.
  SmallVector<DeclContext*, 4> DeclContextsToReenter;
  for (DeclContext *DC = FunD; DC && !DC->isTranslationUnit();
       DC = DC->getLexicalParent())
    DeclContextsToReenter.push_back(DC);

  // Reenter scopes from outermost to innermost.
  for (DeclContext *DC : reverse(DeclContextsToReenter)) {
    CurTemplateDepthTracker.addDepth(
        ReenterTemplateScopes(Scopes, cast<Decl>(DC)));
    Scopes.Enter(Scope::DeclScope);
    // We'll reenter the function context itself below.
    if (DC != FunD)
      Actions.PushDeclContext(Actions.getCurScope(), DC);
  }

  // Parsing should occur with empty FP pragma stack and FP options used in the
  // point of the template definition.
  Sema::FpPragmaStackSaveRAII SavedStack(Actions);
  Actions.resetFPOptions(LPT.FPO);

  assert(!LPT.Toks.empty() && "Empty body!");

  // Append the current token at the end of the new token stream so that it
  // doesn't get lost.
  LPT.Toks.push_back(Tok);
  PP.EnterTokenStream(LPT.Toks, true, /*IsReinject*/true);

  // Consume the previously pushed token.
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);
  assert(Tok.isOneOf(tok::l_brace, tok::colon, tok::kw_try) &&
         "Inline method not starting with '{', ':' or 'try'");

  // Parse the method body. Function body parsing code is similar enough
  // to be re-used for method bodies as well.
  ParseScope FnScope(this, Scope::FnScope | Scope::DeclScope |
                               Scope::CompoundStmtScope);

  // Recreate the containing function DeclContext.
  Sema::ContextRAII FunctionSavedContext(Actions, FunD->getLexicalParent());

  Actions.ActOnStartOfFunctionDef(getCurScope(), FunD);

  if (Tok.is(tok::kw_try)) {
    ParseFunctionTryBlock(LPT.D, FnScope);
  } else {
    if (Tok.is(tok::colon))
      ParseConstructorInitializer(LPT.D);
    else
      Actions.ActOnDefaultCtorInitializers(LPT.D);

    if (Tok.is(tok::l_brace)) {
      assert((!isa<FunctionTemplateDecl>(LPT.D) ||
              cast<FunctionTemplateDecl>(LPT.D)
                      ->getTemplateParameters()
                      ->getDepth() == TemplateParameterDepth - 1) &&
             "TemplateParameterDepth should be greater than the depth of "
             "current template being instantiated!");
      ParseFunctionStatementBody(LPT.D, FnScope);
      Actions.UnmarkAsLateParsedTemplate(FunD);
    } else
      Actions.ActOnFinishFunctionBody(LPT.D, nullptr);
  }
}

/// Lex a delayed template function for late parsing.
void Parser::LexTemplateFunctionForLateParsing(CachedTokens &Toks) {
  tok::TokenKind kind = Tok.getKind();
  if (!ConsumeAndStoreFunctionPrologue(Toks)) {
    // Consume everything up to (and including) the matching right brace.
    ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/false);
  }

  // If we're in a function-try-block, we need to store all the catch blocks.
  if (kind == tok::kw_try) {
    while (Tok.is(tok::kw_catch)) {
      ConsumeAndStoreUntil(tok::l_brace, Toks, /*StopAtSemi=*/false);
      ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/false);
    }
  }
}

/// We've parsed something that could plausibly be intended to be a template
/// name (\p LHS) followed by a '<' token, and the following code can't possibly
/// be an expression. Determine if this is likely to be a template-id and if so,
/// diagnose it.
bool Parser::diagnoseUnknownTemplateId(ExprResult LHS, SourceLocation Less) {
  TentativeParsingAction TPA(*this);
  // FIXME: We could look at the token sequence in a lot more detail here.
  if (SkipUntil(tok::greater, tok::greatergreater, tok::greatergreatergreater,
                StopAtSemi | StopBeforeMatch)) {
    TPA.Commit();

    SourceLocation Greater;
    ParseGreaterThanInTemplateList(Less, Greater, true, false);
    Actions.diagnoseExprIntendedAsTemplateName(getCurScope(), LHS,
                                               Less, Greater);
    return true;
  }

  // There's no matching '>' token, this probably isn't supposed to be
  // interpreted as a template-id. Parse it as an (ill-formed) comparison.
  TPA.Revert();
  return false;
}

void Parser::checkPotentialAngleBracket(ExprResult &PotentialTemplateName) {
  assert(Tok.is(tok::less) && "not at a potential angle bracket");

  bool DependentTemplateName = false;
  if (!Actions.mightBeIntendedToBeTemplateName(PotentialTemplateName,
                                               DependentTemplateName))
    return;

  // OK, this might be a name that the user intended to be parsed as a
  // template-name, followed by a '<' token. Check for some easy cases.

  // If we have potential_template<>, then it's supposed to be a template-name.
  if (NextToken().is(tok::greater) ||
      (getLangOpts().CPlusPlus11 &&
       NextToken().isOneOf(tok::greatergreater, tok::greatergreatergreater))) {
    SourceLocation Less = ConsumeToken();
    SourceLocation Greater;
    ParseGreaterThanInTemplateList(Less, Greater, true, false);
    Actions.diagnoseExprIntendedAsTemplateName(
        getCurScope(), PotentialTemplateName, Less, Greater);
    // FIXME: Perform error recovery.
    PotentialTemplateName = ExprError();
    return;
  }

  // If we have 'potential_template<type-id', assume it's supposed to be a
  // template-name if there's a matching '>' later on.
  {
    // FIXME: Avoid the tentative parse when NextToken() can't begin a type.
    TentativeParsingAction TPA(*this);
    SourceLocation Less = ConsumeToken();
    if (isTypeIdUnambiguously() &&
        diagnoseUnknownTemplateId(PotentialTemplateName, Less)) {
      TPA.Commit();
      // FIXME: Perform error recovery.
      PotentialTemplateName = ExprError();
      return;
    }
    TPA.Revert();
  }

  // Otherwise, remember that we saw this in case we see a potentially-matching
  // '>' token later on.
  AngleBracketTracker::Priority Priority =
      (DependentTemplateName ? AngleBracketTracker::DependentName
                             : AngleBracketTracker::PotentialTypo) |
      (Tok.hasLeadingSpace() ? AngleBracketTracker::SpaceBeforeLess
                             : AngleBracketTracker::NoSpaceBeforeLess);
  AngleBrackets.add(*this, PotentialTemplateName.get(), Tok.getLocation(),
                    Priority);
}

bool Parser::checkPotentialAngleBracketDelimiter(
    const AngleBracketTracker::Loc &LAngle, const Token &OpToken) {
  // If a comma in an expression context is followed by a type that can be a
  // template argument and cannot be an expression, then this is ill-formed,
  // but might be intended to be part of a template-id.
  if (OpToken.is(tok::comma) && isTypeIdUnambiguously() &&
      diagnoseUnknownTemplateId(LAngle.TemplateName, LAngle.LessLoc)) {
    AngleBrackets.clear(*this);
    return true;
  }

  // If a context that looks like a template-id is followed by '()', then
  // this is ill-formed, but might be intended to be a template-id
  // followed by '()'.
  if (OpToken.is(tok::greater) && Tok.is(tok::l_paren) &&
      NextToken().is(tok::r_paren)) {
    Actions.diagnoseExprIntendedAsTemplateName(
        getCurScope(), LAngle.TemplateName, LAngle.LessLoc,
        OpToken.getLocation());
    AngleBrackets.clear(*this);
    return true;
  }

  // After a '>' (etc), we're no longer potentially in a construct that's
  // intended to be treated as a template-id.
  if (OpToken.is(tok::greater) ||
      (getLangOpts().CPlusPlus11 &&
       OpToken.isOneOf(tok::greatergreater, tok::greatergreatergreater)))
    AngleBrackets.clear(*this);
  return false;
}
