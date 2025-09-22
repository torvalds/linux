//===--- TokenAnnotator.cpp - Format C++ code -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a token annotator, i.e. creates
/// \c AnnotatedTokens out of \c FormatTokens with required extra information.
///
//===----------------------------------------------------------------------===//

#include "TokenAnnotator.h"
#include "FormatToken.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TokenKinds.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "format-token-annotator"

namespace clang {
namespace format {

static bool mustBreakAfterAttributes(const FormatToken &Tok,
                                     const FormatStyle &Style) {
  switch (Style.BreakAfterAttributes) {
  case FormatStyle::ABS_Always:
    return true;
  case FormatStyle::ABS_Leave:
    return Tok.NewlinesBefore > 0;
  default:
    return false;
  }
}

namespace {

/// Returns \c true if the line starts with a token that can start a statement
/// with an initializer.
static bool startsWithInitStatement(const AnnotatedLine &Line) {
  return Line.startsWith(tok::kw_for) || Line.startsWith(tok::kw_if) ||
         Line.startsWith(tok::kw_switch);
}

/// Returns \c true if the token can be used as an identifier in
/// an Objective-C \c \@selector, \c false otherwise.
///
/// Because getFormattingLangOpts() always lexes source code as
/// Objective-C++, C++ keywords like \c new and \c delete are
/// lexed as tok::kw_*, not tok::identifier, even for Objective-C.
///
/// For Objective-C and Objective-C++, both identifiers and keywords
/// are valid inside @selector(...) (or a macro which
/// invokes @selector(...)). So, we allow treat any identifier or
/// keyword as a potential Objective-C selector component.
static bool canBeObjCSelectorComponent(const FormatToken &Tok) {
  return Tok.Tok.getIdentifierInfo();
}

/// With `Left` being '(', check if we're at either `[...](` or
/// `[...]<...>(`, where the [ opens a lambda capture list.
static bool isLambdaParameterList(const FormatToken *Left) {
  // Skip <...> if present.
  if (Left->Previous && Left->Previous->is(tok::greater) &&
      Left->Previous->MatchingParen &&
      Left->Previous->MatchingParen->is(TT_TemplateOpener)) {
    Left = Left->Previous->MatchingParen;
  }

  // Check for `[...]`.
  return Left->Previous && Left->Previous->is(tok::r_square) &&
         Left->Previous->MatchingParen &&
         Left->Previous->MatchingParen->is(TT_LambdaLSquare);
}

/// Returns \c true if the token is followed by a boolean condition, \c false
/// otherwise.
static bool isKeywordWithCondition(const FormatToken &Tok) {
  return Tok.isOneOf(tok::kw_if, tok::kw_for, tok::kw_while, tok::kw_switch,
                     tok::kw_constexpr, tok::kw_catch);
}

/// Returns \c true if the token starts a C++ attribute, \c false otherwise.
static bool isCppAttribute(bool IsCpp, const FormatToken &Tok) {
  if (!IsCpp || !Tok.startsSequence(tok::l_square, tok::l_square))
    return false;
  // The first square bracket is part of an ObjC array literal
  if (Tok.Previous && Tok.Previous->is(tok::at))
    return false;
  const FormatToken *AttrTok = Tok.Next->Next;
  if (!AttrTok)
    return false;
  // C++17 '[[using ns: foo, bar(baz, blech)]]'
  // We assume nobody will name an ObjC variable 'using'.
  if (AttrTok->startsSequence(tok::kw_using, tok::identifier, tok::colon))
    return true;
  if (AttrTok->isNot(tok::identifier))
    return false;
  while (AttrTok && !AttrTok->startsSequence(tok::r_square, tok::r_square)) {
    // ObjC message send. We assume nobody will use : in a C++11 attribute
    // specifier parameter, although this is technically valid:
    // [[foo(:)]].
    if (AttrTok->is(tok::colon) ||
        AttrTok->startsSequence(tok::identifier, tok::identifier) ||
        AttrTok->startsSequence(tok::r_paren, tok::identifier)) {
      return false;
    }
    if (AttrTok->is(tok::ellipsis))
      return true;
    AttrTok = AttrTok->Next;
  }
  return AttrTok && AttrTok->startsSequence(tok::r_square, tok::r_square);
}

/// A parser that gathers additional information about tokens.
///
/// The \c TokenAnnotator tries to match parenthesis and square brakets and
/// store a parenthesis levels. It also tries to resolve matching "<" and ">"
/// into template parameter lists.
class AnnotatingParser {
public:
  AnnotatingParser(const FormatStyle &Style, AnnotatedLine &Line,
                   const AdditionalKeywords &Keywords,
                   SmallVector<ScopeType> &Scopes)
      : Style(Style), Line(Line), CurrentToken(Line.First), AutoFound(false),
        IsCpp(Style.isCpp()), LangOpts(getFormattingLangOpts(Style)),
        Keywords(Keywords), Scopes(Scopes), TemplateDeclarationDepth(0) {
    assert(IsCpp == LangOpts.CXXOperatorNames);
    Contexts.push_back(Context(tok::unknown, 1, /*IsExpression=*/false));
    resetTokenMetadata();
  }

private:
  ScopeType getScopeType(const FormatToken &Token) const {
    switch (Token.getType()) {
    case TT_FunctionLBrace:
    case TT_LambdaLBrace:
      return ST_Function;
    case TT_ClassLBrace:
    case TT_StructLBrace:
    case TT_UnionLBrace:
      return ST_Class;
    default:
      return ST_Other;
    }
  }

  bool parseAngle() {
    if (!CurrentToken || !CurrentToken->Previous)
      return false;
    if (NonTemplateLess.count(CurrentToken->Previous) > 0)
      return false;

    if (const auto &Previous = *CurrentToken->Previous; // The '<'.
        Previous.Previous) {
      if (Previous.Previous->Tok.isLiteral())
        return false;
      if (Previous.Previous->is(tok::r_brace))
        return false;
      if (Previous.Previous->is(tok::r_paren) && Contexts.size() > 1 &&
          (!Previous.Previous->MatchingParen ||
           Previous.Previous->MatchingParen->isNot(
               TT_OverloadedOperatorLParen))) {
        return false;
      }
      if (Previous.Previous->is(tok::kw_operator) &&
          CurrentToken->is(tok::l_paren)) {
        return false;
      }
    }

    FormatToken *Left = CurrentToken->Previous;
    Left->ParentBracket = Contexts.back().ContextKind;
    ScopedContextCreator ContextCreator(*this, tok::less, 12);
    Contexts.back().IsExpression = false;

    const auto *BeforeLess = Left->Previous;

    // If there's a template keyword before the opening angle bracket, this is a
    // template parameter, not an argument.
    if (BeforeLess && BeforeLess->isNot(tok::kw_template))
      Contexts.back().ContextType = Context::TemplateArgument;

    if (Style.Language == FormatStyle::LK_Java &&
        CurrentToken->is(tok::question)) {
      next();
    }

    for (bool SeenTernaryOperator = false, MaybeAngles = true; CurrentToken;) {
      const bool InExpr = Contexts[Contexts.size() - 2].IsExpression;
      if (CurrentToken->is(tok::greater)) {
        const auto *Next = CurrentToken->Next;
        if (CurrentToken->isNot(TT_TemplateCloser)) {
          // Try to do a better job at looking for ">>" within the condition of
          // a statement. Conservatively insert spaces between consecutive ">"
          // tokens to prevent splitting right shift operators and potentially
          // altering program semantics. This check is overly conservative and
          // will prevent spaces from being inserted in select nested template
          // parameter cases, but should not alter program semantics.
          if (Next && Next->is(tok::greater) &&
              Left->ParentBracket != tok::less &&
              CurrentToken->getStartOfNonWhitespace() ==
                  Next->getStartOfNonWhitespace().getLocWithOffset(-1)) {
            return false;
          }
          if (InExpr && SeenTernaryOperator &&
              (!Next || !Next->isOneOf(tok::l_paren, tok::l_brace))) {
            return false;
          }
          if (!MaybeAngles)
            return false;
        }
        Left->MatchingParen = CurrentToken;
        CurrentToken->MatchingParen = Left;
        // In TT_Proto, we must distignuish between:
        //   map<key, value>
        //   msg < item: data >
        //   msg: < item: data >
        // In TT_TextProto, map<key, value> does not occur.
        if (Style.Language == FormatStyle::LK_TextProto ||
            (Style.Language == FormatStyle::LK_Proto && BeforeLess &&
             BeforeLess->isOneOf(TT_SelectorName, TT_DictLiteral))) {
          CurrentToken->setType(TT_DictLiteral);
        } else {
          CurrentToken->setType(TT_TemplateCloser);
          CurrentToken->Tok.setLength(1);
        }
        if (Next && Next->Tok.isLiteral())
          return false;
        next();
        return true;
      }
      if (CurrentToken->is(tok::question) &&
          Style.Language == FormatStyle::LK_Java) {
        next();
        continue;
      }
      if (CurrentToken->isOneOf(tok::r_paren, tok::r_square, tok::r_brace))
        return false;
      const auto &Prev = *CurrentToken->Previous;
      // If a && or || is found and interpreted as a binary operator, this set
      // of angles is likely part of something like "a < b && c > d". If the
      // angles are inside an expression, the ||/&& might also be a binary
      // operator that was misinterpreted because we are parsing template
      // parameters.
      // FIXME: This is getting out of hand, write a decent parser.
      if (MaybeAngles && InExpr && !Line.startsWith(tok::kw_template) &&
          Prev.is(TT_BinaryOperator)) {
        const auto Precedence = Prev.getPrecedence();
        if (Precedence > prec::Conditional && Precedence < prec::Relational)
          MaybeAngles = false;
      }
      if (Prev.isOneOf(tok::question, tok::colon) && !Style.isProto())
        SeenTernaryOperator = true;
      updateParameterCount(Left, CurrentToken);
      if (Style.Language == FormatStyle::LK_Proto) {
        if (FormatToken *Previous = CurrentToken->getPreviousNonComment()) {
          if (CurrentToken->is(tok::colon) ||
              (CurrentToken->isOneOf(tok::l_brace, tok::less) &&
               Previous->isNot(tok::colon))) {
            Previous->setType(TT_SelectorName);
          }
        }
      }
      if (Style.isTableGen()) {
        if (CurrentToken->isOneOf(tok::comma, tok::equal)) {
          // They appear as separators. Unless they are not in class definition.
          next();
          continue;
        }
        // In angle, there must be Value like tokens. Types are also able to be
        // parsed in the same way with Values.
        if (!parseTableGenValue())
          return false;
        continue;
      }
      if (!consumeToken())
        return false;
    }
    return false;
  }

  bool parseUntouchableParens() {
    while (CurrentToken) {
      CurrentToken->Finalized = true;
      switch (CurrentToken->Tok.getKind()) {
      case tok::l_paren:
        next();
        if (!parseUntouchableParens())
          return false;
        continue;
      case tok::r_paren:
        next();
        return true;
      default:
        // no-op
        break;
      }
      next();
    }
    return false;
  }

  bool parseParens(bool LookForDecls = false) {
    if (!CurrentToken)
      return false;
    assert(CurrentToken->Previous && "Unknown previous token");
    FormatToken &OpeningParen = *CurrentToken->Previous;
    assert(OpeningParen.is(tok::l_paren));
    FormatToken *PrevNonComment = OpeningParen.getPreviousNonComment();
    OpeningParen.ParentBracket = Contexts.back().ContextKind;
    ScopedContextCreator ContextCreator(*this, tok::l_paren, 1);

    // FIXME: This is a bit of a hack. Do better.
    Contexts.back().ColonIsForRangeExpr =
        Contexts.size() == 2 && Contexts[0].ColonIsForRangeExpr;

    if (OpeningParen.Previous &&
        OpeningParen.Previous->is(TT_UntouchableMacroFunc)) {
      OpeningParen.Finalized = true;
      return parseUntouchableParens();
    }

    bool StartsObjCMethodExpr = false;
    if (!Style.isVerilog()) {
      if (FormatToken *MaybeSel = OpeningParen.Previous) {
        // @selector( starts a selector.
        if (MaybeSel->isObjCAtKeyword(tok::objc_selector) &&
            MaybeSel->Previous && MaybeSel->Previous->is(tok::at)) {
          StartsObjCMethodExpr = true;
        }
      }
    }

    if (OpeningParen.is(TT_OverloadedOperatorLParen)) {
      // Find the previous kw_operator token.
      FormatToken *Prev = &OpeningParen;
      while (Prev->isNot(tok::kw_operator)) {
        Prev = Prev->Previous;
        assert(Prev && "Expect a kw_operator prior to the OperatorLParen!");
      }

      // If faced with "a.operator*(argument)" or "a->operator*(argument)",
      // i.e. the operator is called as a member function,
      // then the argument must be an expression.
      bool OperatorCalledAsMemberFunction =
          Prev->Previous && Prev->Previous->isOneOf(tok::period, tok::arrow);
      Contexts.back().IsExpression = OperatorCalledAsMemberFunction;
    } else if (OpeningParen.is(TT_VerilogInstancePortLParen)) {
      Contexts.back().IsExpression = true;
      Contexts.back().ContextType = Context::VerilogInstancePortList;
    } else if (Style.isJavaScript() &&
               (Line.startsWith(Keywords.kw_type, tok::identifier) ||
                Line.startsWith(tok::kw_export, Keywords.kw_type,
                                tok::identifier))) {
      // type X = (...);
      // export type X = (...);
      Contexts.back().IsExpression = false;
    } else if (OpeningParen.Previous &&
               (OpeningParen.Previous->isOneOf(
                    tok::kw_static_assert, tok::kw_noexcept, tok::kw_explicit,
                    tok::kw_while, tok::l_paren, tok::comma,
                    TT_BinaryOperator) ||
                OpeningParen.Previous->isIf())) {
      // static_assert, if and while usually contain expressions.
      Contexts.back().IsExpression = true;
    } else if (Style.isJavaScript() && OpeningParen.Previous &&
               (OpeningParen.Previous->is(Keywords.kw_function) ||
                (OpeningParen.Previous->endsSequence(tok::identifier,
                                                     Keywords.kw_function)))) {
      // function(...) or function f(...)
      Contexts.back().IsExpression = false;
    } else if (Style.isJavaScript() && OpeningParen.Previous &&
               OpeningParen.Previous->is(TT_JsTypeColon)) {
      // let x: (SomeType);
      Contexts.back().IsExpression = false;
    } else if (isLambdaParameterList(&OpeningParen)) {
      // This is a parameter list of a lambda expression.
      Contexts.back().IsExpression = false;
    } else if (OpeningParen.is(TT_RequiresExpressionLParen)) {
      Contexts.back().IsExpression = false;
    } else if (OpeningParen.Previous &&
               OpeningParen.Previous->is(tok::kw__Generic)) {
      Contexts.back().ContextType = Context::C11GenericSelection;
      Contexts.back().IsExpression = true;
    } else if (Line.InPPDirective &&
               (!OpeningParen.Previous ||
                OpeningParen.Previous->isNot(tok::identifier))) {
      Contexts.back().IsExpression = true;
    } else if (Contexts[Contexts.size() - 2].CaretFound) {
      // This is the parameter list of an ObjC block.
      Contexts.back().IsExpression = false;
    } else if (OpeningParen.Previous &&
               OpeningParen.Previous->is(TT_ForEachMacro)) {
      // The first argument to a foreach macro is a declaration.
      Contexts.back().ContextType = Context::ForEachMacro;
      Contexts.back().IsExpression = false;
    } else if (OpeningParen.Previous && OpeningParen.Previous->MatchingParen &&
               OpeningParen.Previous->MatchingParen->isOneOf(
                   TT_ObjCBlockLParen, TT_FunctionTypeLParen)) {
      Contexts.back().IsExpression = false;
    } else if (!Line.MustBeDeclaration && !Line.InPPDirective) {
      bool IsForOrCatch =
          OpeningParen.Previous &&
          OpeningParen.Previous->isOneOf(tok::kw_for, tok::kw_catch);
      Contexts.back().IsExpression = !IsForOrCatch;
    }

    if (Style.isTableGen()) {
      if (FormatToken *Prev = OpeningParen.Previous) {
        if (Prev->is(TT_TableGenCondOperator)) {
          Contexts.back().IsTableGenCondOpe = true;
          Contexts.back().IsExpression = true;
        } else if (Contexts.size() > 1 &&
                   Contexts[Contexts.size() - 2].IsTableGenBangOpe) {
          // Hack to handle bang operators. The parent context's flag
          // was set by parseTableGenSimpleValue().
          // We have to specify the context outside because the prev of "(" may
          // be ">", not the bang operator in this case.
          Contexts.back().IsTableGenBangOpe = true;
          Contexts.back().IsExpression = true;
        } else {
          // Otherwise, this paren seems DAGArg.
          if (!parseTableGenDAGArg())
            return false;
          return parseTableGenDAGArgAndList(&OpeningParen);
        }
      }
    }

    // Infer the role of the l_paren based on the previous token if we haven't
    // detected one yet.
    if (PrevNonComment && OpeningParen.is(TT_Unknown)) {
      if (PrevNonComment->isAttribute()) {
        OpeningParen.setType(TT_AttributeLParen);
      } else if (PrevNonComment->isOneOf(TT_TypenameMacro, tok::kw_decltype,
                                         tok::kw_typeof,
#define TRANSFORM_TYPE_TRAIT_DEF(_, Trait) tok::kw___##Trait,
#include "clang/Basic/TransformTypeTraits.def"
                                         tok::kw__Atomic)) {
        OpeningParen.setType(TT_TypeDeclarationParen);
        // decltype() and typeof() usually contain expressions.
        if (PrevNonComment->isOneOf(tok::kw_decltype, tok::kw_typeof))
          Contexts.back().IsExpression = true;
      }
    }

    if (StartsObjCMethodExpr) {
      Contexts.back().ColonIsObjCMethodExpr = true;
      OpeningParen.setType(TT_ObjCMethodExpr);
    }

    // MightBeFunctionType and ProbablyFunctionType are used for
    // function pointer and reference types as well as Objective-C
    // block types:
    //
    // void (*FunctionPointer)(void);
    // void (&FunctionReference)(void);
    // void (&&FunctionReference)(void);
    // void (^ObjCBlock)(void);
    bool MightBeFunctionType = !Contexts[Contexts.size() - 2].IsExpression;
    bool ProbablyFunctionType =
        CurrentToken->isPointerOrReference() || CurrentToken->is(tok::caret);
    bool HasMultipleLines = false;
    bool HasMultipleParametersOnALine = false;
    bool MightBeObjCForRangeLoop =
        OpeningParen.Previous && OpeningParen.Previous->is(tok::kw_for);
    FormatToken *PossibleObjCForInToken = nullptr;
    while (CurrentToken) {
      // LookForDecls is set when "if (" has been seen. Check for
      // 'identifier' '*' 'identifier' followed by not '=' -- this
      // '*' has to be a binary operator but determineStarAmpUsage() will
      // categorize it as an unary operator, so set the right type here.
      if (LookForDecls && CurrentToken->Next) {
        FormatToken *Prev = CurrentToken->getPreviousNonComment();
        if (Prev) {
          FormatToken *PrevPrev = Prev->getPreviousNonComment();
          FormatToken *Next = CurrentToken->Next;
          if (PrevPrev && PrevPrev->is(tok::identifier) &&
              PrevPrev->isNot(TT_TypeName) && Prev->isPointerOrReference() &&
              CurrentToken->is(tok::identifier) && Next->isNot(tok::equal)) {
            Prev->setType(TT_BinaryOperator);
            LookForDecls = false;
          }
        }
      }

      if (CurrentToken->Previous->is(TT_PointerOrReference) &&
          CurrentToken->Previous->Previous->isOneOf(tok::l_paren,
                                                    tok::coloncolon)) {
        ProbablyFunctionType = true;
      }
      if (CurrentToken->is(tok::comma))
        MightBeFunctionType = false;
      if (CurrentToken->Previous->is(TT_BinaryOperator))
        Contexts.back().IsExpression = true;
      if (CurrentToken->is(tok::r_paren)) {
        if (OpeningParen.isNot(TT_CppCastLParen) && MightBeFunctionType &&
            ProbablyFunctionType && CurrentToken->Next &&
            (CurrentToken->Next->is(tok::l_paren) ||
             (CurrentToken->Next->is(tok::l_square) &&
              Line.MustBeDeclaration))) {
          OpeningParen.setType(OpeningParen.Next->is(tok::caret)
                                   ? TT_ObjCBlockLParen
                                   : TT_FunctionTypeLParen);
        }
        OpeningParen.MatchingParen = CurrentToken;
        CurrentToken->MatchingParen = &OpeningParen;

        if (CurrentToken->Next && CurrentToken->Next->is(tok::l_brace) &&
            OpeningParen.Previous && OpeningParen.Previous->is(tok::l_paren)) {
          // Detect the case where macros are used to generate lambdas or
          // function bodies, e.g.:
          //   auto my_lambda = MACRO((Type *type, int i) { .. body .. });
          for (FormatToken *Tok = &OpeningParen; Tok != CurrentToken;
               Tok = Tok->Next) {
            if (Tok->is(TT_BinaryOperator) && Tok->isPointerOrReference())
              Tok->setType(TT_PointerOrReference);
          }
        }

        if (StartsObjCMethodExpr) {
          CurrentToken->setType(TT_ObjCMethodExpr);
          if (Contexts.back().FirstObjCSelectorName) {
            Contexts.back().FirstObjCSelectorName->LongestObjCSelectorName =
                Contexts.back().LongestObjCSelectorName;
          }
        }

        if (OpeningParen.is(TT_AttributeLParen))
          CurrentToken->setType(TT_AttributeRParen);
        if (OpeningParen.is(TT_TypeDeclarationParen))
          CurrentToken->setType(TT_TypeDeclarationParen);
        if (OpeningParen.Previous &&
            OpeningParen.Previous->is(TT_JavaAnnotation)) {
          CurrentToken->setType(TT_JavaAnnotation);
        }
        if (OpeningParen.Previous &&
            OpeningParen.Previous->is(TT_LeadingJavaAnnotation)) {
          CurrentToken->setType(TT_LeadingJavaAnnotation);
        }
        if (OpeningParen.Previous &&
            OpeningParen.Previous->is(TT_AttributeSquare)) {
          CurrentToken->setType(TT_AttributeSquare);
        }

        if (!HasMultipleLines)
          OpeningParen.setPackingKind(PPK_Inconclusive);
        else if (HasMultipleParametersOnALine)
          OpeningParen.setPackingKind(PPK_BinPacked);
        else
          OpeningParen.setPackingKind(PPK_OnePerLine);

        next();
        return true;
      }
      if (CurrentToken->isOneOf(tok::r_square, tok::r_brace))
        return false;

      if (CurrentToken->is(tok::l_brace) && OpeningParen.is(TT_ObjCBlockLParen))
        OpeningParen.setType(TT_Unknown);
      if (CurrentToken->is(tok::comma) && CurrentToken->Next &&
          !CurrentToken->Next->HasUnescapedNewline &&
          !CurrentToken->Next->isTrailingComment()) {
        HasMultipleParametersOnALine = true;
      }
      bool ProbablyFunctionTypeLParen =
          (CurrentToken->is(tok::l_paren) && CurrentToken->Next &&
           CurrentToken->Next->isOneOf(tok::star, tok::amp, tok::caret));
      if ((CurrentToken->Previous->isOneOf(tok::kw_const, tok::kw_auto) ||
           CurrentToken->Previous->isTypeName(LangOpts)) &&
          !(CurrentToken->is(tok::l_brace) ||
            (CurrentToken->is(tok::l_paren) && !ProbablyFunctionTypeLParen))) {
        Contexts.back().IsExpression = false;
      }
      if (CurrentToken->isOneOf(tok::semi, tok::colon)) {
        MightBeObjCForRangeLoop = false;
        if (PossibleObjCForInToken) {
          PossibleObjCForInToken->setType(TT_Unknown);
          PossibleObjCForInToken = nullptr;
        }
      }
      if (MightBeObjCForRangeLoop && CurrentToken->is(Keywords.kw_in)) {
        PossibleObjCForInToken = CurrentToken;
        PossibleObjCForInToken->setType(TT_ObjCForIn);
      }
      // When we discover a 'new', we set CanBeExpression to 'false' in order to
      // parse the type correctly. Reset that after a comma.
      if (CurrentToken->is(tok::comma))
        Contexts.back().CanBeExpression = true;

      if (Style.isTableGen()) {
        if (CurrentToken->is(tok::comma)) {
          if (Contexts.back().IsTableGenCondOpe)
            CurrentToken->setType(TT_TableGenCondOperatorComma);
          next();
        } else if (CurrentToken->is(tok::colon)) {
          if (Contexts.back().IsTableGenCondOpe)
            CurrentToken->setType(TT_TableGenCondOperatorColon);
          next();
        }
        // In TableGen there must be Values in parens.
        if (!parseTableGenValue())
          return false;
        continue;
      }

      FormatToken *Tok = CurrentToken;
      if (!consumeToken())
        return false;
      updateParameterCount(&OpeningParen, Tok);
      if (CurrentToken && CurrentToken->HasUnescapedNewline)
        HasMultipleLines = true;
    }
    return false;
  }

  bool isCSharpAttributeSpecifier(const FormatToken &Tok) {
    if (!Style.isCSharp())
      return false;

    // `identifier[i]` is not an attribute.
    if (Tok.Previous && Tok.Previous->is(tok::identifier))
      return false;

    // Chains of [] in `identifier[i][j][k]` are not attributes.
    if (Tok.Previous && Tok.Previous->is(tok::r_square)) {
      auto *MatchingParen = Tok.Previous->MatchingParen;
      if (!MatchingParen || MatchingParen->is(TT_ArraySubscriptLSquare))
        return false;
    }

    const FormatToken *AttrTok = Tok.Next;
    if (!AttrTok)
      return false;

    // Just an empty declaration e.g. string [].
    if (AttrTok->is(tok::r_square))
      return false;

    // Move along the tokens inbetween the '[' and ']' e.g. [STAThread].
    while (AttrTok && AttrTok->isNot(tok::r_square))
      AttrTok = AttrTok->Next;

    if (!AttrTok)
      return false;

    // Allow an attribute to be the only content of a file.
    AttrTok = AttrTok->Next;
    if (!AttrTok)
      return true;

    // Limit this to being an access modifier that follows.
    if (AttrTok->isAccessSpecifierKeyword() ||
        AttrTok->isOneOf(tok::comment, tok::kw_class, tok::kw_static,
                         tok::l_square, Keywords.kw_internal)) {
      return true;
    }

    // incase its a [XXX] retval func(....
    if (AttrTok->Next &&
        AttrTok->Next->startsSequence(tok::identifier, tok::l_paren)) {
      return true;
    }

    return false;
  }

