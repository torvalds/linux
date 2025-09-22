//===--- ParseCXXInlineMethods.cpp - C++ class inline methods parsing------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements parsing for C++ class inline methods.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclTemplate.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Scope.h"

using namespace clang;

/// Parse the optional ("message") part of a deleted-function-body.
StringLiteral *Parser::ParseCXXDeletedFunctionMessage() {
  if (!Tok.is(tok::l_paren))
    return nullptr;
  StringLiteral *Message = nullptr;
  BalancedDelimiterTracker BT{*this, tok::l_paren};
  BT.consumeOpen();

  if (isTokenStringLiteral()) {
    ExprResult Res = ParseUnevaluatedStringLiteralExpression();
    if (Res.isUsable()) {
      Message = Res.getAs<StringLiteral>();
      Diag(Message->getBeginLoc(), getLangOpts().CPlusPlus26
                                       ? diag::warn_cxx23_delete_with_message
                                       : diag::ext_delete_with_message)
          << Message->getSourceRange();
    }
  } else {
    Diag(Tok.getLocation(), diag::err_expected_string_literal)
        << /*Source='in'*/ 0 << "'delete'";
    SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch);
  }

  BT.consumeClose();
  return Message;
}

/// If we've encountered '= delete' in a context where it is ill-formed, such
/// as in the declaration of a non-function, also skip the ("message") part if
/// it is present to avoid issuing further diagnostics.
void Parser::SkipDeletedFunctionBody() {
  if (!Tok.is(tok::l_paren))
    return;

  BalancedDelimiterTracker BT{*this, tok::l_paren};
  BT.consumeOpen();

  // Just skip to the end of the current declaration.
  SkipUntil(tok::r_paren, tok::comma, StopAtSemi | StopBeforeMatch);
  if (Tok.is(tok::r_paren))
    BT.consumeClose();
}

/// ParseCXXInlineMethodDef - We parsed and verified that the specified
/// Declarator is a well formed C++ inline method definition. Now lex its body
/// and store its tokens for parsing after the C++ class is complete.
NamedDecl *Parser::ParseCXXInlineMethodDef(
    AccessSpecifier AS, const ParsedAttributesView &AccessAttrs,
    ParsingDeclarator &D, const ParsedTemplateInfo &TemplateInfo,
    const VirtSpecifiers &VS, SourceLocation PureSpecLoc) {
  assert(D.isFunctionDeclarator() && "This isn't a function declarator!");
  assert(Tok.isOneOf(tok::l_brace, tok::colon, tok::kw_try, tok::equal) &&
         "Current token not a '{', ':', '=', or 'try'!");

  MultiTemplateParamsArg TemplateParams(
      TemplateInfo.TemplateParams ? TemplateInfo.TemplateParams->data()
                                  : nullptr,
      TemplateInfo.TemplateParams ? TemplateInfo.TemplateParams->size() : 0);

  NamedDecl *FnD;
  if (D.getDeclSpec().isFriendSpecified())
    FnD = Actions.ActOnFriendFunctionDecl(getCurScope(), D,
                                          TemplateParams);
  else {
    FnD = Actions.ActOnCXXMemberDeclarator(getCurScope(), AS, D,
                                           TemplateParams, nullptr,
                                           VS, ICIS_NoInit);
    if (FnD) {
      Actions.ProcessDeclAttributeList(getCurScope(), FnD, AccessAttrs);
      if (PureSpecLoc.isValid())
        Actions.ActOnPureSpecifier(FnD, PureSpecLoc);
    }
  }

  if (FnD)
    HandleMemberFunctionDeclDelays(D, FnD);

  D.complete(FnD);

  if (TryConsumeToken(tok::equal)) {
    if (!FnD) {
      SkipUntil(tok::semi);
      return nullptr;
    }

    bool Delete = false;
    SourceLocation KWLoc;
    SourceLocation KWEndLoc = Tok.getEndLoc().getLocWithOffset(-1);
    if (TryConsumeToken(tok::kw_delete, KWLoc)) {
      Diag(KWLoc, getLangOpts().CPlusPlus11
                      ? diag::warn_cxx98_compat_defaulted_deleted_function
                      : diag::ext_defaulted_deleted_function)
        << 1 /* deleted */;
      StringLiteral *Message = ParseCXXDeletedFunctionMessage();
      Actions.SetDeclDeleted(FnD, KWLoc, Message);
      Delete = true;
      if (auto *DeclAsFunction = dyn_cast<FunctionDecl>(FnD)) {
        DeclAsFunction->setRangeEnd(KWEndLoc);
      }
    } else if (TryConsumeToken(tok::kw_default, KWLoc)) {
      Diag(KWLoc, getLangOpts().CPlusPlus11
                      ? diag::warn_cxx98_compat_defaulted_deleted_function
                      : diag::ext_defaulted_deleted_function)
        << 0 /* defaulted */;
      Actions.SetDeclDefaulted(FnD, KWLoc);
      if (auto *DeclAsFunction = dyn_cast<FunctionDecl>(FnD)) {
        DeclAsFunction->setRangeEnd(KWEndLoc);
      }
    } else {
      llvm_unreachable("function definition after = not 'delete' or 'default'");
    }

    if (Tok.is(tok::comma)) {
      Diag(KWLoc, diag::err_default_delete_in_multiple_declaration)
        << Delete;
      SkipUntil(tok::semi);
    } else if (ExpectAndConsume(tok::semi, diag::err_expected_after,
                                Delete ? "delete" : "default")) {
      SkipUntil(tok::semi);
    }

    return FnD;
  }

  if (SkipFunctionBodies && (!FnD || Actions.canSkipFunctionBody(FnD)) &&
      trySkippingFunctionBody()) {
    Actions.ActOnSkippedFunctionBody(FnD);
    return FnD;
  }

  // In delayed template parsing mode, if we are within a class template
  // or if we are about to parse function member template then consume
  // the tokens and store them for parsing at the end of the translation unit.
  if (getLangOpts().DelayedTemplateParsing &&
      D.getFunctionDefinitionKind() == FunctionDefinitionKind::Definition &&
      !D.getDeclSpec().hasConstexprSpecifier() &&
      !(FnD && FnD->getAsFunction() &&
        FnD->getAsFunction()->getReturnType()->getContainedAutoType()) &&
      ((Actions.CurContext->isDependentContext() ||
        (TemplateInfo.Kind != ParsedTemplateInfo::NonTemplate &&
         TemplateInfo.Kind != ParsedTemplateInfo::ExplicitSpecialization)) &&
       !Actions.IsInsideALocalClassWithinATemplateFunction())) {

    CachedTokens Toks;
    LexTemplateFunctionForLateParsing(Toks);

    if (FnD) {
      FunctionDecl *FD = FnD->getAsFunction();
      Actions.CheckForFunctionRedefinition(FD);
      Actions.MarkAsLateParsedTemplate(FD, FnD, Toks);
    }

    return FnD;
  }

  // Consume the tokens and store them for later parsing.

  LexedMethod* LM = new LexedMethod(this, FnD);
  getCurrentClass().LateParsedDeclarations.push_back(LM);
  CachedTokens &Toks = LM->Toks;

  tok::TokenKind kind = Tok.getKind();
  // Consume everything up to (and including) the left brace of the
  // function body.
  if (ConsumeAndStoreFunctionPrologue(Toks)) {
    // We didn't find the left-brace we expected after the
    // constructor initializer.

    // If we're code-completing and the completion point was in the broken
    // initializer, we want to parse it even though that will fail.
    if (PP.isCodeCompletionEnabled() &&
        llvm::any_of(Toks, [](const Token &Tok) {
          return Tok.is(tok::code_completion);
        })) {
      // If we gave up at the completion point, the initializer list was
      // likely truncated, so don't eat more tokens. We'll hit some extra
      // errors, but they should be ignored in code completion.
      return FnD;
    }

    // We already printed an error, and it's likely impossible to recover,
    // so don't try to parse this method later.
    // Skip over the rest of the decl and back to somewhere that looks
    // reasonable.
    SkipMalformedDecl();
    delete getCurrentClass().LateParsedDeclarations.back();
    getCurrentClass().LateParsedDeclarations.pop_back();
    return FnD;
  } else {
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

  if (FnD) {
    FunctionDecl *FD = FnD->getAsFunction();
    // Track that this function will eventually have a body; Sema needs
    // to know this.
    Actions.CheckForFunctionRedefinition(FD);
    FD->setWillHaveBody(true);
  } else {
    // If semantic analysis could not build a function declaration,
    // just throw away the late-parsed declaration.
    delete getCurrentClass().LateParsedDeclarations.back();
    getCurrentClass().LateParsedDeclarations.pop_back();
  }

  return FnD;
}

/// ParseCXXNonStaticMemberInitializer - We parsed and verified that the
/// specified Declarator is a well formed C++ non-static data member
/// declaration. Now lex its initializer and store its tokens for parsing
/// after the class is complete.
void Parser::ParseCXXNonStaticMemberInitializer(Decl *VarD) {
  assert(Tok.isOneOf(tok::l_brace, tok::equal) &&
         "Current token not a '{' or '='!");

  LateParsedMemberInitializer *MI =
    new LateParsedMemberInitializer(this, VarD);
  getCurrentClass().LateParsedDeclarations.push_back(MI);
  CachedTokens &Toks = MI->Toks;

  tok::TokenKind kind = Tok.getKind();
  if (kind == tok::equal) {
    Toks.push_back(Tok);
    ConsumeToken();
  }

  if (kind == tok::l_brace) {
    // Begin by storing the '{' token.
    Toks.push_back(Tok);
    ConsumeBrace();

    // Consume everything up to (and including) the matching right brace.
    ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/true);
  } else {
    // Consume everything up to (but excluding) the comma or semicolon.
    ConsumeAndStoreInitializer(Toks, CIK_DefaultInitializer);
  }

  // Store an artificial EOF token to ensure that we don't run off the end of
  // the initializer when we come to parse it.
  Token Eof;
  Eof.startToken();
  Eof.setKind(tok::eof);
  Eof.setLocation(Tok.getLocation());
  Eof.setEofData(VarD);
  Toks.push_back(Eof);
}

