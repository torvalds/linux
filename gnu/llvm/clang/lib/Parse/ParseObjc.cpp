//===--- ParseObjC.cpp - Objective C Parsing ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Objective-C portions of the Parser interface.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/ODRDiagsEmitter.h"
#include "clang/AST/PrettyDeclStackTrace.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaCodeCompletion.h"
#include "clang/Sema/SemaObjC.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"

using namespace clang;

/// Skips attributes after an Objective-C @ directive. Emits a diagnostic.
void Parser::MaybeSkipAttributes(tok::ObjCKeywordKind Kind) {
  ParsedAttributes attrs(AttrFactory);
  if (Tok.is(tok::kw___attribute)) {
    if (Kind == tok::objc_interface || Kind == tok::objc_protocol)
      Diag(Tok, diag::err_objc_postfix_attribute_hint)
          << (Kind == tok::objc_protocol);
    else
      Diag(Tok, diag::err_objc_postfix_attribute);
    ParseGNUAttributes(attrs);
  }
}

/// ParseObjCAtDirectives - Handle parts of the external-declaration production:
///       external-declaration: [C99 6.9]
/// [OBJC]  objc-class-definition
/// [OBJC]  objc-class-declaration
/// [OBJC]  objc-alias-declaration
/// [OBJC]  objc-protocol-definition
/// [OBJC]  objc-method-definition
/// [OBJC]  '@' 'end'
Parser::DeclGroupPtrTy
Parser::ParseObjCAtDirectives(ParsedAttributes &DeclAttrs,
                              ParsedAttributes &DeclSpecAttrs) {
  DeclAttrs.takeAllFrom(DeclSpecAttrs);

  SourceLocation AtLoc = ConsumeToken(); // the "@"

  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteObjCAtDirective(getCurScope());
    return nullptr;
  }

  switch (Tok.getObjCKeywordID()) {
  case tok::objc_interface:
  case tok::objc_protocol:
  case tok::objc_implementation:
    break;
  default:
    for (const auto &Attr : DeclAttrs) {
      if (Attr.isGNUAttribute())
        Diag(Tok.getLocation(), diag::err_objc_unexpected_attr);
    }
  }

  Decl *SingleDecl = nullptr;
  switch (Tok.getObjCKeywordID()) {
  case tok::objc_class:
    return ParseObjCAtClassDeclaration(AtLoc);
  case tok::objc_interface:
    SingleDecl = ParseObjCAtInterfaceDeclaration(AtLoc, DeclAttrs);
    break;
  case tok::objc_protocol:
    return ParseObjCAtProtocolDeclaration(AtLoc, DeclAttrs);
  case tok::objc_implementation:
    return ParseObjCAtImplementationDeclaration(AtLoc, DeclAttrs);
  case tok::objc_end:
    return ParseObjCAtEndDeclaration(AtLoc);
  case tok::objc_compatibility_alias:
    SingleDecl = ParseObjCAtAliasDeclaration(AtLoc);
    break;
  case tok::objc_synthesize:
    SingleDecl = ParseObjCPropertySynthesize(AtLoc);
    break;
  case tok::objc_dynamic:
    SingleDecl = ParseObjCPropertyDynamic(AtLoc);
    break;
  case tok::objc_import:
    if (getLangOpts().Modules || getLangOpts().DebuggerSupport) {
      Sema::ModuleImportState IS = Sema::ModuleImportState::NotACXX20Module;
      SingleDecl = ParseModuleImport(AtLoc, IS);
      break;
    }
    Diag(AtLoc, diag::err_atimport);
    SkipUntil(tok::semi);
    return Actions.ConvertDeclToDeclGroup(nullptr);
  default:
    Diag(AtLoc, diag::err_unexpected_at);
    SkipUntil(tok::semi);
    SingleDecl = nullptr;
    break;
  }
  return Actions.ConvertDeclToDeclGroup(SingleDecl);
}

/// Class to handle popping type parameters when leaving the scope.
class Parser::ObjCTypeParamListScope {
  Sema &Actions;
  Scope *S;
  ObjCTypeParamList *Params;

public:
  ObjCTypeParamListScope(Sema &Actions, Scope *S)
      : Actions(Actions), S(S), Params(nullptr) {}

  ~ObjCTypeParamListScope() {
    leave();
  }

  void enter(ObjCTypeParamList *P) {
    assert(!Params);
    Params = P;
  }

  void leave() {
    if (Params)
      Actions.ObjC().popObjCTypeParamList(S, Params);
    Params = nullptr;
  }
};

///
/// objc-class-declaration:
///    '@' 'class' objc-class-forward-decl (',' objc-class-forward-decl)* ';'
///
/// objc-class-forward-decl:
///   identifier objc-type-parameter-list[opt]
///
Parser::DeclGroupPtrTy
Parser::ParseObjCAtClassDeclaration(SourceLocation atLoc) {
  ConsumeToken(); // the identifier "class"
  SmallVector<IdentifierInfo *, 8> ClassNames;
  SmallVector<SourceLocation, 8> ClassLocs;
  SmallVector<ObjCTypeParamList *, 8> ClassTypeParams;

  while (true) {
    MaybeSkipAttributes(tok::objc_class);
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteObjCClassForwardDecl(getCurScope());
      return Actions.ConvertDeclToDeclGroup(nullptr);
    }
    if (expectIdentifier()) {
      SkipUntil(tok::semi);
      return Actions.ConvertDeclToDeclGroup(nullptr);
    }
    ClassNames.push_back(Tok.getIdentifierInfo());
    ClassLocs.push_back(Tok.getLocation());
    ConsumeToken();

    // Parse the optional objc-type-parameter-list.
    ObjCTypeParamList *TypeParams = nullptr;
    if (Tok.is(tok::less))
      TypeParams = parseObjCTypeParamList();
    ClassTypeParams.push_back(TypeParams);
    if (!TryConsumeToken(tok::comma))
      break;
  }

  // Consume the ';'.
  if (ExpectAndConsume(tok::semi, diag::err_expected_after, "@class"))
    return Actions.ConvertDeclToDeclGroup(nullptr);

  return Actions.ObjC().ActOnForwardClassDeclaration(
      atLoc, ClassNames.data(), ClassLocs.data(), ClassTypeParams,
      ClassNames.size());
}

void Parser::CheckNestedObjCContexts(SourceLocation AtLoc)
{
  SemaObjC::ObjCContainerKind ock = Actions.ObjC().getObjCContainerKind();
  if (ock == SemaObjC::OCK_None)
    return;

  Decl *Decl = Actions.ObjC().getObjCDeclContext();
  if (CurParsedObjCImpl) {
    CurParsedObjCImpl->finish(AtLoc);
  } else {
    Actions.ObjC().ActOnAtEnd(getCurScope(), AtLoc);
  }
  Diag(AtLoc, diag::err_objc_missing_end)
      << FixItHint::CreateInsertion(AtLoc, "@end\n");
  if (Decl)
    Diag(Decl->getBeginLoc(), diag::note_objc_container_start) << (int)ock;
}

///
///   objc-interface:
///     objc-class-interface-attributes[opt] objc-class-interface
///     objc-category-interface
///
///   objc-class-interface:
///     '@' 'interface' identifier objc-type-parameter-list[opt]
///       objc-superclass[opt] objc-protocol-refs[opt]
///       objc-class-instance-variables[opt]
///       objc-interface-decl-list
///     @end
///
///   objc-category-interface:
///     '@' 'interface' identifier objc-type-parameter-list[opt]
///       '(' identifier[opt] ')' objc-protocol-refs[opt]
///       objc-interface-decl-list
///     @end
///
///   objc-superclass:
///     ':' identifier objc-type-arguments[opt]
///
///   objc-class-interface-attributes:
///     __attribute__((visibility("default")))
///     __attribute__((visibility("hidden")))
///     __attribute__((deprecated))
///     __attribute__((unavailable))
///     __attribute__((objc_exception)) - used by NSException on 64-bit
///     __attribute__((objc_root_class))
///
Decl *Parser::ParseObjCAtInterfaceDeclaration(SourceLocation AtLoc,
                                              ParsedAttributes &attrs) {
  assert(Tok.isObjCAtKeyword(tok::objc_interface) &&
         "ParseObjCAtInterfaceDeclaration(): Expected @interface");
  CheckNestedObjCContexts(AtLoc);
  ConsumeToken(); // the "interface" identifier

  // Code completion after '@interface'.
  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteObjCInterfaceDecl(getCurScope());
    return nullptr;
  }

  MaybeSkipAttributes(tok::objc_interface);

  if (expectIdentifier())
    return nullptr; // missing class or category name.

  // We have a class or category name - consume it.
  IdentifierInfo *nameId = Tok.getIdentifierInfo();
  SourceLocation nameLoc = ConsumeToken();

  // Parse the objc-type-parameter-list or objc-protocol-refs. For the latter
  // case, LAngleLoc will be valid and ProtocolIdents will capture the
  // protocol references (that have not yet been resolved).
  SourceLocation LAngleLoc, EndProtoLoc;
  SmallVector<IdentifierLocPair, 8> ProtocolIdents;
  ObjCTypeParamList *typeParameterList = nullptr;
  ObjCTypeParamListScope typeParamScope(Actions, getCurScope());
  if (Tok.is(tok::less))
    typeParameterList = parseObjCTypeParamListOrProtocolRefs(
        typeParamScope, LAngleLoc, ProtocolIdents, EndProtoLoc);

  if (Tok.is(tok::l_paren) &&
      !isKnownToBeTypeSpecifier(GetLookAheadToken(1))) { // we have a category.

    BalancedDelimiterTracker T(*this, tok::l_paren);
    T.consumeOpen();

    SourceLocation categoryLoc;
    IdentifierInfo *categoryId = nullptr;
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteObjCInterfaceCategory(
          getCurScope(), nameId, nameLoc);
      return nullptr;
    }

    // For ObjC2, the category name is optional (not an error).
    if (Tok.is(tok::identifier)) {
      categoryId = Tok.getIdentifierInfo();
      categoryLoc = ConsumeToken();
    }
    else if (!getLangOpts().ObjC) {
      Diag(Tok, diag::err_expected)
          << tok::identifier; // missing category name.
      return nullptr;
    }

    T.consumeClose();
    if (T.getCloseLocation().isInvalid())
      return nullptr;

    // Next, we need to check for any protocol references.
    assert(LAngleLoc.isInvalid() && "Cannot have already parsed protocols");
    SmallVector<Decl *, 8> ProtocolRefs;
    SmallVector<SourceLocation, 8> ProtocolLocs;
    if (Tok.is(tok::less) &&
        ParseObjCProtocolReferences(ProtocolRefs, ProtocolLocs, true, true,
                                    LAngleLoc, EndProtoLoc,
                                    /*consumeLastToken=*/true))
      return nullptr;

    ObjCCategoryDecl *CategoryType = Actions.ObjC().ActOnStartCategoryInterface(
        AtLoc, nameId, nameLoc, typeParameterList, categoryId, categoryLoc,
        ProtocolRefs.data(), ProtocolRefs.size(), ProtocolLocs.data(),
        EndProtoLoc, attrs);

    if (Tok.is(tok::l_brace))
      ParseObjCClassInstanceVariables(CategoryType, tok::objc_private, AtLoc);

    ParseObjCInterfaceDeclList(tok::objc_not_keyword, CategoryType);

    return CategoryType;
  }
  // Parse a class interface.
  IdentifierInfo *superClassId = nullptr;
  SourceLocation superClassLoc;
  SourceLocation typeArgsLAngleLoc;
  SmallVector<ParsedType, 4> typeArgs;
  SourceLocation typeArgsRAngleLoc;
  SmallVector<Decl *, 4> protocols;
  SmallVector<SourceLocation, 4> protocolLocs;
  if (Tok.is(tok::colon)) { // a super class is specified.
    ConsumeToken();

    // Code completion of superclass names.
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteObjCSuperclass(getCurScope(), nameId,
                                                          nameLoc);
      return nullptr;
    }

    if (expectIdentifier())
      return nullptr; // missing super class name.
    superClassId = Tok.getIdentifierInfo();
    superClassLoc = ConsumeToken();

    // Type arguments for the superclass or protocol conformances.
    if (Tok.is(tok::less)) {
      parseObjCTypeArgsOrProtocolQualifiers(
          nullptr, typeArgsLAngleLoc, typeArgs, typeArgsRAngleLoc, LAngleLoc,
          protocols, protocolLocs, EndProtoLoc,
          /*consumeLastToken=*/true,
          /*warnOnIncompleteProtocols=*/true);
      if (Tok.is(tok::eof))
        return nullptr;
    }
  }

  // Next, we need to check for any protocol references.
  if (LAngleLoc.isValid()) {
    if (!ProtocolIdents.empty()) {
      // We already parsed the protocols named when we thought we had a
      // type parameter list. Translate them into actual protocol references.
      for (const auto &pair : ProtocolIdents) {
        protocolLocs.push_back(pair.second);
      }
      Actions.ObjC().FindProtocolDeclaration(/*WarnOnDeclarations=*/true,
                                             /*ForObjCContainer=*/true,
                                             ProtocolIdents, protocols);
    }
  } else if (protocols.empty() && Tok.is(tok::less) &&
             ParseObjCProtocolReferences(protocols, protocolLocs, true, true,
                                         LAngleLoc, EndProtoLoc,
                                         /*consumeLastToken=*/true)) {
    return nullptr;
  }

  if (Tok.isNot(tok::less))
    Actions.ObjC().ActOnTypedefedProtocols(protocols, protocolLocs,
                                           superClassId, superClassLoc);

  SkipBodyInfo SkipBody;
  ObjCInterfaceDecl *ClsType = Actions.ObjC().ActOnStartClassInterface(
      getCurScope(), AtLoc, nameId, nameLoc, typeParameterList, superClassId,
      superClassLoc, typeArgs,
      SourceRange(typeArgsLAngleLoc, typeArgsRAngleLoc), protocols.data(),
      protocols.size(), protocolLocs.data(), EndProtoLoc, attrs, &SkipBody);

  if (Tok.is(tok::l_brace))
    ParseObjCClassInstanceVariables(ClsType, tok::objc_protected, AtLoc);

  ParseObjCInterfaceDeclList(tok::objc_interface, ClsType);

  if (SkipBody.CheckSameAsPrevious) {
    auto *PreviousDef = cast<ObjCInterfaceDecl>(SkipBody.Previous);
    if (Actions.ActOnDuplicateODRHashDefinition(ClsType, PreviousDef)) {
      ClsType->mergeDuplicateDefinitionWithCommon(PreviousDef->getDefinition());
    } else {
      ODRDiagsEmitter DiagsEmitter(Diags, Actions.getASTContext(),
                                   getPreprocessor().getLangOpts());
      DiagsEmitter.diagnoseMismatch(PreviousDef, ClsType);
      ClsType->setInvalidDecl();
    }
  }

  return ClsType;
}

/// Add an attribute for a context-sensitive type nullability to the given
/// declarator.
static void addContextSensitiveTypeNullability(Parser &P,
                                               Declarator &D,
                                               NullabilityKind nullability,
                                               SourceLocation nullabilityLoc,
                                               bool &addedToDeclSpec) {
  // Create the attribute.
  auto getNullabilityAttr = [&](AttributePool &Pool) -> ParsedAttr * {
    return Pool.create(P.getNullabilityKeyword(nullability),
                       SourceRange(nullabilityLoc), nullptr, SourceLocation(),
                       nullptr, 0, ParsedAttr::Form::ContextSensitiveKeyword());
  };

  if (D.getNumTypeObjects() > 0) {
    // Add the attribute to the declarator chunk nearest the declarator.
    D.getTypeObject(0).getAttrs().addAtEnd(
        getNullabilityAttr(D.getAttributePool()));
  } else if (!addedToDeclSpec) {
    // Otherwise, just put it on the declaration specifiers (if one
    // isn't there already).
    D.getMutableDeclSpec().getAttributes().addAtEnd(
        getNullabilityAttr(D.getMutableDeclSpec().getAttributes().getPool()));
    addedToDeclSpec = true;
  }
}