  bool parseSquare() {
    if (!CurrentToken)
      return false;

    // A '[' could be an index subscript (after an identifier or after
    // ')' or ']'), it could be the start of an Objective-C method
    // expression, it could the start of an Objective-C array literal,
    // or it could be a C++ attribute specifier [[foo::bar]].
    FormatToken *Left = CurrentToken->Previous;
    Left->ParentBracket = Contexts.back().ContextKind;
    FormatToken *Parent = Left->getPreviousNonComment();

    // Cases where '>' is followed by '['.
    // In C++, this can happen either in array of templates (foo<int>[10])
    // or when array is a nested template type (unique_ptr<type1<type2>[]>).
    bool CppArrayTemplates =
        IsCpp && Parent && Parent->is(TT_TemplateCloser) &&
        (Contexts.back().CanBeExpression || Contexts.back().IsExpression ||
         Contexts.back().ContextType == Context::TemplateArgument);

    const bool IsInnerSquare = Contexts.back().InCpp11AttributeSpecifier;
    const bool IsCpp11AttributeSpecifier =
        isCppAttribute(IsCpp, *Left) || IsInnerSquare;

    // Treat C# Attributes [STAThread] much like C++ attributes [[...]].
    bool IsCSharpAttributeSpecifier =
        isCSharpAttributeSpecifier(*Left) ||
        Contexts.back().InCSharpAttributeSpecifier;

    bool InsideInlineASM = Line.startsWith(tok::kw_asm);
    bool IsCppStructuredBinding = Left->isCppStructuredBinding(IsCpp);
    bool StartsObjCMethodExpr =
        !IsCppStructuredBinding && !InsideInlineASM && !CppArrayTemplates &&
        IsCpp && !IsCpp11AttributeSpecifier && !IsCSharpAttributeSpecifier &&
        Contexts.back().CanBeExpression && Left->isNot(TT_LambdaLSquare) &&
        !CurrentToken->isOneOf(tok::l_brace, tok::r_square) &&
        (!Parent ||
         Parent->isOneOf(tok::colon, tok::l_square, tok::l_paren,
                         tok::kw_return, tok::kw_throw) ||
         Parent->isUnaryOperator() ||
         // FIXME(bug 36976): ObjC return types shouldn't use TT_CastRParen.
         Parent->isOneOf(TT_ObjCForIn, TT_CastRParen) ||
         (getBinOpPrecedence(Parent->Tok.getKind(), true, true) >
          prec::Unknown));
    bool ColonFound = false;

    unsigned BindingIncrease = 1;
    if (IsCppStructuredBinding) {
      Left->setType(TT_StructuredBindingLSquare);
    } else if (Left->is(TT_Unknown)) {
      if (StartsObjCMethodExpr) {
        Left->setType(TT_ObjCMethodExpr);
      } else if (InsideInlineASM) {
        Left->setType(TT_InlineASMSymbolicNameLSquare);
      } else if (IsCpp11AttributeSpecifier) {
        Left->setType(TT_AttributeSquare);
        if (!IsInnerSquare && Left->Previous)
          Left->Previous->EndsCppAttributeGroup = false;
      } else if (Style.isJavaScript() && Parent &&
                 Contexts.back().ContextKind == tok::l_brace &&
                 Parent->isOneOf(tok::l_brace, tok::comma)) {
        Left->setType(TT_JsComputedPropertyName);
      } else if (IsCpp && Contexts.back().ContextKind == tok::l_brace &&
                 Parent && Parent->isOneOf(tok::l_brace, tok::comma)) {
        Left->setType(TT_DesignatedInitializerLSquare);
      } else if (IsCSharpAttributeSpecifier) {
        Left->setType(TT_AttributeSquare);
      } else if (CurrentToken->is(tok::r_square) && Parent &&
                 Parent->is(TT_TemplateCloser)) {
        Left->setType(TT_ArraySubscriptLSquare);
      } else if (Style.isProto()) {
        // Square braces in LK_Proto can either be message field attributes:
        //
        // optional Aaa aaa = 1 [
        //   (aaa) = aaa
        // ];
        //
        // extensions 123 [
        //   (aaa) = aaa
        // ];
        //
        // or text proto extensions (in options):
        //
        // option (Aaa.options) = {
        //   [type.type/type] {
        //     key: value
        //   }
        // }
        //
        // or repeated fields (in options):
        //
        // option (Aaa.options) = {
        //   keys: [ 1, 2, 3 ]
        // }
        //
        // In the first and the third case we want to spread the contents inside
        // the square braces; in the second we want to keep them inline.
        Left->setType(TT_ArrayInitializerLSquare);
        if (!Left->endsSequence(tok::l_square, tok::numeric_constant,
                                tok::equal) &&
            !Left->endsSequence(tok::l_square, tok::numeric_constant,
                                tok::identifier) &&
            !Left->endsSequence(tok::l_square, tok::colon, TT_SelectorName)) {
          Left->setType(TT_ProtoExtensionLSquare);
          BindingIncrease = 10;
        }
      } else if (!CppArrayTemplates && Parent &&
                 Parent->isOneOf(TT_BinaryOperator, TT_TemplateCloser, tok::at,
                                 tok::comma, tok::l_paren, tok::l_square,
                                 tok::question, tok::colon, tok::kw_return,
                                 // Should only be relevant to JavaScript:
                                 tok::kw_default)) {
        Left->setType(TT_ArrayInitializerLSquare);
      } else {
        BindingIncrease = 10;
        Left->setType(TT_ArraySubscriptLSquare);
      }
    }

    ScopedContextCreator ContextCreator(*this, tok::l_square, BindingIncrease);
    Contexts.back().IsExpression = true;
    if (Style.isJavaScript() && Parent && Parent->is(TT_JsTypeColon))
      Contexts.back().IsExpression = false;

    Contexts.back().ColonIsObjCMethodExpr = StartsObjCMethodExpr;
    Contexts.back().InCpp11AttributeSpecifier = IsCpp11AttributeSpecifier;
    Contexts.back().InCSharpAttributeSpecifier = IsCSharpAttributeSpecifier;

    while (CurrentToken) {
      if (CurrentToken->is(tok::r_square)) {
        if (IsCpp11AttributeSpecifier) {
          CurrentToken->setType(TT_AttributeSquare);
          if (!IsInnerSquare)
            CurrentToken->EndsCppAttributeGroup = true;
        }
        if (IsCSharpAttributeSpecifier) {
          CurrentToken->setType(TT_AttributeSquare);
        } else if (((CurrentToken->Next &&
                     CurrentToken->Next->is(tok::l_paren)) ||
                    (CurrentToken->Previous &&
                     CurrentToken->Previous->Previous == Left)) &&
                   Left->is(TT_ObjCMethodExpr)) {
          // An ObjC method call is rarely followed by an open parenthesis. It
          // also can't be composed of just one token, unless it's a macro that
          // will be expanded to more tokens.
          // FIXME: Do we incorrectly label ":" with this?
          StartsObjCMethodExpr = false;
          Left->setType(TT_Unknown);
        }
        if (StartsObjCMethodExpr && CurrentToken->Previous != Left) {
          CurrentToken->setType(TT_ObjCMethodExpr);
          // If we haven't seen a colon yet, make sure the last identifier
          // before the r_square is tagged as a selector name component.
          if (!ColonFound && CurrentToken->Previous &&
              CurrentToken->Previous->is(TT_Unknown) &&
              canBeObjCSelectorComponent(*CurrentToken->Previous)) {
            CurrentToken->Previous->setType(TT_SelectorName);
          }
          // determineStarAmpUsage() thinks that '*' '[' is allocating an
          // array of pointers, but if '[' starts a selector then '*' is a
          // binary operator.
          if (Parent && Parent->is(TT_PointerOrReference))
            Parent->overwriteFixedType(TT_BinaryOperator);
        }
        // An arrow after an ObjC method expression is not a lambda arrow.
        if (CurrentToken->is(TT_ObjCMethodExpr) && CurrentToken->Next &&
            CurrentToken->Next->is(TT_LambdaArrow)) {
          CurrentToken->Next->overwriteFixedType(TT_Unknown);
        }
        Left->MatchingParen = CurrentToken;
        CurrentToken->MatchingParen = Left;
        // FirstObjCSelectorName is set when a colon is found. This does
        // not work, however, when the method has no parameters.
        // Here, we set FirstObjCSelectorName when the end of the method call is
        // reached, in case it was not set already.
        if (!Contexts.back().FirstObjCSelectorName) {
          FormatToken *Previous = CurrentToken->getPreviousNonComment();
          if (Previous && Previous->is(TT_SelectorName)) {
            Previous->ObjCSelectorNameParts = 1;
            Contexts.back().FirstObjCSelectorName = Previous;
          }
        } else {
          Left->ParameterCount =
              Contexts.back().FirstObjCSelectorName->ObjCSelectorNameParts;
        }
        if (Contexts.back().FirstObjCSelectorName) {
          Contexts.back().FirstObjCSelectorName->LongestObjCSelectorName =
              Contexts.back().LongestObjCSelectorName;
          if (Left->BlockParameterCount > 1)
            Contexts.back().FirstObjCSelectorName->LongestObjCSelectorName = 0;
        }
        if (Style.isTableGen() && Left->is(TT_TableGenListOpener))
          CurrentToken->setType(TT_TableGenListCloser);
        next();
        return true;
      }
      if (CurrentToken->isOneOf(tok::r_paren, tok::r_brace))
        return false;
      if (CurrentToken->is(tok::colon)) {
        if (IsCpp11AttributeSpecifier &&
            CurrentToken->endsSequence(tok::colon, tok::identifier,
                                       tok::kw_using)) {
          // Remember that this is a [[using ns: foo]] C++ attribute, so we
          // don't add a space before the colon (unlike other colons).
          CurrentToken->setType(TT_AttributeColon);
        } else if (!Style.isVerilog() && !Line.InPragmaDirective &&
                   Left->isOneOf(TT_ArraySubscriptLSquare,
                                 TT_DesignatedInitializerLSquare)) {
          Left->setType(TT_ObjCMethodExpr);
          StartsObjCMethodExpr = true;
          Contexts.back().ColonIsObjCMethodExpr = true;
          if (Parent && Parent->is(tok::r_paren)) {
            // FIXME(bug 36976): ObjC return types shouldn't use TT_CastRParen.
            Parent->setType(TT_CastRParen);
          }
        }
        ColonFound = true;
      }
      if (CurrentToken->is(tok::comma) && Left->is(TT_ObjCMethodExpr) &&
          !ColonFound) {
        Left->setType(TT_ArrayInitializerLSquare);
      }
      FormatToken *Tok = CurrentToken;
      if (Style.isTableGen()) {
        if (CurrentToken->isOneOf(tok::comma, tok::minus, tok::ellipsis)) {
          // '-' and '...' appears as a separator in slice.
          next();
        } else {
          // In TableGen there must be a list of Values in square brackets.
          // It must be ValueList or SliceElements.
          if (!parseTableGenValue())
            return false;
        }
        updateParameterCount(Left, Tok);
        continue;
      }
      if (!consumeToken())
        return false;
      updateParameterCount(Left, Tok);
    }
    return false;
  }

  void skipToNextNonComment() {
    next();
    while (CurrentToken && CurrentToken->is(tok::comment))
      next();
  }

  // Simplified parser for TableGen Value. Returns true on success.
  // It consists of SimpleValues, SimpleValues with Suffixes, and Value followed
  // by '#', paste operator.
  // There also exists the case the Value is parsed as NameValue.
  // In this case, the Value ends if '{' is found.
  bool parseTableGenValue(bool ParseNameMode = false) {
    if (!CurrentToken)
      return false;
    while (CurrentToken->is(tok::comment))
      next();
    if (!parseTableGenSimpleValue())
      return false;
    if (!CurrentToken)
      return true;
    // Value "#" [Value]
    if (CurrentToken->is(tok::hash)) {
      if (CurrentToken->Next &&
          CurrentToken->Next->isOneOf(tok::colon, tok::semi, tok::l_brace)) {
        // Trailing paste operator.
        // These are only the allowed cases in TGParser::ParseValue().
        CurrentToken->setType(TT_TableGenTrailingPasteOperator);
        next();
        return true;
      }
      FormatToken *HashTok = CurrentToken;
      skipToNextNonComment();
      HashTok->setType(TT_Unknown);
      if (!parseTableGenValue(ParseNameMode))
        return false;
    }
    // In name mode, '{' is regarded as the end of the value.
    // See TGParser::ParseValue in TGParser.cpp
    if (ParseNameMode && CurrentToken->is(tok::l_brace))
      return true;
    // These tokens indicates this is a value with suffixes.
    if (CurrentToken->isOneOf(tok::l_brace, tok::l_square, tok::period)) {
      CurrentToken->setType(TT_TableGenValueSuffix);
      FormatToken *Suffix = CurrentToken;
      skipToNextNonComment();
      if (Suffix->is(tok::l_square))
        return parseSquare();
      if (Suffix->is(tok::l_brace)) {
        Scopes.push_back(getScopeType(*Suffix));
        return parseBrace();
      }
    }
    return true;
  }

  // TokVarName    ::=  "$" ualpha (ualpha |  "0"..."9")*
  // Appears as a part of DagArg.
  // This does not change the current token on fail.
  bool tryToParseTableGenTokVar() {
    if (!CurrentToken)
      return false;
    if (CurrentToken->is(tok::identifier) &&
        CurrentToken->TokenText.front() == '$') {
      skipToNextNonComment();
      return true;
    }
    return false;
  }

  // DagArg       ::=  Value [":" TokVarName] | TokVarName
  // Appears as a part of SimpleValue6.
  bool parseTableGenDAGArg(bool AlignColon = false) {
    if (tryToParseTableGenTokVar())
      return true;
    if (parseTableGenValue()) {
      if (CurrentToken && CurrentToken->is(tok::colon)) {
        if (AlignColon)
          CurrentToken->setType(TT_TableGenDAGArgListColonToAlign);
        else
          CurrentToken->setType(TT_TableGenDAGArgListColon);
        skipToNextNonComment();
        return tryToParseTableGenTokVar();
      }
      return true;
    }
    return false;
  }

  // Judge if the token is a operator ID to insert line break in DAGArg.
  // That is, TableGenBreakingDAGArgOperators is empty (by the definition of the
  // option) or the token is in the list.
  bool isTableGenDAGArgBreakingOperator(const FormatToken &Tok) {
    auto &Opes = Style.TableGenBreakingDAGArgOperators;
    // If the list is empty, all operators are breaking operators.
    if (Opes.empty())
      return true;
    // Otherwise, the operator is limited to normal identifiers.
    if (Tok.isNot(tok::identifier) ||
        Tok.isOneOf(TT_TableGenBangOperator, TT_TableGenCondOperator)) {
      return false;
    }
    // The case next is colon, it is not a operator of identifier.
    if (!Tok.Next || Tok.Next->is(tok::colon))
      return false;
    return std::find(Opes.begin(), Opes.end(), Tok.TokenText.str()) !=
           Opes.end();
  }

  // SimpleValue6 ::=  "(" DagArg [DagArgList] ")"
  // This parses SimpleValue 6's inside part of "(" ")"
  bool parseTableGenDAGArgAndList(FormatToken *Opener) {
    FormatToken *FirstTok = CurrentToken;
    if (!parseTableGenDAGArg())
      return false;
    bool BreakInside = false;
    if (Style.TableGenBreakInsideDAGArg != FormatStyle::DAS_DontBreak) {
      // Specialized detection for DAGArgOperator, that determines the way of
      // line break for this DAGArg elements.
      if (isTableGenDAGArgBreakingOperator(*FirstTok)) {
        // Special case for identifier DAGArg operator.
        BreakInside = true;
        Opener->setType(TT_TableGenDAGArgOpenerToBreak);
        if (FirstTok->isOneOf(TT_TableGenBangOperator,
                              TT_TableGenCondOperator)) {
          // Special case for bang/cond operators. Set the whole operator as
          // the DAGArg operator. Always break after it.
          CurrentToken->Previous->setType(TT_TableGenDAGArgOperatorToBreak);
        } else if (FirstTok->is(tok::identifier)) {
          if (Style.TableGenBreakInsideDAGArg == FormatStyle::DAS_BreakAll)
            FirstTok->setType(TT_TableGenDAGArgOperatorToBreak);
          else
            FirstTok->setType(TT_TableGenDAGArgOperatorID);
        }
      }
    }
    // Parse the [DagArgList] part
    bool FirstDAGArgListElm = true;
    while (CurrentToken) {
      if (!FirstDAGArgListElm && CurrentToken->is(tok::comma)) {
        CurrentToken->setType(BreakInside ? TT_TableGenDAGArgListCommaToBreak
                                          : TT_TableGenDAGArgListComma);
        skipToNextNonComment();
      }
      if (CurrentToken && CurrentToken->is(tok::r_paren)) {
        CurrentToken->setType(TT_TableGenDAGArgCloser);
        Opener->MatchingParen = CurrentToken;
        CurrentToken->MatchingParen = Opener;
        skipToNextNonComment();
        return true;
      }
      if (!parseTableGenDAGArg(
              BreakInside &&
              Style.AlignConsecutiveTableGenBreakingDAGArgColons.Enabled)) {
        return false;
      }
      FirstDAGArgListElm = false;
    }
    return false;
  }

  bool parseTableGenSimpleValue() {
    assert(Style.isTableGen());
    if (!CurrentToken)
      return false;
    FormatToken *Tok = CurrentToken;
    skipToNextNonComment();
    // SimpleValue 1, 2, 3: Literals
    if (Tok->isOneOf(tok::numeric_constant, tok::string_literal,
                     TT_TableGenMultiLineString, tok::kw_true, tok::kw_false,
                     tok::question, tok::kw_int)) {
      return true;
    }
    // SimpleValue 4: ValueList, Type
    if (Tok->is(tok::l_brace)) {
      Scopes.push_back(getScopeType(*Tok));
      return parseBrace();
    }
    // SimpleValue 5: List initializer
    if (Tok->is(tok::l_square)) {
      Tok->setType(TT_TableGenListOpener);
      if (!parseSquare())
        return false;
      if (Tok->is(tok::less)) {
        CurrentToken->setType(TT_TemplateOpener);
        return parseAngle();
      }
      return true;
    }
    // SimpleValue 6: DAGArg [DAGArgList]
    // SimpleValue6 ::=  "(" DagArg [DagArgList] ")"
    if (Tok->is(tok::l_paren)) {
      Tok->setType(TT_TableGenDAGArgOpener);
      return parseTableGenDAGArgAndList(Tok);
    }
    // SimpleValue 9: Bang operator
    if (Tok->is(TT_TableGenBangOperator)) {
      if (CurrentToken && CurrentToken->is(tok::less)) {
        CurrentToken->setType(TT_TemplateOpener);
        skipToNextNonComment();
        if (!parseAngle())
          return false;
      }
      if (!CurrentToken || CurrentToken->isNot(tok::l_paren))
        return false;
      skipToNextNonComment();
      // FIXME: Hack using inheritance to child context
      Contexts.back().IsTableGenBangOpe = true;
      bool Result = parseParens();
      Contexts.back().IsTableGenBangOpe = false;
      return Result;
    }
    // SimpleValue 9: Cond operator
    if (Tok->is(TT_TableGenCondOperator)) {
      Tok = CurrentToken;
      skipToNextNonComment();
      if (!Tok || Tok->isNot(tok::l_paren))
        return false;
      bool Result = parseParens();
      return Result;
    }
    // We have to check identifier at the last because the kind of bang/cond
    // operators are also identifier.
    // SimpleValue 7: Identifiers
    if (Tok->is(tok::identifier)) {
      // SimpleValue 8: Anonymous record
      if (CurrentToken && CurrentToken->is(tok::less)) {
        CurrentToken->setType(TT_TemplateOpener);
        skipToNextNonComment();
        return parseAngle();
      }
      return true;
    }

    return false;
  }

  bool couldBeInStructArrayInitializer() const {
    if (Contexts.size() < 2)
      return false;
    // We want to back up no more then 2 context levels i.e.
    // . { { <-
    const auto End = std::next(Contexts.rbegin(), 2);
    auto Last = Contexts.rbegin();
    unsigned Depth = 0;
    for (; Last != End; ++Last)
      if (Last->ContextKind == tok::l_brace)
        ++Depth;
    return Depth == 2 && Last->ContextKind != tok::l_brace;
  }

  bool parseBrace() {
    if (!CurrentToken)
      return true;

    assert(CurrentToken->Previous);
    FormatToken &OpeningBrace = *CurrentToken->Previous;
    assert(OpeningBrace.is(tok::l_brace));
    OpeningBrace.ParentBracket = Contexts.back().ContextKind;

    if (Contexts.back().CaretFound)
      OpeningBrace.overwriteFixedType(TT_ObjCBlockLBrace);
    Contexts.back().CaretFound = false;

    ScopedContextCreator ContextCreator(*this, tok::l_brace, 1);
    Contexts.back().ColonIsDictLiteral = true;
    if (OpeningBrace.is(BK_BracedInit))
      Contexts.back().IsExpression = true;
    if (Style.isJavaScript() && OpeningBrace.Previous &&
        OpeningBrace.Previous->is(TT_JsTypeColon)) {
      Contexts.back().IsExpression = false;
    }
    if (Style.isVerilog() &&
        (!OpeningBrace.getPreviousNonComment() ||
         OpeningBrace.getPreviousNonComment()->isNot(Keywords.kw_apostrophe))) {
      Contexts.back().VerilogMayBeConcatenation = true;
    }
    if (Style.isTableGen())
      Contexts.back().ColonIsDictLiteral = false;

    unsigned CommaCount = 0;
    while (CurrentToken) {
      if (CurrentToken->is(tok::r_brace)) {
        assert(!Scopes.empty());
        assert(Scopes.back() == getScopeType(OpeningBrace));
        Scopes.pop_back();
        assert(OpeningBrace.Optional == CurrentToken->Optional);
        OpeningBrace.MatchingParen = CurrentToken;
        CurrentToken->MatchingParen = &OpeningBrace;
        if (Style.AlignArrayOfStructures != FormatStyle::AIAS_None) {
          if (OpeningBrace.ParentBracket == tok::l_brace &&
              couldBeInStructArrayInitializer() && CommaCount > 0) {
            Contexts.back().ContextType = Context::StructArrayInitializer;
          }
        }
        next();
        return true;
      }
      if (CurrentToken->isOneOf(tok::r_paren, tok::r_square))
        return false;
      updateParameterCount(&OpeningBrace, CurrentToken);
      if (CurrentToken->isOneOf(tok::colon, tok::l_brace, tok::less)) {
        FormatToken *Previous = CurrentToken->getPreviousNonComment();
        if (Previous->is(TT_JsTypeOptionalQuestion))
          Previous = Previous->getPreviousNonComment();
        if ((CurrentToken->is(tok::colon) && !Style.isTableGen() &&
             (!Contexts.back().ColonIsDictLiteral || !IsCpp)) ||
            Style.isProto()) {
          OpeningBrace.setType(TT_DictLiteral);
          if (Previous->Tok.getIdentifierInfo() ||
              Previous->is(tok::string_literal)) {
            Previous->setType(TT_SelectorName);
          }
        }
        if (CurrentToken->is(tok::colon) && OpeningBrace.is(TT_Unknown) &&
            !Style.isTableGen()) {
          OpeningBrace.setType(TT_DictLiteral);
        } else if (Style.isJavaScript()) {
          OpeningBrace.overwriteFixedType(TT_DictLiteral);
        }
      }
      if (CurrentToken->is(tok::comma)) {
        if (Style.isJavaScript())
          OpeningBrace.overwriteFixedType(TT_DictLiteral);
        ++CommaCount;
      }
      if (!consumeToken())
        return false;
    }
    return true;
  }

  void updateParameterCount(FormatToken *Left, FormatToken *Current) {
    // For ObjC methods, the number of parameters is calculated differently as
    // method declarations have a different structure (the parameters are not
    // inside a bracket scope).
    if (Current->is(tok::l_brace) && Current->is(BK_Block))
      ++Left->BlockParameterCount;
    if (Current->is(tok::comma)) {
      ++Left->ParameterCount;
      if (!Left->Role)
        Left->Role.reset(new CommaSeparatedList(Style));
      Left->Role->CommaFound(Current);
    } else if (Left->ParameterCount == 0 && Current->isNot(tok::comment)) {
      Left->ParameterCount = 1;
    }
  }

  bool parseConditional() {
    while (CurrentToken) {
      if (CurrentToken->is(tok::colon)) {
        CurrentToken->setType(TT_ConditionalExpr);
        next();
        return true;
      }
      if (!consumeToken())
        return false;
    }
    return false;
  }

  bool parseTemplateDeclaration() {
    if (!CurrentToken || CurrentToken->isNot(tok::less))
      return false;

    CurrentToken->setType(TT_TemplateOpener);
    next();

    TemplateDeclarationDepth++;
    const bool WellFormed = parseAngle();
    TemplateDeclarationDepth--;
    if (!WellFormed)
      return false;

    if (CurrentToken && TemplateDeclarationDepth == 0)
      CurrentToken->Previous->ClosesTemplateDeclaration = true;

    return true;
  }

  bool consumeToken() {
    if (IsCpp) {
      const auto *Prev = CurrentToken->getPreviousNonComment();
      if (Prev && Prev->is(tok::r_square) && Prev->is(TT_AttributeSquare) &&
          CurrentToken->isOneOf(tok::kw_if, tok::kw_switch, tok::kw_case,
                                tok::kw_default, tok::kw_for, tok::kw_while) &&
          mustBreakAfterAttributes(*CurrentToken, Style)) {
        CurrentToken->MustBreakBefore = true;
      }
    }
    FormatToken *Tok = CurrentToken;
    next();
    // In Verilog primitives' state tables, `:`, `?`, and `-` aren't normal
    // operators.
    if (Tok->is(TT_VerilogTableItem))
      return true;
    // Multi-line string itself is a single annotated token.
    if (Tok->is(TT_TableGenMultiLineString))
      return true;
    switch (Tok->Tok.getKind()) {
    case tok::plus:
    case tok::minus:
      if (!Tok->Previous && Line.MustBeDeclaration)
        Tok->setType(TT_ObjCMethodSpecifier);
      break;
    case tok::colon:
      if (!Tok->Previous)
        return false;
      // Goto labels and case labels are already identified in
      // UnwrappedLineParser.
      if (Tok->isTypeFinalized())
        break;
      // Colons from ?: are handled in parseConditional().
      if (Style.isJavaScript()) {
        if (Contexts.back().ColonIsForRangeExpr || // colon in for loop
            (Contexts.size() == 1 &&               // switch/case labels
             !Line.First->isOneOf(tok::kw_enum, tok::kw_case)) ||
            Contexts.back().ContextKind == tok::l_paren ||  // function params
            Contexts.back().ContextKind == tok::l_square || // array type
            (!Contexts.back().IsExpression &&
             Contexts.back().ContextKind == tok::l_brace) || // object type
            (Contexts.size() == 1 &&
             Line.MustBeDeclaration)) { // method/property declaration
          Contexts.back().IsExpression = false;
          Tok->setType(TT_JsTypeColon);
          break;
        }
      } else if (Style.isCSharp()) {
        if (Contexts.back().InCSharpAttributeSpecifier) {
          Tok->setType(TT_AttributeColon);
          break;
        }
        if (Contexts.back().ContextKind == tok::l_paren) {
          Tok->setType(TT_CSharpNamedArgumentColon);
          break;
        }
      } else if (Style.isVerilog() && Tok->isNot(TT_BinaryOperator)) {
        // The distribution weight operators are labeled
        // TT_BinaryOperator by the lexer.
        if (Keywords.isVerilogEnd(*Tok->Previous) ||
            Keywords.isVerilogBegin(*Tok->Previous)) {
          Tok->setType(TT_VerilogBlockLabelColon);
        } else if (Contexts.back().ContextKind == tok::l_square) {
          Tok->setType(TT_BitFieldColon);
        } else if (Contexts.back().ColonIsDictLiteral) {
          Tok->setType(TT_DictLiteral);
        } else if (Contexts.size() == 1) {
          // In Verilog a case label doesn't have the case keyword. We
          // assume a colon following an expression is a case label.
          // Colons from ?: are annotated in parseConditional().
          Tok->setType(TT_CaseLabelColon);
          if (Line.Level > 1 || (!Line.InPPDirective && Line.Level > 0))
            --Line.Level;
        }
        break;
      }
      if (Line.First->isOneOf(Keywords.kw_module, Keywords.kw_import) ||
          Line.First->startsSequence(tok::kw_export, Keywords.kw_module) ||
          Line.First->startsSequence(tok::kw_export, Keywords.kw_import)) {
        Tok->setType(TT_ModulePartitionColon);
      } else if (Line.First->is(tok::kw_asm)) {
        Tok->setType(TT_InlineASMColon);
      } else if (Contexts.back().ColonIsDictLiteral || Style.isProto()) {
        Tok->setType(TT_DictLiteral);
        if (Style.Language == FormatStyle::LK_TextProto) {
          if (FormatToken *Previous = Tok->getPreviousNonComment())
            Previous->setType(TT_SelectorName);
        }
      } else if (Contexts.back().ColonIsObjCMethodExpr ||
                 Line.startsWith(TT_ObjCMethodSpecifier)) {
        Tok->setType(TT_ObjCMethodExpr);
        const FormatToken *BeforePrevious = Tok->Previous->Previous;
        // Ensure we tag all identifiers in method declarations as
        // TT_SelectorName.
        bool UnknownIdentifierInMethodDeclaration =
            Line.startsWith(TT_ObjCMethodSpecifier) &&
            Tok->Previous->is(tok::identifier) && Tok->Previous->is(TT_Unknown);
        if (!BeforePrevious ||
            // FIXME(bug 36976): ObjC return types shouldn't use TT_CastRParen.
            !(BeforePrevious->is(TT_CastRParen) ||
              (BeforePrevious->is(TT_ObjCMethodExpr) &&
               BeforePrevious->is(tok::colon))) ||
            BeforePrevious->is(tok::r_square) ||
            Contexts.back().LongestObjCSelectorName == 0 ||
            UnknownIdentifierInMethodDeclaration) {
          Tok->Previous->setType(TT_SelectorName);
          if (!Contexts.back().FirstObjCSelectorName) {
            Contexts.back().FirstObjCSelectorName = Tok->Previous;
          } else if (Tok->Previous->ColumnWidth >
                     Contexts.back().LongestObjCSelectorName) {
            Contexts.back().LongestObjCSelectorName =
                Tok->Previous->ColumnWidth;
          }
          Tok->Previous->ParameterIndex =
              Contexts.back().FirstObjCSelectorName->ObjCSelectorNameParts;
          ++Contexts.back().FirstObjCSelectorName->ObjCSelectorNameParts;
        }
      } else if (Contexts.back().ColonIsForRangeExpr) {
        Tok->setType(TT_RangeBasedForLoopColon);
      } else if (Contexts.back().ContextType == Context::C11GenericSelection) {
        Tok->setType(TT_GenericSelectionColon);
      } else if (CurrentToken && CurrentToken->is(tok::numeric_constant)) {
        Tok->setType(TT_BitFieldColon);
      } else if (Contexts.size() == 1 &&
                 !Line.First->isOneOf(tok::kw_enum, tok::kw_case,
                                      tok::kw_default)) {
        FormatToken *Prev = Tok->getPreviousNonComment();
        if (!Prev)
          break;
        if (Prev->isOneOf(tok::r_paren, tok::kw_noexcept) ||
            Prev->ClosesRequiresClause) {
          Tok->setType(TT_CtorInitializerColon);
        } else if (Prev->is(tok::kw_try)) {
          // Member initializer list within function try block.
          FormatToken *PrevPrev = Prev->getPreviousNonComment();
          if (!PrevPrev)
            break;
          if (PrevPrev && PrevPrev->isOneOf(tok::r_paren, tok::kw_noexcept))
            Tok->setType(TT_CtorInitializerColon);
        } else {
          Tok->setType(TT_InheritanceColon);
          if (Prev->isAccessSpecifierKeyword())
            Line.Type = LT_AccessModifier;
        }
      } else if (canBeObjCSelectorComponent(*Tok->Previous) && Tok->Next &&
                 (Tok->Next->isOneOf(tok::r_paren, tok::comma) ||
                  (canBeObjCSelectorComponent(*Tok->Next) && Tok->Next->Next &&
                   Tok->Next->Next->is(tok::colon)))) {
        // This handles a special macro in ObjC code where selectors including
        // the colon are passed as macro arguments.
        Tok->setType(TT_ObjCMethodExpr);
      }
      break;
    case tok::pipe:
    case tok::amp:
      // | and & in declarations/type expressions represent union and
      // intersection types, respectively.
      if (Style.isJavaScript() && !Contexts.back().IsExpression)
        Tok->setType(TT_JsTypeOperator);
      break;
    case tok::kw_if:
      if (Style.isTableGen()) {
        // In TableGen it has the form 'if' <value> 'then'.
        if (!parseTableGenValue())
          return false;
        if (CurrentToken && CurrentToken->is(Keywords.kw_then))
          next(); // skip then
        break;
      }
      if (CurrentToken &&
          CurrentToken->isOneOf(tok::kw_constexpr, tok::identifier)) {
        next();
      }
      [[fallthrough]];
    case tok::kw_while:
      if (CurrentToken && CurrentToken->is(tok::l_paren)) {
        next();
        if (!parseParens(/*LookForDecls=*/true))
          return false;
      }
      break;
    case tok::kw_for:
      if (Style.isJavaScript()) {
        // x.for and {for: ...}
        if ((Tok->Previous && Tok->Previous->is(tok::period)) ||
            (Tok->Next && Tok->Next->is(tok::colon))) {
          break;
        }
        // JS' for await ( ...
        if (CurrentToken && CurrentToken->is(Keywords.kw_await))
          next();
      }
      if (IsCpp && CurrentToken && CurrentToken->is(tok::kw_co_await))
        next();
      Contexts.back().ColonIsForRangeExpr = true;
      if (!CurrentToken || CurrentToken->isNot(tok::l_paren))
        return false;
      next();
      if (!parseParens())
        return false;
      break;
    case tok::l_paren:
      // When faced with 'operator()()', the kw_operator handler incorrectly
      // marks the first l_paren as a OverloadedOperatorLParen. Here, we make
      // the first two parens OverloadedOperators and the second l_paren an
      // OverloadedOperatorLParen.
      if (Tok->Previous && Tok->Previous->is(tok::r_paren) &&
          Tok->Previous->MatchingParen &&
          Tok->Previous->MatchingParen->is(TT_OverloadedOperatorLParen)) {
        Tok->Previous->setType(TT_OverloadedOperator);
        Tok->Previous->MatchingParen->setType(TT_OverloadedOperator);
        Tok->setType(TT_OverloadedOperatorLParen);
      }

      if (Style.isVerilog()) {
        // Identify the parameter list and port list in a module instantiation.
        // This is still needed when we already have
        // UnwrappedLineParser::parseVerilogHierarchyHeader because that
        // function is only responsible for the definition, not the
        // instantiation.
        auto IsInstancePort = [&]() {
          const FormatToken *Prev = Tok->getPreviousNonComment();
          const FormatToken *PrevPrev;
          // In the following example all 4 left parentheses will be treated as
          // 'TT_VerilogInstancePortLParen'.
          //
          //   module_x instance_1(port_1); // Case A.
          //   module_x #(parameter_1)      // Case B.
          //       instance_2(port_1),      // Case C.
          //       instance_3(port_1);      // Case D.
          if (!Prev || !(PrevPrev = Prev->getPreviousNonComment()))
            return false;
          // Case A.
          if (Keywords.isVerilogIdentifier(*Prev) &&
              Keywords.isVerilogIdentifier(*PrevPrev)) {
            return true;
          }
          // Case B.
          if (Prev->is(Keywords.kw_verilogHash) &&
              Keywords.isVerilogIdentifier(*PrevPrev)) {
            return true;
          }
          // Case C.
          if (Keywords.isVerilogIdentifier(*Prev) && PrevPrev->is(tok::r_paren))
            return true;
          // Case D.
          if (Keywords.isVerilogIdentifier(*Prev) && PrevPrev->is(tok::comma)) {
            const FormatToken *PrevParen = PrevPrev->getPreviousNonComment();
            if (PrevParen->is(tok::r_paren) && PrevParen->MatchingParen &&
                PrevParen->MatchingParen->is(TT_VerilogInstancePortLParen)) {
              return true;
            }
          }
          return false;
        };

        if (IsInstancePort())
          Tok->setFinalizedType(TT_VerilogInstancePortLParen);
      }

      if (!parseParens())
        return false;
      if (Line.MustBeDeclaration && Contexts.size() == 1 &&
          !Contexts.back().IsExpression && !Line.startsWith(TT_ObjCProperty) &&
          !Line.startsWith(tok::l_paren) &&
          !Tok->isOneOf(TT_TypeDeclarationParen, TT_RequiresExpressionLParen)) {
        if (const auto *Previous = Tok->Previous;
            !Previous ||
            (!Previous->isAttribute() &&
             !Previous->isOneOf(TT_RequiresClause, TT_LeadingJavaAnnotation))) {
          Line.MightBeFunctionDecl = true;
          Tok->MightBeFunctionDeclParen = true;
        }
      }
      break;
    case tok::l_square:
      if (Style.isTableGen())
        Tok->setType(TT_TableGenListOpener);
      if (!parseSquare())
        return false;
      break;
    case tok::l_brace:
      if (Style.Language == FormatStyle::LK_TextProto) {
        FormatToken *Previous = Tok->getPreviousNonComment();
        if (Previous && Previous->isNot(TT_DictLiteral))
          Previous->setType(TT_SelectorName);
      }
      Scopes.push_back(getScopeType(*Tok));
      if (!parseBrace())
        return false;
      break;
    case tok::less:
      if (parseAngle()) {
        Tok->setType(TT_TemplateOpener);
        // In TT_Proto, we must distignuish between:
        //   map<key, value>
        //   msg < item: data >
        //   msg: < item: data >
        // In TT_TextProto, map<key, value> does not occur.
        if (Style.Language == FormatStyle::LK_TextProto ||
            (Style.Language == FormatStyle::LK_Proto && Tok->Previous &&
             Tok->Previous->isOneOf(TT_SelectorName, TT_DictLiteral))) {
          Tok->setType(TT_DictLiteral);
          FormatToken *Previous = Tok->getPreviousNonComment();
          if (Previous && Previous->isNot(TT_DictLiteral))
            Previous->setType(TT_SelectorName);
        }
        if (Style.isTableGen())
          Tok->setType(TT_TemplateOpener);
      } else {
        Tok->setType(TT_BinaryOperator);
        NonTemplateLess.insert(Tok);
        CurrentToken = Tok;
        next();
      }
      break;
    case tok::r_paren:
    case tok::r_square:
      return false;
    case tok::r_brace:
      // Don't pop scope when encountering unbalanced r_brace.
      if (!Scopes.empty())
        Scopes.pop_back();
      // Lines can start with '}'.
      if (Tok->Previous)
        return false;
      break;
    case tok::greater:
      if (Style.Language != FormatStyle::LK_TextProto && Tok->is(TT_Unknown))
        Tok->setType(TT_BinaryOperator);
      if (Tok->Previous && Tok->Previous->is(TT_TemplateCloser))
        Tok->SpacesRequiredBefore = 1;
      break;
    case tok::kw_operator:
      if (Style.isProto())
        break;
      while (CurrentToken &&
             !CurrentToken->isOneOf(tok::l_paren, tok::semi, tok::r_paren)) {
        if (CurrentToken->isOneOf(tok::star, tok::amp))
          CurrentToken->setType(TT_PointerOrReference);
        auto Next = CurrentToken->getNextNonComment();
        if (!Next)
          break;
        if (Next->is(tok::less))
          next();
        else
          consumeToken();
        if (!CurrentToken)
          break;
        auto Previous = CurrentToken->getPreviousNonComment();
        assert(Previous);
        if (CurrentToken->is(tok::comma) && Previous->isNot(tok::kw_operator))
          break;
        if (Previous->isOneOf(TT_BinaryOperator, TT_UnaryOperator, tok::comma,
                              tok::star, tok::arrow, tok::amp, tok::ampamp) ||
            // User defined literal.
            Previous->TokenText.starts_with("\"\"")) {
          Previous->setType(TT_OverloadedOperator);
          if (CurrentToken->isOneOf(tok::less, tok::greater))
            break;
        }
      }
      if (CurrentToken && CurrentToken->is(tok::l_paren))
        CurrentToken->setType(TT_OverloadedOperatorLParen);
      if (CurrentToken && CurrentToken->Previous->is(TT_BinaryOperator))
        CurrentToken->Previous->setType(TT_OverloadedOperator);
      break;
    case tok::question:
      if (Style.isJavaScript() && Tok->Next &&
          Tok->Next->isOneOf(tok::semi, tok::comma, tok::colon, tok::r_paren,
                             tok::r_brace, tok::r_square)) {
        // Question marks before semicolons, colons, etc. indicate optional
        // types (fields, parameters), e.g.
        //   function(x?: string, y?) {...}
        //   class X { y?; }
        Tok->setType(TT_JsTypeOptionalQuestion);
        break;
      }
      // Declarations cannot be conditional expressions, this can only be part
      // of a type declaration.
      if (Line.MustBeDeclaration && !Contexts.back().IsExpression &&
          Style.isJavaScript()) {
        break;
      }
      if (Style.isCSharp()) {
        // `Type?)`, `Type?>`, `Type? name;` and `Type? name =` can only be
        // nullable types.

        // `Type?)`, `Type?>`, `Type? name;`
        if (Tok->Next &&
            (Tok->Next->startsSequence(tok::question, tok::r_paren) ||
             Tok->Next->startsSequence(tok::question, tok::greater) ||
             Tok->Next->startsSequence(tok::question, tok::identifier,
                                       tok::semi))) {
          Tok->setType(TT_CSharpNullable);
          break;
        }

        // `Type? name =`
        if (Tok->Next && Tok->Next->is(tok::identifier) && Tok->Next->Next &&
            Tok->Next->Next->is(tok::equal)) {
          Tok->setType(TT_CSharpNullable);
          break;
        }

        // Line.MustBeDeclaration will be true for `Type? name;`.
        // But not
        // cond ? "A" : "B";
        // cond ? id : "B";
        // cond ? cond2 ? "A" : "B" : "C";
        if (!Contexts.back().IsExpression && Line.MustBeDeclaration &&
            (!Tok->Next ||
             !Tok->Next->isOneOf(tok::identifier, tok::string_literal) ||
             !Tok->Next->Next ||
             !Tok->Next->Next->isOneOf(tok::colon, tok::question))) {
          Tok->setType(TT_CSharpNullable);
          break;
        }
      }
      parseConditional();
      break;
    case tok::kw_template:
      parseTemplateDeclaration();
      break;
    case tok::comma:
      switch (Contexts.back().ContextType) {
      case Context::CtorInitializer:
        Tok->setType(TT_CtorInitializerComma);
        break;
      case Context::InheritanceList:
        Tok->setType(TT_InheritanceComma);
        break;
      case Context::VerilogInstancePortList:
        Tok->setFinalizedType(TT_VerilogInstancePortComma);
        break;
      default:
        if (Style.isVerilog() && Contexts.size() == 1 &&
            Line.startsWith(Keywords.kw_assign)) {
          Tok->setFinalizedType(TT_VerilogAssignComma);
        } else if (Contexts.back().FirstStartOfName &&
                   (Contexts.size() == 1 || startsWithInitStatement(Line))) {
          Contexts.back().FirstStartOfName->PartOfMultiVariableDeclStmt = true;
          Line.IsMultiVariableDeclStmt = true;
        }
        break;
      }
      if (Contexts.back().ContextType == Context::ForEachMacro)
        Contexts.back().IsExpression = true;
      break;
    case tok::kw_default:
      // Unindent case labels.
      if (Style.isVerilog() && Keywords.isVerilogEndOfLabel(*Tok) &&
          (Line.Level > 1 || (!Line.InPPDirective && Line.Level > 0))) {
        --Line.Level;
      }
      break;
    case tok::identifier:
      if (Tok->isOneOf(Keywords.kw___has_include,
                       Keywords.kw___has_include_next)) {
        parseHasInclude();
      }
      if (Style.isCSharp() && Tok->is(Keywords.kw_where) && Tok->Next &&
          Tok->Next->isNot(tok::l_paren)) {
        Tok->setType(TT_CSharpGenericTypeConstraint);
        parseCSharpGenericTypeConstraint();
        if (!Tok->getPreviousNonComment())
          Line.IsContinuation = true;
      }
      if (Style.isTableGen()) {
        if (Tok->is(Keywords.kw_assert)) {
          if (!parseTableGenValue())
            return false;
        } else if (Tok->isOneOf(Keywords.kw_def, Keywords.kw_defm) &&
                   (!Tok->Next ||
                    !Tok->Next->isOneOf(tok::colon, tok::l_brace))) {
          // The case NameValue appears.
          if (!parseTableGenValue(true))
            return false;
        }
      }
      break;
    case tok::arrow:
      if (Tok->isNot(TT_LambdaArrow) && Tok->Previous &&
          Tok->Previous->is(tok::kw_noexcept)) {
        Tok->setType(TT_TrailingReturnArrow);
      }
      break;
    case tok::equal:
      // In TableGen, there must be a value after "=";
      if (Style.isTableGen() && !parseTableGenValue())
        return false;
      break;
    default:
      break;
    }
    return true;
  }