Parser::LateParsedDeclaration::~LateParsedDeclaration() {}
void Parser::LateParsedDeclaration::ParseLexedMethodDeclarations() {}
void Parser::LateParsedDeclaration::ParseLexedMemberInitializers() {}
void Parser::LateParsedDeclaration::ParseLexedMethodDefs() {}
void Parser::LateParsedDeclaration::ParseLexedAttributes() {}
void Parser::LateParsedDeclaration::ParseLexedPragmas() {}

Parser::LateParsedClass::LateParsedClass(Parser *P, ParsingClass *C)
  : Self(P), Class(C) {}

Parser::LateParsedClass::~LateParsedClass() {
  Self->DeallocateParsedClasses(Class);
}

void Parser::LateParsedClass::ParseLexedMethodDeclarations() {
  Self->ParseLexedMethodDeclarations(*Class);
}

void Parser::LateParsedClass::ParseLexedMemberInitializers() {
  Self->ParseLexedMemberInitializers(*Class);
}

void Parser::LateParsedClass::ParseLexedMethodDefs() {
  Self->ParseLexedMethodDefs(*Class);
}

void Parser::LateParsedClass::ParseLexedAttributes() {
  Self->ParseLexedAttributes(*Class);
}

void Parser::LateParsedClass::ParseLexedPragmas() {
  Self->ParseLexedPragmas(*Class);
}

void Parser::LateParsedMethodDeclaration::ParseLexedMethodDeclarations() {
  Self->ParseLexedMethodDeclaration(*this);
}

void Parser::LexedMethod::ParseLexedMethodDefs() {
  Self->ParseLexedMethodDef(*this);
}

void Parser::LateParsedMemberInitializer::ParseLexedMemberInitializers() {
  Self->ParseLexedMemberInitializer(*this);
}

void Parser::LateParsedAttribute::ParseLexedAttributes() {
  Self->ParseLexedAttribute(*this, true, false);
}

void Parser::LateParsedPragma::ParseLexedPragmas() {
  Self->ParseLexedPragma(*this);
}

/// Utility to re-enter a possibly-templated scope while parsing its
/// late-parsed components.
struct Parser::ReenterTemplateScopeRAII {
  Parser &P;
  MultiParseScope Scopes;
  TemplateParameterDepthRAII CurTemplateDepthTracker;

  ReenterTemplateScopeRAII(Parser &P, Decl *MaybeTemplated, bool Enter = true)
      : P(P), Scopes(P), CurTemplateDepthTracker(P.TemplateParameterDepth) {
    if (Enter) {
      CurTemplateDepthTracker.addDepth(
          P.ReenterTemplateScopes(Scopes, MaybeTemplated));
    }
  }
};

/// Utility to re-enter a class scope while parsing its late-parsed components.
struct Parser::ReenterClassScopeRAII : ReenterTemplateScopeRAII {
  ParsingClass &Class;

  ReenterClassScopeRAII(Parser &P, ParsingClass &Class)
      : ReenterTemplateScopeRAII(P, Class.TagOrTemplate,
                                 /*Enter=*/!Class.TopLevelClass),
        Class(Class) {
    // If this is the top-level class, we're still within its scope.
    if (Class.TopLevelClass)
      return;

    // Re-enter the class scope itself.
    Scopes.Enter(Scope::ClassScope|Scope::DeclScope);
    P.Actions.ActOnStartDelayedMemberDeclarations(P.getCurScope(),
                                                  Class.TagOrTemplate);
  }
  ~ReenterClassScopeRAII() {
    if (Class.TopLevelClass)
      return;

    P.Actions.ActOnFinishDelayedMemberDeclarations(P.getCurScope(),
                                                   Class.TagOrTemplate);
  }
};

