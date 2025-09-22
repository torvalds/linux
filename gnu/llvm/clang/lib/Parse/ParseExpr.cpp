//===--- ParseExpr.cpp - Expression Parsing -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Provides the Expression parsing implementation.
///
/// Expressions in C99 basically consist of a bunch of binary operators with
/// unary operators and other random stuff at the leaves.
///
/// In the C99 grammar, these unary operators bind tightest and are represented
/// as the 'cast-expression' production.  Everything else is either a binary
/// operator (e.g. '/') or a ternary operator ("?:").  The unary leaves are
/// handled by ParseCastExpression, the higher level pieces are handled by
/// ParseBinaryExpression.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/PrettyStackTrace.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaCodeCompletion.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/SemaOpenACC.h"
#include "clang/Sema/SemaOpenMP.h"
#include "clang/Sema/SemaSYCL.h"
#include "clang/Sema/TypoCorrection.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>
using namespace clang;

/// Simple precedence-based parser for binary/ternary operators.
///
/// Note: we diverge from the C99 grammar when parsing the assignment-expression
/// production.  C99 specifies that the LHS of an assignment operator should be
/// parsed as a unary-expression, but consistency dictates that it be a
/// conditional-expession.  In practice, the important thing here is that the
/// LHS of an assignment has to be an l-value, which productions between
/// unary-expression and conditional-expression don't produce.  Because we want
/// consistency, we parse the LHS as a conditional-expression, then check for
/// l-value-ness in semantic analysis stages.
///
/// \verbatim
///       pm-expression: [C++ 5.5]
///         cast-expression
///         pm-expression '.*' cast-expression
///         pm-expression '->*' cast-expression
///
///       multiplicative-expression: [C99 6.5.5]
///     Note: in C++, apply pm-expression instead of cast-expression
///         cast-expression
///         multiplicative-expression '*' cast-expression
///         multiplicative-expression '/' cast-expression
///         multiplicative-expression '%' cast-expression
///
///       additive-expression: [C99 6.5.6]
///         multiplicative-expression
///         additive-expression '+' multiplicative-expression
///         additive-expression '-' multiplicative-expression
///
///       shift-expression: [C99 6.5.7]
///         additive-expression
///         shift-expression '<<' additive-expression
///         shift-expression '>>' additive-expression
///
///       compare-expression: [C++20 expr.spaceship]
///         shift-expression
///         compare-expression '<=>' shift-expression
///
///       relational-expression: [C99 6.5.8]
///         compare-expression
///         relational-expression '<' compare-expression
///         relational-expression '>' compare-expression
///         relational-expression '<=' compare-expression
///         relational-expression '>=' compare-expression
///
///       equality-expression: [C99 6.5.9]
///         relational-expression
///         equality-expression '==' relational-expression
///         equality-expression '!=' relational-expression
///
///       AND-expression: [C99 6.5.10]
///         equality-expression
///         AND-expression '&' equality-expression
///
///       exclusive-OR-expression: [C99 6.5.11]
///         AND-expression
///         exclusive-OR-expression '^' AND-expression
///
///       inclusive-OR-expression: [C99 6.5.12]
///         exclusive-OR-expression
///         inclusive-OR-expression '|' exclusive-OR-expression
///
///       logical-AND-expression: [C99 6.5.13]
///         inclusive-OR-expression
///         logical-AND-expression '&&' inclusive-OR-expression
///
///       logical-OR-expression: [C99 6.5.14]
///         logical-AND-expression
///         logical-OR-expression '||' logical-AND-expression
///
///       conditional-expression: [C99 6.5.15]
///         logical-OR-expression
///         logical-OR-expression '?' expression ':' conditional-expression
/// [GNU]   logical-OR-expression '?' ':' conditional-expression
/// [C++] the third operand is an assignment-expression
///
///       assignment-expression: [C99 6.5.16]
///         conditional-expression
///         unary-expression assignment-operator assignment-expression
/// [C++]   throw-expression [C++ 15]
///
///       assignment-operator: one of
///         = *= /= %= += -= <<= >>= &= ^= |=
///
///       expression: [C99 6.5.17]
///         assignment-expression ...[opt]
///         expression ',' assignment-expression ...[opt]
/// \endverbatim
ExprResult Parser::ParseExpression(TypeCastState isTypeCast) {
  ExprResult LHS(ParseAssignmentExpression(isTypeCast));
  return ParseRHSOfBinaryExpression(LHS, prec::Comma);
}

/// This routine is called when the '@' is seen and consumed.
/// Current token is an Identifier and is not a 'try'. This
/// routine is necessary to disambiguate \@try-statement from,
/// for example, \@encode-expression.
///
ExprResult
Parser::ParseExpressionWithLeadingAt(SourceLocation AtLoc) {
  ExprResult LHS(ParseObjCAtExpression(AtLoc));
  return ParseRHSOfBinaryExpression(LHS, prec::Comma);
}

/// This routine is called when a leading '__extension__' is seen and
/// consumed.  This is necessary because the token gets consumed in the
/// process of disambiguating between an expression and a declaration.
ExprResult
Parser::ParseExpressionWithLeadingExtension(SourceLocation ExtLoc) {
  ExprResult LHS(true);
  {
    // Silence extension warnings in the sub-expression
    ExtensionRAIIObject O(Diags);

    LHS = ParseCastExpression(AnyCastExpr);
  }

  if (!LHS.isInvalid())
    LHS = Actions.ActOnUnaryOp(getCurScope(), ExtLoc, tok::kw___extension__,
                               LHS.get());

  return ParseRHSOfBinaryExpression(LHS, prec::Comma);
}

/// Parse an expr that doesn't include (top-level) commas.
ExprResult Parser::ParseAssignmentExpression(TypeCastState isTypeCast) {
  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteExpression(
        getCurScope(), PreferredType.get(Tok.getLocation()));
    return ExprError();
  }

  if (Tok.is(tok::kw_throw))
    return ParseThrowExpression();
  if (Tok.is(tok::kw_co_yield))
    return ParseCoyieldExpression();

  ExprResult LHS = ParseCastExpression(AnyCastExpr,
                                       /*isAddressOfOperand=*/false,
                                       isTypeCast);
  return ParseRHSOfBinaryExpression(LHS, prec::Assignment);
}

ExprResult Parser::ParseConditionalExpression() {
  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteExpression(
        getCurScope(), PreferredType.get(Tok.getLocation()));
    return ExprError();
  }

  ExprResult LHS = ParseCastExpression(
      AnyCastExpr, /*isAddressOfOperand=*/false, NotTypeCast);
  return ParseRHSOfBinaryExpression(LHS, prec::Conditional);
}

/// Parse an assignment expression where part of an Objective-C message
/// send has already been parsed.
///
/// In this case \p LBracLoc indicates the location of the '[' of the message
/// send, and either \p ReceiverName or \p ReceiverExpr is non-null indicating
/// the receiver of the message.
///
/// Since this handles full assignment-expression's, it handles postfix
/// expressions and other binary operators for these expressions as well.
ExprResult
Parser::ParseAssignmentExprWithObjCMessageExprStart(SourceLocation LBracLoc,
                                                    SourceLocation SuperLoc,
                                                    ParsedType ReceiverType,
                                                    Expr *ReceiverExpr) {
  ExprResult R
    = ParseObjCMessageExpressionBody(LBracLoc, SuperLoc,
                                     ReceiverType, ReceiverExpr);
  R = ParsePostfixExpressionSuffix(R);
  return ParseRHSOfBinaryExpression(R, prec::Assignment);
}

ExprResult
Parser::ParseConstantExpressionInExprEvalContext(TypeCastState isTypeCast) {
  assert(Actions.ExprEvalContexts.back().Context ==
             Sema::ExpressionEvaluationContext::ConstantEvaluated &&
         "Call this function only if your ExpressionEvaluationContext is "
         "already ConstantEvaluated");
  ExprResult LHS(ParseCastExpression(AnyCastExpr, false, isTypeCast));
  ExprResult Res(ParseRHSOfBinaryExpression(LHS, prec::Conditional));
  return Actions.ActOnConstantExpression(Res);
}

ExprResult Parser::ParseConstantExpression() {
  // C++03 [basic.def.odr]p2:
  //   An expression is potentially evaluated unless it appears where an
  //   integral constant expression is required (see 5.19) [...].
  // C++98 and C++11 have no such rule, but this is only a defect in C++98.
  EnterExpressionEvaluationContext ConstantEvaluated(
      Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);
  return ParseConstantExpressionInExprEvalContext(NotTypeCast);
}

ExprResult Parser::ParseArrayBoundExpression() {
  EnterExpressionEvaluationContext ConstantEvaluated(
      Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);
  // If we parse the bound of a VLA... we parse a non-constant
  // constant-expression!
  Actions.ExprEvalContexts.back().InConditionallyConstantEvaluateContext = true;
  return ParseConstantExpressionInExprEvalContext(NotTypeCast);
}

ExprResult Parser::ParseCaseExpression(SourceLocation CaseLoc) {
  EnterExpressionEvaluationContext ConstantEvaluated(
      Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);
  ExprResult LHS(ParseCastExpression(AnyCastExpr, false, NotTypeCast));
  ExprResult Res(ParseRHSOfBinaryExpression(LHS, prec::Conditional));
  return Actions.ActOnCaseExpr(CaseLoc, Res);
}

/// Parse a constraint-expression.
///
/// \verbatim
///       constraint-expression: C++2a[temp.constr.decl]p1
///         logical-or-expression
/// \endverbatim
ExprResult Parser::ParseConstraintExpression() {
  EnterExpressionEvaluationContext ConstantEvaluated(
      Actions, Sema::ExpressionEvaluationContext::Unevaluated);
  ExprResult LHS(ParseCastExpression(AnyCastExpr));
  ExprResult Res(ParseRHSOfBinaryExpression(LHS, prec::LogicalOr));
  if (Res.isUsable() && !Actions.CheckConstraintExpression(Res.get())) {
    Actions.CorrectDelayedTyposInExpr(Res);
    return ExprError();
  }
  return Res;
}

/// \brief Parse a constraint-logical-and-expression.
///
/// \verbatim
///       C++2a[temp.constr.decl]p1
///       constraint-logical-and-expression:
///         primary-expression
///         constraint-logical-and-expression '&&' primary-expression
///
/// \endverbatim
ExprResult
Parser::ParseConstraintLogicalAndExpression(bool IsTrailingRequiresClause) {
  EnterExpressionEvaluationContext ConstantEvaluated(
      Actions, Sema::ExpressionEvaluationContext::Unevaluated);
  bool NotPrimaryExpression = false;
  auto ParsePrimary = [&] () {
    ExprResult E = ParseCastExpression(PrimaryExprOnly,
                                       /*isAddressOfOperand=*/false,
                                       /*isTypeCast=*/NotTypeCast,
                                       /*isVectorLiteral=*/false,
                                       &NotPrimaryExpression);
    if (E.isInvalid())
      return ExprError();
    auto RecoverFromNonPrimary = [&] (ExprResult E, bool Note) {
        E = ParsePostfixExpressionSuffix(E);
        // Use InclusiveOr, the precedence just after '&&' to not parse the
        // next arguments to the logical and.
        E = ParseRHSOfBinaryExpression(E, prec::InclusiveOr);
        if (!E.isInvalid())
          Diag(E.get()->getExprLoc(),
               Note
               ? diag::note_unparenthesized_non_primary_expr_in_requires_clause
               : diag::err_unparenthesized_non_primary_expr_in_requires_clause)
               << FixItHint::CreateInsertion(E.get()->getBeginLoc(), "(")
               << FixItHint::CreateInsertion(
                   PP.getLocForEndOfToken(E.get()->getEndLoc()), ")")
               << E.get()->getSourceRange();
        return E;
    };

    if (NotPrimaryExpression ||
        // Check if the following tokens must be a part of a non-primary
        // expression
        getBinOpPrecedence(Tok.getKind(), GreaterThanIsOperator,
                           /*CPlusPlus11=*/true) > prec::LogicalAnd ||
        // Postfix operators other than '(' (which will be checked for in
        // CheckConstraintExpression).
        Tok.isOneOf(tok::period, tok::plusplus, tok::minusminus) ||
        (Tok.is(tok::l_square) && !NextToken().is(tok::l_square))) {
      E = RecoverFromNonPrimary(E, /*Note=*/false);
      if (E.isInvalid())
        return ExprError();
      NotPrimaryExpression = false;
    }
    bool PossibleNonPrimary;
    bool IsConstraintExpr =
        Actions.CheckConstraintExpression(E.get(), Tok, &PossibleNonPrimary,
                                          IsTrailingRequiresClause);
    if (!IsConstraintExpr || PossibleNonPrimary) {
      // Atomic constraint might be an unparenthesized non-primary expression
      // (such as a binary operator), in which case we might get here (e.g. in
      // 'requires 0 + 1 && true' we would now be at '+', and parse and ignore
      // the rest of the addition expression). Try to parse the rest of it here.
      if (PossibleNonPrimary)
        E = RecoverFromNonPrimary(E, /*Note=*/!IsConstraintExpr);
      Actions.CorrectDelayedTyposInExpr(E);
      return ExprError();
    }
    return E;
  };
  ExprResult LHS = ParsePrimary();
  if (LHS.isInvalid())
    return ExprError();
  while (Tok.is(tok::ampamp)) {
    SourceLocation LogicalAndLoc = ConsumeToken();
    ExprResult RHS = ParsePrimary();
    if (RHS.isInvalid()) {
      Actions.CorrectDelayedTyposInExpr(LHS);
      return ExprError();
    }
    ExprResult Op = Actions.ActOnBinOp(getCurScope(), LogicalAndLoc,
                                       tok::ampamp, LHS.get(), RHS.get());
    if (!Op.isUsable()) {
      Actions.CorrectDelayedTyposInExpr(RHS);
      Actions.CorrectDelayedTyposInExpr(LHS);
      return ExprError();
    }
    LHS = Op;
  }
  return LHS;
}

/// \brief Parse a constraint-logical-or-expression.
///
/// \verbatim
///       C++2a[temp.constr.decl]p1
///       constraint-logical-or-expression:
///         constraint-logical-and-expression
///         constraint-logical-or-expression '||'
///             constraint-logical-and-expression
///
/// \endverbatim
ExprResult
Parser::ParseConstraintLogicalOrExpression(bool IsTrailingRequiresClause) {
  ExprResult LHS(ParseConstraintLogicalAndExpression(IsTrailingRequiresClause));
  if (!LHS.isUsable())
    return ExprError();
  while (Tok.is(tok::pipepipe)) {
    SourceLocation LogicalOrLoc = ConsumeToken();
    ExprResult RHS =
        ParseConstraintLogicalAndExpression(IsTrailingRequiresClause);
    if (!RHS.isUsable()) {
      Actions.CorrectDelayedTyposInExpr(LHS);
      return ExprError();
    }
    ExprResult Op = Actions.ActOnBinOp(getCurScope(), LogicalOrLoc,
                                       tok::pipepipe, LHS.get(), RHS.get());
    if (!Op.isUsable()) {
      Actions.CorrectDelayedTyposInExpr(RHS);
      Actions.CorrectDelayedTyposInExpr(LHS);
      return ExprError();
    }
    LHS = Op;
  }
  return LHS;
}

bool Parser::isNotExpressionStart() {
  tok::TokenKind K = Tok.getKind();
  if (K == tok::l_brace || K == tok::r_brace  ||
      K == tok::kw_for  || K == tok::kw_while ||
      K == tok::kw_if   || K == tok::kw_else  ||
      K == tok::kw_goto || K == tok::kw_try)
    return true;
  // If this is a decl-specifier, we can't be at the start of an expression.
  return isKnownToBeDeclarationSpecifier();
}

bool Parser::isFoldOperator(prec::Level Level) const {
  return Level > prec::Unknown && Level != prec::Conditional &&
         Level != prec::Spaceship;
}

bool Parser::isFoldOperator(tok::TokenKind Kind) const {
  return isFoldOperator(getBinOpPrecedence(Kind, GreaterThanIsOperator, true));
}