/// Parse an Objective-C type parameter list, if present, or capture
/// the locations of the protocol identifiers for a list of protocol
/// references.
///
///   objc-type-parameter-list:
///     '<' objc-type-parameter (',' objc-type-parameter)* '>'
///
///   objc-type-parameter:
///     objc-type-parameter-variance? identifier objc-type-parameter-bound[opt]
///
///   objc-type-parameter-bound:
///     ':' type-name
///
///   objc-type-parameter-variance:
///     '__covariant'
///     '__contravariant'
///
/// \param lAngleLoc The location of the starting '<'.
///
/// \param protocolIdents Will capture the list of identifiers, if the
/// angle brackets contain a list of protocol references rather than a
/// type parameter list.
///
/// \param rAngleLoc The location of the ending '>'.
ObjCTypeParamList *Parser::parseObjCTypeParamListOrProtocolRefs(
    ObjCTypeParamListScope &Scope, SourceLocation &lAngleLoc,
    SmallVectorImpl<IdentifierLocPair> &protocolIdents,
    SourceLocation &rAngleLoc, bool mayBeProtocolList) {
  assert(Tok.is(tok::less) && "Not at the beginning of a type parameter list");

  // Within the type parameter list, don't treat '>' as an operator.
  GreaterThanIsOperatorScope G(GreaterThanIsOperator, false);

  // Local function to "flush" the protocol identifiers, turning them into
  // type parameters.
  SmallVector<Decl *, 4> typeParams;
  auto makeProtocolIdentsIntoTypeParameters = [&]() {
    unsigned index = 0;
    for (const auto &pair : protocolIdents) {
      DeclResult typeParam = Actions.ObjC().actOnObjCTypeParam(
          getCurScope(), ObjCTypeParamVariance::Invariant, SourceLocation(),
          index++, pair.first, pair.second, SourceLocation(), nullptr);
      if (typeParam.isUsable())
        typeParams.push_back(typeParam.get());
    }

    protocolIdents.clear();
    mayBeProtocolList = false;
  };

  bool invalid = false;
  lAngleLoc = ConsumeToken();

  do {
    // Parse the variance, if any.
    SourceLocation varianceLoc;
    ObjCTypeParamVariance variance = ObjCTypeParamVariance::Invariant;
    if (Tok.is(tok::kw___covariant) || Tok.is(tok::kw___contravariant)) {
      variance = Tok.is(tok::kw___covariant)
                   ? ObjCTypeParamVariance::Covariant
                   : ObjCTypeParamVariance::Contravariant;
      varianceLoc = ConsumeToken();

      // Once we've seen a variance specific , we know this is not a
      // list of protocol references.
      if (mayBeProtocolList) {
        // Up until now, we have been queuing up parameters because they
        // might be protocol references. Turn them into parameters now.
        makeProtocolIdentsIntoTypeParameters();
      }
    }

    // Parse the identifier.
    if (!Tok.is(tok::identifier)) {
      // Code completion.
      if (Tok.is(tok::code_completion)) {
        // FIXME: If these aren't protocol references, we'll need different
        // completions.
        cutOffParsing();
        Actions.CodeCompletion().CodeCompleteObjCProtocolReferences(
            protocolIdents);

        // FIXME: Better recovery here?.
        return nullptr;
      }

      Diag(Tok, diag::err_objc_expected_type_parameter);
      invalid = true;
      break;
    }

    IdentifierInfo *paramName = Tok.getIdentifierInfo();
    SourceLocation paramLoc = ConsumeToken();

    // If there is a bound, parse it.
    SourceLocation colonLoc;
    TypeResult boundType;
    if (TryConsumeToken(tok::colon, colonLoc)) {
      // Once we've seen a bound, we know this is not a list of protocol
      // references.
      if (mayBeProtocolList) {
        // Up until now, we have been queuing up parameters because they
        // might be protocol references. Turn them into parameters now.
        makeProtocolIdentsIntoTypeParameters();
      }

      // type-name
      boundType = ParseTypeName();
      if (boundType.isInvalid())
        invalid = true;
    } else if (mayBeProtocolList) {
      // If this could still be a protocol list, just capture the identifier.
      // We don't want to turn it into a parameter.
      protocolIdents.push_back(std::make_pair(paramName, paramLoc));
      continue;
    }

    // Create the type parameter.
    DeclResult typeParam = Actions.ObjC().actOnObjCTypeParam(
        getCurScope(), variance, varianceLoc, typeParams.size(), paramName,
        paramLoc, colonLoc, boundType.isUsable() ? boundType.get() : nullptr);
    if (typeParam.isUsable())
      typeParams.push_back(typeParam.get());
  } while (TryConsumeToken(tok::comma));

  // Parse the '>'.
  if (invalid) {
    SkipUntil(tok::greater, tok::at, StopBeforeMatch);
    if (Tok.is(tok::greater))
      ConsumeToken();
  } else if (ParseGreaterThanInTemplateList(lAngleLoc, rAngleLoc,
                                            /*ConsumeLastToken=*/true,
                                            /*ObjCGenericList=*/true)) {
    SkipUntil({tok::greater, tok::greaterequal, tok::at, tok::minus,
               tok::minus, tok::plus, tok::colon, tok::l_paren, tok::l_brace,
               tok::comma, tok::semi },
              StopBeforeMatch);
    if (Tok.is(tok::greater))
      ConsumeToken();
  }

  if (mayBeProtocolList) {
    // A type parameter list must be followed by either a ':' (indicating the
    // presence of a superclass) or a '(' (indicating that this is a category
    // or extension). This disambiguates between an objc-type-parameter-list
    // and a objc-protocol-refs.
    if (Tok.isNot(tok::colon) && Tok.isNot(tok::l_paren)) {
      // Returning null indicates that we don't have a type parameter list.
      // The results the caller needs to handle the protocol references are
      // captured in the reference parameters already.
      return nullptr;
    }

    // We have a type parameter list that looks like a list of protocol
    // references. Turn that parameter list into type parameters.
    makeProtocolIdentsIntoTypeParameters();
  }

  // Form the type parameter list and enter its scope.
  ObjCTypeParamList *list = Actions.ObjC().actOnObjCTypeParamList(
      getCurScope(), lAngleLoc, typeParams, rAngleLoc);
  Scope.enter(list);

  // Clear out the angle locations; they're used by the caller to indicate
  // whether there are any protocol references.
  lAngleLoc = SourceLocation();
  rAngleLoc = SourceLocation();
  return invalid ? nullptr : list;
}

/// Parse an objc-type-parameter-list.
ObjCTypeParamList *Parser::parseObjCTypeParamList() {
  SourceLocation lAngleLoc;
  SmallVector<IdentifierLocPair, 1> protocolIdents;
  SourceLocation rAngleLoc;

  ObjCTypeParamListScope Scope(Actions, getCurScope());
  return parseObjCTypeParamListOrProtocolRefs(Scope, lAngleLoc, protocolIdents,
                                              rAngleLoc,
                                              /*mayBeProtocolList=*/false);
}

static bool isTopLevelObjCKeyword(tok::ObjCKeywordKind DirectiveKind) {
  switch (DirectiveKind) {
  case tok::objc_class:
  case tok::objc_compatibility_alias:
  case tok::objc_interface:
  case tok::objc_implementation:
  case tok::objc_protocol:
    return true;
  default:
    return false;
  }
}

///   objc-interface-decl-list:
///     empty
///     objc-interface-decl-list objc-property-decl [OBJC2]
///     objc-interface-decl-list objc-method-requirement [OBJC2]
///     objc-interface-decl-list objc-method-proto ';'
///     objc-interface-decl-list declaration
///     objc-interface-decl-list ';'
///
///   objc-method-requirement: [OBJC2]
///     @required
///     @optional
///
void Parser::ParseObjCInterfaceDeclList(tok::ObjCKeywordKind contextKey,
                                        Decl *CDecl) {
  SmallVector<Decl *, 32> allMethods;
  SmallVector<DeclGroupPtrTy, 8> allTUVariables;
  tok::ObjCKeywordKind MethodImplKind = tok::objc_not_keyword;

  SourceRange AtEnd;

  while (true) {
    // If this is a method prototype, parse it.
    if (Tok.isOneOf(tok::minus, tok::plus)) {
      if (Decl *methodPrototype =
          ParseObjCMethodPrototype(MethodImplKind, false))
        allMethods.push_back(methodPrototype);
      // Consume the ';' here, since ParseObjCMethodPrototype() is re-used for
      // method definitions.
      if (ExpectAndConsumeSemi(diag::err_expected_semi_after_method_proto)) {
        // We didn't find a semi and we error'ed out. Skip until a ';' or '@'.
        SkipUntil(tok::at, StopAtSemi | StopBeforeMatch);
        if (Tok.is(tok::semi))
          ConsumeToken();
      }
      continue;
    }
    if (Tok.is(tok::l_paren)) {
      Diag(Tok, diag::err_expected_minus_or_plus);
      ParseObjCMethodDecl(Tok.getLocation(),
                          tok::minus,
                          MethodImplKind, false);
      continue;
    }
    // Ignore excess semicolons.
    if (Tok.is(tok::semi)) {
      // FIXME: This should use ConsumeExtraSemi() for extraneous semicolons,
      // to make -Wextra-semi diagnose them.
      ConsumeToken();
      continue;
    }

    // If we got to the end of the file, exit the loop.
    if (isEofOrEom())
      break;

    // Code completion within an Objective-C interface.
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteOrdinaryName(
          getCurScope(), CurParsedObjCImpl
                             ? SemaCodeCompletion::PCC_ObjCImplementation
                             : SemaCodeCompletion::PCC_ObjCInterface);
      return;
    }

    // If we don't have an @ directive, parse it as a function definition.
    if (Tok.isNot(tok::at)) {
      // The code below does not consume '}'s because it is afraid of eating the
      // end of a namespace.  Because of the way this code is structured, an
      // erroneous r_brace would cause an infinite loop if not handled here.
      if (Tok.is(tok::r_brace))
        break;

      ParsedAttributes EmptyDeclAttrs(AttrFactory);
      ParsedAttributes EmptyDeclSpecAttrs(AttrFactory);

      // Since we call ParseDeclarationOrFunctionDefinition() instead of
      // ParseExternalDeclaration() below (so that this doesn't parse nested
      // @interfaces), this needs to duplicate some code from the latter.
      if (Tok.isOneOf(tok::kw_static_assert, tok::kw__Static_assert)) {
        SourceLocation DeclEnd;
        ParsedAttributes EmptyDeclSpecAttrs(AttrFactory);
        allTUVariables.push_back(ParseDeclaration(DeclaratorContext::File,
                                                  DeclEnd, EmptyDeclAttrs,
                                                  EmptyDeclSpecAttrs));
        continue;
      }

      allTUVariables.push_back(ParseDeclarationOrFunctionDefinition(
          EmptyDeclAttrs, EmptyDeclSpecAttrs));
      continue;
    }

    // Otherwise, we have an @ directive, peak at the next token
    SourceLocation AtLoc = Tok.getLocation();
    const auto &NextTok = NextToken();
    if (NextTok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteObjCAtDirective(getCurScope());
      return;
    }

    tok::ObjCKeywordKind DirectiveKind = NextTok.getObjCKeywordID();
    if (DirectiveKind == tok::objc_end) { // @end -> terminate list
      ConsumeToken();                     // the "@"
      AtEnd.setBegin(AtLoc);
      AtEnd.setEnd(Tok.getLocation());
      break;
    } else if (DirectiveKind == tok::objc_not_keyword) {
      Diag(NextTok, diag::err_objc_unknown_at);
      SkipUntil(tok::semi);
      continue;
    }

    // If we see something like '@interface' that's only allowed at the top
    // level, bail out as if we saw an '@end'. We'll diagnose this below.
    if (isTopLevelObjCKeyword(DirectiveKind))
      break;

    // Otherwise parse it as part of the current declaration. Eat "@identifier".
    ConsumeToken();
    ConsumeToken();

    switch (DirectiveKind) {
    default:
      // FIXME: If someone forgets an @end on a protocol, this loop will
      // continue to eat up tons of stuff and spew lots of nonsense errors.  It
      // would probably be better to bail out if we saw an @class or @interface
      // or something like that.
      Diag(AtLoc, diag::err_objc_illegal_interface_qual);
      // Skip until we see an '@' or '}' or ';'.
      SkipUntil(tok::r_brace, tok::at, StopAtSemi);
      break;

    case tok::objc_required:
    case tok::objc_optional:
      // This is only valid on protocols.
      if (contextKey != tok::objc_protocol)
        Diag(AtLoc, diag::err_objc_directive_only_in_protocol);
      else
        MethodImplKind = DirectiveKind;
      break;

    case tok::objc_property:
      ObjCDeclSpec OCDS;
      SourceLocation LParenLoc;
      // Parse property attribute list, if any.
      if (Tok.is(tok::l_paren)) {
        LParenLoc = Tok.getLocation();
        ParseObjCPropertyAttribute(OCDS);
      }

      bool addedToDeclSpec = false;
      auto ObjCPropertyCallback = [&](ParsingFieldDeclarator &FD) -> Decl * {
        if (FD.D.getIdentifier() == nullptr) {
          Diag(AtLoc, diag::err_objc_property_requires_field_name)
              << FD.D.getSourceRange();
          return nullptr;
        }
        if (FD.BitfieldSize) {
          Diag(AtLoc, diag::err_objc_property_bitfield)
              << FD.D.getSourceRange();
          return nullptr;
        }

        // Map a nullability property attribute to a context-sensitive keyword
        // attribute.
        if (OCDS.getPropertyAttributes() &
            ObjCPropertyAttribute::kind_nullability)
          addContextSensitiveTypeNullability(*this, FD.D, OCDS.getNullability(),
                                             OCDS.getNullabilityLoc(),
                                             addedToDeclSpec);

        // Install the property declarator into interfaceDecl.
        const IdentifierInfo *SelName =
            OCDS.getGetterName() ? OCDS.getGetterName() : FD.D.getIdentifier();

        Selector GetterSel = PP.getSelectorTable().getNullarySelector(SelName);
        const IdentifierInfo *SetterName = OCDS.getSetterName();
        Selector SetterSel;
        if (SetterName)
          SetterSel = PP.getSelectorTable().getSelector(1, &SetterName);
        else
          SetterSel = SelectorTable::constructSetterSelector(
              PP.getIdentifierTable(), PP.getSelectorTable(),
              FD.D.getIdentifier());
        Decl *Property = Actions.ObjC().ActOnProperty(
            getCurScope(), AtLoc, LParenLoc, FD, OCDS, GetterSel, SetterSel,
            MethodImplKind);

        FD.complete(Property);
        return Property;
      };

      // Parse all the comma separated declarators.
      ParsingDeclSpec DS(*this);
      ParseStructDeclaration(DS, ObjCPropertyCallback);

      ExpectAndConsume(tok::semi, diag::err_expected_semi_decl_list);
      break;
    }
  }

  // We break out of the big loop in 3 cases: when we see @end or when we see
  // top-level ObjC keyword or EOF. In the former case, eat the @end. In the
  // later cases, emit an error.
  if (Tok.isObjCAtKeyword(tok::objc_end)) {
    ConsumeToken(); // the "end" identifier
  } else {
    Diag(Tok, diag::err_objc_missing_end)
        << FixItHint::CreateInsertion(Tok.getLocation(), "\n@end\n");
    Diag(CDecl->getBeginLoc(), diag::note_objc_container_start)
        << (int)Actions.ObjC().getObjCContainerKind();
    AtEnd.setBegin(Tok.getLocation());
    AtEnd.setEnd(Tok.getLocation());
  }

  // Insert collected methods declarations into the @interface object.
  // This passes in an invalid SourceLocation for AtEndLoc when EOF is hit.
  Actions.ObjC().ActOnAtEnd(getCurScope(), AtEnd, allMethods, allTUVariables);
}

/// Diagnose redundant or conflicting nullability information.
static void diagnoseRedundantPropertyNullability(Parser &P,
                                                 ObjCDeclSpec &DS,
                                                 NullabilityKind nullability,
                                                 SourceLocation nullabilityLoc){
  if (DS.getNullability() == nullability) {
    P.Diag(nullabilityLoc, diag::warn_nullability_duplicate)
      << DiagNullabilityKind(nullability, true)
      << SourceRange(DS.getNullabilityLoc());
    return;
  }

  P.Diag(nullabilityLoc, diag::err_nullability_conflicting)
    << DiagNullabilityKind(nullability, true)
    << DiagNullabilityKind(DS.getNullability(), true)
    << SourceRange(DS.getNullabilityLoc());
}

///   Parse property attribute declarations.
///
///   property-attr-decl: '(' property-attrlist ')'
///   property-attrlist:
///     property-attribute
///     property-attrlist ',' property-attribute
///   property-attribute:
///     getter '=' identifier
///     setter '=' identifier ':'
///     direct
///     readonly
///     readwrite
///     assign
///     retain
///     copy
///     nonatomic
///     atomic
///     strong
///     weak
///     unsafe_unretained
///     nonnull
///     nullable
///     null_unspecified
///     null_resettable
///     class
///
void Parser::ParseObjCPropertyAttribute(ObjCDeclSpec &DS) {
  assert(Tok.getKind() == tok::l_paren);
  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();

  while (true) {
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteObjCPropertyFlags(getCurScope(), DS);
      return;
    }
    const IdentifierInfo *II = Tok.getIdentifierInfo();

    // If this is not an identifier at all, bail out early.
    if (!II) {
      T.consumeClose();
      return;
    }

    SourceLocation AttrName = ConsumeToken(); // consume last attribute name

    if (II->isStr("readonly"))
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_readonly);
    else if (II->isStr("assign"))
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_assign);
    else if (II->isStr("unsafe_unretained"))
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_unsafe_unretained);
    else if (II->isStr("readwrite"))
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_readwrite);
    else if (II->isStr("retain"))
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_retain);
    else if (II->isStr("strong"))
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_strong);
    else if (II->isStr("copy"))
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_copy);
    else if (II->isStr("nonatomic"))
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_nonatomic);
    else if (II->isStr("atomic"))
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_atomic);
    else if (II->isStr("weak"))
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_weak);
    else if (II->isStr("getter") || II->isStr("setter")) {
      bool IsSetter = II->getNameStart()[0] == 's';

      // getter/setter require extra treatment.
      unsigned DiagID = IsSetter ? diag::err_objc_expected_equal_for_setter :
                                   diag::err_objc_expected_equal_for_getter;

      if (ExpectAndConsume(tok::equal, DiagID)) {
        SkipUntil(tok::r_paren, StopAtSemi);
        return;
      }

      if (Tok.is(tok::code_completion)) {
        cutOffParsing();
        if (IsSetter)
          Actions.CodeCompletion().CodeCompleteObjCPropertySetter(
              getCurScope());
        else
          Actions.CodeCompletion().CodeCompleteObjCPropertyGetter(
              getCurScope());
        return;
      }

      SourceLocation SelLoc;
      IdentifierInfo *SelIdent = ParseObjCSelectorPiece(SelLoc);

      if (!SelIdent) {
        Diag(Tok, diag::err_objc_expected_selector_for_getter_setter)
          << IsSetter;
        SkipUntil(tok::r_paren, StopAtSemi);
        return;
      }

      if (IsSetter) {
        DS.setPropertyAttributes(ObjCPropertyAttribute::kind_setter);
        DS.setSetterName(SelIdent, SelLoc);

        if (ExpectAndConsume(tok::colon,
                             diag::err_expected_colon_after_setter_name)) {
          SkipUntil(tok::r_paren, StopAtSemi);
          return;
        }
      } else {
        DS.setPropertyAttributes(ObjCPropertyAttribute::kind_getter);
        DS.setGetterName(SelIdent, SelLoc);
      }
    } else if (II->isStr("nonnull")) {
      if (DS.getPropertyAttributes() & ObjCPropertyAttribute::kind_nullability)
        diagnoseRedundantPropertyNullability(*this, DS,
                                             NullabilityKind::NonNull,
                                             Tok.getLocation());
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_nullability);
      DS.setNullability(Tok.getLocation(), NullabilityKind::NonNull);
    } else if (II->isStr("nullable")) {
      if (DS.getPropertyAttributes() & ObjCPropertyAttribute::kind_nullability)
        diagnoseRedundantPropertyNullability(*this, DS,
                                             NullabilityKind::Nullable,
                                             Tok.getLocation());
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_nullability);
      DS.setNullability(Tok.getLocation(), NullabilityKind::Nullable);
    } else if (II->isStr("null_unspecified")) {
      if (DS.getPropertyAttributes() & ObjCPropertyAttribute::kind_nullability)
        diagnoseRedundantPropertyNullability(*this, DS,
                                             NullabilityKind::Unspecified,
                                             Tok.getLocation());
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_nullability);
      DS.setNullability(Tok.getLocation(), NullabilityKind::Unspecified);
    } else if (II->isStr("null_resettable")) {
      if (DS.getPropertyAttributes() & ObjCPropertyAttribute::kind_nullability)
        diagnoseRedundantPropertyNullability(*this, DS,
                                             NullabilityKind::Unspecified,
                                             Tok.getLocation());
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_nullability);
      DS.setNullability(Tok.getLocation(), NullabilityKind::Unspecified);

      // Also set the null_resettable bit.
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_null_resettable);
    } else if (II->isStr("class")) {
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_class);
    } else if (II->isStr("direct")) {
      DS.setPropertyAttributes(ObjCPropertyAttribute::kind_direct);
    } else {
      Diag(AttrName, diag::err_objc_expected_property_attr) << II;
      SkipUntil(tok::r_paren, StopAtSemi);
      return;
    }

    if (Tok.isNot(tok::comma))
      break;

    ConsumeToken();
  }

  T.consumeClose();
}