/// ParseLexedMethodDeclarations - We finished parsing the member
/// specification of a top (non-nested) C++ class. Now go over the
/// stack of method declarations with some parts for which parsing was
/// delayed (such as default arguments) and parse them.
void Parser::ParseLexedMethodDeclarations(ParsingClass &Class) {
  ReenterClassScopeRAII InClassScope(*this, Class);

  for (LateParsedDeclaration *LateD : Class.LateParsedDeclarations)
    LateD->ParseLexedMethodDeclarations();
}

void Parser::ParseLexedMethodDeclaration(LateParsedMethodDeclaration &LM) {
  // If this is a member template, introduce the template parameter scope.
  ReenterTemplateScopeRAII InFunctionTemplateScope(*this, LM.Method);

  // Start the delayed C++ method declaration
  Actions.ActOnStartDelayedCXXMethodDeclaration(getCurScope(), LM.Method);

  // Introduce the parameters into scope and parse their default
  // arguments.
  InFunctionTemplateScope.Scopes.Enter(Scope::FunctionPrototypeScope |
                                       Scope::FunctionDeclarationScope |
                                       Scope::DeclScope);
  for (unsigned I = 0, N = LM.DefaultArgs.size(); I != N; ++I) {
    auto Param = cast<ParmVarDecl>(LM.DefaultArgs[I].Param);
    // Introduce the parameter into scope.
    bool HasUnparsed = Param->hasUnparsedDefaultArg();
    Actions.ActOnDelayedCXXMethodParameter(getCurScope(), Param);
    std::unique_ptr<CachedTokens> Toks = std::move(LM.DefaultArgs[I].Toks);
    if (Toks) {
      ParenBraceBracketBalancer BalancerRAIIObj(*this);

      // Mark the end of the default argument so that we know when to stop when
      // we parse it later on.
      Token LastDefaultArgToken = Toks->back();
      Token DefArgEnd;
      DefArgEnd.startToken();
      DefArgEnd.setKind(tok::eof);
      DefArgEnd.setLocation(LastDefaultArgToken.getEndLoc());
      DefArgEnd.setEofData(Param);
      Toks->push_back(DefArgEnd);

      // Parse the default argument from its saved token stream.
      Toks->push_back(Tok); // So that the current token doesn't get lost
      PP.EnterTokenStream(*Toks, true, /*IsReinject*/ true);

      // Consume the previously-pushed token.
      ConsumeAnyToken();

      // Consume the '='.
      assert(Tok.is(tok::equal) && "Default argument not starting with '='");
      SourceLocation EqualLoc = ConsumeToken();

      // The argument isn't actually potentially evaluated unless it is
      // used.
      EnterExpressionEvaluationContext Eval(
          Actions,
          Sema::ExpressionEvaluationContext::PotentiallyEvaluatedIfUsed, Param);

      ExprResult DefArgResult;
      if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
        Diag(Tok, diag::warn_cxx98_compat_generalized_initializer_lists);
        DefArgResult = ParseBraceInitializer();
      } else
        DefArgResult = ParseAssignmentExpression();
      DefArgResult = Actions.CorrectDelayedTyposInExpr(DefArgResult, Param);
      if (DefArgResult.isInvalid()) {
        Actions.ActOnParamDefaultArgumentError(Param, EqualLoc,
                                               /*DefaultArg=*/nullptr);
      } else {
        if (Tok.isNot(tok::eof) || Tok.getEofData() != Param) {
          // The last two tokens are the terminator and the saved value of
          // Tok; the last token in the default argument is the one before
          // those.
          assert(Toks->size() >= 3 && "expected a token in default arg");
          Diag(Tok.getLocation(), diag::err_default_arg_unparsed)
            << SourceRange(Tok.getLocation(),
                           (*Toks)[Toks->size() - 3].getLocation());
        }
        Actions.ActOnParamDefaultArgument(Param, EqualLoc,
                                          DefArgResult.get());
      }

      // There could be leftover tokens (e.g. because of an error).
      // Skip through until we reach the 'end of default argument' token.
      while (Tok.isNot(tok::eof))
        ConsumeAnyToken();

      if (Tok.is(tok::eof) && Tok.getEofData() == Param)
        ConsumeAnyToken();
    } else if (HasUnparsed) {
      assert(Param->hasInheritedDefaultArg());
      FunctionDecl *Old;
      if (const auto *FunTmpl = dyn_cast<FunctionTemplateDecl>(LM.Method))
        Old =
            cast<FunctionDecl>(FunTmpl->getTemplatedDecl())->getPreviousDecl();
      else
        Old = cast<FunctionDecl>(LM.Method)->getPreviousDecl();
      if (Old) {
        ParmVarDecl *OldParam = Old->getParamDecl(I);
        assert(!OldParam->hasUnparsedDefaultArg());
        if (OldParam->hasUninstantiatedDefaultArg())
          Param->setUninstantiatedDefaultArg(
              OldParam->getUninstantiatedDefaultArg());
        else
          Param->setDefaultArg(OldParam->getInit());
      }
    }
  }

  // Parse a delayed exception-specification, if there is one.
  if (CachedTokens *Toks = LM.ExceptionSpecTokens) {
    ParenBraceBracketBalancer BalancerRAIIObj(*this);

    // Add the 'stop' token.
    Token LastExceptionSpecToken = Toks->back();
    Token ExceptionSpecEnd;
    ExceptionSpecEnd.startToken();
    ExceptionSpecEnd.setKind(tok::eof);
    ExceptionSpecEnd.setLocation(LastExceptionSpecToken.getEndLoc());
    ExceptionSpecEnd.setEofData(LM.Method);
    Toks->push_back(ExceptionSpecEnd);

    // Parse the default argument from its saved token stream.
    Toks->push_back(Tok); // So that the current token doesn't get lost
    PP.EnterTokenStream(*Toks, true, /*IsReinject*/true);

    // Consume the previously-pushed token.
    ConsumeAnyToken();

    // C++11 [expr.prim.general]p3:
    //   If a declaration declares a member function or member function
    //   template of a class X, the expression this is a prvalue of type
    //   "pointer to cv-qualifier-seq X" between the optional cv-qualifer-seq
    //   and the end of the function-definition, member-declarator, or
    //   declarator.
    CXXMethodDecl *Method;
    FunctionDecl *FunctionToPush;
    if (FunctionTemplateDecl *FunTmpl
          = dyn_cast<FunctionTemplateDecl>(LM.Method))
      FunctionToPush = FunTmpl->getTemplatedDecl();
    else
      FunctionToPush = cast<FunctionDecl>(LM.Method);
    Method = dyn_cast<CXXMethodDecl>(FunctionToPush);

    // Push a function scope so that tryCaptureVariable() can properly visit
    // function scopes involving function parameters that are referenced inside
    // the noexcept specifier e.g. through a lambda expression.
    // Example:
    // struct X {
    //   void ICE(int val) noexcept(noexcept([val]{}));
    // };
    // Setup the CurScope to match the function DeclContext - we have such
    // assumption in IsInFnTryBlockHandler().
    ParseScope FnScope(this, Scope::FnScope);
    Sema::ContextRAII FnContext(Actions, FunctionToPush,
                                /*NewThisContext=*/false);
    Sema::FunctionScopeRAII PopFnContext(Actions);
    Actions.PushFunctionScope();

    Sema::CXXThisScopeRAII ThisScope(
        Actions, Method ? Method->getParent() : nullptr,
        Method ? Method->getMethodQualifiers() : Qualifiers{},
        Method && getLangOpts().CPlusPlus11);

    // Parse the exception-specification.
    SourceRange SpecificationRange;
    SmallVector<ParsedType, 4> DynamicExceptions;
    SmallVector<SourceRange, 4> DynamicExceptionRanges;
    ExprResult NoexceptExpr;
    CachedTokens *ExceptionSpecTokens;

    ExceptionSpecificationType EST
      = tryParseExceptionSpecification(/*Delayed=*/false, SpecificationRange,
                                       DynamicExceptions,
                                       DynamicExceptionRanges, NoexceptExpr,
                                       ExceptionSpecTokens);

    if (Tok.isNot(tok::eof) || Tok.getEofData() != LM.Method)
      Diag(Tok.getLocation(), diag::err_except_spec_unparsed);

    // Attach the exception-specification to the method.
    Actions.actOnDelayedExceptionSpecification(LM.Method, EST,
                                               SpecificationRange,
                                               DynamicExceptions,
                                               DynamicExceptionRanges,
                                               NoexceptExpr.isUsable()?
                                                 NoexceptExpr.get() : nullptr);

    // There could be leftover tokens (e.g. because of an error).
    // Skip through until we reach the original token position.
    while (Tok.isNot(tok::eof))
      ConsumeAnyToken();

    // Clean up the remaining EOF token.
    if (Tok.is(tok::eof) && Tok.getEofData() == LM.Method)
      ConsumeAnyToken();

    delete Toks;
    LM.ExceptionSpecTokens = nullptr;
  }

  InFunctionTemplateScope.Scopes.Exit();

  // Finish the delayed C++ method declaration.
  Actions.ActOnFinishDelayedCXXMethodDeclaration(getCurScope(), LM.Method);
}