/// Parse a binary expression that starts with \p LHS and has a
/// precedence of at least \p MinPrec.
ExprResult
Parser::ParseRHSOfBinaryExpression(ExprResult LHS, prec::Level MinPrec) {
  prec::Level NextTokPrec = getBinOpPrecedence(Tok.getKind(),
                                               GreaterThanIsOperator,
                                               getLangOpts().CPlusPlus11);
  SourceLocation ColonLoc;

  auto SavedType = PreferredType;
  while (true) {
    // Every iteration may rely on a preferred type for the whole expression.
    PreferredType = SavedType;
    // If this token has a lower precedence than we are allowed to parse (e.g.
    // because we are called recursively, or because the token is not a binop),
    // then we are done!
    if (NextTokPrec < MinPrec)
      return LHS;

    // Consume the operator, saving the operator token for error reporting.
    Token OpToken = Tok;
    ConsumeToken();

    if (OpToken.is(tok::caretcaret)) {
      return ExprError(Diag(Tok, diag::err_opencl_logical_exclusive_or));
    }

    // If we're potentially in a template-id, we may now be able to determine
    // whether we're actually in one or not.
    if (OpToken.isOneOf(tok::comma, tok::greater, tok::greatergreater,
                        tok::greatergreatergreater) &&
        checkPotentialAngleBracketDelimiter(OpToken))
      return ExprError();

    // Bail out when encountering a comma followed by a token which can't
    // possibly be the start of an expression. For instance:
    //   int f() { return 1, }
    // We can't do this before consuming the comma, because
    // isNotExpressionStart() looks at the token stream.
    if (OpToken.is(tok::comma) && isNotExpressionStart()) {
      PP.EnterToken(Tok, /*IsReinject*/true);
      Tok = OpToken;
      return LHS;
    }

    // If the next token is an ellipsis, then this is a fold-expression. Leave
    // it alone so we can handle it in the paren expression.
    if (isFoldOperator(NextTokPrec) && Tok.is(tok::ellipsis)) {
      // FIXME: We can't check this via lookahead before we consume the token
      // because that tickles a lexer bug.
      PP.EnterToken(Tok, /*IsReinject*/true);
      Tok = OpToken;
      return LHS;
    }

    // In Objective-C++, alternative operator tokens can be used as keyword args
    // in message expressions. Unconsume the token so that it can reinterpreted
    // as an identifier in ParseObjCMessageExpressionBody. i.e., we support:
    //   [foo meth:0 and:0];
    //   [foo not_eq];
    if (getLangOpts().ObjC && getLangOpts().CPlusPlus &&
        Tok.isOneOf(tok::colon, tok::r_square) &&
        OpToken.getIdentifierInfo() != nullptr) {
      PP.EnterToken(Tok, /*IsReinject*/true);
      Tok = OpToken;
      return LHS;
    }

    // Special case handling for the ternary operator.
    ExprResult TernaryMiddle(true);
    if (NextTokPrec == prec::Conditional) {
      if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
        // Parse a braced-init-list here for error recovery purposes.
        SourceLocation BraceLoc = Tok.getLocation();
        TernaryMiddle = ParseBraceInitializer();
        if (!TernaryMiddle.isInvalid()) {
          Diag(BraceLoc, diag::err_init_list_bin_op)
              << /*RHS*/ 1 << PP.getSpelling(OpToken)
              << Actions.getExprRange(TernaryMiddle.get());
          TernaryMiddle = ExprError();
        }
      } else if (Tok.isNot(tok::colon)) {
        // Don't parse FOO:BAR as if it were a typo for FOO::BAR.
        ColonProtectionRAIIObject X(*this);

        // Handle this production specially:
        //   logical-OR-expression '?' expression ':' conditional-expression
        // In particular, the RHS of the '?' is 'expression', not
        // 'logical-OR-expression' as we might expect.
        TernaryMiddle = ParseExpression();
      } else {
        // Special case handling of "X ? Y : Z" where Y is empty:
        //   logical-OR-expression '?' ':' conditional-expression   [GNU]
        TernaryMiddle = nullptr;
        Diag(Tok, diag::ext_gnu_conditional_expr);
      }

      if (TernaryMiddle.isInvalid()) {
        Actions.CorrectDelayedTyposInExpr(LHS);
        LHS = ExprError();
        TernaryMiddle = nullptr;
      }

      if (!TryConsumeToken(tok::colon, ColonLoc)) {
        // Otherwise, we're missing a ':'.  Assume that this was a typo that
        // the user forgot. If we're not in a macro expansion, we can suggest
        // a fixit hint. If there were two spaces before the current token,
        // suggest inserting the colon in between them, otherwise insert ": ".
        SourceLocation FILoc = Tok.getLocation();
        const char *FIText = ": ";
        const SourceManager &SM = PP.getSourceManager();
        if (FILoc.isFileID() || PP.isAtStartOfMacroExpansion(FILoc, &FILoc)) {
          assert(FILoc.isFileID());
          bool IsInvalid = false;
          const char *SourcePtr =
            SM.getCharacterData(FILoc.getLocWithOffset(-1), &IsInvalid);
          if (!IsInvalid && *SourcePtr == ' ') {
            SourcePtr =
              SM.getCharacterData(FILoc.getLocWithOffset(-2), &IsInvalid);
            if (!IsInvalid && *SourcePtr == ' ') {
              FILoc = FILoc.getLocWithOffset(-1);
              FIText = ":";
            }
          }
        }

        Diag(Tok, diag::err_expected)
            << tok::colon << FixItHint::CreateInsertion(FILoc, FIText);
        Diag(OpToken, diag::note_matching) << tok::question;
        ColonLoc = Tok.getLocation();
      }
    }

    PreferredType.enterBinary(Actions, Tok.getLocation(), LHS.get(),
                              OpToken.getKind());
    // Parse another leaf here for the RHS of the operator.
    // ParseCastExpression works here because all RHS expressions in C have it
    // as a prefix, at least. However, in C++, an assignment-expression could
    // be a throw-expression, which is not a valid cast-expression.
    // Therefore we need some special-casing here.
    // Also note that the third operand of the conditional operator is
    // an assignment-expression in C++, and in C++11, we can have a
    // braced-init-list on the RHS of an assignment. For better diagnostics,
    // parse as if we were allowed braced-init-lists everywhere, and check that
    // they only appear on the RHS of assignments later.
    ExprResult RHS;
    bool RHSIsInitList = false;
    if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
      RHS = ParseBraceInitializer();
      RHSIsInitList = true;
    } else if (getLangOpts().CPlusPlus && NextTokPrec <= prec::Conditional)
      RHS = ParseAssignmentExpression();
    else
      RHS = ParseCastExpression(AnyCastExpr);

    if (RHS.isInvalid()) {
      // FIXME: Errors generated by the delayed typo correction should be
      // printed before errors from parsing the RHS, not after.
      Actions.CorrectDelayedTyposInExpr(LHS);
      if (TernaryMiddle.isUsable())
        TernaryMiddle = Actions.CorrectDelayedTyposInExpr(TernaryMiddle);
      LHS = ExprError();
    }

    // Remember the precedence of this operator and get the precedence of the
    // operator immediately to the right of the RHS.
    prec::Level ThisPrec = NextTokPrec;
    NextTokPrec = getBinOpPrecedence(Tok.getKind(), GreaterThanIsOperator,
                                     getLangOpts().CPlusPlus11);

    // Assignment and conditional expressions are right-associative.
    bool isRightAssoc = ThisPrec == prec::Conditional ||
                        ThisPrec == prec::Assignment;

    // Get the precedence of the operator to the right of the RHS.  If it binds
    // more tightly with RHS than we do, evaluate it completely first.
    if (ThisPrec < NextTokPrec ||
        (ThisPrec == NextTokPrec && isRightAssoc)) {
      if (!RHS.isInvalid() && RHSIsInitList) {
        Diag(Tok, diag::err_init_list_bin_op)
          << /*LHS*/0 << PP.getSpelling(Tok) << Actions.getExprRange(RHS.get());
        RHS = ExprError();
      }
      // If this is left-associative, only parse things on the RHS that bind
      // more tightly than the current operator.  If it is left-associative, it
      // is okay, to bind exactly as tightly.  For example, compile A=B=C=D as
      // A=(B=(C=D)), where each paren is a level of recursion here.
      // The function takes ownership of the RHS.
      RHS = ParseRHSOfBinaryExpression(RHS,
                            static_cast<prec::Level>(ThisPrec + !isRightAssoc));
      RHSIsInitList = false;

      if (RHS.isInvalid()) {
        // FIXME: Errors generated by the delayed typo correction should be
        // printed before errors from ParseRHSOfBinaryExpression, not after.
        Actions.CorrectDelayedTyposInExpr(LHS);
        if (TernaryMiddle.isUsable())
          TernaryMiddle = Actions.CorrectDelayedTyposInExpr(TernaryMiddle);
        LHS = ExprError();
      }

      NextTokPrec = getBinOpPrecedence(Tok.getKind(), GreaterThanIsOperator,
                                       getLangOpts().CPlusPlus11);
    }

    if (!RHS.isInvalid() && RHSIsInitList) {
      if (ThisPrec == prec::Assignment) {
        Diag(OpToken, diag::warn_cxx98_compat_generalized_initializer_lists)
          << Actions.getExprRange(RHS.get());
      } else if (ColonLoc.isValid()) {
        Diag(ColonLoc, diag::err_init_list_bin_op)
          << /*RHS*/1 << ":"
          << Actions.getExprRange(RHS.get());
        LHS = ExprError();
      } else {
        Diag(OpToken, diag::err_init_list_bin_op)
          << /*RHS*/1 << PP.getSpelling(OpToken)
          << Actions.getExprRange(RHS.get());
        LHS = ExprError();
      }
    }

    ExprResult OrigLHS = LHS;
    if (!LHS.isInvalid()) {
      // Combine the LHS and RHS into the LHS (e.g. build AST).
      if (TernaryMiddle.isInvalid()) {
        // If we're using '>>' as an operator within a template
        // argument list (in C++98), suggest the addition of
        // parentheses so that the code remains well-formed in C++0x.
        if (!GreaterThanIsOperator && OpToken.is(tok::greatergreater))
          SuggestParentheses(OpToken.getLocation(),
                             diag::warn_cxx11_right_shift_in_template_arg,
                         SourceRange(Actions.getExprRange(LHS.get()).getBegin(),
                                     Actions.getExprRange(RHS.get()).getEnd()));

        ExprResult BinOp =
            Actions.ActOnBinOp(getCurScope(), OpToken.getLocation(),
                               OpToken.getKind(), LHS.get(), RHS.get());
        if (BinOp.isInvalid())
          BinOp = Actions.CreateRecoveryExpr(LHS.get()->getBeginLoc(),
                                             RHS.get()->getEndLoc(),
                                             {LHS.get(), RHS.get()});

        LHS = BinOp;
      } else {
        ExprResult CondOp = Actions.ActOnConditionalOp(
            OpToken.getLocation(), ColonLoc, LHS.get(), TernaryMiddle.get(),
            RHS.get());
        if (CondOp.isInvalid()) {
          std::vector<clang::Expr *> Args;
          // TernaryMiddle can be null for the GNU conditional expr extension.
          if (TernaryMiddle.get())
            Args = {LHS.get(), TernaryMiddle.get(), RHS.get()};
          else
            Args = {LHS.get(), RHS.get()};
          CondOp = Actions.CreateRecoveryExpr(LHS.get()->getBeginLoc(),
                                              RHS.get()->getEndLoc(), Args);
        }

        LHS = CondOp;
      }
      // In this case, ActOnBinOp or ActOnConditionalOp performed the
      // CorrectDelayedTyposInExpr check.
      if (!getLangOpts().CPlusPlus)
        continue;
    }

    // Ensure potential typos aren't left undiagnosed.
    if (LHS.isInvalid()) {
      Actions.CorrectDelayedTyposInExpr(OrigLHS);
      Actions.CorrectDelayedTyposInExpr(TernaryMiddle);
      Actions.CorrectDelayedTyposInExpr(RHS);
    }
  }
}

/// Parse a cast-expression, unary-expression or primary-expression, based
/// on \p ExprType.
///
/// \p isAddressOfOperand exists because an id-expression that is the
/// operand of address-of gets special treatment due to member pointers.
///
ExprResult Parser::ParseCastExpression(CastParseKind ParseKind,
                                       bool isAddressOfOperand,
                                       TypeCastState isTypeCast,
                                       bool isVectorLiteral,
                                       bool *NotPrimaryExpression) {
  bool NotCastExpr;
  ExprResult Res = ParseCastExpression(ParseKind,
                                       isAddressOfOperand,
                                       NotCastExpr,
                                       isTypeCast,
                                       isVectorLiteral,
                                       NotPrimaryExpression);
  if (NotCastExpr)
    Diag(Tok, diag::err_expected_expression);
  return Res;
}

namespace {
class CastExpressionIdValidator final : public CorrectionCandidateCallback {
 public:
  CastExpressionIdValidator(Token Next, bool AllowTypes, bool AllowNonTypes)
      : NextToken(Next), AllowNonTypes(AllowNonTypes) {
    WantTypeSpecifiers = WantFunctionLikeCasts = AllowTypes;
  }

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    NamedDecl *ND = candidate.getCorrectionDecl();
    if (!ND)
      return candidate.isKeyword();

    if (isa<TypeDecl>(ND))
      return WantTypeSpecifiers;

    if (!AllowNonTypes || !CorrectionCandidateCallback::ValidateCandidate(candidate))
      return false;

    if (!NextToken.isOneOf(tok::equal, tok::arrow, tok::period))
      return true;

    for (auto *C : candidate) {
      NamedDecl *ND = C->getUnderlyingDecl();
      if (isa<ValueDecl>(ND) && !isa<FunctionDecl>(ND))
        return true;
    }
    return false;
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<CastExpressionIdValidator>(*this);
  }

 private:
  Token NextToken;
  bool AllowNonTypes;
};
}