///   objc-method-proto:
///     objc-instance-method objc-method-decl objc-method-attributes[opt]
///     objc-class-method objc-method-decl objc-method-attributes[opt]
///
///   objc-instance-method: '-'
///   objc-class-method: '+'
///
///   objc-method-attributes:         [OBJC2]
///     __attribute__((deprecated))
///
Decl *Parser::ParseObjCMethodPrototype(tok::ObjCKeywordKind MethodImplKind,
                                       bool MethodDefinition) {
  assert(Tok.isOneOf(tok::minus, tok::plus) && "expected +/-");

  tok::TokenKind methodType = Tok.getKind();
  SourceLocation mLoc = ConsumeToken();
  Decl *MDecl = ParseObjCMethodDecl(mLoc, methodType, MethodImplKind,
                                    MethodDefinition);
  // Since this rule is used for both method declarations and definitions,
  // the caller is (optionally) responsible for consuming the ';'.
  return MDecl;
}

///   objc-selector:
///     identifier
///     one of
///       enum struct union if else while do for switch case default
///       break continue return goto asm sizeof typeof __alignof
///       unsigned long const short volatile signed restrict _Complex
///       in out inout bycopy byref oneway int char float double void _Bool
///
IdentifierInfo *Parser::ParseObjCSelectorPiece(SourceLocation &SelectorLoc) {

  switch (Tok.getKind()) {
  default:
    return nullptr;
  case tok::colon:
    // Empty selector piece uses the location of the ':'.
    SelectorLoc = Tok.getLocation();
    return nullptr;
  case tok::ampamp:
  case tok::ampequal:
  case tok::amp:
  case tok::pipe:
  case tok::tilde:
  case tok::exclaim:
  case tok::exclaimequal:
  case tok::pipepipe:
  case tok::pipeequal:
  case tok::caret:
  case tok::caretequal: {
    std::string ThisTok(PP.getSpelling(Tok));
    if (isLetter(ThisTok[0])) {
      IdentifierInfo *II = &PP.getIdentifierTable().get(ThisTok);
      Tok.setKind(tok::identifier);
      SelectorLoc = ConsumeToken();
      return II;
    }
    return nullptr;
  }

  case tok::identifier:
  case tok::kw_asm:
  case tok::kw_auto:
  case tok::kw_bool:
  case tok::kw_break:
  case tok::kw_case:
  case tok::kw_catch:
  case tok::kw_char:
  case tok::kw_class:
  case tok::kw_const:
  case tok::kw_const_cast:
  case tok::kw_continue:
  case tok::kw_default:
  case tok::kw_delete:
  case tok::kw_do:
  case tok::kw_double:
  case tok::kw_dynamic_cast:
  case tok::kw_else:
  case tok::kw_enum:
  case tok::kw_explicit:
  case tok::kw_export:
  case tok::kw_extern:
  case tok::kw_false:
  case tok::kw_float:
  case tok::kw_for:
  case tok::kw_friend:
  case tok::kw_goto:
  case tok::kw_if:
  case tok::kw_inline:
  case tok::kw_int:
  case tok::kw_long:
  case tok::kw_mutable:
  case tok::kw_namespace:
  case tok::kw_new:
  case tok::kw_operator:
  case tok::kw_private:
  case tok::kw_protected:
  case tok::kw_public:
  case tok::kw_register:
  case tok::kw_reinterpret_cast:
  case tok::kw_restrict:
  case tok::kw_return:
  case tok::kw_short:
  case tok::kw_signed:
  case tok::kw_sizeof:
  case tok::kw_static:
  case tok::kw_static_cast:
  case tok::kw_struct:
  case tok::kw_switch:
  case tok::kw_template:
  case tok::kw_this:
  case tok::kw_throw:
  case tok::kw_true:
  case tok::kw_try:
  case tok::kw_typedef:
  case tok::kw_typeid:
  case tok::kw_typename:
  case tok::kw_typeof:
  case tok::kw_union:
  case tok::kw_unsigned:
  case tok::kw_using:
  case tok::kw_virtual:
  case tok::kw_void:
  case tok::kw_volatile:
  case tok::kw_wchar_t:
  case tok::kw_while:
  case tok::kw__Bool:
  case tok::kw__Complex:
  case tok::kw___alignof:
  case tok::kw___auto_type:
    IdentifierInfo *II = Tok.getIdentifierInfo();
    SelectorLoc = ConsumeToken();
    return II;
  }
}

///  objc-for-collection-in: 'in'
///
bool Parser::isTokIdentifier_in() const {
  // FIXME: May have to do additional look-ahead to only allow for
  // valid tokens following an 'in'; such as an identifier, unary operators,
  // '[' etc.
  return (getLangOpts().ObjC && Tok.is(tok::identifier) &&
          Tok.getIdentifierInfo() == ObjCTypeQuals[objc_in]);
}

/// ParseObjCTypeQualifierList - This routine parses the objective-c's type
/// qualifier list and builds their bitmask representation in the input
/// argument.
///
///   objc-type-qualifiers:
///     objc-type-qualifier
///     objc-type-qualifiers objc-type-qualifier
///
///   objc-type-qualifier:
///     'in'
///     'out'
///     'inout'
///     'oneway'
///     'bycopy'
///     'byref'
///     'nonnull'
///     'nullable'
///     'null_unspecified'
///
void Parser::ParseObjCTypeQualifierList(ObjCDeclSpec &DS,
                                        DeclaratorContext Context) {
  assert(Context == DeclaratorContext::ObjCParameter ||
         Context == DeclaratorContext::ObjCResult);

  while (true) {
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteObjCPassingType(
          getCurScope(), DS, Context == DeclaratorContext::ObjCParameter);
      return;
    }

    if (Tok.isNot(tok::identifier))
      return;

    const IdentifierInfo *II = Tok.getIdentifierInfo();
    for (unsigned i = 0; i != objc_NumQuals; ++i) {
      if (II != ObjCTypeQuals[i] ||
          NextToken().is(tok::less) ||
          NextToken().is(tok::coloncolon))
        continue;

      ObjCDeclSpec::ObjCDeclQualifier Qual;
      NullabilityKind Nullability;
      switch (i) {
      default: llvm_unreachable("Unknown decl qualifier");
      case objc_in:     Qual = ObjCDeclSpec::DQ_In; break;
      case objc_out:    Qual = ObjCDeclSpec::DQ_Out; break;
      case objc_inout:  Qual = ObjCDeclSpec::DQ_Inout; break;
      case objc_oneway: Qual = ObjCDeclSpec::DQ_Oneway; break;
      case objc_bycopy: Qual = ObjCDeclSpec::DQ_Bycopy; break;
      case objc_byref:  Qual = ObjCDeclSpec::DQ_Byref; break;

      case objc_nonnull:
        Qual = ObjCDeclSpec::DQ_CSNullability;
        Nullability = NullabilityKind::NonNull;
        break;

      case objc_nullable:
        Qual = ObjCDeclSpec::DQ_CSNullability;
        Nullability = NullabilityKind::Nullable;
        break;

      case objc_null_unspecified:
        Qual = ObjCDeclSpec::DQ_CSNullability;
        Nullability = NullabilityKind::Unspecified;
        break;
      }

      // FIXME: Diagnose redundant specifiers.
      DS.setObjCDeclQualifier(Qual);
      if (Qual == ObjCDeclSpec::DQ_CSNullability)
        DS.setNullability(Tok.getLocation(), Nullability);

      ConsumeToken();
      II = nullptr;
      break;
    }

    // If this wasn't a recognized qualifier, bail out.
    if (II) return;
  }
}

/// Take all the decl attributes out of the given list and add
/// them to the given attribute set.
static void takeDeclAttributes(ParsedAttributesView &attrs,
                               ParsedAttributesView &from) {
  for (auto &AL : llvm::reverse(from)) {
    if (!AL.isUsedAsTypeAttr()) {
      from.remove(&AL);
      attrs.addAtEnd(&AL);
    }
  }
}

/// takeDeclAttributes - Take all the decl attributes from the given
/// declarator and add them to the given list.
static void takeDeclAttributes(ParsedAttributes &attrs,
                               Declarator &D) {
  // This gets called only from Parser::ParseObjCTypeName(), and that should
  // never add declaration attributes to the Declarator.
  assert(D.getDeclarationAttributes().empty());

  // First, take ownership of all attributes.
  attrs.getPool().takeAllFrom(D.getAttributePool());
  attrs.getPool().takeAllFrom(D.getDeclSpec().getAttributePool());

  // Now actually move the attributes over.
  takeDeclAttributes(attrs, D.getMutableDeclSpec().getAttributes());
  takeDeclAttributes(attrs, D.getAttributes());
  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i)
    takeDeclAttributes(attrs, D.getTypeObject(i).getAttrs());
}

///   objc-type-name:
///     '(' objc-type-qualifiers[opt] type-name ')'
///     '(' objc-type-qualifiers[opt] ')'
///
ParsedType Parser::ParseObjCTypeName(ObjCDeclSpec &DS,
                                     DeclaratorContext context,
                                     ParsedAttributes *paramAttrs) {
  assert(context == DeclaratorContext::ObjCParameter ||
         context == DeclaratorContext::ObjCResult);
  assert((paramAttrs != nullptr) ==
         (context == DeclaratorContext::ObjCParameter));

  assert(Tok.is(tok::l_paren) && "expected (");

  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();

  ObjCDeclContextSwitch ObjCDC(*this);

  // Parse type qualifiers, in, inout, etc.
  ParseObjCTypeQualifierList(DS, context);
  SourceLocation TypeStartLoc = Tok.getLocation();

  ParsedType Ty;
  if (isTypeSpecifierQualifier() || isObjCInstancetype()) {
    // Parse an abstract declarator.
    DeclSpec declSpec(AttrFactory);
    declSpec.setObjCQualifiers(&DS);
    DeclSpecContext dsContext = DeclSpecContext::DSC_normal;
    if (context == DeclaratorContext::ObjCResult)
      dsContext = DeclSpecContext::DSC_objc_method_result;
    ParseSpecifierQualifierList(declSpec, AS_none, dsContext);
    Declarator declarator(declSpec, ParsedAttributesView::none(), context);
    ParseDeclarator(declarator);

    // If that's not invalid, extract a type.
    if (!declarator.isInvalidType()) {
      // Map a nullability specifier to a context-sensitive keyword attribute.
      bool addedToDeclSpec = false;
      if (DS.getObjCDeclQualifier() & ObjCDeclSpec::DQ_CSNullability)
        addContextSensitiveTypeNullability(*this, declarator,
                                           DS.getNullability(),
                                           DS.getNullabilityLoc(),
                                           addedToDeclSpec);

      TypeResult type = Actions.ActOnTypeName(declarator);
      if (!type.isInvalid())
        Ty = type.get();

      // If we're parsing a parameter, steal all the decl attributes
      // and add them to the decl spec.
      if (context == DeclaratorContext::ObjCParameter)
        takeDeclAttributes(*paramAttrs, declarator);
    }
  }

  if (Tok.is(tok::r_paren))
    T.consumeClose();
  else if (Tok.getLocation() == TypeStartLoc) {
    // If we didn't eat any tokens, then this isn't a type.
    Diag(Tok, diag::err_expected_type);
    SkipUntil(tok::r_paren, StopAtSemi);
  } else {
    // Otherwise, we found *something*, but didn't get a ')' in the right
    // place.  Emit an error then return what we have as the type.
    T.consumeClose();
  }
  return Ty;
}