/// ParseLexedMethodDefs - We finished parsing the member specification of a top
/// (non-nested) C++ class. Now go over the stack of lexed methods that were
/// collected during its parsing and parse them all.
void Parser::ParseLexedMethodDefs(ParsingClass &Class) {
  ReenterClassScopeRAII InClassScope(*this, Class);

  for (LateParsedDeclaration *D : Class.LateParsedDeclarations)
    D->ParseLexedMethodDefs();
}

void Parser::ParseLexedMethodDef(LexedMethod &LM) {
  // If this is a member template, introduce the template parameter scope.
  ReenterTemplateScopeRAII InFunctionTemplateScope(*this, LM.D);

  ParenBraceBracketBalancer BalancerRAIIObj(*this);

  assert(!LM.Toks.empty() && "Empty body!");
  Token LastBodyToken = LM.Toks.back();
  Token BodyEnd;
  BodyEnd.startToken();
  BodyEnd.setKind(tok::eof);
  BodyEnd.setLocation(LastBodyToken.getEndLoc());
  BodyEnd.setEofData(LM.D);
  LM.Toks.push_back(BodyEnd);
  // Append the current token at the end of the new token stream so that it
  // doesn't get lost.
  LM.Toks.push_back(Tok);
  PP.EnterTokenStream(LM.Toks, true, /*IsReinject*/true);

  // Consume the previously pushed token.
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);
  assert(Tok.isOneOf(tok::l_brace, tok::colon, tok::kw_try)
         && "Inline method not starting with '{', ':' or 'try'");

  // Parse the method body. Function body parsing code is similar enough
  // to be re-used for method bodies as well.
  ParseScope FnScope(this, Scope::FnScope | Scope::DeclScope |
                               Scope::CompoundStmtScope);
  Sema::FPFeaturesStateRAII SaveFPFeatures(Actions);

  Actions.ActOnStartOfFunctionDef(getCurScope(), LM.D);

  if (Tok.is(tok::kw_try)) {
    ParseFunctionTryBlock(LM.D, FnScope);

    while (Tok.isNot(tok::eof))
      ConsumeAnyToken();

    if (Tok.is(tok::eof) && Tok.getEofData() == LM.D)
      ConsumeAnyToken();
    return;
  }
  if (Tok.is(tok::colon)) {
    ParseConstructorInitializer(LM.D);

    // Error recovery.
    if (!Tok.is(tok::l_brace)) {
      FnScope.Exit();
      Actions.ActOnFinishFunctionBody(LM.D, nullptr);

      while (Tok.isNot(tok::eof))
        ConsumeAnyToken();

      if (Tok.is(tok::eof) && Tok.getEofData() == LM.D)
        ConsumeAnyToken();
      return;
    }
  } else
    Actions.ActOnDefaultCtorInitializers(LM.D);

  assert((Actions.getDiagnostics().hasErrorOccurred() ||
          !isa<FunctionTemplateDecl>(LM.D) ||
          cast<FunctionTemplateDecl>(LM.D)->getTemplateParameters()->getDepth()
            < TemplateParameterDepth) &&
         "TemplateParameterDepth should be greater than the depth of "
         "current template being instantiated!");

  ParseFunctionStatementBody(LM.D, FnScope);

  while (Tok.isNot(tok::eof))
    ConsumeAnyToken();

  if (Tok.is(tok::eof) && Tok.getEofData() == LM.D)
    ConsumeAnyToken();

  if (auto *FD = dyn_cast_or_null<FunctionDecl>(LM.D))
    if (isa<CXXMethodDecl>(FD) ||
        FD->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend))
      Actions.ActOnFinishInlineFunctionDef(FD);
}

/// ParseLexedMemberInitializers - We finished parsing the member specification
/// of a top (non-nested) C++ class. Now go over the stack of lexed data member
/// initializers that were collected during its parsing and parse them all.
void Parser::ParseLexedMemberInitializers(ParsingClass &Class) {
  ReenterClassScopeRAII InClassScope(*this, Class);

  if (!Class.LateParsedDeclarations.empty()) {
    // C++11 [expr.prim.general]p4:
    //   Otherwise, if a member-declarator declares a non-static data member
    //  (9.2) of a class X, the expression this is a prvalue of type "pointer
    //  to X" within the optional brace-or-equal-initializer. It shall not
    //  appear elsewhere in the member-declarator.
    // FIXME: This should be done in ParseLexedMemberInitializer, not here.
    Sema::CXXThisScopeRAII ThisScope(Actions, Class.TagOrTemplate,
                                     Qualifiers());

    for (LateParsedDeclaration *D : Class.LateParsedDeclarations)
      D->ParseLexedMemberInitializers();
  }

  Actions.ActOnFinishDelayedMemberInitializers(Class.TagOrTemplate);
}