bool Parser::isRevertibleTypeTrait(const IdentifierInfo *II,
                                   tok::TokenKind *Kind) {
  if (RevertibleTypeTraits.empty()) {
// Revertible type trait is a feature for backwards compatibility with older
// standard libraries that declare their own structs with the same name as
// the builtins listed below. New builtins should NOT be added to this list.
#define RTT_JOIN(X, Y) X##Y
#define REVERTIBLE_TYPE_TRAIT(Name)                                            \
  RevertibleTypeTraits[PP.getIdentifierInfo(#Name)] = RTT_JOIN(tok::kw_, Name)

    REVERTIBLE_TYPE_TRAIT(__is_abstract);
    REVERTIBLE_TYPE_TRAIT(__is_aggregate);
    REVERTIBLE_TYPE_TRAIT(__is_arithmetic);
    REVERTIBLE_TYPE_TRAIT(__is_array);
    REVERTIBLE_TYPE_TRAIT(__is_assignable);
    REVERTIBLE_TYPE_TRAIT(__is_base_of);
    REVERTIBLE_TYPE_TRAIT(__is_bounded_array);
    REVERTIBLE_TYPE_TRAIT(__is_class);
    REVERTIBLE_TYPE_TRAIT(__is_complete_type);
    REVERTIBLE_TYPE_TRAIT(__is_compound);
    REVERTIBLE_TYPE_TRAIT(__is_const);
    REVERTIBLE_TYPE_TRAIT(__is_constructible);
    REVERTIBLE_TYPE_TRAIT(__is_convertible);
    REVERTIBLE_TYPE_TRAIT(__is_convertible_to);
    REVERTIBLE_TYPE_TRAIT(__is_destructible);
    REVERTIBLE_TYPE_TRAIT(__is_empty);
    REVERTIBLE_TYPE_TRAIT(__is_enum);
    REVERTIBLE_TYPE_TRAIT(__is_floating_point);
    REVERTIBLE_TYPE_TRAIT(__is_final);
    REVERTIBLE_TYPE_TRAIT(__is_function);
    REVERTIBLE_TYPE_TRAIT(__is_fundamental);
    REVERTIBLE_TYPE_TRAIT(__is_integral);
    REVERTIBLE_TYPE_TRAIT(__is_interface_class);
    REVERTIBLE_TYPE_TRAIT(__is_literal);
    REVERTIBLE_TYPE_TRAIT(__is_lvalue_expr);
    REVERTIBLE_TYPE_TRAIT(__is_lvalue_reference);
    REVERTIBLE_TYPE_TRAIT(__is_member_function_pointer);
    REVERTIBLE_TYPE_TRAIT(__is_member_object_pointer);
    REVERTIBLE_TYPE_TRAIT(__is_member_pointer);
    REVERTIBLE_TYPE_TRAIT(__is_nothrow_assignable);
    REVERTIBLE_TYPE_TRAIT(__is_nothrow_constructible);
    REVERTIBLE_TYPE_TRAIT(__is_nothrow_destructible);
    REVERTIBLE_TYPE_TRAIT(__is_nullptr);
    REVERTIBLE_TYPE_TRAIT(__is_object);
    REVERTIBLE_TYPE_TRAIT(__is_pod);
    REVERTIBLE_TYPE_TRAIT(__is_pointer);
    REVERTIBLE_TYPE_TRAIT(__is_polymorphic);
    REVERTIBLE_TYPE_TRAIT(__is_reference);
    REVERTIBLE_TYPE_TRAIT(__is_referenceable);
    REVERTIBLE_TYPE_TRAIT(__is_rvalue_expr);
    REVERTIBLE_TYPE_TRAIT(__is_rvalue_reference);
    REVERTIBLE_TYPE_TRAIT(__is_same);
    REVERTIBLE_TYPE_TRAIT(__is_scalar);
    REVERTIBLE_TYPE_TRAIT(__is_scoped_enum);
    REVERTIBLE_TYPE_TRAIT(__is_sealed);
    REVERTIBLE_TYPE_TRAIT(__is_signed);
    REVERTIBLE_TYPE_TRAIT(__is_standard_layout);
    REVERTIBLE_TYPE_TRAIT(__is_trivial);
    REVERTIBLE_TYPE_TRAIT(__is_trivially_assignable);
    REVERTIBLE_TYPE_TRAIT(__is_trivially_constructible);
    REVERTIBLE_TYPE_TRAIT(__is_trivially_copyable);
    REVERTIBLE_TYPE_TRAIT(__is_unbounded_array);
    REVERTIBLE_TYPE_TRAIT(__is_union);
    REVERTIBLE_TYPE_TRAIT(__is_unsigned);
    REVERTIBLE_TYPE_TRAIT(__is_void);
    REVERTIBLE_TYPE_TRAIT(__is_volatile);
    REVERTIBLE_TYPE_TRAIT(__reference_binds_to_temporary);
#define TRANSFORM_TYPE_TRAIT_DEF(_, Trait)                                     \
  REVERTIBLE_TYPE_TRAIT(RTT_JOIN(__, Trait));
#include "clang/Basic/TransformTypeTraits.def"
#undef REVERTIBLE_TYPE_TRAIT
#undef RTT_JOIN
  }
  llvm::SmallDenseMap<IdentifierInfo *, tok::TokenKind>::iterator Known =
      RevertibleTypeTraits.find(II);
  if (Known != RevertibleTypeTraits.end()) {
    if (Kind)
      *Kind = Known->second;
    return true;
  }
  return false;
}

ExprResult Parser::ParseBuiltinPtrauthTypeDiscriminator() {
  SourceLocation Loc = ConsumeToken();

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.expectAndConsume())
    return ExprError();

  TypeResult Ty = ParseTypeName();
  if (Ty.isInvalid()) {
    SkipUntil(tok::r_paren, StopAtSemi);
    return ExprError();
  }

  SourceLocation EndLoc = Tok.getLocation();
  T.consumeClose();
  return Actions.ActOnUnaryExprOrTypeTraitExpr(
      Loc, UETT_PtrAuthTypeDiscriminator,
      /*isType=*/true, Ty.get().getAsOpaquePtr(), SourceRange(Loc, EndLoc));
}

/// Parse a cast-expression, or, if \pisUnaryExpression is true, parse
/// a unary-expression.
///
/// \p isAddressOfOperand exists because an id-expression that is the operand
/// of address-of gets special treatment due to member pointers. NotCastExpr
/// is set to true if the token is not the start of a cast-expression, and no
/// diagnostic is emitted in this case and no tokens are consumed.
///
/// \verbatim
///       cast-expression: [C99 6.5.4]
///         unary-expression
///         '(' type-name ')' cast-expression
///
///       unary-expression:  [C99 6.5.3]
///         postfix-expression
///         '++' unary-expression
///         '--' unary-expression
/// [Coro]  'co_await' cast-expression
///         unary-operator cast-expression
///         'sizeof' unary-expression
///         'sizeof' '(' type-name ')'
/// [C++11] 'sizeof' '...' '(' identifier ')'
/// [GNU]   '__alignof' unary-expression
/// [GNU]   '__alignof' '(' type-name ')'
/// [C11]   '_Alignof' '(' type-name ')'
/// [C++11] 'alignof' '(' type-id ')'
/// [GNU]   '&&' identifier
/// [C++11] 'noexcept' '(' expression ')' [C++11 5.3.7]
/// [C++]   new-expression
/// [C++]   delete-expression
///
///       unary-operator: one of
///         '&'  '*'  '+'  '-'  '~'  '!'
/// [GNU]   '__extension__'  '__real'  '__imag'
///
///       primary-expression: [C99 6.5.1]
/// [C99]   identifier
/// [C++]   id-expression
///         constant
///         string-literal
/// [C++]   boolean-literal  [C++ 2.13.5]
/// [C++11] 'nullptr'        [C++11 2.14.7]
/// [C++11] user-defined-literal
///         '(' expression ')'
/// [C11]   generic-selection
/// [C++2a] requires-expression
///         '__func__'        [C99 6.4.2.2]
/// [GNU]   '__FUNCTION__'
/// [MS]    '__FUNCDNAME__'
/// [MS]    'L__FUNCTION__'
/// [MS]    '__FUNCSIG__'
/// [MS]    'L__FUNCSIG__'
/// [GNU]   '__PRETTY_FUNCTION__'
/// [GNU]   '(' compound-statement ')'
/// [GNU]   '__builtin_va_arg' '(' assignment-expression ',' type-name ')'
/// [GNU]   '__builtin_offsetof' '(' type-name ',' offsetof-member-designator')'
/// [GNU]   '__builtin_choose_expr' '(' assign-expr ',' assign-expr ','
///                                     assign-expr ')'
/// [GNU]   '__builtin_FILE' '(' ')'
/// [CLANG] '__builtin_FILE_NAME' '(' ')'
/// [GNU]   '__builtin_FUNCTION' '(' ')'
/// [MS]    '__builtin_FUNCSIG' '(' ')'
/// [GNU]   '__builtin_LINE' '(' ')'
/// [CLANG] '__builtin_COLUMN' '(' ')'
/// [GNU]   '__builtin_source_location' '(' ')'
/// [GNU]   '__builtin_types_compatible_p' '(' type-name ',' type-name ')'
/// [GNU]   '__null'
/// [OBJC]  '[' objc-message-expr ']'
/// [OBJC]  '\@selector' '(' objc-selector-arg ')'
/// [OBJC]  '\@protocol' '(' identifier ')'
/// [OBJC]  '\@encode' '(' type-name ')'
/// [OBJC]  objc-string-literal
/// [C++]   simple-type-specifier '(' expression-list[opt] ')'      [C++ 5.2.3]
/// [C++11] simple-type-specifier braced-init-list                  [C++11 5.2.3]
/// [C++]   typename-specifier '(' expression-list[opt] ')'         [C++ 5.2.3]
/// [C++11] typename-specifier braced-init-list                     [C++11 5.2.3]
/// [C++]   'const_cast' '<' type-name '>' '(' expression ')'       [C++ 5.2p1]
/// [C++]   'dynamic_cast' '<' type-name '>' '(' expression ')'     [C++ 5.2p1]
/// [C++]   'reinterpret_cast' '<' type-name '>' '(' expression ')' [C++ 5.2p1]
/// [C++]   'static_cast' '<' type-name '>' '(' expression ')'      [C++ 5.2p1]
/// [C++]   'typeid' '(' expression ')'                             [C++ 5.2p1]
/// [C++]   'typeid' '(' type-id ')'                                [C++ 5.2p1]
/// [C++]   'this'          [C++ 9.3.2]
/// [G++]   unary-type-trait '(' type-id ')'
/// [G++]   binary-type-trait '(' type-id ',' type-id ')'           [TODO]
/// [EMBT]  array-type-trait '(' type-id ',' integer ')'
/// [clang] '^' block-literal
///
///       constant: [C99 6.4.4]
///         integer-constant
///         floating-constant
///         enumeration-constant -> identifier
///         character-constant
///
///       id-expression: [C++ 5.1]
///                   unqualified-id
///                   qualified-id
///
///       unqualified-id: [C++ 5.1]
///                   identifier
///                   operator-function-id
///                   conversion-function-id
///                   '~' class-name
///                   template-id
///
///       new-expression: [C++ 5.3.4]
///                   '::'[opt] 'new' new-placement[opt] new-type-id
///                                     new-initializer[opt]
///                   '::'[opt] 'new' new-placement[opt] '(' type-id ')'
///                                     new-initializer[opt]
///
///       delete-expression: [C++ 5.3.5]
///                   '::'[opt] 'delete' cast-expression
///                   '::'[opt] 'delete' '[' ']' cast-expression
///
/// [GNU/Embarcadero] unary-type-trait:
///                   '__is_arithmetic'
///                   '__is_floating_point'
///                   '__is_integral'
///                   '__is_lvalue_expr'
///                   '__is_rvalue_expr'
///                   '__is_complete_type'
///                   '__is_void'
///                   '__is_array'
///                   '__is_function'
///                   '__is_reference'
///                   '__is_lvalue_reference'
///                   '__is_rvalue_reference'
///                   '__is_fundamental'
///                   '__is_object'
///                   '__is_scalar'
///                   '__is_compound'
///                   '__is_pointer'
///                   '__is_member_object_pointer'
///                   '__is_member_function_pointer'
///                   '__is_member_pointer'
///                   '__is_const'
///                   '__is_volatile'
///                   '__is_trivial'
///                   '__is_standard_layout'
///                   '__is_signed'
///                   '__is_unsigned'
///
/// [GNU] unary-type-trait:
///                   '__has_nothrow_assign'
///                   '__has_nothrow_copy'
///                   '__has_nothrow_constructor'
///                   '__has_trivial_assign'                  [TODO]
///                   '__has_trivial_copy'                    [TODO]
///                   '__has_trivial_constructor'
///                   '__has_trivial_destructor'
///                   '__has_virtual_destructor'
///                   '__is_abstract'                         [TODO]
///                   '__is_class'
///                   '__is_empty'                            [TODO]
///                   '__is_enum'
///                   '__is_final'
///                   '__is_pod'
///                   '__is_polymorphic'
///                   '__is_sealed'                           [MS]
///                   '__is_trivial'
///                   '__is_union'
///                   '__has_unique_object_representations'
///
/// [Clang] unary-type-trait:
///                   '__is_aggregate'
///                   '__trivially_copyable'
///
///       binary-type-trait:
/// [GNU]             '__is_base_of'
/// [MS]              '__is_convertible_to'
///                   '__is_convertible'
///                   '__is_same'
///
/// [Embarcadero] array-type-trait:
///                   '__array_rank'
///                   '__array_extent'
///
/// [Embarcadero] expression-trait:
///                   '__is_lvalue_expr'
///                   '__is_rvalue_expr'
/// \endverbatim
///
ExprResult Parser::ParseCastExpression(CastParseKind ParseKind,
                                       bool isAddressOfOperand,
                                       bool &NotCastExpr,
                                       TypeCastState isTypeCast,
                                       bool isVectorLiteral,
                                       bool *NotPrimaryExpression) {
  ExprResult Res;
  tok::TokenKind SavedKind = Tok.getKind();
  auto SavedType = PreferredType;
  NotCastExpr = false;

  // Are postfix-expression suffix operators permitted after this
  // cast-expression? If not, and we find some, we'll parse them anyway and
  // diagnose them.
  bool AllowSuffix = true;

  // This handles all of cast-expression, unary-expression, postfix-expression,
  // and primary-expression.  We handle them together like this for efficiency
  // and to simplify handling of an expression starting with a '(' token: which
  // may be one of a parenthesized expression, cast-expression, compound literal
  // expression, or statement expression.
  //
  // If the parsed tokens consist of a primary-expression, the cases below
  // break out of the switch;  at the end we call ParsePostfixExpressionSuffix
  // to handle the postfix expression suffixes.  Cases that cannot be followed
  // by postfix exprs should set AllowSuffix to false.
  switch (SavedKind) {
  case tok::l_paren: {
    // If this expression is limited to being a unary-expression, the paren can
    // not start a cast expression.
    ParenParseOption ParenExprType;
    switch (ParseKind) {
      case CastParseKind::UnaryExprOnly:
        assert(getLangOpts().CPlusPlus && "not possible to get here in C");
        [[fallthrough]];
      case CastParseKind::AnyCastExpr:
        ParenExprType = ParenParseOption::CastExpr;
        break;
      case CastParseKind::PrimaryExprOnly:
        ParenExprType = FoldExpr;
        break;
    }
    ParsedType CastTy;
    SourceLocation RParenLoc;
    Res = ParseParenExpression(ParenExprType, false/*stopIfCastExr*/,
                               isTypeCast == IsTypeCast, CastTy, RParenLoc);

    // FIXME: What should we do if a vector literal is followed by a
    // postfix-expression suffix? Usually postfix operators are permitted on
    // literals.
    if (isVectorLiteral)
      return Res;

    switch (ParenExprType) {
    case SimpleExpr:   break;    // Nothing else to do.
    case CompoundStmt: break;  // Nothing else to do.
    case CompoundLiteral:
      // We parsed '(' type-name ')' '{' ... '}'.  If any suffixes of
      // postfix-expression exist, parse them now.
      break;
    case CastExpr:
      // We have parsed the cast-expression and no postfix-expr pieces are
      // following.
      return Res;
    case FoldExpr:
      // We only parsed a fold-expression. There might be postfix-expr pieces
      // afterwards; parse them now.
      break;
    }

    break;
  }

    // primary-expression
  case tok::numeric_constant:
  case tok::binary_data:
    // constant: integer-constant
    // constant: floating-constant

    Res = Actions.ActOnNumericConstant(Tok, /*UDLScope*/getCurScope());
    ConsumeToken();
    break;

  case tok::kw_true:
  case tok::kw_false:
    Res = ParseCXXBoolLiteral();
    break;

  case tok::kw___objc_yes:
  case tok::kw___objc_no:
    Res = ParseObjCBoolLiteral();
    break;

  case tok::kw_nullptr:
    if (getLangOpts().CPlusPlus)
      Diag(Tok, diag::warn_cxx98_compat_nullptr);
    else
      Diag(Tok, getLangOpts().C23 ? diag::warn_c23_compat_keyword
                                  : diag::ext_c_nullptr) << Tok.getName();

    Res = Actions.ActOnCXXNullPtrLiteral(ConsumeToken());
    break;

  case tok::annot_primary_expr:
  case tok::annot_overload_set:
    Res = getExprAnnotation(Tok);
    if (!Res.isInvalid() && Tok.getKind() == tok::annot_overload_set)
      Res = Actions.ActOnNameClassifiedAsOverloadSet(getCurScope(), Res.get());
    ConsumeAnnotationToken();
    if (!Res.isInvalid() && Tok.is(tok::less))
      checkPotentialAngleBracket(Res);
    break;

  case tok::annot_non_type:
  case tok::annot_non_type_dependent:
  case tok::annot_non_type_undeclared: {
    CXXScopeSpec SS;
    Token Replacement;
    Res = tryParseCXXIdExpression(SS, isAddressOfOperand, Replacement);
    assert(!Res.isUnset() &&
           "should not perform typo correction on annotation token");
    break;
  }

  case tok::annot_embed: {
    injectEmbedTokens();
    return ParseCastExpression(ParseKind, isAddressOfOperand, isTypeCast,
                               isVectorLiteral, NotPrimaryExpression);
  }

  case tok::kw___super:
  case tok::kw_decltype:
    // Annotate the token and tail recurse.
    if (TryAnnotateTypeOrScopeToken())
      return ExprError();
    assert(Tok.isNot(tok::kw_decltype) && Tok.isNot(tok::kw___super));
    return ParseCastExpression(ParseKind, isAddressOfOperand, isTypeCast,
                               isVectorLiteral, NotPrimaryExpression);

  case tok::identifier:
  ParseIdentifier: {    // primary-expression: identifier
                        // unqualified-id: identifier
                        // constant: enumeration-constant
    // Turn a potentially qualified name into a annot_typename or
    // annot_cxxscope if it would be valid.  This handles things like x::y, etc.
    if (getLangOpts().CPlusPlus) {
      // Avoid the unnecessary parse-time lookup in the common case
      // where the syntax forbids a type.
      Token Next = NextToken();

      if (Next.is(tok::ellipsis) && Tok.is(tok::identifier) &&
          GetLookAheadToken(2).is(tok::l_square)) {
        // Annotate the token and tail recurse.
        // If the token is not annotated, then it might be an expression pack
        // indexing
        if (!TryAnnotateTypeOrScopeToken() &&
            Tok.is(tok::annot_pack_indexing_type))
          return ParseCastExpression(ParseKind, isAddressOfOperand, isTypeCast,
                                     isVectorLiteral, NotPrimaryExpression);
      }

      // If this identifier was reverted from a token ID, and the next token
      // is a parenthesis, this is likely to be a use of a type trait. Check
      // those tokens.
      else if (Next.is(tok::l_paren) && Tok.is(tok::identifier) &&
               Tok.getIdentifierInfo()->hasRevertedTokenIDToIdentifier()) {
        IdentifierInfo *II = Tok.getIdentifierInfo();
        tok::TokenKind Kind;
        if (isRevertibleTypeTrait(II, &Kind)) {
          Tok.setKind(Kind);
          return ParseCastExpression(ParseKind, isAddressOfOperand,
                                     NotCastExpr, isTypeCast,
                                     isVectorLiteral, NotPrimaryExpression);
        }
      }

      else if ((!ColonIsSacred && Next.is(tok::colon)) ||
               Next.isOneOf(tok::coloncolon, tok::less, tok::l_paren,
                            tok::l_brace)) {
        // If TryAnnotateTypeOrScopeToken annotates the token, tail recurse.
        if (TryAnnotateTypeOrScopeToken())
          return ExprError();
        if (!Tok.is(tok::identifier))
          return ParseCastExpression(ParseKind, isAddressOfOperand,
                                     NotCastExpr, isTypeCast,
                                     isVectorLiteral,
                                     NotPrimaryExpression);
      }
    }

    // Consume the identifier so that we can see if it is followed by a '(' or
    // '.'.
    IdentifierInfo &II = *Tok.getIdentifierInfo();
    SourceLocation ILoc = ConsumeToken();

    // Support 'Class.property' and 'super.property' notation.
    if (getLangOpts().ObjC && Tok.is(tok::period) &&
        (Actions.getTypeName(II, ILoc, getCurScope()) ||
         // Allow the base to be 'super' if in an objc-method.
         (&II == Ident_super && getCurScope()->isInObjcMethodScope()))) {
      ConsumeToken();

      if (Tok.is(tok::code_completion) && &II != Ident_super) {
        cutOffParsing();
        Actions.CodeCompletion().CodeCompleteObjCClassPropertyRefExpr(
            getCurScope(), II, ILoc, ExprStatementTokLoc == ILoc);
        return ExprError();
      }
      // Allow either an identifier or the keyword 'class' (in C++).
      if (Tok.isNot(tok::identifier) &&
          !(getLangOpts().CPlusPlus && Tok.is(tok::kw_class))) {
        Diag(Tok, diag::err_expected_property_name);
        return ExprError();
      }
      IdentifierInfo &PropertyName = *Tok.getIdentifierInfo();
      SourceLocation PropertyLoc = ConsumeToken();

      Res = Actions.ObjC().ActOnClassPropertyRefExpr(II, PropertyName, ILoc,
                                                     PropertyLoc);
      break;
    }

    // In an Objective-C method, if we have "super" followed by an identifier,
    // the token sequence is ill-formed. However, if there's a ':' or ']' after
    // that identifier, this is probably a message send with a missing open
    // bracket. Treat it as such.
    if (getLangOpts().ObjC && &II == Ident_super && !InMessageExpression &&
        getCurScope()->isInObjcMethodScope() &&
        ((Tok.is(tok::identifier) &&
         (NextToken().is(tok::colon) || NextToken().is(tok::r_square))) ||
         Tok.is(tok::code_completion))) {
      Res = ParseObjCMessageExpressionBody(SourceLocation(), ILoc, nullptr,
                                           nullptr);
      break;
    }

    // If we have an Objective-C class name followed by an identifier
    // and either ':' or ']', this is an Objective-C class message
    // send that's missing the opening '['. Recovery
    // appropriately. Also take this path if we're performing code
    // completion after an Objective-C class name.
    if (getLangOpts().ObjC &&
        ((Tok.is(tok::identifier) && !InMessageExpression) ||
         Tok.is(tok::code_completion))) {
      const Token& Next = NextToken();
      if (Tok.is(tok::code_completion) ||
          Next.is(tok::colon) || Next.is(tok::r_square))
        if (ParsedType Typ = Actions.getTypeName(II, ILoc, getCurScope()))
          if (Typ.get()->isObjCObjectOrInterfaceType()) {
            // Fake up a Declarator to use with ActOnTypeName.
            DeclSpec DS(AttrFactory);
            DS.SetRangeStart(ILoc);
            DS.SetRangeEnd(ILoc);
            const char *PrevSpec = nullptr;
            unsigned DiagID;
            DS.SetTypeSpecType(TST_typename, ILoc, PrevSpec, DiagID, Typ,
                               Actions.getASTContext().getPrintingPolicy());

            Declarator DeclaratorInfo(DS, ParsedAttributesView::none(),
                                      DeclaratorContext::TypeName);
            TypeResult Ty = Actions.ActOnTypeName(DeclaratorInfo);
            if (Ty.isInvalid())
              break;

            Res = ParseObjCMessageExpressionBody(SourceLocation(),
                                                 SourceLocation(),
                                                 Ty.get(), nullptr);
            break;
          }
    }

    // Make sure to pass down the right value for isAddressOfOperand.
    if (isAddressOfOperand && isPostfixExpressionSuffixStart())
      isAddressOfOperand = false;

    // Function designators are allowed to be undeclared (C99 6.5.1p2), so we
    // need to know whether or not this identifier is a function designator or
    // not.
    UnqualifiedId Name;
    CXXScopeSpec ScopeSpec;
    SourceLocation TemplateKWLoc;
    Token Replacement;
    CastExpressionIdValidator Validator(
        /*Next=*/Tok,
        /*AllowTypes=*/isTypeCast != NotTypeCast,
        /*AllowNonTypes=*/isTypeCast != IsTypeCast);
    Validator.IsAddressOfOperand = isAddressOfOperand;
    if (Tok.isOneOf(tok::periodstar, tok::arrowstar)) {
      Validator.WantExpressionKeywords = false;
      Validator.WantRemainingKeywords = false;
    } else {
      Validator.WantRemainingKeywords = Tok.isNot(tok::r_paren);
    }
    Name.setIdentifier(&II, ILoc);
    Res = Actions.ActOnIdExpression(
        getCurScope(), ScopeSpec, TemplateKWLoc, Name, Tok.is(tok::l_paren),
        isAddressOfOperand, &Validator,
        /*IsInlineAsmIdentifier=*/false,
        Tok.is(tok::r_paren) ? nullptr : &Replacement);
    if (!Res.isInvalid() && Res.isUnset()) {
      UnconsumeToken(Replacement);
      return ParseCastExpression(ParseKind, isAddressOfOperand,
                                 NotCastExpr, isTypeCast,
                                 /*isVectorLiteral=*/false,
                                 NotPrimaryExpression);
    }
    Res = tryParseCXXPackIndexingExpression(Res);
    if (!Res.isInvalid() && Tok.is(tok::less))
      checkPotentialAngleBracket(Res);
    break;
  }
  case tok::char_constant:     // constant: character-constant
  case tok::wide_char_constant:
  case tok::utf8_char_constant:
  case tok::utf16_char_constant:
  case tok::utf32_char_constant:
    Res = Actions.ActOnCharacterConstant(Tok, /*UDLScope*/getCurScope());
    ConsumeToken();
    break;
  case tok::kw___func__:       // primary-expression: __func__ [C99 6.4.2.2]
  case tok::kw___FUNCTION__:   // primary-expression: __FUNCTION__ [GNU]
  case tok::kw___FUNCDNAME__:   // primary-expression: __FUNCDNAME__ [MS]
  case tok::kw___FUNCSIG__:     // primary-expression: __FUNCSIG__ [MS]
  case tok::kw_L__FUNCTION__:   // primary-expression: L__FUNCTION__ [MS]
  case tok::kw_L__FUNCSIG__:    // primary-expression: L__FUNCSIG__ [MS]
  case tok::kw___PRETTY_FUNCTION__:  // primary-expression: __P..Y_F..N__ [GNU]
    // Function local predefined macros are represented by PredefinedExpr except
    // when Microsoft extensions are enabled and one of these macros is adjacent
    // to a string literal or another one of these macros.
    if (!(getLangOpts().MicrosoftExt &&
          tokenIsLikeStringLiteral(Tok, getLangOpts()) &&
          tokenIsLikeStringLiteral(NextToken(), getLangOpts()))) {
      Res = Actions.ActOnPredefinedExpr(Tok.getLocation(), SavedKind);
      ConsumeToken();
      break;
    }
    [[fallthrough]]; // treat MS function local macros as concatenable strings
  case tok::string_literal:    // primary-expression: string-literal
  case tok::wide_string_literal:
  case tok::utf8_string_literal:
  case tok::utf16_string_literal:
  case tok::utf32_string_literal:
    Res = ParseStringLiteralExpression(true);
    break;
  case tok::kw__Generic:   // primary-expression: generic-selection [C11 6.5.1]
    Res = ParseGenericSelectionExpression();
    break;
  case tok::kw___builtin_available:
    Res = ParseAvailabilityCheckExpr(Tok.getLocation());
    break;
  case tok::kw___builtin_va_arg:
  case tok::kw___builtin_offsetof:
  case tok::kw___builtin_choose_expr:
  case tok::kw___builtin_astype: // primary-expression: [OCL] as_type()
  case tok::kw___builtin_convertvector:
  case tok::kw___builtin_COLUMN:
  case tok::kw___builtin_FILE:
  case tok::kw___builtin_FILE_NAME:
  case tok::kw___builtin_FUNCTION:
  case tok::kw___builtin_FUNCSIG:
  case tok::kw___builtin_LINE:
  case tok::kw___builtin_source_location:
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    // This parses the complete suffix; we can return early.
    return ParseBuiltinPrimaryExpression();
  case tok::kw___null:
    Res = Actions.ActOnGNUNullExpr(ConsumeToken());
    break;

  case tok::plusplus:      // unary-expression: '++' unary-expression [C99]
  case tok::minusminus: {  // unary-expression: '--' unary-expression [C99]
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    // C++ [expr.unary] has:
    //   unary-expression:
    //     ++ cast-expression
    //     -- cast-expression
    Token SavedTok = Tok;
    ConsumeToken();

    PreferredType.enterUnary(Actions, Tok.getLocation(), SavedTok.getKind(),
                             SavedTok.getLocation());
    // One special case is implicitly handled here: if the preceding tokens are
    // an ambiguous cast expression, such as "(T())++", then we recurse to
    // determine whether the '++' is prefix or postfix.
    Res = ParseCastExpression(getLangOpts().CPlusPlus ?
                                  UnaryExprOnly : AnyCastExpr,
                              /*isAddressOfOperand*/false, NotCastExpr,
                              NotTypeCast);
    if (NotCastExpr) {
      // If we return with NotCastExpr = true, we must not consume any tokens,
      // so put the token back where we found it.
      assert(Res.isInvalid());
      UnconsumeToken(SavedTok);
      return ExprError();
    }
    if (!Res.isInvalid()) {
      Expr *Arg = Res.get();
      Res = Actions.ActOnUnaryOp(getCurScope(), SavedTok.getLocation(),
                                 SavedKind, Arg);
      if (Res.isInvalid())
        Res = Actions.CreateRecoveryExpr(SavedTok.getLocation(),
                                         Arg->getEndLoc(), Arg);
    }
    return Res;
  }
  case tok::amp: {         // unary-expression: '&' cast-expression
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    // Special treatment because of member pointers
    SourceLocation SavedLoc = ConsumeToken();
    PreferredType.enterUnary(Actions, Tok.getLocation(), tok::amp, SavedLoc);

    Res = ParseCastExpression(AnyCastExpr, /*isAddressOfOperand=*/true);
    if (!Res.isInvalid()) {
      Expr *Arg = Res.get();
      Res = Actions.ActOnUnaryOp(getCurScope(), SavedLoc, SavedKind, Arg);
      if (Res.isInvalid())
        Res = Actions.CreateRecoveryExpr(Tok.getLocation(), Arg->getEndLoc(),
                                         Arg);
    }
    return Res;
  }

  case tok::star:          // unary-expression: '*' cast-expression
  case tok::plus:          // unary-expression: '+' cast-expression
  case tok::minus:         // unary-expression: '-' cast-expression
  case tok::tilde:         // unary-expression: '~' cast-expression
  case tok::exclaim:       // unary-expression: '!' cast-expression
  case tok::kw___real:     // unary-expression: '__real' cast-expression [GNU]
  case tok::kw___imag: {   // unary-expression: '__imag' cast-expression [GNU]
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    SourceLocation SavedLoc = ConsumeToken();
    PreferredType.enterUnary(Actions, Tok.getLocation(), SavedKind, SavedLoc);
    Res = ParseCastExpression(AnyCastExpr);
    if (!Res.isInvalid()) {
      Expr *Arg = Res.get();
      Res = Actions.ActOnUnaryOp(getCurScope(), SavedLoc, SavedKind, Arg,
                                 isAddressOfOperand);
      if (Res.isInvalid())
        Res = Actions.CreateRecoveryExpr(SavedLoc, Arg->getEndLoc(), Arg);
    }
    return Res;
  }

  case tok::kw_co_await: {  // unary-expression: 'co_await' cast-expression
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    SourceLocation CoawaitLoc = ConsumeToken();
    Res = ParseCastExpression(AnyCastExpr);
    if (!Res.isInvalid())
      Res = Actions.ActOnCoawaitExpr(getCurScope(), CoawaitLoc, Res.get());
    return Res;
  }

  case tok::kw___extension__:{//unary-expression:'__extension__' cast-expr [GNU]
    // __extension__ silences extension warnings in the subexpression.
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    ExtensionRAIIObject O(Diags);  // Use RAII to do this.
    SourceLocation SavedLoc = ConsumeToken();
    Res = ParseCastExpression(AnyCastExpr);
    if (!Res.isInvalid())
      Res = Actions.ActOnUnaryOp(getCurScope(), SavedLoc, SavedKind, Res.get());
    return Res;
  }
  case tok::kw__Alignof:   // unary-expression: '_Alignof' '(' type-name ')'
    diagnoseUseOfC11Keyword(Tok);
    [[fallthrough]];
  case tok::kw_alignof:    // unary-expression: 'alignof' '(' type-id ')'
  case tok::kw___alignof:  // unary-expression: '__alignof' unary-expression
                           // unary-expression: '__alignof' '(' type-name ')'
  case tok::kw_sizeof:     // unary-expression: 'sizeof' unary-expression
                           // unary-expression: 'sizeof' '(' type-name ')'
  // unary-expression: '__datasizeof' unary-expression
  // unary-expression: '__datasizeof' '(' type-name ')'
  case tok::kw___datasizeof:
  case tok::kw_vec_step:   // unary-expression: OpenCL 'vec_step' expression
  // unary-expression: '__builtin_omp_required_simd_align' '(' type-name ')'
  case tok::kw___builtin_omp_required_simd_align:
  case tok::kw___builtin_vectorelements:
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    AllowSuffix = false;
    Res = ParseUnaryExprOrTypeTraitExpression();
    break;
  case tok::ampamp: {      // unary-expression: '&&' identifier
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    SourceLocation AmpAmpLoc = ConsumeToken();
    if (Tok.isNot(tok::identifier))
      return ExprError(Diag(Tok, diag::err_expected) << tok::identifier);

    if (getCurScope()->getFnParent() == nullptr)
      return ExprError(Diag(Tok, diag::err_address_of_label_outside_fn));

    Diag(AmpAmpLoc, diag::ext_gnu_address_of_label);
    LabelDecl *LD = Actions.LookupOrCreateLabel(Tok.getIdentifierInfo(),
                                                Tok.getLocation());
    Res = Actions.ActOnAddrLabel(AmpAmpLoc, Tok.getLocation(), LD);
    ConsumeToken();
    AllowSuffix = false;
    break;
  }
  case tok::kw_const_cast:
  case tok::kw_dynamic_cast:
  case tok::kw_reinterpret_cast:
  case tok::kw_static_cast:
  case tok::kw_addrspace_cast:
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    Res = ParseCXXCasts();
    break;
  case tok::kw___builtin_bit_cast:
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    Res = ParseBuiltinBitCast();
    break;
  case tok::kw_typeid:
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    Res = ParseCXXTypeid();
    break;
  case tok::kw___uuidof:
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    Res = ParseCXXUuidof();
    break;
  case tok::kw_this:
    Res = ParseCXXThis();
    break;
  case tok::kw___builtin_sycl_unique_stable_name:
    Res = ParseSYCLUniqueStableNameExpression();
    break;

  case tok::annot_typename:
    if (isStartOfObjCClassMessageMissingOpenBracket()) {
      TypeResult Type = getTypeAnnotation(Tok);

      // Fake up a Declarator to use with ActOnTypeName.
      DeclSpec DS(AttrFactory);
      DS.SetRangeStart(Tok.getLocation());
      DS.SetRangeEnd(Tok.getLastLoc());

      const char *PrevSpec = nullptr;
      unsigned DiagID;
      DS.SetTypeSpecType(TST_typename, Tok.getAnnotationEndLoc(),
                         PrevSpec, DiagID, Type,
                         Actions.getASTContext().getPrintingPolicy());

      Declarator DeclaratorInfo(DS, ParsedAttributesView::none(),
                                DeclaratorContext::TypeName);
      TypeResult Ty = Actions.ActOnTypeName(DeclaratorInfo);
      if (Ty.isInvalid())
        break;

      ConsumeAnnotationToken();
      Res = ParseObjCMessageExpressionBody(SourceLocation(), SourceLocation(),
                                           Ty.get(), nullptr);
      break;
    }
    [[fallthrough]];

  case tok::annot_decltype:
  case tok::annot_pack_indexing_type:
  case tok::kw_char:
  case tok::kw_wchar_t:
  case tok::kw_char8_t:
  case tok::kw_char16_t:
  case tok::kw_char32_t:
  case tok::kw_bool:
  case tok::kw_short:
  case tok::kw_int:
  case tok::kw_long:
  case tok::kw___int64:
  case tok::kw___int128:
  case tok::kw__ExtInt:
  case tok::kw__BitInt:
  case tok::kw_signed:
  case tok::kw_unsigned:
  case tok::kw_half:
  case tok::kw_float:
  case tok::kw_double:
  case tok::kw___bf16:
  case tok::kw__Float16:
  case tok::kw___float128:
  case tok::kw___ibm128:
  case tok::kw_void:
  case tok::kw_auto:
  case tok::kw_typename:
  case tok::kw_typeof:
  case tok::kw___vector:
  case tok::kw__Accum:
  case tok::kw__Fract:
  case tok::kw__Sat:
#define GENERIC_IMAGE_TYPE(ImgType, Id) case tok::kw_##ImgType##_t:
#include "clang/Basic/OpenCLImageTypes.def"
  {
    if (!getLangOpts().CPlusPlus) {
      Diag(Tok, diag::err_expected_expression);
      return ExprError();
    }

    // Everything henceforth is a postfix-expression.
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;

    if (SavedKind == tok::kw_typename) {
      // postfix-expression: typename-specifier '(' expression-list[opt] ')'
      //                     typename-specifier braced-init-list
      if (TryAnnotateTypeOrScopeToken())
        return ExprError();

      if (!Tok.isSimpleTypeSpecifier(getLangOpts()))
        // We are trying to parse a simple-type-specifier but might not get such
        // a token after error recovery.
        return ExprError();
    }

    // postfix-expression: simple-type-specifier '(' expression-list[opt] ')'
    //                     simple-type-specifier braced-init-list
    //
    DeclSpec DS(AttrFactory);

    ParseCXXSimpleTypeSpecifier(DS);
    if (Tok.isNot(tok::l_paren) &&
        (!getLangOpts().CPlusPlus11 || Tok.isNot(tok::l_brace)))
      return ExprError(Diag(Tok, diag::err_expected_lparen_after_type)
                         << DS.getSourceRange());

    if (Tok.is(tok::l_brace))
      Diag(Tok, diag::warn_cxx98_compat_generalized_initializer_lists);

    Res = ParseCXXTypeConstructExpression(DS);
    break;
  }

  case tok::annot_cxxscope: { // [C++] id-expression: qualified-id
    // If TryAnnotateTypeOrScopeToken annotates the token, tail recurse.
    // (We can end up in this situation after tentative parsing.)
    if (TryAnnotateTypeOrScopeToken())
      return ExprError();
    if (!Tok.is(tok::annot_cxxscope))
      return ParseCastExpression(ParseKind, isAddressOfOperand, NotCastExpr,
                                 isTypeCast, isVectorLiteral,
                                 NotPrimaryExpression);

    Token Next = NextToken();
    if (Next.is(tok::annot_template_id)) {
      TemplateIdAnnotation *TemplateId = takeTemplateIdAnnotation(Next);
      if (TemplateId->Kind == TNK_Type_template) {
        // We have a qualified template-id that we know refers to a
        // type, translate it into a type and continue parsing as a
        // cast expression.
        CXXScopeSpec SS;
        ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/nullptr,
                                       /*ObjectHasErrors=*/false,
                                       /*EnteringContext=*/false);
        AnnotateTemplateIdTokenAsType(SS, ImplicitTypenameContext::Yes);
        return ParseCastExpression(ParseKind, isAddressOfOperand, NotCastExpr,
                                   isTypeCast, isVectorLiteral,
                                   NotPrimaryExpression);
      }
    }

    // Parse as an id-expression.
    Res = ParseCXXIdExpression(isAddressOfOperand);
    break;
  }

  case tok::annot_template_id: { // [C++]          template-id
    TemplateIdAnnotation *TemplateId = takeTemplateIdAnnotation(Tok);
    if (TemplateId->Kind == TNK_Type_template) {
      // We have a template-id that we know refers to a type,
      // translate it into a type and continue parsing as a cast
      // expression.
      CXXScopeSpec SS;
      AnnotateTemplateIdTokenAsType(SS, ImplicitTypenameContext::Yes);
      return ParseCastExpression(ParseKind, isAddressOfOperand,
                                 NotCastExpr, isTypeCast, isVectorLiteral,
                                 NotPrimaryExpression);
    }

    // Fall through to treat the template-id as an id-expression.
    [[fallthrough]];
  }

  case tok::kw_operator: // [C++] id-expression: operator/conversion-function-id
    Res = ParseCXXIdExpression(isAddressOfOperand);
    break;

  case tok::coloncolon: {
    // ::foo::bar -> global qualified name etc.   If TryAnnotateTypeOrScopeToken
    // annotates the token, tail recurse.
    if (TryAnnotateTypeOrScopeToken())
      return ExprError();
    if (!Tok.is(tok::coloncolon))
      return ParseCastExpression(ParseKind, isAddressOfOperand, isTypeCast,
                                 isVectorLiteral, NotPrimaryExpression);

    // ::new -> [C++] new-expression
    // ::delete -> [C++] delete-expression
    SourceLocation CCLoc = ConsumeToken();
    if (Tok.is(tok::kw_new)) {
      if (NotPrimaryExpression)
        *NotPrimaryExpression = true;
      Res = ParseCXXNewExpression(true, CCLoc);
      AllowSuffix = false;
      break;
    }
    if (Tok.is(tok::kw_delete)) {
      if (NotPrimaryExpression)
        *NotPrimaryExpression = true;
      Res = ParseCXXDeleteExpression(true, CCLoc);
      AllowSuffix = false;
      break;
    }

    // This is not a type name or scope specifier, it is an invalid expression.
    Diag(CCLoc, diag::err_expected_expression);
    return ExprError();
  }

  case tok::kw_new: // [C++] new-expression
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    Res = ParseCXXNewExpression(false, Tok.getLocation());
    AllowSuffix = false;
    break;

  case tok::kw_delete: // [C++] delete-expression
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    Res = ParseCXXDeleteExpression(false, Tok.getLocation());
    AllowSuffix = false;
    break;

  case tok::kw_requires: // [C++2a] requires-expression
    Res = ParseRequiresExpression();
    AllowSuffix = false;
    break;

  case tok::kw_noexcept: { // [C++0x] 'noexcept' '(' expression ')'
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    Diag(Tok, diag::warn_cxx98_compat_noexcept_expr);
    SourceLocation KeyLoc = ConsumeToken();
    BalancedDelimiterTracker T(*this, tok::l_paren);

    if (T.expectAndConsume(diag::err_expected_lparen_after, "noexcept"))
      return ExprError();
    // C++11 [expr.unary.noexcept]p1:
    //   The noexcept operator determines whether the evaluation of its operand,
    //   which is an unevaluated operand, can throw an exception.
    EnterExpressionEvaluationContext Unevaluated(
        Actions, Sema::ExpressionEvaluationContext::Unevaluated);
    Res = ParseExpression();

    T.consumeClose();

    if (!Res.isInvalid())
      Res = Actions.ActOnNoexceptExpr(KeyLoc, T.getOpenLocation(), Res.get(),
                                      T.getCloseLocation());
    AllowSuffix = false;
    break;
  }

#define TYPE_TRAIT(N,Spelling,K) \
  case tok::kw_##Spelling:
#include "clang/Basic/TokenKinds.def"
    Res = ParseTypeTrait();
    break;

  case tok::kw___array_rank:
  case tok::kw___array_extent:
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    Res = ParseArrayTypeTrait();
    break;

  case tok::kw___builtin_ptrauth_type_discriminator:
    return ParseBuiltinPtrauthTypeDiscriminator();

  case tok::kw___is_lvalue_expr:
  case tok::kw___is_rvalue_expr:
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    Res = ParseExpressionTrait();
    break;

  case tok::at: {
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    SourceLocation AtLoc = ConsumeToken();
    return ParseObjCAtExpression(AtLoc);
  }
  case tok::caret:
    Res = ParseBlockLiteralExpression();
    break;
  case tok::code_completion: {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteExpression(
        getCurScope(), PreferredType.get(Tok.getLocation()));
    return ExprError();
  }
#define TRANSFORM_TYPE_TRAIT_DEF(_, Trait) case tok::kw___##Trait:
#include "clang/Basic/TransformTypeTraits.def"
    // HACK: libstdc++ uses some of the transform-type-traits as alias
    // templates, so we need to work around this.
    if (!NextToken().is(tok::l_paren)) {
      Tok.setKind(tok::identifier);
      Diag(Tok, diag::ext_keyword_as_ident)
          << Tok.getIdentifierInfo()->getName() << 0;
      goto ParseIdentifier;
    }
    goto ExpectedExpression;
  case tok::l_square:
    if (getLangOpts().CPlusPlus) {
      if (getLangOpts().ObjC) {
        // C++11 lambda expressions and Objective-C message sends both start with a
        // square bracket.  There are three possibilities here:
        // we have a valid lambda expression, we have an invalid lambda
        // expression, or we have something that doesn't appear to be a lambda.
        // If we're in the last case, we fall back to ParseObjCMessageExpression.
        Res = TryParseLambdaExpression();
        if (!Res.isInvalid() && !Res.get()) {
          // We assume Objective-C++ message expressions are not
          // primary-expressions.
          if (NotPrimaryExpression)
            *NotPrimaryExpression = true;
          Res = ParseObjCMessageExpression();
        }
        break;
      }
      Res = ParseLambdaExpression();
      break;
    }
    if (getLangOpts().ObjC) {
      Res = ParseObjCMessageExpression();
      break;
    }
    [[fallthrough]];
  default:
  ExpectedExpression:
    NotCastExpr = true;
    return ExprError();
  }

  // Check to see whether Res is a function designator only. If it is and we
  // are compiling for OpenCL, we need to return an error as this implies
  // that the address of the function is being taken, which is illegal in CL.

  if (ParseKind == PrimaryExprOnly)
    // This is strictly a primary-expression - no postfix-expr pieces should be
    // parsed.
    return Res;

  if (!AllowSuffix) {
    // FIXME: Don't parse a primary-expression suffix if we encountered a parse
    // error already.
    if (Res.isInvalid())
      return Res;

    switch (Tok.getKind()) {
    case tok::l_square:
    case tok::l_paren:
    case tok::plusplus:
    case tok::minusminus:
      // "expected ';'" or similar is probably the right diagnostic here. Let
      // the caller decide what to do.
      if (Tok.isAtStartOfLine())
        return Res;

      [[fallthrough]];
    case tok::period:
    case tok::arrow:
      break;

    default:
      return Res;
    }

    // This was a unary-expression for which a postfix-expression suffix is
    // not permitted by the grammar (eg, a sizeof expression or
    // new-expression or similar). Diagnose but parse the suffix anyway.
    Diag(Tok.getLocation(), diag::err_postfix_after_unary_requires_parens)
        << Tok.getKind() << Res.get()->getSourceRange()
        << FixItHint::CreateInsertion(Res.get()->getBeginLoc(), "(")
        << FixItHint::CreateInsertion(PP.getLocForEndOfToken(PrevTokLocation),
                                      ")");
  }

  // These can be followed by postfix-expr pieces.
  PreferredType = SavedType;
  Res = ParsePostfixExpressionSuffix(Res);
  if (getLangOpts().OpenCL &&
      !getActions().getOpenCLOptions().isAvailableOption(
          "__cl_clang_function_pointers", getLangOpts()))
    if (Expr *PostfixExpr = Res.get()) {
      QualType Ty = PostfixExpr->getType();
      if (!Ty.isNull() && Ty->isFunctionType()) {
        Diag(PostfixExpr->getExprLoc(),
             diag::err_opencl_taking_function_address_parser);
        return ExprError();
      }
    }

  return Res;
}