  void parseCSharpGenericTypeConstraint() {
    int OpenAngleBracketsCount = 0;
    while (CurrentToken) {
      if (CurrentToken->is(tok::less)) {
        // parseAngle is too greedy and will consume the whole line.
        CurrentToken->setType(TT_TemplateOpener);
        ++OpenAngleBracketsCount;
        next();
      } else if (CurrentToken->is(tok::greater)) {
        CurrentToken->setType(TT_TemplateCloser);
        --OpenAngleBracketsCount;
        next();
      } else if (CurrentToken->is(tok::comma) && OpenAngleBracketsCount == 0) {
        // We allow line breaks after GenericTypeConstraintComma's
        // so do not flag commas in Generics as GenericTypeConstraintComma's.
        CurrentToken->setType(TT_CSharpGenericTypeConstraintComma);
        next();
      } else if (CurrentToken->is(Keywords.kw_where)) {
        CurrentToken->setType(TT_CSharpGenericTypeConstraint);
        next();
      } else if (CurrentToken->is(tok::colon)) {
        CurrentToken->setType(TT_CSharpGenericTypeConstraintColon);
        next();
      } else {
        next();
      }
    }
  }

  void parseIncludeDirective() {
    if (CurrentToken && CurrentToken->is(tok::less)) {
      next();
      while (CurrentToken) {
        // Mark tokens up to the trailing line comments as implicit string
        // literals.
        if (CurrentToken->isNot(tok::comment) &&
            !CurrentToken->TokenText.starts_with("//")) {
          CurrentToken->setType(TT_ImplicitStringLiteral);
        }
        next();
      }
    }
  }

  void parseWarningOrError() {
    next();
    // We still want to format the whitespace left of the first token of the
    // warning or error.
    next();
    while (CurrentToken) {
      CurrentToken->setType(TT_ImplicitStringLiteral);
      next();
    }
  }

  void parsePragma() {
    next(); // Consume "pragma".
    if (CurrentToken &&
        CurrentToken->isOneOf(Keywords.kw_mark, Keywords.kw_option,
                              Keywords.kw_region)) {
      bool IsMarkOrRegion =
          CurrentToken->isOneOf(Keywords.kw_mark, Keywords.kw_region);
      next();
      next(); // Consume first token (so we fix leading whitespace).
      while (CurrentToken) {
        if (IsMarkOrRegion || CurrentToken->Previous->is(TT_BinaryOperator))
          CurrentToken->setType(TT_ImplicitStringLiteral);
        next();
      }
    }
  }

  void parseHasInclude() {
    if (!CurrentToken || CurrentToken->isNot(tok::l_paren))
      return;
    next(); // '('
    parseIncludeDirective();
    next(); // ')'
  }

  LineType parsePreprocessorDirective() {
    bool IsFirstToken = CurrentToken->IsFirst;
    LineType Type = LT_PreprocessorDirective;
    next();
    if (!CurrentToken)
      return Type;

    if (Style.isJavaScript() && IsFirstToken) {
      // JavaScript files can contain shebang lines of the form:
      // #!/usr/bin/env node
      // Treat these like C++ #include directives.
      while (CurrentToken) {
        // Tokens cannot be comments here.
        CurrentToken->setType(TT_ImplicitStringLiteral);
        next();
      }
      return LT_ImportStatement;
    }

    if (CurrentToken->is(tok::numeric_constant)) {
      CurrentToken->SpacesRequiredBefore = 1;
      return Type;
    }
    // Hashes in the middle of a line can lead to any strange token
    // sequence.
    if (!CurrentToken->Tok.getIdentifierInfo())
      return Type;
    // In Verilog macro expansions start with a backtick just like preprocessor
    // directives. Thus we stop if the word is not a preprocessor directive.
    if (Style.isVerilog() && !Keywords.isVerilogPPDirective(*CurrentToken))
      return LT_Invalid;
    switch (CurrentToken->Tok.getIdentifierInfo()->getPPKeywordID()) {
    case tok::pp_include:
    case tok::pp_include_next:
    case tok::pp_import:
      next();
      parseIncludeDirective();
      Type = LT_ImportStatement;
      break;
    case tok::pp_error:
    case tok::pp_warning:
      parseWarningOrError();
      break;
    case tok::pp_pragma:
      parsePragma();
      break;
    case tok::pp_if:
    case tok::pp_elif:
      Contexts.back().IsExpression = true;
      next();
      if (CurrentToken)
        CurrentToken->SpacesRequiredBefore = true;
      parseLine();
      break;
    default:
      break;
    }
    while (CurrentToken) {
      FormatToken *Tok = CurrentToken;
      next();
      if (Tok->is(tok::l_paren)) {
        parseParens();
      } else if (Tok->isOneOf(Keywords.kw___has_include,
                              Keywords.kw___has_include_next)) {
        parseHasInclude();
      }
    }
    return Type;
  }

public:
  LineType parseLine() {
    if (!CurrentToken)
      return LT_Invalid;
    NonTemplateLess.clear();
    if (!Line.InMacroBody && CurrentToken->is(tok::hash)) {
      // We were not yet allowed to use C++17 optional when this was being
      // written. So we used LT_Invalid to mark that the line is not a
      // preprocessor directive.
      auto Type = parsePreprocessorDirective();
      if (Type != LT_Invalid)
        return Type;
    }

    // Directly allow to 'import <string-literal>' to support protocol buffer
    // definitions (github.com/google/protobuf) or missing "#" (either way we
    // should not break the line).
    IdentifierInfo *Info = CurrentToken->Tok.getIdentifierInfo();
    if ((Style.Language == FormatStyle::LK_Java &&
         CurrentToken->is(Keywords.kw_package)) ||
        (!Style.isVerilog() && Info &&
         Info->getPPKeywordID() == tok::pp_import && CurrentToken->Next &&
         CurrentToken->Next->isOneOf(tok::string_literal, tok::identifier,
                                     tok::kw_static))) {
      next();
      parseIncludeDirective();
      return LT_ImportStatement;
    }

    // If this line starts and ends in '<' and '>', respectively, it is likely
    // part of "#define <a/b.h>".
    if (CurrentToken->is(tok::less) && Line.Last->is(tok::greater)) {
      parseIncludeDirective();
      return LT_ImportStatement;
    }

    // In .proto files, top-level options and package statements are very
    // similar to import statements and should not be line-wrapped.
    if (Style.Language == FormatStyle::LK_Proto && Line.Level == 0 &&
        CurrentToken->isOneOf(Keywords.kw_option, Keywords.kw_package)) {
      next();
      if (CurrentToken && CurrentToken->is(tok::identifier)) {
        while (CurrentToken)
          next();
        return LT_ImportStatement;
      }
    }

    bool KeywordVirtualFound = false;
    bool ImportStatement = false;

    // import {...} from '...';
    if (Style.isJavaScript() && CurrentToken->is(Keywords.kw_import))
      ImportStatement = true;

    while (CurrentToken) {
      if (CurrentToken->is(tok::kw_virtual))
        KeywordVirtualFound = true;
      if (Style.isJavaScript()) {
        // export {...} from '...';
        // An export followed by "from 'some string';" is a re-export from
        // another module identified by a URI and is treated as a
        // LT_ImportStatement (i.e. prevent wraps on it for long URIs).
        // Just "export {...};" or "export class ..." should not be treated as
        // an import in this sense.
        if (Line.First->is(tok::kw_export) &&
            CurrentToken->is(Keywords.kw_from) && CurrentToken->Next &&
            CurrentToken->Next->isStringLiteral()) {
          ImportStatement = true;
        }
        if (isClosureImportStatement(*CurrentToken))
          ImportStatement = true;
      }
      if (!consumeToken())
        return LT_Invalid;
    }
    if (Line.Type == LT_AccessModifier)
      return LT_AccessModifier;
    if (KeywordVirtualFound)
      return LT_VirtualFunctionDecl;
    if (ImportStatement)
      return LT_ImportStatement;

    if (Line.startsWith(TT_ObjCMethodSpecifier)) {
      if (Contexts.back().FirstObjCSelectorName) {
        Contexts.back().FirstObjCSelectorName->LongestObjCSelectorName =
            Contexts.back().LongestObjCSelectorName;
      }
      return LT_ObjCMethodDecl;
    }

    for (const auto &ctx : Contexts)
      if (ctx.ContextType == Context::StructArrayInitializer)
        return LT_ArrayOfStructInitializer;

    return LT_Other;
  }

private:
  bool isClosureImportStatement(const FormatToken &Tok) {
    // FIXME: Closure-library specific stuff should not be hard-coded but be
    // configurable.
    return Tok.TokenText == "goog" && Tok.Next && Tok.Next->is(tok::period) &&
           Tok.Next->Next &&
           (Tok.Next->Next->TokenText == "module" ||
            Tok.Next->Next->TokenText == "provide" ||
            Tok.Next->Next->TokenText == "require" ||
            Tok.Next->Next->TokenText == "requireType" ||
            Tok.Next->Next->TokenText == "forwardDeclare") &&
           Tok.Next->Next->Next && Tok.Next->Next->Next->is(tok::l_paren);
  }

  void resetTokenMetadata() {
    if (!CurrentToken)
      return;

    // Reset token type in case we have already looked at it and then
    // recovered from an error (e.g. failure to find the matching >).
    if (!CurrentToken->isTypeFinalized() &&
        !CurrentToken->isOneOf(
            TT_LambdaLSquare, TT_LambdaLBrace, TT_AttributeMacro, TT_IfMacro,
            TT_ForEachMacro, TT_TypenameMacro, TT_FunctionLBrace,
            TT_ImplicitStringLiteral, TT_InlineASMBrace, TT_FatArrow,
            TT_LambdaArrow, TT_NamespaceMacro, TT_OverloadedOperator,
            TT_RegexLiteral, TT_TemplateString, TT_ObjCStringLiteral,
            TT_UntouchableMacroFunc, TT_StatementAttributeLikeMacro,
            TT_FunctionLikeOrFreestandingMacro, TT_ClassLBrace, TT_EnumLBrace,
            TT_RecordLBrace, TT_StructLBrace, TT_UnionLBrace, TT_RequiresClause,
            TT_RequiresClauseInARequiresExpression, TT_RequiresExpression,
            TT_RequiresExpressionLParen, TT_RequiresExpressionLBrace,
            TT_BracedListLBrace)) {
      CurrentToken->setType(TT_Unknown);
    }
    CurrentToken->Role.reset();
    CurrentToken->MatchingParen = nullptr;
    CurrentToken->FakeLParens.clear();
    CurrentToken->FakeRParens = 0;
  }

  void next() {
    if (!CurrentToken)
      return;

    CurrentToken->NestingLevel = Contexts.size() - 1;
    CurrentToken->BindingStrength = Contexts.back().BindingStrength;
    modifyContext(*CurrentToken);
    determineTokenType(*CurrentToken);
    CurrentToken = CurrentToken->Next;

    resetTokenMetadata();
  }

  /// A struct to hold information valid in a specific context, e.g.
  /// a pair of parenthesis.
  struct Context {
    Context(tok::TokenKind ContextKind, unsigned BindingStrength,
            bool IsExpression)
        : ContextKind(ContextKind), BindingStrength(BindingStrength),
          IsExpression(IsExpression) {}

    tok::TokenKind ContextKind;
    unsigned BindingStrength;
    bool IsExpression;
    unsigned LongestObjCSelectorName = 0;
    bool ColonIsForRangeExpr = false;
    bool ColonIsDictLiteral = false;
    bool ColonIsObjCMethodExpr = false;
    FormatToken *FirstObjCSelectorName = nullptr;
    FormatToken *FirstStartOfName = nullptr;
    bool CanBeExpression = true;
    bool CaretFound = false;
    bool InCpp11AttributeSpecifier = false;
    bool InCSharpAttributeSpecifier = false;
    bool VerilogAssignmentFound = false;
    // Whether the braces may mean concatenation instead of structure or array
    // literal.
    bool VerilogMayBeConcatenation = false;
    bool IsTableGenDAGArg = false;
    bool IsTableGenBangOpe = false;
    bool IsTableGenCondOpe = false;
    enum {
      Unknown,
      // Like the part after `:` in a constructor.
      //   Context(...) : IsExpression(IsExpression)
      CtorInitializer,
      // Like in the parentheses in a foreach.
      ForEachMacro,
      // Like the inheritance list in a class declaration.
      //   class Input : public IO
      InheritanceList,
      // Like in the braced list.
      //   int x[] = {};
      StructArrayInitializer,
      // Like in `static_cast<int>`.
      TemplateArgument,
      // C11 _Generic selection.
      C11GenericSelection,
      // Like in the outer parentheses in `ffnand ff1(.q());`.
      VerilogInstancePortList,
    } ContextType = Unknown;
  };

  /// Puts a new \c Context onto the stack \c Contexts for the lifetime
  /// of each instance.
  struct ScopedContextCreator {
    AnnotatingParser &P;

    ScopedContextCreator(AnnotatingParser &P, tok::TokenKind ContextKind,
                         unsigned Increase)
        : P(P) {
      P.Contexts.push_back(Context(ContextKind,
                                   P.Contexts.back().BindingStrength + Increase,
                                   P.Contexts.back().IsExpression));
    }

    ~ScopedContextCreator() {
      if (P.Style.AlignArrayOfStructures != FormatStyle::AIAS_None) {
        if (P.Contexts.back().ContextType == Context::StructArrayInitializer) {
          P.Contexts.pop_back();
          P.Contexts.back().ContextType = Context::StructArrayInitializer;
          return;
        }
      }
      P.Contexts.pop_back();
    }
  };

  void modifyContext(const FormatToken &Current) {
    auto AssignmentStartsExpression = [&]() {
      if (Current.getPrecedence() != prec::Assignment)
        return false;

      if (Line.First->isOneOf(tok::kw_using, tok::kw_return))
        return false;
      if (Line.First->is(tok::kw_template)) {
        assert(Current.Previous);
        if (Current.Previous->is(tok::kw_operator)) {
          // `template ... operator=` cannot be an expression.
          return false;
        }

        // `template` keyword can start a variable template.
        const FormatToken *Tok = Line.First->getNextNonComment();
        assert(Tok); // Current token is on the same line.
        if (Tok->isNot(TT_TemplateOpener)) {
          // Explicit template instantiations do not have `<>`.
          return false;
        }

        // This is the default value of a template parameter, determine if it's
        // type or non-type.
        if (Contexts.back().ContextKind == tok::less) {
          assert(Current.Previous->Previous);
          return !Current.Previous->Previous->isOneOf(tok::kw_typename,
                                                      tok::kw_class);
        }

        Tok = Tok->MatchingParen;
        if (!Tok)
          return false;
        Tok = Tok->getNextNonComment();
        if (!Tok)
          return false;

        if (Tok->isOneOf(tok::kw_class, tok::kw_enum, tok::kw_struct,
                         tok::kw_using)) {
          return false;
        }

        return true;
      }

      // Type aliases use `type X = ...;` in TypeScript and can be exported
      // using `export type ...`.
      if (Style.isJavaScript() &&
          (Line.startsWith(Keywords.kw_type, tok::identifier) ||
           Line.startsWith(tok::kw_export, Keywords.kw_type,
                           tok::identifier))) {
        return false;
      }

      return !Current.Previous || Current.Previous->isNot(tok::kw_operator);
    };

    if (AssignmentStartsExpression()) {
      Contexts.back().IsExpression = true;
      if (!Line.startsWith(TT_UnaryOperator)) {
        for (FormatToken *Previous = Current.Previous;
             Previous && Previous->Previous &&
             !Previous->Previous->isOneOf(tok::comma, tok::semi);
             Previous = Previous->Previous) {
          if (Previous->isOneOf(tok::r_square, tok::r_paren, tok::greater)) {
            Previous = Previous->MatchingParen;
            if (!Previous)
              break;
          }
          if (Previous->opensScope())
            break;
          if (Previous->isOneOf(TT_BinaryOperator, TT_UnaryOperator) &&
              Previous->isPointerOrReference() && Previous->Previous &&
              Previous->Previous->isNot(tok::equal)) {
            Previous->setType(TT_PointerOrReference);
          }
        }
      }
    } else if (Current.is(tok::lessless) &&
               (!Current.Previous ||
                Current.Previous->isNot(tok::kw_operator))) {
      Contexts.back().IsExpression = true;
    } else if (Current.isOneOf(tok::kw_return, tok::kw_throw)) {
      Contexts.back().IsExpression = true;
    } else if (Current.is(TT_TrailingReturnArrow)) {
      Contexts.back().IsExpression = false;
    } else if (Current.isOneOf(TT_LambdaArrow, Keywords.kw_assert)) {
      Contexts.back().IsExpression = Style.Language == FormatStyle::LK_Java;
    } else if (Current.Previous &&
               Current.Previous->is(TT_CtorInitializerColon)) {
      Contexts.back().IsExpression = true;
      Contexts.back().ContextType = Context::CtorInitializer;
    } else if (Current.Previous && Current.Previous->is(TT_InheritanceColon)) {
      Contexts.back().ContextType = Context::InheritanceList;
    } else if (Current.isOneOf(tok::r_paren, tok::greater, tok::comma)) {
      for (FormatToken *Previous = Current.Previous;
           Previous && Previous->isOneOf(tok::star, tok::amp);
           Previous = Previous->Previous) {
        Previous->setType(TT_PointerOrReference);
      }
      if (Line.MustBeDeclaration &&
          Contexts.front().ContextType != Context::CtorInitializer) {
        Contexts.back().IsExpression = false;
      }
    } else if (Current.is(tok::kw_new)) {
      Contexts.back().CanBeExpression = false;
    } else if (Current.is(tok::semi) ||
               (Current.is(tok::exclaim) && Current.Previous &&
                Current.Previous->isNot(tok::kw_operator))) {
      // This should be the condition or increment in a for-loop.
      // But not operator !() (can't use TT_OverloadedOperator here as its not
      // been annotated yet).
      Contexts.back().IsExpression = true;
    }
  }

  static FormatToken *untilMatchingParen(FormatToken *Current) {
    // Used when `MatchingParen` is not yet established.
    int ParenLevel = 0;
    while (Current) {
      if (Current->is(tok::l_paren))
        ++ParenLevel;
      if (Current->is(tok::r_paren))
        --ParenLevel;
      if (ParenLevel < 1)
        break;
      Current = Current->Next;
    }
    return Current;
  }

  static bool isDeductionGuide(FormatToken &Current) {
    // Look for a deduction guide template<T> A(...) -> A<...>;
    if (Current.Previous && Current.Previous->is(tok::r_paren) &&
        Current.startsSequence(tok::arrow, tok::identifier, tok::less)) {
      // Find the TemplateCloser.
      FormatToken *TemplateCloser = Current.Next->Next;
      int NestingLevel = 0;
      while (TemplateCloser) {
        // Skip over an expressions in parens  A<(3 < 2)>;
        if (TemplateCloser->is(tok::l_paren)) {
          // No Matching Paren yet so skip to matching paren
          TemplateCloser = untilMatchingParen(TemplateCloser);
          if (!TemplateCloser)
            break;
        }
        if (TemplateCloser->is(tok::less))
          ++NestingLevel;
        if (TemplateCloser->is(tok::greater))
          --NestingLevel;
        if (NestingLevel < 1)
          break;
        TemplateCloser = TemplateCloser->Next;
      }
      // Assuming we have found the end of the template ensure its followed
      // with a semi-colon.
      if (TemplateCloser && TemplateCloser->Next &&
          TemplateCloser->Next->is(tok::semi) &&
          Current.Previous->MatchingParen) {
        // Determine if the identifier `A` prior to the A<..>; is the same as
        // prior to the A(..)
        FormatToken *LeadingIdentifier =
            Current.Previous->MatchingParen->Previous;

        return LeadingIdentifier &&
               LeadingIdentifier->TokenText == Current.Next->TokenText;
      }
    }
    return false;
  }