void Parser::ParseLexedMemberInitializer(LateParsedMemberInitializer &MI) {
  if (!MI.Field || MI.Field->isInvalidDecl())
    return;

  ParenBraceBracketBalancer BalancerRAIIObj(*this);

  // Append the current token at the end of the new token stream so that it
  // doesn't get lost.
  MI.Toks.push_back(Tok);
  PP.EnterTokenStream(MI.Toks, true, /*IsReinject*/true);

  // Consume the previously pushed token.
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);

  SourceLocation EqualLoc;

  Actions.ActOnStartCXXInClassMemberInitializer();

  // The initializer isn't actually potentially evaluated unless it is
  // used.
  EnterExpressionEvaluationContext Eval(
      Actions, Sema::ExpressionEvaluationContext::PotentiallyEvaluatedIfUsed);

  ExprResult Init = ParseCXXMemberInitializer(MI.Field, /*IsFunction=*/false,
                                              EqualLoc);

  Actions.ActOnFinishCXXInClassMemberInitializer(MI.Field, EqualLoc,
                                                 Init.get());

  // The next token should be our artificial terminating EOF token.
  if (Tok.isNot(tok::eof)) {
    if (!Init.isInvalid()) {
      SourceLocation EndLoc = PP.getLocForEndOfToken(PrevTokLocation);
      if (!EndLoc.isValid())
        EndLoc = Tok.getLocation();
      // No fixit; we can't recover as if there were a semicolon here.
      Diag(EndLoc, diag::err_expected_semi_decl_list);
    }

    // Consume tokens until we hit the artificial EOF.
    while (Tok.isNot(tok::eof))
      ConsumeAnyToken();
  }
  // Make sure this is *our* artificial EOF token.
  if (Tok.getEofData() == MI.Field)
    ConsumeAnyToken();
}

/// Wrapper class which calls ParseLexedAttribute, after setting up the
/// scope appropriately.
void Parser::ParseLexedAttributes(ParsingClass &Class) {
  ReenterClassScopeRAII InClassScope(*this, Class);

  for (LateParsedDeclaration *LateD : Class.LateParsedDeclarations)
    LateD->ParseLexedAttributes();
}

/// Parse all attributes in LAs, and attach them to Decl D.
void Parser::ParseLexedAttributeList(LateParsedAttrList &LAs, Decl *D,
                                     bool EnterScope, bool OnDefinition) {
  assert(LAs.parseSoon() &&
         "Attribute list should be marked for immediate parsing.");
  for (unsigned i = 0, ni = LAs.size(); i < ni; ++i) {
    if (D)
      LAs[i]->addDecl(D);
    ParseLexedAttribute(*LAs[i], EnterScope, OnDefinition);
    delete LAs[i];
  }
  LAs.clear();
}

/// Finish parsing an attribute for which parsing was delayed.
/// This will be called at the end of parsing a class declaration
/// for each LateParsedAttribute. We consume the saved tokens and
/// create an attribute with the arguments filled in. We add this
/// to the Attribute list for the decl.
void Parser::ParseLexedAttribute(LateParsedAttribute &LA,
                                 bool EnterScope, bool OnDefinition) {
  // Create a fake EOF so that attribute parsing won't go off the end of the
  // attribute.
  Token AttrEnd;
  AttrEnd.startToken();
  AttrEnd.setKind(tok::eof);
  AttrEnd.setLocation(Tok.getLocation());
  AttrEnd.setEofData(LA.Toks.data());
  LA.Toks.push_back(AttrEnd);

  // Append the current token at the end of the new token stream so that it
  // doesn't get lost.
  LA.Toks.push_back(Tok);
  PP.EnterTokenStream(LA.Toks, true, /*IsReinject=*/true);
  // Consume the previously pushed token.
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);

  ParsedAttributes Attrs(AttrFactory);

  if (LA.Decls.size() > 0) {
    Decl *D = LA.Decls[0];
    NamedDecl *ND  = dyn_cast<NamedDecl>(D);
    RecordDecl *RD = dyn_cast_or_null<RecordDecl>(D->getDeclContext());

    // Allow 'this' within late-parsed attributes.
    Sema::CXXThisScopeRAII ThisScope(Actions, RD, Qualifiers(),
                                     ND && ND->isCXXInstanceMember());

    if (LA.Decls.size() == 1) {
      // If the Decl is templatized, add template parameters to scope.
      ReenterTemplateScopeRAII InDeclScope(*this, D, EnterScope);

      // If the Decl is on a function, add function parameters to the scope.
      bool HasFunScope = EnterScope && D->isFunctionOrFunctionTemplate();
      if (HasFunScope) {
        InDeclScope.Scopes.Enter(Scope::FnScope | Scope::DeclScope |
                                 Scope::CompoundStmtScope);
        Actions.ActOnReenterFunctionContext(Actions.CurScope, D);
      }

      ParseGNUAttributeArgs(&LA.AttrName, LA.AttrNameLoc, Attrs, nullptr,
                            nullptr, SourceLocation(), ParsedAttr::Form::GNU(),
                            nullptr);

      if (HasFunScope)
        Actions.ActOnExitFunctionContext();
    } else {
      // If there are multiple decls, then the decl cannot be within the
      // function scope.
      ParseGNUAttributeArgs(&LA.AttrName, LA.AttrNameLoc, Attrs, nullptr,
                            nullptr, SourceLocation(), ParsedAttr::Form::GNU(),
                            nullptr);
    }
  } else {
    Diag(Tok, diag::warn_attribute_no_decl) << LA.AttrName.getName();
  }

  if (OnDefinition && !Attrs.empty() && !Attrs.begin()->isCXX11Attribute() &&
      Attrs.begin()->isKnownToGCC())
    Diag(Tok, diag::warn_attribute_on_function_definition)
      << &LA.AttrName;

  for (unsigned i = 0, ni = LA.Decls.size(); i < ni; ++i)
    Actions.ActOnFinishDelayedAttribute(getCurScope(), LA.Decls[i], Attrs);

  // Due to a parsing error, we either went over the cached tokens or
  // there are still cached tokens left, so we skip the leftover tokens.
  while (Tok.isNot(tok::eof))
    ConsumeAnyToken();

  if (Tok.is(tok::eof) && Tok.getEofData() == AttrEnd.getEofData())
    ConsumeAnyToken();
}

void Parser::ParseLexedPragmas(ParsingClass &Class) {
  ReenterClassScopeRAII InClassScope(*this, Class);

  for (LateParsedDeclaration *D : Class.LateParsedDeclarations)
    D->ParseLexedPragmas();
}

void Parser::ParseLexedPragma(LateParsedPragma &LP) {
  PP.EnterToken(Tok, /*IsReinject=*/true);
  PP.EnterTokenStream(LP.toks(), /*DisableMacroExpansion=*/true,
                      /*IsReinject=*/true);

  // Consume the previously pushed token.
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);
  assert(Tok.isAnnotation() && "Expected annotation token.");
  switch (Tok.getKind()) {
  case tok::annot_attr_openmp:
  case tok::annot_pragma_openmp: {
    AccessSpecifier AS = LP.getAccessSpecifier();
    ParsedAttributes Attrs(AttrFactory);
    (void)ParseOpenMPDeclarativeDirectiveWithExtDecl(AS, Attrs);
    break;
  }
  default:
    llvm_unreachable("Unexpected token.");
  }
}