///   objc-method-decl:
///     objc-selector
///     objc-keyword-selector objc-parmlist[opt]
///     objc-type-name objc-selector
///     objc-type-name objc-keyword-selector objc-parmlist[opt]
///
///   objc-keyword-selector:
///     objc-keyword-decl
///     objc-keyword-selector objc-keyword-decl
///
///   objc-keyword-decl:
///     objc-selector ':' objc-type-name objc-keyword-attributes[opt] identifier
///     objc-selector ':' objc-keyword-attributes[opt] identifier
///     ':' objc-type-name objc-keyword-attributes[opt] identifier
///     ':' objc-keyword-attributes[opt] identifier
///
///   objc-parmlist:
///     objc-parms objc-ellipsis[opt]
///
///   objc-parms:
///     objc-parms , parameter-declaration
///
///   objc-ellipsis:
///     , ...
///
///   objc-keyword-attributes:         [OBJC2]
///     __attribute__((unused))
///
Decl *Parser::ParseObjCMethodDecl(SourceLocation mLoc,
                                  tok::TokenKind mType,
                                  tok::ObjCKeywordKind MethodImplKind,
                                  bool MethodDefinition) {
  ParsingDeclRAIIObject PD(*this, ParsingDeclRAIIObject::NoParent);

  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteObjCMethodDecl(getCurScope(),
                                                        mType == tok::minus,
                                                        /*ReturnType=*/nullptr);
    return nullptr;
  }

  // Parse the return type if present.
  ParsedType ReturnType;
  ObjCDeclSpec DSRet;
  if (Tok.is(tok::l_paren))
    ReturnType =
        ParseObjCTypeName(DSRet, DeclaratorContext::ObjCResult, nullptr);

  // If attributes exist before the method, parse them.
  ParsedAttributes methodAttrs(AttrFactory);
  MaybeParseAttributes(PAKM_CXX11 | (getLangOpts().ObjC ? PAKM_GNU : 0),
                       methodAttrs);

  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteObjCMethodDecl(
        getCurScope(), mType == tok::minus, ReturnType);
    return nullptr;
  }

  // Now parse the selector.
  SourceLocation selLoc;
  IdentifierInfo *SelIdent = ParseObjCSelectorPiece(selLoc);

  // An unnamed colon is valid.
  if (!SelIdent && Tok.isNot(tok::colon)) { // missing selector name.
    Diag(Tok, diag::err_expected_selector_for_method)
      << SourceRange(mLoc, Tok.getLocation());
    // Skip until we get a ; or @.
    SkipUntil(tok::at, StopAtSemi | StopBeforeMatch);
    return nullptr;
  }

  SmallVector<DeclaratorChunk::ParamInfo, 8> CParamInfo;
  if (Tok.isNot(tok::colon)) {
    // If attributes exist after the method, parse them.
    MaybeParseAttributes(PAKM_CXX11 | (getLangOpts().ObjC ? PAKM_GNU : 0),
                         methodAttrs);

    Selector Sel = PP.getSelectorTable().getNullarySelector(SelIdent);
    Decl *Result = Actions.ObjC().ActOnMethodDeclaration(
        getCurScope(), mLoc, Tok.getLocation(), mType, DSRet, ReturnType,
        selLoc, Sel, nullptr, CParamInfo.data(), CParamInfo.size(), methodAttrs,
        MethodImplKind, false, MethodDefinition);
    PD.complete(Result);
    return Result;
  }

  SmallVector<const IdentifierInfo *, 12> KeyIdents;
  SmallVector<SourceLocation, 12> KeyLocs;
  SmallVector<SemaObjC::ObjCArgInfo, 12> ArgInfos;
  ParseScope PrototypeScope(this, Scope::FunctionPrototypeScope |
                            Scope::FunctionDeclarationScope | Scope::DeclScope);

  AttributePool allParamAttrs(AttrFactory);
  while (true) {
    ParsedAttributes paramAttrs(AttrFactory);
    SemaObjC::ObjCArgInfo ArgInfo;

    // Each iteration parses a single keyword argument.
    if (ExpectAndConsume(tok::colon))
      break;

    ArgInfo.Type = nullptr;
    if (Tok.is(tok::l_paren)) // Parse the argument type if present.
      ArgInfo.Type = ParseObjCTypeName(
          ArgInfo.DeclSpec, DeclaratorContext::ObjCParameter, &paramAttrs);

    // If attributes exist before the argument name, parse them.
    // Regardless, collect all the attributes we've parsed so far.
    MaybeParseAttributes(PAKM_CXX11 | (getLangOpts().ObjC ? PAKM_GNU : 0),
                         paramAttrs);
    ArgInfo.ArgAttrs = paramAttrs;

    // Code completion for the next piece of the selector.
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      KeyIdents.push_back(SelIdent);
      Actions.CodeCompletion().CodeCompleteObjCMethodDeclSelector(
          getCurScope(), mType == tok::minus,
          /*AtParameterName=*/true, ReturnType, KeyIdents);
      return nullptr;
    }

    if (expectIdentifier())
      break; // missing argument name.

    ArgInfo.Name = Tok.getIdentifierInfo();
    ArgInfo.NameLoc = Tok.getLocation();
    ConsumeToken(); // Eat the identifier.

    ArgInfos.push_back(ArgInfo);
    KeyIdents.push_back(SelIdent);
    KeyLocs.push_back(selLoc);

    // Make sure the attributes persist.
    allParamAttrs.takeAllFrom(paramAttrs.getPool());

    // Code completion for the next piece of the selector.
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteObjCMethodDeclSelector(
          getCurScope(), mType == tok::minus,
          /*AtParameterName=*/false, ReturnType, KeyIdents);
      return nullptr;
    }

    // Check for another keyword selector.
    SelIdent = ParseObjCSelectorPiece(selLoc);
    if (!SelIdent && Tok.isNot(tok::colon))
      break;
    if (!SelIdent) {
      SourceLocation ColonLoc = Tok.getLocation();
      if (PP.getLocForEndOfToken(ArgInfo.NameLoc) == ColonLoc) {
        Diag(ArgInfo.NameLoc, diag::warn_missing_selector_name) << ArgInfo.Name;
        Diag(ArgInfo.NameLoc, diag::note_missing_selector_name) << ArgInfo.Name;
        Diag(ColonLoc, diag::note_force_empty_selector_name) << ArgInfo.Name;
      }
    }
    // We have a selector or a colon, continue parsing.
  }

  bool isVariadic = false;
  bool cStyleParamWarned = false;
  // Parse the (optional) parameter list.
  while (Tok.is(tok::comma)) {
    ConsumeToken();
    if (Tok.is(tok::ellipsis)) {
      isVariadic = true;
      ConsumeToken();
      break;
    }
    if (!cStyleParamWarned) {
      Diag(Tok, diag::warn_cstyle_param);
      cStyleParamWarned = true;
    }
    DeclSpec DS(AttrFactory);
    ParsedTemplateInfo TemplateInfo;
    ParseDeclarationSpecifiers(DS, TemplateInfo);
    // Parse the declarator.
    Declarator ParmDecl(DS, ParsedAttributesView::none(),
                        DeclaratorContext::Prototype);
    ParseDeclarator(ParmDecl);
    const IdentifierInfo *ParmII = ParmDecl.getIdentifier();
    Decl *Param = Actions.ActOnParamDeclarator(getCurScope(), ParmDecl);
    CParamInfo.push_back(DeclaratorChunk::ParamInfo(ParmII,
                                                    ParmDecl.getIdentifierLoc(),
                                                    Param,
                                                    nullptr));
  }

  // FIXME: Add support for optional parameter list...
  // If attributes exist after the method, parse them.
  MaybeParseAttributes(PAKM_CXX11 | (getLangOpts().ObjC ? PAKM_GNU : 0),
                       methodAttrs);

  if (KeyIdents.size() == 0)
    return nullptr;

  Selector Sel = PP.getSelectorTable().getSelector(KeyIdents.size(),
                                                   &KeyIdents[0]);
  Decl *Result = Actions.ObjC().ActOnMethodDeclaration(
      getCurScope(), mLoc, Tok.getLocation(), mType, DSRet, ReturnType, KeyLocs,
      Sel, &ArgInfos[0], CParamInfo.data(), CParamInfo.size(), methodAttrs,
      MethodImplKind, isVariadic, MethodDefinition);

  PD.complete(Result);
  return Result;
}

///   objc-protocol-refs:
///     '<' identifier-list '>'
///
bool Parser::
ParseObjCProtocolReferences(SmallVectorImpl<Decl *> &Protocols,
                            SmallVectorImpl<SourceLocation> &ProtocolLocs,
                            bool WarnOnDeclarations, bool ForObjCContainer,
                            SourceLocation &LAngleLoc, SourceLocation &EndLoc,
                            bool consumeLastToken) {
  assert(Tok.is(tok::less) && "expected <");

  LAngleLoc = ConsumeToken(); // the "<"

  SmallVector<IdentifierLocPair, 8> ProtocolIdents;

  while (true) {
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteObjCProtocolReferences(
          ProtocolIdents);
      return true;
    }

    if (expectIdentifier()) {
      SkipUntil(tok::greater, StopAtSemi);
      return true;
    }
    ProtocolIdents.push_back(std::make_pair(Tok.getIdentifierInfo(),
                                       Tok.getLocation()));
    ProtocolLocs.push_back(Tok.getLocation());
    ConsumeToken();

    if (!TryConsumeToken(tok::comma))
      break;
  }

  // Consume the '>'.
  if (ParseGreaterThanInTemplateList(LAngleLoc, EndLoc, consumeLastToken,
                                     /*ObjCGenericList=*/false))
    return true;

  // Convert the list of protocols identifiers into a list of protocol decls.
  Actions.ObjC().FindProtocolDeclaration(WarnOnDeclarations, ForObjCContainer,
                                         ProtocolIdents, Protocols);
  return false;
}

TypeResult Parser::parseObjCProtocolQualifierType(SourceLocation &rAngleLoc) {
  assert(Tok.is(tok::less) && "Protocol qualifiers start with '<'");
  assert(getLangOpts().ObjC && "Protocol qualifiers only exist in Objective-C");

  SourceLocation lAngleLoc;
  SmallVector<Decl *, 8> protocols;
  SmallVector<SourceLocation, 8> protocolLocs;
  (void)ParseObjCProtocolReferences(protocols, protocolLocs, false, false,
                                    lAngleLoc, rAngleLoc,
                                    /*consumeLastToken=*/true);
  TypeResult result = Actions.ObjC().actOnObjCProtocolQualifierType(
      lAngleLoc, protocols, protocolLocs, rAngleLoc);
  if (result.isUsable()) {
    Diag(lAngleLoc, diag::warn_objc_protocol_qualifier_missing_id)
      << FixItHint::CreateInsertion(lAngleLoc, "id")
      << SourceRange(lAngleLoc, rAngleLoc);
  }

  return result;
}

/// Parse Objective-C type arguments or protocol qualifiers.
///
///   objc-type-arguments:
///     '<' type-name '...'[opt] (',' type-name '...'[opt])* '>'
///
void Parser::parseObjCTypeArgsOrProtocolQualifiers(
       ParsedType baseType,
       SourceLocation &typeArgsLAngleLoc,
       SmallVectorImpl<ParsedType> &typeArgs,
       SourceLocation &typeArgsRAngleLoc,
       SourceLocation &protocolLAngleLoc,
       SmallVectorImpl<Decl *> &protocols,
       SmallVectorImpl<SourceLocation> &protocolLocs,
       SourceLocation &protocolRAngleLoc,
       bool consumeLastToken,
       bool warnOnIncompleteProtocols) {
  assert(Tok.is(tok::less) && "Not at the start of type args or protocols");
  SourceLocation lAngleLoc = ConsumeToken();

  // Whether all of the elements we've parsed thus far are single
  // identifiers, which might be types or might be protocols.
  bool allSingleIdentifiers = true;
  SmallVector<IdentifierInfo *, 4> identifiers;
  SmallVectorImpl<SourceLocation> &identifierLocs = protocolLocs;

  // Parse a list of comma-separated identifiers, bailing out if we
  // see something different.
  do {
    // Parse a single identifier.
    if (Tok.is(tok::identifier) &&
        (NextToken().is(tok::comma) ||
         NextToken().is(tok::greater) ||
         NextToken().is(tok::greatergreater))) {
      identifiers.push_back(Tok.getIdentifierInfo());
      identifierLocs.push_back(ConsumeToken());
      continue;
    }

    if (Tok.is(tok::code_completion)) {
      // FIXME: Also include types here.
      SmallVector<IdentifierLocPair, 4> identifierLocPairs;
      for (unsigned i = 0, n = identifiers.size(); i != n; ++i) {
        identifierLocPairs.push_back(IdentifierLocPair(identifiers[i],
                                                       identifierLocs[i]));
      }

      QualType BaseT = Actions.GetTypeFromParser(baseType);
      cutOffParsing();
      if (!BaseT.isNull() && BaseT->acceptsObjCTypeParams()) {
        Actions.CodeCompletion().CodeCompleteOrdinaryName(
            getCurScope(), SemaCodeCompletion::PCC_Type);
      } else {
        Actions.CodeCompletion().CodeCompleteObjCProtocolReferences(
            identifierLocPairs);
      }
      return;
    }

    allSingleIdentifiers = false;
    break;
  } while (TryConsumeToken(tok::comma));

  // If we parsed an identifier list, semantic analysis sorts out
  // whether it refers to protocols or to type arguments.
  if (allSingleIdentifiers) {
    // Parse the closing '>'.
    SourceLocation rAngleLoc;
    (void)ParseGreaterThanInTemplateList(lAngleLoc, rAngleLoc, consumeLastToken,
                                         /*ObjCGenericList=*/true);

    // Let Sema figure out what we parsed.
    Actions.ObjC().actOnObjCTypeArgsOrProtocolQualifiers(
        getCurScope(), baseType, lAngleLoc, identifiers, identifierLocs,
        rAngleLoc, typeArgsLAngleLoc, typeArgs, typeArgsRAngleLoc,
        protocolLAngleLoc, protocols, protocolRAngleLoc,
        warnOnIncompleteProtocols);
    return;
  }

  // We parsed an identifier list but stumbled into non single identifiers, this
  // means we might (a) check that what we already parsed is a legitimate type
  // (not a protocol or unknown type) and (b) parse the remaining ones, which
  // must all be type args.

  // Convert the identifiers into type arguments.
  bool invalid = false;
  IdentifierInfo *foundProtocolId = nullptr, *foundValidTypeId = nullptr;
  SourceLocation foundProtocolSrcLoc, foundValidTypeSrcLoc;
  SmallVector<IdentifierInfo *, 2> unknownTypeArgs;
  SmallVector<SourceLocation, 2> unknownTypeArgsLoc;

  for (unsigned i = 0, n = identifiers.size(); i != n; ++i) {
    ParsedType typeArg
      = Actions.getTypeName(*identifiers[i], identifierLocs[i], getCurScope());
    if (typeArg) {
      DeclSpec DS(AttrFactory);
      const char *prevSpec = nullptr;
      unsigned diagID;
      DS.SetTypeSpecType(TST_typename, identifierLocs[i], prevSpec, diagID,
                         typeArg, Actions.getASTContext().getPrintingPolicy());

      // Form a declarator to turn this into a type.
      Declarator D(DS, ParsedAttributesView::none(),
                   DeclaratorContext::TypeName);
      TypeResult fullTypeArg = Actions.ActOnTypeName(D);
      if (fullTypeArg.isUsable()) {
        typeArgs.push_back(fullTypeArg.get());
        if (!foundValidTypeId) {
          foundValidTypeId = identifiers[i];
          foundValidTypeSrcLoc = identifierLocs[i];
        }
      } else {
        invalid = true;
        unknownTypeArgs.push_back(identifiers[i]);
        unknownTypeArgsLoc.push_back(identifierLocs[i]);
      }
    } else {
      invalid = true;
      if (!Actions.ObjC().LookupProtocol(identifiers[i], identifierLocs[i])) {
        unknownTypeArgs.push_back(identifiers[i]);
        unknownTypeArgsLoc.push_back(identifierLocs[i]);
      } else if (!foundProtocolId) {
        foundProtocolId = identifiers[i];
        foundProtocolSrcLoc = identifierLocs[i];
      }
    }
  }

  // Continue parsing type-names.
  do {
    Token CurTypeTok = Tok;
    TypeResult typeArg = ParseTypeName();

    // Consume the '...' for a pack expansion.
    SourceLocation ellipsisLoc;
    TryConsumeToken(tok::ellipsis, ellipsisLoc);
    if (typeArg.isUsable() && ellipsisLoc.isValid()) {
      typeArg = Actions.ActOnPackExpansion(typeArg.get(), ellipsisLoc);
    }

    if (typeArg.isUsable()) {
      typeArgs.push_back(typeArg.get());
      if (!foundValidTypeId) {
        foundValidTypeId = CurTypeTok.getIdentifierInfo();
        foundValidTypeSrcLoc = CurTypeTok.getLocation();
      }
    } else {
      invalid = true;
    }
  } while (TryConsumeToken(tok::comma));

  // Diagnose the mix between type args and protocols.
  if (foundProtocolId && foundValidTypeId)
    Actions.ObjC().DiagnoseTypeArgsAndProtocols(
        foundProtocolId, foundProtocolSrcLoc, foundValidTypeId,
        foundValidTypeSrcLoc);

  // Diagnose unknown arg types.
  ParsedType T;
  if (unknownTypeArgs.size())
    for (unsigned i = 0, e = unknownTypeArgsLoc.size(); i < e; ++i)
      Actions.DiagnoseUnknownTypeName(unknownTypeArgs[i], unknownTypeArgsLoc[i],
                                      getCurScope(), nullptr, T);

  // Parse the closing '>'.
  SourceLocation rAngleLoc;
  (void)ParseGreaterThanInTemplateList(lAngleLoc, rAngleLoc, consumeLastToken,
                                       /*ObjCGenericList=*/true);

  if (invalid) {
    typeArgs.clear();
    return;
  }

  // Record left/right angle locations.
  typeArgsLAngleLoc = lAngleLoc;
  typeArgsRAngleLoc = rAngleLoc;
}

void Parser::parseObjCTypeArgsAndProtocolQualifiers(
       ParsedType baseType,
       SourceLocation &typeArgsLAngleLoc,
       SmallVectorImpl<ParsedType> &typeArgs,
       SourceLocation &typeArgsRAngleLoc,
       SourceLocation &protocolLAngleLoc,
       SmallVectorImpl<Decl *> &protocols,
       SmallVectorImpl<SourceLocation> &protocolLocs,
       SourceLocation &protocolRAngleLoc,
       bool consumeLastToken) {
  assert(Tok.is(tok::less));

  // Parse the first angle-bracket-delimited clause.
  parseObjCTypeArgsOrProtocolQualifiers(baseType,
                                        typeArgsLAngleLoc,
                                        typeArgs,
                                        typeArgsRAngleLoc,
                                        protocolLAngleLoc,
                                        protocols,
                                        protocolLocs,
                                        protocolRAngleLoc,
                                        consumeLastToken,
                                        /*warnOnIncompleteProtocols=*/false);
  if (Tok.is(tok::eof)) // Nothing else to do here...
    return;

  // An Objective-C object pointer followed by type arguments
  // can then be followed again by a set of protocol references, e.g.,
  // \c NSArray<NSView><NSTextDelegate>
  if ((consumeLastToken && Tok.is(tok::less)) ||
      (!consumeLastToken && NextToken().is(tok::less))) {
    // If we aren't consuming the last token, the prior '>' is still hanging
    // there. Consume it before we parse the protocol qualifiers.
    if (!consumeLastToken)
      ConsumeToken();

    if (!protocols.empty()) {
      SkipUntilFlags skipFlags = SkipUntilFlags();
      if (!consumeLastToken)
        skipFlags = skipFlags | StopBeforeMatch;
      Diag(Tok, diag::err_objc_type_args_after_protocols)
        << SourceRange(protocolLAngleLoc, protocolRAngleLoc);
      SkipUntil(tok::greater, tok::greatergreater, skipFlags);
    } else {
      ParseObjCProtocolReferences(protocols, protocolLocs,
                                  /*WarnOnDeclarations=*/false,
                                  /*ForObjCContainer=*/false,
                                  protocolLAngleLoc, protocolRAngleLoc,
                                  consumeLastToken);
    }
  }
}

TypeResult Parser::parseObjCTypeArgsAndProtocolQualifiers(
             SourceLocation loc,
             ParsedType type,
             bool consumeLastToken,
             SourceLocation &endLoc) {
  assert(Tok.is(tok::less));
  SourceLocation typeArgsLAngleLoc;
  SmallVector<ParsedType, 4> typeArgs;
  SourceLocation typeArgsRAngleLoc;
  SourceLocation protocolLAngleLoc;
  SmallVector<Decl *, 4> protocols;
  SmallVector<SourceLocation, 4> protocolLocs;
  SourceLocation protocolRAngleLoc;

  // Parse type arguments and protocol qualifiers.
  parseObjCTypeArgsAndProtocolQualifiers(type, typeArgsLAngleLoc, typeArgs,
                                         typeArgsRAngleLoc, protocolLAngleLoc,
                                         protocols, protocolLocs,
                                         protocolRAngleLoc, consumeLastToken);

  if (Tok.is(tok::eof))
    return true; // Invalid type result.

  // Compute the location of the last token.
  if (consumeLastToken)
    endLoc = PrevTokLocation;
  else
    endLoc = Tok.getLocation();

  return Actions.ObjC().actOnObjCTypeArgsAndProtocolQualifiers(
      getCurScope(), loc, type, typeArgsLAngleLoc, typeArgs, typeArgsRAngleLoc,
      protocolLAngleLoc, protocols, protocolLocs, protocolRAngleLoc);
}

