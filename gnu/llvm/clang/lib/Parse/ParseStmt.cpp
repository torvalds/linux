//===--- ParseStmt.cpp - Statement and Block Parser -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Statement and Block portions of the Parser
// interface.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/PrettyDeclStackTrace.h"
#include "clang/Basic/Attributes.h"
#include "clang/Basic/PrettyStackTrace.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Parse/LoopHint.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaCodeCompletion.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaOpenMP.h"
#include "clang/Sema/TypoCorrection.h"
#include "llvm/ADT/STLExtras.h"
#include <optional>

using namespace clang;

//===----------------------------------------------------------------------===//
// C99 6.8: Statements and Blocks.
//===----------------------------------------------------------------------===//

/// Parse a standalone statement (for instance, as the body of an 'if',
/// 'while', or 'for').
StmtResult Parser::ParseStatement(SourceLocation *TrailingElseLoc,
                                  ParsedStmtContext StmtCtx) {
  StmtResult Res;

  // We may get back a null statement if we found a #pragma. Keep going until
  // we get an actual statement.
  StmtVector Stmts;
  do {
    Res = ParseStatementOrDeclaration(Stmts, StmtCtx, TrailingElseLoc);
  } while (!Res.isInvalid() && !Res.get());

  return Res;
}

/// ParseStatementOrDeclaration - Read 'statement' or 'declaration'.
///       StatementOrDeclaration:
///         statement
///         declaration
///
///       statement:
///         labeled-statement
///         compound-statement
///         expression-statement
///         selection-statement
///         iteration-statement
///         jump-statement
/// [C++]   declaration-statement
/// [C++]   try-block
/// [MS]    seh-try-block
/// [OBC]   objc-throw-statement
/// [OBC]   objc-try-catch-statement
/// [OBC]   objc-synchronized-statement
/// [GNU]   asm-statement
/// [OMP]   openmp-construct             [TODO]
///
///       labeled-statement:
///         identifier ':' statement
///         'case' constant-expression ':' statement
///         'default' ':' statement
///
///       selection-statement:
///         if-statement
///         switch-statement
///
///       iteration-statement:
///         while-statement
///         do-statement
///         for-statement
///
///       expression-statement:
///         expression[opt] ';'
///
///       jump-statement:
///         'goto' identifier ';'
///         'continue' ';'
///         'break' ';'
///         'return' expression[opt] ';'
/// [GNU]   'goto' '*' expression ';'
///
/// [OBC] objc-throw-statement:
/// [OBC]   '@' 'throw' expression ';'
/// [OBC]   '@' 'throw' ';'
///
StmtResult
Parser::ParseStatementOrDeclaration(StmtVector &Stmts,
                                    ParsedStmtContext StmtCtx,
                                    SourceLocation *TrailingElseLoc) {

  ParenBraceBracketBalancer BalancerRAIIObj(*this);

  // Because we're parsing either a statement or a declaration, the order of
  // attribute parsing is important. [[]] attributes at the start of a
  // statement are different from [[]] attributes that follow an __attribute__
  // at the start of the statement. Thus, we're not using MaybeParseAttributes
  // here because we don't want to allow arbitrary orderings.
  ParsedAttributes CXX11Attrs(AttrFactory);
  MaybeParseCXX11Attributes(CXX11Attrs, /*MightBeObjCMessageSend*/ true);
  ParsedAttributes GNUOrMSAttrs(AttrFactory);
  if (getLangOpts().OpenCL)
    MaybeParseGNUAttributes(GNUOrMSAttrs);

  if (getLangOpts().HLSL)
    MaybeParseMicrosoftAttributes(GNUOrMSAttrs);

  StmtResult Res = ParseStatementOrDeclarationAfterAttributes(
      Stmts, StmtCtx, TrailingElseLoc, CXX11Attrs, GNUOrMSAttrs);
  MaybeDestroyTemplateIds();

  // Attributes that are left should all go on the statement, so concatenate the
  // two lists.
  ParsedAttributes Attrs(AttrFactory);
  takeAndConcatenateAttrs(CXX11Attrs, GNUOrMSAttrs, Attrs);

  assert((Attrs.empty() || Res.isInvalid() || Res.isUsable()) &&
         "attributes on empty statement");

  if (Attrs.empty() || Res.isInvalid())
    return Res;

  return Actions.ActOnAttributedStmt(Attrs, Res.get());
}

namespace {
class StatementFilterCCC final : public CorrectionCandidateCallback {
public:
  StatementFilterCCC(Token nextTok) : NextToken(nextTok) {
    WantTypeSpecifiers = nextTok.isOneOf(tok::l_paren, tok::less, tok::l_square,
                                         tok::identifier, tok::star, tok::amp);
    WantExpressionKeywords =
        nextTok.isOneOf(tok::l_paren, tok::identifier, tok::arrow, tok::period);
    WantRemainingKeywords =
        nextTok.isOneOf(tok::l_paren, tok::semi, tok::identifier, tok::l_brace);
    WantCXXNamedCasts = false;
  }

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    if (FieldDecl *FD = candidate.getCorrectionDeclAs<FieldDecl>())
      return !candidate.getCorrectionSpecifier() || isa<ObjCIvarDecl>(FD);
    if (NextToken.is(tok::equal))
      return candidate.getCorrectionDeclAs<VarDecl>();
    if (NextToken.is(tok::period) &&
        candidate.getCorrectionDeclAs<NamespaceDecl>())
      return false;
    return CorrectionCandidateCallback::ValidateCandidate(candidate);
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<StatementFilterCCC>(*this);
  }

private:
  Token NextToken;
};
}

StmtResult Parser::ParseStatementOrDeclarationAfterAttributes(
    StmtVector &Stmts, ParsedStmtContext StmtCtx,
    SourceLocation *TrailingElseLoc, ParsedAttributes &CXX11Attrs,
    ParsedAttributes &GNUAttrs) {
  const char *SemiError = nullptr;
  StmtResult Res;
  SourceLocation GNUAttributeLoc;

  // Cases in this switch statement should fall through if the parser expects
  // the token to end in a semicolon (in which case SemiError should be set),
  // or they directly 'return;' if not.
Retry:
  tok::TokenKind Kind  = Tok.getKind();
  SourceLocation AtLoc;
  switch (Kind) {
  case tok::at: // May be a @try or @throw statement
    {
      AtLoc = ConsumeToken();  // consume @
      return ParseObjCAtStatement(AtLoc, StmtCtx);
    }

  case tok::code_completion:
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteOrdinaryName(
        getCurScope(), SemaCodeCompletion::PCC_Statement);
    return StmtError();

  case tok::identifier:
  ParseIdentifier: {
    Token Next = NextToken();
    if (Next.is(tok::colon)) { // C99 6.8.1: labeled-statement
      // Both C++11 and GNU attributes preceding the label appertain to the
      // label, so put them in a single list to pass on to
      // ParseLabeledStatement().
      ParsedAttributes Attrs(AttrFactory);
      takeAndConcatenateAttrs(CXX11Attrs, GNUAttrs, Attrs);

      // identifier ':' statement
      return ParseLabeledStatement(Attrs, StmtCtx);
    }

    // Look up the identifier, and typo-correct it to a keyword if it's not
    // found.
    if (Next.isNot(tok::coloncolon)) {
      // Try to limit which sets of keywords should be included in typo
      // correction based on what the next token is.
      StatementFilterCCC CCC(Next);
      if (TryAnnotateName(&CCC) == ANK_Error) {
        // Handle errors here by skipping up to the next semicolon or '}', and
        // eat the semicolon if that's what stopped us.
        SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
        if (Tok.is(tok::semi))
          ConsumeToken();
        return StmtError();
      }

      // If the identifier was typo-corrected, try again.
      if (Tok.isNot(tok::identifier))
        goto Retry;
    }

    // Fall through
    [[fallthrough]];
  }

  default: {
    bool HaveAttrs = !CXX11Attrs.empty() || !GNUAttrs.empty();
    auto IsStmtAttr = [](ParsedAttr &Attr) { return Attr.isStmtAttr(); };
    bool AllAttrsAreStmtAttrs = llvm::all_of(CXX11Attrs, IsStmtAttr) &&
                                llvm::all_of(GNUAttrs, IsStmtAttr);
    // In C, the grammar production for statement (C23 6.8.1p1) does not allow
    // for declarations, which is different from C++ (C++23 [stmt.pre]p1). So
    // in C++, we always allow a declaration, but in C we need to check whether
    // we're in a statement context that allows declarations. e.g., in C, the
    // following is invalid: if (1) int x;
    if ((getLangOpts().CPlusPlus || getLangOpts().MicrosoftExt ||
         (StmtCtx & ParsedStmtContext::AllowDeclarationsInC) !=
             ParsedStmtContext()) &&
        ((GNUAttributeLoc.isValid() && !(HaveAttrs && AllAttrsAreStmtAttrs)) ||
         isDeclarationStatement())) {
      SourceLocation DeclStart = Tok.getLocation(), DeclEnd;
      DeclGroupPtrTy Decl;
      if (GNUAttributeLoc.isValid()) {
        DeclStart = GNUAttributeLoc;
        Decl = ParseDeclaration(DeclaratorContext::Block, DeclEnd, CXX11Attrs,
                                GNUAttrs, &GNUAttributeLoc);
      } else {
        Decl = ParseDeclaration(DeclaratorContext::Block, DeclEnd, CXX11Attrs,
                                GNUAttrs);
      }
      if (CXX11Attrs.Range.getBegin().isValid()) {
        // The caller must guarantee that the CXX11Attrs appear before the
        // GNUAttrs, and we rely on that here.
        assert(GNUAttrs.Range.getBegin().isInvalid() ||
               GNUAttrs.Range.getBegin() > CXX11Attrs.Range.getBegin());
        DeclStart = CXX11Attrs.Range.getBegin();
      } else if (GNUAttrs.Range.getBegin().isValid())
        DeclStart = GNUAttrs.Range.getBegin();
      return Actions.ActOnDeclStmt(Decl, DeclStart, DeclEnd);
    }

    if (Tok.is(tok::r_brace)) {
      Diag(Tok, diag::err_expected_statement);
      return StmtError();
    }

    switch (Tok.getKind()) {
#define TRANSFORM_TYPE_TRAIT_DEF(_, Trait) case tok::kw___##Trait:
#include "clang/Basic/TransformTypeTraits.def"
      if (NextToken().is(tok::less)) {
        Tok.setKind(tok::identifier);
        Diag(Tok, diag::ext_keyword_as_ident)
            << Tok.getIdentifierInfo()->getName() << 0;
        goto ParseIdentifier;
      }
      [[fallthrough]];
    default:
      return ParseExprStatement(StmtCtx);
    }
  }

  case tok::kw___attribute: {
    GNUAttributeLoc = Tok.getLocation();
    ParseGNUAttributes(GNUAttrs);
    goto Retry;
  }

  case tok::kw_case:                // C99 6.8.1: labeled-statement
    return ParseCaseStatement(StmtCtx);
  case tok::kw_default:             // C99 6.8.1: labeled-statement
    return ParseDefaultStatement(StmtCtx);

  case tok::l_brace:                // C99 6.8.2: compound-statement
    return ParseCompoundStatement();
  case tok::semi: {                 // C99 6.8.3p3: expression[opt] ';'
    bool HasLeadingEmptyMacro = Tok.hasLeadingEmptyMacro();
    return Actions.ActOnNullStmt(ConsumeToken(), HasLeadingEmptyMacro);
  }

  case tok::kw_if:                  // C99 6.8.4.1: if-statement
    return ParseIfStatement(TrailingElseLoc);
  case tok::kw_switch:              // C99 6.8.4.2: switch-statement
    return ParseSwitchStatement(TrailingElseLoc);

  case tok::kw_while:               // C99 6.8.5.1: while-statement
    return ParseWhileStatement(TrailingElseLoc);
  case tok::kw_do:                  // C99 6.8.5.2: do-statement
    Res = ParseDoStatement();
    SemiError = "do/while";
    break;
  case tok::kw_for:                 // C99 6.8.5.3: for-statement
    return ParseForStatement(TrailingElseLoc);

  case tok::kw_goto:                // C99 6.8.6.1: goto-statement
    Res = ParseGotoStatement();
    SemiError = "goto";
    break;
  case tok::kw_continue:            // C99 6.8.6.2: continue-statement
    Res = ParseContinueStatement();
    SemiError = "continue";
    break;
  case tok::kw_break:               // C99 6.8.6.3: break-statement
    Res = ParseBreakStatement();
    SemiError = "break";
    break;
  case tok::kw_return:              // C99 6.8.6.4: return-statement
    Res = ParseReturnStatement();
    SemiError = "return";
    break;
  case tok::kw_co_return:            // C++ Coroutines: co_return statement
    Res = ParseReturnStatement();
    SemiError = "co_return";
    break;

  case tok::kw_asm: {
    for (const ParsedAttr &AL : CXX11Attrs)
      // Could be relaxed if asm-related regular keyword attributes are
      // added later.
      (AL.isRegularKeywordAttribute()
           ? Diag(AL.getRange().getBegin(), diag::err_keyword_not_allowed)
           : Diag(AL.getRange().getBegin(), diag::warn_attribute_ignored))
          << AL;
    // Prevent these from being interpreted as statement attributes later on.
    CXX11Attrs.clear();
    ProhibitAttributes(GNUAttrs);
    bool msAsm = false;
    Res = ParseAsmStatement(msAsm);
    if (msAsm) return Res;
    SemiError = "asm";
    break;
  }

  case tok::kw___if_exists:
  case tok::kw___if_not_exists:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    ParseMicrosoftIfExistsStatement(Stmts);
    // An __if_exists block is like a compound statement, but it doesn't create
    // a new scope.
    return StmtEmpty();

  case tok::kw_try:                 // C++ 15: try-block
    return ParseCXXTryBlock();

  case tok::kw___try:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    return ParseSEHTryBlock();

  case tok::kw___leave:
    Res = ParseSEHLeaveStatement();
    SemiError = "__leave";
    break;

  case tok::annot_pragma_vis:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    HandlePragmaVisibility();
    return StmtEmpty();

  case tok::annot_pragma_pack:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    HandlePragmaPack();
    return StmtEmpty();

  case tok::annot_pragma_msstruct:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    HandlePragmaMSStruct();
    return StmtEmpty();

  case tok::annot_pragma_align:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    HandlePragmaAlign();
    return StmtEmpty();

  case tok::annot_pragma_weak:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    HandlePragmaWeak();
    return StmtEmpty();

  case tok::annot_pragma_weakalias:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    HandlePragmaWeakAlias();
    return StmtEmpty();

  case tok::annot_pragma_redefine_extname:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    HandlePragmaRedefineExtname();
    return StmtEmpty();

  case tok::annot_pragma_fp_contract:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    Diag(Tok, diag::err_pragma_file_or_compound_scope) << "fp_contract";
    ConsumeAnnotationToken();
    return StmtError();

  case tok::annot_pragma_fp:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    Diag(Tok, diag::err_pragma_file_or_compound_scope) << "clang fp";
    ConsumeAnnotationToken();
    return StmtError();

  case tok::annot_pragma_fenv_access:
  case tok::annot_pragma_fenv_access_ms:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    Diag(Tok, diag::err_pragma_file_or_compound_scope)
        << (Kind == tok::annot_pragma_fenv_access ? "STDC FENV_ACCESS"
                                                    : "fenv_access");
    ConsumeAnnotationToken();
    return StmtEmpty();

  case tok::annot_pragma_fenv_round:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    Diag(Tok, diag::err_pragma_file_or_compound_scope) << "STDC FENV_ROUND";
    ConsumeAnnotationToken();
    return StmtError();

  case tok::annot_pragma_cx_limited_range:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    Diag(Tok, diag::err_pragma_file_or_compound_scope)
        << "STDC CX_LIMITED_RANGE";
    ConsumeAnnotationToken();
    return StmtError();

  case tok::annot_pragma_float_control:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    Diag(Tok, diag::err_pragma_file_or_compound_scope) << "float_control";
    ConsumeAnnotationToken();
    return StmtError();

  case tok::annot_pragma_opencl_extension:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    HandlePragmaOpenCLExtension();
    return StmtEmpty();

  case tok::annot_pragma_captured:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    return HandlePragmaCaptured();

  case tok::annot_pragma_openmp:
    // Prohibit attributes that are not OpenMP attributes, but only before
    // processing a #pragma omp clause.
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    [[fallthrough]];
  case tok::annot_attr_openmp:
    // Do not prohibit attributes if they were OpenMP attributes.
    return ParseOpenMPDeclarativeOrExecutableDirective(StmtCtx);

  case tok::annot_pragma_openacc:
    return ParseOpenACCDirectiveStmt();

  case tok::annot_pragma_ms_pointers_to_members:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    HandlePragmaMSPointersToMembers();
    return StmtEmpty();

  case tok::annot_pragma_ms_pragma:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    HandlePragmaMSPragma();
    return StmtEmpty();

  case tok::annot_pragma_ms_vtordisp:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    HandlePragmaMSVtorDisp();
    return StmtEmpty();

  case tok::annot_pragma_loop_hint:
    ProhibitAttributes(CXX11Attrs);
    ProhibitAttributes(GNUAttrs);
    return ParsePragmaLoopHint(Stmts, StmtCtx, TrailingElseLoc, CXX11Attrs);

  case tok::annot_pragma_dump:
    HandlePragmaDump();
    return StmtEmpty();

  case tok::annot_pragma_attribute:
    HandlePragmaAttribute();
    return StmtEmpty();
  }

  // If we reached this code, the statement must end in a semicolon.
  if (!TryConsumeToken(tok::semi) && !Res.isInvalid()) {
    // If the result was valid, then we do want to diagnose this.  Use
    // ExpectAndConsume to emit the diagnostic, even though we know it won't
    // succeed.
    ExpectAndConsume(tok::semi, diag::err_expected_semi_after_stmt, SemiError);
    // Skip until we see a } or ;, but don't eat it.
    SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
  }

  return Res;
}

