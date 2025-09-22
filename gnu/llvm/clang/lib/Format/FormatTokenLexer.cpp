//===--- FormatTokenLexer.cpp - Lex FormatTokens -------------*- C++ ----*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements FormatTokenLexer, which tokenizes a source file
/// into a FormatToken stream suitable for ClangFormat.
///
//===----------------------------------------------------------------------===//

#include "FormatTokenLexer.h"
#include "FormatToken.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Format/Format.h"
#include "llvm/Support/Regex.h"

namespace clang {
namespace format {

FormatTokenLexer::FormatTokenLexer(
    const SourceManager &SourceMgr, FileID ID, unsigned Column,
    const FormatStyle &Style, encoding::Encoding Encoding,
    llvm::SpecificBumpPtrAllocator<FormatToken> &Allocator,
    IdentifierTable &IdentTable)
    : FormatTok(nullptr), IsFirstToken(true), StateStack({LexerState::NORMAL}),
      Column(Column), TrailingWhitespace(0),
      LangOpts(getFormattingLangOpts(Style)), SourceMgr(SourceMgr), ID(ID),
      Style(Style), IdentTable(IdentTable), Keywords(IdentTable),
      Encoding(Encoding), Allocator(Allocator), FirstInLineIndex(0),
      FormattingDisabled(false), MacroBlockBeginRegex(Style.MacroBlockBegin),
      MacroBlockEndRegex(Style.MacroBlockEnd) {
  Lex.reset(new Lexer(ID, SourceMgr.getBufferOrFake(ID), SourceMgr, LangOpts));
  Lex->SetKeepWhitespaceMode(true);

  for (const std::string &ForEachMacro : Style.ForEachMacros) {
    auto Identifier = &IdentTable.get(ForEachMacro);
    Macros.insert({Identifier, TT_ForEachMacro});
  }
  for (const std::string &IfMacro : Style.IfMacros) {
    auto Identifier = &IdentTable.get(IfMacro);
    Macros.insert({Identifier, TT_IfMacro});
  }
  for (const std::string &AttributeMacro : Style.AttributeMacros) {
    auto Identifier = &IdentTable.get(AttributeMacro);
    Macros.insert({Identifier, TT_AttributeMacro});
  }
  for (const std::string &StatementMacro : Style.StatementMacros) {
    auto Identifier = &IdentTable.get(StatementMacro);
    Macros.insert({Identifier, TT_StatementMacro});
  }
  for (const std::string &TypenameMacro : Style.TypenameMacros) {
    auto Identifier = &IdentTable.get(TypenameMacro);
    Macros.insert({Identifier, TT_TypenameMacro});
  }
  for (const std::string &NamespaceMacro : Style.NamespaceMacros) {
    auto Identifier = &IdentTable.get(NamespaceMacro);
    Macros.insert({Identifier, TT_NamespaceMacro});
  }
  for (const std::string &WhitespaceSensitiveMacro :
       Style.WhitespaceSensitiveMacros) {
    auto Identifier = &IdentTable.get(WhitespaceSensitiveMacro);
    Macros.insert({Identifier, TT_UntouchableMacroFunc});
  }
  for (const std::string &StatementAttributeLikeMacro :
       Style.StatementAttributeLikeMacros) {
    auto Identifier = &IdentTable.get(StatementAttributeLikeMacro);
    Macros.insert({Identifier, TT_StatementAttributeLikeMacro});
  }

  for (const auto &TypeName : Style.TypeNames)
    TypeNames.insert(&IdentTable.get(TypeName));
}

ArrayRef<FormatToken *> FormatTokenLexer::lex() {
  assert(Tokens.empty());
  assert(FirstInLineIndex == 0);
  do {
    Tokens.push_back(getNextToken());
    if (Style.isJavaScript()) {
      tryParseJSRegexLiteral();
      handleTemplateStrings();
    }
    if (Style.Language == FormatStyle::LK_TextProto)
      tryParsePythonComment();
    tryMergePreviousTokens();
    if (Style.isCSharp()) {
      // This needs to come after tokens have been merged so that C#
      // string literals are correctly identified.
      handleCSharpVerbatimAndInterpolatedStrings();
    }
    if (Style.isTableGen()) {
      handleTableGenMultilineString();
      handleTableGenNumericLikeIdentifier();
    }
    if (Tokens.back()->NewlinesBefore > 0 || Tokens.back()->IsMultiline)
      FirstInLineIndex = Tokens.size() - 1;
  } while (Tokens.back()->isNot(tok::eof));
  if (Style.InsertNewlineAtEOF) {
    auto &TokEOF = *Tokens.back();
    if (TokEOF.NewlinesBefore == 0) {
      TokEOF.NewlinesBefore = 1;
      TokEOF.OriginalColumn = 0;
    }
  }
  return Tokens;
}

void FormatTokenLexer::tryMergePreviousTokens() {
  if (tryMerge_TMacro())
    return;
  if (tryMergeConflictMarkers())
    return;
  if (tryMergeLessLess())
    return;
  if (tryMergeGreaterGreater())
    return;
  if (tryMergeForEach())
    return;
  if (Style.isCpp() && tryTransformTryUsageForC())
    return;

  if (Style.isJavaScript() || Style.isCSharp()) {
    static const tok::TokenKind NullishCoalescingOperator[] = {tok::question,
                                                               tok::question};
    static const tok::TokenKind NullPropagatingOperator[] = {tok::question,
                                                             tok::period};
    static const tok::TokenKind FatArrow[] = {tok::equal, tok::greater};

    if (tryMergeTokens(FatArrow, TT_FatArrow))
      return;
    if (tryMergeTokens(NullishCoalescingOperator, TT_NullCoalescingOperator)) {
      // Treat like the "||" operator (as opposed to the ternary ?).
      Tokens.back()->Tok.setKind(tok::pipepipe);
      return;
    }
    if (tryMergeTokens(NullPropagatingOperator, TT_NullPropagatingOperator)) {
      // Treat like a regular "." access.
      Tokens.back()->Tok.setKind(tok::period);
      return;
    }
    if (tryMergeNullishCoalescingEqual())
      return;
  }

  if (Style.isCSharp()) {
    static const tok::TokenKind CSharpNullConditionalLSquare[] = {
        tok::question, tok::l_square};

    if (tryMergeCSharpKeywordVariables())
      return;
    if (tryMergeCSharpStringLiteral())
      return;
    if (tryTransformCSharpForEach())
      return;
    if (tryMergeTokens(CSharpNullConditionalLSquare,
                       TT_CSharpNullConditionalLSquare)) {
      // Treat like a regular "[" operator.
      Tokens.back()->Tok.setKind(tok::l_square);
      return;
    }
  }

  if (tryMergeNSStringLiteral())
    return;

  if (Style.isJavaScript()) {
    static const tok::TokenKind JSIdentity[] = {tok::equalequal, tok::equal};
    static const tok::TokenKind JSNotIdentity[] = {tok::exclaimequal,
                                                   tok::equal};
    static const tok::TokenKind JSShiftEqual[] = {tok::greater, tok::greater,
                                                  tok::greaterequal};
    static const tok::TokenKind JSExponentiation[] = {tok::star, tok::star};
    static const tok::TokenKind JSExponentiationEqual[] = {tok::star,
                                                           tok::starequal};
    static const tok::TokenKind JSPipePipeEqual[] = {tok::pipepipe, tok::equal};
    static const tok::TokenKind JSAndAndEqual[] = {tok::ampamp, tok::equal};

    // FIXME: Investigate what token type gives the correct operator priority.
    if (tryMergeTokens(JSIdentity, TT_BinaryOperator))
      return;
    if (tryMergeTokens(JSNotIdentity, TT_BinaryOperator))
      return;
    if (tryMergeTokens(JSShiftEqual, TT_BinaryOperator))
      return;
    if (tryMergeTokens(JSExponentiation, TT_JsExponentiation))
      return;
    if (tryMergeTokens(JSExponentiationEqual, TT_JsExponentiationEqual)) {
      Tokens.back()->Tok.setKind(tok::starequal);
      return;
    }
    if (tryMergeTokens(JSAndAndEqual, TT_JsAndAndEqual) ||
        tryMergeTokens(JSPipePipeEqual, TT_JsPipePipeEqual)) {
      // Treat like the "=" assignment operator.
      Tokens.back()->Tok.setKind(tok::equal);
      return;
    }
    if (tryMergeJSPrivateIdentifier())
      return;
  }

  if (Style.Language == FormatStyle::LK_Java) {
    static const tok::TokenKind JavaRightLogicalShiftAssign[] = {
        tok::greater, tok::greater, tok::greaterequal};
    if (tryMergeTokens(JavaRightLogicalShiftAssign, TT_BinaryOperator))
      return;
  }

  if (Style.isVerilog()) {
    // Merge the number following a base like `'h?a0`.
    if (Tokens.size() >= 3 && Tokens.end()[-3]->is(TT_VerilogNumberBase) &&
        Tokens.end()[-2]->is(tok::numeric_constant) &&
        Tokens.back()->isOneOf(tok::numeric_constant, tok::identifier,
                               tok::question) &&
        tryMergeTokens(2, TT_Unknown)) {
      return;
    }
    // Part select.
    if (tryMergeTokensAny({{tok::minus, tok::colon}, {tok::plus, tok::colon}},
                          TT_BitFieldColon)) {
      return;
    }
    // Xnor. The combined token is treated as a caret which can also be either a
    // unary or binary operator. The actual type is determined in
    // TokenAnnotator. We also check the token length so we know it is not
    // already a merged token.
    if (Tokens.back()->TokenText.size() == 1 &&
        tryMergeTokensAny({{tok::caret, tok::tilde}, {tok::tilde, tok::caret}},
                          TT_BinaryOperator)) {
      Tokens.back()->Tok.setKind(tok::caret);
      return;
    }
    // Signed shift and distribution weight.
    if (tryMergeTokens({tok::less, tok::less}, TT_BinaryOperator)) {
      Tokens.back()->Tok.setKind(tok::lessless);
      return;
    }
    if (tryMergeTokens({tok::greater, tok::greater}, TT_BinaryOperator)) {
      Tokens.back()->Tok.setKind(tok::greatergreater);
      return;
    }
    if (tryMergeTokensAny({{tok::lessless, tok::equal},
                           {tok::lessless, tok::lessequal},
                           {tok::greatergreater, tok::equal},
                           {tok::greatergreater, tok::greaterequal},
                           {tok::colon, tok::equal},
                           {tok::colon, tok::slash}},
                          TT_BinaryOperator)) {
      Tokens.back()->ForcedPrecedence = prec::Assignment;
      return;
    }
    // Exponentiation, signed shift, case equality, and wildcard equality.
    if (tryMergeTokensAny({{tok::star, tok::star},
                           {tok::lessless, tok::less},
                           {tok::greatergreater, tok::greater},
                           {tok::exclaimequal, tok::equal},
                           {tok::exclaimequal, tok::question},
                           {tok::equalequal, tok::equal},
                           {tok::equalequal, tok::question}},
                          TT_BinaryOperator)) {
      return;
    }
    // Module paths in specify blocks and the implication and boolean equality
    // operators.
    if (tryMergeTokensAny({{tok::plusequal, tok::greater},
                           {tok::plus, tok::star, tok::greater},
                           {tok::minusequal, tok::greater},
                           {tok::minus, tok::star, tok::greater},
                           {tok::less, tok::arrow},
                           {tok::equal, tok::greater},
                           {tok::star, tok::greater},
                           {tok::pipeequal, tok::greater},
                           {tok::pipe, tok::arrow},
                           {tok::hash, tok::minus, tok::hash},
                           {tok::hash, tok::equal, tok::hash}},
                          TT_BinaryOperator) ||
        Tokens.back()->is(tok::arrow)) {
      Tokens.back()->ForcedPrecedence = prec::Comma;
      return;
    }
  }
  if (Style.isTableGen()) {
    // TableGen's Multi line string starts with [{
    if (tryMergeTokens({tok::l_square, tok::l_brace},
                       TT_TableGenMultiLineString)) {
      // Set again with finalizing. This must never be annotated as other types.
      Tokens.back()->setFinalizedType(TT_TableGenMultiLineString);
      Tokens.back()->Tok.setKind(tok::string_literal);
      return;
    }
    // TableGen's bang operator is the form !<name>.
    // !cond is a special case with specific syntax.
    if (tryMergeTokens({tok::exclaim, tok::identifier},
                       TT_TableGenBangOperator)) {
      Tokens.back()->Tok.setKind(tok::identifier);
      Tokens.back()->Tok.setIdentifierInfo(nullptr);
      if (Tokens.back()->TokenText == "!cond")
        Tokens.back()->setFinalizedType(TT_TableGenCondOperator);
      else
        Tokens.back()->setFinalizedType(TT_TableGenBangOperator);
      return;
    }
    if (tryMergeTokens({tok::exclaim, tok::kw_if}, TT_TableGenBangOperator)) {
      // Here, "! if" becomes "!if".  That is, ! captures if even when the space
      // exists. That is only one possibility in TableGen's syntax.
      Tokens.back()->Tok.setKind(tok::identifier);
      Tokens.back()->Tok.setIdentifierInfo(nullptr);
      Tokens.back()->setFinalizedType(TT_TableGenBangOperator);
      return;
    }
    // +, - with numbers are literals. Not unary operators.
    if (tryMergeTokens({tok::plus, tok::numeric_constant}, TT_Unknown)) {
      Tokens.back()->Tok.setKind(tok::numeric_constant);
      return;
    }
    if (tryMergeTokens({tok::minus, tok::numeric_constant}, TT_Unknown)) {
      Tokens.back()->Tok.setKind(tok::numeric_constant);
      return;
    }
  }
}

bool FormatTokenLexer::tryMergeNSStringLiteral() {
  if (Tokens.size() < 2)
    return false;
  auto &At = *(Tokens.end() - 2);
  auto &String = *(Tokens.end() - 1);
  if (At->isNot(tok::at) || String->isNot(tok::string_literal))
    return false;
  At->Tok.setKind(tok::string_literal);
  At->TokenText = StringRef(At->TokenText.begin(),
                            String->TokenText.end() - At->TokenText.begin());
  At->ColumnWidth += String->ColumnWidth;
  At->setType(TT_ObjCStringLiteral);
  Tokens.erase(Tokens.end() - 1);
  return true;
}

bool FormatTokenLexer::tryMergeJSPrivateIdentifier() {
  // Merges #idenfier into a single identifier with the text #identifier
  // but the token tok::identifier.
  if (Tokens.size() < 2)
    return false;
  auto &Hash = *(Tokens.end() - 2);
  auto &Identifier = *(Tokens.end() - 1);
  if (Hash->isNot(tok::hash) || Identifier->isNot(tok::identifier))
    return false;
  Hash->Tok.setKind(tok::identifier);
  Hash->TokenText =
      StringRef(Hash->TokenText.begin(),
                Identifier->TokenText.end() - Hash->TokenText.begin());
  Hash->ColumnWidth += Identifier->ColumnWidth;
  Hash->setType(TT_JsPrivateIdentifier);
  Tokens.erase(Tokens.end() - 1);
  return true;
}

// Search for verbatim or interpolated string literals @"ABC" or
// $"aaaaa{abc}aaaaa" i and mark the token as TT_CSharpStringLiteral, and to
// prevent splitting of @, $ and ".
// Merging of multiline verbatim strings with embedded '"' is handled in
// handleCSharpVerbatimAndInterpolatedStrings with lower-level lexing.
bool FormatTokenLexer::tryMergeCSharpStringLiteral() {
  if (Tokens.size() < 2)
    return false;

  // Look for @"aaaaaa" or $"aaaaaa".
  const auto String = *(Tokens.end() - 1);
  if (String->isNot(tok::string_literal))
    return false;

  auto Prefix = *(Tokens.end() - 2);
  if (Prefix->isNot(tok::at) && Prefix->TokenText != "$")
    return false;

  if (Tokens.size() > 2) {
    const auto Tok = *(Tokens.end() - 3);
    if ((Tok->TokenText == "$" && Prefix->is(tok::at)) ||
        (Tok->is(tok::at) && Prefix->TokenText == "$")) {
      // This looks like $@"aaa" or @$"aaa" so we need to combine all 3 tokens.
      Tok->ColumnWidth += Prefix->ColumnWidth;
      Tokens.erase(Tokens.end() - 2);
      Prefix = Tok;
    }
  }

  // Convert back into just a string_literal.
  Prefix->Tok.setKind(tok::string_literal);
  Prefix->TokenText =
      StringRef(Prefix->TokenText.begin(),
                String->TokenText.end() - Prefix->TokenText.begin());
  Prefix->ColumnWidth += String->ColumnWidth;
  Prefix->setType(TT_CSharpStringLiteral);
  Tokens.erase(Tokens.end() - 1);
  return true;
}

// Valid C# attribute targets:
// https://docs.microsoft.com/en-us/dotnet/csharp/programming-guide/concepts/attributes/#attribute-targets
const llvm::StringSet<> FormatTokenLexer::CSharpAttributeTargets = {
    "assembly", "module",   "field",  "event", "method",
    "param",    "property", "return", "type",
};

bool FormatTokenLexer::tryMergeNullishCoalescingEqual() {
  if (Tokens.size() < 2)
    return false;
  auto &NullishCoalescing = *(Tokens.end() - 2);
  auto &Equal = *(Tokens.end() - 1);
  if (NullishCoalescing->isNot(TT_NullCoalescingOperator) ||
      Equal->isNot(tok::equal)) {
    return false;
  }
  NullishCoalescing->Tok.setKind(tok::equal); // no '??=' in clang tokens.
  NullishCoalescing->TokenText =
      StringRef(NullishCoalescing->TokenText.begin(),
                Equal->TokenText.end() - NullishCoalescing->TokenText.begin());
  NullishCoalescing->ColumnWidth += Equal->ColumnWidth;
  NullishCoalescing->setType(TT_NullCoalescingEqual);
  Tokens.erase(Tokens.end() - 1);
  return true;
}

bool FormatTokenLexer::tryMergeCSharpKeywordVariables() {
  if (Tokens.size() < 2)
    return false;
  const auto At = *(Tokens.end() - 2);
  if (At->isNot(tok::at))
    return false;
  const auto Keyword = *(Tokens.end() - 1);
  if (Keyword->TokenText == "$")
    return false;
  if (!Keywords.isCSharpKeyword(*Keyword))
    return false;

  At->Tok.setKind(tok::identifier);
  At->TokenText = StringRef(At->TokenText.begin(),
                            Keyword->TokenText.end() - At->TokenText.begin());
  At->ColumnWidth += Keyword->ColumnWidth;
  At->setType(Keyword->getType());
  Tokens.erase(Tokens.end() - 1);
  return true;
}

// In C# transform identifier foreach into kw_foreach
bool FormatTokenLexer::tryTransformCSharpForEach() {
  if (Tokens.size() < 1)
    return false;
  auto &Identifier = *(Tokens.end() - 1);
  if (Identifier->isNot(tok::identifier))
    return false;
  if (Identifier->TokenText != "foreach")
    return false;

  Identifier->setType(TT_ForEachMacro);
  Identifier->Tok.setKind(tok::kw_for);
  return true;
}

bool FormatTokenLexer::tryMergeForEach() {
  if (Tokens.size() < 2)
    return false;
  auto &For = *(Tokens.end() - 2);
  auto &Each = *(Tokens.end() - 1);
  if (For->isNot(tok::kw_for))
    return false;
  if (Each->isNot(tok::identifier))
    return false;
  if (Each->TokenText != "each")
    return false;

  For->setType(TT_ForEachMacro);
  For->Tok.setKind(tok::kw_for);

  For->TokenText = StringRef(For->TokenText.begin(),
                             Each->TokenText.end() - For->TokenText.begin());
  For->ColumnWidth += Each->ColumnWidth;
  Tokens.erase(Tokens.end() - 1);
  return true;
}

bool FormatTokenLexer::tryTransformTryUsageForC() {
  if (Tokens.size() < 2)
    return false;
  auto &Try = *(Tokens.end() - 2);
  if (Try->isNot(tok::kw_try))
    return false;
  auto &Next = *(Tokens.end() - 1);
  if (Next->isOneOf(tok::l_brace, tok::colon, tok::hash, tok::comment))
    return false;

  if (Tokens.size() > 2) {
    auto &At = *(Tokens.end() - 3);
    if (At->is(tok::at))
      return false;
  }

  Try->Tok.setKind(tok::identifier);
  return true;
}

bool FormatTokenLexer::tryMergeLessLess() {
  // Merge X,less,less,Y into X,lessless,Y unless X or Y is less.
  if (Tokens.size() < 3)
    return false;

  auto First = Tokens.end() - 3;
  if (First[0]->isNot(tok::less) || First[1]->isNot(tok::less))
    return false;

  // Only merge if there currently is no whitespace between the two "<".
  if (First[1]->hasWhitespaceBefore())
    return false;

  auto X = Tokens.size() > 3 ? First[-1] : nullptr;
  if (X && X->is(tok::less))
    return false;

  auto Y = First[2];
  if ((!X || X->isNot(tok::kw_operator)) && Y->is(tok::less))
    return false;

  First[0]->Tok.setKind(tok::lessless);
  First[0]->TokenText = "<<";
  First[0]->ColumnWidth += 1;
  Tokens.erase(Tokens.end() - 2);
  return true;
}

bool FormatTokenLexer::tryMergeGreaterGreater() {
  // Merge kw_operator,greater,greater into kw_operator,greatergreater.
  if (Tokens.size() < 2)
    return false;

  auto First = Tokens.end() - 2;
  if (First[0]->isNot(tok::greater) || First[1]->isNot(tok::greater))
    return false;

  // Only merge if there currently is no whitespace between the first two ">".
  if (First[1]->hasWhitespaceBefore())
    return false;

  auto Tok = Tokens.size() > 2 ? First[-1] : nullptr;
  if (Tok && Tok->isNot(tok::kw_operator))
    return false;

  First[0]->Tok.setKind(tok::greatergreater);
  First[0]->TokenText = ">>";
  First[0]->ColumnWidth += 1;
  Tokens.erase(Tokens.end() - 1);
  return true;
}

bool FormatTokenLexer::tryMergeTokens(ArrayRef<tok::TokenKind> Kinds,
                                      TokenType NewType) {
  if (Tokens.size() < Kinds.size())
    return false;

  SmallVectorImpl<FormatToken *>::const_iterator First =
      Tokens.end() - Kinds.size();
  for (unsigned i = 0; i < Kinds.size(); ++i)
    if (First[i]->isNot(Kinds[i]))
      return false;

  return tryMergeTokens(Kinds.size(), NewType);
}

bool FormatTokenLexer::tryMergeTokens(size_t Count, TokenType NewType) {
  if (Tokens.size() < Count)
    return false;

  SmallVectorImpl<FormatToken *>::const_iterator First = Tokens.end() - Count;
  unsigned AddLength = 0;
  for (size_t i = 1; i < Count; ++i) {
    // If there is whitespace separating the token and the previous one,
    // they should not be merged.
    if (First[i]->hasWhitespaceBefore())
      return false;
    AddLength += First[i]->TokenText.size();
  }

  Tokens.resize(Tokens.size() - Count + 1);
  First[0]->TokenText = StringRef(First[0]->TokenText.data(),
                                  First[0]->TokenText.size() + AddLength);
  First[0]->ColumnWidth += AddLength;
  First[0]->setType(NewType);
  return true;
}

bool FormatTokenLexer::tryMergeTokensAny(
    ArrayRef<ArrayRef<tok::TokenKind>> Kinds, TokenType NewType) {
  return llvm::any_of(Kinds, [this, NewType](ArrayRef<tok::TokenKind> Kinds) {
    return tryMergeTokens(Kinds, NewType);
  });
}

// Returns \c true if \p Tok can only be followed by an operand in JavaScript.
bool FormatTokenLexer::precedesOperand(FormatToken *Tok) {
  // NB: This is not entirely correct, as an r_paren can introduce an operand
  // location in e.g. `if (foo) /bar/.exec(...);`. That is a rare enough
  // corner case to not matter in practice, though.
  return Tok->isOneOf(tok::period, tok::l_paren, tok::comma, tok::l_brace,
                      tok::r_brace, tok::l_square, tok::semi, tok::exclaim,
                      tok::colon, tok::question, tok::tilde) ||
         Tok->isOneOf(tok::kw_return, tok::kw_do, tok::kw_case, tok::kw_throw,
                      tok::kw_else, tok::kw_new, tok::kw_delete, tok::kw_void,
                      tok::kw_typeof, Keywords.kw_instanceof, Keywords.kw_in) ||
         Tok->isBinaryOperator();
}

bool FormatTokenLexer::canPrecedeRegexLiteral(FormatToken *Prev) {
  if (!Prev)
    return true;

  // Regex literals can only follow after prefix unary operators, not after
  // postfix unary operators. If the '++' is followed by a non-operand
  // introducing token, the slash here is the operand and not the start of a
  // regex.
  // `!` is an unary prefix operator, but also a post-fix operator that casts
  // away nullability, so the same check applies.
  if (Prev->isOneOf(tok::plusplus, tok::minusminus, tok::exclaim))
    return Tokens.size() < 3 || precedesOperand(Tokens[Tokens.size() - 3]);

  // The previous token must introduce an operand location where regex
  // literals can occur.
  if (!precedesOperand(Prev))
    return false;

  return true;
}

// Tries to parse a JavaScript Regex literal starting at the current token,
// if that begins with a slash and is in a location where JavaScript allows
// regex literals. Changes the current token to a regex literal and updates
// its text if successful.
void FormatTokenLexer::tryParseJSRegexLiteral() {
  FormatToken *RegexToken = Tokens.back();
  if (!RegexToken->isOneOf(tok::slash, tok::slashequal))
    return;

  FormatToken *Prev = nullptr;
  for (FormatToken *FT : llvm::drop_begin(llvm::reverse(Tokens))) {
    // NB: Because previous pointers are not initialized yet, this cannot use
    // Token.getPreviousNonComment.
    if (FT->isNot(tok::comment)) {
      Prev = FT;
      break;
    }
  }

  if (!canPrecedeRegexLiteral(Prev))
    return;

  // 'Manually' lex ahead in the current file buffer.
  const char *Offset = Lex->getBufferLocation();
  const char *RegexBegin = Offset - RegexToken->TokenText.size();
  StringRef Buffer = Lex->getBuffer();
  bool InCharacterClass = false;
  bool HaveClosingSlash = false;
  for (; !HaveClosingSlash && Offset != Buffer.end(); ++Offset) {
    // Regular expressions are terminated with a '/', which can only be
    // escaped using '\' or a character class between '[' and ']'.
    // See http://www.ecma-international.org/ecma-262/5.1/#sec-7.8.5.
    switch (*Offset) {
    case '\\':
      // Skip the escaped character.
      ++Offset;
      break;
    case '[':
      InCharacterClass = true;
      break;
    case ']':
      InCharacterClass = false;
      break;
    case '/':
      if (!InCharacterClass)
        HaveClosingSlash = true;
      break;
    }
  }

  RegexToken->setType(TT_RegexLiteral);
  // Treat regex literals like other string_literals.
  RegexToken->Tok.setKind(tok::string_literal);
  RegexToken->TokenText = StringRef(RegexBegin, Offset - RegexBegin);
  RegexToken->ColumnWidth = RegexToken->TokenText.size();

  resetLexer(SourceMgr.getFileOffset(Lex->getSourceLocation(Offset)));
}

static auto lexCSharpString(const char *Begin, const char *End, bool Verbatim,
                            bool Interpolated) {
  auto Repeated = [&Begin, End]() {
    return Begin + 1 < End && Begin[1] == Begin[0];
  };

  // Look for a terminating '"' in the current file buffer.
  // Make no effort to format code within an interpolated or verbatim string.
  //
  // Interpolated strings could contain { } with " characters inside.
  // $"{x ?? "null"}"
  // should not be split into $"{x ?? ", null, "}" but should be treated as a
  // single string-literal.
  //
  // We opt not to try and format expressions inside {} within a C#
  // interpolated string. Formatting expressions within an interpolated string
  // would require similar work as that done for JavaScript template strings
  // in `handleTemplateStrings()`.
  for (int UnmatchedOpeningBraceCount = 0; Begin < End; ++Begin) {
    switch (*Begin) {
    case '\\':
      if (!Verbatim)
        ++Begin;
      break;
    case '{':
      if (Interpolated) {
        // {{ inside an interpolated string is escaped, so skip it.
        if (Repeated())
          ++Begin;
        else
          ++UnmatchedOpeningBraceCount;
      }
      break;
    case '}':
      if (Interpolated) {
        // }} inside an interpolated string is escaped, so skip it.
        if (Repeated())
          ++Begin;
        else if (UnmatchedOpeningBraceCount > 0)
          --UnmatchedOpeningBraceCount;
        else
          return End;
      }
      break;
    case '"':
      if (UnmatchedOpeningBraceCount > 0)
        break;
      // "" within a verbatim string is an escaped double quote: skip it.
      if (Verbatim && Repeated()) {
        ++Begin;
        break;
      }
      return Begin;
    }
  }

  return End;
}

void FormatTokenLexer::handleCSharpVerbatimAndInterpolatedStrings() {
  FormatToken *CSharpStringLiteral = Tokens.back();

  if (CSharpStringLiteral->isNot(TT_CSharpStringLiteral))
    return;

  auto &TokenText = CSharpStringLiteral->TokenText;

  bool Verbatim = false;
  bool Interpolated = false;
  if (TokenText.starts_with(R"($@")") || TokenText.starts_with(R"(@$")")) {
    Verbatim = true;
    Interpolated = true;
  } else if (TokenText.starts_with(R"(@")")) {
    Verbatim = true;
  } else if (TokenText.starts_with(R"($")")) {
    Interpolated = true;
  }

  // Deal with multiline strings.
  if (!Verbatim && !Interpolated)
    return;

  const char *StrBegin = Lex->getBufferLocation() - TokenText.size();
  const char *Offset = StrBegin;
  if (Verbatim && Interpolated)
    Offset += 3;
  else
    Offset += 2;

  const auto End = Lex->getBuffer().end();
  Offset = lexCSharpString(Offset, End, Verbatim, Interpolated);

  // Make no attempt to format code properly if a verbatim string is
  // unterminated.
  if (Offset >= End)
    return;

  StringRef LiteralText(StrBegin, Offset - StrBegin + 1);
  TokenText = LiteralText;

  // Adjust width for potentially multiline string literals.
  size_t FirstBreak = LiteralText.find('\n');
  StringRef FirstLineText = FirstBreak == StringRef::npos
                                ? LiteralText
                                : LiteralText.substr(0, FirstBreak);
  CSharpStringLiteral->ColumnWidth = encoding::columnWidthWithTabs(
      FirstLineText, CSharpStringLiteral->OriginalColumn, Style.TabWidth,
      Encoding);
  size_t LastBreak = LiteralText.rfind('\n');
  if (LastBreak != StringRef::npos) {
    CSharpStringLiteral->IsMultiline = true;
    unsigned StartColumn = 0;
    CSharpStringLiteral->LastLineColumnWidth =
        encoding::columnWidthWithTabs(LiteralText.substr(LastBreak + 1),
                                      StartColumn, Style.TabWidth, Encoding);
  }

  assert(Offset < End);
  resetLexer(SourceMgr.getFileOffset(Lex->getSourceLocation(Offset + 1)));
}

void FormatTokenLexer::handleTableGenMultilineString() {
  FormatToken *MultiLineString = Tokens.back();
  if (MultiLineString->isNot(TT_TableGenMultiLineString))
    return;

  auto OpenOffset = Lex->getCurrentBufferOffset() - 2 /* "[{" */;
  // "}]" is the end of multi line string.
  auto CloseOffset = Lex->getBuffer().find("}]", OpenOffset);
  if (CloseOffset == StringRef::npos)
    return;
  auto Text = Lex->getBuffer().substr(OpenOffset, CloseOffset - OpenOffset + 2);
  MultiLineString->TokenText = Text;
  resetLexer(SourceMgr.getFileOffset(
      Lex->getSourceLocation(Lex->getBufferLocation() - 2 + Text.size())));
  auto FirstLineText = Text;
  auto FirstBreak = Text.find('\n');
  // Set ColumnWidth and LastLineColumnWidth when it has multiple lines.
  if (FirstBreak != StringRef::npos) {
    MultiLineString->IsMultiline = true;
    FirstLineText = Text.substr(0, FirstBreak + 1);
    // LastLineColumnWidth holds the width of the last line.
    auto LastBreak = Text.rfind('\n');
    MultiLineString->LastLineColumnWidth = encoding::columnWidthWithTabs(
        Text.substr(LastBreak + 1), MultiLineString->OriginalColumn,
        Style.TabWidth, Encoding);
  }
  // ColumnWidth holds only the width of the first line.
  MultiLineString->ColumnWidth = encoding::columnWidthWithTabs(
      FirstLineText, MultiLineString->OriginalColumn, Style.TabWidth, Encoding);
}

void FormatTokenLexer::handleTableGenNumericLikeIdentifier() {
  FormatToken *Tok = Tokens.back();
  // TableGen identifiers can begin with digits. Such tokens are lexed as
  // numeric_constant now.
  if (Tok->isNot(tok::numeric_constant))
    return;
  StringRef Text = Tok->TokenText;
  // The following check is based on llvm::TGLexer::LexToken.
  // That lexes the token as a number if any of the following holds:
  // 1. It starts with '+', '-'.
  // 2. All the characters are digits.
  // 3. The first non-digit character is 'b', and the next is '0' or '1'.
  // 4. The first non-digit character is 'x', and the next is a hex digit.
  // Note that in the case 3 and 4, if the next character does not exists in
  // this token, the token is an identifier.
  if (Text.size() < 1 || Text[0] == '+' || Text[0] == '-')
    return;
  const auto NonDigitPos = Text.find_if([](char C) { return !isdigit(C); });
  // All the characters are digits
  if (NonDigitPos == StringRef::npos)
    return;
  char FirstNonDigit = Text[NonDigitPos];
  if (NonDigitPos < Text.size() - 1) {
    char TheNext = Text[NonDigitPos + 1];
    // Regarded as a binary number.
    if (FirstNonDigit == 'b' && (TheNext == '0' || TheNext == '1'))
      return;
    // Regarded as hex number.
    if (FirstNonDigit == 'x' && isxdigit(TheNext))
      return;
  }
  if (isalpha(FirstNonDigit) || FirstNonDigit == '_') {
    // This is actually an identifier in TableGen.
    Tok->Tok.setKind(tok::identifier);
    Tok->Tok.setIdentifierInfo(nullptr);
  }
}

void FormatTokenLexer::handleTemplateStrings() {
  FormatToken *BacktickToken = Tokens.back();

  if (BacktickToken->is(tok::l_brace)) {
    StateStack.push(LexerState::NORMAL);
    return;
  }
  if (BacktickToken->is(tok::r_brace)) {
    if (StateStack.size() == 1)
      return;
    StateStack.pop();
    if (StateStack.top() != LexerState::TEMPLATE_STRING)
      return;
    // If back in TEMPLATE_STRING, fallthrough and continue parsing the
  } else if (BacktickToken->is(tok::unknown) &&
             BacktickToken->TokenText == "`") {
    StateStack.push(LexerState::TEMPLATE_STRING);
  } else {
    return; // Not actually a template
  }

  // 'Manually' lex ahead in the current file buffer.
  const char *Offset = Lex->getBufferLocation();
  const char *TmplBegin = Offset - BacktickToken->TokenText.size(); // at "`"
  for (; Offset != Lex->getBuffer().end(); ++Offset) {
    if (Offset[0] == '`') {
      StateStack.pop();
      ++Offset;
      break;
    }
    if (Offset[0] == '\\') {
      ++Offset; // Skip the escaped character.
    } else if (Offset + 1 < Lex->getBuffer().end() && Offset[0] == '$' &&
               Offset[1] == '{') {
      // '${' introduces an expression interpolation in the template string.
      StateStack.push(LexerState::NORMAL);
      Offset += 2;
      break;
    }
  }

  StringRef LiteralText(TmplBegin, Offset - TmplBegin);
  BacktickToken->setType(TT_TemplateString);
  BacktickToken->Tok.setKind(tok::string_literal);
  BacktickToken->TokenText = LiteralText;

  // Adjust width for potentially multiline string literals.
  size_t FirstBreak = LiteralText.find('\n');
  StringRef FirstLineText = FirstBreak == StringRef::npos
                                ? LiteralText
                                : LiteralText.substr(0, FirstBreak);
  BacktickToken->ColumnWidth = encoding::columnWidthWithTabs(
      FirstLineText, BacktickToken->OriginalColumn, Style.TabWidth, Encoding);
  size_t LastBreak = LiteralText.rfind('\n');
  if (LastBreak != StringRef::npos) {
    BacktickToken->IsMultiline = true;
    unsigned StartColumn = 0; // The template tail spans the entire line.
    BacktickToken->LastLineColumnWidth =
        encoding::columnWidthWithTabs(LiteralText.substr(LastBreak + 1),
                                      StartColumn, Style.TabWidth, Encoding);
  }

  SourceLocation loc = Lex->getSourceLocation(Offset);
  resetLexer(SourceMgr.getFileOffset(loc));
}

void FormatTokenLexer::tryParsePythonComment() {
  FormatToken *HashToken = Tokens.back();
  if (!HashToken->isOneOf(tok::hash, tok::hashhash))
    return;
  // Turn the remainder of this line into a comment.
  const char *CommentBegin =
      Lex->getBufferLocation() - HashToken->TokenText.size(); // at "#"
  size_t From = CommentBegin - Lex->getBuffer().begin();
  size_t To = Lex->getBuffer().find_first_of('\n', From);
  if (To == StringRef::npos)
    To = Lex->getBuffer().size();
  size_t Len = To - From;
  HashToken->setType(TT_LineComment);
  HashToken->Tok.setKind(tok::comment);
  HashToken->TokenText = Lex->getBuffer().substr(From, Len);
  SourceLocation Loc = To < Lex->getBuffer().size()
                           ? Lex->getSourceLocation(CommentBegin + Len)
                           : SourceMgr.getLocForEndOfFile(ID);
  resetLexer(SourceMgr.getFileOffset(Loc));
}

bool FormatTokenLexer::tryMerge_TMacro() {
  if (Tokens.size() < 4)
    return false;
  FormatToken *Last = Tokens.back();
  if (Last->isNot(tok::r_paren))
    return false;

  FormatToken *String = Tokens[Tokens.size() - 2];
  if (String->isNot(tok::string_literal) || String->IsMultiline)
    return false;

  if (Tokens[Tokens.size() - 3]->isNot(tok::l_paren))
    return false;

  FormatToken *Macro = Tokens[Tokens.size() - 4];
  if (Macro->TokenText != "_T")
    return false;

  const char *Start = Macro->TokenText.data();
  const char *End = Last->TokenText.data() + Last->TokenText.size();
  String->TokenText = StringRef(Start, End - Start);
  String->IsFirst = Macro->IsFirst;
  String->LastNewlineOffset = Macro->LastNewlineOffset;
  String->WhitespaceRange = Macro->WhitespaceRange;
  String->OriginalColumn = Macro->OriginalColumn;
  String->ColumnWidth = encoding::columnWidthWithTabs(
      String->TokenText, String->OriginalColumn, Style.TabWidth, Encoding);
  String->NewlinesBefore = Macro->NewlinesBefore;
  String->HasUnescapedNewline = Macro->HasUnescapedNewline;

  Tokens.pop_back();
  Tokens.pop_back();
  Tokens.pop_back();
  Tokens.back() = String;
  if (FirstInLineIndex >= Tokens.size())
    FirstInLineIndex = Tokens.size() - 1;
  return true;
}

bool FormatTokenLexer::tryMergeConflictMarkers() {
  if (Tokens.back()->NewlinesBefore == 0 && Tokens.back()->isNot(tok::eof))
    return false;

  // Conflict lines look like:
  // <marker> <text from the vcs>
  // For example:
  // >>>>>>> /file/in/file/system at revision 1234
  //
  // We merge all tokens in a line that starts with a conflict marker
  // into a single token with a special token type that the unwrapped line
  // parser will use to correctly rebuild the underlying code.

  FileID ID;
  // Get the position of the first token in the line.
  unsigned FirstInLineOffset;
  std::tie(ID, FirstInLineOffset) = SourceMgr.getDecomposedLoc(
      Tokens[FirstInLineIndex]->getStartOfNonWhitespace());
  StringRef Buffer = SourceMgr.getBufferOrFake(ID).getBuffer();
  // Calculate the offset of the start of the current line.
  auto LineOffset = Buffer.rfind('\n', FirstInLineOffset);
  if (LineOffset == StringRef::npos)
    LineOffset = 0;
  else
    ++LineOffset;

  auto FirstSpace = Buffer.find_first_of(" \n", LineOffset);
  StringRef LineStart;
  if (FirstSpace == StringRef::npos)
    LineStart = Buffer.substr(LineOffset);
  else
    LineStart = Buffer.substr(LineOffset, FirstSpace - LineOffset);

  TokenType Type = TT_Unknown;
  if (LineStart == "<<<<<<<" || LineStart == ">>>>") {
    Type = TT_ConflictStart;
  } else if (LineStart == "|||||||" || LineStart == "=======" ||
             LineStart == "====") {
    Type = TT_ConflictAlternative;
  } else if (LineStart == ">>>>>>>" || LineStart == "<<<<") {
    Type = TT_ConflictEnd;
  }

  if (Type != TT_Unknown) {
    FormatToken *Next = Tokens.back();

    Tokens.resize(FirstInLineIndex + 1);
    // We do not need to build a complete token here, as we will skip it
    // during parsing anyway (as we must not touch whitespace around conflict
    // markers).
    Tokens.back()->setType(Type);
    Tokens.back()->Tok.setKind(tok::kw___unknown_anytype);

    Tokens.push_back(Next);
    return true;
  }

  return false;
}

FormatToken *FormatTokenLexer::getStashedToken() {
  // Create a synthesized second '>' or '<' token.
  Token Tok = FormatTok->Tok;
  StringRef TokenText = FormatTok->TokenText;

  unsigned OriginalColumn = FormatTok->OriginalColumn;
  FormatTok = new (Allocator.Allocate()) FormatToken;
  FormatTok->Tok = Tok;
  SourceLocation TokLocation =
      FormatTok->Tok.getLocation().getLocWithOffset(Tok.getLength() - 1);
  FormatTok->Tok.setLocation(TokLocation);
  FormatTok->WhitespaceRange = SourceRange(TokLocation, TokLocation);
  FormatTok->TokenText = TokenText;
  FormatTok->ColumnWidth = 1;
  FormatTok->OriginalColumn = OriginalColumn + 1;

  return FormatTok;
}

/// Truncate the current token to the new length and make the lexer continue
/// from the end of the truncated token. Used for other languages that have
/// different token boundaries, like JavaScript in which a comment ends at a
/// line break regardless of whether the line break follows a backslash. Also
/// used to set the lexer to the end of whitespace if the lexer regards
/// whitespace and an unrecognized symbol as one token.
void FormatTokenLexer::truncateToken(size_t NewLen) {
  assert(NewLen <= FormatTok->TokenText.size());
  resetLexer(SourceMgr.getFileOffset(Lex->getSourceLocation(
      Lex->getBufferLocation() - FormatTok->TokenText.size() + NewLen)));
  FormatTok->TokenText = FormatTok->TokenText.substr(0, NewLen);
  FormatTok->ColumnWidth = encoding::columnWidthWithTabs(
      FormatTok->TokenText, FormatTok->OriginalColumn, Style.TabWidth,
      Encoding);
  FormatTok->Tok.setLength(NewLen);
}

/// Count the length of leading whitespace in a token.
static size_t countLeadingWhitespace(StringRef Text) {
  // Basically counting the length matched by this regex.
  // "^([\n\r\f\v \t]|(\\\\|\\?\\?/)[\n\r])+"
  // Directly using the regex turned out to be slow. With the regex
  // version formatting all files in this directory took about 1.25
  // seconds. This version took about 0.5 seconds.
  const unsigned char *const Begin = Text.bytes_begin();
  const unsigned char *const End = Text.bytes_end();
  const unsigned char *Cur = Begin;
  while (Cur < End) {
    if (isspace(Cur[0])) {
      ++Cur;
    } else if (Cur[0] == '\\' && (Cur[1] == '\n' || Cur[1] == '\r')) {
      // A '\' followed by a newline always escapes the newline, regardless
      // of whether there is another '\' before it.
      // The source has a null byte at the end. So the end of the entire input
      // isn't reached yet. Also the lexer doesn't break apart an escaped
      // newline.
      assert(End - Cur >= 2);
      Cur += 2;
    } else if (Cur[0] == '?' && Cur[1] == '?' && Cur[2] == '/' &&
               (Cur[3] == '\n' || Cur[3] == '\r')) {
      // Newlines can also be escaped by a '?' '?' '/' trigraph. By the way, the
      // characters are quoted individually in this comment because if we write
      // them together some compilers warn that we have a trigraph in the code.
      assert(End - Cur >= 4);
      Cur += 4;
    } else {
      break;
    }
  }
  return Cur - Begin;
}

FormatToken *FormatTokenLexer::getNextToken() {
  if (StateStack.top() == LexerState::TOKEN_STASHED) {
    StateStack.pop();
    return getStashedToken();
  }

  FormatTok = new (Allocator.Allocate()) FormatToken;
  readRawToken(*FormatTok);
  SourceLocation WhitespaceStart =
      FormatTok->Tok.getLocation().getLocWithOffset(-TrailingWhitespace);
  FormatTok->IsFirst = IsFirstToken;
  IsFirstToken = false;

  // Consume and record whitespace until we find a significant token.
  // Some tok::unknown tokens are not just whitespace, e.g. whitespace
  // followed by a symbol such as backtick. Those symbols may be
  // significant in other languages.
  unsigned WhitespaceLength = TrailingWhitespace;
  while (FormatTok->isNot(tok::eof)) {
    auto LeadingWhitespace = countLeadingWhitespace(FormatTok->TokenText);
    if (LeadingWhitespace == 0)
      break;
    if (LeadingWhitespace < FormatTok->TokenText.size())
      truncateToken(LeadingWhitespace);
    StringRef Text = FormatTok->TokenText;
    bool InEscape = false;
    for (int i = 0, e = Text.size(); i != e; ++i) {
      switch (Text[i]) {
      case '\r':
        // If this is a CRLF sequence, break here and the LF will be handled on
        // the next loop iteration. Otherwise, this is a single Mac CR, treat it
        // the same as a single LF.
        if (i + 1 < e && Text[i + 1] == '\n')
          break;
        [[fallthrough]];
      case '\n':
        ++FormatTok->NewlinesBefore;
        if (!InEscape)
          FormatTok->HasUnescapedNewline = true;
        else
          InEscape = false;
        FormatTok->LastNewlineOffset = WhitespaceLength + i + 1;
        Column = 0;
        break;
      case '\f':
      case '\v':
        Column = 0;
        break;
      case ' ':
        ++Column;
        break;
      case '\t':
        Column +=
            Style.TabWidth - (Style.TabWidth ? Column % Style.TabWidth : 0);
        break;
      case '\\':
      case '?':
      case '/':
        // The text was entirely whitespace when this loop was entered. Thus
        // this has to be an escape sequence.
        assert(Text.substr(i, 2) == "\\\r" || Text.substr(i, 2) == "\\\n" ||
               Text.substr(i, 4) == "\?\?/\r" ||
               Text.substr(i, 4) == "\?\?/\n" ||
               (i >= 1 && (Text.substr(i - 1, 4) == "\?\?/\r" ||
                           Text.substr(i - 1, 4) == "\?\?/\n")) ||
               (i >= 2 && (Text.substr(i - 2, 4) == "\?\?/\r" ||
                           Text.substr(i - 2, 4) == "\?\?/\n")));
        InEscape = true;
        break;
      default:
        // This shouldn't happen.
        assert(false);
        break;
      }
    }
    WhitespaceLength += Text.size();
    readRawToken(*FormatTok);
  }

  if (FormatTok->is(tok::unknown))
    FormatTok->setType(TT_ImplicitStringLiteral);

  // JavaScript and Java do not allow to escape the end of the line with a
  // backslash. Backslashes are syntax errors in plain source, but can occur in
  // comments. When a single line comment ends with a \, it'll cause the next
  // line of code to be lexed as a comment, breaking formatting. The code below
  // finds comments that contain a backslash followed by a line break, truncates
  // the comment token at the backslash, and resets the lexer to restart behind
  // the backslash.
  if ((Style.isJavaScript() || Style.Language == FormatStyle::LK_Java) &&
      FormatTok->is(tok::comment) && FormatTok->TokenText.starts_with("//")) {
    size_t BackslashPos = FormatTok->TokenText.find('\\');
    while (BackslashPos != StringRef::npos) {
      if (BackslashPos + 1 < FormatTok->TokenText.size() &&
          FormatTok->TokenText[BackslashPos + 1] == '\n') {
        truncateToken(BackslashPos + 1);
        break;
      }
      BackslashPos = FormatTok->TokenText.find('\\', BackslashPos + 1);
    }
  }

  if (Style.isVerilog()) {
    static const llvm::Regex NumberBase("^s?[bdho]", llvm::Regex::IgnoreCase);
    SmallVector<StringRef, 1> Matches;
    // Verilog uses the backtick instead of the hash for preprocessor stuff.
    // And it uses the hash for delays and parameter lists. In order to continue
    // using `tok::hash` in other places, the backtick gets marked as the hash
    // here.  And in order to tell the backtick and hash apart for
    // Verilog-specific stuff, the hash becomes an identifier.
    if (FormatTok->is(tok::numeric_constant)) {
      // In Verilog the quote is not part of a number.
      auto Quote = FormatTok->TokenText.find('\'');
      if (Quote != StringRef::npos)
        truncateToken(Quote);
    } else if (FormatTok->isOneOf(tok::hash, tok::hashhash)) {
      FormatTok->Tok.setKind(tok::raw_identifier);
    } else if (FormatTok->is(tok::raw_identifier)) {
      if (FormatTok->TokenText == "`") {
        FormatTok->Tok.setIdentifierInfo(nullptr);
        FormatTok->Tok.setKind(tok::hash);
      } else if (FormatTok->TokenText == "``") {
        FormatTok->Tok.setIdentifierInfo(nullptr);
        FormatTok->Tok.setKind(tok::hashhash);
      } else if (Tokens.size() > 0 &&
                 Tokens.back()->is(Keywords.kw_apostrophe) &&
                 NumberBase.match(FormatTok->TokenText, &Matches)) {
        // In Verilog in a based number literal like `'b10`, there may be
        // whitespace between `'b` and `10`. Therefore we handle the base and
        // the rest of the number literal as two tokens. But if there is no
        // space in the input code, we need to manually separate the two parts.
        truncateToken(Matches[0].size());
        FormatTok->setFinalizedType(TT_VerilogNumberBase);
      }
    }
  }

  FormatTok->WhitespaceRange = SourceRange(
      WhitespaceStart, WhitespaceStart.getLocWithOffset(WhitespaceLength));

  FormatTok->OriginalColumn = Column;

  TrailingWhitespace = 0;
  if (FormatTok->is(tok::comment)) {
    // FIXME: Add the trimmed whitespace to Column.
    StringRef UntrimmedText = FormatTok->TokenText;
    FormatTok->TokenText = FormatTok->TokenText.rtrim(" \t\v\f");
    TrailingWhitespace = UntrimmedText.size() - FormatTok->TokenText.size();
  } else if (FormatTok->is(tok::raw_identifier)) {
    IdentifierInfo &Info = IdentTable.get(FormatTok->TokenText);
    FormatTok->Tok.setIdentifierInfo(&Info);
    FormatTok->Tok.setKind(Info.getTokenID());
    if (Style.Language == FormatStyle::LK_Java &&
        FormatTok->isOneOf(tok::kw_struct, tok::kw_union, tok::kw_delete,
                           tok::kw_operator)) {
      FormatTok->Tok.setKind(tok::identifier);
      FormatTok->Tok.setIdentifierInfo(nullptr);
    } else if (Style.isJavaScript() &&
               FormatTok->isOneOf(tok::kw_struct, tok::kw_union,
                                  tok::kw_operator)) {
      FormatTok->Tok.setKind(tok::identifier);
      FormatTok->Tok.setIdentifierInfo(nullptr);
    } else if (Style.isTableGen() && !Keywords.isTableGenKeyword(*FormatTok)) {
      FormatTok->Tok.setKind(tok::identifier);
      FormatTok->Tok.setIdentifierInfo(nullptr);
    }
  } else if (FormatTok->is(tok::greatergreater)) {
    FormatTok->Tok.setKind(tok::greater);
    FormatTok->TokenText = FormatTok->TokenText.substr(0, 1);
    ++Column;
    StateStack.push(LexerState::TOKEN_STASHED);
  } else if (FormatTok->is(tok::lessless)) {
    FormatTok->Tok.setKind(tok::less);
    FormatTok->TokenText = FormatTok->TokenText.substr(0, 1);
    ++Column;
    StateStack.push(LexerState::TOKEN_STASHED);
  }

  if (Style.isVerilog() && Tokens.size() > 0 &&
      Tokens.back()->is(TT_VerilogNumberBase) &&
      FormatTok->Tok.isOneOf(tok::identifier, tok::question)) {
    // Mark the number following a base like `'h?a0` as a number.
    FormatTok->Tok.setKind(tok::numeric_constant);
  }

  // Now FormatTok is the next non-whitespace token.

  StringRef Text = FormatTok->TokenText;
  size_t FirstNewlinePos = Text.find('\n');
  if (FirstNewlinePos == StringRef::npos) {
    // FIXME: ColumnWidth actually depends on the start column, we need to
    // take this into account when the token is moved.
    FormatTok->ColumnWidth =
        encoding::columnWidthWithTabs(Text, Column, Style.TabWidth, Encoding);
    Column += FormatTok->ColumnWidth;
  } else {
    FormatTok->IsMultiline = true;
    // FIXME: ColumnWidth actually depends on the start column, we need to
    // take this into account when the token is moved.
    FormatTok->ColumnWidth = encoding::columnWidthWithTabs(
        Text.substr(0, FirstNewlinePos), Column, Style.TabWidth, Encoding);

    // The last line of the token always starts in column 0.
    // Thus, the length can be precomputed even in the presence of tabs.
    FormatTok->LastLineColumnWidth = encoding::columnWidthWithTabs(
        Text.substr(Text.find_last_of('\n') + 1), 0, Style.TabWidth, Encoding);
    Column = FormatTok->LastLineColumnWidth;
  }

  if (Style.isCpp()) {
    auto *Identifier = FormatTok->Tok.getIdentifierInfo();
    auto it = Macros.find(Identifier);
    if (!(Tokens.size() > 0 && Tokens.back()->Tok.getIdentifierInfo() &&
          Tokens.back()->Tok.getIdentifierInfo()->getPPKeywordID() ==
              tok::pp_define) &&
        it != Macros.end()) {
      FormatTok->setType(it->second);
      if (it->second == TT_IfMacro) {
        // The lexer token currently has type tok::kw_unknown. However, for this
        // substitution to be treated correctly in the TokenAnnotator, faking
        // the tok value seems to be needed. Not sure if there's a more elegant
        // way.
        FormatTok->Tok.setKind(tok::kw_if);
      }
    } else if (FormatTok->is(tok::identifier)) {
      if (MacroBlockBeginRegex.match(Text))
        FormatTok->setType(TT_MacroBlockBegin);
      else if (MacroBlockEndRegex.match(Text))
        FormatTok->setType(TT_MacroBlockEnd);
      else if (TypeNames.contains(Identifier))
        FormatTok->setFinalizedType(TT_TypeName);
    }
  }

  return FormatTok;
}

bool FormatTokenLexer::readRawTokenVerilogSpecific(Token &Tok) {
  // In Verilog the quote is not a character literal.
  //
  // Make the backtick and double backtick identifiers to match against them
  // more easily.
  //
  // In Verilog an escaped identifier starts with backslash and ends with
  // whitespace. Unless that whitespace is an escaped newline. A backslash can
  // also begin an escaped newline outside of an escaped identifier. We check
  // for that outside of the Regex since we can't use negative lookhead
  // assertions. Simply changing the '*' to '+' breaks stuff as the escaped
  // identifier may have a length of 0 according to Section A.9.3.
  // FIXME: If there is an escaped newline in the middle of an escaped
  // identifier, allow for pasting the two lines together, But escaped
  // identifiers usually occur only in generated code anyway.
  static const llvm::Regex VerilogToken(R"re(^('|``?|\\(\\)re"
                                        "(\r?\n|\r)|[^[:space:]])*)");

  SmallVector<StringRef, 4> Matches;
  const char *Start = Lex->getBufferLocation();
  if (!VerilogToken.match(StringRef(Start, Lex->getBuffer().end() - Start),
                          &Matches)) {
    return false;
  }
  // There is a null byte at the end of the buffer, so we don't have to check
  // Start[1] is within the buffer.
  if (Start[0] == '\\' && (Start[1] == '\r' || Start[1] == '\n'))
    return false;
  size_t Len = Matches[0].size();

  // The kind has to be an identifier so we can match it against those defined
  // in Keywords. The kind has to be set before the length because the setLength
  // function checks that the kind is not an annotation.
  Tok.setKind(tok::raw_identifier);
  Tok.setLength(Len);
  Tok.setLocation(Lex->getSourceLocation(Start, Len));
  Tok.setRawIdentifierData(Start);
  Lex->seek(Lex->getCurrentBufferOffset() + Len, /*IsAtStartofline=*/false);
  return true;
}

void FormatTokenLexer::readRawToken(FormatToken &Tok) {
  // For Verilog, first see if there is a special token, and fall back to the
  // normal lexer if there isn't one.
  if (!Style.isVerilog() || !readRawTokenVerilogSpecific(Tok.Tok))
    Lex->LexFromRawLexer(Tok.Tok);
  Tok.TokenText = StringRef(SourceMgr.getCharacterData(Tok.Tok.getLocation()),
                            Tok.Tok.getLength());
  // For formatting, treat unterminated string literals like normal string
  // literals.
  if (Tok.is(tok::unknown)) {
    if (Tok.TokenText.starts_with("\"")) {
      Tok.Tok.setKind(tok::string_literal);
      Tok.IsUnterminatedLiteral = true;
    } else if (Style.isJavaScript() && Tok.TokenText == "''") {
      Tok.Tok.setKind(tok::string_literal);
    }
  }

  if ((Style.isJavaScript() || Style.isProto()) && Tok.is(tok::char_constant))
    Tok.Tok.setKind(tok::string_literal);

  if (Tok.is(tok::comment) && isClangFormatOn(Tok.TokenText))
    FormattingDisabled = false;

  Tok.Finalized = FormattingDisabled;

  if (Tok.is(tok::comment) && isClangFormatOff(Tok.TokenText))
    FormattingDisabled = true;
}

void FormatTokenLexer::resetLexer(unsigned Offset) {
  StringRef Buffer = SourceMgr.getBufferData(ID);
  Lex.reset(new Lexer(SourceMgr.getLocForStartOfFile(ID), LangOpts,
                      Buffer.begin(), Buffer.begin() + Offset, Buffer.end()));
  Lex->SetKeepWhitespaceMode(true);
  TrailingWhitespace = 0;
}

} // namespace format
} // namespace clang