/// ConsumeAndStoreUntil - Consume and store the token at the passed token
/// container until the token 'T' is reached (which gets
/// consumed/stored too, if ConsumeFinalToken).
/// If StopAtSemi is true, then we will stop early at a ';' character.
/// Returns true if token 'T1' or 'T2' was found.
/// NOTE: This is a specialized version of Parser::SkipUntil.
bool Parser::ConsumeAndStoreUntil(tok::TokenKind T1, tok::TokenKind T2,
                                  CachedTokens &Toks,
                                  bool StopAtSemi, bool ConsumeFinalToken) {
  // We always want this function to consume at least one token if the first
  // token isn't T and if not at EOF.
  bool isFirstTokenConsumed = true;
  while (true) {
    // If we found one of the tokens, stop and return true.
    if (Tok.is(T1) || Tok.is(T2)) {
      if (ConsumeFinalToken) {
        Toks.push_back(Tok);
        ConsumeAnyToken();
      }
      return true;
    }

    switch (Tok.getKind()) {
    case tok::eof:
    case tok::annot_module_begin:
    case tok::annot_module_end:
    case tok::annot_module_include:
    case tok::annot_repl_input_end:
      // Ran out of tokens.
      return false;

    case tok::l_paren:
      // Recursively consume properly-nested parens.
      Toks.push_back(Tok);
      ConsumeParen();
      ConsumeAndStoreUntil(tok::r_paren, Toks, /*StopAtSemi=*/false);
      break;
    case tok::l_square:
      // Recursively consume properly-nested square brackets.
      Toks.push_back(Tok);
      ConsumeBracket();
      ConsumeAndStoreUntil(tok::r_square, Toks, /*StopAtSemi=*/false);
      break;
    case tok::l_brace:
      // Recursively consume properly-nested braces.
      Toks.push_back(Tok);
      ConsumeBrace();
      ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/false);
      break;

    // Okay, we found a ']' or '}' or ')', which we think should be balanced.
    // Since the user wasn't looking for this token (if they were, it would
    // already be handled), this isn't balanced.  If there is a LHS token at a
    // higher level, we will assume that this matches the unbalanced token
    // and return it.  Otherwise, this is a spurious RHS token, which we skip.
    case tok::r_paren:
      if (ParenCount && !isFirstTokenConsumed)
        return false;  // Matches something.
      Toks.push_back(Tok);
      ConsumeParen();
      break;
    case tok::r_square:
      if (BracketCount && !isFirstTokenConsumed)
        return false;  // Matches something.
      Toks.push_back(Tok);
      ConsumeBracket();
      break;
    case tok::r_brace:
      if (BraceCount && !isFirstTokenConsumed)
        return false;  // Matches something.
      Toks.push_back(Tok);
      ConsumeBrace();
      break;

    case tok::semi:
      if (StopAtSemi)
        return false;
      [[fallthrough]];
    default:
      // consume this token.
      Toks.push_back(Tok);
      ConsumeAnyToken(/*ConsumeCodeCompletionTok*/true);
      break;
    }
    isFirstTokenConsumed = false;
  }
}