/// Parse an expression statement.
StmtResult Parser::ParseExprStatement(ParsedStmtContext StmtCtx) {
  // If a case keyword is missing, this is where it should be inserted.
  Token OldToken = Tok;

  ExprStatementTokLoc = Tok.getLocation();

  // expression[opt] ';'
  ExprResult Expr(ParseExpression());
  if (Expr.isInvalid()) {
    // If the expression is invalid, skip ahead to the next semicolon or '}'.
    // Not doing this opens us up to the possibility of infinite loops if
    // ParseExpression does not consume any tokens.
    SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
    if (Tok.is(tok::semi))
      ConsumeToken();
    return Actions.ActOnExprStmtError();
  }

  if (Tok.is(tok::colon) && getCurScope()->isSwitchScope() &&
      Actions.CheckCaseExpression(Expr.get())) {
    // If a constant expression is followed by a colon inside a switch block,
    // suggest a missing case keyword.
    Diag(OldToken, diag::err_expected_case_before_expression)
      << FixItHint::CreateInsertion(OldToken.getLocation(), "case ");

    // Recover parsing as a case statement.
    return ParseCaseStatement(StmtCtx, /*MissingCase=*/true, Expr);
  }

  Token *CurTok = nullptr;
  // Note we shouldn't eat the token since the callback needs it.
  if (Tok.is(tok::annot_repl_input_end))
    CurTok = &Tok;
  else
    // Otherwise, eat the semicolon.
    ExpectAndConsumeSemi(diag::err_expected_semi_after_expr);

  StmtResult R = handleExprStmt(Expr, StmtCtx);
  if (CurTok && !R.isInvalid())
    CurTok->setAnnotationValue(R.get());

  return R;
}

/// ParseSEHTryBlockCommon
///
/// seh-try-block:
///   '__try' compound-statement seh-handler
///
/// seh-handler:
///   seh-except-block
///   seh-finally-block
///
StmtResult Parser::ParseSEHTryBlock() {
  assert(Tok.is(tok::kw___try) && "Expected '__try'");
  SourceLocation TryLoc = ConsumeToken();

  if (Tok.isNot(tok::l_brace))
    return StmtError(Diag(Tok, diag::err_expected) << tok::l_brace);

  StmtResult TryBlock(ParseCompoundStatement(
      /*isStmtExpr=*/false,
      Scope::DeclScope | Scope::CompoundStmtScope | Scope::SEHTryScope));
  if (TryBlock.isInvalid())
    return TryBlock;

  StmtResult Handler;
  if (Tok.is(tok::identifier) &&
      Tok.getIdentifierInfo() == getSEHExceptKeyword()) {
    SourceLocation Loc = ConsumeToken();
    Handler = ParseSEHExceptBlock(Loc);
  } else if (Tok.is(tok::kw___finally)) {
    SourceLocation Loc = ConsumeToken();
    Handler = ParseSEHFinallyBlock(Loc);
  } else {
    return StmtError(Diag(Tok, diag::err_seh_expected_handler));
  }

  if(Handler.isInvalid())
    return Handler;

  return Actions.ActOnSEHTryBlock(false /* IsCXXTry */,
                                  TryLoc,
                                  TryBlock.get(),
                                  Handler.get());
}

/// ParseSEHExceptBlock - Handle __except
///
/// seh-except-block:
///   '__except' '(' seh-filter-expression ')' compound-statement
///
StmtResult Parser::ParseSEHExceptBlock(SourceLocation ExceptLoc) {
  PoisonIdentifierRAIIObject raii(Ident__exception_code, false),
    raii2(Ident___exception_code, false),
    raii3(Ident_GetExceptionCode, false);

  if (ExpectAndConsume(tok::l_paren))
    return StmtError();

  ParseScope ExpectScope(this, Scope::DeclScope | Scope::ControlScope |
                                   Scope::SEHExceptScope);

  if (getLangOpts().Borland) {
    Ident__exception_info->setIsPoisoned(false);
    Ident___exception_info->setIsPoisoned(false);
    Ident_GetExceptionInfo->setIsPoisoned(false);
  }

  ExprResult FilterExpr;
  {
    ParseScopeFlags FilterScope(this, getCurScope()->getFlags() |
                                          Scope::SEHFilterScope);
    FilterExpr = Actions.CorrectDelayedTyposInExpr(ParseExpression());
  }

  if (getLangOpts().Borland) {
    Ident__exception_info->setIsPoisoned(true);
    Ident___exception_info->setIsPoisoned(true);
    Ident_GetExceptionInfo->setIsPoisoned(true);
  }

  if(FilterExpr.isInvalid())
    return StmtError();

  if (ExpectAndConsume(tok::r_paren))
    return StmtError();

  if (Tok.isNot(tok::l_brace))
    return StmtError(Diag(Tok, diag::err_expected) << tok::l_brace);

  StmtResult Block(ParseCompoundStatement());

  if(Block.isInvalid())
    return Block;

  return Actions.ActOnSEHExceptBlock(ExceptLoc, FilterExpr.get(), Block.get());
}

/// ParseSEHFinallyBlock - Handle __finally
///
/// seh-finally-block:
///   '__finally' compound-statement
///
StmtResult Parser::ParseSEHFinallyBlock(SourceLocation FinallyLoc) {
  PoisonIdentifierRAIIObject raii(Ident__abnormal_termination, false),
    raii2(Ident___abnormal_termination, false),
    raii3(Ident_AbnormalTermination, false);

  if (Tok.isNot(tok::l_brace))
    return StmtError(Diag(Tok, diag::err_expected) << tok::l_brace);

  ParseScope FinallyScope(this, 0);
  Actions.ActOnStartSEHFinallyBlock();

  StmtResult Block(ParseCompoundStatement());
  if(Block.isInvalid()) {
    Actions.ActOnAbortSEHFinallyBlock();
    return Block;
  }

  return Actions.ActOnFinishSEHFinallyBlock(FinallyLoc, Block.get());
}

/// Handle __leave
///
/// seh-leave-statement:
///   '__leave' ';'
///
StmtResult Parser::ParseSEHLeaveStatement() {
  SourceLocation LeaveLoc = ConsumeToken();  // eat the '__leave'.
  return Actions.ActOnSEHLeaveStmt(LeaveLoc, getCurScope());
}

static void DiagnoseLabelFollowedByDecl(Parser &P, const Stmt *SubStmt) {
  // When in C mode (but not Microsoft extensions mode), diagnose use of a
  // label that is followed by a declaration rather than a statement.
  if (!P.getLangOpts().CPlusPlus && !P.getLangOpts().MicrosoftExt &&
      isa<DeclStmt>(SubStmt)) {
    P.Diag(SubStmt->getBeginLoc(),
           P.getLangOpts().C23
               ? diag::warn_c23_compat_label_followed_by_declaration
               : diag::ext_c_label_followed_by_declaration);
  }
}

/// ParseLabeledStatement - We have an identifier and a ':' after it.
///
///       label:
///         identifier ':'
/// [GNU]   identifier ':' attributes[opt]
///
///       labeled-statement:
///         label statement
///
StmtResult Parser::ParseLabeledStatement(ParsedAttributes &Attrs,
                                         ParsedStmtContext StmtCtx) {
  assert(Tok.is(tok::identifier) && Tok.getIdentifierInfo() &&
         "Not an identifier!");

  // [OpenMP 5.1] 2.1.3: A stand-alone directive may not be used in place of a
  // substatement in a selection statement, in place of the loop body in an
  // iteration statement, or in place of the statement that follows a label.
  StmtCtx &= ~ParsedStmtContext::AllowStandaloneOpenMPDirectives;

  Token IdentTok = Tok;  // Save the whole token.
  ConsumeToken();  // eat the identifier.

  assert(Tok.is(tok::colon) && "Not a label!");

  // identifier ':' statement
  SourceLocation ColonLoc = ConsumeToken();

  // Read label attributes, if present.
  StmtResult SubStmt;
  if (Tok.is(tok::kw___attribute)) {
    ParsedAttributes TempAttrs(AttrFactory);
    ParseGNUAttributes(TempAttrs);

    // In C++, GNU attributes only apply to the label if they are followed by a
    // semicolon, to disambiguate label attributes from attributes on a labeled
    // declaration.
    //
    // This doesn't quite match what GCC does; if the attribute list is empty
    // and followed by a semicolon, GCC will reject (it appears to parse the
    // attributes as part of a statement in that case). That looks like a bug.
    if (!getLangOpts().CPlusPlus || Tok.is(tok::semi))
      Attrs.takeAllFrom(TempAttrs);
    else {
      StmtVector Stmts;
      ParsedAttributes EmptyCXX11Attrs(AttrFactory);
      SubStmt = ParseStatementOrDeclarationAfterAttributes(
          Stmts, StmtCtx, nullptr, EmptyCXX11Attrs, TempAttrs);
      if (!TempAttrs.empty() && !SubStmt.isInvalid())
        SubStmt = Actions.ActOnAttributedStmt(TempAttrs, SubStmt.get());
    }
  }

  // The label may have no statement following it
  if (SubStmt.isUnset() && Tok.is(tok::r_brace)) {
    DiagnoseLabelAtEndOfCompoundStatement();
    SubStmt = Actions.ActOnNullStmt(ColonLoc);
  }

  // If we've not parsed a statement yet, parse one now.
  if (!SubStmt.isInvalid() && !SubStmt.isUsable())
    SubStmt = ParseStatement(nullptr, StmtCtx);

  // Broken substmt shouldn't prevent the label from being added to the AST.
  if (SubStmt.isInvalid())
    SubStmt = Actions.ActOnNullStmt(ColonLoc);

  DiagnoseLabelFollowedByDecl(*this, SubStmt.get());

  LabelDecl *LD = Actions.LookupOrCreateLabel(IdentTok.getIdentifierInfo(),
                                              IdentTok.getLocation());
  Actions.ProcessDeclAttributeList(Actions.CurScope, LD, Attrs);
  Attrs.clear();

  return Actions.ActOnLabelStmt(IdentTok.getLocation(), LD, ColonLoc,
                                SubStmt.get());
}