void Parser::HelperActionsForIvarDeclarations(
    ObjCContainerDecl *interfaceDecl, SourceLocation atLoc,
    BalancedDelimiterTracker &T, SmallVectorImpl<Decl *> &AllIvarDecls,
    bool RBraceMissing) {
  if (!RBraceMissing)
    T.consumeClose();

  assert(getObjCDeclContext() == interfaceDecl &&
         "Ivars should have interfaceDecl as their decl context");
  Actions.ActOnLastBitfield(T.getCloseLocation(), AllIvarDecls);
  // Call ActOnFields() even if we don't have any decls. This is useful
  // for code rewriting tools that need to be aware of the empty list.
  Actions.ActOnFields(getCurScope(), atLoc, interfaceDecl, AllIvarDecls,
                      T.getOpenLocation(), T.getCloseLocation(),
                      ParsedAttributesView());
}

///   objc-class-instance-variables:
///     '{' objc-instance-variable-decl-list[opt] '}'
///
///   objc-instance-variable-decl-list:
///     objc-visibility-spec
///     objc-instance-variable-decl ';'
///     ';'
///     objc-instance-variable-decl-list objc-visibility-spec
///     objc-instance-variable-decl-list objc-instance-variable-decl ';'
///     objc-instance-variable-decl-list static_assert-declaration
///     objc-instance-variable-decl-list ';'
///
///   objc-visibility-spec:
///     @private
///     @protected
///     @public
///     @package [OBJC2]
///
///   objc-instance-variable-decl:
///     struct-declaration
///
void Parser::ParseObjCClassInstanceVariables(ObjCContainerDecl *interfaceDecl,
                                             tok::ObjCKeywordKind visibility,
                                             SourceLocation atLoc) {
  assert(Tok.is(tok::l_brace) && "expected {");
  SmallVector<Decl *, 32> AllIvarDecls;

  ParseScope ClassScope(this, Scope::DeclScope | Scope::ClassScope);

  BalancedDelimiterTracker T(*this, tok::l_brace);
  T.consumeOpen();
  // While we still have something to read, read the instance variables.
  while (Tok.isNot(tok::r_brace) && !isEofOrEom()) {
    // Each iteration of this loop reads one objc-instance-variable-decl.

    // Check for extraneous top-level semicolon.
    if (Tok.is(tok::semi)) {
      ConsumeExtraSemi(InstanceVariableList);
      continue;
    }

    // Set the default visibility to private.
    if (TryConsumeToken(tok::at)) { // parse objc-visibility-spec
      if (Tok.is(tok::code_completion)) {
        cutOffParsing();
        Actions.CodeCompletion().CodeCompleteObjCAtVisibility(getCurScope());
        return;
      }

      switch (Tok.getObjCKeywordID()) {
      case tok::objc_private:
      case tok::objc_public:
      case tok::objc_protected:
      case tok::objc_package:
        visibility = Tok.getObjCKeywordID();
        ConsumeToken();
        continue;

      case tok::objc_end:
        Diag(Tok, diag::err_objc_unexpected_atend);
        Tok.setLocation(Tok.getLocation().getLocWithOffset(-1));
        Tok.setKind(tok::at);
        Tok.setLength(1);
        PP.EnterToken(Tok, /*IsReinject*/true);
        HelperActionsForIvarDeclarations(interfaceDecl, atLoc,
                                         T, AllIvarDecls, true);
        return;

      default:
        Diag(Tok, diag::err_objc_illegal_visibility_spec);
        continue;
      }
    }

    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteOrdinaryName(
          getCurScope(), SemaCodeCompletion::PCC_ObjCInstanceVariableList);
      return;
    }

    // This needs to duplicate a small amount of code from
    // ParseStructUnionBody() for things that should work in both
    // C struct and in Objective-C class instance variables.
    if (Tok.isOneOf(tok::kw_static_assert, tok::kw__Static_assert)) {
      SourceLocation DeclEnd;
      ParseStaticAssertDeclaration(DeclEnd);
      continue;
    }

    auto ObjCIvarCallback = [&](ParsingFieldDeclarator &FD) -> Decl * {
      assert(getObjCDeclContext() == interfaceDecl &&
             "Ivar should have interfaceDecl as its decl context");
      // Install the declarator into the interface decl.
      FD.D.setObjCIvar(true);
      Decl *Field = Actions.ObjC().ActOnIvar(
          getCurScope(), FD.D.getDeclSpec().getSourceRange().getBegin(), FD.D,
          FD.BitfieldSize, visibility);
      if (Field)
        AllIvarDecls.push_back(Field);
      FD.complete(Field);
      return Field;
    };

    // Parse all the comma separated declarators.
    ParsingDeclSpec DS(*this);
    ParseStructDeclaration(DS, ObjCIvarCallback);

    if (Tok.is(tok::semi)) {
      ConsumeToken();
    } else {
      Diag(Tok, diag::err_expected_semi_decl_list);
      // Skip to end of block or statement
      SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
    }
  }
  HelperActionsForIvarDeclarations(interfaceDecl, atLoc,
                                   T, AllIvarDecls, false);
}

///   objc-protocol-declaration:
///     objc-protocol-definition
///     objc-protocol-forward-reference
///
///   objc-protocol-definition:
///     \@protocol identifier
///       objc-protocol-refs[opt]
///       objc-interface-decl-list
///     \@end
///
///   objc-protocol-forward-reference:
///     \@protocol identifier-list ';'
///
///   "\@protocol identifier ;" should be resolved as "\@protocol
///   identifier-list ;": objc-interface-decl-list may not start with a
///   semicolon in the first alternative if objc-protocol-refs are omitted.
Parser::DeclGroupPtrTy
Parser::ParseObjCAtProtocolDeclaration(SourceLocation AtLoc,
                                       ParsedAttributes &attrs) {
  assert(Tok.isObjCAtKeyword(tok::objc_protocol) &&
         "ParseObjCAtProtocolDeclaration(): Expected @protocol");
  ConsumeToken(); // the "protocol" identifier

  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteObjCProtocolDecl(getCurScope());
    return nullptr;
  }

  MaybeSkipAttributes(tok::objc_protocol);

  if (expectIdentifier())
    return nullptr; // missing protocol name.
  // Save the protocol name, then consume it.
  IdentifierInfo *protocolName = Tok.getIdentifierInfo();
  SourceLocation nameLoc = ConsumeToken();

  if (TryConsumeToken(tok::semi)) { // forward declaration of one protocol.
    IdentifierLocPair ProtoInfo(protocolName, nameLoc);
    return Actions.ObjC().ActOnForwardProtocolDeclaration(AtLoc, ProtoInfo,
                                                          attrs);
  }

  CheckNestedObjCContexts(AtLoc);

  if (Tok.is(tok::comma)) { // list of forward declarations.
    SmallVector<IdentifierLocPair, 8> ProtocolRefs;
    ProtocolRefs.push_back(std::make_pair(protocolName, nameLoc));

    // Parse the list of forward declarations.
    while (true) {
      ConsumeToken(); // the ','
      if (expectIdentifier()) {
        SkipUntil(tok::semi);
        return nullptr;
      }
      ProtocolRefs.push_back(IdentifierLocPair(Tok.getIdentifierInfo(),
                                               Tok.getLocation()));
      ConsumeToken(); // the identifier

      if (Tok.isNot(tok::comma))
        break;
    }
    // Consume the ';'.
    if (ExpectAndConsume(tok::semi, diag::err_expected_after, "@protocol"))
      return nullptr;

    return Actions.ObjC().ActOnForwardProtocolDeclaration(AtLoc, ProtocolRefs,
                                                          attrs);
  }

  // Last, and definitely not least, parse a protocol declaration.
  SourceLocation LAngleLoc, EndProtoLoc;

  SmallVector<Decl *, 8> ProtocolRefs;
  SmallVector<SourceLocation, 8> ProtocolLocs;
  if (Tok.is(tok::less) &&
      ParseObjCProtocolReferences(ProtocolRefs, ProtocolLocs, false, true,
                                  LAngleLoc, EndProtoLoc,
                                  /*consumeLastToken=*/true))
    return nullptr;

  SkipBodyInfo SkipBody;
  ObjCProtocolDecl *ProtoType = Actions.ObjC().ActOnStartProtocolInterface(
      AtLoc, protocolName, nameLoc, ProtocolRefs.data(), ProtocolRefs.size(),
      ProtocolLocs.data(), EndProtoLoc, attrs, &SkipBody);

  ParseObjCInterfaceDeclList(tok::objc_protocol, ProtoType);
  if (SkipBody.CheckSameAsPrevious) {
    auto *PreviousDef = cast<ObjCProtocolDecl>(SkipBody.Previous);
    if (Actions.ActOnDuplicateODRHashDefinition(ProtoType, PreviousDef)) {
      ProtoType->mergeDuplicateDefinitionWithCommon(
          PreviousDef->getDefinition());
    } else {
      ODRDiagsEmitter DiagsEmitter(Diags, Actions.getASTContext(),
                                   getPreprocessor().getLangOpts());
      DiagsEmitter.diagnoseMismatch(PreviousDef, ProtoType);
    }
  }
  return Actions.ConvertDeclToDeclGroup(ProtoType);
}

///   objc-implementation:
///     objc-class-implementation-prologue
///     objc-category-implementation-prologue
///
///   objc-class-implementation-prologue:
///     @implementation identifier objc-superclass[opt]
///       objc-class-instance-variables[opt]
///
///   objc-category-implementation-prologue:
///     @implementation identifier ( identifier )
Parser::DeclGroupPtrTy
Parser::ParseObjCAtImplementationDeclaration(SourceLocation AtLoc,
                                             ParsedAttributes &Attrs) {
  assert(Tok.isObjCAtKeyword(tok::objc_implementation) &&
         "ParseObjCAtImplementationDeclaration(): Expected @implementation");
  CheckNestedObjCContexts(AtLoc);
  ConsumeToken(); // the "implementation" identifier

  // Code completion after '@implementation'.
  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteObjCImplementationDecl(getCurScope());
    return nullptr;
  }

  MaybeSkipAttributes(tok::objc_implementation);

  if (expectIdentifier())
    return nullptr; // missing class or category name.
  // We have a class or category name - consume it.
  IdentifierInfo *nameId = Tok.getIdentifierInfo();
  SourceLocation nameLoc = ConsumeToken(); // consume class or category name
  ObjCImplDecl *ObjCImpDecl = nullptr;

  // Neither a type parameter list nor a list of protocol references is
  // permitted here. Parse and diagnose them.
  if (Tok.is(tok::less)) {
    SourceLocation lAngleLoc, rAngleLoc;
    SmallVector<IdentifierLocPair, 8> protocolIdents;
    SourceLocation diagLoc = Tok.getLocation();
    ObjCTypeParamListScope typeParamScope(Actions, getCurScope());
    if (parseObjCTypeParamListOrProtocolRefs(typeParamScope, lAngleLoc,
                                             protocolIdents, rAngleLoc)) {
      Diag(diagLoc, diag::err_objc_parameterized_implementation)
        << SourceRange(diagLoc, PrevTokLocation);
    } else if (lAngleLoc.isValid()) {
      Diag(lAngleLoc, diag::err_unexpected_protocol_qualifier)
        << FixItHint::CreateRemoval(SourceRange(lAngleLoc, rAngleLoc));
    }
  }

  if (Tok.is(tok::l_paren)) {
    // we have a category implementation.
    ConsumeParen();
    SourceLocation categoryLoc, rparenLoc;
    IdentifierInfo *categoryId = nullptr;

    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteObjCImplementationCategory(
          getCurScope(), nameId, nameLoc);
      return nullptr;
    }

    if (Tok.is(tok::identifier)) {
      categoryId = Tok.getIdentifierInfo();
      categoryLoc = ConsumeToken();
    } else {
      Diag(Tok, diag::err_expected)
          << tok::identifier; // missing category name.
      return nullptr;
    }
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      SkipUntil(tok::r_paren); // don't stop at ';'
      return nullptr;
    }
    rparenLoc = ConsumeParen();
    if (Tok.is(tok::less)) { // we have illegal '<' try to recover
      Diag(Tok, diag::err_unexpected_protocol_qualifier);
      SourceLocation protocolLAngleLoc, protocolRAngleLoc;
      SmallVector<Decl *, 4> protocols;
      SmallVector<SourceLocation, 4> protocolLocs;
      (void)ParseObjCProtocolReferences(protocols, protocolLocs,
                                        /*warnOnIncompleteProtocols=*/false,
                                        /*ForObjCContainer=*/false,
                                        protocolLAngleLoc, protocolRAngleLoc,
                                        /*consumeLastToken=*/true);
    }
    ObjCImpDecl = Actions.ObjC().ActOnStartCategoryImplementation(
        AtLoc, nameId, nameLoc, categoryId, categoryLoc, Attrs);

  } else {
    // We have a class implementation
    SourceLocation superClassLoc;
    IdentifierInfo *superClassId = nullptr;
    if (TryConsumeToken(tok::colon)) {
      // We have a super class
      if (expectIdentifier())
        return nullptr; // missing super class name.
      superClassId = Tok.getIdentifierInfo();
      superClassLoc = ConsumeToken(); // Consume super class name
    }
    ObjCImpDecl = Actions.ObjC().ActOnStartClassImplementation(
        AtLoc, nameId, nameLoc, superClassId, superClassLoc, Attrs);

    if (Tok.is(tok::l_brace)) // we have ivars
      ParseObjCClassInstanceVariables(ObjCImpDecl, tok::objc_private, AtLoc);
    else if (Tok.is(tok::less)) { // we have illegal '<' try to recover
      Diag(Tok, diag::err_unexpected_protocol_qualifier);

      SourceLocation protocolLAngleLoc, protocolRAngleLoc;
      SmallVector<Decl *, 4> protocols;
      SmallVector<SourceLocation, 4> protocolLocs;
      (void)ParseObjCProtocolReferences(protocols, protocolLocs,
                                        /*warnOnIncompleteProtocols=*/false,
                                        /*ForObjCContainer=*/false,
                                        protocolLAngleLoc, protocolRAngleLoc,
                                        /*consumeLastToken=*/true);
    }
  }
  assert(ObjCImpDecl);

  SmallVector<Decl *, 8> DeclsInGroup;

  {
    ObjCImplParsingDataRAII ObjCImplParsing(*this, ObjCImpDecl);
    while (!ObjCImplParsing.isFinished() && !isEofOrEom()) {
      ParsedAttributes DeclAttrs(AttrFactory);
      MaybeParseCXX11Attributes(DeclAttrs);
      ParsedAttributes EmptyDeclSpecAttrs(AttrFactory);
      if (DeclGroupPtrTy DGP =
              ParseExternalDeclaration(DeclAttrs, EmptyDeclSpecAttrs)) {
        DeclGroupRef DG = DGP.get();
        DeclsInGroup.append(DG.begin(), DG.end());
      }
    }
  }

  return Actions.ObjC().ActOnFinishObjCImplementation(ObjCImpDecl,
                                                      DeclsInGroup);
}

Parser::DeclGroupPtrTy
Parser::ParseObjCAtEndDeclaration(SourceRange atEnd) {
  assert(Tok.isObjCAtKeyword(tok::objc_end) &&
         "ParseObjCAtEndDeclaration(): Expected @end");
  ConsumeToken(); // the "end" identifier
  if (CurParsedObjCImpl)
    CurParsedObjCImpl->finish(atEnd);
  else
    // missing @implementation
    Diag(atEnd.getBegin(), diag::err_expected_objc_container);
  return nullptr;
}

Parser::ObjCImplParsingDataRAII::~ObjCImplParsingDataRAII() {
  if (!Finished) {
    finish(P.Tok.getLocation());
    if (P.isEofOrEom()) {
      P.Diag(P.Tok, diag::err_objc_missing_end)
          << FixItHint::CreateInsertion(P.Tok.getLocation(), "\n@end\n");
      P.Diag(Dcl->getBeginLoc(), diag::note_objc_container_start)
          << SemaObjC::OCK_Implementation;
    }
  }
  P.CurParsedObjCImpl = nullptr;
  assert(LateParsedObjCMethods.empty());
}

void Parser::ObjCImplParsingDataRAII::finish(SourceRange AtEnd) {
  assert(!Finished);
  P.Actions.ObjC().DefaultSynthesizeProperties(P.getCurScope(), Dcl,
                                               AtEnd.getBegin());
  for (size_t i = 0; i < LateParsedObjCMethods.size(); ++i)
    P.ParseLexedObjCMethodDefs(*LateParsedObjCMethods[i],
                               true/*Methods*/);

  P.Actions.ObjC().ActOnAtEnd(P.getCurScope(), AtEnd);

  if (HasCFunction)
    for (size_t i = 0; i < LateParsedObjCMethods.size(); ++i)
      P.ParseLexedObjCMethodDefs(*LateParsedObjCMethods[i],
                                 false/*c-functions*/);

  /// Clear and free the cached objc methods.
  for (LateParsedObjCMethodContainer::iterator
         I = LateParsedObjCMethods.begin(),
         E = LateParsedObjCMethods.end(); I != E; ++I)
    delete *I;
  LateParsedObjCMethods.clear();

  Finished = true;
}

///   compatibility-alias-decl:
///     @compatibility_alias alias-name  class-name ';'
///
Decl *Parser::ParseObjCAtAliasDeclaration(SourceLocation atLoc) {
  assert(Tok.isObjCAtKeyword(tok::objc_compatibility_alias) &&
         "ParseObjCAtAliasDeclaration(): Expected @compatibility_alias");
  ConsumeToken(); // consume compatibility_alias
  if (expectIdentifier())
    return nullptr;
  IdentifierInfo *aliasId = Tok.getIdentifierInfo();
  SourceLocation aliasLoc = ConsumeToken(); // consume alias-name
  if (expectIdentifier())
    return nullptr;
  IdentifierInfo *classId = Tok.getIdentifierInfo();
  SourceLocation classLoc = ConsumeToken(); // consume class-name;
  ExpectAndConsume(tok::semi, diag::err_expected_after, "@compatibility_alias");
  return Actions.ObjC().ActOnCompatibilityAlias(atLoc, aliasId, aliasLoc,
                                                classId, classLoc);
}