/// Once the leading part of a postfix-expression is parsed, this
/// method parses any suffixes that apply.
///
/// \verbatim
///       postfix-expression: [C99 6.5.2]
///         primary-expression
///         postfix-expression '[' expression ']'
///         postfix-expression '[' braced-init-list ']'
///         postfix-expression '[' expression-list [opt] ']'  [C++23 12.4.5]
///         postfix-expression '(' argument-expression-list[opt] ')'
///         postfix-expression '.' identifier
///         postfix-expression '->' identifier
///         postfix-expression '++'
///         postfix-expression '--'
///         '(' type-name ')' '{' initializer-list '}'
///         '(' type-name ')' '{' initializer-list ',' '}'
///
///       argument-expression-list: [C99 6.5.2]
///         argument-expression ...[opt]
///         argument-expression-list ',' assignment-expression ...[opt]
/// \endverbatim
ExprResult
Parser::ParsePostfixExpressionSuffix(ExprResult LHS) {
  // Now that the primary-expression piece of the postfix-expression has been
  // parsed, see if there are any postfix-expression pieces here.
  SourceLocation Loc;
  auto SavedType = PreferredType;
  while (true) {
    // Each iteration relies on preferred type for the whole expression.
    PreferredType = SavedType;
    switch (Tok.getKind()) {
    case tok::code_completion:
      if (InMessageExpression)
        return LHS;

      cutOffParsing();
      Actions.CodeCompletion().CodeCompletePostfixExpression(
          getCurScope(), LHS, PreferredType.get(Tok.getLocation()));
      return ExprError();

    case tok::identifier:
      // If we see identifier: after an expression, and we're not already in a
      // message send, then this is probably a message send with a missing
      // opening bracket '['.
      if (getLangOpts().ObjC && !InMessageExpression &&
          (NextToken().is(tok::colon) || NextToken().is(tok::r_square))) {
        LHS = ParseObjCMessageExpressionBody(SourceLocation(), SourceLocation(),
                                             nullptr, LHS.get());
        break;
      }
      // Fall through; this isn't a message send.
      [[fallthrough]];

    default:  // Not a postfix-expression suffix.
      return LHS;
    case tok::l_square: {  // postfix-expression: p-e '[' expression ']'
      // If we have a array postfix expression that starts on a new line and
      // Objective-C is enabled, it is highly likely that the user forgot a
      // semicolon after the base expression and that the array postfix-expr is
      // actually another message send.  In this case, do some look-ahead to see
      // if the contents of the square brackets are obviously not a valid
      // expression and recover by pretending there is no suffix.
      if (getLangOpts().ObjC && Tok.isAtStartOfLine() &&
          isSimpleObjCMessageExpression())
        return LHS;

      // Reject array indices starting with a lambda-expression. '[[' is
      // reserved for attributes.
      if (CheckProhibitedCXX11Attribute()) {
        (void)Actions.CorrectDelayedTyposInExpr(LHS);
        return ExprError();
      }
      BalancedDelimiterTracker T(*this, tok::l_square);
      T.consumeOpen();
      Loc = T.getOpenLocation();
      ExprResult Length, Stride;
      SourceLocation ColonLocFirst, ColonLocSecond;
      ExprVector ArgExprs;
      bool HasError = false;
      PreferredType.enterSubscript(Actions, Tok.getLocation(), LHS.get());

      // We try to parse a list of indexes in all language mode first
      // and, in we find 0 or one index, we try to parse an OpenMP/OpenACC array
      // section. This allow us to support C++23 multi dimensional subscript and
      // OpenMP/OpenACC sections in the same language mode.
      if ((!getLangOpts().OpenMP && !AllowOpenACCArraySections) ||
          Tok.isNot(tok::colon)) {
        if (!getLangOpts().CPlusPlus23) {
          ExprResult Idx;
          if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
            Diag(Tok, diag::warn_cxx98_compat_generalized_initializer_lists);
            Idx = ParseBraceInitializer();
          } else {
            Idx = ParseExpression(); // May be a comma expression
          }
          LHS = Actions.CorrectDelayedTyposInExpr(LHS);
          Idx = Actions.CorrectDelayedTyposInExpr(Idx);
          if (Idx.isInvalid()) {
            HasError = true;
          } else {
            ArgExprs.push_back(Idx.get());
          }
        } else if (Tok.isNot(tok::r_square)) {
          if (ParseExpressionList(ArgExprs)) {
            LHS = Actions.CorrectDelayedTyposInExpr(LHS);
            HasError = true;
          }
        }
      }

      // Handle OpenACC first, since 'AllowOpenACCArraySections' is only enabled
      // when actively parsing a 'var' in a 'var-list' during clause/'cache'
      // parsing, so it is the most specific, and best allows us to handle
      // OpenACC and OpenMP at the same time.
      if (ArgExprs.size() <= 1 && AllowOpenACCArraySections) {
        ColonProtectionRAIIObject RAII(*this);
        if (Tok.is(tok::colon)) {
          // Consume ':'
          ColonLocFirst = ConsumeToken();
          if (Tok.isNot(tok::r_square))
            Length = Actions.CorrectDelayedTyposInExpr(ParseExpression());
        }
      } else if (ArgExprs.size() <= 1 && getLangOpts().OpenMP) {
        ColonProtectionRAIIObject RAII(*this);
        if (Tok.is(tok::colon)) {
          // Consume ':'
          ColonLocFirst = ConsumeToken();
          if (Tok.isNot(tok::r_square) &&
              (getLangOpts().OpenMP < 50 ||
               ((Tok.isNot(tok::colon) && getLangOpts().OpenMP >= 50)))) {
            Length = ParseExpression();
            Length = Actions.CorrectDelayedTyposInExpr(Length);
          }
        }
        if (getLangOpts().OpenMP >= 50 &&
            (OMPClauseKind == llvm::omp::Clause::OMPC_to ||
             OMPClauseKind == llvm::omp::Clause::OMPC_from) &&
            Tok.is(tok::colon)) {
          // Consume ':'
          ColonLocSecond = ConsumeToken();
          if (Tok.isNot(tok::r_square)) {
            Stride = ParseExpression();
          }
        }
      }

      SourceLocation RLoc = Tok.getLocation();
      LHS = Actions.CorrectDelayedTyposInExpr(LHS);

      if (!LHS.isInvalid() && !HasError && !Length.isInvalid() &&
          !Stride.isInvalid() && Tok.is(tok::r_square)) {
        if (ColonLocFirst.isValid() || ColonLocSecond.isValid()) {
          // Like above, AllowOpenACCArraySections is 'more specific' and only
          // enabled when actively parsing a 'var' in a 'var-list' during
          // clause/'cache' construct parsing, so it is more specific. So we
          // should do it first, so that the correct node gets created.
          if (AllowOpenACCArraySections) {
            assert(!Stride.isUsable() && !ColonLocSecond.isValid() &&
                   "Stride/second colon not allowed for OpenACC");
            LHS = Actions.OpenACC().ActOnArraySectionExpr(
                LHS.get(), Loc, ArgExprs.empty() ? nullptr : ArgExprs[0],
                ColonLocFirst, Length.get(), RLoc);
          } else {
            LHS = Actions.OpenMP().ActOnOMPArraySectionExpr(
                LHS.get(), Loc, ArgExprs.empty() ? nullptr : ArgExprs[0],
                ColonLocFirst, ColonLocSecond, Length.get(), Stride.get(),
                RLoc);
          }
        } else {
          LHS = Actions.ActOnArraySubscriptExpr(getCurScope(), LHS.get(), Loc,
                                                ArgExprs, RLoc);
        }
      } else {
        LHS = ExprError();
      }

      // Match the ']'.
      T.consumeClose();
      break;
    }

    case tok::l_paren:         // p-e: p-e '(' argument-expression-list[opt] ')'
    case tok::lesslessless: {  // p-e: p-e '<<<' argument-expression-list '>>>'
                               //   '(' argument-expression-list[opt] ')'
      tok::TokenKind OpKind = Tok.getKind();
      InMessageExpressionRAIIObject InMessage(*this, false);

      Expr *ExecConfig = nullptr;

      BalancedDelimiterTracker PT(*this, tok::l_paren);

      if (OpKind == tok::lesslessless) {
        ExprVector ExecConfigExprs;
        SourceLocation OpenLoc = ConsumeToken();

        if (ParseSimpleExpressionList(ExecConfigExprs)) {
          (void)Actions.CorrectDelayedTyposInExpr(LHS);
          LHS = ExprError();
        }

        SourceLocation CloseLoc;
        if (TryConsumeToken(tok::greatergreatergreater, CloseLoc)) {
        } else if (LHS.isInvalid()) {
          SkipUntil(tok::greatergreatergreater, StopAtSemi);
        } else {
          // There was an error closing the brackets
          Diag(Tok, diag::err_expected) << tok::greatergreatergreater;
          Diag(OpenLoc, diag::note_matching) << tok::lesslessless;
          SkipUntil(tok::greatergreatergreater, StopAtSemi);
          LHS = ExprError();
        }

        if (!LHS.isInvalid()) {
          if (ExpectAndConsume(tok::l_paren))
            LHS = ExprError();
          else
            Loc = PrevTokLocation;
        }

        if (!LHS.isInvalid()) {
          ExprResult ECResult = Actions.CUDA().ActOnExecConfigExpr(
              getCurScope(), OpenLoc, ExecConfigExprs, CloseLoc);
          if (ECResult.isInvalid())
            LHS = ExprError();
          else
            ExecConfig = ECResult.get();
        }
      } else {
        PT.consumeOpen();
        Loc = PT.getOpenLocation();
      }

      ExprVector ArgExprs;
      auto RunSignatureHelp = [&]() -> QualType {
        QualType PreferredType =
            Actions.CodeCompletion().ProduceCallSignatureHelp(
                LHS.get(), ArgExprs, PT.getOpenLocation());
        CalledSignatureHelp = true;
        return PreferredType;
      };
      if (OpKind == tok::l_paren || !LHS.isInvalid()) {
        if (Tok.isNot(tok::r_paren)) {
          if (ParseExpressionList(ArgExprs, [&] {
                PreferredType.enterFunctionArgument(Tok.getLocation(),
                                                    RunSignatureHelp);
              })) {
            (void)Actions.CorrectDelayedTyposInExpr(LHS);
            // If we got an error when parsing expression list, we don't call
            // the CodeCompleteCall handler inside the parser. So call it here
            // to make sure we get overload suggestions even when we are in the
            // middle of a parameter.
            if (PP.isCodeCompletionReached() && !CalledSignatureHelp)
              RunSignatureHelp();
            LHS = ExprError();
          } else if (LHS.isInvalid()) {
            for (auto &E : ArgExprs)
              Actions.CorrectDelayedTyposInExpr(E);
          }
        }
      }

      // Match the ')'.
      if (LHS.isInvalid()) {
        SkipUntil(tok::r_paren, StopAtSemi);
      } else if (Tok.isNot(tok::r_paren)) {
        bool HadDelayedTypo = false;
        if (Actions.CorrectDelayedTyposInExpr(LHS).get() != LHS.get())
          HadDelayedTypo = true;
        for (auto &E : ArgExprs)
          if (Actions.CorrectDelayedTyposInExpr(E).get() != E)
            HadDelayedTypo = true;
        // If there were delayed typos in the LHS or ArgExprs, call SkipUntil
        // instead of PT.consumeClose() to avoid emitting extra diagnostics for
        // the unmatched l_paren.
        if (HadDelayedTypo)
          SkipUntil(tok::r_paren, StopAtSemi);
        else
          PT.consumeClose();
        LHS = ExprError();
      } else {
        Expr *Fn = LHS.get();
        SourceLocation RParLoc = Tok.getLocation();
        LHS = Actions.ActOnCallExpr(getCurScope(), Fn, Loc, ArgExprs, RParLoc,
                                    ExecConfig);
        if (LHS.isInvalid()) {
          ArgExprs.insert(ArgExprs.begin(), Fn);
          LHS =
              Actions.CreateRecoveryExpr(Fn->getBeginLoc(), RParLoc, ArgExprs);
        }
        PT.consumeClose();
      }

      break;
    }
    case tok::arrow:
    case tok::period: {
      // postfix-expression: p-e '->' template[opt] id-expression
      // postfix-expression: p-e '.' template[opt] id-expression
      tok::TokenKind OpKind = Tok.getKind();
      SourceLocation OpLoc = ConsumeToken();  // Eat the "." or "->" token.

      CXXScopeSpec SS;
      ParsedType ObjectType;
      bool MayBePseudoDestructor = false;
      Expr* OrigLHS = !LHS.isInvalid() ? LHS.get() : nullptr;

      PreferredType.enterMemAccess(Actions, Tok.getLocation(), OrigLHS);

      if (getLangOpts().CPlusPlus && !LHS.isInvalid()) {
        Expr *Base = OrigLHS;
        const Type* BaseType = Base->getType().getTypePtrOrNull();
        if (BaseType && Tok.is(tok::l_paren) &&
            (BaseType->isFunctionType() ||
             BaseType->isSpecificPlaceholderType(BuiltinType::BoundMember))) {
          Diag(OpLoc, diag::err_function_is_not_record)
              << OpKind << Base->getSourceRange()
              << FixItHint::CreateRemoval(OpLoc);
          return ParsePostfixExpressionSuffix(Base);
        }

        LHS = Actions.ActOnStartCXXMemberReference(getCurScope(), Base, OpLoc,
                                                   OpKind, ObjectType,
                                                   MayBePseudoDestructor);
        if (LHS.isInvalid()) {
          // Clang will try to perform expression based completion as a
          // fallback, which is confusing in case of member references. So we
          // stop here without any completions.
          if (Tok.is(tok::code_completion)) {
            cutOffParsing();
            return ExprError();
          }
          break;
        }
        ParseOptionalCXXScopeSpecifier(
            SS, ObjectType, LHS.get() && LHS.get()->containsErrors(),
            /*EnteringContext=*/false, &MayBePseudoDestructor);
        if (SS.isNotEmpty())
          ObjectType = nullptr;
      }

      if (Tok.is(tok::code_completion)) {
        tok::TokenKind CorrectedOpKind =
            OpKind == tok::arrow ? tok::period : tok::arrow;
        ExprResult CorrectedLHS(/*Invalid=*/true);
        if (getLangOpts().CPlusPlus && OrigLHS) {
          // FIXME: Creating a TentativeAnalysisScope from outside Sema is a
          // hack.
          Sema::TentativeAnalysisScope Trap(Actions);
          CorrectedLHS = Actions.ActOnStartCXXMemberReference(
              getCurScope(), OrigLHS, OpLoc, CorrectedOpKind, ObjectType,
              MayBePseudoDestructor);
        }

        Expr *Base = LHS.get();
        Expr *CorrectedBase = CorrectedLHS.get();
        if (!CorrectedBase && !getLangOpts().CPlusPlus)
          CorrectedBase = Base;

        // Code completion for a member access expression.
        cutOffParsing();
        Actions.CodeCompletion().CodeCompleteMemberReferenceExpr(
            getCurScope(), Base, CorrectedBase, OpLoc, OpKind == tok::arrow,
            Base && ExprStatementTokLoc == Base->getBeginLoc(),
            PreferredType.get(Tok.getLocation()));

        return ExprError();
      }

      if (MayBePseudoDestructor && !LHS.isInvalid()) {
        LHS = ParseCXXPseudoDestructor(LHS.get(), OpLoc, OpKind, SS,
                                       ObjectType);
        break;
      }

      // Either the action has told us that this cannot be a
      // pseudo-destructor expression (based on the type of base
      // expression), or we didn't see a '~' in the right place. We
      // can still parse a destructor name here, but in that case it
      // names a real destructor.
      // Allow explicit constructor calls in Microsoft mode.
      // FIXME: Add support for explicit call of template constructor.
      SourceLocation TemplateKWLoc;
      UnqualifiedId Name;
      if (getLangOpts().ObjC && OpKind == tok::period &&
          Tok.is(tok::kw_class)) {
        // Objective-C++:
        //   After a '.' in a member access expression, treat the keyword
        //   'class' as if it were an identifier.
        //
        // This hack allows property access to the 'class' method because it is
        // such a common method name. For other C++ keywords that are
        // Objective-C method names, one must use the message send syntax.
        IdentifierInfo *Id = Tok.getIdentifierInfo();
        SourceLocation Loc = ConsumeToken();
        Name.setIdentifier(Id, Loc);
      } else if (ParseUnqualifiedId(
                     SS, ObjectType, LHS.get() && LHS.get()->containsErrors(),
                     /*EnteringContext=*/false,
                     /*AllowDestructorName=*/true,
                     /*AllowConstructorName=*/
                     getLangOpts().MicrosoftExt && SS.isNotEmpty(),
                     /*AllowDeductionGuide=*/false, &TemplateKWLoc, Name)) {
        (void)Actions.CorrectDelayedTyposInExpr(LHS);
        LHS = ExprError();
      }

      if (!LHS.isInvalid())
        LHS = Actions.ActOnMemberAccessExpr(getCurScope(), LHS.get(), OpLoc,
                                            OpKind, SS, TemplateKWLoc, Name,
                                 CurParsedObjCImpl ? CurParsedObjCImpl->Dcl
                                                   : nullptr);
      if (!LHS.isInvalid()) {
        if (Tok.is(tok::less))
          checkPotentialAngleBracket(LHS);
      } else if (OrigLHS && Name.isValid()) {
        // Preserve the LHS if the RHS is an invalid member.
        LHS = Actions.CreateRecoveryExpr(OrigLHS->getBeginLoc(),
                                         Name.getEndLoc(), {OrigLHS});
      }
      break;
    }
    case tok::plusplus:    // postfix-expression: postfix-expression '++'
    case tok::minusminus:  // postfix-expression: postfix-expression '--'
      if (!LHS.isInvalid()) {
        Expr *Arg = LHS.get();
        LHS = Actions.ActOnPostfixUnaryOp(getCurScope(), Tok.getLocation(),
                                          Tok.getKind(), Arg);
        if (LHS.isInvalid())
          LHS = Actions.CreateRecoveryExpr(Arg->getBeginLoc(),
                                           Tok.getLocation(), Arg);
      }
      ConsumeToken();
      break;
    }
  }
}