/// ParseCaseStatement
///       labeled-statement:
///         'case' constant-expression ':' statement
/// [GNU]   'case' constant-expression '...' constant-expression ':' statement
///
StmtResult Parser::ParseCaseStatement(ParsedStmtContext StmtCtx,
                                      bool MissingCase, ExprResult Expr) {
  assert((MissingCase || Tok.is(tok::kw_case)) && "Not a case stmt!");

  // [OpenMP 5.1] 2.1.3: A stand-alone directive may not be used in place of a
  // substatement in a selection statement, in place of the loop body in an
  // iteration statement, or in place of the statement that follows a label.
  StmtCtx &= ~ParsedStmtContext::AllowStandaloneOpenMPDirectives;

  // It is very common for code to contain many case statements recursively
  // nested, as in (but usually without indentation):
  //  case 1:
  //    case 2:
  //      case 3:
  //         case 4:
  //           case 5: etc.
  //
  // Parsing this naively works, but is both inefficient and can cause us to run
  // out of stack space in our recursive descent parser.  As a special case,
  // flatten this recursion into an iterative loop.  This is complex and gross,
  // but all the grossness is constrained to ParseCaseStatement (and some
  // weirdness in the actions), so this is just local grossness :).

  // TopLevelCase - This is the highest level we have parsed.  'case 1' in the
  // example above.
  StmtResult TopLevelCase(true);

  // DeepestParsedCaseStmt - This is the deepest statement we have parsed, which
  // gets updated each time a new case is parsed, and whose body is unset so
  // far.  When parsing 'case 4', this is the 'case 3' node.
  Stmt *DeepestParsedCaseStmt = nullptr;

  // While we have case statements, eat and stack them.
  SourceLocation ColonLoc;
  do {
    SourceLocation CaseLoc = MissingCase ? Expr.get()->getExprLoc() :
                                           ConsumeToken();  // eat the 'case'.
    ColonLoc = SourceLocation();

    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteCase(getCurScope());
      return StmtError();
    }

    /// We don't want to treat 'case x : y' as a potential typo for 'case x::y'.
    /// Disable this form of error recovery while we're parsing the case
    /// expression.
    ColonProtectionRAIIObject ColonProtection(*this);

    ExprResult LHS;
    if (!MissingCase) {
      LHS = ParseCaseExpression(CaseLoc);
      if (LHS.isInvalid()) {
        // If constant-expression is parsed unsuccessfully, recover by skipping
        // current case statement (moving to the colon that ends it).
        if (!SkipUntil(tok::colon, tok::r_brace, StopAtSemi | StopBeforeMatch))
          return StmtError();
      }
    } else {
      LHS = Expr;
      MissingCase = false;
    }

    // GNU case range extension.
    SourceLocation DotDotDotLoc;
    ExprResult RHS;
    if (TryConsumeToken(tok::ellipsis, DotDotDotLoc)) {
      Diag(DotDotDotLoc, diag::ext_gnu_case_range);
      RHS = ParseCaseExpression(CaseLoc);
      if (RHS.isInvalid()) {
        if (!SkipUntil(tok::colon, tok::r_brace, StopAtSemi | StopBeforeMatch))
          return StmtError();
      }
    }

    ColonProtection.restore();

    if (TryConsumeToken(tok::colon, ColonLoc)) {
    } else if (TryConsumeToken(tok::semi, ColonLoc) ||
               TryConsumeToken(tok::coloncolon, ColonLoc)) {
      // Treat "case blah;" or "case blah::" as a typo for "case blah:".
      Diag(ColonLoc, diag::err_expected_after)
          << "'case'" << tok::colon
          << FixItHint::CreateReplacement(ColonLoc, ":");
    } else {
      SourceLocation ExpectedLoc = PP.getLocForEndOfToken(PrevTokLocation);
      Diag(ExpectedLoc, diag::err_expected_after)
          << "'case'" << tok::colon
          << FixItHint::CreateInsertion(ExpectedLoc, ":");
      ColonLoc = ExpectedLoc;
    }

    StmtResult Case =
        Actions.ActOnCaseStmt(CaseLoc, LHS, DotDotDotLoc, RHS, ColonLoc);

    // If we had a sema error parsing this case, then just ignore it and
    // continue parsing the sub-stmt.
    if (Case.isInvalid()) {
      if (TopLevelCase.isInvalid())  // No parsed case stmts.
        return ParseStatement(/*TrailingElseLoc=*/nullptr, StmtCtx);
      // Otherwise, just don't add it as a nested case.
    } else {
      // If this is the first case statement we parsed, it becomes TopLevelCase.
      // Otherwise we link it into the current chain.
      Stmt *NextDeepest = Case.get();
      if (TopLevelCase.isInvalid())
        TopLevelCase = Case;
      else
        Actions.ActOnCaseStmtBody(DeepestParsedCaseStmt, Case.get());
      DeepestParsedCaseStmt = NextDeepest;
    }

    // Handle all case statements.
  } while (Tok.is(tok::kw_case));

  // If we found a non-case statement, start by parsing it.
  StmtResult SubStmt;

  if (Tok.is(tok::r_brace)) {
    // "switch (X) { case 4: }", is valid and is treated as if label was
    // followed by a null statement.
    DiagnoseLabelAtEndOfCompoundStatement();
    SubStmt = Actions.ActOnNullStmt(ColonLoc);
  } else {
    SubStmt = ParseStatement(/*TrailingElseLoc=*/nullptr, StmtCtx);
  }

  // Install the body into the most deeply-nested case.
  if (DeepestParsedCaseStmt) {
    // Broken sub-stmt shouldn't prevent forming the case statement properly.
    if (SubStmt.isInvalid())
      SubStmt = Actions.ActOnNullStmt(SourceLocation());
    DiagnoseLabelFollowedByDecl(*this, SubStmt.get());
    Actions.ActOnCaseStmtBody(DeepestParsedCaseStmt, SubStmt.get());
  }

  // Return the top level parsed statement tree.
  return TopLevelCase;
}

/// ParseDefaultStatement
///       labeled-statement:
///         'default' ':' statement
/// Note that this does not parse the 'statement' at the end.
///
StmtResult Parser::ParseDefaultStatement(ParsedStmtContext StmtCtx) {
  assert(Tok.is(tok::kw_default) && "Not a default stmt!");

  // [OpenMP 5.1] 2.1.3: A stand-alone directive may not be used in place of a
  // substatement in a selection statement, in place of the loop body in an
  // iteration statement, or in place of the statement that follows a label.
  StmtCtx &= ~ParsedStmtContext::AllowStandaloneOpenMPDirectives;

  SourceLocation DefaultLoc = ConsumeToken();  // eat the 'default'.

  SourceLocation ColonLoc;
  if (TryConsumeToken(tok::colon, ColonLoc)) {
  } else if (TryConsumeToken(tok::semi, ColonLoc)) {
    // Treat "default;" as a typo for "default:".
    Diag(ColonLoc, diag::err_expected_after)
        << "'default'" << tok::colon
        << FixItHint::CreateReplacement(ColonLoc, ":");
  } else {
    SourceLocation ExpectedLoc = PP.getLocForEndOfToken(PrevTokLocation);
    Diag(ExpectedLoc, diag::err_expected_after)
        << "'default'" << tok::colon
        << FixItHint::CreateInsertion(ExpectedLoc, ":");
    ColonLoc = ExpectedLoc;
  }

  StmtResult SubStmt;

  if (Tok.is(tok::r_brace)) {
    // "switch (X) {... default: }", is valid and is treated as if label was
    // followed by a null statement.
    DiagnoseLabelAtEndOfCompoundStatement();
    SubStmt = Actions.ActOnNullStmt(ColonLoc);
  } else {
    SubStmt = ParseStatement(/*TrailingElseLoc=*/nullptr, StmtCtx);
  }

  // Broken sub-stmt shouldn't prevent forming the case statement properly.
  if (SubStmt.isInvalid())
    SubStmt = Actions.ActOnNullStmt(ColonLoc);

  DiagnoseLabelFollowedByDecl(*this, SubStmt.get());
  return Actions.ActOnDefaultStmt(DefaultLoc, ColonLoc,
                                  SubStmt.get(), getCurScope());
}

StmtResult Parser::ParseCompoundStatement(bool isStmtExpr) {
  return ParseCompoundStatement(isStmtExpr,
                                Scope::DeclScope | Scope::CompoundStmtScope);
}

/// ParseCompoundStatement - Parse a "{}" block.
///
///       compound-statement: [C99 6.8.2]
///         { block-item-list[opt] }
/// [GNU]   { label-declarations block-item-list } [TODO]
///
///       block-item-list:
///         block-item
///         block-item-list block-item
///
///       block-item:
///         declaration
/// [GNU]   '__extension__' declaration
///         statement
///
/// [GNU] label-declarations:
/// [GNU]   label-declaration
/// [GNU]   label-declarations label-declaration
///
/// [GNU] label-declaration:
/// [GNU]   '__label__' identifier-list ';'
///
StmtResult Parser::ParseCompoundStatement(bool isStmtExpr,
                                          unsigned ScopeFlags) {
  assert(Tok.is(tok::l_brace) && "Not a compound stmt!");

  // Enter a scope to hold everything within the compound stmt.  Compound
  // statements can always hold declarations.
  ParseScope CompoundScope(this, ScopeFlags);

  // Parse the statements in the body.
  return ParseCompoundStatementBody(isStmtExpr);
}

/// Parse any pragmas at the start of the compound expression. We handle these
/// separately since some pragmas (FP_CONTRACT) must appear before any C
/// statement in the compound, but may be intermingled with other pragmas.
void Parser::ParseCompoundStatementLeadingPragmas() {
  bool checkForPragmas = true;
  while (checkForPragmas) {
    switch (Tok.getKind()) {
    case tok::annot_pragma_vis:
      HandlePragmaVisibility();
      break;
    case tok::annot_pragma_pack:
      HandlePragmaPack();
      break;
    case tok::annot_pragma_msstruct:
      HandlePragmaMSStruct();
      break;
    case tok::annot_pragma_align:
      HandlePragmaAlign();
      break;
    case tok::annot_pragma_weak:
      HandlePragmaWeak();
      break;
    case tok::annot_pragma_weakalias:
      HandlePragmaWeakAlias();
      break;
    case tok::annot_pragma_redefine_extname:
      HandlePragmaRedefineExtname();
      break;
    case tok::annot_pragma_opencl_extension:
      HandlePragmaOpenCLExtension();
      break;
    case tok::annot_pragma_fp_contract:
      HandlePragmaFPContract();
      break;
    case tok::annot_pragma_fp:
      HandlePragmaFP();
      break;
    case tok::annot_pragma_fenv_access:
    case tok::annot_pragma_fenv_access_ms:
      HandlePragmaFEnvAccess();
      break;
    case tok::annot_pragma_fenv_round:
      HandlePragmaFEnvRound();
      break;
    case tok::annot_pragma_cx_limited_range:
      HandlePragmaCXLimitedRange();
      break;
    case tok::annot_pragma_float_control:
      HandlePragmaFloatControl();
      break;
    case tok::annot_pragma_ms_pointers_to_members:
      HandlePragmaMSPointersToMembers();
      break;
    case tok::annot_pragma_ms_pragma:
      HandlePragmaMSPragma();
      break;
    case tok::annot_pragma_ms_vtordisp:
      HandlePragmaMSVtorDisp();
      break;
    case tok::annot_pragma_dump:
      HandlePragmaDump();
      break;
    default:
      checkForPragmas = false;
      break;
    }
  }

}

void Parser::DiagnoseLabelAtEndOfCompoundStatement() {
  if (getLangOpts().CPlusPlus) {
    Diag(Tok, getLangOpts().CPlusPlus23
                  ? diag::warn_cxx20_compat_label_end_of_compound_statement
                  : diag::ext_cxx_label_end_of_compound_statement);
  } else {
    Diag(Tok, getLangOpts().C23
                  ? diag::warn_c23_compat_label_end_of_compound_statement
                  : diag::ext_c_label_end_of_compound_statement);
  }
}