///   property-synthesis:
///     @synthesize property-ivar-list ';'
///
///   property-ivar-list:
///     property-ivar
///     property-ivar-list ',' property-ivar
///
///   property-ivar:
///     identifier
///     identifier '=' identifier
///
Decl *Parser::ParseObjCPropertySynthesize(SourceLocation atLoc) {
  assert(Tok.isObjCAtKeyword(tok::objc_synthesize) &&
         "ParseObjCPropertySynthesize(): Expected '@synthesize'");
  ConsumeToken(); // consume synthesize

  while (true) {
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteObjCPropertyDefinition(
          getCurScope());
      return nullptr;
    }

    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_synthesized_property_name);
      SkipUntil(tok::semi);
      return nullptr;
    }

    IdentifierInfo *propertyIvar = nullptr;
    IdentifierInfo *propertyId = Tok.getIdentifierInfo();
    SourceLocation propertyLoc = ConsumeToken(); // consume property name
    SourceLocation propertyIvarLoc;
    if (TryConsumeToken(tok::equal)) {
      // property '=' ivar-name
      if (Tok.is(tok::code_completion)) {
        cutOffParsing();
        Actions.CodeCompletion().CodeCompleteObjCPropertySynthesizeIvar(
            getCurScope(), propertyId);
        return nullptr;
      }

      if (expectIdentifier())
        break;
      propertyIvar = Tok.getIdentifierInfo();
      propertyIvarLoc = ConsumeToken(); // consume ivar-name
    }
    Actions.ObjC().ActOnPropertyImplDecl(
        getCurScope(), atLoc, propertyLoc, true, propertyId, propertyIvar,
        propertyIvarLoc, ObjCPropertyQueryKind::OBJC_PR_query_unknown);
    if (Tok.isNot(tok::comma))
      break;
    ConsumeToken(); // consume ','
  }
  ExpectAndConsume(tok::semi, diag::err_expected_after, "@synthesize");
  return nullptr;
}

///   property-dynamic:
///     @dynamic  property-list
///
///   property-list:
///     identifier
///     property-list ',' identifier
///
Decl *Parser::ParseObjCPropertyDynamic(SourceLocation atLoc) {
  assert(Tok.isObjCAtKeyword(tok::objc_dynamic) &&
         "ParseObjCPropertyDynamic(): Expected '@dynamic'");
  ConsumeToken(); // consume dynamic

  bool isClassProperty = false;
  if (Tok.is(tok::l_paren)) {
    ConsumeParen();
    const IdentifierInfo *II = Tok.getIdentifierInfo();

    if (!II) {
      Diag(Tok, diag::err_objc_expected_property_attr) << II;
      SkipUntil(tok::r_paren, StopAtSemi);
    } else {
      SourceLocation AttrName = ConsumeToken(); // consume attribute name
      if (II->isStr("class")) {
        isClassProperty = true;
        if (Tok.isNot(tok::r_paren)) {
          Diag(Tok, diag::err_expected) << tok::r_paren;
          SkipUntil(tok::r_paren, StopAtSemi);
        } else
          ConsumeParen();
      } else {
        Diag(AttrName, diag::err_objc_expected_property_attr) << II;
        SkipUntil(tok::r_paren, StopAtSemi);
      }
    }
  }

  while (true) {
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteObjCPropertyDefinition(
          getCurScope());
      return nullptr;
    }

    if (expectIdentifier()) {
      SkipUntil(tok::semi);
      return nullptr;
    }

    IdentifierInfo *propertyId = Tok.getIdentifierInfo();
    SourceLocation propertyLoc = ConsumeToken(); // consume property name
    Actions.ObjC().ActOnPropertyImplDecl(
        getCurScope(), atLoc, propertyLoc, false, propertyId, nullptr,
        SourceLocation(),
        isClassProperty ? ObjCPropertyQueryKind::OBJC_PR_query_class
                        : ObjCPropertyQueryKind::OBJC_PR_query_unknown);

    if (Tok.isNot(tok::comma))
      break;
    ConsumeToken(); // consume ','
  }
  ExpectAndConsume(tok::semi, diag::err_expected_after, "@dynamic");
  return nullptr;
}

///  objc-throw-statement:
///    throw expression[opt];
///
StmtResult Parser::ParseObjCThrowStmt(SourceLocation atLoc) {
  ExprResult Res;
  ConsumeToken(); // consume throw
  if (Tok.isNot(tok::semi)) {
    Res = ParseExpression();
    if (Res.isInvalid()) {
      SkipUntil(tok::semi);
      return StmtError();
    }
  }
  // consume ';'
  ExpectAndConsume(tok::semi, diag::err_expected_after, "@throw");
  return Actions.ObjC().ActOnObjCAtThrowStmt(atLoc, Res.get(), getCurScope());
}

/// objc-synchronized-statement:
///   @synchronized '(' expression ')' compound-statement
///
StmtResult
Parser::ParseObjCSynchronizedStmt(SourceLocation atLoc) {
  ConsumeToken(); // consume synchronized
  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen_after) << "@synchronized";
    return StmtError();
  }

  // The operand is surrounded with parentheses.
  ConsumeParen();  // '('
  ExprResult operand(ParseExpression());

  if (Tok.is(tok::r_paren)) {
    ConsumeParen();  // ')'
  } else {
    if (!operand.isInvalid())
      Diag(Tok, diag::err_expected) << tok::r_paren;

    // Skip forward until we see a left brace, but don't consume it.
    SkipUntil(tok::l_brace, StopAtSemi | StopBeforeMatch);
  }

  // Require a compound statement.
  if (Tok.isNot(tok::l_brace)) {
    if (!operand.isInvalid())
      Diag(Tok, diag::err_expected) << tok::l_brace;
    return StmtError();
  }

  // Check the @synchronized operand now.
  if (!operand.isInvalid())
    operand =
        Actions.ObjC().ActOnObjCAtSynchronizedOperand(atLoc, operand.get());

  // Parse the compound statement within a new scope.
  ParseScope bodyScope(this, Scope::DeclScope | Scope::CompoundStmtScope);
  StmtResult body(ParseCompoundStatementBody());
  bodyScope.Exit();

  // If there was a semantic or parse error earlier with the
  // operand, fail now.
  if (operand.isInvalid())
    return StmtError();

  if (body.isInvalid())
    body = Actions.ActOnNullStmt(Tok.getLocation());

  return Actions.ObjC().ActOnObjCAtSynchronizedStmt(atLoc, operand.get(),
                                                    body.get());
}

///  objc-try-catch-statement:
///    @try compound-statement objc-catch-list[opt]
///    @try compound-statement objc-catch-list[opt] @finally compound-statement
///
///  objc-catch-list:
///    @catch ( parameter-declaration ) compound-statement
///    objc-catch-list @catch ( catch-parameter-declaration ) compound-statement
///  catch-parameter-declaration:
///     parameter-declaration
///     '...' [OBJC2]
///
StmtResult Parser::ParseObjCTryStmt(SourceLocation atLoc) {
  bool catch_or_finally_seen = false;

  ConsumeToken(); // consume try
  if (Tok.isNot(tok::l_brace)) {
    Diag(Tok, diag::err_expected) << tok::l_brace;
    return StmtError();
  }
  StmtVector CatchStmts;
  StmtResult FinallyStmt;
  ParseScope TryScope(this, Scope::DeclScope | Scope::CompoundStmtScope);
  StmtResult TryBody(ParseCompoundStatementBody());
  TryScope.Exit();
  if (TryBody.isInvalid())
    TryBody = Actions.ActOnNullStmt(Tok.getLocation());

  while (Tok.is(tok::at)) {
    // At this point, we need to lookahead to determine if this @ is the start
    // of an @catch or @finally.  We don't want to consume the @ token if this
    // is an @try or @encode or something else.
    Token AfterAt = GetLookAheadToken(1);
    if (!AfterAt.isObjCAtKeyword(tok::objc_catch) &&
        !AfterAt.isObjCAtKeyword(tok::objc_finally))
      break;

    SourceLocation AtCatchFinallyLoc = ConsumeToken();
    if (Tok.isObjCAtKeyword(tok::objc_catch)) {
      Decl *FirstPart = nullptr;
      ConsumeToken(); // consume catch
      if (Tok.is(tok::l_paren)) {
        ConsumeParen();
        ParseScope CatchScope(this, Scope::DeclScope |
                                        Scope::CompoundStmtScope |
                                        Scope::AtCatchScope);
        if (Tok.isNot(tok::ellipsis)) {
          DeclSpec DS(AttrFactory);
          ParsedTemplateInfo TemplateInfo;
          ParseDeclarationSpecifiers(DS, TemplateInfo);
          Declarator ParmDecl(DS, ParsedAttributesView::none(),
                              DeclaratorContext::ObjCCatch);
          ParseDeclarator(ParmDecl);

          // Inform the actions module about the declarator, so it
          // gets added to the current scope.
          FirstPart =
              Actions.ObjC().ActOnObjCExceptionDecl(getCurScope(), ParmDecl);
        } else
          ConsumeToken(); // consume '...'

        SourceLocation RParenLoc;

        if (Tok.is(tok::r_paren))
          RParenLoc = ConsumeParen();
        else // Skip over garbage, until we get to ')'.  Eat the ')'.
          SkipUntil(tok::r_paren, StopAtSemi);

        StmtResult CatchBody(true);
        if (Tok.is(tok::l_brace))
          CatchBody = ParseCompoundStatementBody();
        else
          Diag(Tok, diag::err_expected) << tok::l_brace;
        if (CatchBody.isInvalid())
          CatchBody = Actions.ActOnNullStmt(Tok.getLocation());

        StmtResult Catch = Actions.ObjC().ActOnObjCAtCatchStmt(
            AtCatchFinallyLoc, RParenLoc, FirstPart, CatchBody.get());
        if (!Catch.isInvalid())
          CatchStmts.push_back(Catch.get());

      } else {
        Diag(AtCatchFinallyLoc, diag::err_expected_lparen_after)
          << "@catch clause";
        return StmtError();
      }
      catch_or_finally_seen = true;
    } else {
      assert(Tok.isObjCAtKeyword(tok::objc_finally) && "Lookahead confused?");
      ConsumeToken(); // consume finally
      ParseScope FinallyScope(this,
                              Scope::DeclScope | Scope::CompoundStmtScope);

      bool ShouldCapture =
          getTargetInfo().getTriple().isWindowsMSVCEnvironment();
      if (ShouldCapture)
        Actions.ActOnCapturedRegionStart(Tok.getLocation(), getCurScope(),
                                         CR_ObjCAtFinally, 1);

      StmtResult FinallyBody(true);
      if (Tok.is(tok::l_brace))
        FinallyBody = ParseCompoundStatementBody();
      else
        Diag(Tok, diag::err_expected) << tok::l_brace;

      if (FinallyBody.isInvalid()) {
        FinallyBody = Actions.ActOnNullStmt(Tok.getLocation());
        if (ShouldCapture)
          Actions.ActOnCapturedRegionError();
      } else if (ShouldCapture) {
        FinallyBody = Actions.ActOnCapturedRegionEnd(FinallyBody.get());
      }

      FinallyStmt = Actions.ObjC().ActOnObjCAtFinallyStmt(AtCatchFinallyLoc,
                                                          FinallyBody.get());
      catch_or_finally_seen = true;
      break;
    }
  }
  if (!catch_or_finally_seen) {
    Diag(atLoc, diag::err_missing_catch_finally);
    return StmtError();
  }

  return Actions.ObjC().ActOnObjCAtTryStmt(atLoc, TryBody.get(), CatchStmts,
                                           FinallyStmt.get());
}

/// objc-autoreleasepool-statement:
///   @autoreleasepool compound-statement
///
StmtResult
Parser::ParseObjCAutoreleasePoolStmt(SourceLocation atLoc) {
  ConsumeToken(); // consume autoreleasepool
  if (Tok.isNot(tok::l_brace)) {
    Diag(Tok, diag::err_expected) << tok::l_brace;
    return StmtError();
  }
  // Enter a scope to hold everything within the compound stmt.  Compound
  // statements can always hold declarations.
  ParseScope BodyScope(this, Scope::DeclScope | Scope::CompoundStmtScope);

  StmtResult AutoreleasePoolBody(ParseCompoundStatementBody());

  BodyScope.Exit();
  if (AutoreleasePoolBody.isInvalid())
    AutoreleasePoolBody = Actions.ActOnNullStmt(Tok.getLocation());
  return Actions.ObjC().ActOnObjCAutoreleasePoolStmt(atLoc,
                                                     AutoreleasePoolBody.get());
}

/// StashAwayMethodOrFunctionBodyTokens -  Consume the tokens and store them
/// for later parsing.
void Parser::StashAwayMethodOrFunctionBodyTokens(Decl *MDecl) {
  if (SkipFunctionBodies && (!MDecl || Actions.canSkipFunctionBody(MDecl)) &&
      trySkippingFunctionBody()) {
    Actions.ActOnSkippedFunctionBody(MDecl);
    return;
  }

  LexedMethod* LM = new LexedMethod(this, MDecl);
  CurParsedObjCImpl->LateParsedObjCMethods.push_back(LM);
  CachedTokens &Toks = LM->Toks;
  // Begin by storing the '{' or 'try' or ':' token.
  Toks.push_back(Tok);
  if (Tok.is(tok::kw_try)) {
    ConsumeToken();
    if (Tok.is(tok::colon)) {
      Toks.push_back(Tok);
      ConsumeToken();
      while (Tok.isNot(tok::l_brace)) {
        ConsumeAndStoreUntil(tok::l_paren, Toks, /*StopAtSemi=*/false);
        ConsumeAndStoreUntil(tok::r_paren, Toks, /*StopAtSemi=*/false);
      }
    }
    Toks.push_back(Tok); // also store '{'
  }
  else if (Tok.is(tok::colon)) {
    ConsumeToken();
    // FIXME: This is wrong, due to C++11 braced initialization.
    while (Tok.isNot(tok::l_brace)) {
      ConsumeAndStoreUntil(tok::l_paren, Toks, /*StopAtSemi=*/false);
      ConsumeAndStoreUntil(tok::r_paren, Toks, /*StopAtSemi=*/false);
    }
    Toks.push_back(Tok); // also store '{'
  }
  ConsumeBrace();
  // Consume everything up to (and including) the matching right brace.
  ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/false);
  while (Tok.is(tok::kw_catch)) {
    ConsumeAndStoreUntil(tok::l_brace, Toks, /*StopAtSemi=*/false);
    ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/false);
  }
}

///   objc-method-def: objc-method-proto ';'[opt] '{' body '}'
///
Decl *Parser::ParseObjCMethodDefinition() {
  Decl *MDecl = ParseObjCMethodPrototype();

  PrettyDeclStackTraceEntry CrashInfo(Actions.Context, MDecl, Tok.getLocation(),
                                      "parsing Objective-C method");

  // parse optional ';'
  if (Tok.is(tok::semi)) {
    if (CurParsedObjCImpl) {
      Diag(Tok, diag::warn_semicolon_before_method_body)
        << FixItHint::CreateRemoval(Tok.getLocation());
    }
    ConsumeToken();
  }

  // We should have an opening brace now.
  if (Tok.isNot(tok::l_brace)) {
    Diag(Tok, diag::err_expected_method_body);

    // Skip over garbage, until we get to '{'.  Don't eat the '{'.
    SkipUntil(tok::l_brace, StopAtSemi | StopBeforeMatch);

    // If we didn't find the '{', bail out.
    if (Tok.isNot(tok::l_brace))
      return nullptr;
  }

  if (!MDecl) {
    ConsumeBrace();
    SkipUntil(tok::r_brace);
    return nullptr;
  }

  // Allow the rest of sema to find private method decl implementations.
  Actions.ObjC().AddAnyMethodToGlobalPool(MDecl);
  assert (CurParsedObjCImpl
          && "ParseObjCMethodDefinition - Method out of @implementation");
  // Consume the tokens and store them for later parsing.
  StashAwayMethodOrFunctionBodyTokens(MDecl);
  return MDecl;
}

StmtResult Parser::ParseObjCAtStatement(SourceLocation AtLoc,
                                        ParsedStmtContext StmtCtx) {
  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteObjCAtStatement(getCurScope());
    return StmtError();
  }

  if (Tok.isObjCAtKeyword(tok::objc_try))
    return ParseObjCTryStmt(AtLoc);

  if (Tok.isObjCAtKeyword(tok::objc_throw))
    return ParseObjCThrowStmt(AtLoc);

  if (Tok.isObjCAtKeyword(tok::objc_synchronized))
    return ParseObjCSynchronizedStmt(AtLoc);

  if (Tok.isObjCAtKeyword(tok::objc_autoreleasepool))
    return ParseObjCAutoreleasePoolStmt(AtLoc);

  if (Tok.isObjCAtKeyword(tok::objc_import) &&
      getLangOpts().DebuggerSupport) {
    SkipUntil(tok::semi);
    return Actions.ActOnNullStmt(Tok.getLocation());
  }

  ExprStatementTokLoc = AtLoc;
  ExprResult Res(ParseExpressionWithLeadingAt(AtLoc));
  if (Res.isInvalid()) {
    // If the expression is invalid, skip ahead to the next semicolon. Not
    // doing this opens us up to the possibility of infinite loops if
    // ParseExpression does not consume any tokens.
    SkipUntil(tok::semi);
    return StmtError();
  }

  // Otherwise, eat the semicolon.
  ExpectAndConsumeSemi(diag::err_expected_semi_after_expr);
  return handleExprStmt(Res, StmtCtx);
}

