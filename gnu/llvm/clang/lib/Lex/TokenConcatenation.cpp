//===--- TokenConcatenation.cpp - Token Concatenation Avoidance -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the TokenConcatenation class.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/TokenConcatenation.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/Support/ErrorHandling.h"
using namespace clang;


/// IsStringPrefix - Return true if Str is a string prefix.
/// 'L', 'u', 'U', or 'u8'. Including raw versions.
static bool IsStringPrefix(StringRef Str, bool CPlusPlus11) {

  if (Str[0] == 'L' ||
      (CPlusPlus11 && (Str[0] == 'u' || Str[0] == 'U' || Str[0] == 'R'))) {

    if (Str.size() == 1)
      return true; // "L", "u", "U", and "R"

    // Check for raw flavors. Need to make sure the first character wasn't
    // already R. Need CPlusPlus11 check for "LR".
    if (Str[1] == 'R' && Str[0] != 'R' && Str.size() == 2 && CPlusPlus11)
      return true; // "LR", "uR", "UR"

    // Check for "u8" and "u8R"
    if (Str[0] == 'u' && Str[1] == '8') {
      if (Str.size() == 2) return true; // "u8"
      if (Str.size() == 3 && Str[2] == 'R') return true; // "u8R"
    }
  }

  return false;
}

/// IsIdentifierStringPrefix - Return true if the spelling of the token
/// is literally 'L', 'u', 'U', or 'u8'. Including raw versions.
bool TokenConcatenation::IsIdentifierStringPrefix(const Token &Tok) const {
  const LangOptions &LangOpts = PP.getLangOpts();

  if (!Tok.needsCleaning()) {
    if (Tok.getLength() < 1 || Tok.getLength() > 3)
      return false;
    SourceManager &SM = PP.getSourceManager();
    const char *Ptr = SM.getCharacterData(SM.getSpellingLoc(Tok.getLocation()));
    return IsStringPrefix(StringRef(Ptr, Tok.getLength()),
                          LangOpts.CPlusPlus11);
  }

  if (Tok.getLength() < 256) {
    char Buffer[256];
    const char *TokPtr = Buffer;
    unsigned length = PP.getSpelling(Tok, TokPtr);
    return IsStringPrefix(StringRef(TokPtr, length), LangOpts.CPlusPlus11);
  }

  return IsStringPrefix(StringRef(PP.getSpelling(Tok)), LangOpts.CPlusPlus11);
}

TokenConcatenation::TokenConcatenation(const Preprocessor &pp) : PP(pp) {
  memset(TokenInfo, 0, sizeof(TokenInfo));

  // These tokens have custom code in AvoidConcat.
  TokenInfo[tok::identifier      ] |= aci_custom;
  TokenInfo[tok::numeric_constant] |= aci_custom_firstchar;
  TokenInfo[tok::period          ] |= aci_custom_firstchar;
  TokenInfo[tok::amp             ] |= aci_custom_firstchar;
  TokenInfo[tok::plus            ] |= aci_custom_firstchar;
  TokenInfo[tok::minus           ] |= aci_custom_firstchar;
  TokenInfo[tok::slash           ] |= aci_custom_firstchar;
  TokenInfo[tok::less            ] |= aci_custom_firstchar;
  TokenInfo[tok::greater         ] |= aci_custom_firstchar;
  TokenInfo[tok::pipe            ] |= aci_custom_firstchar;
  TokenInfo[tok::percent         ] |= aci_custom_firstchar;
  TokenInfo[tok::colon           ] |= aci_custom_firstchar;
  TokenInfo[tok::hash            ] |= aci_custom_firstchar;
  TokenInfo[tok::arrow           ] |= aci_custom_firstchar;

  // These tokens have custom code in C++11 mode.
  if (PP.getLangOpts().CPlusPlus11) {
    TokenInfo[tok::string_literal      ] |= aci_custom;
    TokenInfo[tok::wide_string_literal ] |= aci_custom;
    TokenInfo[tok::utf8_string_literal ] |= aci_custom;
    TokenInfo[tok::utf16_string_literal] |= aci_custom;
    TokenInfo[tok::utf32_string_literal] |= aci_custom;
    TokenInfo[tok::char_constant       ] |= aci_custom;
    TokenInfo[tok::wide_char_constant  ] |= aci_custom;
    TokenInfo[tok::utf16_char_constant ] |= aci_custom;
    TokenInfo[tok::utf32_char_constant ] |= aci_custom;
  }

  // These tokens have custom code in C++17 mode.
  if (PP.getLangOpts().CPlusPlus17)
    TokenInfo[tok::utf8_char_constant] |= aci_custom;

  // These tokens have custom code in C++2a mode.
  if (PP.getLangOpts().CPlusPlus20)
    TokenInfo[tok::lessequal ] |= aci_custom_firstchar;

  // These tokens change behavior if followed by an '='.
  TokenInfo[tok::amp         ] |= aci_avoid_equal;           // &=
  TokenInfo[tok::plus        ] |= aci_avoid_equal;           // +=
  TokenInfo[tok::minus       ] |= aci_avoid_equal;           // -=
  TokenInfo[tok::slash       ] |= aci_avoid_equal;           // /=
  TokenInfo[tok::less        ] |= aci_avoid_equal;           // <=
  TokenInfo[tok::greater     ] |= aci_avoid_equal;           // >=
  TokenInfo[tok::pipe        ] |= aci_avoid_equal;           // |=
  TokenInfo[tok::percent     ] |= aci_avoid_equal;           // %=
  TokenInfo[tok::star        ] |= aci_avoid_equal;           // *=
  TokenInfo[tok::exclaim     ] |= aci_avoid_equal;           // !=
  TokenInfo[tok::lessless    ] |= aci_avoid_equal;           // <<=
  TokenInfo[tok::greatergreater] |= aci_avoid_equal;         // >>=
  TokenInfo[tok::caret       ] |= aci_avoid_equal;           // ^=
  TokenInfo[tok::equal       ] |= aci_avoid_equal;           // ==
}