/// Consume any extra semi-colons resulting in null statements,
/// returning true if any tok::semi were consumed.
bool Parser::ConsumeNullStmt(StmtVector &Stmts) {
  if (!Tok.is(tok::semi))
    return false;

  SourceLocation StartLoc = Tok.getLocation();
  SourceLocation EndLoc;

  while (Tok.is(tok::semi) && !Tok.hasLeadingEmptyMacro() &&
         Tok.getLocation().isValid() && !Tok.getLocation().isMacroID()) {
    EndLoc = Tok.getLocation();

    // Don't just ConsumeToken() this tok::semi, do store it in AST.
    StmtResult R =
        ParseStatementOrDeclaration(Stmts, ParsedStmtContext::SubStmt);
    if (R.isUsable())
      Stmts.push_back(R.get());
  }

  // Did not consume any extra semi.
  if (EndLoc.isInvalid())
    return false;

  Diag(StartLoc, diag::warn_null_statement)
      << FixItHint::CreateRemoval(SourceRange(StartLoc, EndLoc));
  return true;
}

StmtResult Parser::handleExprStmt(ExprResult E, ParsedStmtContext StmtCtx) {
  bool IsStmtExprResult = false;
  if ((StmtCtx & ParsedStmtContext::InStmtExpr) != ParsedStmtContext()) {
    // For GCC compatibility we skip past NullStmts.
    unsigned LookAhead = 0;
    while (GetLookAheadToken(LookAhead).is(tok::semi)) {
      ++LookAhead;
    }
    // Then look to see if the next two tokens close the statement expression;
    // if so, this expression statement is the last statement in a statement
    // expression.
    IsStmtExprResult = GetLookAheadToken(LookAhead).is(tok::r_brace) &&
                       GetLookAheadToken(LookAhead + 1).is(tok::r_paren);
  }

  if (IsStmtExprResult)
    E = Actions.ActOnStmtExprResult(E);
  return Actions.ActOnExprStmt(E, /*DiscardedValue=*/!IsStmtExprResult);
}

/// ParseCompoundStatementBody - Parse a sequence of statements optionally
/// followed by a label and invoke the ActOnCompoundStmt action.  This expects
/// the '{' to be the current token, and consume the '}' at the end of the
/// block.  It does not manipulate the scope stack.
StmtResult Parser::ParseCompoundStatementBody(bool isStmtExpr) {
  PrettyStackTraceLoc CrashInfo(PP.getSourceManager(),
                                Tok.getLocation(),
                                "in compound statement ('{}')");

  // Record the current FPFeatures, restore on leaving the
  // compound statement.
  Sema::FPFeaturesStateRAII SaveFPFeatures(Actions);

  InMessageExpressionRAIIObject InMessage(*this, false);
  BalancedDelimiterTracker T(*this, tok::l_brace);
  if (T.consumeOpen())
    return StmtError();

  Sema::CompoundScopeRAII CompoundScope(Actions, isStmtExpr);

  // Parse any pragmas at the beginning of the compound statement.
  ParseCompoundStatementLeadingPragmas();
  Actions.ActOnAfterCompoundStatementLeadingPragmas();

  StmtVector Stmts;

  // "__label__ X, Y, Z;" is the GNU "Local Label" extension.  These are
  // only allowed at the start of a compound stmt regardless of the language.
  while (Tok.is(tok::kw___label__)) {
    SourceLocation LabelLoc = ConsumeToken();

    SmallVector<Decl *, 8> DeclsInGroup;
    while (true) {
      if (Tok.isNot(tok::identifier)) {
        Diag(Tok, diag::err_expected) << tok::identifier;
        break;
      }

      IdentifierInfo *II = Tok.getIdentifierInfo();
      SourceLocation IdLoc = ConsumeToken();
      DeclsInGroup.push_back(Actions.LookupOrCreateLabel(II, IdLoc, LabelLoc));

      if (!TryConsumeToken(tok::comma))
        break;
    }

    DeclSpec DS(AttrFactory);
    DeclGroupPtrTy Res =
        Actions.FinalizeDeclaratorGroup(getCurScope(), DS, DeclsInGroup);
    StmtResult R = Actions.ActOnDeclStmt(Res, LabelLoc, Tok.getLocation());

    ExpectAndConsumeSemi(diag::err_expected_semi_declaration);
    if (R.isUsable())
      Stmts.push_back(R.get());
  }

  ParsedStmtContext SubStmtCtx =
      ParsedStmtContext::Compound |
      (isStmtExpr ? ParsedStmtContext::InStmtExpr : ParsedStmtContext());

  while (!tryParseMisplacedModuleImport() && Tok.isNot(tok::r_brace) &&
         Tok.isNot(tok::eof)) {
    if (Tok.is(tok::annot_pragma_unused)) {
      HandlePragmaUnused();
      continue;
    }

    if (ConsumeNullStmt(Stmts))
      continue;

    StmtResult R;
    if (Tok.isNot(tok::kw___extension__)) {
      R = ParseStatementOrDeclaration(Stmts, SubStmtCtx);
    } else {
      // __extension__ can start declarations and it can also be a unary
      // operator for expressions.  Consume multiple __extension__ markers here
      // until we can determine which is which.
      // FIXME: This loses extension expressions in the AST!
      SourceLocation ExtLoc = ConsumeToken();
      while (Tok.is(tok::kw___extension__))
        ConsumeToken();

      ParsedAttributes attrs(AttrFactory);
      MaybeParseCXX11Attributes(attrs, /*MightBeObjCMessageSend*/ true);

      // If this is the start of a declaration, parse it as such.
      if (isDeclarationStatement()) {
        // __extension__ silences extension warnings in the subdeclaration.
        // FIXME: Save the __extension__ on the decl as a node somehow?
        ExtensionRAIIObject O(Diags);

        SourceLocation DeclStart = Tok.getLocation(), DeclEnd;
        ParsedAttributes DeclSpecAttrs(AttrFactory);
        DeclGroupPtrTy Res = ParseDeclaration(DeclaratorContext::Block, DeclEnd,
                                              attrs, DeclSpecAttrs);
        R = Actions.ActOnDeclStmt(Res, DeclStart, DeclEnd);
      } else {
        // Otherwise this was a unary __extension__ marker.
        ExprResult Res(ParseExpressionWithLeadingExtension(ExtLoc));

        if (Res.isInvalid()) {
          SkipUntil(tok::semi);
          continue;
        }

        // Eat the semicolon at the end of stmt and convert the expr into a
        // statement.
        ExpectAndConsumeSemi(diag::err_expected_semi_after_expr);
        R = handleExprStmt(Res, SubStmtCtx);
        if (R.isUsable())
          R = Actions.ActOnAttributedStmt(attrs, R.get());
      }
    }

    if (R.isUsable())
      Stmts.push_back(R.get());
  }
  // Warn the user that using option `-ffp-eval-method=source` on a
  // 32-bit target and feature `sse` disabled, or using
  // `pragma clang fp eval_method=source` and feature `sse` disabled, is not
  // supported.
  if (!PP.getTargetInfo().supportSourceEvalMethod() &&
      (PP.getLastFPEvalPragmaLocation().isValid() ||
       PP.getCurrentFPEvalMethod() ==
           LangOptions::FPEvalMethodKind::FEM_Source))
    Diag(Tok.getLocation(),
         diag::warn_no_support_for_eval_method_source_on_m32);

  SourceLocation CloseLoc = Tok.getLocation();

  // We broke out of the while loop because we found a '}' or EOF.
  if (!T.consumeClose()) {
    // If this is the '})' of a statement expression, check that it's written
    // in a sensible way.
    if (isStmtExpr && Tok.is(tok::r_paren))
      checkCompoundToken(CloseLoc, tok::r_brace, CompoundToken::StmtExprEnd);
  } else {
    // Recover by creating a compound statement with what we parsed so far,
    // instead of dropping everything and returning StmtError().
  }

  if (T.getCloseLocation().isValid())
    CloseLoc = T.getCloseLocation();

  return Actions.ActOnCompoundStmt(T.getOpenLocation(), CloseLoc,
                                   Stmts, isStmtExpr);
}

/// ParseParenExprOrCondition:
/// [C  ]     '(' expression ')'
/// [C++]     '(' condition ')'
/// [C++1z]   '(' init-statement[opt] condition ')'
///
/// This function parses and performs error recovery on the specified condition
/// or expression (depending on whether we're in C++ or C mode).  This function
/// goes out of its way to recover well.  It returns true if there was a parser
/// error (the right paren couldn't be found), which indicates that the caller
/// should try to recover harder.  It returns false if the condition is
/// successfully parsed.  Note that a successful parse can still have semantic
/// errors in the condition.
/// Additionally, it will assign the location of the outer-most '(' and ')',
/// to LParenLoc and RParenLoc, respectively.
bool Parser::ParseParenExprOrCondition(StmtResult *InitStmt,
                                       Sema::ConditionResult &Cond,
                                       SourceLocation Loc,
                                       Sema::ConditionKind CK,
                                       SourceLocation &LParenLoc,
                                       SourceLocation &RParenLoc) {
  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();
  SourceLocation Start = Tok.getLocation();

  if (getLangOpts().CPlusPlus) {
    Cond = ParseCXXCondition(InitStmt, Loc, CK, false);
  } else {
    ExprResult CondExpr = ParseExpression();

    // If required, convert to a boolean value.
    if (CondExpr.isInvalid())
      Cond = Sema::ConditionError();
    else
      Cond = Actions.ActOnCondition(getCurScope(), Loc, CondExpr.get(), CK,
                                    /*MissingOK=*/false);
  }

  // If the parser was confused by the condition and we don't have a ')', try to
  // recover by skipping ahead to a semi and bailing out.  If condexp is
  // semantically invalid but we have well formed code, keep going.
  if (Cond.isInvalid() && Tok.isNot(tok::r_paren)) {
    SkipUntil(tok::semi);
    // Skipping may have stopped if it found the containing ')'.  If so, we can
    // continue parsing the if statement.
    if (Tok.isNot(tok::r_paren))
      return true;
  }

  if (Cond.isInvalid()) {
    ExprResult CondExpr = Actions.CreateRecoveryExpr(
        Start, Tok.getLocation() == Start ? Start : PrevTokLocation, {},
        Actions.PreferredConditionType(CK));
    if (!CondExpr.isInvalid())
      Cond = Actions.ActOnCondition(getCurScope(), Loc, CondExpr.get(), CK,
                                    /*MissingOK=*/false);
  }

  // Either the condition is valid or the rparen is present.
  T.consumeClose();
  LParenLoc = T.getOpenLocation();
  RParenLoc = T.getCloseLocation();

  // Check for extraneous ')'s to catch things like "if (foo())) {".  We know
  // that all callers are looking for a statement after the condition, so ")"
  // isn't valid.
  while (Tok.is(tok::r_paren)) {
    Diag(Tok, diag::err_extraneous_rparen_in_condition)
      << FixItHint::CreateRemoval(Tok.getLocation());
    ConsumeParen();
  }

  return false;
}

namespace {

enum MisleadingStatementKind { MSK_if, MSK_else, MSK_for, MSK_while };

struct MisleadingIndentationChecker {
  Parser &P;
  SourceLocation StmtLoc;
  SourceLocation PrevLoc;
  unsigned NumDirectives;
  MisleadingStatementKind Kind;
  bool ShouldSkip;
  MisleadingIndentationChecker(Parser &P, MisleadingStatementKind K,
                               SourceLocation SL)
      : P(P), StmtLoc(SL), PrevLoc(P.getCurToken().getLocation()),
        NumDirectives(P.getPreprocessor().getNumDirectives()), Kind(K),
        ShouldSkip(P.getCurToken().is(tok::l_brace)) {
    if (!P.MisleadingIndentationElseLoc.isInvalid()) {
      StmtLoc = P.MisleadingIndentationElseLoc;
      P.MisleadingIndentationElseLoc = SourceLocation();
    }
    if (Kind == MSK_else && !ShouldSkip)
      P.MisleadingIndentationElseLoc = SL;
  }

  /// Compute the column number will aligning tabs on TabStop (-ftabstop), this
  /// gives the visual indentation of the SourceLocation.
  static unsigned getVisualIndentation(SourceManager &SM, SourceLocation Loc) {
    unsigned TabStop = SM.getDiagnostics().getDiagnosticOptions().TabStop;

    unsigned ColNo = SM.getSpellingColumnNumber(Loc);
    if (ColNo == 0 || TabStop == 1)
      return ColNo;

    std::pair<FileID, unsigned> FIDAndOffset = SM.getDecomposedLoc(Loc);

    bool Invalid;
    StringRef BufData = SM.getBufferData(FIDAndOffset.first, &Invalid);
    if (Invalid)
      return 0;

    const char *EndPos = BufData.data() + FIDAndOffset.second;
    // FileOffset are 0-based and Column numbers are 1-based
    assert(FIDAndOffset.second + 1 >= ColNo &&
           "Column number smaller than file offset?");

    unsigned VisualColumn = 0; // Stored as 0-based column, here.
    // Loop from beginning of line up to Loc's file position, counting columns,
    // expanding tabs.
    for (const char *CurPos = EndPos - (ColNo - 1); CurPos != EndPos;
         ++CurPos) {
      if (*CurPos == '\t')
        // Advance visual column to next tabstop.
        VisualColumn += (TabStop - VisualColumn % TabStop);
      else
        VisualColumn++;
    }
    return VisualColumn + 1;
  }