/// ParseExprAfterUnaryExprOrTypeTrait - We parsed a typeof/sizeof/alignof/
/// vec_step and we are at the start of an expression or a parenthesized
/// type-id. OpTok is the operand token (typeof/sizeof/alignof). Returns the
/// expression (isCastExpr == false) or the type (isCastExpr == true).
///
/// \verbatim
///       unary-expression:  [C99 6.5.3]
///         'sizeof' unary-expression
///         'sizeof' '(' type-name ')'
/// [Clang] '__datasizeof' unary-expression
/// [Clang] '__datasizeof' '(' type-name ')'
/// [GNU]   '__alignof' unary-expression
/// [GNU]   '__alignof' '(' type-name ')'
/// [C11]   '_Alignof' '(' type-name ')'
/// [C++0x] 'alignof' '(' type-id ')'
///
/// [GNU]   typeof-specifier:
///           typeof ( expressions )
///           typeof ( type-name )
/// [GNU/C++] typeof unary-expression
/// [C23]   typeof-specifier:
///           typeof '(' typeof-specifier-argument ')'
///           typeof_unqual '(' typeof-specifier-argument ')'
///
///         typeof-specifier-argument:
///           expression
///           type-name
///
/// [OpenCL 1.1 6.11.12] vec_step built-in function:
///           vec_step ( expressions )
///           vec_step ( type-name )
/// \endverbatim
ExprResult
Parser::ParseExprAfterUnaryExprOrTypeTrait(const Token &OpTok,
                                           bool &isCastExpr,
                                           ParsedType &CastTy,
                                           SourceRange &CastRange) {

  assert(OpTok.isOneOf(tok::kw_typeof, tok::kw_typeof_unqual, tok::kw_sizeof,
                       tok::kw___datasizeof, tok::kw___alignof, tok::kw_alignof,
                       tok::kw__Alignof, tok::kw_vec_step,
                       tok::kw___builtin_omp_required_simd_align,
                       tok::kw___builtin_vectorelements) &&
         "Not a typeof/sizeof/alignof/vec_step expression!");

  ExprResult Operand;

  // If the operand doesn't start with an '(', it must be an expression.
  if (Tok.isNot(tok::l_paren)) {
    // If construct allows a form without parenthesis, user may forget to put
    // pathenthesis around type name.
    if (OpTok.isOneOf(tok::kw_sizeof, tok::kw___datasizeof, tok::kw___alignof,
                      tok::kw_alignof, tok::kw__Alignof)) {
      if (isTypeIdUnambiguously()) {
        DeclSpec DS(AttrFactory);
        ParseSpecifierQualifierList(DS);
        Declarator DeclaratorInfo(DS, ParsedAttributesView::none(),
                                  DeclaratorContext::TypeName);
        ParseDeclarator(DeclaratorInfo);

        SourceLocation LParenLoc = PP.getLocForEndOfToken(OpTok.getLocation());
        SourceLocation RParenLoc = PP.getLocForEndOfToken(PrevTokLocation);
        if (LParenLoc.isInvalid() || RParenLoc.isInvalid()) {
          Diag(OpTok.getLocation(),
               diag::err_expected_parentheses_around_typename)
              << OpTok.getName();
        } else {
          Diag(LParenLoc, diag::err_expected_parentheses_around_typename)
              << OpTok.getName() << FixItHint::CreateInsertion(LParenLoc, "(")
              << FixItHint::CreateInsertion(RParenLoc, ")");
        }
        isCastExpr = true;
        return ExprEmpty();
      }
    }

    isCastExpr = false;
    if (OpTok.isOneOf(tok::kw_typeof, tok::kw_typeof_unqual) &&
        !getLangOpts().CPlusPlus) {
      Diag(Tok, diag::err_expected_after) << OpTok.getIdentifierInfo()
                                          << tok::l_paren;
      return ExprError();
    }

    Operand = ParseCastExpression(UnaryExprOnly);
  } else {
    // If it starts with a '(', we know that it is either a parenthesized
    // type-name, or it is a unary-expression that starts with a compound
    // literal, or starts with a primary-expression that is a parenthesized
    // expression.
    ParenParseOption ExprType = CastExpr;
    SourceLocation LParenLoc = Tok.getLocation(), RParenLoc;

    Operand = ParseParenExpression(ExprType, true/*stopIfCastExpr*/,
                                   false, CastTy, RParenLoc);
    CastRange = SourceRange(LParenLoc, RParenLoc);

    // If ParseParenExpression parsed a '(typename)' sequence only, then this is
    // a type.
    if (ExprType == CastExpr) {
      isCastExpr = true;
      return ExprEmpty();
    }

    if (getLangOpts().CPlusPlus ||
        !OpTok.isOneOf(tok::kw_typeof, tok::kw_typeof_unqual)) {
      // GNU typeof in C requires the expression to be parenthesized. Not so for
      // sizeof/alignof or in C++. Therefore, the parenthesized expression is
      // the start of a unary-expression, but doesn't include any postfix
      // pieces. Parse these now if present.
      if (!Operand.isInvalid())
        Operand = ParsePostfixExpressionSuffix(Operand.get());
    }
  }

  // If we get here, the operand to the typeof/sizeof/alignof was an expression.
  isCastExpr = false;
  return Operand;
}