/// Consume tokens and store them in the passed token container until
/// we've passed the try keyword and constructor initializers and have consumed
/// the opening brace of the function body. The opening brace will be consumed
/// if and only if there was no error.
///
/// \return True on error.
bool Parser::ConsumeAndStoreFunctionPrologue(CachedTokens &Toks) {
  if (Tok.is(tok::kw_try)) {
    Toks.push_back(Tok);
    ConsumeToken();
  }

  if (Tok.isNot(tok::colon)) {
    // Easy case, just a function body.

    // Grab any remaining garbage to be diagnosed later. We stop when we reach a
    // brace: an opening one is the function body, while a closing one probably
    // means we've reached the end of the class.
    ConsumeAndStoreUntil(tok::l_brace, tok::r_brace, Toks,
                         /*StopAtSemi=*/true,
                         /*ConsumeFinalToken=*/false);
    if (Tok.isNot(tok::l_brace))
      return Diag(Tok.getLocation(), diag::err_expected) << tok::l_brace;

    Toks.push_back(Tok);
    ConsumeBrace();
    return false;
  }

  Toks.push_back(Tok);
  ConsumeToken();

  // We can't reliably skip over a mem-initializer-id, because it could be
  // a template-id involving not-yet-declared names. Given:
  //
  //   S ( ) : a < b < c > ( e )
  //
  // 'e' might be an initializer or part of a template argument, depending
  // on whether 'b' is a template.

  // Track whether we might be inside a template argument. We can give
  // significantly better diagnostics if we know that we're not.
  bool MightBeTemplateArgument = false;

  while (true) {
    // Skip over the mem-initializer-id, if possible.
    if (Tok.is(tok::kw_decltype)) {
      Toks.push_back(Tok);
      SourceLocation OpenLoc = ConsumeToken();
      if (Tok.isNot(tok::l_paren))
        return Diag(Tok.getLocation(), diag::err_expected_lparen_after)
                 << "decltype";
      Toks.push_back(Tok);
      ConsumeParen();
      if (!ConsumeAndStoreUntil(tok::r_paren, Toks, /*StopAtSemi=*/true)) {
        Diag(Tok.getLocation(), diag::err_expected) << tok::r_paren;
        Diag(OpenLoc, diag::note_matching) << tok::l_paren;
        return true;
      }
    }
    do {
      // Walk over a component of a nested-name-specifier.
      if (Tok.is(tok::coloncolon)) {
        Toks.push_back(Tok);
        ConsumeToken();

        if (Tok.is(tok::kw_template)) {
          Toks.push_back(Tok);
          ConsumeToken();
        }
      }

      if (Tok.is(tok::identifier)) {
        Toks.push_back(Tok);
        ConsumeToken();
      } else {
        break;
      }
      // Pack indexing
      if (Tok.is(tok::ellipsis) && NextToken().is(tok::l_square)) {
        Toks.push_back(Tok);
        SourceLocation OpenLoc = ConsumeToken();
        Toks.push_back(Tok);
        ConsumeBracket();
        if (!ConsumeAndStoreUntil(tok::r_square, Toks, /*StopAtSemi=*/true)) {
          Diag(Tok.getLocation(), diag::err_expected) << tok::r_square;
          Diag(OpenLoc, diag::note_matching) << tok::l_square;
          return true;
        }
      }

    } while (Tok.is(tok::coloncolon));

    if (Tok.is(tok::code_completion)) {
      Toks.push_back(Tok);
      ConsumeCodeCompletionToken();
      if (Tok.isOneOf(tok::identifier, tok::coloncolon, tok::kw_decltype)) {
        // Could be the start of another member initializer (the ',' has not
        // been written yet)
        continue;
      }
    }

    if (Tok.is(tok::comma)) {
      // The initialization is missing, we'll diagnose it later.
      Toks.push_back(Tok);
      ConsumeToken();
      continue;
    }
    if (Tok.is(tok::less))
      MightBeTemplateArgument = true;

    if (MightBeTemplateArgument) {
      // We may be inside a template argument list. Grab up to the start of the
      // next parenthesized initializer or braced-init-list. This *might* be the
      // initializer, or it might be a subexpression in the template argument
      // list.
      // FIXME: Count angle brackets, and clear MightBeTemplateArgument
      //        if all angles are closed.
      if (!ConsumeAndStoreUntil(tok::l_paren, tok::l_brace, Toks,
                                /*StopAtSemi=*/true,
                                /*ConsumeFinalToken=*/false)) {
        // We're not just missing the initializer, we're also missing the
        // function body!
        return Diag(Tok.getLocation(), diag::err_expected) << tok::l_brace;
      }
    } else if (Tok.isNot(tok::l_paren) && Tok.isNot(tok::l_brace)) {
      // We found something weird in a mem-initializer-id.
      if (getLangOpts().CPlusPlus11)
        return Diag(Tok.getLocation(), diag::err_expected_either)
               << tok::l_paren << tok::l_brace;
      else
        return Diag(Tok.getLocation(), diag::err_expected) << tok::l_paren;
    }

    tok::TokenKind kind = Tok.getKind();
    Toks.push_back(Tok);
    bool IsLParen = (kind == tok::l_paren);
    SourceLocation OpenLoc = Tok.getLocation();

    if (IsLParen) {
      ConsumeParen();
    } else {
      assert(kind == tok::l_brace && "Must be left paren or brace here.");
      ConsumeBrace();
      // In C++03, this has to be the start of the function body, which
      // means the initializer is malformed; we'll diagnose it later.
      if (!getLangOpts().CPlusPlus11)
        return false;

      const Token &PreviousToken = Toks[Toks.size() - 2];
      if (!MightBeTemplateArgument &&
          !PreviousToken.isOneOf(tok::identifier, tok::greater,
                                 tok::greatergreater)) {
        // If the opening brace is not preceded by one of these tokens, we are
        // missing the mem-initializer-id. In order to recover better, we need
        // to use heuristics to determine if this '{' is most likely the
        // beginning of a brace-init-list or the function body.
        // Check the token after the corresponding '}'.
        TentativeParsingAction PA(*this);
        if (SkipUntil(tok::r_brace) &&
            !Tok.isOneOf(tok::comma, tok::ellipsis, tok::l_brace)) {
          // Consider there was a malformed initializer and this is the start
          // of the function body. We'll diagnose it later.
          PA.Revert();
          return false;
        }
        PA.Revert();
      }
    }

    // Grab the initializer (or the subexpression of the template argument).
    // FIXME: If we support lambdas here, we'll need to set StopAtSemi to false
    //        if we might be inside the braces of a lambda-expression.
    tok::TokenKind CloseKind = IsLParen ? tok::r_paren : tok::r_brace;
    if (!ConsumeAndStoreUntil(CloseKind, Toks, /*StopAtSemi=*/true)) {
      Diag(Tok, diag::err_expected) << CloseKind;
      Diag(OpenLoc, diag::note_matching) << kind;
      return true;
    }

    // Grab pack ellipsis, if present.
    if (Tok.is(tok::ellipsis)) {
      Toks.push_back(Tok);
      ConsumeToken();
    }

    // If we know we just consumed a mem-initializer, we must have ',' or '{'
    // next.
    if (Tok.is(tok::comma)) {
      Toks.push_back(Tok);
      ConsumeToken();
    } else if (Tok.is(tok::l_brace)) {
      // This is the function body if the ')' or '}' is immediately followed by
      // a '{'. That cannot happen within a template argument, apart from the
      // case where a template argument contains a compound literal:
      //
      //   S ( ) : a < b < c > ( d ) { }
      //   // End of declaration, or still inside the template argument?
      //
      // ... and the case where the template argument contains a lambda:
      //
      //   S ( ) : a < 0 && b < c > ( d ) + [ ] ( ) { return 0; }
      //     ( ) > ( ) { }
      //
      // FIXME: Disambiguate these cases. Note that the latter case is probably
      //        going to be made ill-formed by core issue 1607.
      Toks.push_back(Tok);
      ConsumeBrace();
      return false;
    } else if (!MightBeTemplateArgument) {
      return Diag(Tok.getLocation(), diag::err_expected_either) << tok::l_brace
                                                                << tok::comma;
    }
  }
}

/// Consume and store tokens from the '?' to the ':' in a conditional
/// expression.
bool Parser::ConsumeAndStoreConditional(CachedTokens &Toks) {
  // Consume '?'.
  assert(Tok.is(tok::question));
  Toks.push_back(Tok);
  ConsumeToken();

  while (Tok.isNot(tok::colon)) {
    if (!ConsumeAndStoreUntil(tok::question, tok::colon, Toks,
                              /*StopAtSemi=*/true,
                              /*ConsumeFinalToken=*/false))
      return false;

    // If we found a nested conditional, consume it.
    if (Tok.is(tok::question) && !ConsumeAndStoreConditional(Toks))
      return false;
  }

  // Consume ':'.
  Toks.push_back(Tok);
  ConsumeToken();
  return true;
}

/// A tentative parsing action that can also revert token annotations.
class Parser::UnannotatedTentativeParsingAction : public TentativeParsingAction {
public:
  explicit UnannotatedTentativeParsingAction(Parser &Self,
                                             tok::TokenKind EndKind)
      : TentativeParsingAction(Self), Self(Self), EndKind(EndKind) {
    // Stash away the old token stream, so we can restore it once the
    // tentative parse is complete.
    TentativeParsingAction Inner(Self);
    Self.ConsumeAndStoreUntil(EndKind, Toks, true, /*ConsumeFinalToken*/false);
    Inner.Revert();
  }

  void RevertAnnotations() {
    Revert();

    // Put back the original tokens.
    Self.SkipUntil(EndKind, StopAtSemi | StopBeforeMatch);
    if (Toks.size()) {
      auto Buffer = std::make_unique<Token[]>(Toks.size());
      std::copy(Toks.begin() + 1, Toks.end(), Buffer.get());
      Buffer[Toks.size() - 1] = Self.Tok;
      Self.PP.EnterTokenStream(std::move(Buffer), Toks.size(), true,
                               /*IsReinject*/ true);

      Self.Tok = Toks.front();
    }
  }

private:
  Parser &Self;
  CachedTokens Toks;
  tok::TokenKind EndKind;
};