  void Check() {
    Token Tok = P.getCurToken();
    if (P.getActions().getDiagnostics().isIgnored(
            diag::warn_misleading_indentation, Tok.getLocation()) ||
        ShouldSkip || NumDirectives != P.getPreprocessor().getNumDirectives() ||
        Tok.isOneOf(tok::semi, tok::r_brace) || Tok.isAnnotation() ||
        Tok.getLocation().isMacroID() || PrevLoc.isMacroID() ||
        StmtLoc.isMacroID() ||
        (Kind == MSK_else && P.MisleadingIndentationElseLoc.isInvalid())) {
      P.MisleadingIndentationElseLoc = SourceLocation();
      return;
    }
    if (Kind == MSK_else)
      P.MisleadingIndentationElseLoc = SourceLocation();

    SourceManager &SM = P.getPreprocessor().getSourceManager();
    unsigned PrevColNum = getVisualIndentation(SM, PrevLoc);
    unsigned CurColNum = getVisualIndentation(SM, Tok.getLocation());
    unsigned StmtColNum = getVisualIndentation(SM, StmtLoc);

    if (PrevColNum != 0 && CurColNum != 0 && StmtColNum != 0 &&
        ((PrevColNum > StmtColNum && PrevColNum == CurColNum) ||
         !Tok.isAtStartOfLine()) &&
        SM.getPresumedLineNumber(StmtLoc) !=
            SM.getPresumedLineNumber(Tok.getLocation()) &&
        (Tok.isNot(tok::identifier) ||
         P.getPreprocessor().LookAhead(0).isNot(tok::colon))) {
      P.Diag(Tok.getLocation(), diag::warn_misleading_indentation) << Kind;
      P.Diag(StmtLoc, diag::note_previous_statement);
    }
  }
};

}

/// ParseIfStatement
///       if-statement: [C99 6.8.4.1]
///         'if' '(' expression ')' statement
///         'if' '(' expression ')' statement 'else' statement
/// [C++]   'if' '(' condition ')' statement
/// [C++]   'if' '(' condition ')' statement 'else' statement
/// [C++23] 'if' '!' [opt] consteval compound-statement
/// [C++23] 'if' '!' [opt] consteval compound-statement 'else' statement
///
StmtResult Parser::ParseIfStatement(SourceLocation *TrailingElseLoc) {
  assert(Tok.is(tok::kw_if) && "Not an if stmt!");
  SourceLocation IfLoc = ConsumeToken();  // eat the 'if'.

  bool IsConstexpr = false;
  bool IsConsteval = false;
  SourceLocation NotLocation;
  SourceLocation ConstevalLoc;

  if (Tok.is(tok::kw_constexpr)) {
    // C23 supports constexpr keyword, but only for object definitions.
    if (getLangOpts().CPlusPlus) {
      Diag(Tok, getLangOpts().CPlusPlus17 ? diag::warn_cxx14_compat_constexpr_if
                                          : diag::ext_constexpr_if);
      IsConstexpr = true;
      ConsumeToken();
    }
  } else {
    if (Tok.is(tok::exclaim)) {
      NotLocation = ConsumeToken();
    }

    if (Tok.is(tok::kw_consteval)) {
      Diag(Tok, getLangOpts().CPlusPlus23 ? diag::warn_cxx20_compat_consteval_if
                                          : diag::ext_consteval_if);
      IsConsteval = true;
      ConstevalLoc = ConsumeToken();
    }
  }
  if (!IsConsteval && (NotLocation.isValid() || Tok.isNot(tok::l_paren))) {
    Diag(Tok, diag::err_expected_lparen_after) << "if";
    SkipUntil(tok::semi);
    return StmtError();
  }

  bool C99orCXX = getLangOpts().C99 || getLangOpts().CPlusPlus;

  // C99 6.8.4p3 - In C99, the if statement is a block.  This is not
  // the case for C90.
  //
  // C++ 6.4p3:
  // A name introduced by a declaration in a condition is in scope from its
  // point of declaration until the end of the substatements controlled by the
  // condition.
  // C++ 3.3.2p4:
  // Names declared in the for-init-statement, and in the condition of if,
  // while, for, and switch statements are local to the if, while, for, or
  // switch statement (including the controlled statement).
  //
  ParseScope IfScope(this, Scope::DeclScope | Scope::ControlScope, C99orCXX);

  // Parse the condition.
  StmtResult InitStmt;
  Sema::ConditionResult Cond;
  SourceLocation LParen;
  SourceLocation RParen;
  std::optional<bool> ConstexprCondition;
  if (!IsConsteval) {

    if (ParseParenExprOrCondition(&InitStmt, Cond, IfLoc,
                                  IsConstexpr ? Sema::ConditionKind::ConstexprIf
                                              : Sema::ConditionKind::Boolean,
                                  LParen, RParen))
      return StmtError();

    if (IsConstexpr)
      ConstexprCondition = Cond.getKnownValue();
  }

  bool IsBracedThen = Tok.is(tok::l_brace);

  // C99 6.8.4p3 - In C99, the body of the if statement is a scope, even if
  // there is no compound stmt.  C90 does not have this clause.  We only do this
  // if the body isn't a compound statement to avoid push/pop in common cases.
  //
  // C++ 6.4p1:
  // The substatement in a selection-statement (each substatement, in the else
  // form of the if statement) implicitly defines a local scope.
  //
  // For C++ we create a scope for the condition and a new scope for
  // substatements because:
  // -When the 'then' scope exits, we want the condition declaration to still be
  //    active for the 'else' scope too.
  // -Sema will detect name clashes by considering declarations of a
  //    'ControlScope' as part of its direct subscope.
  // -If we wanted the condition and substatement to be in the same scope, we
  //    would have to notify ParseStatement not to create a new scope. It's
  //    simpler to let it create a new scope.
  //
  ParseScope InnerScope(this, Scope::DeclScope, C99orCXX, IsBracedThen);

  MisleadingIndentationChecker MIChecker(*this, MSK_if, IfLoc);

  // Read the 'then' stmt.
  SourceLocation ThenStmtLoc = Tok.getLocation();

  SourceLocation InnerStatementTrailingElseLoc;
  StmtResult ThenStmt;
  {
    bool ShouldEnter = ConstexprCondition && !*ConstexprCondition;
    Sema::ExpressionEvaluationContext Context =
        Sema::ExpressionEvaluationContext::DiscardedStatement;
    if (NotLocation.isInvalid() && IsConsteval) {
      Context = Sema::ExpressionEvaluationContext::ImmediateFunctionContext;
      ShouldEnter = true;
    }

    EnterExpressionEvaluationContext PotentiallyDiscarded(
        Actions, Context, nullptr,
        Sema::ExpressionEvaluationContextRecord::EK_Other, ShouldEnter);
    ThenStmt = ParseStatement(&InnerStatementTrailingElseLoc);
  }

  if (Tok.isNot(tok::kw_else))
    MIChecker.Check();

  // Pop the 'if' scope if needed.
  InnerScope.Exit();

  // If it has an else, parse it.
  SourceLocation ElseLoc;
  SourceLocation ElseStmtLoc;
  StmtResult ElseStmt;

  if (Tok.is(tok::kw_else)) {
    if (TrailingElseLoc)
      *TrailingElseLoc = Tok.getLocation();

    ElseLoc = ConsumeToken();
    ElseStmtLoc = Tok.getLocation();

    // C99 6.8.4p3 - In C99, the body of the if statement is a scope, even if
    // there is no compound stmt.  C90 does not have this clause.  We only do
    // this if the body isn't a compound statement to avoid push/pop in common
    // cases.
    //
    // C++ 6.4p1:
    // The substatement in a selection-statement (each substatement, in the else
    // form of the if statement) implicitly defines a local scope.
    //
    ParseScope InnerScope(this, Scope::DeclScope, C99orCXX,
                          Tok.is(tok::l_brace));

    MisleadingIndentationChecker MIChecker(*this, MSK_else, ElseLoc);
    bool ShouldEnter = ConstexprCondition && *ConstexprCondition;
    Sema::ExpressionEvaluationContext Context =
        Sema::ExpressionEvaluationContext::DiscardedStatement;
    if (NotLocation.isValid() && IsConsteval) {
      Context = Sema::ExpressionEvaluationContext::ImmediateFunctionContext;
      ShouldEnter = true;
    }

    EnterExpressionEvaluationContext PotentiallyDiscarded(
        Actions, Context, nullptr,
        Sema::ExpressionEvaluationContextRecord::EK_Other, ShouldEnter);
    ElseStmt = ParseStatement();

    if (ElseStmt.isUsable())
      MIChecker.Check();

    // Pop the 'else' scope if needed.
    InnerScope.Exit();
  } else if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteAfterIf(getCurScope(), IsBracedThen);
    return StmtError();
  } else if (InnerStatementTrailingElseLoc.isValid()) {
    Diag(InnerStatementTrailingElseLoc, diag::warn_dangling_else);
  }

  IfScope.Exit();

  // If the then or else stmt is invalid and the other is valid (and present),
  // turn the invalid one into a null stmt to avoid dropping the other
  // part.  If both are invalid, return error.
  if ((ThenStmt.isInvalid() && ElseStmt.isInvalid()) ||
      (ThenStmt.isInvalid() && ElseStmt.get() == nullptr) ||
      (ThenStmt.get() == nullptr && ElseStmt.isInvalid())) {
    // Both invalid, or one is invalid and other is non-present: return error.
    return StmtError();
  }

  if (IsConsteval) {
    auto IsCompoundStatement = [](const Stmt *S) {
      if (const auto *Outer = dyn_cast_if_present<AttributedStmt>(S))
        S = Outer->getSubStmt();
      return isa_and_nonnull<clang::CompoundStmt>(S);
    };

    if (!IsCompoundStatement(ThenStmt.get())) {
      Diag(ConstevalLoc, diag::err_expected_after) << "consteval"
                                                   << "{";
      return StmtError();
    }
    if (!ElseStmt.isUnset() && !IsCompoundStatement(ElseStmt.get())) {
      Diag(ElseLoc, diag::err_expected_after) << "else"
                                              << "{";
      return StmtError();
    }
  }

  // Now if either are invalid, replace with a ';'.
  if (ThenStmt.isInvalid())
    ThenStmt = Actions.ActOnNullStmt(ThenStmtLoc);
  if (ElseStmt.isInvalid())
    ElseStmt = Actions.ActOnNullStmt(ElseStmtLoc);

  IfStatementKind Kind = IfStatementKind::Ordinary;
  if (IsConstexpr)
    Kind = IfStatementKind::Constexpr;
  else if (IsConsteval)
    Kind = NotLocation.isValid() ? IfStatementKind::ConstevalNegated
                                 : IfStatementKind::ConstevalNonNegated;

  return Actions.ActOnIfStmt(IfLoc, Kind, LParen, InitStmt.get(), Cond, RParen,
                             ThenStmt.get(), ElseLoc, ElseStmt.get());
}

/// ParseSwitchStatement
///       switch-statement:
///         'switch' '(' expression ')' statement
/// [C++]   'switch' '(' condition ')' statement
StmtResult Parser::ParseSwitchStatement(SourceLocation *TrailingElseLoc) {
  assert(Tok.is(tok::kw_switch) && "Not a switch stmt!");
  SourceLocation SwitchLoc = ConsumeToken();  // eat the 'switch'.

  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen_after) << "switch";
    SkipUntil(tok::semi);
    return StmtError();
  }

  bool C99orCXX = getLangOpts().C99 || getLangOpts().CPlusPlus;

  // C99 6.8.4p3 - In C99, the switch statement is a block.  This is
  // not the case for C90.  Start the switch scope.
  //
  // C++ 6.4p3:
  // A name introduced by a declaration in a condition is in scope from its
  // point of declaration until the end of the substatements controlled by the
  // condition.
  // C++ 3.3.2p4:
  // Names declared in the for-init-statement, and in the condition of if,
  // while, for, and switch statements are local to the if, while, for, or
  // switch statement (including the controlled statement).
  //
  unsigned ScopeFlags = Scope::SwitchScope;
  if (C99orCXX)
    ScopeFlags |= Scope::DeclScope | Scope::ControlScope;
  ParseScope SwitchScope(this, ScopeFlags);

  // Parse the condition.
  StmtResult InitStmt;
  Sema::ConditionResult Cond;
  SourceLocation LParen;
  SourceLocation RParen;
  if (ParseParenExprOrCondition(&InitStmt, Cond, SwitchLoc,
                                Sema::ConditionKind::Switch, LParen, RParen))
    return StmtError();

  StmtResult Switch = Actions.ActOnStartOfSwitchStmt(
      SwitchLoc, LParen, InitStmt.get(), Cond, RParen);

  if (Switch.isInvalid()) {
    // Skip the switch body.
    // FIXME: This is not optimal recovery, but parsing the body is more
    // dangerous due to the presence of case and default statements, which
    // will have no place to connect back with the switch.
    if (Tok.is(tok::l_brace)) {
      ConsumeBrace();
      SkipUntil(tok::r_brace);
    } else
      SkipUntil(tok::semi);
    return Switch;
  }

  // C99 6.8.4p3 - In C99, the body of the switch statement is a scope, even if
  // there is no compound stmt.  C90 does not have this clause.  We only do this
  // if the body isn't a compound statement to avoid push/pop in common cases.
  //
  // C++ 6.4p1:
  // The substatement in a selection-statement (each substatement, in the else
  // form of the if statement) implicitly defines a local scope.
  //
  // See comments in ParseIfStatement for why we create a scope for the
  // condition and a new scope for substatement in C++.
  //
  getCurScope()->AddFlags(Scope::BreakScope);
  ParseScope InnerScope(this, Scope::DeclScope, C99orCXX, Tok.is(tok::l_brace));

  // We have incremented the mangling number for the SwitchScope and the
  // InnerScope, which is one too many.
  if (C99orCXX)
    getCurScope()->decrementMSManglingNumber();

  // Read the body statement.
  StmtResult Body(ParseStatement(TrailingElseLoc));

  // Pop the scopes.
  InnerScope.Exit();
  SwitchScope.Exit();

  return Actions.ActOnFinishSwitchStmt(SwitchLoc, Switch.get(), Body.get());
}