/// Parse a __builtin_sycl_unique_stable_name expression.  Accepts a type-id as
/// a parameter.
ExprResult Parser::ParseSYCLUniqueStableNameExpression() {
  assert(Tok.is(tok::kw___builtin_sycl_unique_stable_name) &&
         "Not __builtin_sycl_unique_stable_name");

  SourceLocation OpLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, tok::l_paren);

  // __builtin_sycl_unique_stable_name expressions are always parenthesized.
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         "__builtin_sycl_unique_stable_name"))
    return ExprError();

  TypeResult Ty = ParseTypeName();

  if (Ty.isInvalid()) {
    T.skipToEnd();
    return ExprError();
  }

  if (T.consumeClose())
    return ExprError();

  return Actions.SYCL().ActOnUniqueStableNameExpr(
      OpLoc, T.getOpenLocation(), T.getCloseLocation(), Ty.get());
}

/// Parse a sizeof or alignof expression.
///
/// \verbatim
///       unary-expression:  [C99 6.5.3]
///         'sizeof' unary-expression
///         'sizeof' '(' type-name ')'
/// [C++11] 'sizeof' '...' '(' identifier ')'
/// [Clang] '__datasizeof' unary-expression
/// [Clang] '__datasizeof' '(' type-name ')'
/// [GNU]   '__alignof' unary-expression
/// [GNU]   '__alignof' '(' type-name ')'
/// [C11]   '_Alignof' '(' type-name ')'
/// [C++11] 'alignof' '(' type-id ')'
/// \endverbatim
ExprResult Parser::ParseUnaryExprOrTypeTraitExpression() {
  assert(Tok.isOneOf(tok::kw_sizeof, tok::kw___datasizeof, tok::kw___alignof,
                     tok::kw_alignof, tok::kw__Alignof, tok::kw_vec_step,
                     tok::kw___builtin_omp_required_simd_align,
                     tok::kw___builtin_vectorelements) &&
         "Not a sizeof/alignof/vec_step expression!");
  Token OpTok = Tok;
  ConsumeToken();

  // [C++11] 'sizeof' '...' '(' identifier ')'
  if (Tok.is(tok::ellipsis) && OpTok.is(tok::kw_sizeof)) {
    SourceLocation EllipsisLoc = ConsumeToken();
    SourceLocation LParenLoc, RParenLoc;
    IdentifierInfo *Name = nullptr;
    SourceLocation NameLoc;
    if (Tok.is(tok::l_paren)) {
      BalancedDelimiterTracker T(*this, tok::l_paren);
      T.consumeOpen();
      LParenLoc = T.getOpenLocation();
      if (Tok.is(tok::identifier)) {
        Name = Tok.getIdentifierInfo();
        NameLoc = ConsumeToken();
        T.consumeClose();
        RParenLoc = T.getCloseLocation();
        if (RParenLoc.isInvalid())
          RParenLoc = PP.getLocForEndOfToken(NameLoc);
      } else {
        Diag(Tok, diag::err_expected_parameter_pack);
        SkipUntil(tok::r_paren, StopAtSemi);
      }
    } else if (Tok.is(tok::identifier)) {
      Name = Tok.getIdentifierInfo();
      NameLoc = ConsumeToken();
      LParenLoc = PP.getLocForEndOfToken(EllipsisLoc);
      RParenLoc = PP.getLocForEndOfToken(NameLoc);
      Diag(LParenLoc, diag::err_paren_sizeof_parameter_pack)
        << Name
        << FixItHint::CreateInsertion(LParenLoc, "(")
        << FixItHint::CreateInsertion(RParenLoc, ")");
    } else {
      Diag(Tok, diag::err_sizeof_parameter_pack);
    }

    if (!Name)
      return ExprError();

    EnterExpressionEvaluationContext Unevaluated(
        Actions, Sema::ExpressionEvaluationContext::Unevaluated,
        Sema::ReuseLambdaContextDecl);

    return Actions.ActOnSizeofParameterPackExpr(getCurScope(),
                                                OpTok.getLocation(),
                                                *Name, NameLoc,
                                                RParenLoc);
  }

  if (getLangOpts().CPlusPlus &&
      OpTok.isOneOf(tok::kw_alignof, tok::kw__Alignof))
    Diag(OpTok, diag::warn_cxx98_compat_alignof);
  else if (getLangOpts().C23 && OpTok.is(tok::kw_alignof))
    Diag(OpTok, diag::warn_c23_compat_keyword) << OpTok.getName();

  EnterExpressionEvaluationContext Unevaluated(
      Actions, Sema::ExpressionEvaluationContext::Unevaluated,
      Sema::ReuseLambdaContextDecl);

  bool isCastExpr;
  ParsedType CastTy;
  SourceRange CastRange;
  ExprResult Operand = ParseExprAfterUnaryExprOrTypeTrait(OpTok,
                                                          isCastExpr,
                                                          CastTy,
                                                          CastRange);

  UnaryExprOrTypeTrait ExprKind = UETT_SizeOf;
  switch (OpTok.getKind()) {
  case tok::kw_alignof:
  case tok::kw__Alignof:
    ExprKind = UETT_AlignOf;
    break;
  case tok::kw___alignof:
    ExprKind = UETT_PreferredAlignOf;
    break;
  case tok::kw_vec_step:
    ExprKind = UETT_VecStep;
    break;
  case tok::kw___builtin_omp_required_simd_align:
    ExprKind = UETT_OpenMPRequiredSimdAlign;
    break;
  case tok::kw___datasizeof:
    ExprKind = UETT_DataSizeOf;
    break;
  case tok::kw___builtin_vectorelements:
    ExprKind = UETT_VectorElements;
    break;
  default:
    break;
  }

  if (isCastExpr)
    return Actions.ActOnUnaryExprOrTypeTraitExpr(OpTok.getLocation(),
                                                 ExprKind,
                                                 /*IsType=*/true,
                                                 CastTy.getAsOpaquePtr(),
                                                 CastRange);

  if (OpTok.isOneOf(tok::kw_alignof, tok::kw__Alignof))
    Diag(OpTok, diag::ext_alignof_expr) << OpTok.getIdentifierInfo();

  // If we get here, the operand to the sizeof/alignof was an expression.
  if (!Operand.isInvalid())
    Operand = Actions.ActOnUnaryExprOrTypeTraitExpr(OpTok.getLocation(),
                                                    ExprKind,
                                                    /*IsType=*/false,
                                                    Operand.get(),
                                                    CastRange);
  return Operand;
}