ExprResult Parser::ParseObjCAtExpression(SourceLocation AtLoc) {
  switch (Tok.getKind()) {
  case tok::code_completion:
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteObjCAtExpression(getCurScope());
    return ExprError();

  case tok::minus:
  case tok::plus: {
    tok::TokenKind Kind = Tok.getKind();
    SourceLocation OpLoc = ConsumeToken();

    if (!Tok.is(tok::numeric_constant)) {
      const char *Symbol = nullptr;
      switch (Kind) {
      case tok::minus: Symbol = "-"; break;
      case tok::plus: Symbol = "+"; break;
      default: llvm_unreachable("missing unary operator case");
      }
      Diag(Tok, diag::err_nsnumber_nonliteral_unary)
        << Symbol;
      return ExprError();
    }

    ExprResult Lit(Actions.ActOnNumericConstant(Tok));
    if (Lit.isInvalid()) {
      return Lit;
    }
    ConsumeToken(); // Consume the literal token.

    Lit = Actions.ActOnUnaryOp(getCurScope(), OpLoc, Kind, Lit.get());
    if (Lit.isInvalid())
      return Lit;

    return ParsePostfixExpressionSuffix(
        Actions.ObjC().BuildObjCNumericLiteral(AtLoc, Lit.get()));
  }

  case tok::string_literal:    // primary-expression: string-literal
  case tok::wide_string_literal:
    return ParsePostfixExpressionSuffix(ParseObjCStringLiteral(AtLoc));

  case tok::char_constant:
    return ParsePostfixExpressionSuffix(ParseObjCCharacterLiteral(AtLoc));

  case tok::numeric_constant:
    return ParsePostfixExpressionSuffix(ParseObjCNumericLiteral(AtLoc));

  case tok::kw_true:  // Objective-C++, etc.
  case tok::kw___objc_yes: // c/c++/objc/objc++ __objc_yes
    return ParsePostfixExpressionSuffix(ParseObjCBooleanLiteral(AtLoc, true));
  case tok::kw_false: // Objective-C++, etc.
  case tok::kw___objc_no: // c/c++/objc/objc++ __objc_no
    return ParsePostfixExpressionSuffix(ParseObjCBooleanLiteral(AtLoc, false));

  case tok::l_square:
    // Objective-C array literal
    return ParsePostfixExpressionSuffix(ParseObjCArrayLiteral(AtLoc));

  case tok::l_brace:
    // Objective-C dictionary literal
    return ParsePostfixExpressionSuffix(ParseObjCDictionaryLiteral(AtLoc));

  case tok::l_paren:
    // Objective-C boxed expression
    return ParsePostfixExpressionSuffix(ParseObjCBoxedExpr(AtLoc));

  default:
    if (Tok.getIdentifierInfo() == nullptr)
      return ExprError(Diag(AtLoc, diag::err_unexpected_at));

    switch (Tok.getIdentifierInfo()->getObjCKeywordID()) {
    case tok::objc_encode:
      return ParsePostfixExpressionSuffix(ParseObjCEncodeExpression(AtLoc));
    case tok::objc_protocol:
      return ParsePostfixExpressionSuffix(ParseObjCProtocolExpression(AtLoc));
    case tok::objc_selector:
      return ParsePostfixExpressionSuffix(ParseObjCSelectorExpression(AtLoc));
    case tok::objc_available:
      return ParseAvailabilityCheckExpr(AtLoc);
      default: {
        const char *str = nullptr;
        // Only provide the @try/@finally/@autoreleasepool fixit when we're sure
        // that this is a proper statement where such directives could actually
        // occur.
        if (GetLookAheadToken(1).is(tok::l_brace) &&
            ExprStatementTokLoc == AtLoc) {
          char ch = Tok.getIdentifierInfo()->getNameStart()[0];
          str =
            ch == 't' ? "try"
                      : (ch == 'f' ? "finally"
                                   : (ch == 'a' ? "autoreleasepool" : nullptr));
        }
        if (str) {
          SourceLocation kwLoc = Tok.getLocation();
          return ExprError(Diag(AtLoc, diag::err_unexpected_at) <<
                             FixItHint::CreateReplacement(kwLoc, str));
        }
        else
          return ExprError(Diag(AtLoc, diag::err_unexpected_at));
      }
    }
  }
}

/// Parse the receiver of an Objective-C++ message send.
///
/// This routine parses the receiver of a message send in
/// Objective-C++ either as a type or as an expression. Note that this
/// routine must not be called to parse a send to 'super', since it
/// has no way to return such a result.
///
/// \param IsExpr Whether the receiver was parsed as an expression.
///
/// \param TypeOrExpr If the receiver was parsed as an expression (\c
/// IsExpr is true), the parsed expression. If the receiver was parsed
/// as a type (\c IsExpr is false), the parsed type.
///
/// \returns True if an error occurred during parsing or semantic
/// analysis, in which case the arguments do not have valid
/// values. Otherwise, returns false for a successful parse.
///
///   objc-receiver: [C++]
///     'super' [not parsed here]
///     expression
///     simple-type-specifier
///     typename-specifier
bool Parser::ParseObjCXXMessageReceiver(bool &IsExpr, void *&TypeOrExpr) {
  InMessageExpressionRAIIObject InMessage(*this, true);

  if (Tok.isOneOf(tok::identifier, tok::coloncolon, tok::kw_typename,
                  tok::annot_cxxscope))
    TryAnnotateTypeOrScopeToken();

  if (!Tok.isSimpleTypeSpecifier(getLangOpts())) {
    //   objc-receiver:
    //     expression
    // Make sure any typos in the receiver are corrected or diagnosed, so that
    // proper recovery can happen. FIXME: Perhaps filter the corrected expr to
    // only the things that are valid ObjC receivers?
    ExprResult Receiver = Actions.CorrectDelayedTyposInExpr(ParseExpression());
    if (Receiver.isInvalid())
      return true;

    IsExpr = true;
    TypeOrExpr = Receiver.get();
    return false;
  }

  // objc-receiver:
  //   typename-specifier
  //   simple-type-specifier
  //   expression (that starts with one of the above)
  DeclSpec DS(AttrFactory);
  ParseCXXSimpleTypeSpecifier(DS);

  if (Tok.is(tok::l_paren)) {
    // If we see an opening parentheses at this point, we are
    // actually parsing an expression that starts with a
    // function-style cast, e.g.,
    //
    //   postfix-expression:
    //     simple-type-specifier ( expression-list [opt] )
    //     typename-specifier ( expression-list [opt] )
    //
    // Parse the remainder of this case, then the (optional)
    // postfix-expression suffix, followed by the (optional)
    // right-hand side of the binary expression. We have an
    // instance method.
    ExprResult Receiver = ParseCXXTypeConstructExpression(DS);
    if (!Receiver.isInvalid())
      Receiver = ParsePostfixExpressionSuffix(Receiver.get());
    if (!Receiver.isInvalid())
      Receiver = ParseRHSOfBinaryExpression(Receiver.get(), prec::Comma);
    if (Receiver.isInvalid())
      return true;

    IsExpr = true;
    TypeOrExpr = Receiver.get();
    return false;
  }

  // We have a class message. Turn the simple-type-specifier or
  // typename-specifier we parsed into a type and parse the
  // remainder of the class message.
  Declarator DeclaratorInfo(DS, ParsedAttributesView::none(),
                            DeclaratorContext::TypeName);
  TypeResult Type = Actions.ActOnTypeName(DeclaratorInfo);
  if (Type.isInvalid())
    return true;

  IsExpr = false;
  TypeOrExpr = Type.get().getAsOpaquePtr();
  return false;
}

/// Determine whether the parser is currently referring to a an
/// Objective-C message send, using a simplified heuristic to avoid overhead.
///
/// This routine will only return true for a subset of valid message-send
/// expressions.
bool Parser::isSimpleObjCMessageExpression() {
  assert(Tok.is(tok::l_square) && getLangOpts().ObjC &&
         "Incorrect start for isSimpleObjCMessageExpression");
  return GetLookAheadToken(1).is(tok::identifier) &&
         GetLookAheadToken(2).is(tok::identifier);
}

bool Parser::isStartOfObjCClassMessageMissingOpenBracket() {
  if (!getLangOpts().ObjC || !NextToken().is(tok::identifier) ||
      InMessageExpression)
    return false;

  TypeResult Type;

  if (Tok.is(tok::annot_typename))
    Type = getTypeAnnotation(Tok);
  else if (Tok.is(tok::identifier))
    Type = Actions.getTypeName(*Tok.getIdentifierInfo(), Tok.getLocation(),
                               getCurScope());
  else
    return false;

  // FIXME: Should not be querying properties of types from the parser.
  if (Type.isUsable() && Type.get().get()->isObjCObjectOrInterfaceType()) {
    const Token &AfterNext = GetLookAheadToken(2);
    if (AfterNext.isOneOf(tok::colon, tok::r_square)) {
      if (Tok.is(tok::identifier))
        TryAnnotateTypeOrScopeToken();

      return Tok.is(tok::annot_typename);
    }
  }

  return false;
}

///   objc-message-expr:
///     '[' objc-receiver objc-message-args ']'
///
///   objc-receiver: [C]
///     'super'
///     expression
///     class-name
///     type-name
///
ExprResult Parser::ParseObjCMessageExpression() {
  assert(Tok.is(tok::l_square) && "'[' expected");
  SourceLocation LBracLoc = ConsumeBracket(); // consume '['

  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteObjCMessageReceiver(getCurScope());
    return ExprError();
  }

  InMessageExpressionRAIIObject InMessage(*this, true);

  if (getLangOpts().CPlusPlus) {
    // We completely separate the C and C++ cases because C++ requires
    // more complicated (read: slower) parsing.

    // Handle send to super.
    // FIXME: This doesn't benefit from the same typo-correction we
    // get in Objective-C.
    if (Tok.is(tok::identifier) && Tok.getIdentifierInfo() == Ident_super &&
        NextToken().isNot(tok::period) && getCurScope()->isInObjcMethodScope())
      return ParseObjCMessageExpressionBody(LBracLoc, ConsumeToken(), nullptr,
                                            nullptr);

    // Parse the receiver, which is either a type or an expression.
    bool IsExpr;
    void *TypeOrExpr = nullptr;
    if (ParseObjCXXMessageReceiver(IsExpr, TypeOrExpr)) {
      SkipUntil(tok::r_square, StopAtSemi);
      return ExprError();
    }

    if (IsExpr)
      return ParseObjCMessageExpressionBody(LBracLoc, SourceLocation(), nullptr,
                                            static_cast<Expr *>(TypeOrExpr));

    return ParseObjCMessageExpressionBody(LBracLoc, SourceLocation(),
                              ParsedType::getFromOpaquePtr(TypeOrExpr),
                                          nullptr);
  }

  if (Tok.is(tok::identifier)) {
    IdentifierInfo *Name = Tok.getIdentifierInfo();
    SourceLocation NameLoc = Tok.getLocation();
    ParsedType ReceiverType;
    switch (Actions.ObjC().getObjCMessageKind(
        getCurScope(), Name, NameLoc, Name == Ident_super,
        NextToken().is(tok::period), ReceiverType)) {
    case SemaObjC::ObjCSuperMessage:
      return ParseObjCMessageExpressionBody(LBracLoc, ConsumeToken(), nullptr,
                                            nullptr);

    case SemaObjC::ObjCClassMessage:
      if (!ReceiverType) {
        SkipUntil(tok::r_square, StopAtSemi);
        return ExprError();
      }

      ConsumeToken(); // the type name

      // Parse type arguments and protocol qualifiers.
      if (Tok.is(tok::less)) {
        SourceLocation NewEndLoc;
        TypeResult NewReceiverType
          = parseObjCTypeArgsAndProtocolQualifiers(NameLoc, ReceiverType,
                                                   /*consumeLastToken=*/true,
                                                   NewEndLoc);
        if (!NewReceiverType.isUsable()) {
          SkipUntil(tok::r_square, StopAtSemi);
          return ExprError();
        }

        ReceiverType = NewReceiverType.get();
      }

      return ParseObjCMessageExpressionBody(LBracLoc, SourceLocation(),
                                            ReceiverType, nullptr);

    case SemaObjC::ObjCInstanceMessage:
      // Fall through to parse an expression.
      break;
    }
  }

  // Otherwise, an arbitrary expression can be the receiver of a send.
  ExprResult Res = Actions.CorrectDelayedTyposInExpr(ParseExpression());
  if (Res.isInvalid()) {
    SkipUntil(tok::r_square, StopAtSemi);
    return Res;
  }

  return ParseObjCMessageExpressionBody(LBracLoc, SourceLocation(), nullptr,
                                        Res.get());
}

/// Parse the remainder of an Objective-C message following the
/// '[' objc-receiver.
///
/// This routine handles sends to super, class messages (sent to a
/// class name), and instance messages (sent to an object), and the
/// target is represented by \p SuperLoc, \p ReceiverType, or \p
/// ReceiverExpr, respectively. Only one of these parameters may have
/// a valid value.
///
/// \param LBracLoc The location of the opening '['.
///
/// \param SuperLoc If this is a send to 'super', the location of the
/// 'super' keyword that indicates a send to the superclass.
///
/// \param ReceiverType If this is a class message, the type of the
/// class we are sending a message to.
///
/// \param ReceiverExpr If this is an instance message, the expression
/// used to compute the receiver object.
///
///   objc-message-args:
///     objc-selector
///     objc-keywordarg-list
///
///   objc-keywordarg-list:
///     objc-keywordarg
///     objc-keywordarg-list objc-keywordarg
///
///   objc-keywordarg:
///     selector-name[opt] ':' objc-keywordexpr
///
///   objc-keywordexpr:
///     nonempty-expr-list
///
///   nonempty-expr-list:
///     assignment-expression
///     nonempty-expr-list , assignment-expression
///
ExprResult
Parser::ParseObjCMessageExpressionBody(SourceLocation LBracLoc,
                                       SourceLocation SuperLoc,
                                       ParsedType ReceiverType,
                                       Expr *ReceiverExpr) {
  InMessageExpressionRAIIObject InMessage(*this, true);

  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    if (SuperLoc.isValid())
      Actions.CodeCompletion().CodeCompleteObjCSuperMessage(
          getCurScope(), SuperLoc, std::nullopt, false);
    else if (ReceiverType)
      Actions.CodeCompletion().CodeCompleteObjCClassMessage(
          getCurScope(), ReceiverType, std::nullopt, false);
    else
      Actions.CodeCompletion().CodeCompleteObjCInstanceMessage(
          getCurScope(), ReceiverExpr, std::nullopt, false);
    return ExprError();
  }

  // Parse objc-selector
  SourceLocation Loc;
  IdentifierInfo *selIdent = ParseObjCSelectorPiece(Loc);

  SmallVector<const IdentifierInfo *, 12> KeyIdents;
  SmallVector<SourceLocation, 12> KeyLocs;
  ExprVector KeyExprs;

  if (Tok.is(tok::colon)) {
    while (true) {
      // Each iteration parses a single keyword argument.
      KeyIdents.push_back(selIdent);
      KeyLocs.push_back(Loc);

      if (ExpectAndConsume(tok::colon)) {
        // We must manually skip to a ']', otherwise the expression skipper will
        // stop at the ']' when it skips to the ';'.  We want it to skip beyond
        // the enclosing expression.
        SkipUntil(tok::r_square, StopAtSemi);
        return ExprError();
      }

      ///  Parse the expression after ':'

      if (Tok.is(tok::code_completion)) {
        cutOffParsing();
        if (SuperLoc.isValid())
          Actions.CodeCompletion().CodeCompleteObjCSuperMessage(
              getCurScope(), SuperLoc, KeyIdents,
              /*AtArgumentExpression=*/true);
        else if (ReceiverType)
          Actions.CodeCompletion().CodeCompleteObjCClassMessage(
              getCurScope(), ReceiverType, KeyIdents,
              /*AtArgumentExpression=*/true);
        else
          Actions.CodeCompletion().CodeCompleteObjCInstanceMessage(
              getCurScope(), ReceiverExpr, KeyIdents,
              /*AtArgumentExpression=*/true);

        return ExprError();
      }

      ExprResult Expr;
      if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
        Diag(Tok, diag::warn_cxx98_compat_generalized_initializer_lists);
        Expr = ParseBraceInitializer();
      } else
        Expr = ParseAssignmentExpression();

      ExprResult Res(Expr);
      if (Res.isInvalid()) {
        // We must manually skip to a ']', otherwise the expression skipper will
        // stop at the ']' when it skips to the ';'.  We want it to skip beyond
        // the enclosing expression.
        SkipUntil(tok::r_square, StopAtSemi);
        return Res;
      }

      // We have a valid expression.
      KeyExprs.push_back(Res.get());

      // Code completion after each argument.
      if (Tok.is(tok::code_completion)) {
        cutOffParsing();
        if (SuperLoc.isValid())
          Actions.CodeCompletion().CodeCompleteObjCSuperMessage(
              getCurScope(), SuperLoc, KeyIdents,
              /*AtArgumentExpression=*/false);
        else if (ReceiverType)
          Actions.CodeCompletion().CodeCompleteObjCClassMessage(
              getCurScope(), ReceiverType, KeyIdents,
              /*AtArgumentExpression=*/false);
        else
          Actions.CodeCompletion().CodeCompleteObjCInstanceMessage(
              getCurScope(), ReceiverExpr, KeyIdents,
              /*AtArgumentExpression=*/false);
        return ExprError();
      }

      // Check for another keyword selector.
      selIdent = ParseObjCSelectorPiece(Loc);
      if (!selIdent && Tok.isNot(tok::colon))
        break;
      // We have a selector or a colon, continue parsing.
    }
    // Parse the, optional, argument list, comma separated.
    while (Tok.is(tok::comma)) {
      SourceLocation commaLoc = ConsumeToken(); // Eat the ','.
      ///  Parse the expression after ','
      ExprResult Res(ParseAssignmentExpression());
      if (Tok.is(tok::colon))
        Res = Actions.CorrectDelayedTyposInExpr(Res);
      if (Res.isInvalid()) {
        if (Tok.is(tok::colon)) {
          Diag(commaLoc, diag::note_extra_comma_message_arg) <<
            FixItHint::CreateRemoval(commaLoc);
        }
        // We must manually skip to a ']', otherwise the expression skipper will
        // stop at the ']' when it skips to the ';'.  We want it to skip beyond
        // the enclosing expression.
        SkipUntil(tok::r_square, StopAtSemi);
        return Res;
      }

      // We have a valid expression.
      KeyExprs.push_back(Res.get());
    }
  } else if (!selIdent) {
    Diag(Tok, diag::err_expected) << tok::identifier; // missing selector name.

    // We must manually skip to a ']', otherwise the expression skipper will
    // stop at the ']' when it skips to the ';'.  We want it to skip beyond
    // the enclosing expression.
    SkipUntil(tok::r_square, StopAtSemi);
    return ExprError();
  }

  if (Tok.isNot(tok::r_square)) {
    Diag(Tok, diag::err_expected)
        << (Tok.is(tok::identifier) ? tok::colon : tok::r_square);
    // We must manually skip to a ']', otherwise the expression skipper will
    // stop at the ']' when it skips to the ';'.  We want it to skip beyond
    // the enclosing expression.
    SkipUntil(tok::r_square, StopAtSemi);
    return ExprError();
  }

  SourceLocation RBracLoc = ConsumeBracket(); // consume ']'

  unsigned nKeys = KeyIdents.size();
  if (nKeys == 0) {
    KeyIdents.push_back(selIdent);
    KeyLocs.push_back(Loc);
  }
  Selector Sel = PP.getSelectorTable().getSelector(nKeys, &KeyIdents[0]);

  if (SuperLoc.isValid())
    return Actions.ObjC().ActOnSuperMessage(
        getCurScope(), SuperLoc, Sel, LBracLoc, KeyLocs, RBracLoc, KeyExprs);
  else if (ReceiverType)
    return Actions.ObjC().ActOnClassMessage(getCurScope(), ReceiverType, Sel,
                                            LBracLoc, KeyLocs, RBracLoc,
                                            KeyExprs);
  return Actions.ObjC().ActOnInstanceMessage(
      getCurScope(), ReceiverExpr, Sel, LBracLoc, KeyLocs, RBracLoc, KeyExprs);
}