/// ParseWhileStatement
///       while-statement: [C99 6.8.5.1]
///         'while' '(' expression ')' statement
/// [C++]   'while' '(' condition ')' statement
StmtResult Parser::ParseWhileStatement(SourceLocation *TrailingElseLoc) {
  assert(Tok.is(tok::kw_while) && "Not a while stmt!");
  SourceLocation WhileLoc = Tok.getLocation();
  ConsumeToken();  // eat the 'while'.

  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen_after) << "while";
    SkipUntil(tok::semi);
    return StmtError();
  }

  bool C99orCXX = getLangOpts().C99 || getLangOpts().CPlusPlus;

  // C99 6.8.5p5 - In C99, the while statement is a block.  This is not
  // the case for C90.  Start the loop scope.
  //
  // C++ 6.4p3:
  // A name introduced by a declaration in a condition is in scope from its
  // point of declaration until the end of the substatements controlled by the
  // condition.
  // C++ 3.3.2p4:
  // Names declared in the for-init-statement, and in the condition of if,
  // while, for, and switch statements are local to the if, while, for, or
  // switch statement (including the controlled statement).
  //
  unsigned ScopeFlags;
  if (C99orCXX)
    ScopeFlags = Scope::BreakScope | Scope::ContinueScope |
                 Scope::DeclScope  | Scope::ControlScope;
  else
    ScopeFlags = Scope::BreakScope | Scope::ContinueScope;
  ParseScope WhileScope(this, ScopeFlags);

  // Parse the condition.
  Sema::ConditionResult Cond;
  SourceLocation LParen;
  SourceLocation RParen;
  if (ParseParenExprOrCondition(nullptr, Cond, WhileLoc,
                                Sema::ConditionKind::Boolean, LParen, RParen))
    return StmtError();

  // C99 6.8.5p5 - In C99, the body of the while statement is a scope, even if
  // there is no compound stmt.  C90 does not have this clause.  We only do this
  // if the body isn't a compound statement to avoid push/pop in common cases.
  //
  // C++ 6.5p2:
  // The substatement in an iteration-statement implicitly defines a local scope
  // which is entered and exited each time through the loop.
  //
  // See comments in ParseIfStatement for why we create a scope for the
  // condition and a new scope for substatement in C++.
  //
  ParseScope InnerScope(this, Scope::DeclScope, C99orCXX, Tok.is(tok::l_brace));

  MisleadingIndentationChecker MIChecker(*this, MSK_while, WhileLoc);

  // Read the body statement.
  StmtResult Body(ParseStatement(TrailingElseLoc));

  if (Body.isUsable())
    MIChecker.Check();
  // Pop the body scope if needed.
  InnerScope.Exit();
  WhileScope.Exit();

  if (Cond.isInvalid() || Body.isInvalid())
    return StmtError();

  return Actions.ActOnWhileStmt(WhileLoc, LParen, Cond, RParen, Body.get());
}

/// ParseDoStatement
///       do-statement: [C99 6.8.5.2]
///         'do' statement 'while' '(' expression ')' ';'
/// Note: this lets the caller parse the end ';'.
StmtResult Parser::ParseDoStatement() {
  assert(Tok.is(tok::kw_do) && "Not a do stmt!");
  SourceLocation DoLoc = ConsumeToken();  // eat the 'do'.

  // C99 6.8.5p5 - In C99, the do statement is a block.  This is not
  // the case for C90.  Start the loop scope.
  unsigned ScopeFlags;
  if (getLangOpts().C99)
    ScopeFlags = Scope::BreakScope | Scope::ContinueScope | Scope::DeclScope;
  else
    ScopeFlags = Scope::BreakScope | Scope::ContinueScope;

  ParseScope DoScope(this, ScopeFlags);

  // C99 6.8.5p5 - In C99, the body of the do statement is a scope, even if
  // there is no compound stmt.  C90 does not have this clause. We only do this
  // if the body isn't a compound statement to avoid push/pop in common cases.
  //
  // C++ 6.5p2:
  // The substatement in an iteration-statement implicitly defines a local scope
  // which is entered and exited each time through the loop.
  //
  bool C99orCXX = getLangOpts().C99 || getLangOpts().CPlusPlus;
  ParseScope InnerScope(this, Scope::DeclScope, C99orCXX, Tok.is(tok::l_brace));

  // Read the body statement.
  StmtResult Body(ParseStatement());

  // Pop the body scope if needed.
  InnerScope.Exit();

  if (Tok.isNot(tok::kw_while)) {
    if (!Body.isInvalid()) {
      Diag(Tok, diag::err_expected_while);
      Diag(DoLoc, diag::note_matching) << "'do'";
      SkipUntil(tok::semi, StopBeforeMatch);
    }
    return StmtError();
  }
  SourceLocation WhileLoc = ConsumeToken();

  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen_after) << "do/while";
    SkipUntil(tok::semi, StopBeforeMatch);
    return StmtError();
  }

  // Parse the parenthesized expression.
  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();

  // A do-while expression is not a condition, so can't have attributes.
  DiagnoseAndSkipCXX11Attributes();

  SourceLocation Start = Tok.getLocation();
  ExprResult Cond = ParseExpression();
  // Correct the typos in condition before closing the scope.
  if (Cond.isUsable())
    Cond = Actions.CorrectDelayedTyposInExpr(Cond, /*InitDecl=*/nullptr,
                                             /*RecoverUncorrectedTypos=*/true);
  else {
    if (!Tok.isOneOf(tok::r_paren, tok::r_square, tok::r_brace))
      SkipUntil(tok::semi);
    Cond = Actions.CreateRecoveryExpr(
        Start, Start == Tok.getLocation() ? Start : PrevTokLocation, {},
        Actions.getASTContext().BoolTy);
  }
  T.consumeClose();
  DoScope.Exit();

  if (Cond.isInvalid() || Body.isInvalid())
    return StmtError();

  return Actions.ActOnDoStmt(DoLoc, Body.get(), WhileLoc, T.getOpenLocation(),
                             Cond.get(), T.getCloseLocation());
}

bool Parser::isForRangeIdentifier() {
  assert(Tok.is(tok::identifier));

  const Token &Next = NextToken();
  if (Next.is(tok::colon))
    return true;

  if (Next.isOneOf(tok::l_square, tok::kw_alignas)) {
    TentativeParsingAction PA(*this);
    ConsumeToken();
    SkipCXX11Attributes();
    bool Result = Tok.is(tok::colon);
    PA.Revert();
    return Result;
  }

  return false;
}