/// ParseBuiltinPrimaryExpression
///
/// \verbatim
///       primary-expression: [C99 6.5.1]
/// [GNU]   '__builtin_va_arg' '(' assignment-expression ',' type-name ')'
/// [GNU]   '__builtin_offsetof' '(' type-name ',' offsetof-member-designator')'
/// [GNU]   '__builtin_choose_expr' '(' assign-expr ',' assign-expr ','
///                                     assign-expr ')'
/// [GNU]   '__builtin_types_compatible_p' '(' type-name ',' type-name ')'
/// [GNU]   '__builtin_FILE' '(' ')'
/// [CLANG] '__builtin_FILE_NAME' '(' ')'
/// [GNU]   '__builtin_FUNCTION' '(' ')'
/// [MS]    '__builtin_FUNCSIG' '(' ')'
/// [GNU]   '__builtin_LINE' '(' ')'
/// [CLANG] '__builtin_COLUMN' '(' ')'
/// [GNU]   '__builtin_source_location' '(' ')'
/// [OCL]   '__builtin_astype' '(' assignment-expression ',' type-name ')'
///
/// [GNU] offsetof-member-designator:
/// [GNU]   identifier
/// [GNU]   offsetof-member-designator '.' identifier
/// [GNU]   offsetof-member-designator '[' expression ']'
/// \endverbatim
ExprResult Parser::ParseBuiltinPrimaryExpression() {
  ExprResult Res;
  const IdentifierInfo *BuiltinII = Tok.getIdentifierInfo();

  tok::TokenKind T = Tok.getKind();
  SourceLocation StartLoc = ConsumeToken();   // Eat the builtin identifier.

  // All of these start with an open paren.
  if (Tok.isNot(tok::l_paren))
    return ExprError(Diag(Tok, diag::err_expected_after) << BuiltinII
                                                         << tok::l_paren);

  BalancedDelimiterTracker PT(*this, tok::l_paren);
  PT.consumeOpen();

  // TODO: Build AST.

  switch (T) {
  default: llvm_unreachable("Not a builtin primary expression!");
  case tok::kw___builtin_va_arg: {
    ExprResult Expr(ParseAssignmentExpression());

    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      Expr = ExprError();
    }

    TypeResult Ty = ParseTypeName();

    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      Expr = ExprError();
    }

    if (Expr.isInvalid() || Ty.isInvalid())
      Res = ExprError();
    else
      Res = Actions.ActOnVAArg(StartLoc, Expr.get(), Ty.get(), ConsumeParen());
    break;
  }
  case tok::kw___builtin_offsetof: {
    SourceLocation TypeLoc = Tok.getLocation();
    auto OOK = Sema::OffsetOfKind::OOK_Builtin;
    if (Tok.getLocation().isMacroID()) {
      StringRef MacroName = Lexer::getImmediateMacroNameForDiagnostics(
          Tok.getLocation(), PP.getSourceManager(), getLangOpts());
      if (MacroName == "offsetof")
        OOK = Sema::OffsetOfKind::OOK_Macro;
    }
    TypeResult Ty;
    {
      OffsetOfStateRAIIObject InOffsetof(*this, OOK);
      Ty = ParseTypeName();
      if (Ty.isInvalid()) {
        SkipUntil(tok::r_paren, StopAtSemi);
        return ExprError();
      }
    }

    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // We must have at least one identifier here.
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected) << tok::identifier;
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // Keep track of the various subcomponents we see.
    SmallVector<Sema::OffsetOfComponent, 4> Comps;

    Comps.push_back(Sema::OffsetOfComponent());
    Comps.back().isBrackets = false;
    Comps.back().U.IdentInfo = Tok.getIdentifierInfo();
    Comps.back().LocStart = Comps.back().LocEnd = ConsumeToken();

    // FIXME: This loop leaks the index expressions on error.
    while (true) {
      if (Tok.is(tok::period)) {
        // offsetof-member-designator: offsetof-member-designator '.' identifier
        Comps.push_back(Sema::OffsetOfComponent());
        Comps.back().isBrackets = false;
        Comps.back().LocStart = ConsumeToken();

        if (Tok.isNot(tok::identifier)) {
          Diag(Tok, diag::err_expected) << tok::identifier;
          SkipUntil(tok::r_paren, StopAtSemi);
          return ExprError();
        }
        Comps.back().U.IdentInfo = Tok.getIdentifierInfo();
        Comps.back().LocEnd = ConsumeToken();
      } else if (Tok.is(tok::l_square)) {
        if (CheckProhibitedCXX11Attribute())
          return ExprError();

        // offsetof-member-designator: offsetof-member-design '[' expression ']'
        Comps.push_back(Sema::OffsetOfComponent());
        Comps.back().isBrackets = true;
        BalancedDelimiterTracker ST(*this, tok::l_square);
        ST.consumeOpen();
        Comps.back().LocStart = ST.getOpenLocation();
        Res = ParseExpression();
        if (Res.isInvalid()) {
          SkipUntil(tok::r_paren, StopAtSemi);
          return Res;
        }
        Comps.back().U.E = Res.get();

        ST.consumeClose();
        Comps.back().LocEnd = ST.getCloseLocation();
      } else {
        if (Tok.isNot(tok::r_paren)) {
          PT.consumeClose();
          Res = ExprError();
        } else if (Ty.isInvalid()) {
          Res = ExprError();
        } else {
          PT.consumeClose();
          Res = Actions.ActOnBuiltinOffsetOf(getCurScope(), StartLoc, TypeLoc,
                                             Ty.get(), Comps,
                                             PT.getCloseLocation());
        }
        break;
      }
    }
    break;
  }
  case tok::kw___builtin_choose_expr: {
    ExprResult Cond(ParseAssignmentExpression());
    if (Cond.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return Cond;
    }
    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    ExprResult Expr1(ParseAssignmentExpression());
    if (Expr1.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return Expr1;
    }
    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    ExprResult Expr2(ParseAssignmentExpression());
    if (Expr2.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return Expr2;
    }
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      return ExprError();
    }
    Res = Actions.ActOnChooseExpr(StartLoc, Cond.get(), Expr1.get(),
                                  Expr2.get(), ConsumeParen());
    break;
  }
  case tok::kw___builtin_astype: {
    // The first argument is an expression to be converted, followed by a comma.
    ExprResult Expr(ParseAssignmentExpression());
    if (Expr.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // Second argument is the type to bitcast to.
    TypeResult DestTy = ParseTypeName();
    if (DestTy.isInvalid())
      return ExprError();

    // Attempt to consume the r-paren.
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    Res = Actions.ActOnAsTypeExpr(Expr.get(), DestTy.get(), StartLoc,
                                  ConsumeParen());
    break;
  }
  case tok::kw___builtin_convertvector: {
    // The first argument is an expression to be converted, followed by a comma.
    ExprResult Expr(ParseAssignmentExpression());
    if (Expr.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    if (ExpectAndConsume(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // Second argument is the type to bitcast to.
    TypeResult DestTy = ParseTypeName();
    if (DestTy.isInvalid())
      return ExprError();

    // Attempt to consume the r-paren.
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    Res = Actions.ActOnConvertVectorExpr(Expr.get(), DestTy.get(), StartLoc,
                                         ConsumeParen());
    break;
  }
  case tok::kw___builtin_COLUMN:
  case tok::kw___builtin_FILE:
  case tok::kw___builtin_FILE_NAME:
  case tok::kw___builtin_FUNCTION:
  case tok::kw___builtin_FUNCSIG:
  case tok::kw___builtin_LINE:
  case tok::kw___builtin_source_location: {
    // Attempt to consume the r-paren.
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }
    SourceLocIdentKind Kind = [&] {
      switch (T) {
      case tok::kw___builtin_FILE:
        return SourceLocIdentKind::File;
      case tok::kw___builtin_FILE_NAME:
        return SourceLocIdentKind::FileName;
      case tok::kw___builtin_FUNCTION:
        return SourceLocIdentKind::Function;
      case tok::kw___builtin_FUNCSIG:
        return SourceLocIdentKind::FuncSig;
      case tok::kw___builtin_LINE:
        return SourceLocIdentKind::Line;
      case tok::kw___builtin_COLUMN:
        return SourceLocIdentKind::Column;
      case tok::kw___builtin_source_location:
        return SourceLocIdentKind::SourceLocStruct;
      default:
        llvm_unreachable("invalid keyword");
      }
    }();
    Res = Actions.ActOnSourceLocExpr(Kind, StartLoc, ConsumeParen());
    break;
  }
  }

  if (Res.isInvalid())
    return ExprError();

  // These can be followed by postfix-expr pieces because they are
  // primary-expressions.
  return ParsePostfixExpressionSuffix(Res.get());
}

bool Parser::tryParseOpenMPArrayShapingCastPart() {
  assert(Tok.is(tok::l_square) && "Expected open bracket");
  bool ErrorFound = true;
  TentativeParsingAction TPA(*this);
  do {
    if (Tok.isNot(tok::l_square))
      break;
    // Consume '['
    ConsumeBracket();
    // Skip inner expression.
    while (!SkipUntil(tok::r_square, tok::annot_pragma_openmp_end,
                      StopAtSemi | StopBeforeMatch))
      ;
    if (Tok.isNot(tok::r_square))
      break;
    // Consume ']'
    ConsumeBracket();
    // Found ')' - done.
    if (Tok.is(tok::r_paren)) {
      ErrorFound = false;
      break;
    }
  } while (Tok.isNot(tok::annot_pragma_openmp_end));
  TPA.Revert();
  return !ErrorFound;
}

/// ParseParenExpression - This parses the unit that starts with a '(' token,
/// based on what is allowed by ExprType.  The actual thing parsed is returned
/// in ExprType. If stopIfCastExpr is true, it will only return the parsed type,
/// not the parsed cast-expression.
///
/// \verbatim
///       primary-expression: [C99 6.5.1]
///         '(' expression ')'
/// [GNU]   '(' compound-statement ')'      (if !ParenExprOnly)
///       postfix-expression: [C99 6.5.2]
///         '(' type-name ')' '{' initializer-list '}'
///         '(' type-name ')' '{' initializer-list ',' '}'
///       cast-expression: [C99 6.5.4]
///         '(' type-name ')' cast-expression
/// [ARC]   bridged-cast-expression
/// [ARC] bridged-cast-expression:
///         (__bridge type-name) cast-expression
///         (__bridge_transfer type-name) cast-expression
///         (__bridge_retained type-name) cast-expression
///       fold-expression: [C++1z]
///         '(' cast-expression fold-operator '...' ')'
///         '(' '...' fold-operator cast-expression ')'
///         '(' cast-expression fold-operator '...'
///                 fold-operator cast-expression ')'
/// [OPENMP] Array shaping operation
///       '(' '[' expression ']' { '[' expression ']' } cast-expression
/// \endverbatim
ExprResult
Parser::ParseParenExpression(ParenParseOption &ExprType, bool stopIfCastExpr,
                             bool isTypeCast, ParsedType &CastTy,
                             SourceLocation &RParenLoc) {
  assert(Tok.is(tok::l_paren) && "Not a paren expr!");
  ColonProtectionRAIIObject ColonProtection(*this, false);
  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.consumeOpen())
    return ExprError();
  SourceLocation OpenLoc = T.getOpenLocation();

  PreferredType.enterParenExpr(Tok.getLocation(), OpenLoc);

  ExprResult Result(true);
  bool isAmbiguousTypeId;
  CastTy = nullptr;

  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteExpression(
        getCurScope(), PreferredType.get(Tok.getLocation()),
        /*IsParenthesized=*/ExprType >= CompoundLiteral);
    return ExprError();
  }

  // Diagnose use of bridge casts in non-arc mode.
  bool BridgeCast = (getLangOpts().ObjC &&
                     Tok.isOneOf(tok::kw___bridge,
                                 tok::kw___bridge_transfer,
                                 tok::kw___bridge_retained,
                                 tok::kw___bridge_retain));
  if (BridgeCast && !getLangOpts().ObjCAutoRefCount) {
    if (!TryConsumeToken(tok::kw___bridge)) {
      StringRef BridgeCastName = Tok.getName();
      SourceLocation BridgeKeywordLoc = ConsumeToken();
      if (!PP.getSourceManager().isInSystemHeader(BridgeKeywordLoc))
        Diag(BridgeKeywordLoc, diag::warn_arc_bridge_cast_nonarc)
          << BridgeCastName
          << FixItHint::CreateReplacement(BridgeKeywordLoc, "");
    }
    BridgeCast = false;
  }

  // None of these cases should fall through with an invalid Result
  // unless they've already reported an error.
  if (ExprType >= CompoundStmt && Tok.is(tok::l_brace)) {
    Diag(Tok, OpenLoc.isMacroID() ? diag::ext_gnu_statement_expr_macro
                                  : diag::ext_gnu_statement_expr);

    checkCompoundToken(OpenLoc, tok::l_paren, CompoundToken::StmtExprBegin);

    if (!getCurScope()->getFnParent() && !getCurScope()->getBlockParent()) {
      Result = ExprError(Diag(OpenLoc, diag::err_stmtexpr_file_scope));
    } else {
      // Find the nearest non-record decl context. Variables declared in a
      // statement expression behave as if they were declared in the enclosing
      // function, block, or other code construct.
      DeclContext *CodeDC = Actions.CurContext;
      while (CodeDC->isRecord() || isa<EnumDecl>(CodeDC)) {
        CodeDC = CodeDC->getParent();
        assert(CodeDC && !CodeDC->isFileContext() &&
               "statement expr not in code context");
      }
      Sema::ContextRAII SavedContext(Actions, CodeDC, /*NewThisContext=*/false);

      Actions.ActOnStartStmtExpr();

      StmtResult Stmt(ParseCompoundStatement(true));
      ExprType = CompoundStmt;

      // If the substmt parsed correctly, build the AST node.
      if (!Stmt.isInvalid()) {
        Result = Actions.ActOnStmtExpr(getCurScope(), OpenLoc, Stmt.get(),
                                       Tok.getLocation());
      } else {
        Actions.ActOnStmtExprError();
      }
    }
  } else if (ExprType >= CompoundLiteral && BridgeCast) {
    tok::TokenKind tokenKind = Tok.getKind();
    SourceLocation BridgeKeywordLoc = ConsumeToken();

    // Parse an Objective-C ARC ownership cast expression.
    ObjCBridgeCastKind Kind;
    if (tokenKind == tok::kw___bridge)
      Kind = OBC_Bridge;
    else if (tokenKind == tok::kw___bridge_transfer)
      Kind = OBC_BridgeTransfer;
    else if (tokenKind == tok::kw___bridge_retained)
      Kind = OBC_BridgeRetained;
    else {
      // As a hopefully temporary workaround, allow __bridge_retain as
      // a synonym for __bridge_retained, but only in system headers.
      assert(tokenKind == tok::kw___bridge_retain);
      Kind = OBC_BridgeRetained;
      if (!PP.getSourceManager().isInSystemHeader(BridgeKeywordLoc))
        Diag(BridgeKeywordLoc, diag::err_arc_bridge_retain)
          << FixItHint::CreateReplacement(BridgeKeywordLoc,
                                          "__bridge_retained");
    }

    TypeResult Ty = ParseTypeName();
    T.consumeClose();
    ColonProtection.restore();
    RParenLoc = T.getCloseLocation();

    PreferredType.enterTypeCast(Tok.getLocation(), Ty.get().get());
    ExprResult SubExpr = ParseCastExpression(AnyCastExpr);

    if (Ty.isInvalid() || SubExpr.isInvalid())
      return ExprError();

    return Actions.ObjC().ActOnObjCBridgedCast(getCurScope(), OpenLoc, Kind,
                                               BridgeKeywordLoc, Ty.get(),
                                               RParenLoc, SubExpr.get());
  } else if (ExprType >= CompoundLiteral &&
             isTypeIdInParens(isAmbiguousTypeId)) {

    // Otherwise, this is a compound literal expression or cast expression.

    // In C++, if the type-id is ambiguous we disambiguate based on context.
    // If stopIfCastExpr is true the context is a typeof/sizeof/alignof
    // in which case we should treat it as type-id.
    // if stopIfCastExpr is false, we need to determine the context past the
    // parens, so we defer to ParseCXXAmbiguousParenExpression for that.
    if (isAmbiguousTypeId && !stopIfCastExpr) {
      ExprResult res = ParseCXXAmbiguousParenExpression(ExprType, CastTy, T,
                                                        ColonProtection);
      RParenLoc = T.getCloseLocation();
      return res;
    }

    // Parse the type declarator.
    DeclSpec DS(AttrFactory);
    ParseSpecifierQualifierList(DS);
    Declarator DeclaratorInfo(DS, ParsedAttributesView::none(),
                              DeclaratorContext::TypeName);
    ParseDeclarator(DeclaratorInfo);

    // If our type is followed by an identifier and either ':' or ']', then
    // this is probably an Objective-C message send where the leading '[' is
    // missing. Recover as if that were the case.
    if (!DeclaratorInfo.isInvalidType() && Tok.is(tok::identifier) &&
        !InMessageExpression && getLangOpts().ObjC &&
        (NextToken().is(tok::colon) || NextToken().is(tok::r_square))) {
      TypeResult Ty;
      {
        InMessageExpressionRAIIObject InMessage(*this, false);
        Ty = Actions.ActOnTypeName(DeclaratorInfo);
      }
      Result = ParseObjCMessageExpressionBody(SourceLocation(),
                                              SourceLocation(),
                                              Ty.get(), nullptr);
    } else {
      // Match the ')'.
      T.consumeClose();
      ColonProtection.restore();
      RParenLoc = T.getCloseLocation();
      if (Tok.is(tok::l_brace)) {
        ExprType = CompoundLiteral;
        TypeResult Ty;
        {
          InMessageExpressionRAIIObject InMessage(*this, false);
          Ty = Actions.ActOnTypeName(DeclaratorInfo);
        }
        return ParseCompoundLiteralExpression(Ty.get(), OpenLoc, RParenLoc);
      }

      if (Tok.is(tok::l_paren)) {
        // This could be OpenCL vector Literals
        if (getLangOpts().OpenCL)
        {
          TypeResult Ty;
          {
            InMessageExpressionRAIIObject InMessage(*this, false);
            Ty = Actions.ActOnTypeName(DeclaratorInfo);
          }
          if(Ty.isInvalid())
          {
             return ExprError();
          }
          QualType QT = Ty.get().get().getCanonicalType();
          if (QT->isVectorType())
          {
            // We parsed '(' vector-type-name ')' followed by '('

            // Parse the cast-expression that follows it next.
            // isVectorLiteral = true will make sure we don't parse any
            // Postfix expression yet
            Result = ParseCastExpression(/*isUnaryExpression=*/AnyCastExpr,
                                         /*isAddressOfOperand=*/false,
                                         /*isTypeCast=*/IsTypeCast,
                                         /*isVectorLiteral=*/true);

            if (!Result.isInvalid()) {
              Result = Actions.ActOnCastExpr(getCurScope(), OpenLoc,
                                             DeclaratorInfo, CastTy,
                                             RParenLoc, Result.get());
            }

            // After we performed the cast we can check for postfix-expr pieces.
            if (!Result.isInvalid()) {
              Result = ParsePostfixExpressionSuffix(Result);
            }

            return Result;
          }
        }
      }

      if (ExprType == CastExpr) {
        // We parsed '(' type-name ')' and the thing after it wasn't a '{'.

        if (DeclaratorInfo.isInvalidType())
          return ExprError();

        // Note that this doesn't parse the subsequent cast-expression, it just
        // returns the parsed type to the callee.
        if (stopIfCastExpr) {
          TypeResult Ty;
          {
            InMessageExpressionRAIIObject InMessage(*this, false);
            Ty = Actions.ActOnTypeName(DeclaratorInfo);
          }
          CastTy = Ty.get();
          return ExprResult();
        }

        // Reject the cast of super idiom in ObjC.
        if (Tok.is(tok::identifier) && getLangOpts().ObjC &&
            Tok.getIdentifierInfo() == Ident_super &&
            getCurScope()->isInObjcMethodScope() &&
            GetLookAheadToken(1).isNot(tok::period)) {
          Diag(Tok.getLocation(), diag::err_illegal_super_cast)
            << SourceRange(OpenLoc, RParenLoc);
          return ExprError();
        }

        PreferredType.enterTypeCast(Tok.getLocation(), CastTy.get());
        // Parse the cast-expression that follows it next.
        // TODO: For cast expression with CastTy.
        Result = ParseCastExpression(/*isUnaryExpression=*/AnyCastExpr,
                                     /*isAddressOfOperand=*/false,
                                     /*isTypeCast=*/IsTypeCast);
        if (!Result.isInvalid()) {
          Result = Actions.ActOnCastExpr(getCurScope(), OpenLoc,
                                         DeclaratorInfo, CastTy,
                                         RParenLoc, Result.get());
        }
        return Result;
      }

      Diag(Tok, diag::err_expected_lbrace_in_compound_literal);
      return ExprError();
    }
  } else if (ExprType >= FoldExpr && Tok.is(tok::ellipsis) &&
             isFoldOperator(NextToken().getKind())) {
    ExprType = FoldExpr;
    return ParseFoldExpression(ExprResult(), T);
  } else if (isTypeCast) {
    // Parse the expression-list.
    InMessageExpressionRAIIObject InMessage(*this, false);
    ExprVector ArgExprs;

    if (!ParseSimpleExpressionList(ArgExprs)) {
      // FIXME: If we ever support comma expressions as operands to
      // fold-expressions, we'll need to allow multiple ArgExprs here.
      if (ExprType >= FoldExpr && ArgExprs.size() == 1 &&
          isFoldOperator(Tok.getKind()) && NextToken().is(tok::ellipsis)) {
        ExprType = FoldExpr;
        return ParseFoldExpression(ArgExprs[0], T);
      }

      ExprType = SimpleExpr;
      Result = Actions.ActOnParenListExpr(OpenLoc, Tok.getLocation(),
                                          ArgExprs);
    }
  } else if (getLangOpts().OpenMP >= 50 && OpenMPDirectiveParsing &&
             ExprType == CastExpr && Tok.is(tok::l_square) &&
             tryParseOpenMPArrayShapingCastPart()) {
    bool ErrorFound = false;
    SmallVector<Expr *, 4> OMPDimensions;
    SmallVector<SourceRange, 4> OMPBracketsRanges;
    do {
      BalancedDelimiterTracker TS(*this, tok::l_square);
      TS.consumeOpen();
      ExprResult NumElements =
          Actions.CorrectDelayedTyposInExpr(ParseExpression());
      if (!NumElements.isUsable()) {
        ErrorFound = true;
        while (!SkipUntil(tok::r_square, tok::r_paren,
                          StopAtSemi | StopBeforeMatch))
          ;
      }
      TS.consumeClose();
      OMPDimensions.push_back(NumElements.get());
      OMPBracketsRanges.push_back(TS.getRange());
    } while (Tok.isNot(tok::r_paren));
    // Match the ')'.
    T.consumeClose();
    RParenLoc = T.getCloseLocation();
    Result = Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression());
    if (ErrorFound) {
      Result = ExprError();
    } else if (!Result.isInvalid()) {
      Result = Actions.OpenMP().ActOnOMPArrayShapingExpr(
          Result.get(), OpenLoc, RParenLoc, OMPDimensions, OMPBracketsRanges);
    }
    return Result;
  } else {
    InMessageExpressionRAIIObject InMessage(*this, false);

    Result = ParseExpression(MaybeTypeCast);
    if (!getLangOpts().CPlusPlus && Result.isUsable()) {
      // Correct typos in non-C++ code earlier so that implicit-cast-like
      // expressions are parsed correctly.
      Result = Actions.CorrectDelayedTyposInExpr(Result);
    }

    if (ExprType >= FoldExpr && isFoldOperator(Tok.getKind()) &&
        NextToken().is(tok::ellipsis)) {
      ExprType = FoldExpr;
      return ParseFoldExpression(Result, T);
    }
    ExprType = SimpleExpr;

    // Don't build a paren expression unless we actually match a ')'.
    if (!Result.isInvalid() && Tok.is(tok::r_paren))
      Result =
          Actions.ActOnParenExpr(OpenLoc, Tok.getLocation(), Result.get());
  }

  // Match the ')'.
  if (Result.isInvalid()) {
    SkipUntil(tok::r_paren, StopAtSemi);
    return ExprError();
  }

  T.consumeClose();
  RParenLoc = T.getCloseLocation();
  return Result;
}

/// ParseCompoundLiteralExpression - We have parsed the parenthesized type-name
/// and we are at the left brace.
///
/// \verbatim
///       postfix-expression: [C99 6.5.2]
///         '(' type-name ')' '{' initializer-list '}'
///         '(' type-name ')' '{' initializer-list ',' '}'
/// \endverbatim
ExprResult
Parser::ParseCompoundLiteralExpression(ParsedType Ty,
                                       SourceLocation LParenLoc,
                                       SourceLocation RParenLoc) {
  assert(Tok.is(tok::l_brace) && "Not a compound literal!");
  if (!getLangOpts().C99)   // Compound literals don't exist in C90.
    Diag(LParenLoc, diag::ext_c99_compound_literal);
  PreferredType.enterTypeCast(Tok.getLocation(), Ty.get());
  ExprResult Result = ParseInitializer();
  if (!Result.isInvalid() && Ty)
    return Actions.ActOnCompoundLiteral(LParenLoc, Ty, RParenLoc, Result.get());
  return Result;
}

/// ParseStringLiteralExpression - This handles the various token types that
/// form string literals, and also handles string concatenation [C99 5.1.1.2,
/// translation phase #6].
///
/// \verbatim
///       primary-expression: [C99 6.5.1]
///         string-literal
/// \verbatim
ExprResult Parser::ParseStringLiteralExpression(bool AllowUserDefinedLiteral) {
  return ParseStringLiteralExpression(AllowUserDefinedLiteral,
                                      /*Unevaluated=*/false);
}

ExprResult Parser::ParseUnevaluatedStringLiteralExpression() {
  return ParseStringLiteralExpression(/*AllowUserDefinedLiteral=*/false,
                                      /*Unevaluated=*/true);
}

ExprResult Parser::ParseStringLiteralExpression(bool AllowUserDefinedLiteral,
                                                bool Unevaluated) {
  assert(tokenIsLikeStringLiteral(Tok, getLangOpts()) &&
         "Not a string-literal-like token!");

  // String concatenation.
  // Note: some keywords like __FUNCTION__ are not considered to be strings
  // for concatenation purposes, unless Microsoft extensions are enabled.
  SmallVector<Token, 4> StringToks;

  do {
    StringToks.push_back(Tok);
    ConsumeAnyToken();
  } while (tokenIsLikeStringLiteral(Tok, getLangOpts()));

  if (Unevaluated) {
    assert(!AllowUserDefinedLiteral && "UDL are always evaluated");
    return Actions.ActOnUnevaluatedStringLiteral(StringToks);
  }

  // Pass the set of string tokens, ready for concatenation, to the actions.
  return Actions.ActOnStringLiteral(StringToks,
                                    AllowUserDefinedLiteral ? getCurScope()
                                                            : nullptr);
}