/// GetFirstChar - Get the first character of the token \arg Tok,
/// avoiding calls to getSpelling where possible.
static char GetFirstChar(const Preprocessor &PP, const Token &Tok) {
  if (IdentifierInfo *II = Tok.getIdentifierInfo()) {
    // Avoid spelling identifiers, the most common form of token.
    return II->getNameStart()[0];
  } else if (!Tok.needsCleaning()) {
    if (Tok.isLiteral() && Tok.getLiteralData()) {
      return *Tok.getLiteralData();
    } else {
      SourceManager &SM = PP.getSourceManager();
      return *SM.getCharacterData(SM.getSpellingLoc(Tok.getLocation()));
    }
  } else if (Tok.getLength() < 256) {
    char Buffer[256];
    const char *TokPtr = Buffer;
    PP.getSpelling(Tok, TokPtr);
    return TokPtr[0];
  } else {
    return PP.getSpelling(Tok)[0];
  }
}

/// AvoidConcat - If printing PrevTok immediately followed by Tok would cause
/// the two individual tokens to be lexed as a single token, return true
/// (which causes a space to be printed between them).  This allows the output
/// of -E mode to be lexed to the same token stream as lexing the input
/// directly would.
///
/// This code must conservatively return true if it doesn't want to be 100%
/// accurate.  This will cause the output to include extra space characters,
/// but the resulting output won't have incorrect concatenations going on.
/// Examples include "..", which we print with a space between, because we
/// don't want to track enough to tell "x.." from "...".
bool TokenConcatenation::AvoidConcat(const Token &PrevPrevTok,
                                     const Token &PrevTok,
                                     const Token &Tok) const {
  // Conservatively assume that every annotation token that has a printable
  // form requires whitespace.
  if (PrevTok.isAnnotation())
    return true;

  // First, check to see if the tokens were directly adjacent in the original
  // source.  If they were, it must be okay to stick them together: if there
  // were an issue, the tokens would have been lexed differently.
  SourceManager &SM = PP.getSourceManager();
  SourceLocation PrevSpellLoc = SM.getSpellingLoc(PrevTok.getLocation());
  SourceLocation SpellLoc = SM.getSpellingLoc(Tok.getLocation());
  if (PrevSpellLoc.getLocWithOffset(PrevTok.getLength()) == SpellLoc)
    return false;

  tok::TokenKind PrevKind = PrevTok.getKind();
  if (!PrevTok.isAnnotation() && PrevTok.getIdentifierInfo())
    PrevKind = tok::identifier; // Language keyword or named operator.

  // Look up information on when we should avoid concatenation with prevtok.
  unsigned ConcatInfo = TokenInfo[PrevKind];

  // If prevtok never causes a problem for anything after it, return quickly.
  if (ConcatInfo == 0) return false;

  if (ConcatInfo & aci_avoid_equal) {
    // If the next token is '=' or '==', avoid concatenation.
    if (Tok.isOneOf(tok::equal, tok::equalequal))
      return true;
    ConcatInfo &= ~aci_avoid_equal;
  }
  if (Tok.isAnnotation()) {
    // Modules annotation can show up when generated automatically for includes.
    assert(Tok.isOneOf(tok::annot_module_include, tok::annot_module_begin,
                       tok::annot_module_end, tok::annot_embed) &&
           "unexpected annotation in AvoidConcat");

    ConcatInfo = 0;
    if (Tok.is(tok::annot_embed))
      return true;
  }

  if (ConcatInfo == 0)
    return false;

  // Basic algorithm: we look at the first character of the second token, and
  // determine whether it, if appended to the first token, would form (or
  // would contribute) to a larger token if concatenated.
  char FirstChar = 0;
  if (ConcatInfo & aci_custom) {
    // If the token does not need to know the first character, don't get it.
  } else {
    FirstChar = GetFirstChar(PP, Tok);
  }

  switch (PrevKind) {
  default:
    llvm_unreachable("InitAvoidConcatTokenInfo built wrong");

  case tok::raw_identifier:
    llvm_unreachable("tok::raw_identifier in non-raw lexing mode!");

  case tok::string_literal:
  case tok::wide_string_literal:
  case tok::utf8_string_literal:
  case tok::utf16_string_literal:
  case tok::utf32_string_literal:
  case tok::char_constant:
  case tok::wide_char_constant:
  case tok::utf8_char_constant:
  case tok::utf16_char_constant:
  case tok::utf32_char_constant:
    if (!PP.getLangOpts().CPlusPlus11)
      return false;

    // In C++11, a string or character literal followed by an identifier is a
    // single token.
    if (Tok.getIdentifierInfo())
      return true;

    // A ud-suffix is an identifier. If the previous token ends with one, treat
    // it as an identifier.
    if (!PrevTok.hasUDSuffix())
      return false;
    [[fallthrough]];
  case tok::identifier:   // id+id or id+number or id+L"foo".
    // id+'.'... will not append.
    if (Tok.is(tok::numeric_constant))
      return GetFirstChar(PP, Tok) != '.';

    if (Tok.getIdentifierInfo() ||
        Tok.isOneOf(tok::wide_string_literal, tok::utf8_string_literal,
                    tok::utf16_string_literal, tok::utf32_string_literal,
                    tok::wide_char_constant, tok::utf8_char_constant,
                    tok::utf16_char_constant, tok::utf32_char_constant))
      return true;

    // If this isn't identifier + string, we're done.
    if (Tok.isNot(tok::char_constant) && Tok.isNot(tok::string_literal))
      return false;

    // Otherwise, this is a narrow character or string.  If the *identifier*
    // is a literal 'L', 'u8', 'u' or 'U', avoid pasting L "foo" -> L"foo".
    return IsIdentifierStringPrefix(PrevTok);

  case tok::numeric_constant:
    return isPreprocessingNumberBody(FirstChar) ||
           FirstChar == '+' || FirstChar == '-';
  case tok::period:          // ..., .*, .1234
    return (FirstChar == '.' && PrevPrevTok.is(tok::period)) ||
           isDigit(FirstChar) ||
           (PP.getLangOpts().CPlusPlus && FirstChar == '*');
  case tok::amp:             // &&
    return FirstChar == '&';
  case tok::plus:            // ++
    return FirstChar == '+';
  case tok::minus:           // --, ->, ->*
    return FirstChar == '-' || FirstChar == '>';
  case tok::slash:           //, /*, //
    return FirstChar == '*' || FirstChar == '/';
  case tok::less:            // <<, <<=, <:, <%
    return FirstChar == '<' || FirstChar == ':' || FirstChar == '%';
  case tok::greater:         // >>, >>=
    return FirstChar == '>';
  case tok::pipe:            // ||
    return FirstChar == '|';
  case tok::percent:         // %>, %:
    return FirstChar == '>' || FirstChar == ':';
  case tok::colon:           // ::, :>
    return FirstChar == '>' ||
    (PP.getLangOpts().CPlusPlus && FirstChar == ':');
  case tok::hash:            // ##, #@, %:%:
    return FirstChar == '#' || FirstChar == '@' || FirstChar == '%';
  case tok::arrow:           // ->*
    return PP.getLangOpts().CPlusPlus && FirstChar == '*';
  case tok::lessequal:       // <=> (C++2a)
    return PP.getLangOpts().CPlusPlus20 && FirstChar == '>';
  }
}