ExprResult Parser::ParseObjCStringLiteral(SourceLocation AtLoc) {
  ExprResult Res(ParseStringLiteralExpression());
  if (Res.isInvalid()) return Res;

  // @"foo" @"bar" is a valid concatenated string.  Eat any subsequent string
  // expressions.  At this point, we know that the only valid thing that starts
  // with '@' is an @"".
  SmallVector<SourceLocation, 4> AtLocs;
  ExprVector AtStrings;
  AtLocs.push_back(AtLoc);
  AtStrings.push_back(Res.get());

  while (Tok.is(tok::at)) {
    AtLocs.push_back(ConsumeToken()); // eat the @.

    // Invalid unless there is a string literal.
    if (!isTokenStringLiteral())
      return ExprError(Diag(Tok, diag::err_objc_concat_string));

    ExprResult Lit(ParseStringLiteralExpression());
    if (Lit.isInvalid())
      return Lit;

    AtStrings.push_back(Lit.get());
  }

  return Actions.ObjC().ParseObjCStringLiteral(AtLocs.data(), AtStrings);
}

/// ParseObjCBooleanLiteral -
/// objc-scalar-literal : '@' boolean-keyword
///                        ;
/// boolean-keyword: 'true' | 'false' | '__objc_yes' | '__objc_no'
///                        ;
ExprResult Parser::ParseObjCBooleanLiteral(SourceLocation AtLoc,
                                           bool ArgValue) {
  SourceLocation EndLoc = ConsumeToken();             // consume the keyword.
  return Actions.ObjC().ActOnObjCBoolLiteral(AtLoc, EndLoc, ArgValue);
}

/// ParseObjCCharacterLiteral -
/// objc-scalar-literal : '@' character-literal
///                        ;
ExprResult Parser::ParseObjCCharacterLiteral(SourceLocation AtLoc) {
  ExprResult Lit(Actions.ActOnCharacterConstant(Tok));
  if (Lit.isInvalid()) {
    return Lit;
  }
  ConsumeToken(); // Consume the literal token.
  return Actions.ObjC().BuildObjCNumericLiteral(AtLoc, Lit.get());
}

/// ParseObjCNumericLiteral -
/// objc-scalar-literal : '@' scalar-literal
///                        ;
/// scalar-literal : | numeric-constant			/* any numeric constant. */
///                    ;
ExprResult Parser::ParseObjCNumericLiteral(SourceLocation AtLoc) {
  ExprResult Lit(Actions.ActOnNumericConstant(Tok));
  if (Lit.isInvalid()) {
    return Lit;
  }
  ConsumeToken(); // Consume the literal token.
  return Actions.ObjC().BuildObjCNumericLiteral(AtLoc, Lit.get());
}

/// ParseObjCBoxedExpr -
/// objc-box-expression:
///       @( assignment-expression )
ExprResult
Parser::ParseObjCBoxedExpr(SourceLocation AtLoc) {
  if (Tok.isNot(tok::l_paren))
    return ExprError(Diag(Tok, diag::err_expected_lparen_after) << "@");

  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();
  ExprResult ValueExpr(ParseAssignmentExpression());
  if (T.consumeClose())
    return ExprError();

  if (ValueExpr.isInvalid())
    return ExprError();

  // Wrap the sub-expression in a parenthesized expression, to distinguish
  // a boxed expression from a literal.
  SourceLocation LPLoc = T.getOpenLocation(), RPLoc = T.getCloseLocation();
  ValueExpr = Actions.ActOnParenExpr(LPLoc, RPLoc, ValueExpr.get());
  return Actions.ObjC().BuildObjCBoxedExpr(SourceRange(AtLoc, RPLoc),
                                           ValueExpr.get());
}

ExprResult Parser::ParseObjCArrayLiteral(SourceLocation AtLoc) {
  ExprVector ElementExprs;                   // array elements.
  ConsumeBracket(); // consume the l_square.

  bool HasInvalidEltExpr = false;
  while (Tok.isNot(tok::r_square)) {
    // Parse list of array element expressions (all must be id types).
    ExprResult Res(ParseAssignmentExpression());
    if (Res.isInvalid()) {
      // We must manually skip to a ']', otherwise the expression skipper will
      // stop at the ']' when it skips to the ';'.  We want it to skip beyond
      // the enclosing expression.
      SkipUntil(tok::r_square, StopAtSemi);
      return Res;
    }

    Res = Actions.CorrectDelayedTyposInExpr(Res.get());
    if (Res.isInvalid())
      HasInvalidEltExpr = true;

    // Parse the ellipsis that indicates a pack expansion.
    if (Tok.is(tok::ellipsis))
      Res = Actions.ActOnPackExpansion(Res.get(), ConsumeToken());
    if (Res.isInvalid())
      HasInvalidEltExpr = true;

    ElementExprs.push_back(Res.get());

    if (Tok.is(tok::comma))
      ConsumeToken(); // Eat the ','.
    else if (Tok.isNot(tok::r_square))
      return ExprError(Diag(Tok, diag::err_expected_either) << tok::r_square
                                                            << tok::comma);
  }
  SourceLocation EndLoc = ConsumeBracket(); // location of ']'

  if (HasInvalidEltExpr)
    return ExprError();

  MultiExprArg Args(ElementExprs);
  return Actions.ObjC().BuildObjCArrayLiteral(SourceRange(AtLoc, EndLoc), Args);
}

ExprResult Parser::ParseObjCDictionaryLiteral(SourceLocation AtLoc) {
  SmallVector<ObjCDictionaryElement, 4> Elements; // dictionary elements.
  ConsumeBrace(); // consume the l_square.
  bool HasInvalidEltExpr = false;
  while (Tok.isNot(tok::r_brace)) {
    // Parse the comma separated key : value expressions.
    ExprResult KeyExpr;
    {
      ColonProtectionRAIIObject X(*this);
      KeyExpr = ParseAssignmentExpression();
      if (KeyExpr.isInvalid()) {
        // We must manually skip to a '}', otherwise the expression skipper will
        // stop at the '}' when it skips to the ';'.  We want it to skip beyond
        // the enclosing expression.
        SkipUntil(tok::r_brace, StopAtSemi);
        return KeyExpr;
      }
    }

    if (ExpectAndConsume(tok::colon)) {
      SkipUntil(tok::r_brace, StopAtSemi);
      return ExprError();
    }

    ExprResult ValueExpr(ParseAssignmentExpression());
    if (ValueExpr.isInvalid()) {
      // We must manually skip to a '}', otherwise the expression skipper will
      // stop at the '}' when it skips to the ';'.  We want it to skip beyond
      // the enclosing expression.
      SkipUntil(tok::r_brace, StopAtSemi);
      return ValueExpr;
    }

    // Check the key and value for possible typos
    KeyExpr = Actions.CorrectDelayedTyposInExpr(KeyExpr.get());
    ValueExpr = Actions.CorrectDelayedTyposInExpr(ValueExpr.get());
    if (KeyExpr.isInvalid() || ValueExpr.isInvalid())
      HasInvalidEltExpr = true;

    // Parse the ellipsis that designates this as a pack expansion. Do not
    // ActOnPackExpansion here, leave it to template instantiation time where
    // we can get better diagnostics.
    SourceLocation EllipsisLoc;
    if (getLangOpts().CPlusPlus)
      TryConsumeToken(tok::ellipsis, EllipsisLoc);

    // We have a valid expression. Collect it in a vector so we can
    // build the argument list.
    ObjCDictionaryElement Element = {KeyExpr.get(), ValueExpr.get(),
                                     EllipsisLoc, std::nullopt};
    Elements.push_back(Element);

    if (!TryConsumeToken(tok::comma) && Tok.isNot(tok::r_brace))
      return ExprError(Diag(Tok, diag::err_expected_either) << tok::r_brace
                                                            << tok::comma);
  }
  SourceLocation EndLoc = ConsumeBrace();

  if (HasInvalidEltExpr)
    return ExprError();

  // Create the ObjCDictionaryLiteral.
  return Actions.ObjC().BuildObjCDictionaryLiteral(SourceRange(AtLoc, EndLoc),
                                                   Elements);
}

///    objc-encode-expression:
///      \@encode ( type-name )
ExprResult
Parser::ParseObjCEncodeExpression(SourceLocation AtLoc) {
  assert(Tok.isObjCAtKeyword(tok::objc_encode) && "Not an @encode expression!");

  SourceLocation EncLoc = ConsumeToken();

  if (Tok.isNot(tok::l_paren))
    return ExprError(Diag(Tok, diag::err_expected_lparen_after) << "@encode");

  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();

  TypeResult Ty = ParseTypeName();

  T.consumeClose();

  if (Ty.isInvalid())
    return ExprError();

  return Actions.ObjC().ParseObjCEncodeExpression(
      AtLoc, EncLoc, T.getOpenLocation(), Ty.get(), T.getCloseLocation());
}

///     objc-protocol-expression
///       \@protocol ( protocol-name )
ExprResult
Parser::ParseObjCProtocolExpression(SourceLocation AtLoc) {
  SourceLocation ProtoLoc = ConsumeToken();

  if (Tok.isNot(tok::l_paren))
    return ExprError(Diag(Tok, diag::err_expected_lparen_after) << "@protocol");

  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();

  if (expectIdentifier())
    return ExprError();

  IdentifierInfo *protocolId = Tok.getIdentifierInfo();
  SourceLocation ProtoIdLoc = ConsumeToken();

  T.consumeClose();

  return Actions.ObjC().ParseObjCProtocolExpression(
      protocolId, AtLoc, ProtoLoc, T.getOpenLocation(), ProtoIdLoc,
      T.getCloseLocation());
}

///     objc-selector-expression
///       @selector '(' '('[opt] objc-keyword-selector ')'[opt] ')'
ExprResult Parser::ParseObjCSelectorExpression(SourceLocation AtLoc) {
  SourceLocation SelectorLoc = ConsumeToken();

  if (Tok.isNot(tok::l_paren))
    return ExprError(Diag(Tok, diag::err_expected_lparen_after) << "@selector");

  SmallVector<const IdentifierInfo *, 12> KeyIdents;
  SourceLocation sLoc;

  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();
  bool HasOptionalParen = Tok.is(tok::l_paren);
  if (HasOptionalParen)
    ConsumeParen();

  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteObjCSelector(getCurScope(), KeyIdents);
    return ExprError();
  }

  IdentifierInfo *SelIdent = ParseObjCSelectorPiece(sLoc);
  if (!SelIdent &&  // missing selector name.
      Tok.isNot(tok::colon) && Tok.isNot(tok::coloncolon))
    return ExprError(Diag(Tok, diag::err_expected) << tok::identifier);

  KeyIdents.push_back(SelIdent);

  unsigned nColons = 0;
  if (Tok.isNot(tok::r_paren)) {
    while (true) {
      if (TryConsumeToken(tok::coloncolon)) { // Handle :: in C++.
        ++nColons;
        KeyIdents.push_back(nullptr);
      } else if (ExpectAndConsume(tok::colon)) // Otherwise expect ':'.
        return ExprError();
      ++nColons;

      if (Tok.is(tok::r_paren))
        break;

      if (Tok.is(tok::code_completion)) {
        cutOffParsing();
        Actions.CodeCompletion().CodeCompleteObjCSelector(getCurScope(),
                                                          KeyIdents);
        return ExprError();
      }

      // Check for another keyword selector.
      SourceLocation Loc;
      SelIdent = ParseObjCSelectorPiece(Loc);
      KeyIdents.push_back(SelIdent);
      if (!SelIdent && Tok.isNot(tok::colon) && Tok.isNot(tok::coloncolon))
        break;
    }
  }
  if (HasOptionalParen && Tok.is(tok::r_paren))
    ConsumeParen(); // ')'
  T.consumeClose();
  Selector Sel = PP.getSelectorTable().getSelector(nColons, &KeyIdents[0]);
  return Actions.ObjC().ParseObjCSelectorExpression(
      Sel, AtLoc, SelectorLoc, T.getOpenLocation(), T.getCloseLocation(),
      !HasOptionalParen);
}

void Parser::ParseLexedObjCMethodDefs(LexedMethod &LM, bool parseMethod) {
  // MCDecl might be null due to error in method or c-function  prototype, etc.
  Decl *MCDecl = LM.D;
  bool skip =
      MCDecl && ((parseMethod && !Actions.ObjC().isObjCMethodDecl(MCDecl)) ||
                 (!parseMethod && Actions.ObjC().isObjCMethodDecl(MCDecl)));
  if (skip)
    return;

  // Save the current token position.
  SourceLocation OrigLoc = Tok.getLocation();

  assert(!LM.Toks.empty() && "ParseLexedObjCMethodDef - Empty body!");
  // Store an artificial EOF token to ensure that we don't run off the end of
  // the method's body when we come to parse it.
  Token Eof;
  Eof.startToken();
  Eof.setKind(tok::eof);
  Eof.setEofData(MCDecl);
  Eof.setLocation(OrigLoc);
  LM.Toks.push_back(Eof);
  // Append the current token at the end of the new token stream so that it
  // doesn't get lost.
  LM.Toks.push_back(Tok);
  PP.EnterTokenStream(LM.Toks, true, /*IsReinject*/true);

  // Consume the previously pushed token.
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);

  assert(Tok.isOneOf(tok::l_brace, tok::kw_try, tok::colon) &&
         "Inline objective-c method not starting with '{' or 'try' or ':'");
  // Enter a scope for the method or c-function body.
  ParseScope BodyScope(this, (parseMethod ? Scope::ObjCMethodScope : 0) |
                                 Scope::FnScope | Scope::DeclScope |
                                 Scope::CompoundStmtScope);
  Sema::FPFeaturesStateRAII SaveFPFeatures(Actions);

  // Tell the actions module that we have entered a method or c-function definition
  // with the specified Declarator for the method/function.
  if (parseMethod)
    Actions.ObjC().ActOnStartOfObjCMethodDef(getCurScope(), MCDecl);
  else
    Actions.ActOnStartOfFunctionDef(getCurScope(), MCDecl);
  if (Tok.is(tok::kw_try))
    ParseFunctionTryBlock(MCDecl, BodyScope);
  else {
    if (Tok.is(tok::colon))
      ParseConstructorInitializer(MCDecl);
    else
      Actions.ActOnDefaultCtorInitializers(MCDecl);
    ParseFunctionStatementBody(MCDecl, BodyScope);
  }

  if (Tok.getLocation() != OrigLoc) {
    // Due to parsing error, we either went over the cached tokens or
    // there are still cached tokens left. If it's the latter case skip the
    // leftover tokens.
    // Since this is an uncommon situation that should be avoided, use the
    // expensive isBeforeInTranslationUnit call.
    if (PP.getSourceManager().isBeforeInTranslationUnit(Tok.getLocation(),
                                                     OrigLoc))
      while (Tok.getLocation() != OrigLoc && Tok.isNot(tok::eof))
        ConsumeAnyToken();
  }
  // Clean up the remaining EOF token, only if it's inserted by us. Otherwise
  // this might be code-completion token, which must be propagated to callers.
  if (Tok.is(tok::eof) && Tok.getEofData() == MCDecl)
    ConsumeAnyToken();
}