/// ParseForStatement
///       for-statement: [C99 6.8.5.3]
///         'for' '(' expr[opt] ';' expr[opt] ';' expr[opt] ')' statement
///         'for' '(' declaration expr[opt] ';' expr[opt] ')' statement
/// [C++]   'for' '(' for-init-statement condition[opt] ';' expression[opt] ')'
/// [C++]       statement
/// [C++0x] 'for'
///             'co_await'[opt]    [Coroutines]
///             '(' for-range-declaration ':' for-range-initializer ')'
///             statement
/// [OBJC2] 'for' '(' declaration 'in' expr ')' statement
/// [OBJC2] 'for' '(' expr 'in' expr ')' statement
///
/// [C++] for-init-statement:
/// [C++]   expression-statement
/// [C++]   simple-declaration
/// [C++23] alias-declaration
///
/// [C++0x] for-range-declaration:
/// [C++0x]   attribute-specifier-seq[opt] type-specifier-seq declarator
/// [C++0x] for-range-initializer:
/// [C++0x]   expression
/// [C++0x]   braced-init-list            [TODO]
StmtResult Parser::ParseForStatement(SourceLocation *TrailingElseLoc) {
  assert(Tok.is(tok::kw_for) && "Not a for stmt!");
  SourceLocation ForLoc = ConsumeToken();  // eat the 'for'.

  SourceLocation CoawaitLoc;
  if (Tok.is(tok::kw_co_await))
    CoawaitLoc = ConsumeToken();

  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen_after) << "for";
    SkipUntil(tok::semi);
    return StmtError();
  }

  bool C99orCXXorObjC = getLangOpts().C99 || getLangOpts().CPlusPlus ||
    getLangOpts().ObjC;

  // C99 6.8.5p5 - In C99, the for statement is a block.  This is not
  // the case for C90.  Start the loop scope.
  //
  // C++ 6.4p3:
  // A name introduced by a declaration in a condition is in scope from its
  // point of declaration until the end of the substatements controlled by the
  // condition.
  // C++ 3.3.2p4:
  // Names declared in the for-init-statement, and in the condition of if,
  // while, for, and switch statements are local to the if, while, for, or
  // switch statement (including the controlled statement).
  // C++ 6.5.3p1:
  // Names declared in the for-init-statement are in the same declarative-region
  // as those declared in the condition.
  //
  unsigned ScopeFlags = 0;
  if (C99orCXXorObjC)
    ScopeFlags = Scope::DeclScope | Scope::ControlScope;

  ParseScope ForScope(this, ScopeFlags);

  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();

  ExprResult Value;

  bool ForEach = false;
  StmtResult FirstPart;
  Sema::ConditionResult SecondPart;
  ExprResult Collection;
  ForRangeInfo ForRangeInfo;
  FullExprArg ThirdPart(Actions);

  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteOrdinaryName(
        getCurScope(), C99orCXXorObjC ? SemaCodeCompletion::PCC_ForInit
                                      : SemaCodeCompletion::PCC_Expression);
    return StmtError();
  }

  ParsedAttributes attrs(AttrFactory);
  MaybeParseCXX11Attributes(attrs);

  SourceLocation EmptyInitStmtSemiLoc;

  // Parse the first part of the for specifier.
  if (Tok.is(tok::semi)) {  // for (;
    ProhibitAttributes(attrs);
    // no first part, eat the ';'.
    SourceLocation SemiLoc = Tok.getLocation();
    if (!Tok.hasLeadingEmptyMacro() && !SemiLoc.isMacroID())
      EmptyInitStmtSemiLoc = SemiLoc;
    ConsumeToken();
  } else if (getLangOpts().CPlusPlus && Tok.is(tok::identifier) &&
             isForRangeIdentifier()) {
    ProhibitAttributes(attrs);
    IdentifierInfo *Name = Tok.getIdentifierInfo();
    SourceLocation Loc = ConsumeToken();
    MaybeParseCXX11Attributes(attrs);

    ForRangeInfo.ColonLoc = ConsumeToken();
    if (Tok.is(tok::l_brace))
      ForRangeInfo.RangeExpr = ParseBraceInitializer();
    else
      ForRangeInfo.RangeExpr = ParseExpression();

    Diag(Loc, diag::err_for_range_identifier)
      << ((getLangOpts().CPlusPlus11 && !getLangOpts().CPlusPlus17)
              ? FixItHint::CreateInsertion(Loc, "auto &&")
              : FixItHint());

    ForRangeInfo.LoopVar =
        Actions.ActOnCXXForRangeIdentifier(getCurScope(), Loc, Name, attrs);
  } else if (isForInitDeclaration()) {  // for (int X = 4;
    ParenBraceBracketBalancer BalancerRAIIObj(*this);

    // Parse declaration, which eats the ';'.
    if (!C99orCXXorObjC) {   // Use of C99-style for loops in C90 mode?
      Diag(Tok, diag::ext_c99_variable_decl_in_for_loop);
      Diag(Tok, diag::warn_gcc_variable_decl_in_for_loop);
    }
    DeclGroupPtrTy DG;
    SourceLocation DeclStart = Tok.getLocation(), DeclEnd;
    if (Tok.is(tok::kw_using)) {
      DG = ParseAliasDeclarationInInitStatement(DeclaratorContext::ForInit,
                                                attrs);
      FirstPart = Actions.ActOnDeclStmt(DG, DeclStart, Tok.getLocation());
    } else {
      // In C++0x, "for (T NS:a" might not be a typo for ::
      bool MightBeForRangeStmt = getLangOpts().CPlusPlus;
      ColonProtectionRAIIObject ColonProtection(*this, MightBeForRangeStmt);
      ParsedAttributes DeclSpecAttrs(AttrFactory);
      DG = ParseSimpleDeclaration(
          DeclaratorContext::ForInit, DeclEnd, attrs, DeclSpecAttrs, false,
          MightBeForRangeStmt ? &ForRangeInfo : nullptr);
      FirstPart = Actions.ActOnDeclStmt(DG, DeclStart, Tok.getLocation());
      if (ForRangeInfo.ParsedForRangeDecl()) {
        Diag(ForRangeInfo.ColonLoc, getLangOpts().CPlusPlus11
                                        ? diag::warn_cxx98_compat_for_range
                                        : diag::ext_for_range);
        ForRangeInfo.LoopVar = FirstPart;
        FirstPart = StmtResult();
      } else if (Tok.is(tok::semi)) { // for (int x = 4;
        ConsumeToken();
      } else if ((ForEach = isTokIdentifier_in())) {
        Actions.ActOnForEachDeclStmt(DG);
        // ObjC: for (id x in expr)
        ConsumeToken(); // consume 'in'

        if (Tok.is(tok::code_completion)) {
          cutOffParsing();
          Actions.CodeCompletion().CodeCompleteObjCForCollection(getCurScope(),
                                                                 DG);
          return StmtError();
        }
        Collection = ParseExpression();
      } else {
        Diag(Tok, diag::err_expected_semi_for);
      }
    }
  } else {
    ProhibitAttributes(attrs);
    Value = Actions.CorrectDelayedTyposInExpr(ParseExpression());

    ForEach = isTokIdentifier_in();

    // Turn the expression into a stmt.
    if (!Value.isInvalid()) {
      if (ForEach)
        FirstPart = Actions.ActOnForEachLValueExpr(Value.get());
      else {
        // We already know this is not an init-statement within a for loop, so
        // if we are parsing a C++11 range-based for loop, we should treat this
        // expression statement as being a discarded value expression because
        // we will err below. This way we do not warn on an unused expression
        // that was an error in the first place, like with: for (expr : expr);
        bool IsRangeBasedFor =
            getLangOpts().CPlusPlus11 && !ForEach && Tok.is(tok::colon);
        FirstPart = Actions.ActOnExprStmt(Value, !IsRangeBasedFor);
      }
    }

    if (Tok.is(tok::semi)) {
      ConsumeToken();
    } else if (ForEach) {
      ConsumeToken(); // consume 'in'

      if (Tok.is(tok::code_completion)) {
        cutOffParsing();
        Actions.CodeCompletion().CodeCompleteObjCForCollection(getCurScope(),
                                                               nullptr);
        return StmtError();
      }
      Collection = ParseExpression();
    } else if (getLangOpts().CPlusPlus11 && Tok.is(tok::colon) && FirstPart.get()) {
      // User tried to write the reasonable, but ill-formed, for-range-statement
      //   for (expr : expr) { ... }
      Diag(Tok, diag::err_for_range_expected_decl)
        << FirstPart.get()->getSourceRange();
      SkipUntil(tok::r_paren, StopBeforeMatch);
      SecondPart = Sema::ConditionError();
    } else {
      if (!Value.isInvalid()) {
        Diag(Tok, diag::err_expected_semi_for);
      } else {
        // Skip until semicolon or rparen, don't consume it.
        SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch);
        if (Tok.is(tok::semi))
          ConsumeToken();
      }
    }
  }

  // Parse the second part of the for specifier.
  if (!ForEach && !ForRangeInfo.ParsedForRangeDecl() &&
      !SecondPart.isInvalid()) {
    // Parse the second part of the for specifier.
    if (Tok.is(tok::semi)) {  // for (...;;
      // no second part.
    } else if (Tok.is(tok::r_paren)) {
      // missing both semicolons.
    } else {
      if (getLangOpts().CPlusPlus) {
        // C++2a: We've parsed an init-statement; we might have a
        // for-range-declaration next.
        bool MightBeForRangeStmt = !ForRangeInfo.ParsedForRangeDecl();
        ColonProtectionRAIIObject ColonProtection(*this, MightBeForRangeStmt);
        SourceLocation SecondPartStart = Tok.getLocation();
        Sema::ConditionKind CK = Sema::ConditionKind::Boolean;
        SecondPart = ParseCXXCondition(
            /*InitStmt=*/nullptr, ForLoc, CK,
            // FIXME: recovery if we don't see another semi!
            /*MissingOK=*/true, MightBeForRangeStmt ? &ForRangeInfo : nullptr,
            /*EnterForConditionScope=*/true);

        if (ForRangeInfo.ParsedForRangeDecl()) {
          Diag(FirstPart.get() ? FirstPart.get()->getBeginLoc()
                               : ForRangeInfo.ColonLoc,
               getLangOpts().CPlusPlus20
                   ? diag::warn_cxx17_compat_for_range_init_stmt
                   : diag::ext_for_range_init_stmt)
              << (FirstPart.get() ? FirstPart.get()->getSourceRange()
                                  : SourceRange());
          if (EmptyInitStmtSemiLoc.isValid()) {
            Diag(EmptyInitStmtSemiLoc, diag::warn_empty_init_statement)
                << /*for-loop*/ 2
                << FixItHint::CreateRemoval(EmptyInitStmtSemiLoc);
          }
        }

        if (SecondPart.isInvalid()) {
          ExprResult CondExpr = Actions.CreateRecoveryExpr(
              SecondPartStart,
              Tok.getLocation() == SecondPartStart ? SecondPartStart
                                                   : PrevTokLocation,
              {}, Actions.PreferredConditionType(CK));
          if (!CondExpr.isInvalid())
            SecondPart = Actions.ActOnCondition(getCurScope(), ForLoc,
                                                CondExpr.get(), CK,
                                                /*MissingOK=*/false);
        }

      } else {
        // We permit 'continue' and 'break' in the condition of a for loop.
        getCurScope()->AddFlags(Scope::BreakScope | Scope::ContinueScope);

        ExprResult SecondExpr = ParseExpression();
        if (SecondExpr.isInvalid())
          SecondPart = Sema::ConditionError();
        else
          SecondPart = Actions.ActOnCondition(
              getCurScope(), ForLoc, SecondExpr.get(),
              Sema::ConditionKind::Boolean, /*MissingOK=*/true);
      }
    }
  }

  // Enter a break / continue scope, if we didn't already enter one while
  // parsing the second part.
  if (!getCurScope()->isContinueScope())
    getCurScope()->AddFlags(Scope::BreakScope | Scope::ContinueScope);

  // Parse the third part of the for statement.
  if (!ForEach && !ForRangeInfo.ParsedForRangeDecl()) {
    if (Tok.isNot(tok::semi)) {
      if (!SecondPart.isInvalid())
        Diag(Tok, diag::err_expected_semi_for);
      SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch);
    }

    if (Tok.is(tok::semi)) {
      ConsumeToken();
    }

    if (Tok.isNot(tok::r_paren)) {   // for (...;...;)
      ExprResult Third = ParseExpression();
      // FIXME: The C++11 standard doesn't actually say that this is a
      // discarded-value expression, but it clearly should be.
      ThirdPart = Actions.MakeFullDiscardedValueExpr(Third.get());
    }
  }
  // Match the ')'.
  T.consumeClose();

  // C++ Coroutines [stmt.iter]:
  //   'co_await' can only be used for a range-based for statement.
  if (CoawaitLoc.isValid() && !ForRangeInfo.ParsedForRangeDecl()) {
    Diag(CoawaitLoc, diag::err_for_co_await_not_range_for);
    CoawaitLoc = SourceLocation();
  }

  if (CoawaitLoc.isValid() && getLangOpts().CPlusPlus20)
    Diag(CoawaitLoc, diag::warn_deprecated_for_co_await);

  // We need to perform most of the semantic analysis for a C++0x for-range
  // statememt before parsing the body, in order to be able to deduce the type
  // of an auto-typed loop variable.
  StmtResult ForRangeStmt;
  StmtResult ForEachStmt;

  if (ForRangeInfo.ParsedForRangeDecl()) {
    ExprResult CorrectedRange =
        Actions.CorrectDelayedTyposInExpr(ForRangeInfo.RangeExpr.get());
    ForRangeStmt = Actions.ActOnCXXForRangeStmt(
        getCurScope(), ForLoc, CoawaitLoc, FirstPart.get(),
        ForRangeInfo.LoopVar.get(), ForRangeInfo.ColonLoc, CorrectedRange.get(),
        T.getCloseLocation(), Sema::BFRK_Build,
        ForRangeInfo.LifetimeExtendTemps);
  } else if (ForEach) {
    // Similarly, we need to do the semantic analysis for a for-range
    // statement immediately in order to close over temporaries correctly.
    ForEachStmt = Actions.ObjC().ActOnObjCForCollectionStmt(
        ForLoc, FirstPart.get(), Collection.get(), T.getCloseLocation());
  } else {
    // In OpenMP loop region loop control variable must be captured and be
    // private. Perform analysis of first part (if any).
    if (getLangOpts().OpenMP && FirstPart.isUsable()) {
      Actions.OpenMP().ActOnOpenMPLoopInitialization(ForLoc, FirstPart.get());
    }
  }

  // C99 6.8.5p5 - In C99, the body of the for statement is a scope, even if
  // there is no compound stmt.  C90 does not have this clause.  We only do this
  // if the body isn't a compound statement to avoid push/pop in common cases.
  //
  // C++ 6.5p2:
  // The substatement in an iteration-statement implicitly defines a local scope
  // which is entered and exited each time through the loop.
  //
  // See comments in ParseIfStatement for why we create a scope for
  // for-init-statement/condition and a new scope for substatement in C++.
  //
  ParseScope InnerScope(this, Scope::DeclScope, C99orCXXorObjC,
                        Tok.is(tok::l_brace));

  // The body of the for loop has the same local mangling number as the
  // for-init-statement.
  // It will only be incremented if the body contains other things that would
  // normally increment the mangling number (like a compound statement).
  if (C99orCXXorObjC)
    getCurScope()->decrementMSManglingNumber();

  MisleadingIndentationChecker MIChecker(*this, MSK_for, ForLoc);

  // Read the body statement.
  StmtResult Body(ParseStatement(TrailingElseLoc));

  if (Body.isUsable())
    MIChecker.Check();

  // Pop the body scope if needed.
  InnerScope.Exit();

  // Leave the for-scope.
  ForScope.Exit();

  if (Body.isInvalid())
    return StmtError();

  if (ForEach)
    return Actions.ObjC().FinishObjCForCollectionStmt(ForEachStmt.get(),
                                                      Body.get());

  if (ForRangeInfo.ParsedForRangeDecl())
    return Actions.FinishCXXForRangeStmt(ForRangeStmt.get(), Body.get());

  return Actions.ActOnForStmt(ForLoc, T.getOpenLocation(), FirstPart.get(),
                              SecondPart, ThirdPart, T.getCloseLocation(),
                              Body.get());
}

/// ParseGotoStatement
///       jump-statement:
///         'goto' identifier ';'
/// [GNU]   'goto' '*' expression ';'
///
/// Note: this lets the caller parse the end ';'.
///
StmtResult Parser::ParseGotoStatement() {
  assert(Tok.is(tok::kw_goto) && "Not a goto stmt!");
  SourceLocation GotoLoc = ConsumeToken();  // eat the 'goto'.

  StmtResult Res;
  if (Tok.is(tok::identifier)) {
    LabelDecl *LD = Actions.LookupOrCreateLabel(Tok.getIdentifierInfo(),
                                                Tok.getLocation());
    Res = Actions.ActOnGotoStmt(GotoLoc, Tok.getLocation(), LD);
    ConsumeToken();
  } else if (Tok.is(tok::star)) {
    // GNU indirect goto extension.
    Diag(Tok, diag::ext_gnu_indirect_goto);
    SourceLocation StarLoc = ConsumeToken();
    ExprResult R(ParseExpression());
    if (R.isInvalid()) {  // Skip to the semicolon, but don't consume it.
      SkipUntil(tok::semi, StopBeforeMatch);
      return StmtError();
    }
    Res = Actions.ActOnIndirectGotoStmt(GotoLoc, StarLoc, R.get());
  } else {
    Diag(Tok, diag::err_expected) << tok::identifier;
    return StmtError();
  }

  return Res;
}

/// ParseContinueStatement
///       jump-statement:
///         'continue' ';'
///
/// Note: this lets the caller parse the end ';'.
///
StmtResult Parser::ParseContinueStatement() {
  SourceLocation ContinueLoc = ConsumeToken();  // eat the 'continue'.
  return Actions.ActOnContinueStmt(ContinueLoc, getCurScope());
}

/// ParseBreakStatement
///       jump-statement:
///         'break' ';'
///
/// Note: this lets the caller parse the end ';'.
///
StmtResult Parser::ParseBreakStatement() {
  SourceLocation BreakLoc = ConsumeToken();  // eat the 'break'.
  return Actions.ActOnBreakStmt(BreakLoc, getCurScope());
}

/// ParseReturnStatement
///       jump-statement:
///         'return' expression[opt] ';'
///         'return' braced-init-list ';'
///         'co_return' expression[opt] ';'
///         'co_return' braced-init-list ';'
StmtResult Parser::ParseReturnStatement() {
  assert((Tok.is(tok::kw_return) || Tok.is(tok::kw_co_return)) &&
         "Not a return stmt!");
  bool IsCoreturn = Tok.is(tok::kw_co_return);
  SourceLocation ReturnLoc = ConsumeToken();  // eat the 'return'.

  ExprResult R;
  if (Tok.isNot(tok::semi)) {
    if (!IsCoreturn)
      PreferredType.enterReturn(Actions, Tok.getLocation());
    // FIXME: Code completion for co_return.
    if (Tok.is(tok::code_completion) && !IsCoreturn) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteExpression(
          getCurScope(), PreferredType.get(Tok.getLocation()));
      return StmtError();
    }

    if (Tok.is(tok::l_brace) && getLangOpts().CPlusPlus) {
      R = ParseInitializer();
      if (R.isUsable())
        Diag(R.get()->getBeginLoc(),
             getLangOpts().CPlusPlus11
                 ? diag::warn_cxx98_compat_generalized_initializer_lists
                 : diag::ext_generalized_initializer_lists)
            << R.get()->getSourceRange();
    } else
      R = ParseExpression();
    if (R.isInvalid()) {
      SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
      return StmtError();
    }
  }
  if (IsCoreturn)
    return Actions.ActOnCoreturnStmt(getCurScope(), ReturnLoc, R.get());
  return Actions.ActOnReturnStmt(ReturnLoc, R.get(), getCurScope());
}