  void determineTokenType(FormatToken &Current) {
    if (Current.isNot(TT_Unknown)) {
      // The token type is already known.
      return;
    }

    if ((Style.isJavaScript() || Style.isCSharp()) &&
        Current.is(tok::exclaim)) {
      if (Current.Previous) {
        bool IsIdentifier =
            Style.isJavaScript()
                ? Keywords.isJavaScriptIdentifier(
                      *Current.Previous, /* AcceptIdentifierName= */ true)
                : Current.Previous->is(tok::identifier);
        if (IsIdentifier ||
            Current.Previous->isOneOf(
                tok::kw_default, tok::kw_namespace, tok::r_paren, tok::r_square,
                tok::r_brace, tok::kw_false, tok::kw_true, Keywords.kw_type,
                Keywords.kw_get, Keywords.kw_init, Keywords.kw_set) ||
            Current.Previous->Tok.isLiteral()) {
          Current.setType(TT_NonNullAssertion);
          return;
        }
      }
      if (Current.Next &&
          Current.Next->isOneOf(TT_BinaryOperator, Keywords.kw_as)) {
        Current.setType(TT_NonNullAssertion);
        return;
      }
    }

    // Line.MightBeFunctionDecl can only be true after the parentheses of a
    // function declaration have been found. In this case, 'Current' is a
    // trailing token of this declaration and thus cannot be a name.
    if ((Style.isJavaScript() || Style.Language == FormatStyle::LK_Java) &&
        Current.is(Keywords.kw_instanceof)) {
      Current.setType(TT_BinaryOperator);
    } else if (isStartOfName(Current) &&
               (!Line.MightBeFunctionDecl || Current.NestingLevel != 0)) {
      Contexts.back().FirstStartOfName = &Current;
      Current.setType(TT_StartOfName);
    } else if (Current.is(tok::semi)) {
      // Reset FirstStartOfName after finding a semicolon so that a for loop
      // with multiple increment statements is not confused with a for loop
      // having multiple variable declarations.
      Contexts.back().FirstStartOfName = nullptr;
    } else if (Current.isOneOf(tok::kw_auto, tok::kw___auto_type)) {
      AutoFound = true;
    } else if (Current.is(tok::arrow) &&
               Style.Language == FormatStyle::LK_Java) {
      Current.setType(TT_LambdaArrow);
    } else if (Current.is(tok::arrow) && Style.isVerilog()) {
      // The implication operator.
      Current.setType(TT_BinaryOperator);
    } else if (Current.is(tok::arrow) && AutoFound &&
               Line.MightBeFunctionDecl && Current.NestingLevel == 0 &&
               !Current.Previous->isOneOf(tok::kw_operator, tok::identifier)) {
      // not auto operator->() -> xxx;
      Current.setType(TT_TrailingReturnArrow);
    } else if (Current.is(tok::arrow) && Current.Previous &&
               Current.Previous->is(tok::r_brace)) {
      // Concept implicit conversion constraint needs to be treated like
      // a trailing return type  ... } -> <type>.
      Current.setType(TT_TrailingReturnArrow);
    } else if (isDeductionGuide(Current)) {
      // Deduction guides trailing arrow " A(...) -> A<T>;".
      Current.setType(TT_TrailingReturnArrow);
    } else if (Current.isPointerOrReference()) {
      Current.setType(determineStarAmpUsage(
          Current,
          Contexts.back().CanBeExpression && Contexts.back().IsExpression,
          Contexts.back().ContextType == Context::TemplateArgument));
    } else if (Current.isOneOf(tok::minus, tok::plus, tok::caret) ||
               (Style.isVerilog() && Current.is(tok::pipe))) {
      Current.setType(determinePlusMinusCaretUsage(Current));
      if (Current.is(TT_UnaryOperator) && Current.is(tok::caret))
        Contexts.back().CaretFound = true;
    } else if (Current.isOneOf(tok::minusminus, tok::plusplus)) {
      Current.setType(determineIncrementUsage(Current));
    } else if (Current.isOneOf(tok::exclaim, tok::tilde)) {
      Current.setType(TT_UnaryOperator);
    } else if (Current.is(tok::question)) {
      if (Style.isJavaScript() && Line.MustBeDeclaration &&
          !Contexts.back().IsExpression) {
        // In JavaScript, `interface X { foo?(): bar; }` is an optional method
        // on the interface, not a ternary expression.
        Current.setType(TT_JsTypeOptionalQuestion);
      } else if (Style.isTableGen()) {
        // In TableGen, '?' is just an identifier like token.
        Current.setType(TT_Unknown);
      } else {
        Current.setType(TT_ConditionalExpr);
      }
    } else if (Current.isBinaryOperator() &&
               (!Current.Previous || Current.Previous->isNot(tok::l_square)) &&
               (Current.isNot(tok::greater) &&
                Style.Language != FormatStyle::LK_TextProto)) {
      if (Style.isVerilog()) {
        if (Current.is(tok::lessequal) && Contexts.size() == 1 &&
            !Contexts.back().VerilogAssignmentFound) {
          // In Verilog `<=` is assignment if in its own statement. It is a
          // statement instead of an expression, that is it can not be chained.
          Current.ForcedPrecedence = prec::Assignment;
          Current.setFinalizedType(TT_BinaryOperator);
        }
        if (Current.getPrecedence() == prec::Assignment)
          Contexts.back().VerilogAssignmentFound = true;
      }
      Current.setType(TT_BinaryOperator);
    } else if (Current.is(tok::comment)) {
      if (Current.TokenText.starts_with("/*")) {
        if (Current.TokenText.ends_with("*/")) {
          Current.setType(TT_BlockComment);
        } else {
          // The lexer has for some reason determined a comment here. But we
          // cannot really handle it, if it isn't properly terminated.
          Current.Tok.setKind(tok::unknown);
        }
      } else {
        Current.setType(TT_LineComment);
      }
    } else if (Current.is(tok::string_literal)) {
      if (Style.isVerilog() && Contexts.back().VerilogMayBeConcatenation &&
          Current.getPreviousNonComment() &&
          Current.getPreviousNonComment()->isOneOf(tok::comma, tok::l_brace) &&
          Current.getNextNonComment() &&
          Current.getNextNonComment()->isOneOf(tok::comma, tok::r_brace)) {
        Current.setType(TT_StringInConcatenation);
      }
    } else if (Current.is(tok::l_paren)) {
      if (lParenStartsCppCast(Current))
        Current.setType(TT_CppCastLParen);
    } else if (Current.is(tok::r_paren)) {
      if (rParenEndsCast(Current))
        Current.setType(TT_CastRParen);
      if (Current.MatchingParen && Current.Next &&
          !Current.Next->isBinaryOperator() &&
          !Current.Next->isOneOf(
              tok::semi, tok::colon, tok::l_brace, tok::l_paren, tok::comma,
              tok::period, tok::arrow, tok::coloncolon, tok::kw_noexcept)) {
        if (FormatToken *AfterParen = Current.MatchingParen->Next;
            AfterParen && AfterParen->isNot(tok::caret)) {
          // Make sure this isn't the return type of an Obj-C block declaration.
          if (FormatToken *BeforeParen = Current.MatchingParen->Previous;
              BeforeParen && BeforeParen->is(tok::identifier) &&
              BeforeParen->isNot(TT_TypenameMacro) &&
              BeforeParen->TokenText == BeforeParen->TokenText.upper() &&
              (!BeforeParen->Previous ||
               BeforeParen->Previous->ClosesTemplateDeclaration ||
               BeforeParen->Previous->ClosesRequiresClause)) {
            Current.setType(TT_FunctionAnnotationRParen);
          }
        }
      }
    } else if (Current.is(tok::at) && Current.Next && !Style.isJavaScript() &&
               Style.Language != FormatStyle::LK_Java) {
      // In Java & JavaScript, "@..." is a decorator or annotation. In ObjC, it
      // marks declarations and properties that need special formatting.
      switch (Current.Next->Tok.getObjCKeywordID()) {
      case tok::objc_interface:
      case tok::objc_implementation:
      case tok::objc_protocol:
        Current.setType(TT_ObjCDecl);
        break;
      case tok::objc_property:
        Current.setType(TT_ObjCProperty);
        break;
      default:
        break;
      }
    } else if (Current.is(tok::period)) {
      FormatToken *PreviousNoComment = Current.getPreviousNonComment();
      if (PreviousNoComment &&
          PreviousNoComment->isOneOf(tok::comma, tok::l_brace)) {
        Current.setType(TT_DesignatedInitializerPeriod);
      } else if (Style.Language == FormatStyle::LK_Java && Current.Previous &&
                 Current.Previous->isOneOf(TT_JavaAnnotation,
                                           TT_LeadingJavaAnnotation)) {
        Current.setType(Current.Previous->getType());
      }
    } else if (canBeObjCSelectorComponent(Current) &&
               // FIXME(bug 36976): ObjC return types shouldn't use
               // TT_CastRParen.
               Current.Previous && Current.Previous->is(TT_CastRParen) &&
               Current.Previous->MatchingParen &&
               Current.Previous->MatchingParen->Previous &&
               Current.Previous->MatchingParen->Previous->is(
                   TT_ObjCMethodSpecifier)) {
      // This is the first part of an Objective-C selector name. (If there's no
      // colon after this, this is the only place which annotates the identifier
      // as a selector.)
      Current.setType(TT_SelectorName);
    } else if (Current.isOneOf(tok::identifier, tok::kw_const, tok::kw_noexcept,
                               tok::kw_requires) &&
               Current.Previous &&
               !Current.Previous->isOneOf(tok::equal, tok::at,
                                          TT_CtorInitializerComma,
                                          TT_CtorInitializerColon) &&
               Line.MightBeFunctionDecl && Contexts.size() == 1) {
      // Line.MightBeFunctionDecl can only be true after the parentheses of a
      // function declaration have been found.
      Current.setType(TT_TrailingAnnotation);
    } else if ((Style.Language == FormatStyle::LK_Java ||
                Style.isJavaScript()) &&
               Current.Previous) {
      if (Current.Previous->is(tok::at) &&
          Current.isNot(Keywords.kw_interface)) {
        const FormatToken &AtToken = *Current.Previous;
        const FormatToken *Previous = AtToken.getPreviousNonComment();
        if (!Previous || Previous->is(TT_LeadingJavaAnnotation))
          Current.setType(TT_LeadingJavaAnnotation);
        else
          Current.setType(TT_JavaAnnotation);
      } else if (Current.Previous->is(tok::period) &&
                 Current.Previous->isOneOf(TT_JavaAnnotation,
                                           TT_LeadingJavaAnnotation)) {
        Current.setType(Current.Previous->getType());
      }
    }
  }

  /// Take a guess at whether \p Tok starts a name of a function or
  /// variable declaration.
  ///
  /// This is a heuristic based on whether \p Tok is an identifier following
  /// something that is likely a type.
  bool isStartOfName(const FormatToken &Tok) {
    // Handled in ExpressionParser for Verilog.
    if (Style.isVerilog())
      return false;

    if (Tok.isNot(tok::identifier) || !Tok.Previous)
      return false;

    if (const auto *NextNonComment = Tok.getNextNonComment();
        (!NextNonComment && !Line.InMacroBody) ||
        (NextNonComment &&
         (NextNonComment->isPointerOrReference() ||
          NextNonComment->is(tok::string_literal) ||
          (Line.InPragmaDirective && NextNonComment->is(tok::identifier))))) {
      return false;
    }

    if (Tok.Previous->isOneOf(TT_LeadingJavaAnnotation, Keywords.kw_instanceof,
                              Keywords.kw_as)) {
      return false;
    }
    if (Style.isJavaScript() && Tok.Previous->is(Keywords.kw_in))
      return false;

    // Skip "const" as it does not have an influence on whether this is a name.
    FormatToken *PreviousNotConst = Tok.getPreviousNonComment();

    // For javascript const can be like "let" or "var"
    if (!Style.isJavaScript())
      while (PreviousNotConst && PreviousNotConst->is(tok::kw_const))
        PreviousNotConst = PreviousNotConst->getPreviousNonComment();

    if (!PreviousNotConst)
      return false;

    if (PreviousNotConst->ClosesRequiresClause)
      return false;

    if (Style.isTableGen()) {
      // keywords such as let and def* defines names.
      if (Keywords.isTableGenDefinition(*PreviousNotConst))
        return true;
      // Otherwise C++ style declarations is available only inside the brace.
      if (Contexts.back().ContextKind != tok::l_brace)
        return false;
    }

    bool IsPPKeyword = PreviousNotConst->is(tok::identifier) &&
                       PreviousNotConst->Previous &&
                       PreviousNotConst->Previous->is(tok::hash);

    if (PreviousNotConst->is(TT_TemplateCloser)) {
      return PreviousNotConst && PreviousNotConst->MatchingParen &&
             PreviousNotConst->MatchingParen->Previous &&
             PreviousNotConst->MatchingParen->Previous->isNot(tok::period) &&
             PreviousNotConst->MatchingParen->Previous->isNot(tok::kw_template);
    }

    if ((PreviousNotConst->is(tok::r_paren) &&
         PreviousNotConst->is(TT_TypeDeclarationParen)) ||
        PreviousNotConst->is(TT_AttributeRParen)) {
      return true;
    }

    // If is a preprocess keyword like #define.
    if (IsPPKeyword)
      return false;

    // int a or auto a.
    if (PreviousNotConst->isOneOf(tok::identifier, tok::kw_auto) &&
        PreviousNotConst->isNot(TT_StatementAttributeLikeMacro)) {
      return true;
    }

    // *a or &a or &&a.
    if (PreviousNotConst->is(TT_PointerOrReference))
      return true;

    // MyClass a;
    if (PreviousNotConst->isTypeName(LangOpts))
      return true;

    // type[] a in Java
    if (Style.Language == FormatStyle::LK_Java &&
        PreviousNotConst->is(tok::r_square)) {
      return true;
    }

    // const a = in JavaScript.
    return Style.isJavaScript() && PreviousNotConst->is(tok::kw_const);
  }

  /// Determine whether '(' is starting a C++ cast.
  bool lParenStartsCppCast(const FormatToken &Tok) {
    // C-style casts are only used in C++.
    if (!IsCpp)
      return false;

    FormatToken *LeftOfParens = Tok.getPreviousNonComment();
    if (LeftOfParens && LeftOfParens->is(TT_TemplateCloser) &&
        LeftOfParens->MatchingParen) {
      auto *Prev = LeftOfParens->MatchingParen->getPreviousNonComment();
      if (Prev &&
          Prev->isOneOf(tok::kw_const_cast, tok::kw_dynamic_cast,
                        tok::kw_reinterpret_cast, tok::kw_static_cast)) {
        // FIXME: Maybe we should handle identifiers ending with "_cast",
        // e.g. any_cast?
        return true;
      }
    }
    return false;
  }

  /// Determine whether ')' is ending a cast.
  bool rParenEndsCast(const FormatToken &Tok) {
    assert(Tok.is(tok::r_paren));

    if (!Tok.MatchingParen || !Tok.Previous)
      return false;

    // C-style casts are only used in C++, C# and Java.
    if (!IsCpp && !Style.isCSharp() && Style.Language != FormatStyle::LK_Java)
      return false;

    const auto *LParen = Tok.MatchingParen;
    const auto *BeforeRParen = Tok.Previous;
    const auto *AfterRParen = Tok.Next;

    // Empty parens aren't casts and there are no casts at the end of the line.
    if (BeforeRParen == LParen || !AfterRParen)
      return false;

    if (LParen->is(TT_OverloadedOperatorLParen))
      return false;

    auto *LeftOfParens = LParen->getPreviousNonComment();
    if (LeftOfParens) {
      // If there is a closing parenthesis left of the current
      // parentheses, look past it as these might be chained casts.
      if (LeftOfParens->is(tok::r_paren) &&
          LeftOfParens->isNot(TT_CastRParen)) {
        if (!LeftOfParens->MatchingParen ||
            !LeftOfParens->MatchingParen->Previous) {
          return false;
        }
        LeftOfParens = LeftOfParens->MatchingParen->Previous;
      }

      if (LeftOfParens->is(tok::r_square)) {
        //   delete[] (void *)ptr;
        auto MayBeArrayDelete = [](FormatToken *Tok) -> FormatToken * {
          if (Tok->isNot(tok::r_square))
            return nullptr;

          Tok = Tok->getPreviousNonComment();
          if (!Tok || Tok->isNot(tok::l_square))
            return nullptr;

          Tok = Tok->getPreviousNonComment();
          if (!Tok || Tok->isNot(tok::kw_delete))
            return nullptr;
          return Tok;
        };
        if (FormatToken *MaybeDelete = MayBeArrayDelete(LeftOfParens))
          LeftOfParens = MaybeDelete;
      }

      // The Condition directly below this one will see the operator arguments
      // as a (void *foo) cast.
      //   void operator delete(void *foo) ATTRIB;
      if (LeftOfParens->Tok.getIdentifierInfo() && LeftOfParens->Previous &&
          LeftOfParens->Previous->is(tok::kw_operator)) {
        return false;
      }

      // If there is an identifier (or with a few exceptions a keyword) right
      // before the parentheses, this is unlikely to be a cast.
      if (LeftOfParens->Tok.getIdentifierInfo() &&
          !LeftOfParens->isOneOf(Keywords.kw_in, tok::kw_return, tok::kw_case,
                                 tok::kw_delete, tok::kw_throw)) {
        return false;
      }

      // Certain other tokens right before the parentheses are also signals that
      // this cannot be a cast.
      if (LeftOfParens->isOneOf(tok::at, tok::r_square, TT_OverloadedOperator,
                                TT_TemplateCloser, tok::ellipsis)) {
        return false;
      }
    }

    if (AfterRParen->is(tok::question) ||
        (AfterRParen->is(tok::ampamp) && !BeforeRParen->isTypeName(LangOpts))) {
      return false;
    }

    // `foreach((A a, B b) in someList)` should not be seen as a cast.
    if (AfterRParen->is(Keywords.kw_in) && Style.isCSharp())
      return false;

    // Functions which end with decorations like volatile, noexcept are unlikely
    // to be casts.
    if (AfterRParen->isOneOf(tok::kw_noexcept, tok::kw_volatile, tok::kw_const,
                             tok::kw_requires, tok::kw_throw, tok::arrow,
                             Keywords.kw_override, Keywords.kw_final) ||
        isCppAttribute(IsCpp, *AfterRParen)) {
      return false;
    }

    // As Java has no function types, a "(" after the ")" likely means that this
    // is a cast.
    if (Style.Language == FormatStyle::LK_Java && AfterRParen->is(tok::l_paren))
      return true;

    // If a (non-string) literal follows, this is likely a cast.
    if (AfterRParen->isOneOf(tok::kw_sizeof, tok::kw_alignof) ||
        (AfterRParen->Tok.isLiteral() &&
         AfterRParen->isNot(tok::string_literal))) {
      return true;
    }

    // Heuristically try to determine whether the parentheses contain a type.
    auto IsQualifiedPointerOrReference = [](const FormatToken *T,
                                            const LangOptions &LangOpts) {
      // This is used to handle cases such as x = (foo *const)&y;
      assert(!T->isTypeName(LangOpts) && "Should have already been checked");
      // Strip trailing qualifiers such as const or volatile when checking
      // whether the parens could be a cast to a pointer/reference type.
      while (T) {
        if (T->is(TT_AttributeRParen)) {
          // Handle `x = (foo *__attribute__((foo)))&v;`:
          assert(T->is(tok::r_paren));
          assert(T->MatchingParen);
          assert(T->MatchingParen->is(tok::l_paren));
          assert(T->MatchingParen->is(TT_AttributeLParen));
          if (const auto *Tok = T->MatchingParen->Previous;
              Tok && Tok->isAttribute()) {
            T = Tok->Previous;
            continue;
          }
        } else if (T->is(TT_AttributeSquare)) {
          // Handle `x = (foo *[[clang::foo]])&v;`:
          if (T->MatchingParen && T->MatchingParen->Previous) {
            T = T->MatchingParen->Previous;
            continue;
          }
        } else if (T->canBePointerOrReferenceQualifier()) {
          T = T->Previous;
          continue;
        }
        break;
      }
      return T && T->is(TT_PointerOrReference);
    };
    bool ParensAreType =
        BeforeRParen->isOneOf(TT_TemplateCloser, TT_TypeDeclarationParen) ||
        BeforeRParen->isTypeName(LangOpts) ||
        IsQualifiedPointerOrReference(BeforeRParen, LangOpts);
    bool ParensCouldEndDecl =
        AfterRParen->isOneOf(tok::equal, tok::semi, tok::l_brace, tok::greater);
    if (ParensAreType && !ParensCouldEndDecl)
      return true;

    // At this point, we heuristically assume that there are no casts at the
    // start of the line. We assume that we have found most cases where there
    // are by the logic above, e.g. "(void)x;".
    if (!LeftOfParens)
      return false;

    // Certain token types inside the parentheses mean that this can't be a
    // cast.
    for (const auto *Token = LParen->Next; Token != &Tok; Token = Token->Next)
      if (Token->is(TT_BinaryOperator))
        return false;

    // If the following token is an identifier or 'this', this is a cast. All
    // cases where this can be something else are handled above.
    if (AfterRParen->isOneOf(tok::identifier, tok::kw_this))
      return true;

    // Look for a cast `( x ) (`.
    if (AfterRParen->is(tok::l_paren) && BeforeRParen->Previous) {
      if (BeforeRParen->is(tok::identifier) &&
          BeforeRParen->Previous->is(tok::l_paren)) {
        return true;
      }
    }

    if (!AfterRParen->Next)
      return false;

    if (AfterRParen->is(tok::l_brace) &&
        AfterRParen->getBlockKind() == BK_BracedInit) {
      return true;
    }

    // If the next token after the parenthesis is a unary operator, assume
    // that this is cast, unless there are unexpected tokens inside the
    // parenthesis.
    const bool NextIsAmpOrStar = AfterRParen->isOneOf(tok::amp, tok::star);
    if (!(AfterRParen->isUnaryOperator() || NextIsAmpOrStar) ||
        AfterRParen->is(tok::plus) ||
        !AfterRParen->Next->isOneOf(tok::identifier, tok::numeric_constant)) {
      return false;
    }

    if (NextIsAmpOrStar &&
        (AfterRParen->Next->is(tok::numeric_constant) || Line.InPPDirective)) {
      return false;
    }

    if (Line.InPPDirective && AfterRParen->is(tok::minus))
      return false;

    // Search for unexpected tokens.
    for (auto *Prev = BeforeRParen; Prev != LParen; Prev = Prev->Previous) {
      if (Prev->is(tok::r_paren)) {
        if (Prev->is(TT_CastRParen))
          return false;
        Prev = Prev->MatchingParen;
        if (!Prev)
          return false;
        if (Prev->is(TT_FunctionTypeLParen))
          break;
        continue;
      }
      if (!Prev->isOneOf(tok::kw_const, tok::identifier, tok::coloncolon))
        return false;
    }

    return true;
  }

  /// Returns true if the token is used as a unary operator.
  bool determineUnaryOperatorByUsage(const FormatToken &Tok) {
    const FormatToken *PrevToken = Tok.getPreviousNonComment();
    if (!PrevToken)
      return true;

    // These keywords are deliberately not included here because they may
    // precede only one of unary star/amp and plus/minus but not both.  They are
    // either included in determineStarAmpUsage or determinePlusMinusCaretUsage.
    //
    // @ - It may be followed by a unary `-` in Objective-C literals. We don't
    //   know how they can be followed by a star or amp.
    if (PrevToken->isOneOf(
            TT_ConditionalExpr, tok::l_paren, tok::comma, tok::colon, tok::semi,
            tok::equal, tok::question, tok::l_square, tok::l_brace,
            tok::kw_case, tok::kw_co_await, tok::kw_co_return, tok::kw_co_yield,
            tok::kw_delete, tok::kw_return, tok::kw_throw)) {
      return true;
    }

    // We put sizeof here instead of only in determineStarAmpUsage. In the cases
    // where the unary `+` operator is overloaded, it is reasonable to write
    // things like `sizeof +x`. Like commit 446d6ec996c6c3.
    if (PrevToken->is(tok::kw_sizeof))
      return true;

    // A sequence of leading unary operators.
    if (PrevToken->isOneOf(TT_CastRParen, TT_UnaryOperator))
      return true;

    // There can't be two consecutive binary operators.
    if (PrevToken->is(TT_BinaryOperator))
      return true;

    return false;
  }

  /// Return the type of the given token assuming it is * or &.
  TokenType determineStarAmpUsage(const FormatToken &Tok, bool IsExpression,
                                  bool InTemplateArgument) {
    if (Style.isJavaScript())
      return TT_BinaryOperator;

    // && in C# must be a binary operator.
    if (Style.isCSharp() && Tok.is(tok::ampamp))
      return TT_BinaryOperator;

    if (Style.isVerilog()) {
      // In Verilog, `*` can only be a binary operator.  `&` can be either unary
      // or binary.  `*` also includes `*>` in module path declarations in
      // specify blocks because merged tokens take the type of the first one by
      // default.
      if (Tok.is(tok::star))
        return TT_BinaryOperator;
      return determineUnaryOperatorByUsage(Tok) ? TT_UnaryOperator
                                                : TT_BinaryOperator;
    }

    const FormatToken *PrevToken = Tok.getPreviousNonComment();
    if (!PrevToken)
      return TT_UnaryOperator;
    if (PrevToken->is(TT_TypeName))
      return TT_PointerOrReference;
    if (PrevToken->isOneOf(tok::kw_new, tok::kw_delete) && Tok.is(tok::ampamp))
      return TT_BinaryOperator;

    const FormatToken *NextToken = Tok.getNextNonComment();

    if (InTemplateArgument && NextToken && NextToken->is(tok::kw_noexcept))
      return TT_BinaryOperator;

    if (!NextToken ||
        NextToken->isOneOf(tok::arrow, tok::equal, tok::comma, tok::r_paren,
                           TT_RequiresClause) ||
        (NextToken->is(tok::kw_noexcept) && !IsExpression) ||
        NextToken->canBePointerOrReferenceQualifier() ||
        (NextToken->is(tok::l_brace) && !NextToken->getNextNonComment())) {
      return TT_PointerOrReference;
    }

    if (PrevToken->is(tok::coloncolon))
      return TT_PointerOrReference;

    if (PrevToken->is(tok::r_paren) && PrevToken->is(TT_TypeDeclarationParen))
      return TT_PointerOrReference;

    if (determineUnaryOperatorByUsage(Tok))
      return TT_UnaryOperator;

    if (NextToken->is(tok::l_square) && NextToken->isNot(TT_LambdaLSquare))
      return TT_PointerOrReference;
    if (NextToken->is(tok::kw_operator) && !IsExpression)
      return TT_PointerOrReference;
    if (NextToken->isOneOf(tok::comma, tok::semi))
      return TT_PointerOrReference;

    // After right braces, star tokens are likely to be pointers to struct,
    // union, or class.
    //   struct {} *ptr;
    // This by itself is not sufficient to distinguish from multiplication
    // following a brace-initialized expression, as in:
    // int i = int{42} * 2;
    // In the struct case, the part of the struct declaration until the `{` and
    // the `}` are put on separate unwrapped lines; in the brace-initialized
    // case, the matching `{` is on the same unwrapped line, so check for the
    // presence of the matching brace to distinguish between those.
    if (PrevToken->is(tok::r_brace) && Tok.is(tok::star) &&
        !PrevToken->MatchingParen) {
      return TT_PointerOrReference;
    }

    if (PrevToken->endsSequence(tok::r_square, tok::l_square, tok::kw_delete))
      return TT_UnaryOperator;

    if (PrevToken->Tok.isLiteral() ||
        PrevToken->isOneOf(tok::r_paren, tok::r_square, tok::kw_true,
                           tok::kw_false, tok::r_brace)) {
      return TT_BinaryOperator;
    }

    const FormatToken *NextNonParen = NextToken;
    while (NextNonParen && NextNonParen->is(tok::l_paren))
      NextNonParen = NextNonParen->getNextNonComment();
    if (NextNonParen && (NextNonParen->Tok.isLiteral() ||
                         NextNonParen->isOneOf(tok::kw_true, tok::kw_false) ||
                         NextNonParen->isUnaryOperator())) {
      return TT_BinaryOperator;
    }

    // If we know we're in a template argument, there are no named declarations.
    // Thus, having an identifier on the right-hand side indicates a binary
    // operator.
    if (InTemplateArgument && NextToken->Tok.isAnyIdentifier())
      return TT_BinaryOperator;

    // "&&" followed by "(", "*", or "&" is quite unlikely to be two successive
    // unary "&".
    if (Tok.is(tok::ampamp) &&
        NextToken->isOneOf(tok::l_paren, tok::star, tok::amp)) {
      return TT_BinaryOperator;
    }

    // This catches some cases where evaluation order is used as control flow:
    //   aaa && aaa->f();
    if (NextToken->Tok.isAnyIdentifier()) {
      const FormatToken *NextNextToken = NextToken->getNextNonComment();
      if (NextNextToken && NextNextToken->is(tok::arrow))
        return TT_BinaryOperator;
    }

    // It is very unlikely that we are going to find a pointer or reference type
    // definition on the RHS of an assignment.
    if (IsExpression && !Contexts.back().CaretFound)
      return TT_BinaryOperator;

    // Opeartors at class scope are likely pointer or reference members.
    if (!Scopes.empty() && Scopes.back() == ST_Class)
      return TT_PointerOrReference;

    // Tokens that indicate member access or chained operator& use.
    auto IsChainedOperatorAmpOrMember = [](const FormatToken *token) {
      return !token || token->isOneOf(tok::amp, tok::period, tok::arrow,
                                      tok::arrowstar, tok::periodstar);
    };

    // It's more likely that & represents operator& than an uninitialized
    // reference.
    if (Tok.is(tok::amp) && PrevToken && PrevToken->Tok.isAnyIdentifier() &&
        IsChainedOperatorAmpOrMember(PrevToken->getPreviousNonComment()) &&
        NextToken && NextToken->Tok.isAnyIdentifier()) {
      if (auto NextNext = NextToken->getNextNonComment();
          NextNext &&
          (IsChainedOperatorAmpOrMember(NextNext) || NextNext->is(tok::semi))) {
        return TT_BinaryOperator;
      }
    }

    return TT_PointerOrReference;
  }

  TokenType determinePlusMinusCaretUsage(const FormatToken &Tok) {
    if (determineUnaryOperatorByUsage(Tok))
      return TT_UnaryOperator;

    const FormatToken *PrevToken = Tok.getPreviousNonComment();
    if (!PrevToken)
      return TT_UnaryOperator;

    if (PrevToken->is(tok::at))
      return TT_UnaryOperator;

    // Fall back to marking the token as binary operator.
    return TT_BinaryOperator;
  }

  /// Determine whether ++/-- are pre- or post-increments/-decrements.
  TokenType determineIncrementUsage(const FormatToken &Tok) {
    const FormatToken *PrevToken = Tok.getPreviousNonComment();
    if (!PrevToken || PrevToken->is(TT_CastRParen))
      return TT_UnaryOperator;
    if (PrevToken->isOneOf(tok::r_paren, tok::r_square, tok::identifier))
      return TT_TrailingUnaryOperator;

    return TT_UnaryOperator;
  }

  SmallVector<Context, 8> Contexts;

  const FormatStyle &Style;
  AnnotatedLine &Line;
  FormatToken *CurrentToken;
  bool AutoFound;
  bool IsCpp;
  LangOptions LangOpts;
  const AdditionalKeywords &Keywords;

  SmallVector<ScopeType> &Scopes;

  // Set of "<" tokens that do not open a template parameter list. If parseAngle
  // determines that a specific token can't be a template opener, it will make
  // same decision irrespective of the decisions for tokens leading up to it.
  // Store this information to prevent this from causing exponential runtime.
  llvm::SmallPtrSet<FormatToken *, 16> NonTemplateLess;

  int TemplateDeclarationDepth;
};

static const int PrecedenceUnaryOperator = prec::PointerToMember + 1;
static const int PrecedenceArrowAndPeriod = prec::PointerToMember + 2;

/// Parses binary expressions by inserting fake parenthesis based on
/// operator precedence.
class ExpressionParser {
public:
  ExpressionParser(const FormatStyle &Style, const AdditionalKeywords &Keywords,
                   AnnotatedLine &Line)
      : Style(Style), Keywords(Keywords), Line(Line), Current(Line.First) {}

  /// Parse expressions with the given operator precedence.
  void parse(int Precedence = 0) {
    // Skip 'return' and ObjC selector colons as they are not part of a binary
    // expression.
    while (Current && (Current->is(tok::kw_return) ||
                       (Current->is(tok::colon) &&
                        Current->isOneOf(TT_ObjCMethodExpr, TT_DictLiteral)))) {
      next();
    }

    if (!Current || Precedence > PrecedenceArrowAndPeriod)
      return;

    // Conditional expressions need to be parsed separately for proper nesting.
    if (Precedence == prec::Conditional) {
      parseConditionalExpr();
      return;
    }

    // Parse unary operators, which all have a higher precedence than binary
    // operators.
    if (Precedence == PrecedenceUnaryOperator) {
      parseUnaryOperator();
      return;
    }

    FormatToken *Start = Current;
    FormatToken *LatestOperator = nullptr;
    unsigned OperatorIndex = 0;
    // The first name of the current type in a port list.
    FormatToken *VerilogFirstOfType = nullptr;

    while (Current) {
      // In Verilog ports in a module header that don't have a type take the
      // type of the previous one.  For example,
      //   module a(output b,
      //                   c,
      //            output d);
      // In this case there need to be fake parentheses around b and c.
      if (Style.isVerilog() && Precedence == prec::Comma) {
        VerilogFirstOfType =
            verilogGroupDecl(VerilogFirstOfType, LatestOperator);
      }

      // Consume operators with higher precedence.
      parse(Precedence + 1);

      int CurrentPrecedence = getCurrentPrecedence();

      if (Precedence == CurrentPrecedence && Current &&
          Current->is(TT_SelectorName)) {
        if (LatestOperator)
          addFakeParenthesis(Start, prec::Level(Precedence));
        Start = Current;
      }

      if ((Style.isCSharp() || Style.isJavaScript() ||
           Style.Language == FormatStyle::LK_Java) &&
          Precedence == prec::Additive && Current) {
        // A string can be broken without parentheses around it when it is
        // already in a sequence of strings joined by `+` signs.
        FormatToken *Prev = Current->getPreviousNonComment();
        if (Prev && Prev->is(tok::string_literal) &&
            (Prev == Start || Prev->endsSequence(tok::string_literal, tok::plus,
                                                 TT_StringInConcatenation))) {
          Prev->setType(TT_StringInConcatenation);
        }
      }

      // At the end of the line or when an operator with lower precedence is
      // found, insert fake parenthesis and return.
      if (!Current ||
          (Current->closesScope() &&
           (Current->MatchingParen || Current->is(TT_TemplateString))) ||
          (CurrentPrecedence != -1 && CurrentPrecedence < Precedence) ||
          (CurrentPrecedence == prec::Conditional &&
           Precedence == prec::Assignment && Current->is(tok::colon))) {
        break;
      }

      // Consume scopes: (), [], <> and {}
      // In addition to that we handle require clauses as scope, so that the
      // constraints in that are correctly indented.
      if (Current->opensScope() ||
          Current->isOneOf(TT_RequiresClause,
                           TT_RequiresClauseInARequiresExpression)) {
        // In fragment of a JavaScript template string can look like '}..${' and
        // thus close a scope and open a new one at the same time.
        while (Current && (!Current->closesScope() || Current->opensScope())) {
          next();
          parse();
        }
        next();
      } else {
        // Operator found.
        if (CurrentPrecedence == Precedence) {
          if (LatestOperator)
            LatestOperator->NextOperator = Current;
          LatestOperator = Current;
          Current->OperatorIndex = OperatorIndex;
          ++OperatorIndex;
        }
        next(/*SkipPastLeadingComments=*/Precedence > 0);
      }
    }

    // Group variables of the same type.
    if (Style.isVerilog() && Precedence == prec::Comma && VerilogFirstOfType)
      addFakeParenthesis(VerilogFirstOfType, prec::Comma);

    if (LatestOperator && (Current || Precedence > 0)) {
      // The requires clauses do not neccessarily end in a semicolon or a brace,
      // but just go over to struct/class or a function declaration, we need to
      // intervene so that the fake right paren is inserted correctly.
      auto End =
          (Start->Previous &&
           Start->Previous->isOneOf(TT_RequiresClause,
                                    TT_RequiresClauseInARequiresExpression))
              ? [this]() {
                  auto Ret = Current ? Current : Line.Last;
                  while (!Ret->ClosesRequiresClause && Ret->Previous)
                    Ret = Ret->Previous;
                  return Ret;
                }()
              : nullptr;

      if (Precedence == PrecedenceArrowAndPeriod) {
        // Call expressions don't have a binary operator precedence.
        addFakeParenthesis(Start, prec::Unknown, End);
      } else {
        addFakeParenthesis(Start, prec::Level(Precedence), End);
      }
    }
  }

private:
  /// Gets the precedence (+1) of the given token for binary operators
  /// and other tokens that we treat like binary operators.
  int getCurrentPrecedence() {
    if (Current) {
      const FormatToken *NextNonComment = Current->getNextNonComment();
      if (Current->is(TT_ConditionalExpr))
        return prec::Conditional;
      if (NextNonComment && Current->is(TT_SelectorName) &&
          (NextNonComment->isOneOf(TT_DictLiteral, TT_JsTypeColon) ||
           (Style.isProto() && NextNonComment->is(tok::less)))) {
        return prec::Assignment;
      }
      if (Current->is(TT_JsComputedPropertyName))
        return prec::Assignment;
      if (Current->is(TT_LambdaArrow))
        return prec::Comma;
      if (Current->is(TT_FatArrow))
        return prec::Assignment;
      if (Current->isOneOf(tok::semi, TT_InlineASMColon, TT_SelectorName) ||
          (Current->is(tok::comment) && NextNonComment &&
           NextNonComment->is(TT_SelectorName))) {
        return 0;
      }
      if (Current->is(TT_RangeBasedForLoopColon))
        return prec::Comma;
      if ((Style.Language == FormatStyle::LK_Java || Style.isJavaScript()) &&
          Current->is(Keywords.kw_instanceof)) {
        return prec::Relational;
      }
      if (Style.isJavaScript() &&
          Current->isOneOf(Keywords.kw_in, Keywords.kw_as)) {
        return prec::Relational;
      }
      if (Current->is(TT_BinaryOperator) || Current->is(tok::comma))
        return Current->getPrecedence();
      if (Current->isOneOf(tok::period, tok::arrow) &&
          Current->isNot(TT_TrailingReturnArrow)) {
        return PrecedenceArrowAndPeriod;
      }
      if ((Style.Language == FormatStyle::LK_Java || Style.isJavaScript()) &&
          Current->isOneOf(Keywords.kw_extends, Keywords.kw_implements,
                           Keywords.kw_throws)) {
        return 0;
      }
      // In Verilog case labels are not on separate lines straight out of
      // UnwrappedLineParser. The colon is not part of an expression.
      if (Style.isVerilog() && Current->is(tok::colon))
        return 0;
    }
    return -1;
  }