/// ParseGenericSelectionExpression - Parse a C11 generic-selection
/// [C11 6.5.1.1].
///
/// \verbatim
///    generic-selection:
///           _Generic ( assignment-expression , generic-assoc-list )
///    generic-assoc-list:
///           generic-association
///           generic-assoc-list , generic-association
///    generic-association:
///           type-name : assignment-expression
///           default : assignment-expression
/// \endverbatim
///
/// As an extension, Clang also accepts:
/// \verbatim
///   generic-selection:
///          _Generic ( type-name, generic-assoc-list )
/// \endverbatim
ExprResult Parser::ParseGenericSelectionExpression() {
  assert(Tok.is(tok::kw__Generic) && "_Generic keyword expected");

  diagnoseUseOfC11Keyword(Tok);

  SourceLocation KeyLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.expectAndConsume())
    return ExprError();

  // We either have a controlling expression or we have a controlling type, and
  // we need to figure out which it is.
  TypeResult ControllingType;
  ExprResult ControllingExpr;
  if (isTypeIdForGenericSelection()) {
    ControllingType = ParseTypeName();
    if (ControllingType.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }
    const auto *LIT = cast<LocInfoType>(ControllingType.get().get());
    SourceLocation Loc = LIT->getTypeSourceInfo()->getTypeLoc().getBeginLoc();
    Diag(Loc, getLangOpts().C2y ? diag::warn_c2y_compat_generic_with_type_arg
                                : diag::ext_c2y_generic_with_type_arg);
  } else {
    // C11 6.5.1.1p3 "The controlling expression of a generic selection is
    // not evaluated."
    EnterExpressionEvaluationContext Unevaluated(
        Actions, Sema::ExpressionEvaluationContext::Unevaluated);
    ControllingExpr =
        Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression());
    if (ControllingExpr.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }
  }

  if (ExpectAndConsume(tok::comma)) {
    SkipUntil(tok::r_paren, StopAtSemi);
    return ExprError();
  }

  SourceLocation DefaultLoc;
  SmallVector<ParsedType, 12> Types;
  ExprVector Exprs;
  do {
    ParsedType Ty;
    if (Tok.is(tok::kw_default)) {
      // C11 6.5.1.1p2 "A generic selection shall have no more than one default
      // generic association."
      if (!DefaultLoc.isInvalid()) {
        Diag(Tok, diag::err_duplicate_default_assoc);
        Diag(DefaultLoc, diag::note_previous_default_assoc);
        SkipUntil(tok::r_paren, StopAtSemi);
        return ExprError();
      }
      DefaultLoc = ConsumeToken();
      Ty = nullptr;
    } else {
      ColonProtectionRAIIObject X(*this);
      TypeResult TR = ParseTypeName(nullptr, DeclaratorContext::Association);
      if (TR.isInvalid()) {
        SkipUntil(tok::r_paren, StopAtSemi);
        return ExprError();
      }
      Ty = TR.get();
    }
    Types.push_back(Ty);

    if (ExpectAndConsume(tok::colon)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // FIXME: These expressions should be parsed in a potentially potentially
    // evaluated context.
    ExprResult ER(
        Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression()));
    if (ER.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }
    Exprs.push_back(ER.get());
  } while (TryConsumeToken(tok::comma));

  T.consumeClose();
  if (T.getCloseLocation().isInvalid())
    return ExprError();

  void *ExprOrTy = ControllingExpr.isUsable()
                       ? ControllingExpr.get()
                       : ControllingType.get().getAsOpaquePtr();

  return Actions.ActOnGenericSelectionExpr(
      KeyLoc, DefaultLoc, T.getCloseLocation(), ControllingExpr.isUsable(),
      ExprOrTy, Types, Exprs);
}

/// Parse A C++1z fold-expression after the opening paren and optional
/// left-hand-side expression.
///
/// \verbatim
///   fold-expression:
///       ( cast-expression fold-operator ... )
///       ( ... fold-operator cast-expression )
///       ( cast-expression fold-operator ... fold-operator cast-expression )
ExprResult Parser::ParseFoldExpression(ExprResult LHS,
                                       BalancedDelimiterTracker &T) {
  if (LHS.isInvalid()) {
    T.skipToEnd();
    return true;
  }

  tok::TokenKind Kind = tok::unknown;
  SourceLocation FirstOpLoc;
  if (LHS.isUsable()) {
    Kind = Tok.getKind();
    assert(isFoldOperator(Kind) && "missing fold-operator");
    FirstOpLoc = ConsumeToken();
  }

  assert(Tok.is(tok::ellipsis) && "not a fold-expression");
  SourceLocation EllipsisLoc = ConsumeToken();

  ExprResult RHS;
  if (Tok.isNot(tok::r_paren)) {
    if (!isFoldOperator(Tok.getKind()))
      return Diag(Tok.getLocation(), diag::err_expected_fold_operator);

    if (Kind != tok::unknown && Tok.getKind() != Kind)
      Diag(Tok.getLocation(), diag::err_fold_operator_mismatch)
        << SourceRange(FirstOpLoc);
    Kind = Tok.getKind();
    ConsumeToken();

    RHS = ParseExpression();
    if (RHS.isInvalid()) {
      T.skipToEnd();
      return true;
    }
  }

  Diag(EllipsisLoc, getLangOpts().CPlusPlus17
                        ? diag::warn_cxx14_compat_fold_expression
                        : diag::ext_fold_expression);

  T.consumeClose();
  return Actions.ActOnCXXFoldExpr(getCurScope(), T.getOpenLocation(), LHS.get(),
                                  Kind, EllipsisLoc, RHS.get(),
                                  T.getCloseLocation());
}

void Parser::injectEmbedTokens() {
  EmbedAnnotationData *Data =
      reinterpret_cast<EmbedAnnotationData *>(Tok.getAnnotationValue());
  MutableArrayRef<Token> Toks(PP.getPreprocessorAllocator().Allocate<Token>(
                                  Data->BinaryData.size() * 2 - 1),
                              Data->BinaryData.size() * 2 - 1);
  unsigned I = 0;
  for (auto &Byte : Data->BinaryData) {
    Toks[I].startToken();
    Toks[I].setKind(tok::binary_data);
    Toks[I].setLocation(Tok.getLocation());
    Toks[I].setLength(1);
    Toks[I].setLiteralData(&Byte);
    if (I != ((Data->BinaryData.size() - 1) * 2)) {
      Toks[I + 1].startToken();
      Toks[I + 1].setKind(tok::comma);
      Toks[I + 1].setLocation(Tok.getLocation());
    }
    I += 2;
  }
  PP.EnterTokenStream(std::move(Toks), /*DisableMacroExpansion=*/true,
                      /*IsReinject=*/true);
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);
}

/// ParseExpressionList - Used for C/C++ (argument-)expression-list.
///
/// \verbatim
///       argument-expression-list:
///         assignment-expression
///         argument-expression-list , assignment-expression
///
/// [C++] expression-list:
/// [C++]   assignment-expression
/// [C++]   expression-list , assignment-expression
///
/// [C++0x] expression-list:
/// [C++0x]   initializer-list
///
/// [C++0x] initializer-list
/// [C++0x]   initializer-clause ...[opt]
/// [C++0x]   initializer-list , initializer-clause ...[opt]
///
/// [C++0x] initializer-clause:
/// [C++0x]   assignment-expression
/// [C++0x]   braced-init-list
/// \endverbatim
bool Parser::ParseExpressionList(SmallVectorImpl<Expr *> &Exprs,
                                 llvm::function_ref<void()> ExpressionStarts,
                                 bool FailImmediatelyOnInvalidExpr,
                                 bool EarlyTypoCorrection) {
  bool SawError = false;
  while (true) {
    if (ExpressionStarts)
      ExpressionStarts();

    ExprResult Expr;
    if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
      Diag(Tok, diag::warn_cxx98_compat_generalized_initializer_lists);
      Expr = ParseBraceInitializer();
    } else
      Expr = ParseAssignmentExpression();

    if (EarlyTypoCorrection)
      Expr = Actions.CorrectDelayedTyposInExpr(Expr);

    if (Tok.is(tok::ellipsis))
      Expr = Actions.ActOnPackExpansion(Expr.get(), ConsumeToken());
    else if (Tok.is(tok::code_completion)) {
      // There's nothing to suggest in here as we parsed a full expression.
      // Instead fail and propagate the error since caller might have something
      // the suggest, e.g. signature help in function call. Note that this is
      // performed before pushing the \p Expr, so that signature help can report
      // current argument correctly.
      SawError = true;
      cutOffParsing();
      break;
    }
    if (Expr.isInvalid()) {
      SawError = true;
      if (FailImmediatelyOnInvalidExpr)
        break;
      SkipUntil(tok::comma, tok::r_paren, StopBeforeMatch);
    } else {
      Exprs.push_back(Expr.get());
    }

    if (Tok.isNot(tok::comma))
      break;
    // Move to the next argument, remember where the comma was.
    Token Comma = Tok;
    ConsumeToken();
    checkPotentialAngleBracketDelimiter(Comma);
  }
  if (SawError) {
    // Ensure typos get diagnosed when errors were encountered while parsing the
    // expression list.
    for (auto &E : Exprs) {
      ExprResult Expr = Actions.CorrectDelayedTyposInExpr(E);
      if (Expr.isUsable()) E = Expr.get();
    }
  }
  return SawError;
}

/// ParseSimpleExpressionList - A simple comma-separated list of expressions,
/// used for misc language extensions.
///
/// \verbatim
///       simple-expression-list:
///         assignment-expression
///         simple-expression-list , assignment-expression
/// \endverbatim
bool Parser::ParseSimpleExpressionList(SmallVectorImpl<Expr *> &Exprs) {
  while (true) {
    ExprResult Expr = ParseAssignmentExpression();
    if (Expr.isInvalid())
      return true;

    Exprs.push_back(Expr.get());

    // We might be parsing the LHS of a fold-expression. If we reached the fold
    // operator, stop.
    if (Tok.isNot(tok::comma) || NextToken().is(tok::ellipsis))
      return false;

    // Move to the next argument, remember where the comma was.
    Token Comma = Tok;
    ConsumeToken();
    checkPotentialAngleBracketDelimiter(Comma);
  }
}

/// ParseBlockId - Parse a block-id, which roughly looks like int (int x).
///
/// \verbatim
/// [clang] block-id:
/// [clang]   specifier-qualifier-list block-declarator
/// \endverbatim
void Parser::ParseBlockId(SourceLocation CaretLoc) {
  if (Tok.is(tok::code_completion)) {
    cutOffParsing();
    Actions.CodeCompletion().CodeCompleteOrdinaryName(
        getCurScope(), SemaCodeCompletion::PCC_Type);
    return;
  }

  // Parse the specifier-qualifier-list piece.
  DeclSpec DS(AttrFactory);
  ParseSpecifierQualifierList(DS);

  // Parse the block-declarator.
  Declarator DeclaratorInfo(DS, ParsedAttributesView::none(),
                            DeclaratorContext::BlockLiteral);
  DeclaratorInfo.setFunctionDefinitionKind(FunctionDefinitionKind::Definition);
  ParseDeclarator(DeclaratorInfo);

  MaybeParseGNUAttributes(DeclaratorInfo);

  // Inform sema that we are starting a block.
  Actions.ActOnBlockArguments(CaretLoc, DeclaratorInfo, getCurScope());
}

/// ParseBlockLiteralExpression - Parse a block literal, which roughly looks
/// like ^(int x){ return x+1; }
///
/// \verbatim
///         block-literal:
/// [clang]   '^' block-args[opt] compound-statement
/// [clang]   '^' block-id compound-statement
/// [clang] block-args:
/// [clang]   '(' parameter-list ')'
/// \endverbatim
ExprResult Parser::ParseBlockLiteralExpression() {
  assert(Tok.is(tok::caret) && "block literal starts with ^");
  SourceLocation CaretLoc = ConsumeToken();

  PrettyStackTraceLoc CrashInfo(PP.getSourceManager(), CaretLoc,
                                "block literal parsing");

  // Enter a scope to hold everything within the block.  This includes the
  // argument decls, decls within the compound expression, etc.  This also
  // allows determining whether a variable reference inside the block is
  // within or outside of the block.
  ParseScope BlockScope(this, Scope::BlockScope | Scope::FnScope |
                                  Scope::CompoundStmtScope | Scope::DeclScope);

  // Inform sema that we are starting a block.
  Actions.ActOnBlockStart(CaretLoc, getCurScope());

  // Parse the return type if present.
  DeclSpec DS(AttrFactory);
  Declarator ParamInfo(DS, ParsedAttributesView::none(),
                       DeclaratorContext::BlockLiteral);
  ParamInfo.setFunctionDefinitionKind(FunctionDefinitionKind::Definition);
  // FIXME: Since the return type isn't actually parsed, it can't be used to
  // fill ParamInfo with an initial valid range, so do it manually.
  ParamInfo.SetSourceRange(SourceRange(Tok.getLocation(), Tok.getLocation()));

  // If this block has arguments, parse them.  There is no ambiguity here with
  // the expression case, because the expression case requires a parameter list.
  if (Tok.is(tok::l_paren)) {
    ParseParenDeclarator(ParamInfo);
    // Parse the pieces after the identifier as if we had "int(...)".
    // SetIdentifier sets the source range end, but in this case we're past
    // that location.
    SourceLocation Tmp = ParamInfo.getSourceRange().getEnd();
    ParamInfo.SetIdentifier(nullptr, CaretLoc);
    ParamInfo.SetRangeEnd(Tmp);
    if (ParamInfo.isInvalidType()) {
      // If there was an error parsing the arguments, they may have
      // tried to use ^(x+y) which requires an argument list.  Just
      // skip the whole block literal.
      Actions.ActOnBlockError(CaretLoc, getCurScope());
      return ExprError();
    }

    MaybeParseGNUAttributes(ParamInfo);

    // Inform sema that we are starting a block.
    Actions.ActOnBlockArguments(CaretLoc, ParamInfo, getCurScope());
  } else if (!Tok.is(tok::l_brace)) {
    ParseBlockId(CaretLoc);
  } else {
    // Otherwise, pretend we saw (void).
    SourceLocation NoLoc;
    ParamInfo.AddTypeInfo(
        DeclaratorChunk::getFunction(/*HasProto=*/true,
                                     /*IsAmbiguous=*/false,
                                     /*RParenLoc=*/NoLoc,
                                     /*ArgInfo=*/nullptr,
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
                                     CaretLoc, CaretLoc, ParamInfo),
        CaretLoc);

    MaybeParseGNUAttributes(ParamInfo);

    // Inform sema that we are starting a block.
    Actions.ActOnBlockArguments(CaretLoc, ParamInfo, getCurScope());
  }


  ExprResult Result(true);
  if (!Tok.is(tok::l_brace)) {
    // Saw something like: ^expr
    Diag(Tok, diag::err_expected_expression);
    Actions.ActOnBlockError(CaretLoc, getCurScope());
    return ExprError();
  }

  StmtResult Stmt(ParseCompoundStatementBody());
  BlockScope.Exit();
  if (!Stmt.isInvalid())
    Result = Actions.ActOnBlockStmtExpr(CaretLoc, Stmt.get(), getCurScope());
  else
    Actions.ActOnBlockError(CaretLoc, getCurScope());
  return Result;
}

/// ParseObjCBoolLiteral - This handles the objective-c Boolean literals.
///
///         '__objc_yes'
///         '__objc_no'
ExprResult Parser::ParseObjCBoolLiteral() {
  tok::TokenKind Kind = Tok.getKind();
  return Actions.ObjC().ActOnObjCBoolLiteral(ConsumeToken(), Kind);
}

/// Validate availability spec list, emitting diagnostics if necessary. Returns
/// true if invalid.
static bool CheckAvailabilitySpecList(Parser &P,
                                      ArrayRef<AvailabilitySpec> AvailSpecs) {
  llvm::SmallSet<StringRef, 4> Platforms;
  bool HasOtherPlatformSpec = false;
  bool Valid = true;
  for (const auto &Spec : AvailSpecs) {
    if (Spec.isOtherPlatformSpec()) {
      if (HasOtherPlatformSpec) {
        P.Diag(Spec.getBeginLoc(), diag::err_availability_query_repeated_star);
        Valid = false;
      }

      HasOtherPlatformSpec = true;
      continue;
    }

    bool Inserted = Platforms.insert(Spec.getPlatform()).second;
    if (!Inserted) {
      // Rule out multiple version specs referring to the same platform.
      // For example, we emit an error for:
      // @available(macos 10.10, macos 10.11, *)
      StringRef Platform = Spec.getPlatform();
      P.Diag(Spec.getBeginLoc(), diag::err_availability_query_repeated_platform)
          << Spec.getEndLoc() << Platform;
      Valid = false;
    }
  }

  if (!HasOtherPlatformSpec) {
    SourceLocation InsertWildcardLoc = AvailSpecs.back().getEndLoc();
    P.Diag(InsertWildcardLoc, diag::err_availability_query_wildcard_required)
        << FixItHint::CreateInsertion(InsertWildcardLoc, ", *");
    return true;
  }

  return !Valid;
}

/// Parse availability query specification.
///
///  availability-spec:
///     '*'
///     identifier version-tuple
std::optional<AvailabilitySpec> Parser::ParseAvailabilitySpec() {
  if (Tok.is(tok::star)) {
    return AvailabilitySpec(ConsumeToken());
  } else {
    // Parse the platform name.
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteAvailabilityPlatformName();
      return std::nullopt;
    }
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_avail_query_expected_platform_name);
      return std::nullopt;
    }

    IdentifierLoc *PlatformIdentifier = ParseIdentifierLoc();
    SourceRange VersionRange;
    VersionTuple Version = ParseVersionTuple(VersionRange);

    if (Version.empty())
      return std::nullopt;

    StringRef GivenPlatform = PlatformIdentifier->Ident->getName();
    StringRef Platform =
        AvailabilityAttr::canonicalizePlatformName(GivenPlatform);

    if (AvailabilityAttr::getPrettyPlatformName(Platform).empty() ||
        (GivenPlatform.contains("xros") || GivenPlatform.contains("xrOS"))) {
      Diag(PlatformIdentifier->Loc,
           diag::err_avail_query_unrecognized_platform_name)
          << GivenPlatform;
      return std::nullopt;
    }

    return AvailabilitySpec(Version, Platform, PlatformIdentifier->Loc,
                            VersionRange.getEnd());
  }
}

ExprResult Parser::ParseAvailabilityCheckExpr(SourceLocation BeginLoc) {
  assert(Tok.is(tok::kw___builtin_available) ||
         Tok.isObjCAtKeyword(tok::objc_available));

  // Eat the available or __builtin_available.
  ConsumeToken();

  BalancedDelimiterTracker Parens(*this, tok::l_paren);
  if (Parens.expectAndConsume())
    return ExprError();

  SmallVector<AvailabilitySpec, 4> AvailSpecs;
  bool HasError = false;
  while (true) {
    std::optional<AvailabilitySpec> Spec = ParseAvailabilitySpec();
    if (!Spec)
      HasError = true;
    else
      AvailSpecs.push_back(*Spec);

    if (!TryConsumeToken(tok::comma))
      break;
  }

  if (HasError) {
    SkipUntil(tok::r_paren, StopAtSemi);
    return ExprError();
  }

  CheckAvailabilitySpecList(*this, AvailSpecs);

  if (Parens.consumeClose())
    return ExprError();

  return Actions.ObjC().ActOnObjCAvailabilityCheckExpr(
      AvailSpecs, BeginLoc, Parens.getCloseLocation());
}