StmtResult Parser::ParsePragmaLoopHint(StmtVector &Stmts,
                                       ParsedStmtContext StmtCtx,
                                       SourceLocation *TrailingElseLoc,
                                       ParsedAttributes &Attrs) {
  // Create temporary attribute list.
  ParsedAttributes TempAttrs(AttrFactory);

  SourceLocation StartLoc = Tok.getLocation();

  // Get loop hints and consume annotated token.
  while (Tok.is(tok::annot_pragma_loop_hint)) {
    LoopHint Hint;
    if (!HandlePragmaLoopHint(Hint))
      continue;

    ArgsUnion ArgHints[] = {Hint.PragmaNameLoc, Hint.OptionLoc, Hint.StateLoc,
                            ArgsUnion(Hint.ValueExpr)};
    TempAttrs.addNew(Hint.PragmaNameLoc->Ident, Hint.Range, nullptr,
                     Hint.PragmaNameLoc->Loc, ArgHints, 4,
                     ParsedAttr::Form::Pragma());
  }

  // Get the next statement.
  MaybeParseCXX11Attributes(Attrs);

  ParsedAttributes EmptyDeclSpecAttrs(AttrFactory);
  StmtResult S = ParseStatementOrDeclarationAfterAttributes(
      Stmts, StmtCtx, TrailingElseLoc, Attrs, EmptyDeclSpecAttrs);

  Attrs.takeAllFrom(TempAttrs);

  // Start of attribute range may already be set for some invalid input.
  // See PR46336.
  if (Attrs.Range.getBegin().isInvalid())
    Attrs.Range.setBegin(StartLoc);

  return S;
}

Decl *Parser::ParseFunctionStatementBody(Decl *Decl, ParseScope &BodyScope) {
  assert(Tok.is(tok::l_brace));
  SourceLocation LBraceLoc = Tok.getLocation();

  PrettyDeclStackTraceEntry CrashInfo(Actions.Context, Decl, LBraceLoc,
                                      "parsing function body");

  // Save and reset current vtordisp stack if we have entered a C++ method body.
  bool IsCXXMethod =
      getLangOpts().CPlusPlus && Decl && isa<CXXMethodDecl>(Decl);
  Sema::PragmaStackSentinelRAII
    PragmaStackSentinel(Actions, "InternalPragmaState", IsCXXMethod);

  // Do not enter a scope for the brace, as the arguments are in the same scope
  // (the function body) as the body itself.  Instead, just read the statement
  // list and put it into a CompoundStmt for safe keeping.
  StmtResult FnBody(ParseCompoundStatementBody());

  // If the function body could not be parsed, make a bogus compoundstmt.
  if (FnBody.isInvalid()) {
    Sema::CompoundScopeRAII CompoundScope(Actions);
    FnBody =
        Actions.ActOnCompoundStmt(LBraceLoc, LBraceLoc, std::nullopt, false);
  }

  BodyScope.Exit();
  return Actions.ActOnFinishFunctionBody(Decl, FnBody.get());
}

/// ParseFunctionTryBlock - Parse a C++ function-try-block.
///
///       function-try-block:
///         'try' ctor-initializer[opt] compound-statement handler-seq
///
Decl *Parser::ParseFunctionTryBlock(Decl *Decl, ParseScope &BodyScope) {
  assert(Tok.is(tok::kw_try) && "Expected 'try'");
  SourceLocation TryLoc = ConsumeToken();

  PrettyDeclStackTraceEntry CrashInfo(Actions.Context, Decl, TryLoc,
                                      "parsing function try block");

  // Constructor initializer list?
  if (Tok.is(tok::colon))
    ParseConstructorInitializer(Decl);
  else
    Actions.ActOnDefaultCtorInitializers(Decl);

  // Save and reset current vtordisp stack if we have entered a C++ method body.
  bool IsCXXMethod =
      getLangOpts().CPlusPlus && Decl && isa<CXXMethodDecl>(Decl);
  Sema::PragmaStackSentinelRAII
    PragmaStackSentinel(Actions, "InternalPragmaState", IsCXXMethod);

  SourceLocation LBraceLoc = Tok.getLocation();
  StmtResult FnBody(ParseCXXTryBlockCommon(TryLoc, /*FnTry*/true));
  // If we failed to parse the try-catch, we just give the function an empty
  // compound statement as the body.
  if (FnBody.isInvalid()) {
    Sema::CompoundScopeRAII CompoundScope(Actions);
    FnBody =
        Actions.ActOnCompoundStmt(LBraceLoc, LBraceLoc, std::nullopt, false);
  }

  BodyScope.Exit();
  return Actions.ActOnFinishFunctionBody(Decl, FnBody.get());
}

bool Parser::trySkippingFunctionBody() {
  assert(SkipFunctionBodies &&
         "Should only be called when SkipFunctionBodies is enabled");
  if (!PP.isCodeCompletionEnabled()) {
    SkipFunctionBody();
    return true;
  }

  // We're in code-completion mode. Skip parsing for all function bodies unless
  // the body contains the code-completion point.
  TentativeParsingAction PA(*this);
  bool IsTryCatch = Tok.is(tok::kw_try);
  CachedTokens Toks;
  bool ErrorInPrologue = ConsumeAndStoreFunctionPrologue(Toks);
  if (llvm::any_of(Toks, [](const Token &Tok) {
        return Tok.is(tok::code_completion);
      })) {
    PA.Revert();
    return false;
  }
  if (ErrorInPrologue) {
    PA.Commit();
    SkipMalformedDecl();
    return true;
  }
  if (!SkipUntil(tok::r_brace, StopAtCodeCompletion)) {
    PA.Revert();
    return false;
  }
  while (IsTryCatch && Tok.is(tok::kw_catch)) {
    if (!SkipUntil(tok::l_brace, StopAtCodeCompletion) ||
        !SkipUntil(tok::r_brace, StopAtCodeCompletion)) {
      PA.Revert();
      return false;
    }
  }
  PA.Commit();
  return true;
}

/// ParseCXXTryBlock - Parse a C++ try-block.
///
///       try-block:
///         'try' compound-statement handler-seq
///
StmtResult Parser::ParseCXXTryBlock() {
  assert(Tok.is(tok::kw_try) && "Expected 'try'");

  SourceLocation TryLoc = ConsumeToken();
  return ParseCXXTryBlockCommon(TryLoc);
}

/// ParseCXXTryBlockCommon - Parse the common part of try-block and
/// function-try-block.
///
///       try-block:
///         'try' compound-statement handler-seq
///
///       function-try-block:
///         'try' ctor-initializer[opt] compound-statement handler-seq
///
///       handler-seq:
///         handler handler-seq[opt]
///
///       [Borland] try-block:
///         'try' compound-statement seh-except-block
///         'try' compound-statement seh-finally-block
///
StmtResult Parser::ParseCXXTryBlockCommon(SourceLocation TryLoc, bool FnTry) {
  if (Tok.isNot(tok::l_brace))
    return StmtError(Diag(Tok, diag::err_expected) << tok::l_brace);

  StmtResult TryBlock(ParseCompoundStatement(
      /*isStmtExpr=*/false, Scope::DeclScope | Scope::TryScope |
                                Scope::CompoundStmtScope |
                                (FnTry ? Scope::FnTryCatchScope : 0)));
  if (TryBlock.isInvalid())
    return TryBlock;

  // Borland allows SEH-handlers with 'try'

  if ((Tok.is(tok::identifier) &&
       Tok.getIdentifierInfo() == getSEHExceptKeyword()) ||
      Tok.is(tok::kw___finally)) {
    // TODO: Factor into common return ParseSEHHandlerCommon(...)
    StmtResult Handler;
    if(Tok.getIdentifierInfo() == getSEHExceptKeyword()) {
      SourceLocation Loc = ConsumeToken();
      Handler = ParseSEHExceptBlock(Loc);
    }
    else {
      SourceLocation Loc = ConsumeToken();
      Handler = ParseSEHFinallyBlock(Loc);
    }
    if(Handler.isInvalid())
      return Handler;

    return Actions.ActOnSEHTryBlock(true /* IsCXXTry */,
                                    TryLoc,
                                    TryBlock.get(),
                                    Handler.get());
  }
  else {
    StmtVector Handlers;

    // C++11 attributes can't appear here, despite this context seeming
    // statement-like.
    DiagnoseAndSkipCXX11Attributes();

    if (Tok.isNot(tok::kw_catch))
      return StmtError(Diag(Tok, diag::err_expected_catch));
    while (Tok.is(tok::kw_catch)) {
      StmtResult Handler(ParseCXXCatchBlock(FnTry));
      if (!Handler.isInvalid())
        Handlers.push_back(Handler.get());
    }
    // Don't bother creating the full statement if we don't have any usable
    // handlers.
    if (Handlers.empty())
      return StmtError();

    return Actions.ActOnCXXTryBlock(TryLoc, TryBlock.get(), Handlers);
  }
}

/// ParseCXXCatchBlock - Parse a C++ catch block, called handler in the standard
///
///   handler:
///     'catch' '(' exception-declaration ')' compound-statement
///
///   exception-declaration:
///     attribute-specifier-seq[opt] type-specifier-seq declarator
///     attribute-specifier-seq[opt] type-specifier-seq abstract-declarator[opt]
///     '...'
///
StmtResult Parser::ParseCXXCatchBlock(bool FnCatch) {
  assert(Tok.is(tok::kw_catch) && "Expected 'catch'");

  SourceLocation CatchLoc = ConsumeToken();

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.expectAndConsume())
    return StmtError();

  // C++ 3.3.2p3:
  // The name in a catch exception-declaration is local to the handler and
  // shall not be redeclared in the outermost block of the handler.
  ParseScope CatchScope(this, Scope::DeclScope | Scope::ControlScope |
                                  Scope::CatchScope |
                                  (FnCatch ? Scope::FnTryCatchScope : 0));

  // exception-declaration is equivalent to '...' or a parameter-declaration
  // without default arguments.
  Decl *ExceptionDecl = nullptr;
  if (Tok.isNot(tok::ellipsis)) {
    ParsedAttributes Attributes(AttrFactory);
    MaybeParseCXX11Attributes(Attributes);

    DeclSpec DS(AttrFactory);

    if (ParseCXXTypeSpecifierSeq(DS))
      return StmtError();

    Declarator ExDecl(DS, Attributes, DeclaratorContext::CXXCatch);
    ParseDeclarator(ExDecl);
    ExceptionDecl = Actions.ActOnExceptionDeclarator(getCurScope(), ExDecl);
  } else
    ConsumeToken();

  T.consumeClose();
  if (T.getCloseLocation().isInvalid())
    return StmtError();

  if (Tok.isNot(tok::l_brace))
    return StmtError(Diag(Tok, diag::err_expected) << tok::l_brace);

  // FIXME: Possible draft standard bug: attribute-specifier should be allowed?
  StmtResult Block(ParseCompoundStatement());
  if (Block.isInvalid())
    return Block;

  return Actions.ActOnCXXCatchBlock(CatchLoc, ExceptionDecl, Block.get());
}

void Parser::ParseMicrosoftIfExistsStatement(StmtVector &Stmts) {
  IfExistsCondition Result;
  if (ParseMicrosoftIfExistsCondition(Result))
    return;

  // Handle dependent statements by parsing the braces as a compound statement.
  // This is not the same behavior as Visual C++, which don't treat this as a
  // compound statement, but for Clang's type checking we can't have anything
  // inside these braces escaping to the surrounding code.
  if (Result.Behavior == IEB_Dependent) {
    if (!Tok.is(tok::l_brace)) {
      Diag(Tok, diag::err_expected) << tok::l_brace;
      return;
    }

    StmtResult Compound = ParseCompoundStatement();
    if (Compound.isInvalid())
      return;

    StmtResult DepResult = Actions.ActOnMSDependentExistsStmt(Result.KeywordLoc,
                                                              Result.IsIfExists,
                                                              Result.SS,
                                                              Result.Name,
                                                              Compound.get());
    if (DepResult.isUsable())
      Stmts.push_back(DepResult.get());
    return;
  }

  BalancedDelimiterTracker Braces(*this, tok::l_brace);
  if (Braces.consumeOpen()) {
    Diag(Tok, diag::err_expected) << tok::l_brace;
    return;
  }

  switch (Result.Behavior) {
  case IEB_Parse:
    // Parse the statements below.
    break;

  case IEB_Dependent:
    llvm_unreachable("Dependent case handled above");

  case IEB_Skip:
    Braces.skipToEnd();
    return;
  }

  // Condition is true, parse the statements.
  while (Tok.isNot(tok::r_brace)) {
    StmtResult R =
        ParseStatementOrDeclaration(Stmts, ParsedStmtContext::Compound);
    if (R.isUsable())
      Stmts.push_back(R.get());
  }
  Braces.consumeClose();
}