  void addFakeParenthesis(FormatToken *Start, prec::Level Precedence,
                          FormatToken *End = nullptr) {
    // Do not assign fake parenthesis to tokens that are part of an
    // unexpanded macro call. The line within the macro call contains
    // the parenthesis and commas, and we will not find operators within
    // that structure.
    if (Start->MacroParent)
      return;

    Start->FakeLParens.push_back(Precedence);
    if (Precedence > prec::Unknown)
      Start->StartsBinaryExpression = true;
    if (!End && Current)
      End = Current->getPreviousNonComment();
    if (End) {
      ++End->FakeRParens;
      if (Precedence > prec::Unknown)
        End->EndsBinaryExpression = true;
    }
  }

  /// Parse unary operator expressions and surround them with fake
  /// parentheses if appropriate.
  void parseUnaryOperator() {
    llvm::SmallVector<FormatToken *, 2> Tokens;
    while (Current && Current->is(TT_UnaryOperator)) {
      Tokens.push_back(Current);
      next();
    }
    parse(PrecedenceArrowAndPeriod);
    for (FormatToken *Token : llvm::reverse(Tokens)) {
      // The actual precedence doesn't matter.
      addFakeParenthesis(Token, prec::Unknown);
    }
  }

  void parseConditionalExpr() {
    while (Current && Current->isTrailingComment())
      next();
    FormatToken *Start = Current;
    parse(prec::LogicalOr);
    if (!Current || Current->isNot(tok::question))
      return;
    next();
    parse(prec::Assignment);
    if (!Current || Current->isNot(TT_ConditionalExpr))
      return;
    next();
    parse(prec::Assignment);
    addFakeParenthesis(Start, prec::Conditional);
  }

  void next(bool SkipPastLeadingComments = true) {
    if (Current)
      Current = Current->Next;
    while (Current &&
           (Current->NewlinesBefore == 0 || SkipPastLeadingComments) &&
           Current->isTrailingComment()) {
      Current = Current->Next;
    }
  }

  // Add fake parenthesis around declarations of the same type for example in a
  // module prototype. Return the first port / variable of the current type.
  FormatToken *verilogGroupDecl(FormatToken *FirstOfType,
                                FormatToken *PreviousComma) {
    if (!Current)
      return nullptr;

    FormatToken *Start = Current;

    // Skip attributes.
    while (Start->startsSequence(tok::l_paren, tok::star)) {
      if (!(Start = Start->MatchingParen) ||
          !(Start = Start->getNextNonComment())) {
        return nullptr;
      }
    }

    FormatToken *Tok = Start;

    if (Tok->is(Keywords.kw_assign))
      Tok = Tok->getNextNonComment();

    // Skip any type qualifiers to find the first identifier. It may be either a
    // new type name or a variable name. There can be several type qualifiers
    // preceding a variable name, and we can not tell them apart by looking at
    // the word alone since a macro can be defined as either a type qualifier or
    // a variable name. Thus we use the last word before the dimensions instead
    // of the first word as the candidate for the variable or type name.
    FormatToken *First = nullptr;
    while (Tok) {
      FormatToken *Next = Tok->getNextNonComment();

      if (Tok->is(tok::hash)) {
        // Start of a macro expansion.
        First = Tok;
        Tok = Next;
        if (Tok)
          Tok = Tok->getNextNonComment();
      } else if (Tok->is(tok::hashhash)) {
        // Concatenation. Skip.
        Tok = Next;
        if (Tok)
          Tok = Tok->getNextNonComment();
      } else if (Keywords.isVerilogQualifier(*Tok) ||
                 Keywords.isVerilogIdentifier(*Tok)) {
        First = Tok;
        Tok = Next;
        // The name may have dots like `interface_foo.modport_foo`.
        while (Tok && Tok->isOneOf(tok::period, tok::coloncolon) &&
               (Tok = Tok->getNextNonComment())) {
          if (Keywords.isVerilogIdentifier(*Tok))
            Tok = Tok->getNextNonComment();
        }
      } else if (!Next) {
        Tok = nullptr;
      } else if (Tok->is(tok::l_paren)) {
        // Make sure the parenthesized list is a drive strength. Otherwise the
        // statement may be a module instantiation in which case we have already
        // found the instance name.
        if (Next->isOneOf(
                Keywords.kw_highz0, Keywords.kw_highz1, Keywords.kw_large,
                Keywords.kw_medium, Keywords.kw_pull0, Keywords.kw_pull1,
                Keywords.kw_small, Keywords.kw_strong0, Keywords.kw_strong1,
                Keywords.kw_supply0, Keywords.kw_supply1, Keywords.kw_weak0,
                Keywords.kw_weak1)) {
          Tok->setType(TT_VerilogStrength);
          Tok = Tok->MatchingParen;
          if (Tok) {
            Tok->setType(TT_VerilogStrength);
            Tok = Tok->getNextNonComment();
          }
        } else {
          break;
        }
      } else if (Tok->is(Keywords.kw_verilogHash)) {
        // Delay control.
        if (Next->is(tok::l_paren))
          Next = Next->MatchingParen;
        if (Next)
          Tok = Next->getNextNonComment();
      } else {
        break;
      }
    }

    // Find the second identifier. If it exists it will be the name.
    FormatToken *Second = nullptr;
    // Dimensions.
    while (Tok && Tok->is(tok::l_square) && (Tok = Tok->MatchingParen))
      Tok = Tok->getNextNonComment();
    if (Tok && (Tok->is(tok::hash) || Keywords.isVerilogIdentifier(*Tok)))
      Second = Tok;

    // If the second identifier doesn't exist and there are qualifiers, the type
    // is implied.
    FormatToken *TypedName = nullptr;
    if (Second) {
      TypedName = Second;
      if (First && First->is(TT_Unknown))
        First->setType(TT_VerilogDimensionedTypeName);
    } else if (First != Start) {
      // If 'First' is null, then this isn't a declaration, 'TypedName' gets set
      // to null as intended.
      TypedName = First;
    }

    if (TypedName) {
      // This is a declaration with a new type.
      if (TypedName->is(TT_Unknown))
        TypedName->setType(TT_StartOfName);
      // Group variables of the previous type.
      if (FirstOfType && PreviousComma) {
        PreviousComma->setType(TT_VerilogTypeComma);
        addFakeParenthesis(FirstOfType, prec::Comma, PreviousComma->Previous);
      }

      FirstOfType = TypedName;

      // Don't let higher precedence handle the qualifiers. For example if we
      // have:
      //    parameter x = 0
      // We skip `parameter` here. This way the fake parentheses for the
      // assignment will be around `x = 0`.
      while (Current && Current != FirstOfType) {
        if (Current->opensScope()) {
          next();
          parse();
        }
        next();
      }
    }

    return FirstOfType;
  }