/// ConsumeAndStoreInitializer - Consume and store the token at the passed token
/// container until the end of the current initializer expression (either a
/// default argument or an in-class initializer for a non-static data member).
///
/// Returns \c true if we reached the end of something initializer-shaped,
/// \c false if we bailed out.
bool Parser::ConsumeAndStoreInitializer(CachedTokens &Toks,
                                        CachedInitKind CIK) {
  // We always want this function to consume at least one token if not at EOF.
  bool IsFirstToken = true;

  // Number of possible unclosed <s we've seen so far. These might be templates,
  // and might not, but if there were none of them (or we know for sure that
  // we're within a template), we can avoid a tentative parse.
  unsigned AngleCount = 0;
  unsigned KnownTemplateCount = 0;

  while (true) {
    switch (Tok.getKind()) {
    case tok::comma:
      // If we might be in a template, perform a tentative parse to check.
      if (!AngleCount)
        // Not a template argument: this is the end of the initializer.
        return true;
      if (KnownTemplateCount)
        goto consume_token;

      // We hit a comma inside angle brackets. This is the hard case. The
      // rule we follow is:
      //  * For a default argument, if the tokens after the comma form a
      //    syntactically-valid parameter-declaration-clause, in which each
      //    parameter has an initializer, then this comma ends the default
      //    argument.
      //  * For a default initializer, if the tokens after the comma form a
      //    syntactically-valid init-declarator-list, then this comma ends
      //    the default initializer.
      {
        UnannotatedTentativeParsingAction PA(*this,
                                             CIK == CIK_DefaultInitializer
                                               ? tok::semi : tok::r_paren);
        Sema::TentativeAnalysisScope Scope(Actions);

        TPResult Result = TPResult::Error;
        ConsumeToken();
        switch (CIK) {
        case CIK_DefaultInitializer:
          Result = TryParseInitDeclaratorList();
          // If we parsed a complete, ambiguous init-declarator-list, this
          // is only syntactically-valid if it's followed by a semicolon.
          if (Result == TPResult::Ambiguous && Tok.isNot(tok::semi))
            Result = TPResult::False;
          break;

        case CIK_DefaultArgument:
          bool InvalidAsDeclaration = false;
          Result = TryParseParameterDeclarationClause(
              &InvalidAsDeclaration, /*VersusTemplateArg=*/true);
          // If this is an expression or a declaration with a missing
          // 'typename', assume it's not a declaration.
          if (Result == TPResult::Ambiguous && InvalidAsDeclaration)
            Result = TPResult::False;
          break;
        }

        // Put the token stream back and undo any annotations we performed
        // after the comma. They may reflect a different parse than the one
        // we will actually perform at the end of the class.
        PA.RevertAnnotations();

        // If what follows could be a declaration, it is a declaration.
        if (Result != TPResult::False && Result != TPResult::Error)
          return true;
      }

      // Keep going. We know we're inside a template argument list now.
      ++KnownTemplateCount;
      goto consume_token;

    case tok::eof:
    case tok::annot_module_begin:
    case tok::annot_module_end:
    case tok::annot_module_include:
    case tok::annot_repl_input_end:
      // Ran out of tokens.
      return false;

    case tok::less:
      // FIXME: A '<' can only start a template-id if it's preceded by an
      // identifier, an operator-function-id, or a literal-operator-id.
      ++AngleCount;
      goto consume_token;

    case tok::question:
      // In 'a ? b : c', 'b' can contain an unparenthesized comma. If it does,
      // that is *never* the end of the initializer. Skip to the ':'.
      if (!ConsumeAndStoreConditional(Toks))
        return false;
      break;

    case tok::greatergreatergreater:
      if (!getLangOpts().CPlusPlus11)
        goto consume_token;
      if (AngleCount) --AngleCount;
      if (KnownTemplateCount) --KnownTemplateCount;
      [[fallthrough]];
    case tok::greatergreater:
      if (!getLangOpts().CPlusPlus11)
        goto consume_token;
      if (AngleCount) --AngleCount;
      if (KnownTemplateCount) --KnownTemplateCount;
      [[fallthrough]];
    case tok::greater:
      if (AngleCount) --AngleCount;
      if (KnownTemplateCount) --KnownTemplateCount;
      goto consume_token;

    case tok::kw_template:
      // 'template' identifier '<' is known to start a template argument list,
      // and can be used to disambiguate the parse.
      // FIXME: Support all forms of 'template' unqualified-id '<'.
      Toks.push_back(Tok);
      ConsumeToken();
      if (Tok.is(tok::identifier)) {
        Toks.push_back(Tok);
        ConsumeToken();
        if (Tok.is(tok::less)) {
          ++AngleCount;
          ++KnownTemplateCount;
          Toks.push_back(Tok);
          ConsumeToken();
        }
      }
      break;

    case tok::kw_operator:
      // If 'operator' precedes other punctuation, that punctuation loses
      // its special behavior.
      Toks.push_back(Tok);
      ConsumeToken();
      switch (Tok.getKind()) {
      case tok::comma:
      case tok::greatergreatergreater:
      case tok::greatergreater:
      case tok::greater:
      case tok::less:
        Toks.push_back(Tok);
        ConsumeToken();
        break;
      default:
        break;
      }
      break;

    case tok::l_paren:
      // Recursively consume properly-nested parens.
      Toks.push_back(Tok);
      ConsumeParen();
      ConsumeAndStoreUntil(tok::r_paren, Toks, /*StopAtSemi=*/false);
      break;
    case tok::l_square:
      // Recursively consume properly-nested square brackets.
      Toks.push_back(Tok);
      ConsumeBracket();
      ConsumeAndStoreUntil(tok::r_square, Toks, /*StopAtSemi=*/false);
      break;
    case tok::l_brace:
      // Recursively consume properly-nested braces.
      Toks.push_back(Tok);
      ConsumeBrace();
      ConsumeAndStoreUntil(tok::r_brace, Toks, /*StopAtSemi=*/false);
      break;

    // Okay, we found a ']' or '}' or ')', which we think should be balanced.
    // Since the user wasn't looking for this token (if they were, it would
    // already be handled), this isn't balanced.  If there is a LHS token at a
    // higher level, we will assume that this matches the unbalanced token
    // and return it.  Otherwise, this is a spurious RHS token, which we
    // consume and pass on to downstream code to diagnose.
    case tok::r_paren:
      if (CIK == CIK_DefaultArgument)
        return true; // End of the default argument.
      if (ParenCount && !IsFirstToken)
        return false;
      Toks.push_back(Tok);
      ConsumeParen();
      continue;
    case tok::r_square:
      if (BracketCount && !IsFirstToken)
        return false;
      Toks.push_back(Tok);
      ConsumeBracket();
      continue;
    case tok::r_brace:
      if (BraceCount && !IsFirstToken)
        return false;
      Toks.push_back(Tok);
      ConsumeBrace();
      continue;

    case tok::code_completion:
      Toks.push_back(Tok);
      ConsumeCodeCompletionToken();
      break;

    case tok::string_literal:
    case tok::wide_string_literal:
    case tok::utf8_string_literal:
    case tok::utf16_string_literal:
    case tok::utf32_string_literal:
      Toks.push_back(Tok);
      ConsumeStringToken();
      break;
    case tok::semi:
      if (CIK == CIK_DefaultInitializer)
        return true; // End of the default initializer.
      [[fallthrough]];
    default:
    consume_token:
      Toks.push_back(Tok);
      ConsumeToken();
      break;
    }
    IsFirstToken = false;
  }
}