  const FormatStyle &Style;
  const AdditionalKeywords &Keywords;
  const AnnotatedLine &Line;
  FormatToken *Current;
};

} // end anonymous namespace

void TokenAnnotator::setCommentLineLevels(
    SmallVectorImpl<AnnotatedLine *> &Lines) const {
  const AnnotatedLine *NextNonCommentLine = nullptr;
  for (AnnotatedLine *Line : llvm::reverse(Lines)) {
    assert(Line->First);

    // If the comment is currently aligned with the line immediately following
    // it, that's probably intentional and we should keep it.
    if (NextNonCommentLine && NextNonCommentLine->First->NewlinesBefore < 2 &&
        Line->isComment() && !isClangFormatOff(Line->First->TokenText) &&
        NextNonCommentLine->First->OriginalColumn ==
            Line->First->OriginalColumn) {
      const bool PPDirectiveOrImportStmt =
          NextNonCommentLine->Type == LT_PreprocessorDirective ||
          NextNonCommentLine->Type == LT_ImportStatement;
      if (PPDirectiveOrImportStmt)
        Line->Type = LT_CommentAbovePPDirective;
      // Align comments for preprocessor lines with the # in column 0 if
      // preprocessor lines are not indented. Otherwise, align with the next
      // line.
      Line->Level = Style.IndentPPDirectives != FormatStyle::PPDIS_BeforeHash &&
                            PPDirectiveOrImportStmt
                        ? 0
                        : NextNonCommentLine->Level;
    } else {
      NextNonCommentLine = Line->First->isNot(tok::r_brace) ? Line : nullptr;
    }

    setCommentLineLevels(Line->Children);
  }
}

static unsigned maxNestingDepth(const AnnotatedLine &Line) {
  unsigned Result = 0;
  for (const auto *Tok = Line.First; Tok; Tok = Tok->Next)
    Result = std::max(Result, Tok->NestingLevel);
  return Result;
}

// Returns the name of a function with no return type, e.g. a constructor or
// destructor.
static FormatToken *getFunctionName(const AnnotatedLine &Line,
                                    FormatToken *&OpeningParen) {
  for (FormatToken *Tok = Line.getFirstNonComment(), *Name = nullptr; Tok;
       Tok = Tok->getNextNonComment()) {
    // Skip C++11 attributes both before and after the function name.
    if (Tok->is(tok::l_square) && Tok->is(TT_AttributeSquare)) {
      Tok = Tok->MatchingParen;
      if (!Tok)
        break;
      continue;
    }

    // Make sure the name is followed by a pair of parentheses.
    if (Name) {
      if (Tok->is(tok::l_paren) && Tok->isNot(TT_FunctionTypeLParen) &&
          Tok->MatchingParen) {
        OpeningParen = Tok;
        return Name;
      }
      return nullptr;
    }

    // Skip keywords that may precede the constructor/destructor name.
    if (Tok->isOneOf(tok::kw_friend, tok::kw_inline, tok::kw_virtual,
                     tok::kw_constexpr, tok::kw_consteval, tok::kw_explicit)) {
      continue;
    }

    // A qualified name may start from the global namespace.
    if (Tok->is(tok::coloncolon)) {
      Tok = Tok->Next;
      if (!Tok)
        break;
    }

    // Skip to the unqualified part of the name.
    while (Tok->startsSequence(tok::identifier, tok::coloncolon)) {
      assert(Tok->Next);
      Tok = Tok->Next->Next;
      if (!Tok)
        return nullptr;
    }

    // Skip the `~` if a destructor name.
    if (Tok->is(tok::tilde)) {
      Tok = Tok->Next;
      if (!Tok)
        break;
    }

    // Make sure the name is not already annotated, e.g. as NamespaceMacro.
    if (Tok->isNot(tok::identifier) || Tok->isNot(TT_Unknown))
      break;

    Name = Tok;
  }

  return nullptr;
}

// Checks if Tok is a constructor/destructor name qualified by its class name.
static bool isCtorOrDtorName(const FormatToken *Tok) {
  assert(Tok && Tok->is(tok::identifier));
  const auto *Prev = Tok->Previous;

  if (Prev && Prev->is(tok::tilde))
    Prev = Prev->Previous;

  if (!Prev || !Prev->endsSequence(tok::coloncolon, tok::identifier))
    return false;

  assert(Prev->Previous);
  return Prev->Previous->TokenText == Tok->TokenText;
}

void TokenAnnotator::annotate(AnnotatedLine &Line) {
  AnnotatingParser Parser(Style, Line, Keywords, Scopes);
  Line.Type = Parser.parseLine();

  for (auto &Child : Line.Children)
    annotate(*Child);

  // With very deep nesting, ExpressionParser uses lots of stack and the
  // formatting algorithm is very slow. We're not going to do a good job here
  // anyway - it's probably generated code being formatted by mistake.
  // Just skip the whole line.
  if (maxNestingDepth(Line) > 50)
    Line.Type = LT_Invalid;

  if (Line.Type == LT_Invalid)
    return;

  ExpressionParser ExprParser(Style, Keywords, Line);
  ExprParser.parse();

  if (IsCpp) {
    FormatToken *OpeningParen = nullptr;
    auto *Tok = getFunctionName(Line, OpeningParen);
    if (Tok && ((!Scopes.empty() && Scopes.back() == ST_Class) ||
                Line.endsWith(TT_FunctionLBrace) || isCtorOrDtorName(Tok))) {
      Tok->setFinalizedType(TT_CtorDtorDeclName);
      assert(OpeningParen);
      OpeningParen->setFinalizedType(TT_FunctionDeclarationLParen);
    }
  }

  if (Line.startsWith(TT_ObjCMethodSpecifier))
    Line.Type = LT_ObjCMethodDecl;
  else if (Line.startsWith(TT_ObjCDecl))
    Line.Type = LT_ObjCDecl;
  else if (Line.startsWith(TT_ObjCProperty))
    Line.Type = LT_ObjCProperty;

  auto *First = Line.First;
  First->SpacesRequiredBefore = 1;
  First->CanBreakBefore = First->MustBreakBefore;
}

// This function heuristically determines whether 'Current' starts the name of a
// function declaration.
static bool isFunctionDeclarationName(const LangOptions &LangOpts,
                                      const FormatToken &Current,
                                      const AnnotatedLine &Line,
                                      FormatToken *&ClosingParen) {
  assert(Current.Previous);

  if (Current.is(TT_FunctionDeclarationName))
    return true;

  if (!Current.Tok.getIdentifierInfo())
    return false;

  const auto &Previous = *Current.Previous;

  if (const auto *PrevPrev = Previous.Previous;
      PrevPrev && PrevPrev->is(TT_ObjCDecl)) {
    return false;
  }

  auto skipOperatorName =
      [&LangOpts](const FormatToken *Next) -> const FormatToken * {
    for (; Next; Next = Next->Next) {
      if (Next->is(TT_OverloadedOperatorLParen))
        return Next;
      if (Next->is(TT_OverloadedOperator))
        continue;
      if (Next->isOneOf(tok::kw_new, tok::kw_delete)) {
        // For 'new[]' and 'delete[]'.
        if (Next->Next &&
            Next->Next->startsSequence(tok::l_square, tok::r_square)) {
          Next = Next->Next->Next;
        }
        continue;
      }
      if (Next->startsSequence(tok::l_square, tok::r_square)) {
        // For operator[]().
        Next = Next->Next;
        continue;
      }
      if ((Next->isTypeName(LangOpts) || Next->is(tok::identifier)) &&
          Next->Next && Next->Next->isPointerOrReference()) {
        // For operator void*(), operator char*(), operator Foo*().
        Next = Next->Next;
        continue;
      }
      if (Next->is(TT_TemplateOpener) && Next->MatchingParen) {
        Next = Next->MatchingParen;
        continue;
      }

      break;
    }
    return nullptr;
  };

  const auto *Next = Current.Next;
  const bool IsCpp = LangOpts.CXXOperatorNames;

  // Find parentheses of parameter list.
  if (Current.is(tok::kw_operator)) {
    if (Previous.Tok.getIdentifierInfo() &&
        !Previous.isOneOf(tok::kw_return, tok::kw_co_return)) {
      return true;
    }
    if (Previous.is(tok::r_paren) && Previous.is(TT_TypeDeclarationParen)) {
      assert(Previous.MatchingParen);
      assert(Previous.MatchingParen->is(tok::l_paren));
      assert(Previous.MatchingParen->is(TT_TypeDeclarationParen));
      return true;
    }
    if (!Previous.isPointerOrReference() && Previous.isNot(TT_TemplateCloser))
      return false;
    Next = skipOperatorName(Next);
  } else {
    if (Current.isNot(TT_StartOfName) || Current.NestingLevel != 0)
      return false;
    for (; Next; Next = Next->Next) {
      if (Next->is(TT_TemplateOpener) && Next->MatchingParen) {
        Next = Next->MatchingParen;
      } else if (Next->is(tok::coloncolon)) {
        Next = Next->Next;
        if (!Next)
          return false;
        if (Next->is(tok::kw_operator)) {
          Next = skipOperatorName(Next->Next);
          break;
        }
        if (Next->isNot(tok::identifier))
          return false;
      } else if (isCppAttribute(IsCpp, *Next)) {
        Next = Next->MatchingParen;
        if (!Next)
          return false;
      } else if (Next->is(tok::l_paren)) {
        break;
      } else {
        return false;
      }
    }
  }

  // Check whether parameter list can belong to a function declaration.
  if (!Next || Next->isNot(tok::l_paren) || !Next->MatchingParen)
    return false;
  ClosingParen = Next->MatchingParen;
  assert(ClosingParen->is(tok::r_paren));
  // If the lines ends with "{", this is likely a function definition.
  if (Line.Last->is(tok::l_brace))
    return true;
  if (Next->Next == ClosingParen)
    return true; // Empty parentheses.
  // If there is an &/&& after the r_paren, this is likely a function.
  if (ClosingParen->Next && ClosingParen->Next->is(TT_PointerOrReference))
    return true;

  // Check for K&R C function definitions (and C++ function definitions with
  // unnamed parameters), e.g.:
  //   int f(i)
  //   {
  //     return i + 1;
  //   }
  //   bool g(size_t = 0, bool b = false)
  //   {
  //     return !b;
  //   }
  if (IsCpp && Next->Next && Next->Next->is(tok::identifier) &&
      !Line.endsWith(tok::semi)) {
    return true;
  }

  for (const FormatToken *Tok = Next->Next; Tok && Tok != ClosingParen;
       Tok = Tok->Next) {
    if (Tok->is(TT_TypeDeclarationParen))
      return true;
    if (Tok->isOneOf(tok::l_paren, TT_TemplateOpener) && Tok->MatchingParen) {
      Tok = Tok->MatchingParen;
      continue;
    }
    if (Tok->is(tok::kw_const) || Tok->isTypeName(LangOpts) ||
        Tok->isOneOf(TT_PointerOrReference, TT_StartOfName, tok::ellipsis)) {
      return true;
    }
    if (Tok->isOneOf(tok::l_brace, TT_ObjCMethodExpr) || Tok->Tok.isLiteral())
      return false;
  }
  return false;
}

bool TokenAnnotator::mustBreakForReturnType(const AnnotatedLine &Line) const {
  assert(Line.MightBeFunctionDecl);

  if ((Style.BreakAfterReturnType == FormatStyle::RTBS_TopLevel ||
       Style.BreakAfterReturnType == FormatStyle::RTBS_TopLevelDefinitions) &&
      Line.Level > 0) {
    return false;
  }

  switch (Style.BreakAfterReturnType) {
  case FormatStyle::RTBS_None:
  case FormatStyle::RTBS_Automatic:
  case FormatStyle::RTBS_ExceptShortType:
    return false;
  case FormatStyle::RTBS_All:
  case FormatStyle::RTBS_TopLevel:
    return true;
  case FormatStyle::RTBS_AllDefinitions:
  case FormatStyle::RTBS_TopLevelDefinitions:
    return Line.mightBeFunctionDefinition();
  }

  return false;
}

void TokenAnnotator::calculateFormattingInformation(AnnotatedLine &Line) const {
  for (AnnotatedLine *ChildLine : Line.Children)
    calculateFormattingInformation(*ChildLine);

  auto *First = Line.First;
  First->TotalLength = First->IsMultiline
                           ? Style.ColumnLimit
                           : Line.FirstStartColumn + First->ColumnWidth;
  FormatToken *Current = First->Next;
  bool InFunctionDecl = Line.MightBeFunctionDecl;
  bool AlignArrayOfStructures =
      (Style.AlignArrayOfStructures != FormatStyle::AIAS_None &&
       Line.Type == LT_ArrayOfStructInitializer);
  if (AlignArrayOfStructures)
    calculateArrayInitializerColumnList(Line);

  bool SeenName = false;
  bool LineIsFunctionDeclaration = false;
  FormatToken *ClosingParen = nullptr;
  FormatToken *AfterLastAttribute = nullptr;

  for (auto *Tok = Current; Tok; Tok = Tok->Next) {
    if (Tok->is(TT_StartOfName))
      SeenName = true;
    if (Tok->Previous->EndsCppAttributeGroup)
      AfterLastAttribute = Tok;
    if (const bool IsCtorOrDtor = Tok->is(TT_CtorDtorDeclName);
        IsCtorOrDtor ||
        isFunctionDeclarationName(LangOpts, *Tok, Line, ClosingParen)) {
      if (!IsCtorOrDtor)
        Tok->setFinalizedType(TT_FunctionDeclarationName);
      LineIsFunctionDeclaration = true;
      SeenName = true;
      if (ClosingParen) {
        auto *OpeningParen = ClosingParen->MatchingParen;
        assert(OpeningParen);
        if (OpeningParen->is(TT_Unknown))
          OpeningParen->setType(TT_FunctionDeclarationLParen);
      }
      break;
    }
  }

  if (IsCpp && (LineIsFunctionDeclaration || First->is(TT_CtorDtorDeclName)) &&
      Line.endsWith(tok::semi, tok::r_brace)) {
    auto *Tok = Line.Last->Previous;
    while (Tok->isNot(tok::r_brace))
      Tok = Tok->Previous;
    if (auto *LBrace = Tok->MatchingParen; LBrace) {
      assert(LBrace->is(tok::l_brace));
      Tok->setBlockKind(BK_Block);
      LBrace->setBlockKind(BK_Block);
      LBrace->setFinalizedType(TT_FunctionLBrace);
    }
  }

  if (IsCpp && SeenName && AfterLastAttribute &&
      mustBreakAfterAttributes(*AfterLastAttribute, Style)) {
    AfterLastAttribute->MustBreakBefore = true;
    if (LineIsFunctionDeclaration)
      Line.ReturnTypeWrapped = true;
  }

  if (IsCpp) {
    if (!LineIsFunctionDeclaration) {
      // Annotate */&/&& in `operator` function calls as binary operators.
      for (const auto *Tok = First; Tok; Tok = Tok->Next) {
        if (Tok->isNot(tok::kw_operator))
          continue;
        do {
          Tok = Tok->Next;
        } while (Tok && Tok->isNot(TT_OverloadedOperatorLParen));
        if (!Tok || !Tok->MatchingParen)
          break;
        const auto *LeftParen = Tok;
        for (Tok = Tok->Next; Tok && Tok != LeftParen->MatchingParen;
             Tok = Tok->Next) {
          if (Tok->isNot(tok::identifier))
            continue;
          auto *Next = Tok->Next;
          const bool NextIsBinaryOperator =
              Next && Next->isPointerOrReference() && Next->Next &&
              Next->Next->is(tok::identifier);
          if (!NextIsBinaryOperator)
            continue;
          Next->setType(TT_BinaryOperator);
          Tok = Next;
        }
      }
    } else if (ClosingParen) {
      for (auto *Tok = ClosingParen->Next; Tok; Tok = Tok->Next) {
        if (Tok->is(TT_CtorInitializerColon))
          break;
        if (Tok->is(tok::arrow)) {
          Tok->setType(TT_TrailingReturnArrow);
          break;
        }
        if (Tok->isNot(TT_TrailingAnnotation))
          continue;
        const auto *Next = Tok->Next;
        if (!Next || Next->isNot(tok::l_paren))
          continue;
        Tok = Next->MatchingParen;
        if (!Tok)
          break;
      }
    }
  }

  while (Current) {
    const FormatToken *Prev = Current->Previous;
    if (Current->is(TT_LineComment)) {
      if (Prev->is(BK_BracedInit) && Prev->opensScope()) {
        Current->SpacesRequiredBefore =
            (Style.Cpp11BracedListStyle && !Style.SpacesInParensOptions.Other)
                ? 0
                : 1;
      } else if (Prev->is(TT_VerilogMultiLineListLParen)) {
        Current->SpacesRequiredBefore = 0;
      } else {
        Current->SpacesRequiredBefore = Style.SpacesBeforeTrailingComments;
      }

      // If we find a trailing comment, iterate backwards to determine whether
      // it seems to relate to a specific parameter. If so, break before that
      // parameter to avoid changing the comment's meaning. E.g. don't move 'b'
      // to the previous line in:
      //   SomeFunction(a,
      //                b, // comment
      //                c);
      if (!Current->HasUnescapedNewline) {
        for (FormatToken *Parameter = Current->Previous; Parameter;
             Parameter = Parameter->Previous) {
          if (Parameter->isOneOf(tok::comment, tok::r_brace))
            break;
          if (Parameter->Previous && Parameter->Previous->is(tok::comma)) {
            if (Parameter->Previous->isNot(TT_CtorInitializerComma) &&
                Parameter->HasUnescapedNewline) {
              Parameter->MustBreakBefore = true;
            }
            break;
          }
        }
      }
    } else if (!Current->Finalized && Current->SpacesRequiredBefore == 0 &&
               spaceRequiredBefore(Line, *Current)) {
      Current->SpacesRequiredBefore = 1;
    }

    const auto &Children = Prev->Children;
    if (!Children.empty() && Children.back()->Last->is(TT_LineComment)) {
      Current->MustBreakBefore = true;
    } else {
      Current->MustBreakBefore =
          Current->MustBreakBefore || mustBreakBefore(Line, *Current);
      if (!Current->MustBreakBefore && InFunctionDecl &&
          Current->is(TT_FunctionDeclarationName)) {
        Current->MustBreakBefore = mustBreakForReturnType(Line);
      }
    }

    Current->CanBreakBefore =
        Current->MustBreakBefore || canBreakBefore(Line, *Current);
    unsigned ChildSize = 0;
    if (Prev->Children.size() == 1) {
      FormatToken &LastOfChild = *Prev->Children[0]->Last;
      ChildSize = LastOfChild.isTrailingComment() ? Style.ColumnLimit
                                                  : LastOfChild.TotalLength + 1;
    }
    if (Current->MustBreakBefore || Prev->Children.size() > 1 ||
        (Prev->Children.size() == 1 &&
         Prev->Children[0]->First->MustBreakBefore) ||
        Current->IsMultiline) {
      Current->TotalLength = Prev->TotalLength + Style.ColumnLimit;
    } else {
      Current->TotalLength = Prev->TotalLength + Current->ColumnWidth +
                             ChildSize + Current->SpacesRequiredBefore;
    }

    if (Current->is(TT_CtorInitializerColon))
      InFunctionDecl = false;

    // FIXME: Only calculate this if CanBreakBefore is true once static
    // initializers etc. are sorted out.
    // FIXME: Move magic numbers to a better place.

    // Reduce penalty for aligning ObjC method arguments using the colon
    // alignment as this is the canonical way (still prefer fitting everything
    // into one line if possible). Trying to fit a whole expression into one
    // line should not force other line breaks (e.g. when ObjC method
    // expression is a part of other expression).
    Current->SplitPenalty = splitPenalty(Line, *Current, InFunctionDecl);
    if (Style.Language == FormatStyle::LK_ObjC &&
        Current->is(TT_SelectorName) && Current->ParameterIndex > 0) {
      if (Current->ParameterIndex == 1)
        Current->SplitPenalty += 5 * Current->BindingStrength;
    } else {
      Current->SplitPenalty += 20 * Current->BindingStrength;
    }

    Current = Current->Next;
  }

  calculateUnbreakableTailLengths(Line);
  unsigned IndentLevel = Line.Level;
  for (Current = First; Current; Current = Current->Next) {
    if (Current->Role)
      Current->Role->precomputeFormattingInfos(Current);
    if (Current->MatchingParen &&
        Current->MatchingParen->opensBlockOrBlockTypeList(Style) &&
        IndentLevel > 0) {
      --IndentLevel;
    }
    Current->IndentLevel = IndentLevel;
    if (Current->opensBlockOrBlockTypeList(Style))
      ++IndentLevel;
  }

  LLVM_DEBUG({ printDebugInfo(Line); });
}

void TokenAnnotator::calculateUnbreakableTailLengths(
    AnnotatedLine &Line) const {
  unsigned UnbreakableTailLength = 0;
  FormatToken *Current = Line.Last;
  while (Current) {
    Current->UnbreakableTailLength = UnbreakableTailLength;
    if (Current->CanBreakBefore ||
        Current->isOneOf(tok::comment, tok::string_literal)) {
      UnbreakableTailLength = 0;
    } else {
      UnbreakableTailLength +=
          Current->ColumnWidth + Current->SpacesRequiredBefore;
    }
    Current = Current->Previous;
  }
}

void TokenAnnotator::calculateArrayInitializerColumnList(
    AnnotatedLine &Line) const {
  if (Line.First == Line.Last)
    return;
  auto *CurrentToken = Line.First;
  CurrentToken->ArrayInitializerLineStart = true;
  unsigned Depth = 0;
  while (CurrentToken && CurrentToken != Line.Last) {
    if (CurrentToken->is(tok::l_brace)) {
      CurrentToken->IsArrayInitializer = true;
      if (CurrentToken->Next)
        CurrentToken->Next->MustBreakBefore = true;
      CurrentToken =
          calculateInitializerColumnList(Line, CurrentToken->Next, Depth + 1);
    } else {
      CurrentToken = CurrentToken->Next;
    }
  }
}

FormatToken *TokenAnnotator::calculateInitializerColumnList(
    AnnotatedLine &Line, FormatToken *CurrentToken, unsigned Depth) const {
  while (CurrentToken && CurrentToken != Line.Last) {
    if (CurrentToken->is(tok::l_brace))
      ++Depth;
    else if (CurrentToken->is(tok::r_brace))
      --Depth;
    if (Depth == 2 && CurrentToken->isOneOf(tok::l_brace, tok::comma)) {
      CurrentToken = CurrentToken->Next;
      if (!CurrentToken)
        break;
      CurrentToken->StartsColumn = true;
      CurrentToken = CurrentToken->Previous;
    }
    CurrentToken = CurrentToken->Next;
  }
  return CurrentToken;
}

unsigned TokenAnnotator::splitPenalty(const AnnotatedLine &Line,
                                      const FormatToken &Tok,
                                      bool InFunctionDecl) const {
  const FormatToken &Left = *Tok.Previous;
  const FormatToken &Right = Tok;

  if (Left.is(tok::semi))
    return 0;

  // Language specific handling.
  if (Style.Language == FormatStyle::LK_Java) {
    if (Right.isOneOf(Keywords.kw_extends, Keywords.kw_throws))
      return 1;
    if (Right.is(Keywords.kw_implements))
      return 2;
    if (Left.is(tok::comma) && Left.NestingLevel == 0)
      return 3;
  } else if (Style.isJavaScript()) {
    if (Right.is(Keywords.kw_function) && Left.isNot(tok::comma))
      return 100;
    if (Left.is(TT_JsTypeColon))
      return 35;
    if ((Left.is(TT_TemplateString) && Left.TokenText.ends_with("${")) ||
        (Right.is(TT_TemplateString) && Right.TokenText.starts_with("}"))) {
      return 100;
    }
    // Prefer breaking call chains (".foo") over empty "{}", "[]" or "()".
    if (Left.opensScope() && Right.closesScope())
      return 200;
  } else if (Style.Language == FormatStyle::LK_Proto) {
    if (Right.is(tok::l_square))
      return 1;
    if (Right.is(tok::period))
      return 500;
  }

  if (Right.is(tok::identifier) && Right.Next && Right.Next->is(TT_DictLiteral))
    return 1;
  if (Right.is(tok::l_square)) {
    if (Left.is(tok::r_square))
      return 200;
    // Slightly prefer formatting local lambda definitions like functions.
    if (Right.is(TT_LambdaLSquare) && Left.is(tok::equal))
      return 35;
    if (!Right.isOneOf(TT_ObjCMethodExpr, TT_LambdaLSquare,
                       TT_ArrayInitializerLSquare,
                       TT_DesignatedInitializerLSquare, TT_AttributeSquare)) {
      return 500;
    }
  }

  if (Left.is(tok::coloncolon))
    return Style.PenaltyBreakScopeResolution;
  if (Right.isOneOf(TT_StartOfName, TT_FunctionDeclarationName) ||
      Right.is(tok::kw_operator)) {
    if (Line.startsWith(tok::kw_for) && Right.PartOfMultiVariableDeclStmt)
      return 3;
    if (Left.is(TT_StartOfName))
      return 110;
    if (InFunctionDecl && Right.NestingLevel == 0)
      return Style.PenaltyReturnTypeOnItsOwnLine;
    return 200;
  }
  if (Right.is(TT_PointerOrReference))
    return 190;
  if (Right.is(TT_LambdaArrow))
    return 110;
  if (Left.is(tok::equal) && Right.is(tok::l_brace))
    return 160;
  if (Left.is(TT_CastRParen))
    return 100;
  if (Left.isOneOf(tok::kw_class, tok::kw_struct, tok::kw_union))
    return 5000;
  if (Left.is(tok::comment))
    return 1000;

  if (Left.isOneOf(TT_RangeBasedForLoopColon, TT_InheritanceColon,
                   TT_CtorInitializerColon)) {
    return 2;
  }

  if (Right.isMemberAccess()) {
    // Breaking before the "./->" of a chained call/member access is reasonably
    // cheap, as formatting those with one call per line is generally
    // desirable. In particular, it should be cheaper to break before the call
    // than it is to break inside a call's parameters, which could lead to weird
    // "hanging" indents. The exception is the very last "./->" to support this
    // frequent pattern:
    //
    //   aaaaaaaa.aaaaaaaa.bbbbbbb().ccccccccccccccccccccc(
    //       dddddddd);
    //
    // which might otherwise be blown up onto many lines. Here, clang-format
    // won't produce "hanging" indents anyway as there is no other trailing
    // call.
    //
    // Also apply higher penalty is not a call as that might lead to a wrapping
    // like:
    //
    //   aaaaaaa
    //       .aaaaaaaaa.bbbbbbbb(cccccccc);
    return !Right.NextOperator || !Right.NextOperator->Previous->closesScope()
               ? 150
               : 35;
  }

  if (Right.is(TT_TrailingAnnotation) &&
      (!Right.Next || Right.Next->isNot(tok::l_paren))) {
    // Moving trailing annotations to the next line is fine for ObjC method
    // declarations.
    if (Line.startsWith(TT_ObjCMethodSpecifier))
      return 10;
    // Generally, breaking before a trailing annotation is bad unless it is
    // function-like. It seems to be especially preferable to keep standard
    // annotations (i.e. "const", "final" and "override") on the same line.
    // Use a slightly higher penalty after ")" so that annotations like
    // "const override" are kept together.
    bool is_short_annotation = Right.TokenText.size() < 10;
    return (Left.is(tok::r_paren) ? 100 : 120) + (is_short_annotation ? 50 : 0);
  }

  // In for-loops, prefer breaking at ',' and ';'.
  if (Line.startsWith(tok::kw_for) && Left.is(tok::equal))
    return 4;

  // In Objective-C method expressions, prefer breaking before "param:" over
  // breaking after it.
  if (Right.is(TT_SelectorName))
    return 0;
  if (Left.is(tok::colon) && Left.is(TT_ObjCMethodExpr))
    return Line.MightBeFunctionDecl ? 50 : 500;

  // In Objective-C type declarations, avoid breaking after the category's
  // open paren (we'll prefer breaking after the protocol list's opening
  // angle bracket, if present).
  if (Line.Type == LT_ObjCDecl && Left.is(tok::l_paren) && Left.Previous &&
      Left.Previous->isOneOf(tok::identifier, tok::greater)) {
    return 500;
  }

  if (Left.is(tok::l_paren) && Style.PenaltyBreakOpenParenthesis != 0)
    return Style.PenaltyBreakOpenParenthesis;
  if (Left.is(tok::l_paren) && InFunctionDecl &&
      Style.AlignAfterOpenBracket != FormatStyle::BAS_DontAlign) {
    return 100;
  }
  if (Left.is(tok::l_paren) && Left.Previous &&
      (Left.Previous->isOneOf(tok::kw_for, tok::kw__Generic) ||
       Left.Previous->isIf())) {
    return 1000;
  }
  if (Left.is(tok::equal) && InFunctionDecl)
    return 110;
  if (Right.is(tok::r_brace))
    return 1;
  if (Left.is(TT_TemplateOpener))
    return 100;
  if (Left.opensScope()) {
    // If we aren't aligning after opening parens/braces we can always break
    // here unless the style does not want us to place all arguments on the
    // next line.
    if (Style.AlignAfterOpenBracket == FormatStyle::BAS_DontAlign &&
        (Left.ParameterCount <= 1 || Style.AllowAllArgumentsOnNextLine)) {
      return 0;
    }
    if (Left.is(tok::l_brace) && !Style.Cpp11BracedListStyle)
      return 19;
    return Left.ParameterCount > 1 ? Style.PenaltyBreakBeforeFirstCallParameter
                                   : 19;
  }
  if (Left.is(TT_JavaAnnotation))
    return 50;

  if (Left.is(TT_UnaryOperator))
    return 60;
  if (Left.isOneOf(tok::plus, tok::comma) && Left.Previous &&
      Left.Previous->isLabelString() &&
      (Left.NextOperator || Left.OperatorIndex != 0)) {
    return 50;
  }
  if (Right.is(tok::plus) && Left.isLabelString() &&
      (Right.NextOperator || Right.OperatorIndex != 0)) {
    return 25;
  }
  if (Left.is(tok::comma))
    return 1;
  if (Right.is(tok::lessless) && Left.isLabelString() &&
      (Right.NextOperator || Right.OperatorIndex != 1)) {
    return 25;
  }
  if (Right.is(tok::lessless)) {
    // Breaking at a << is really cheap.
    if (Left.isNot(tok::r_paren) || Right.OperatorIndex > 0) {
      // Slightly prefer to break before the first one in log-like statements.
      return 2;
    }
    return 1;
  }
  if (Left.ClosesTemplateDeclaration)
    return Style.PenaltyBreakTemplateDeclaration;
  if (Left.ClosesRequiresClause)
    return 0;
  if (Left.is(TT_ConditionalExpr))
    return prec::Conditional;
  prec::Level Level = Left.getPrecedence();
  if (Level == prec::Unknown)
    Level = Right.getPrecedence();
  if (Level == prec::Assignment)
    return Style.PenaltyBreakAssignment;
  if (Level != prec::Unknown)
    return Level;

  return 3;
}

bool TokenAnnotator::spaceRequiredBeforeParens(const FormatToken &Right) const {
  if (Style.SpaceBeforeParens == FormatStyle::SBPO_Always)
    return true;
  if (Right.is(TT_OverloadedOperatorLParen) &&
      Style.SpaceBeforeParensOptions.AfterOverloadedOperator) {
    return true;
  }
  if (Style.SpaceBeforeParensOptions.BeforeNonEmptyParentheses &&
      Right.ParameterCount > 0) {
    return true;
  }
  return false;
}

bool TokenAnnotator::spaceRequiredBetween(const AnnotatedLine &Line,
                                          const FormatToken &Left,
                                          const FormatToken &Right) const {
  if (Left.is(tok::kw_return) &&
      !Right.isOneOf(tok::semi, tok::r_paren, tok::hashhash)) {
    return true;
  }
  if (Left.is(tok::kw_throw) && Right.is(tok::l_paren) && Right.MatchingParen &&
      Right.MatchingParen->is(TT_CastRParen)) {
    return true;
  }
  if (Left.is(Keywords.kw_assert) && Style.Language == FormatStyle::LK_Java)
    return true;
  if (Style.ObjCSpaceAfterProperty && Line.Type == LT_ObjCProperty &&
      Left.Tok.getObjCKeywordID() == tok::objc_property) {
    return true;
  }
  if (Right.is(tok::hashhash))
    return Left.is(tok::hash);
  if (Left.isOneOf(tok::hashhash, tok::hash))
    return Right.is(tok::hash);
  if (Left.is(BK_Block) && Right.is(tok::r_brace) &&
      Right.MatchingParen == &Left && Line.Children.empty()) {
    return Style.SpaceInEmptyBlock;
  }
  if ((Left.is(tok::l_paren) && Right.is(tok::r_paren)) ||
      (Left.is(tok::l_brace) && Left.isNot(BK_Block) &&
       Right.is(tok::r_brace) && Right.isNot(BK_Block))) {
    return Style.SpacesInParensOptions.InEmptyParentheses;
  }
  if (Style.SpacesInParens == FormatStyle::SIPO_Custom &&
      Style.SpacesInParensOptions.ExceptDoubleParentheses &&
      Left.is(tok::r_paren) && Right.is(tok::r_paren)) {
    auto *InnerLParen = Left.MatchingParen;
    if (InnerLParen && InnerLParen->Previous == Right.MatchingParen) {
      InnerLParen->SpacesRequiredBefore = 0;
      return false;
    }
  }
  if (Style.SpacesInParensOptions.InConditionalStatements) {
    const FormatToken *LeftParen = nullptr;
    if (Left.is(tok::l_paren))
      LeftParen = &Left;
    else if (Right.is(tok::r_paren) && Right.MatchingParen)
      LeftParen = Right.MatchingParen;
    if (LeftParen) {
      if (LeftParen->is(TT_ConditionLParen))
        return true;
      if (LeftParen->Previous && isKeywordWithCondition(*LeftParen->Previous))
        return true;
    }
  }

  // trailing return type 'auto': []() -> auto {}, auto foo() -> auto {}
  if (Left.is(tok::kw_auto) && Right.isOneOf(TT_LambdaLBrace, TT_FunctionLBrace,
                                             // function return type 'auto'
                                             TT_FunctionTypeLParen)) {
    return true;
  }

  // auto{x} auto(x)
  if (Left.is(tok::kw_auto) && Right.isOneOf(tok::l_paren, tok::l_brace))
    return false;

  const auto *BeforeLeft = Left.Previous;

  // operator co_await(x)
  if (Right.is(tok::l_paren) && Left.is(tok::kw_co_await) && BeforeLeft &&
      BeforeLeft->is(tok::kw_operator)) {
    return false;
  }
  // co_await (x), co_yield (x), co_return (x)
  if (Left.isOneOf(tok::kw_co_await, tok::kw_co_yield, tok::kw_co_return) &&
      !Right.isOneOf(tok::semi, tok::r_paren)) {
    return true;
  }

  if (Left.is(tok::l_paren) || Right.is(tok::r_paren)) {
    return (Right.is(TT_CastRParen) ||
            (Left.MatchingParen && Left.MatchingParen->is(TT_CastRParen)))
               ? Style.SpacesInParensOptions.InCStyleCasts
               : Style.SpacesInParensOptions.Other;
  }
  if (Right.isOneOf(tok::semi, tok::comma))
    return false;
  if (Right.is(tok::less) && Line.Type == LT_ObjCDecl) {
    bool IsLightweightGeneric = Right.MatchingParen &&
                                Right.MatchingParen->Next &&
                                Right.MatchingParen->Next->is(tok::colon);
    return !IsLightweightGeneric && Style.ObjCSpaceBeforeProtocolList;
  }
  if (Right.is(tok::less) && Left.is(tok::kw_template))
    return Style.SpaceAfterTemplateKeyword;
  if (Left.isOneOf(tok::exclaim, tok::tilde))
    return false;
  if (Left.is(tok::at) &&
      Right.isOneOf(tok::identifier, tok::string_literal, tok::char_constant,
                    tok::numeric_constant, tok::l_paren, tok::l_brace,
                    tok::kw_true, tok::kw_false)) {
    return false;
  }
  if (Left.is(tok::colon))
    return Left.isNot(TT_ObjCMethodExpr);
  if (Left.is(tok::coloncolon))
    return false;
  if (Left.is(tok::less) || Right.isOneOf(tok::greater, tok::less)) {
    if (Style.Language == FormatStyle::LK_TextProto ||
        (Style.Language == FormatStyle::LK_Proto &&
         (Left.is(TT_DictLiteral) || Right.is(TT_DictLiteral)))) {
      // Format empty list as `<>`.
      if (Left.is(tok::less) && Right.is(tok::greater))
        return false;
      return !Style.Cpp11BracedListStyle;
    }
    // Don't attempt to format operator<(), as it is handled later.
    if (Right.isNot(TT_OverloadedOperatorLParen))
      return false;
  }
  if (Right.is(tok::ellipsis)) {
    return Left.Tok.isLiteral() || (Left.is(tok::identifier) && BeforeLeft &&
                                    BeforeLeft->is(tok::kw_case));
  }
  if (Left.is(tok::l_square) && Right.is(tok::amp))
    return Style.SpacesInSquareBrackets;
  if (Right.is(TT_PointerOrReference)) {
    if (Left.is(tok::r_paren) && Line.MightBeFunctionDecl) {
      if (!Left.MatchingParen)
        return true;
      FormatToken *TokenBeforeMatchingParen =
          Left.MatchingParen->getPreviousNonComment();
      if (!TokenBeforeMatchingParen || Left.isNot(TT_TypeDeclarationParen))
        return true;
    }
    // Add a space if the previous token is a pointer qualifier or the closing
    // parenthesis of __attribute__(()) expression and the style requires spaces
    // after pointer qualifiers.
    if ((Style.SpaceAroundPointerQualifiers == FormatStyle::SAPQ_After ||
         Style.SpaceAroundPointerQualifiers == FormatStyle::SAPQ_Both) &&
        (Left.is(TT_AttributeRParen) ||
         Left.canBePointerOrReferenceQualifier())) {
      return true;
    }
    if (Left.Tok.isLiteral())
      return true;
    // for (auto a = 0, b = 0; const auto & c : {1, 2, 3})
    if (Left.isTypeOrIdentifier(LangOpts) && Right.Next && Right.Next->Next &&
        Right.Next->Next->is(TT_RangeBasedForLoopColon)) {
      return getTokenPointerOrReferenceAlignment(Right) !=
             FormatStyle::PAS_Left;
    }
    return !Left.isOneOf(TT_PointerOrReference, tok::l_paren) &&
           (getTokenPointerOrReferenceAlignment(Right) !=
                FormatStyle::PAS_Left ||
            (Line.IsMultiVariableDeclStmt &&
             (Left.NestingLevel == 0 ||
              (Left.NestingLevel == 1 && startsWithInitStatement(Line)))));
  }
  if (Right.is(TT_FunctionTypeLParen) && Left.isNot(tok::l_paren) &&
      (Left.isNot(TT_PointerOrReference) ||
       (getTokenPointerOrReferenceAlignment(Left) != FormatStyle::PAS_Right &&
        !Line.IsMultiVariableDeclStmt))) {
    return true;
  }
  if (Left.is(TT_PointerOrReference)) {
    // Add a space if the next token is a pointer qualifier and the style
    // requires spaces before pointer qualifiers.
    if ((Style.SpaceAroundPointerQualifiers == FormatStyle::SAPQ_Before ||
         Style.SpaceAroundPointerQualifiers == FormatStyle::SAPQ_Both) &&
        Right.canBePointerOrReferenceQualifier()) {
      return true;
    }
    // & 1
    if (Right.Tok.isLiteral())
      return true;
    // & /* comment
    if (Right.is(TT_BlockComment))
      return true;
    // foo() -> const Bar * override/final
    // S::foo() & noexcept/requires
    if (Right.isOneOf(Keywords.kw_override, Keywords.kw_final, tok::kw_noexcept,
                      TT_RequiresClause) &&
        Right.isNot(TT_StartOfName)) {
      return true;
    }
    // & {
    if (Right.is(tok::l_brace) && Right.is(BK_Block))
      return true;
    // for (auto a = 0, b = 0; const auto& c : {1, 2, 3})
    if (BeforeLeft && BeforeLeft->isTypeOrIdentifier(LangOpts) && Right.Next &&
        Right.Next->is(TT_RangeBasedForLoopColon)) {
      return getTokenPointerOrReferenceAlignment(Left) !=
             FormatStyle::PAS_Right;
    }
    if (Right.isOneOf(TT_PointerOrReference, TT_ArraySubscriptLSquare,
                      tok::l_paren)) {
      return false;
    }
    if (getTokenPointerOrReferenceAlignment(Left) == FormatStyle::PAS_Right)
      return false;
    // FIXME: Setting IsMultiVariableDeclStmt for the whole line is error-prone,
    // because it does not take into account nested scopes like lambdas.
    // In multi-variable declaration statements, attach */& to the variable
    // independently of the style. However, avoid doing it if we are in a nested
    // scope, e.g. lambda. We still need to special-case statements with
    // initializers.
    if (Line.IsMultiVariableDeclStmt &&
        (Left.NestingLevel == Line.First->NestingLevel ||
         ((Left.NestingLevel == Line.First->NestingLevel + 1) &&
          startsWithInitStatement(Line)))) {
      return false;
    }
    if (!BeforeLeft)
      return false;
    if (BeforeLeft->is(tok::coloncolon)) {
      if (Left.isNot(tok::star))
        return false;
      assert(Style.PointerAlignment != FormatStyle::PAS_Right);
      if (!Right.startsSequence(tok::identifier, tok::r_paren))
        return true;
      assert(Right.Next);
      const auto *LParen = Right.Next->MatchingParen;
      return !LParen || LParen->isNot(TT_FunctionTypeLParen);
    }
    return !BeforeLeft->isOneOf(tok::l_paren, tok::l_square);
  }
  // Ensure right pointer alignment with ellipsis e.g. int *...P
  if (Left.is(tok::ellipsis) && BeforeLeft &&
      BeforeLeft->isPointerOrReference()) {
    return Style.PointerAlignment != FormatStyle::PAS_Right;
  }

  if (Right.is(tok::star) && Left.is(tok::l_paren))
    return false;
  if (Left.is(tok::star) && Right.isPointerOrReference())
    return false;
  if (Right.isPointerOrReference()) {
    const FormatToken *Previous = &Left;
    while (Previous && Previous->isNot(tok::kw_operator)) {
      if (Previous->is(tok::identifier) || Previous->isTypeName(LangOpts)) {
        Previous = Previous->getPreviousNonComment();
        continue;
      }
      if (Previous->is(TT_TemplateCloser) && Previous->MatchingParen) {
        Previous = Previous->MatchingParen->getPreviousNonComment();
        continue;
      }
      if (Previous->is(tok::coloncolon)) {
        Previous = Previous->getPreviousNonComment();
        continue;
      }
      break;
    }
    // Space between the type and the * in:
    //   operator void*()
    //   operator char*()
    //   operator void const*()
    //   operator void volatile*()
    //   operator /*comment*/ const char*()
    //   operator volatile /*comment*/ char*()
    //   operator Foo*()
    //   operator C<T>*()
    //   operator std::Foo*()
    //   operator C<T>::D<U>*()
    // dependent on PointerAlignment style.
    if (Previous) {
      if (Previous->endsSequence(tok::kw_operator))
        return Style.PointerAlignment != FormatStyle::PAS_Left;
      if (Previous->is(tok::kw_const) || Previous->is(tok::kw_volatile)) {
        return (Style.PointerAlignment != FormatStyle::PAS_Left) ||
               (Style.SpaceAroundPointerQualifiers ==
                FormatStyle::SAPQ_After) ||
               (Style.SpaceAroundPointerQualifiers == FormatStyle::SAPQ_Both);
      }
    }
  }
  if (Style.isCSharp() && Left.is(Keywords.kw_is) && Right.is(tok::l_square))
    return true;
  const auto SpaceRequiredForArrayInitializerLSquare =
      [](const FormatToken &LSquareTok, const FormatStyle &Style) {
        return Style.SpacesInContainerLiterals ||
               (Style.isProto() && !Style.Cpp11BracedListStyle &&
                LSquareTok.endsSequence(tok::l_square, tok::colon,
                                        TT_SelectorName));
      };
  if (Left.is(tok::l_square)) {
    return (Left.is(TT_ArrayInitializerLSquare) && Right.isNot(tok::r_square) &&
            SpaceRequiredForArrayInitializerLSquare(Left, Style)) ||
           (Left.isOneOf(TT_ArraySubscriptLSquare, TT_StructuredBindingLSquare,
                         TT_LambdaLSquare) &&
            Style.SpacesInSquareBrackets && Right.isNot(tok::r_square));
  }
  if (Right.is(tok::r_square)) {
    return Right.MatchingParen &&
           ((Right.MatchingParen->is(TT_ArrayInitializerLSquare) &&
             SpaceRequiredForArrayInitializerLSquare(*Right.MatchingParen,
                                                     Style)) ||
            (Style.SpacesInSquareBrackets &&
             Right.MatchingParen->isOneOf(TT_ArraySubscriptLSquare,
                                          TT_StructuredBindingLSquare,
                                          TT_LambdaLSquare)));
  }
  if (Right.is(tok::l_square) &&
      !Right.isOneOf(TT_ObjCMethodExpr, TT_LambdaLSquare,
                     TT_DesignatedInitializerLSquare,
                     TT_StructuredBindingLSquare, TT_AttributeSquare) &&
      !Left.isOneOf(tok::numeric_constant, TT_DictLiteral) &&
      !(Left.isNot(tok::r_square) && Style.SpaceBeforeSquareBrackets &&
        Right.is(TT_ArraySubscriptLSquare))) {
    return false;
  }
  if (Left.is(tok::l_brace) && Right.is(tok::r_brace))
    return !Left.Children.empty(); // No spaces in "{}".
  if ((Left.is(tok::l_brace) && Left.isNot(BK_Block)) ||
      (Right.is(tok::r_brace) && Right.MatchingParen &&
       Right.MatchingParen->isNot(BK_Block))) {
    return !Style.Cpp11BracedListStyle || Style.SpacesInParensOptions.Other;
  }
  if (Left.is(TT_BlockComment)) {
    // No whitespace in x(/*foo=*/1), except for JavaScript.
    return Style.isJavaScript() || !Left.TokenText.ends_with("=*/");
  }

  // Space between template and attribute.
  // e.g. template <typename T> [[nodiscard]] ...
  if (Left.is(TT_TemplateCloser) && Right.is(TT_AttributeSquare))
    return true;
  // Space before parentheses common for all languages
  if (Right.is(tok::l_paren)) {
    if (Left.is(TT_TemplateCloser) && Right.isNot(TT_FunctionTypeLParen))
      return spaceRequiredBeforeParens(Right);
    if (Left.isOneOf(TT_RequiresClause,
                     TT_RequiresClauseInARequiresExpression)) {
      return Style.SpaceBeforeParensOptions.AfterRequiresInClause ||
             spaceRequiredBeforeParens(Right);
    }
    if (Left.is(TT_RequiresExpression)) {
      return Style.SpaceBeforeParensOptions.AfterRequiresInExpression ||
             spaceRequiredBeforeParens(Right);
    }
    if (Left.is(TT_AttributeRParen) ||
        (Left.is(tok::r_square) && Left.is(TT_AttributeSquare))) {
      return true;
    }
    if (Left.is(TT_ForEachMacro)) {
      return Style.SpaceBeforeParensOptions.AfterForeachMacros ||
             spaceRequiredBeforeParens(Right);
    }
    if (Left.is(TT_IfMacro)) {
      return Style.SpaceBeforeParensOptions.AfterIfMacros ||
             spaceRequiredBeforeParens(Right);
    }
    if (Style.SpaceBeforeParens == FormatStyle::SBPO_Custom &&
        Left.isOneOf(tok::kw_new, tok::kw_delete) &&
        Right.isNot(TT_OverloadedOperatorLParen) &&
        !(Line.MightBeFunctionDecl && Left.is(TT_FunctionDeclarationName))) {
      return Style.SpaceBeforeParensOptions.AfterPlacementOperator;
    }
    if (Line.Type == LT_ObjCDecl)
      return true;
    if (Left.is(tok::semi))
      return true;
    if (Left.isOneOf(tok::pp_elif, tok::kw_for, tok::kw_while, tok::kw_switch,
                     tok::kw_case, TT_ForEachMacro, TT_ObjCForIn) ||
        Left.isIf(Line.Type != LT_PreprocessorDirective) ||
        Right.is(TT_ConditionLParen)) {
      return Style.SpaceBeforeParensOptions.AfterControlStatements ||
             spaceRequiredBeforeParens(Right);
    }

    // TODO add Operator overloading specific Options to
    // SpaceBeforeParensOptions
    if (Right.is(TT_OverloadedOperatorLParen))
      return spaceRequiredBeforeParens(Right);
    // Function declaration or definition
    if (Line.MightBeFunctionDecl && Right.is(TT_FunctionDeclarationLParen)) {
      if (spaceRequiredBeforeParens(Right))
        return true;
      const auto &Options = Style.SpaceBeforeParensOptions;
      return Line.mightBeFunctionDefinition()
                 ? Options.AfterFunctionDefinitionName
                 : Options.AfterFunctionDeclarationName;
    }
    // Lambda
    if (Line.Type != LT_PreprocessorDirective && Left.is(tok::r_square) &&
        Left.MatchingParen && Left.MatchingParen->is(TT_LambdaLSquare)) {
      return Style.SpaceBeforeParensOptions.AfterFunctionDefinitionName ||
             spaceRequiredBeforeParens(Right);
    }
    if (!BeforeLeft || !BeforeLeft->isOneOf(tok::period, tok::arrow)) {
      if (Left.isOneOf(tok::kw_try, Keywords.kw___except, tok::kw_catch)) {
        return Style.SpaceBeforeParensOptions.AfterControlStatements ||
               spaceRequiredBeforeParens(Right);
      }
      if (Left.isOneOf(tok::kw_new, tok::kw_delete)) {
        return ((!Line.MightBeFunctionDecl || !BeforeLeft) &&
                Style.SpaceBeforeParens != FormatStyle::SBPO_Never) ||
               spaceRequiredBeforeParens(Right);
      }

      if (Left.is(tok::r_square) && Left.MatchingParen &&
          Left.MatchingParen->Previous &&
          Left.MatchingParen->Previous->is(tok::kw_delete)) {
        return (Style.SpaceBeforeParens != FormatStyle::SBPO_Never) ||
               spaceRequiredBeforeParens(Right);
      }
    }
    // Handle builtins like identifiers.
    if (Line.Type != LT_PreprocessorDirective &&
        (Left.Tok.getIdentifierInfo() || Left.is(tok::r_paren))) {
      return spaceRequiredBeforeParens(Right);
    }
    return false;
  }
  if (Left.is(tok::at) && Right.Tok.getObjCKeywordID() != tok::objc_not_keyword)
    return false;
  if (Right.is(TT_UnaryOperator)) {
    return !Left.isOneOf(tok::l_paren, tok::l_square, tok::at) &&
           (Left.isNot(tok::colon) || Left.isNot(TT_ObjCMethodExpr));
  }
  // No space between the variable name and the initializer list.
  // A a1{1};
  // Verilog doesn't have such syntax, but it has word operators that are C++
  // identifiers like `a inside {b, c}`. So the rule is not applicable.
  if (!Style.isVerilog() &&
      (Left.isOneOf(tok::identifier, tok::greater, tok::r_square,
                    tok::r_paren) ||
       Left.isTypeName(LangOpts)) &&
      Right.is(tok::l_brace) && Right.getNextNonComment() &&
      Right.isNot(BK_Block)) {
    return false;
  }
  if (Left.is(tok::period) || Right.is(tok::period))
    return false;
  // u#str, U#str, L#str, u8#str
  // uR#str, UR#str, LR#str, u8R#str
  if (Right.is(tok::hash) && Left.is(tok::identifier) &&
      (Left.TokenText == "L" || Left.TokenText == "u" ||
       Left.TokenText == "U" || Left.TokenText == "u8" ||
       Left.TokenText == "LR" || Left.TokenText == "uR" ||
       Left.TokenText == "UR" || Left.TokenText == "u8R")) {
    return false;
  }
  if (Left.is(TT_TemplateCloser) && Left.MatchingParen &&
      Left.MatchingParen->Previous &&
      (Left.MatchingParen->Previous->is(tok::period) ||
       Left.MatchingParen->Previous->is(tok::coloncolon))) {
    // Java call to generic function with explicit type:
    // A.<B<C<...>>>DoSomething();
    // A::<B<C<...>>>DoSomething();  // With a Java 8 method reference.
    return false;
  }
  if (Left.is(TT_TemplateCloser) && Right.is(tok::l_square))
    return false;
  if (Left.is(tok::l_brace) && Left.endsSequence(TT_DictLiteral, tok::at)) {
    // Objective-C dictionary literal -> no space after opening brace.
    return false;
  }
  if (Right.is(tok::r_brace) && Right.MatchingParen &&
      Right.MatchingParen->endsSequence(TT_DictLiteral, tok::at)) {
    // Objective-C dictionary literal -> no space before closing brace.
    return false;
  }
  if (Right.is(TT_TrailingAnnotation) && Right.isOneOf(tok::amp, tok::ampamp) &&
      Left.isOneOf(tok::kw_const, tok::kw_volatile) &&
      (!Right.Next || Right.Next->is(tok::semi))) {
    // Match const and volatile ref-qualifiers without any additional
    // qualifiers such as
    // void Fn() const &;
    return getTokenReferenceAlignment(Right) != FormatStyle::PAS_Left;
  }

  return true;
}

bool TokenAnnotator::spaceRequiredBefore(const AnnotatedLine &Line,
                                         const FormatToken &Right) const {
  const FormatToken &Left = *Right.Previous;

  // If the token is finalized don't touch it (as it could be in a
  // clang-format-off section).
  if (Left.Finalized)
    return Right.hasWhitespaceBefore();

  const bool IsVerilog = Style.isVerilog();
  assert(!IsVerilog || !IsCpp);

  // Never ever merge two words.
  if (Keywords.isWordLike(Right, IsVerilog) &&
      Keywords.isWordLike(Left, IsVerilog)) {
    return true;
  }

  // Leave a space between * and /* to avoid C4138 `comment end` found outside
  // of comment.
  if (Left.is(tok::star) && Right.is(tok::comment))
    return true;

  if (IsCpp) {
    if (Left.is(TT_OverloadedOperator) &&
        Right.isOneOf(TT_TemplateOpener, TT_TemplateCloser)) {
      return true;
    }
    // Space between UDL and dot: auto b = 4s .count();
    if (Right.is(tok::period) && Left.is(tok::numeric_constant))
      return true;
    // Space between import <iostream>.
    // or import .....;
    if (Left.is(Keywords.kw_import) && Right.isOneOf(tok::less, tok::ellipsis))
      return true;
    // Space between `module :` and `import :`.
    if (Left.isOneOf(Keywords.kw_module, Keywords.kw_import) &&
        Right.is(TT_ModulePartitionColon)) {
      return true;
    }
    // No space between import foo:bar but keep a space between import :bar;
    if (Left.is(tok::identifier) && Right.is(TT_ModulePartitionColon))
      return false;
    // No space between :bar;
    if (Left.is(TT_ModulePartitionColon) &&
        Right.isOneOf(tok::identifier, tok::kw_private)) {
      return false;
    }
    if (Left.is(tok::ellipsis) && Right.is(tok::identifier) &&
        Line.First->is(Keywords.kw_import)) {
      return false;
    }
    // Space in __attribute__((attr)) ::type.
    if (Left.isOneOf(TT_AttributeRParen, TT_AttributeMacro) &&
        Right.is(tok::coloncolon)) {
      return true;
    }

    if (Left.is(tok::kw_operator))
      return Right.is(tok::coloncolon);
    if (Right.is(tok::l_brace) && Right.is(BK_BracedInit) &&
        !Left.opensScope() && Style.SpaceBeforeCpp11BracedList) {
      return true;
    }
    if (Left.is(tok::less) && Left.is(TT_OverloadedOperator) &&
        Right.is(TT_TemplateOpener)) {
      return true;
    }
    // C++ Core Guidelines suppression tag, e.g. `[[suppress(type.5)]]`.
    if (Left.is(tok::identifier) && Right.is(tok::numeric_constant))
      return Right.TokenText[0] != '.';
    // `Left` is a keyword (including C++ alternative operator) or identifier.
    if (Left.Tok.getIdentifierInfo() && Right.Tok.isLiteral())
      return true;
  } else if (Style.isProto()) {
    if (Right.is(tok::period) &&
        Left.isOneOf(Keywords.kw_optional, Keywords.kw_required,
                     Keywords.kw_repeated, Keywords.kw_extend)) {
      return true;
    }
    if (Right.is(tok::l_paren) &&
        Left.isOneOf(Keywords.kw_returns, Keywords.kw_option)) {
      return true;
    }
    if (Right.isOneOf(tok::l_brace, tok::less) && Left.is(TT_SelectorName))
      return true;
    // Slashes occur in text protocol extension syntax: [type/type] { ... }.
    if (Left.is(tok::slash) || Right.is(tok::slash))
      return false;
    if (Left.MatchingParen &&
        Left.MatchingParen->is(TT_ProtoExtensionLSquare) &&
        Right.isOneOf(tok::l_brace, tok::less)) {
      return !Style.Cpp11BracedListStyle;
    }
    // A percent is probably part of a formatting specification, such as %lld.
    if (Left.is(tok::percent))
      return false;
    // Preserve the existence of a space before a percent for cases like 0x%04x
    // and "%d %d"
    if (Left.is(tok::numeric_constant) && Right.is(tok::percent))
      return Right.hasWhitespaceBefore();
  } else if (Style.isJson()) {
    if (Right.is(tok::colon) && Left.is(tok::string_literal))
      return Style.SpaceBeforeJsonColon;
  } else if (Style.isCSharp()) {
    // Require spaces around '{' and  before '}' unless they appear in
    // interpolated strings. Interpolated strings are merged into a single token
    // so cannot have spaces inserted by this function.

    // No space between 'this' and '['
    if (Left.is(tok::kw_this) && Right.is(tok::l_square))
      return false;

    // No space between 'new' and '('
    if (Left.is(tok::kw_new) && Right.is(tok::l_paren))
      return false;

    // Space before { (including space within '{ {').
    if (Right.is(tok::l_brace))
      return true;

    // Spaces inside braces.
    if (Left.is(tok::l_brace) && Right.isNot(tok::r_brace))
      return true;

    if (Left.isNot(tok::l_brace) && Right.is(tok::r_brace))
      return true;

    // Spaces around '=>'.
    if (Left.is(TT_FatArrow) || Right.is(TT_FatArrow))
      return true;

    // No spaces around attribute target colons
    if (Left.is(TT_AttributeColon) || Right.is(TT_AttributeColon))
      return false;

    // space between type and variable e.g. Dictionary<string,string> foo;
    if (Left.is(TT_TemplateCloser) && Right.is(TT_StartOfName))
      return true;

    // spaces inside square brackets.
    if (Left.is(tok::l_square) || Right.is(tok::r_square))
      return Style.SpacesInSquareBrackets;

    // No space before ? in nullable types.
    if (Right.is(TT_CSharpNullable))
      return false;

    // No space before null forgiving '!'.
    if (Right.is(TT_NonNullAssertion))
      return false;

    // No space between consecutive commas '[,,]'.
    if (Left.is(tok::comma) && Right.is(tok::comma))
      return false;

    // space after var in `var (key, value)`
    if (Left.is(Keywords.kw_var) && Right.is(tok::l_paren))
      return true;

    // space between keywords and paren e.g. "using ("
    if (Right.is(tok::l_paren)) {
      if (Left.isOneOf(tok::kw_using, Keywords.kw_async, Keywords.kw_when,
                       Keywords.kw_lock)) {
        return Style.SpaceBeforeParensOptions.AfterControlStatements ||
               spaceRequiredBeforeParens(Right);
      }
    }

    // space between method modifier and opening parenthesis of a tuple return
    // type
    if ((Left.isAccessSpecifierKeyword() ||
         Left.isOneOf(tok::kw_virtual, tok::kw_extern, tok::kw_static,
                      Keywords.kw_internal, Keywords.kw_abstract,
                      Keywords.kw_sealed, Keywords.kw_override,
                      Keywords.kw_async, Keywords.kw_unsafe)) &&
        Right.is(tok::l_paren)) {
      return true;
    }
  } else if (Style.isJavaScript()) {
    if (Left.is(TT_FatArrow))
      return true;
    // for await ( ...
    if (Right.is(tok::l_paren) && Left.is(Keywords.kw_await) && Left.Previous &&
        Left.Previous->is(tok::kw_for)) {
      return true;
    }
    if (Left.is(Keywords.kw_async) && Right.is(tok::l_paren) &&
        Right.MatchingParen) {
      const FormatToken *Next = Right.MatchingParen->getNextNonComment();
      // An async arrow function, for example: `x = async () => foo();`,
      // as opposed to calling a function called async: `x = async();`
      if (Next && Next->is(TT_FatArrow))
        return true;
    }
    if ((Left.is(TT_TemplateString) && Left.TokenText.ends_with("${")) ||
        (Right.is(TT_TemplateString) && Right.TokenText.starts_with("}"))) {
      return false;
    }
    // In tagged template literals ("html`bar baz`"), there is no space between
    // the tag identifier and the template string.
    if (Keywords.isJavaScriptIdentifier(Left,
                                        /* AcceptIdentifierName= */ false) &&
        Right.is(TT_TemplateString)) {
      return false;
    }
    if (Right.is(tok::star) &&
        Left.isOneOf(Keywords.kw_function, Keywords.kw_yield)) {
      return false;
    }
    if (Right.isOneOf(tok::l_brace, tok::l_square) &&
        Left.isOneOf(Keywords.kw_function, Keywords.kw_yield,
                     Keywords.kw_extends, Keywords.kw_implements)) {
      return true;
    }
    if (Right.is(tok::l_paren)) {
      // JS methods can use some keywords as names (e.g. `delete()`).
      if (Line.MustBeDeclaration && Left.Tok.getIdentifierInfo())
        return false;
      // Valid JS method names can include keywords, e.g. `foo.delete()` or
      // `bar.instanceof()`. Recognize call positions by preceding period.
      if (Left.Previous && Left.Previous->is(tok::period) &&
          Left.Tok.getIdentifierInfo()) {
        return false;
      }
      // Additional unary JavaScript operators that need a space after.
      if (Left.isOneOf(tok::kw_throw, Keywords.kw_await, Keywords.kw_typeof,
                       tok::kw_void)) {
        return true;
      }
    }
    // `foo as const;` casts into a const type.
    if (Left.endsSequence(tok::kw_const, Keywords.kw_as))
      return false;
    if ((Left.isOneOf(Keywords.kw_let, Keywords.kw_var, Keywords.kw_in,
                      tok::kw_const) ||
         // "of" is only a keyword if it appears after another identifier
         // (e.g. as "const x of y" in a for loop), or after a destructuring
         // operation (const [x, y] of z, const {a, b} of c).
         (Left.is(Keywords.kw_of) && Left.Previous &&
          (Left.Previous->is(tok::identifier) ||
           Left.Previous->isOneOf(tok::r_square, tok::r_brace)))) &&
        (!Left.Previous || Left.Previous->isNot(tok::period))) {
      return true;
    }
    if (Left.isOneOf(tok::kw_for, Keywords.kw_as) && Left.Previous &&
        Left.Previous->is(tok::period) && Right.is(tok::l_paren)) {
      return false;
    }
    if (Left.is(Keywords.kw_as) &&
        Right.isOneOf(tok::l_square, tok::l_brace, tok::l_paren)) {
      return true;
    }
    if (Left.is(tok::kw_default) && Left.Previous &&
        Left.Previous->is(tok::kw_export)) {
      return true;
    }
    if (Left.is(Keywords.kw_is) && Right.is(tok::l_brace))
      return true;
    if (Right.isOneOf(TT_JsTypeColon, TT_JsTypeOptionalQuestion))
      return false;
    if (Left.is(TT_JsTypeOperator) || Right.is(TT_JsTypeOperator))
      return false;
    if ((Left.is(tok::l_brace) || Right.is(tok::r_brace)) &&
        Line.First->isOneOf(Keywords.kw_import, tok::kw_export)) {
      return false;
    }
    if (Left.is(tok::ellipsis))
      return false;
    if (Left.is(TT_TemplateCloser) &&
        !Right.isOneOf(tok::equal, tok::l_brace, tok::comma, tok::l_square,
                       Keywords.kw_implements, Keywords.kw_extends)) {
      // Type assertions ('<type>expr') are not followed by whitespace. Other
      // locations that should have whitespace following are identified by the
      // above set of follower tokens.
      return false;
    }
    if (Right.is(TT_NonNullAssertion))
      return false;
    if (Left.is(TT_NonNullAssertion) &&
        Right.isOneOf(Keywords.kw_as, Keywords.kw_in)) {
      return true; // "x! as string", "x! in y"
    }
  } else if (Style.Language == FormatStyle::LK_Java) {
    if (Left.is(TT_CaseLabelArrow) || Right.is(TT_CaseLabelArrow))
      return true;
    if (Left.is(tok::r_square) && Right.is(tok::l_brace))
      return true;
    // spaces inside square brackets.
    if (Left.is(tok::l_square) || Right.is(tok::r_square))
      return Style.SpacesInSquareBrackets;

    if (Left.is(Keywords.kw_synchronized) && Right.is(tok::l_paren)) {
      return Style.SpaceBeforeParensOptions.AfterControlStatements ||
             spaceRequiredBeforeParens(Right);
    }
    if ((Left.isAccessSpecifierKeyword() ||
         Left.isOneOf(tok::kw_static, Keywords.kw_final, Keywords.kw_abstract,
                      Keywords.kw_native)) &&
        Right.is(TT_TemplateOpener)) {
      return true;
    }
  } else if (IsVerilog) {
    // An escaped identifier ends with whitespace.
    if (Left.is(tok::identifier) && Left.TokenText[0] == '\\')
      return true;
    // Add space between things in a primitive's state table unless in a
    // transition like `(0?)`.
    if ((Left.is(TT_VerilogTableItem) &&
         !Right.isOneOf(tok::r_paren, tok::semi)) ||
        (Right.is(TT_VerilogTableItem) && Left.isNot(tok::l_paren))) {
      const FormatToken *Next = Right.getNextNonComment();
      return !(Next && Next->is(tok::r_paren));
    }
    // Don't add space within a delay like `#0`.
    if (Left.isNot(TT_BinaryOperator) &&
        Left.isOneOf(Keywords.kw_verilogHash, Keywords.kw_verilogHashHash)) {
      return false;
    }
    // Add space after a delay.
    if (Right.isNot(tok::semi) &&
        (Left.endsSequence(tok::numeric_constant, Keywords.kw_verilogHash) ||
         Left.endsSequence(tok::numeric_constant,
                           Keywords.kw_verilogHashHash) ||
         (Left.is(tok::r_paren) && Left.MatchingParen &&
          Left.MatchingParen->endsSequence(tok::l_paren, tok::at)))) {
      return true;
    }
    // Don't add embedded spaces in a number literal like `16'h1?ax` or an array
    // literal like `'{}`.
    if (Left.is(Keywords.kw_apostrophe) ||
        (Left.is(TT_VerilogNumberBase) && Right.is(tok::numeric_constant))) {
      return false;
    }
    // Add spaces around the implication operator `->`.
    if (Left.is(tok::arrow) || Right.is(tok::arrow))
      return true;
    // Don't add spaces between two at signs. Like in a coverage event.
    // Don't add spaces between at and a sensitivity list like
    // `@(posedge clk)`.
    if (Left.is(tok::at) && Right.isOneOf(tok::l_paren, tok::star, tok::at))
      return false;
    // Add space between the type name and dimension like `logic [1:0]`.
    if (Right.is(tok::l_square) &&
        Left.isOneOf(TT_VerilogDimensionedTypeName, Keywords.kw_function)) {
      return true;
    }
    // In a tagged union expression, there should be a space after the tag.
    if (Right.isOneOf(tok::period, Keywords.kw_apostrophe) &&
        Keywords.isVerilogIdentifier(Left) && Left.getPreviousNonComment() &&
        Left.getPreviousNonComment()->is(Keywords.kw_tagged)) {
      return true;
    }
    // Don't add spaces between a casting type and the quote or repetition count
    // and the brace. The case of tagged union expressions is handled by the
    // previous rule.
    if ((Right.is(Keywords.kw_apostrophe) ||
         (Right.is(BK_BracedInit) && Right.is(tok::l_brace))) &&
        !(Left.isOneOf(Keywords.kw_assign, Keywords.kw_unique) ||
          Keywords.isVerilogWordOperator(Left)) &&
        (Left.isOneOf(tok::r_square, tok::r_paren, tok::r_brace,
                      tok::numeric_constant) ||
         Keywords.isWordLike(Left))) {
      return false;
    }
    // Don't add spaces in imports like `import foo::*;`.
    if ((Right.is(tok::star) && Left.is(tok::coloncolon)) ||
        (Left.is(tok::star) && Right.is(tok::semi))) {
      return false;
    }
    // Add space in attribute like `(* ASYNC_REG = "TRUE" *)`.
    if (Left.endsSequence(tok::star, tok::l_paren) && Right.is(tok::identifier))
      return true;
    // Add space before drive strength like in `wire (strong1, pull0)`.
    if (Right.is(tok::l_paren) && Right.is(TT_VerilogStrength))
      return true;
    // Don't add space in a streaming concatenation like `{>>{j}}`.
    if ((Left.is(tok::l_brace) &&
         Right.isOneOf(tok::lessless, tok::greatergreater)) ||
        (Left.endsSequence(tok::lessless, tok::l_brace) ||
         Left.endsSequence(tok::greatergreater, tok::l_brace))) {
      return false;
    }
  } else if (Style.isTableGen()) {
    // Avoid to connect [ and {. [{ is start token of multiline string.
    if (Left.is(tok::l_square) && Right.is(tok::l_brace))
      return true;
    if (Left.is(tok::r_brace) && Right.is(tok::r_square))
      return true;
    // Do not insert around colon in DAGArg and cond operator.
    if (Right.isOneOf(TT_TableGenDAGArgListColon,
                      TT_TableGenDAGArgListColonToAlign) ||
        Left.isOneOf(TT_TableGenDAGArgListColon,
                     TT_TableGenDAGArgListColonToAlign)) {
      return false;
    }
    if (Right.is(TT_TableGenCondOperatorColon))
      return false;
    if (Left.isOneOf(TT_TableGenDAGArgOperatorID,
                     TT_TableGenDAGArgOperatorToBreak) &&
        Right.isNot(TT_TableGenDAGArgCloser)) {
      return true;
    }
    // Do not insert bang operators and consequent openers.
    if (Right.isOneOf(tok::l_paren, tok::less) &&
        Left.isOneOf(TT_TableGenBangOperator, TT_TableGenCondOperator)) {
      return false;
    }
    // Trailing paste requires space before '{' or ':', the case in name values.
    // Not before ';', the case in normal values.
    if (Left.is(TT_TableGenTrailingPasteOperator) &&
        Right.isOneOf(tok::l_brace, tok::colon)) {
      return true;
    }
    // Otherwise paste operator does not prefer space around.
    if (Left.is(tok::hash) || Right.is(tok::hash))
      return false;
    // Sure not to connect after defining keywords.
    if (Keywords.isTableGenDefinition(Left))
      return true;
  }

  if (Left.is(TT_ImplicitStringLiteral))
    return Right.hasWhitespaceBefore();
  if (Line.Type == LT_ObjCMethodDecl) {
    if (Left.is(TT_ObjCMethodSpecifier))
      return true;
    if (Left.is(tok::r_paren) && Left.isNot(TT_AttributeRParen) &&
        canBeObjCSelectorComponent(Right)) {
      // Don't space between ')' and <id> or ')' and 'new'. 'new' is not a
      // keyword in Objective-C, and '+ (instancetype)new;' is a standard class
      // method declaration.
      return false;
    }
  }
  if (Line.Type == LT_ObjCProperty &&
      (Right.is(tok::equal) || Left.is(tok::equal))) {
    return false;
  }

  if (Right.isOneOf(TT_TrailingReturnArrow, TT_LambdaArrow) ||
      Left.isOneOf(TT_TrailingReturnArrow, TT_LambdaArrow)) {
    return true;
  }
  if (Left.is(tok::comma) && Right.isNot(TT_OverloadedOperatorLParen) &&
      // In an unexpanded macro call we only find the parentheses and commas
      // in a line; the commas and closing parenthesis do not require a space.
      (Left.Children.empty() || !Left.MacroParent)) {
    return true;
  }
  if (Right.is(tok::comma))
    return false;
  if (Right.is(TT_ObjCBlockLParen))
    return true;
  if (Right.is(TT_CtorInitializerColon))
    return Style.SpaceBeforeCtorInitializerColon;
  if (Right.is(TT_InheritanceColon) && !Style.SpaceBeforeInheritanceColon)
    return false;
  if (Right.is(TT_RangeBasedForLoopColon) &&
      !Style.SpaceBeforeRangeBasedForLoopColon) {
    return false;
  }
  if (Left.is(TT_BitFieldColon)) {
    return Style.BitFieldColonSpacing == FormatStyle::BFCS_Both ||
           Style.BitFieldColonSpacing == FormatStyle::BFCS_After;
  }
  if (Right.is(tok::colon)) {
    if (Right.is(TT_CaseLabelColon))
      return Style.SpaceBeforeCaseColon;
    if (Right.is(TT_GotoLabelColon))
      return false;
    // `private:` and `public:`.
    if (!Right.getNextNonComment())
      return false;
    if (Right.is(TT_ObjCMethodExpr))
      return false;
    if (Left.is(tok::question))
      return false;
    if (Right.is(TT_InlineASMColon) && Left.is(tok::coloncolon))
      return false;
    if (Right.is(TT_DictLiteral))
      return Style.SpacesInContainerLiterals;
    if (Right.is(TT_AttributeColon))
      return false;
    if (Right.is(TT_CSharpNamedArgumentColon))
      return false;
    if (Right.is(TT_GenericSelectionColon))
      return false;
    if (Right.is(TT_BitFieldColon)) {
      return Style.BitFieldColonSpacing == FormatStyle::BFCS_Both ||
             Style.BitFieldColonSpacing == FormatStyle::BFCS_Before;
    }
    return true;
  }
  // Do not merge "- -" into "--".
  if ((Left.isOneOf(tok::minus, tok::minusminus) &&
       Right.isOneOf(tok::minus, tok::minusminus)) ||
      (Left.isOneOf(tok::plus, tok::plusplus) &&
       Right.isOneOf(tok::plus, tok::plusplus))) {
    return true;
  }
  if (Left.is(TT_UnaryOperator)) {
    // Lambda captures allow for a lone &, so "&]" needs to be properly
    // handled.
    if (Left.is(tok::amp) && Right.is(tok::r_square))
      return Style.SpacesInSquareBrackets;
    return Style.SpaceAfterLogicalNot && Left.is(tok::exclaim);
  }

  // If the next token is a binary operator or a selector name, we have
  // incorrectly classified the parenthesis as a cast. FIXME: Detect correctly.
  if (Left.is(TT_CastRParen)) {
    return Style.SpaceAfterCStyleCast ||
           Right.isOneOf(TT_BinaryOperator, TT_SelectorName);
  }

  auto ShouldAddSpacesInAngles = [this, &Right]() {
    if (this->Style.SpacesInAngles == FormatStyle::SIAS_Always)
      return true;
    if (this->Style.SpacesInAngles == FormatStyle::SIAS_Leave)
      return Right.hasWhitespaceBefore();
    return false;
  };

  if (Left.is(tok::greater) && Right.is(tok::greater)) {
    if (Style.Language == FormatStyle::LK_TextProto ||
        (Style.Language == FormatStyle::LK_Proto && Left.is(TT_DictLiteral))) {
      return !Style.Cpp11BracedListStyle;
    }
    return Right.is(TT_TemplateCloser) && Left.is(TT_TemplateCloser) &&
           ((Style.Standard < FormatStyle::LS_Cpp11) ||
            ShouldAddSpacesInAngles());
  }
  if (Right.isOneOf(tok::arrow, tok::arrowstar, tok::periodstar) ||
      Left.isOneOf(tok::arrow, tok::period, tok::arrowstar, tok::periodstar) ||
      (Right.is(tok::period) && Right.isNot(TT_DesignatedInitializerPeriod))) {
    return false;
  }
  if (!Style.SpaceBeforeAssignmentOperators && Left.isNot(TT_TemplateCloser) &&
      Right.getPrecedence() == prec::Assignment) {
    return false;
  }
  if (Style.Language == FormatStyle::LK_Java && Right.is(tok::coloncolon) &&
      (Left.is(tok::identifier) || Left.is(tok::kw_this))) {
    return false;
  }
  if (Right.is(tok::coloncolon) && Left.is(tok::identifier)) {
    // Generally don't remove existing spaces between an identifier and "::".
    // The identifier might actually be a macro name such as ALWAYS_INLINE. If
    // this turns out to be too lenient, add analysis of the identifier itself.
    return Right.hasWhitespaceBefore();
  }
  if (Right.is(tok::coloncolon) &&
      !Left.isOneOf(tok::l_brace, tok::comment, tok::l_paren)) {
    // Put a space between < and :: in vector< ::std::string >
    return (Left.is(TT_TemplateOpener) &&
            ((Style.Standard < FormatStyle::LS_Cpp11) ||
             ShouldAddSpacesInAngles())) ||
           !(Left.isOneOf(tok::l_paren, tok::r_paren, tok::l_square,
                          tok::kw___super, TT_TemplateOpener,
                          TT_TemplateCloser)) ||
           (Left.is(tok::l_paren) && Style.SpacesInParensOptions.Other);
  }
  if ((Left.is(TT_TemplateOpener)) != (Right.is(TT_TemplateCloser)))
    return ShouldAddSpacesInAngles();
  // Space before TT_StructuredBindingLSquare.
  if (Right.is(TT_StructuredBindingLSquare)) {
    return !Left.isOneOf(tok::amp, tok::ampamp) ||
           getTokenReferenceAlignment(Left) != FormatStyle::PAS_Right;
  }
  // Space before & or && following a TT_StructuredBindingLSquare.
  if (Right.Next && Right.Next->is(TT_StructuredBindingLSquare) &&
      Right.isOneOf(tok::amp, tok::ampamp)) {
    return getTokenReferenceAlignment(Right) != FormatStyle::PAS_Left;
  }
  if ((Right.is(TT_BinaryOperator) && Left.isNot(tok::l_paren)) ||
      (Left.isOneOf(TT_BinaryOperator, TT_ConditionalExpr) &&
       Right.isNot(tok::r_paren))) {
    return true;
  }
  if (Right.is(TT_TemplateOpener) && Left.is(tok::r_paren) &&
      Left.MatchingParen &&
      Left.MatchingParen->is(TT_OverloadedOperatorLParen)) {
    return false;
  }
  if (Right.is(tok::less) && Left.isNot(tok::l_paren) &&
      Line.Type == LT_ImportStatement) {
    return true;
  }
  if (Right.is(TT_TrailingUnaryOperator))
    return false;
  if (Left.is(TT_RegexLiteral))
    return false;
  return spaceRequiredBetween(Line, Left, Right);
}

// Returns 'true' if 'Tok' is a brace we'd want to break before in Allman style.
static bool isAllmanBrace(const FormatToken &Tok) {
  return Tok.is(tok::l_brace) && Tok.is(BK_Block) &&
         !Tok.isOneOf(TT_ObjCBlockLBrace, TT_LambdaLBrace, TT_DictLiteral);
}

// Returns 'true' if 'Tok' is a function argument.
static bool IsFunctionArgument(const FormatToken &Tok) {
  return Tok.MatchingParen && Tok.MatchingParen->Next &&
         Tok.MatchingParen->Next->isOneOf(tok::comma, tok::r_paren);
}

static bool
isItAnEmptyLambdaAllowed(const FormatToken &Tok,
                         FormatStyle::ShortLambdaStyle ShortLambdaOption) {
  return Tok.Children.empty() && ShortLambdaOption != FormatStyle::SLS_None;
}

static bool isAllmanLambdaBrace(const FormatToken &Tok) {
  return Tok.is(tok::l_brace) && Tok.is(BK_Block) &&
         !Tok.isOneOf(TT_ObjCBlockLBrace, TT_DictLiteral);
}

bool TokenAnnotator::mustBreakBefore(const AnnotatedLine &Line,
                                     const FormatToken &Right) const {
  const FormatToken &Left = *Right.Previous;
  if (Right.NewlinesBefore > 1 && Style.MaxEmptyLinesToKeep > 0)
    return true;

  if (Style.BreakFunctionDefinitionParameters && Line.MightBeFunctionDecl &&
      Line.mightBeFunctionDefinition() && Left.MightBeFunctionDeclParen &&
      Left.ParameterCount > 0) {
    return true;
  }

  const auto *BeforeLeft = Left.Previous;
  const auto *AfterRight = Right.Next;

  if (Style.isCSharp()) {
    if (Left.is(TT_FatArrow) && Right.is(tok::l_brace) &&
        Style.BraceWrapping.AfterFunction) {
      return true;
    }
    if (Right.is(TT_CSharpNamedArgumentColon) ||
        Left.is(TT_CSharpNamedArgumentColon)) {
      return false;
    }
    if (Right.is(TT_CSharpGenericTypeConstraint))
      return true;
    if (AfterRight && AfterRight->is(TT_FatArrow) &&
        (Right.is(tok::numeric_constant) ||
         (Right.is(tok::identifier) && Right.TokenText == "_"))) {
      return true;
    }

    // Break after C# [...] and before public/protected/private/internal.
    if (Left.is(TT_AttributeSquare) && Left.is(tok::r_square) &&
        (Right.isAccessSpecifier(/*ColonRequired=*/false) ||
         Right.is(Keywords.kw_internal))) {
      return true;
    }
    // Break between ] and [ but only when there are really 2 attributes.
    if (Left.is(TT_AttributeSquare) && Right.is(TT_AttributeSquare) &&
        Left.is(tok::r_square) && Right.is(tok::l_square)) {
      return true;
    }
  } else if (Style.isJavaScript()) {
    // FIXME: This might apply to other languages and token kinds.
    if (Right.is(tok::string_literal) && Left.is(tok::plus) && BeforeLeft &&
        BeforeLeft->is(tok::string_literal)) {
      return true;
    }
    if (Left.is(TT_DictLiteral) && Left.is(tok::l_brace) && Line.Level == 0 &&
        BeforeLeft && BeforeLeft->is(tok::equal) &&
        Line.First->isOneOf(tok::identifier, Keywords.kw_import, tok::kw_export,
                            tok::kw_const) &&
        // kw_var/kw_let are pseudo-tokens that are tok::identifier, so match
        // above.
        !Line.First->isOneOf(Keywords.kw_var, Keywords.kw_let)) {
      // Object literals on the top level of a file are treated as "enum-style".
      // Each key/value pair is put on a separate line, instead of bin-packing.
      return true;
    }
    if (Left.is(tok::l_brace) && Line.Level == 0 &&
        (Line.startsWith(tok::kw_enum) ||
         Line.startsWith(tok::kw_const, tok::kw_enum) ||
         Line.startsWith(tok::kw_export, tok::kw_enum) ||
         Line.startsWith(tok::kw_export, tok::kw_const, tok::kw_enum))) {
      // JavaScript top-level enum key/value pairs are put on separate lines
      // instead of bin-packing.
      return true;
    }
    if (Right.is(tok::r_brace) && Left.is(tok::l_brace) && BeforeLeft &&
        BeforeLeft->is(TT_FatArrow)) {
      // JS arrow function (=> {...}).
      switch (Style.AllowShortLambdasOnASingleLine) {
      case FormatStyle::SLS_All:
        return false;
      case FormatStyle::SLS_None:
        return true;
      case FormatStyle::SLS_Empty:
        return !Left.Children.empty();
      case FormatStyle::SLS_Inline:
        // allow one-lining inline (e.g. in function call args) and empty arrow
        // functions.
        return (Left.NestingLevel == 0 && Line.Level == 0) &&
               !Left.Children.empty();
      }
      llvm_unreachable("Unknown FormatStyle::ShortLambdaStyle enum");
    }

    if (Right.is(tok::r_brace) && Left.is(tok::l_brace) &&
        !Left.Children.empty()) {
      // Support AllowShortFunctionsOnASingleLine for JavaScript.
      return Style.AllowShortFunctionsOnASingleLine == FormatStyle::SFS_None ||
             Style.AllowShortFunctionsOnASingleLine == FormatStyle::SFS_Empty ||
             (Left.NestingLevel == 0 && Line.Level == 0 &&
              Style.AllowShortFunctionsOnASingleLine &
                  FormatStyle::SFS_InlineOnly);
    }
  } else if (Style.Language == FormatStyle::LK_Java) {
    if (Right.is(tok::plus) && Left.is(tok::string_literal) && AfterRight &&
        AfterRight->is(tok::string_literal)) {
      return true;
    }
  } else if (Style.isVerilog()) {
    // Break between assignments.
    if (Left.is(TT_VerilogAssignComma))
      return true;
    // Break between ports of different types.
    if (Left.is(TT_VerilogTypeComma))
      return true;
    // Break between ports in a module instantiation and after the parameter
    // list.
    if (Style.VerilogBreakBetweenInstancePorts &&
        (Left.is(TT_VerilogInstancePortComma) ||
         (Left.is(tok::r_paren) && Keywords.isVerilogIdentifier(Right) &&
          Left.MatchingParen &&
          Left.MatchingParen->is(TT_VerilogInstancePortLParen)))) {
      return true;
    }
    // Break after labels. In Verilog labels don't have the 'case' keyword, so
    // it is hard to identify them in UnwrappedLineParser.
    if (!Keywords.isVerilogBegin(Right) && Keywords.isVerilogEndOfLabel(Left))
      return true;
  } else if (Style.BreakAdjacentStringLiterals &&
             (IsCpp || Style.isProto() ||
              Style.Language == FormatStyle::LK_TableGen)) {
    if (Left.isStringLiteral() && Right.isStringLiteral())
      return true;
  }

  // Basic JSON newline processing.
  if (Style.isJson()) {
    // Always break after a JSON record opener.
    // {
    // }
    if (Left.is(TT_DictLiteral) && Left.is(tok::l_brace))
      return true;
    // Always break after a JSON array opener based on BreakArrays.
    if ((Left.is(TT_ArrayInitializerLSquare) && Left.is(tok::l_square) &&
         Right.isNot(tok::r_square)) ||
        Left.is(tok::comma)) {
      if (Right.is(tok::l_brace))
        return true;
      // scan to the right if an we see an object or an array inside
      // then break.
      for (const auto *Tok = &Right; Tok; Tok = Tok->Next) {
        if (Tok->isOneOf(tok::l_brace, tok::l_square))
          return true;
        if (Tok->isOneOf(tok::r_brace, tok::r_square))
          break;
      }
      return Style.BreakArrays;
    }
  } else if (Style.isTableGen()) {
    // Break the comma in side cond operators.
    // !cond(case1:1,
    //       case2:0);
    if (Left.is(TT_TableGenCondOperatorComma))
      return true;
    if (Left.is(TT_TableGenDAGArgOperatorToBreak) &&
        Right.isNot(TT_TableGenDAGArgCloser)) {
      return true;
    }
    if (Left.is(TT_TableGenDAGArgListCommaToBreak))
      return true;
    if (Right.is(TT_TableGenDAGArgCloser) && Right.MatchingParen &&
        Right.MatchingParen->is(TT_TableGenDAGArgOpenerToBreak) &&
        &Left != Right.MatchingParen->Next) {
      // Check to avoid empty DAGArg such as (ins).
      return Style.TableGenBreakInsideDAGArg == FormatStyle::DAS_BreakAll;
    }
  }

  if (Line.startsWith(tok::kw_asm) && Right.is(TT_InlineASMColon) &&
      Style.BreakBeforeInlineASMColon == FormatStyle::BBIAS_Always) {
    return true;
  }

  // If the last token before a '}', ']', or ')' is a comma or a trailing
  // comment, the intention is to insert a line break after it in order to make
  // shuffling around entries easier. Import statements, especially in
  // JavaScript, can be an exception to this rule.
  if (Style.JavaScriptWrapImports || Line.Type != LT_ImportStatement) {
    const FormatToken *BeforeClosingBrace = nullptr;
    if ((Left.isOneOf(tok::l_brace, TT_ArrayInitializerLSquare) ||
         (Style.isJavaScript() && Left.is(tok::l_paren))) &&
        Left.isNot(BK_Block) && Left.MatchingParen) {
      BeforeClosingBrace = Left.MatchingParen->Previous;
    } else if (Right.MatchingParen &&
               (Right.MatchingParen->isOneOf(tok::l_brace,
                                             TT_ArrayInitializerLSquare) ||
                (Style.isJavaScript() &&
                 Right.MatchingParen->is(tok::l_paren)))) {
      BeforeClosingBrace = &Left;
    }
    if (BeforeClosingBrace && (BeforeClosingBrace->is(tok::comma) ||
                               BeforeClosingBrace->isTrailingComment())) {
      return true;
    }
  }

  if (Right.is(tok::comment)) {
    return Left.isNot(BK_BracedInit) && Left.isNot(TT_CtorInitializerColon) &&
           (Right.NewlinesBefore > 0 && Right.HasUnescapedNewline);
  }
  if (Left.isTrailingComment())
    return true;
  if (Left.IsUnterminatedLiteral)
    return true;

  if (BeforeLeft && BeforeLeft->is(tok::lessless) &&
      Left.is(tok::string_literal) && Right.is(tok::lessless) && AfterRight &&
      AfterRight->is(tok::string_literal)) {
    return Right.NewlinesBefore > 0;
  }

  if (Right.is(TT_RequiresClause)) {
    switch (Style.RequiresClausePosition) {
    case FormatStyle::RCPS_OwnLine:
    case FormatStyle::RCPS_WithFollowing:
      return true;
    default:
      break;
    }
  }
  // Can break after template<> declaration
  if (Left.ClosesTemplateDeclaration && Left.MatchingParen &&
      Left.MatchingParen->NestingLevel == 0) {
    // Put concepts on the next line e.g.
    // template<typename T>
    // concept ...
    if (Right.is(tok::kw_concept))
      return Style.BreakBeforeConceptDeclarations == FormatStyle::BBCDS_Always;
    return Style.BreakTemplateDeclarations == FormatStyle::BTDS_Yes ||
           (Style.BreakTemplateDeclarations == FormatStyle::BTDS_Leave &&
            Right.NewlinesBefore > 0);
  }
  if (Left.ClosesRequiresClause && Right.isNot(tok::semi)) {
    switch (Style.RequiresClausePosition) {
    case FormatStyle::RCPS_OwnLine:
    case FormatStyle::RCPS_WithPreceding:
      return true;
    default:
      break;
    }
  }
  if (Style.PackConstructorInitializers == FormatStyle::PCIS_Never) {
    if (Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeColon &&
        (Left.is(TT_CtorInitializerComma) ||
         Right.is(TT_CtorInitializerColon))) {
      return true;
    }

    if (Style.BreakConstructorInitializers == FormatStyle::BCIS_AfterColon &&
        Left.isOneOf(TT_CtorInitializerColon, TT_CtorInitializerComma)) {
      return true;
    }
  }
  if (Style.PackConstructorInitializers < FormatStyle::PCIS_CurrentLine &&
      Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeComma &&
      Right.isOneOf(TT_CtorInitializerComma, TT_CtorInitializerColon)) {
    return true;
  }
  if (Style.PackConstructorInitializers == FormatStyle::PCIS_NextLineOnly) {
    if ((Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeColon ||
         Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeComma) &&
        Right.is(TT_CtorInitializerColon)) {
      return true;
    }

    if (Style.BreakConstructorInitializers == FormatStyle::BCIS_AfterColon &&
        Left.is(TT_CtorInitializerColon)) {
      return true;
    }
  }
  // Break only if we have multiple inheritance.
  if (Style.BreakInheritanceList == FormatStyle::BILS_BeforeComma &&
      Right.is(TT_InheritanceComma)) {
    return true;
  }
  if (Style.BreakInheritanceList == FormatStyle::BILS_AfterComma &&
      Left.is(TT_InheritanceComma)) {
    return true;
  }
  if (Right.is(tok::string_literal) && Right.TokenText.starts_with("R\"")) {
    // Multiline raw string literals are special wrt. line breaks. The author
    // has made a deliberate choice and might have aligned the contents of the
    // string literal accordingly. Thus, we try keep existing line breaks.
    return Right.IsMultiline && Right.NewlinesBefore > 0;
  }
  if ((Left.is(tok::l_brace) ||
       (Left.is(tok::less) && BeforeLeft && BeforeLeft->is(tok::equal))) &&
      Right.NestingLevel == 1 && Style.Language == FormatStyle::LK_Proto) {
    // Don't put enums or option definitions onto single lines in protocol
    // buffers.
    return true;
  }
  if (Right.is(TT_InlineASMBrace))
    return Right.HasUnescapedNewline;

  if (isAllmanBrace(Left) || isAllmanBrace(Right)) {
    auto *FirstNonComment = Line.getFirstNonComment();
    bool AccessSpecifier =
        FirstNonComment && (FirstNonComment->is(Keywords.kw_internal) ||
                            FirstNonComment->isAccessSpecifierKeyword());

    if (Style.BraceWrapping.AfterEnum) {
      if (Line.startsWith(tok::kw_enum) ||
          Line.startsWith(tok::kw_typedef, tok::kw_enum)) {
        return true;
      }
      // Ensure BraceWrapping for `public enum A {`.
      if (AccessSpecifier && FirstNonComment->Next &&
          FirstNonComment->Next->is(tok::kw_enum)) {
        return true;
      }
    }

    // Ensure BraceWrapping for `public interface A {`.
    if (Style.BraceWrapping.AfterClass &&
        ((AccessSpecifier && FirstNonComment->Next &&
          FirstNonComment->Next->is(Keywords.kw_interface)) ||
         Line.startsWith(Keywords.kw_interface))) {
      return true;
    }

    // Don't attempt to interpret struct return types as structs.
    if (Right.isNot(TT_FunctionLBrace)) {
      return (Line.startsWith(tok::kw_class) &&
              Style.BraceWrapping.AfterClass) ||
             (Line.startsWith(tok::kw_struct) &&
              Style.BraceWrapping.AfterStruct);
    }
  }

  if (Left.is(TT_ObjCBlockLBrace) &&
      Style.AllowShortBlocksOnASingleLine == FormatStyle::SBS_Never) {
    return true;
  }

  // Ensure wrapping after __attribute__((XX)) and @interface etc.
  if (Left.isOneOf(TT_AttributeRParen, TT_AttributeMacro) &&
      Right.is(TT_ObjCDecl)) {
    return true;
  }

  if (Left.is(TT_LambdaLBrace)) {
    if (IsFunctionArgument(Left) &&
        Style.AllowShortLambdasOnASingleLine == FormatStyle::SLS_Inline) {
      return false;
    }

    if (Style.AllowShortLambdasOnASingleLine == FormatStyle::SLS_None ||
        Style.AllowShortLambdasOnASingleLine == FormatStyle::SLS_Inline ||
        (!Left.Children.empty() &&
         Style.AllowShortLambdasOnASingleLine == FormatStyle::SLS_Empty)) {
      return true;
    }
  }

  if (Style.BraceWrapping.BeforeLambdaBody && Right.is(TT_LambdaLBrace) &&
      (Left.isPointerOrReference() || Left.is(TT_TemplateCloser))) {
    return true;
  }

  // Put multiple Java annotation on a new line.
  if ((Style.Language == FormatStyle::LK_Java || Style.isJavaScript()) &&
      Left.is(TT_LeadingJavaAnnotation) &&
      Right.isNot(TT_LeadingJavaAnnotation) && Right.isNot(tok::l_paren) &&
      (Line.Last->is(tok::l_brace) || Style.BreakAfterJavaFieldAnnotations)) {
    return true;
  }

  if (Right.is(TT_ProtoExtensionLSquare))
    return true;

  // In text proto instances if a submessage contains at least 2 entries and at
  // least one of them is a submessage, like A { ... B { ... } ... },
  // put all of the entries of A on separate lines by forcing the selector of
  // the submessage B to be put on a newline.
  //
  // Example: these can stay on one line:
  // a { scalar_1: 1 scalar_2: 2 }
  // a { b { key: value } }
  //
  // and these entries need to be on a new line even if putting them all in one
  // line is under the column limit:
  // a {
  //   scalar: 1
  //   b { key: value }
  // }
  //
  // We enforce this by breaking before a submessage field that has previous
  // siblings, *and* breaking before a field that follows a submessage field.
  //
  // Be careful to exclude the case  [proto.ext] { ... } since the `]` is
  // the TT_SelectorName there, but we don't want to break inside the brackets.
  //
  // Another edge case is @submessage { key: value }, which is a common
  // substitution placeholder. In this case we want to keep `@` and `submessage`
  // together.
  //
  // We ensure elsewhere that extensions are always on their own line.
  if (Style.isProto() && Right.is(TT_SelectorName) &&
      Right.isNot(tok::r_square) && AfterRight) {
    // Keep `@submessage` together in:
    // @submessage { key: value }
    if (Left.is(tok::at))
      return false;
    // Look for the scope opener after selector in cases like:
    // selector { ...
    // selector: { ...
    // selector: @base { ...
    const auto *LBrace = AfterRight;
    if (LBrace && LBrace->is(tok::colon)) {
      LBrace = LBrace->Next;
      if (LBrace && LBrace->is(tok::at)) {
        LBrace = LBrace->Next;
        if (LBrace)
          LBrace = LBrace->Next;
      }
    }
    if (LBrace &&
        // The scope opener is one of {, [, <:
        // selector { ... }
        // selector [ ... ]
        // selector < ... >
        //
        // In case of selector { ... }, the l_brace is TT_DictLiteral.
        // In case of an empty selector {}, the l_brace is not TT_DictLiteral,
        // so we check for immediately following r_brace.
        ((LBrace->is(tok::l_brace) &&
          (LBrace->is(TT_DictLiteral) ||
           (LBrace->Next && LBrace->Next->is(tok::r_brace)))) ||
         LBrace->is(TT_ArrayInitializerLSquare) || LBrace->is(tok::less))) {
      // If Left.ParameterCount is 0, then this submessage entry is not the
      // first in its parent submessage, and we want to break before this entry.
      // If Left.ParameterCount is greater than 0, then its parent submessage
      // might contain 1 or more entries and we want to break before this entry
      // if it contains at least 2 entries. We deal with this case later by
      // detecting and breaking before the next entry in the parent submessage.
      if (Left.ParameterCount == 0)
        return true;
      // However, if this submessage is the first entry in its parent
      // submessage, Left.ParameterCount might be 1 in some cases.
      // We deal with this case later by detecting an entry
      // following a closing paren of this submessage.
    }

    // If this is an entry immediately following a submessage, it will be
    // preceded by a closing paren of that submessage, like in:
    //     left---.  .---right
    //            v  v
    // sub: { ... } key: value
    // If there was a comment between `}` an `key` above, then `key` would be
    // put on a new line anyways.
    if (Left.isOneOf(tok::r_brace, tok::greater, tok::r_square))
      return true;
  }

  return false;
}

bool TokenAnnotator::canBreakBefore(const AnnotatedLine &Line,
                                    const FormatToken &Right) const {
  const FormatToken &Left = *Right.Previous;
  // Language-specific stuff.
  if (Style.isCSharp()) {
    if (Left.isOneOf(TT_CSharpNamedArgumentColon, TT_AttributeColon) ||
        Right.isOneOf(TT_CSharpNamedArgumentColon, TT_AttributeColon)) {
      return false;
    }
    // Only break after commas for generic type constraints.
    if (Line.First->is(TT_CSharpGenericTypeConstraint))
      return Left.is(TT_CSharpGenericTypeConstraintComma);
    // Keep nullable operators attached to their identifiers.
    if (Right.is(TT_CSharpNullable))
      return false;
  } else if (Style.Language == FormatStyle::LK_Java) {
    if (Left.isOneOf(Keywords.kw_throws, Keywords.kw_extends,
                     Keywords.kw_implements)) {
      return false;
    }
    if (Right.isOneOf(Keywords.kw_throws, Keywords.kw_extends,
                      Keywords.kw_implements)) {
      return true;
    }
  } else if (Style.isJavaScript()) {
    const FormatToken *NonComment = Right.getPreviousNonComment();
    if (NonComment &&
        (NonComment->isAccessSpecifierKeyword() ||
         NonComment->isOneOf(
             tok::kw_return, Keywords.kw_yield, tok::kw_continue, tok::kw_break,
             tok::kw_throw, Keywords.kw_interface, Keywords.kw_type,
             tok::kw_static, Keywords.kw_readonly, Keywords.kw_override,
             Keywords.kw_abstract, Keywords.kw_get, Keywords.kw_set,
             Keywords.kw_async, Keywords.kw_await))) {
      return false; // Otherwise automatic semicolon insertion would trigger.
    }
    if (Right.NestingLevel == 0 &&
        (Left.Tok.getIdentifierInfo() ||
         Left.isOneOf(tok::r_square, tok::r_paren)) &&
        Right.isOneOf(tok::l_square, tok::l_paren)) {
      return false; // Otherwise automatic semicolon insertion would trigger.
    }
    if (NonComment && NonComment->is(tok::identifier) &&
        NonComment->TokenText == "asserts") {
      return false;
    }
    if (Left.is(TT_FatArrow) && Right.is(tok::l_brace))
      return false;
    if (Left.is(TT_JsTypeColon))
      return true;
    // Don't wrap between ":" and "!" of a strict prop init ("field!: type;").
    if (Left.is(tok::exclaim) && Right.is(tok::colon))
      return false;
    // Look for is type annotations like:
    // function f(): a is B { ... }
    // Do not break before is in these cases.
    if (Right.is(Keywords.kw_is)) {
      const FormatToken *Next = Right.getNextNonComment();
      // If `is` is followed by a colon, it's likely that it's a dict key, so
      // ignore it for this check.
      // For example this is common in Polymer:
      // Polymer({
      //   is: 'name',
      //   ...
      // });
      if (!Next || Next->isNot(tok::colon))
        return false;
    }
    if (Left.is(Keywords.kw_in))
      return Style.BreakBeforeBinaryOperators == FormatStyle::BOS_None;
    if (Right.is(Keywords.kw_in))
      return Style.BreakBeforeBinaryOperators != FormatStyle::BOS_None;
    if (Right.is(Keywords.kw_as))
      return false; // must not break before as in 'x as type' casts
    if (Right.isOneOf(Keywords.kw_extends, Keywords.kw_infer)) {
      // extends and infer can appear as keywords in conditional types:
      //   https://www.typescriptlang.org/docs/handbook/release-notes/typescript-2-8.html#conditional-types
      // do not break before them, as the expressions are subject to ASI.
      return false;
    }
    if (Left.is(Keywords.kw_as))
      return true;
    if (Left.is(TT_NonNullAssertion))
      return true;
    if (Left.is(Keywords.kw_declare) &&
        Right.isOneOf(Keywords.kw_module, tok::kw_namespace,
                      Keywords.kw_function, tok::kw_class, tok::kw_enum,
                      Keywords.kw_interface, Keywords.kw_type, Keywords.kw_var,
                      Keywords.kw_let, tok::kw_const)) {
      // See grammar for 'declare' statements at:
      // https://github.com/Microsoft/TypeScript/blob/main/doc/spec-ARCHIVED.md#A.10
      return false;
    }
    if (Left.isOneOf(Keywords.kw_module, tok::kw_namespace) &&
        Right.isOneOf(tok::identifier, tok::string_literal)) {
      return false; // must not break in "module foo { ...}"
    }
    if (Right.is(TT_TemplateString) && Right.closesScope())
      return false;
    // Don't split tagged template literal so there is a break between the tag
    // identifier and template string.
    if (Left.is(tok::identifier) && Right.is(TT_TemplateString))
      return false;
    if (Left.is(TT_TemplateString) && Left.opensScope())
      return true;
  } else if (Style.isTableGen()) {
    // Avoid to break after "def", "class", "let" and so on.
    if (Keywords.isTableGenDefinition(Left))
      return false;
    // Avoid to break after '(' in the cases that is in bang operators.
    if (Right.is(tok::l_paren)) {
      return !Left.isOneOf(TT_TableGenBangOperator, TT_TableGenCondOperator,
                           TT_TemplateCloser);
    }
    // Avoid to break between the value and its suffix part.
    if (Left.is(TT_TableGenValueSuffix))
      return false;
    // Avoid to break around paste operator.
    if (Left.is(tok::hash) || Right.is(tok::hash))
      return false;
    if (Left.isOneOf(TT_TableGenBangOperator, TT_TableGenCondOperator))
      return false;
  }

  if (Left.is(tok::at))
    return false;
  if (Left.Tok.getObjCKeywordID() == tok::objc_interface)
    return false;
  if (Left.isOneOf(TT_JavaAnnotation, TT_LeadingJavaAnnotation))
    return Right.isNot(tok::l_paren);
  if (Right.is(TT_PointerOrReference)) {
    return Line.IsMultiVariableDeclStmt ||
           (getTokenPointerOrReferenceAlignment(Right) ==
                FormatStyle::PAS_Right &&
            (!Right.Next || Right.Next->isNot(TT_FunctionDeclarationName)));
  }
  if (Right.isOneOf(TT_StartOfName, TT_FunctionDeclarationName) ||
      Right.is(tok::kw_operator)) {
    return true;
  }
  if (Left.is(TT_PointerOrReference))
    return false;
  if (Right.isTrailingComment()) {
    // We rely on MustBreakBefore being set correctly here as we should not
    // change the "binding" behavior of a comment.
    // The first comment in a braced lists is always interpreted as belonging to
    // the first list element. Otherwise, it should be placed outside of the
    // list.
    return Left.is(BK_BracedInit) ||
           (Left.is(TT_CtorInitializerColon) && Right.NewlinesBefore > 0 &&
            Style.BreakConstructorInitializers == FormatStyle::BCIS_AfterColon);
  }
  if (Left.is(tok::question) && Right.is(tok::colon))
    return false;
  if (Right.is(TT_ConditionalExpr) || Right.is(tok::question))
    return Style.BreakBeforeTernaryOperators;
  if (Left.is(TT_ConditionalExpr) || Left.is(tok::question))
    return !Style.BreakBeforeTernaryOperators;
  if (Left.is(TT_InheritanceColon))
    return Style.BreakInheritanceList == FormatStyle::BILS_AfterColon;
  if (Right.is(TT_InheritanceColon))
    return Style.BreakInheritanceList != FormatStyle::BILS_AfterColon;
  if (Right.is(TT_ObjCMethodExpr) && Right.isNot(tok::r_square) &&
      Left.isNot(TT_SelectorName)) {
    return true;
  }

  if (Right.is(tok::colon) &&
      !Right.isOneOf(TT_CtorInitializerColon, TT_InlineASMColon)) {
    return false;
  }
  if (Left.is(tok::colon) && Left.isOneOf(TT_DictLiteral, TT_ObjCMethodExpr)) {
    if (Style.isProto()) {
      if (!Style.AlwaysBreakBeforeMultilineStrings && Right.isStringLiteral())
        return false;
      // Prevent cases like:
      //
      // submessage:
      //     { key: valueeeeeeeeeeee }
      //
      // when the snippet does not fit into one line.
      // Prefer:
      //
      // submessage: {
      //   key: valueeeeeeeeeeee
      // }
      //
      // instead, even if it is longer by one line.
      //
      // Note that this allows the "{" to go over the column limit
      // when the column limit is just between ":" and "{", but that does
      // not happen too often and alternative formattings in this case are
      // not much better.
      //
      // The code covers the cases:
      //
      // submessage: { ... }
      // submessage: < ... >
      // repeated: [ ... ]
      if (((Right.is(tok::l_brace) || Right.is(tok::less)) &&
           Right.is(TT_DictLiteral)) ||
          Right.is(TT_ArrayInitializerLSquare)) {
        return false;
      }
    }
    return true;
  }
  if (Right.is(tok::r_square) && Right.MatchingParen &&
      Right.MatchingParen->is(TT_ProtoExtensionLSquare)) {
    return false;
  }
  if (Right.is(TT_SelectorName) || (Right.is(tok::identifier) && Right.Next &&
                                    Right.Next->is(TT_ObjCMethodExpr))) {
    return Left.isNot(tok::period); // FIXME: Properly parse ObjC calls.
  }
  if (Left.is(tok::r_paren) && Line.Type == LT_ObjCProperty)
    return true;
  if (Right.is(tok::kw_concept))
    return Style.BreakBeforeConceptDeclarations != FormatStyle::BBCDS_Never;
  if (Right.is(TT_RequiresClause))
    return true;
  if (Left.ClosesTemplateDeclaration) {
    return Style.BreakTemplateDeclarations != FormatStyle::BTDS_Leave ||
           Right.NewlinesBefore > 0;
  }
  if (Left.is(TT_FunctionAnnotationRParen))
    return true;
  if (Left.ClosesRequiresClause)
    return true;
  if (Right.isOneOf(TT_RangeBasedForLoopColon, TT_OverloadedOperatorLParen,
                    TT_OverloadedOperator)) {
    return false;
  }
  if (Left.is(TT_RangeBasedForLoopColon))
    return true;
  if (Right.is(TT_RangeBasedForLoopColon))
    return false;
  if (Left.is(TT_TemplateCloser) && Right.is(TT_TemplateOpener))
    return true;
  if ((Left.is(tok::greater) && Right.is(tok::greater)) ||
      (Left.is(tok::less) && Right.is(tok::less))) {
    return false;
  }
  if (Right.is(TT_BinaryOperator) &&
      Style.BreakBeforeBinaryOperators != FormatStyle::BOS_None &&
      (Style.BreakBeforeBinaryOperators == FormatStyle::BOS_All ||
       Right.getPrecedence() != prec::Assignment)) {
    return true;
  }
  if (Left.isOneOf(TT_TemplateCloser, TT_UnaryOperator) ||
      Left.is(tok::kw_operator)) {
    return false;
  }
  if (Left.is(tok::equal) && !Right.isOneOf(tok::kw_default, tok::kw_delete) &&
      Line.Type == LT_VirtualFunctionDecl && Left.NestingLevel == 0) {
    return false;
  }
  if (Left.is(tok::equal) && Right.is(tok::l_brace) &&
      !Style.Cpp11BracedListStyle) {
    return false;
  }
  if (Left.is(TT_AttributeLParen) ||
      (Left.is(tok::l_paren) && Left.is(TT_TypeDeclarationParen))) {
    return false;
  }
  if (Left.is(tok::l_paren) && Left.Previous &&
      (Left.Previous->isOneOf(TT_BinaryOperator, TT_CastRParen))) {
    return false;
  }
  if (Right.is(TT_ImplicitStringLiteral))
    return false;

  if (Right.is(TT_TemplateCloser))
    return false;
  if (Right.is(tok::r_square) && Right.MatchingParen &&
      Right.MatchingParen->is(TT_LambdaLSquare)) {
    return false;
  }

  // We only break before r_brace if there was a corresponding break before
  // the l_brace, which is tracked by BreakBeforeClosingBrace.
  if (Right.is(tok::r_brace)) {
    return Right.MatchingParen && (Right.MatchingParen->is(BK_Block) ||
                                   (Right.isBlockIndentedInitRBrace(Style)));
  }

  // We only break before r_paren if we're in a block indented context.
  if (Right.is(tok::r_paren)) {
    if (Style.AlignAfterOpenBracket != FormatStyle::BAS_BlockIndent ||
        !Right.MatchingParen) {
      return false;
    }
    auto Next = Right.Next;
    if (Next && Next->is(tok::r_paren))
      Next = Next->Next;
    if (Next && Next->is(tok::l_paren))
      return false;
    const FormatToken *Previous = Right.MatchingParen->Previous;
    return !(Previous && (Previous->is(tok::kw_for) || Previous->isIf()));
  }

  // Allow breaking after a trailing annotation, e.g. after a method
  // declaration.
  if (Left.is(TT_TrailingAnnotation)) {
    return !Right.isOneOf(tok::l_brace, tok::semi, tok::equal, tok::l_paren,
                          tok::less, tok::coloncolon);
  }

  if (Right.isAttribute())
    return true;

  if (Right.is(tok::l_square) && Right.is(TT_AttributeSquare))
    return Left.isNot(TT_AttributeSquare);

  if (Left.is(tok::identifier) && Right.is(tok::string_literal))
    return true;

  if (Right.is(tok::identifier) && Right.Next && Right.Next->is(TT_DictLiteral))
    return true;

  if (Left.is(TT_CtorInitializerColon)) {
    return Style.BreakConstructorInitializers == FormatStyle::BCIS_AfterColon &&
           (!Right.isTrailingComment() || Right.NewlinesBefore > 0);
  }
  if (Right.is(TT_CtorInitializerColon))
    return Style.BreakConstructorInitializers != FormatStyle::BCIS_AfterColon;
  if (Left.is(TT_CtorInitializerComma) &&
      Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeComma) {
    return false;
  }
  if (Right.is(TT_CtorInitializerComma) &&
      Style.BreakConstructorInitializers == FormatStyle::BCIS_BeforeComma) {
    return true;
  }
  if (Left.is(TT_InheritanceComma) &&
      Style.BreakInheritanceList == FormatStyle::BILS_BeforeComma) {
    return false;
  }
  if (Right.is(TT_InheritanceComma) &&
      Style.BreakInheritanceList == FormatStyle::BILS_BeforeComma) {
    return true;
  }
  if (Left.is(TT_ArrayInitializerLSquare))
    return true;
  if (Right.is(tok::kw_typename) && Left.isNot(tok::kw_const))
    return true;
  if ((Left.isBinaryOperator() || Left.is(TT_BinaryOperator)) &&
      !Left.isOneOf(tok::arrowstar, tok::lessless) &&
      Style.BreakBeforeBinaryOperators != FormatStyle::BOS_All &&
      (Style.BreakBeforeBinaryOperators == FormatStyle::BOS_None ||
       Left.getPrecedence() == prec::Assignment)) {
    return true;
  }
  if ((Left.is(TT_AttributeSquare) && Right.is(tok::l_square)) ||
      (Left.is(tok::r_square) && Right.is(TT_AttributeSquare))) {
    return false;
  }

  auto ShortLambdaOption = Style.AllowShortLambdasOnASingleLine;
  if (Style.BraceWrapping.BeforeLambdaBody && Right.is(TT_LambdaLBrace)) {
    if (isAllmanLambdaBrace(Left))
      return !isItAnEmptyLambdaAllowed(Left, ShortLambdaOption);
    if (isAllmanLambdaBrace(Right))
      return !isItAnEmptyLambdaAllowed(Right, ShortLambdaOption);
  }

  if (Right.is(tok::kw_noexcept) && Right.is(TT_TrailingAnnotation)) {
    switch (Style.AllowBreakBeforeNoexceptSpecifier) {
    case FormatStyle::BBNSS_Never:
      return false;
    case FormatStyle::BBNSS_Always:
      return true;
    case FormatStyle::BBNSS_OnlyWithParen:
      return Right.Next && Right.Next->is(tok::l_paren);
    }
  }

  return Left.isOneOf(tok::comma, tok::coloncolon, tok::semi, tok::l_brace,
                      tok::kw_class, tok::kw_struct, tok::comment) ||
         Right.isMemberAccess() ||
         Right.isOneOf(TT_TrailingReturnArrow, TT_LambdaArrow, tok::lessless,
                       tok::colon, tok::l_square, tok::at) ||
         (Left.is(tok::r_paren) &&
          Right.isOneOf(tok::identifier, tok::kw_const)) ||
         (Left.is(tok::l_paren) && Right.isNot(tok::r_paren)) ||
         (Left.is(TT_TemplateOpener) && Right.isNot(TT_TemplateCloser));
}

void TokenAnnotator::printDebugInfo(const AnnotatedLine &Line) const {
  llvm::errs() << "AnnotatedTokens(L=" << Line.Level << ", P=" << Line.PPLevel
               << ", T=" << Line.Type << ", C=" << Line.IsContinuation
               << "):\n";
  const FormatToken *Tok = Line.First;
  while (Tok) {
    llvm::errs() << " M=" << Tok->MustBreakBefore
                 << " C=" << Tok->CanBreakBefore
                 << " T=" << getTokenTypeName(Tok->getType())
                 << " S=" << Tok->SpacesRequiredBefore
                 << " F=" << Tok->Finalized << " B=" << Tok->BlockParameterCount
                 << " BK=" << Tok->getBlockKind() << " P=" << Tok->SplitPenalty
                 << " Name=" << Tok->Tok.getName() << " L=" << Tok->TotalLength
                 << " PPK=" << Tok->getPackingKind() << " FakeLParens=";
    for (prec::Level LParen : Tok->FakeLParens)
      llvm::errs() << LParen << "/";
    llvm::errs() << " FakeRParens=" << Tok->FakeRParens;
    llvm::errs() << " II=" << Tok->Tok.getIdentifierInfo();
    llvm::errs() << " Text='" << Tok->TokenText << "'\n";
    if (!Tok->Next)
      assert(Tok == Line.Last);
    Tok = Tok->Next;
  }
  llvm::errs() << "----\n";
}

FormatStyle::PointerAlignmentStyle
TokenAnnotator::getTokenReferenceAlignment(const FormatToken &Reference) const {
  assert(Reference.isOneOf(tok::amp, tok::ampamp));
  switch (Style.ReferenceAlignment) {
  case FormatStyle::RAS_Pointer:
    return Style.PointerAlignment;
  case FormatStyle::RAS_Left:
    return FormatStyle::PAS_Left;
  case FormatStyle::RAS_Right:
    return FormatStyle::PAS_Right;
  case FormatStyle::RAS_Middle:
    return FormatStyle::PAS_Middle;
  }
  assert(0); //"Unhandled value of ReferenceAlignment"
  return Style.PointerAlignment;
}

FormatStyle::PointerAlignmentStyle
TokenAnnotator::getTokenPointerOrReferenceAlignment(
    const FormatToken &PointerOrReference) const {
  if (PointerOrReference.isOneOf(tok::amp, tok::ampamp)) {
    switch (Style.ReferenceAlignment) {
    case FormatStyle::RAS_Pointer:
      return Style.PointerAlignment;
    case FormatStyle::RAS_Left:
      return FormatStyle::PAS_Left;
    case FormatStyle::RAS_Right:
      return FormatStyle::PAS_Right;
    case FormatStyle::RAS_Middle:
      return FormatStyle::PAS_Middle;
    }
  }
  assert(PointerOrReference.is(tok::star));
  return Style.PointerAlignment;
}

} // namespace format
} // namespace clang
