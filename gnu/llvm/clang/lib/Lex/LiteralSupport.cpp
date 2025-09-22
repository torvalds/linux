//===--- LiteralSupport.cpp - Code to parse and process literals ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the NumericLiteralParser, CharLiteralParser, and
// StringLiteralParser interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/LiteralSupport.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Unicode.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

using namespace clang;

static unsigned getCharWidth(tok::TokenKind kind, const TargetInfo &Target) {
  switch (kind) {
  default: llvm_unreachable("Unknown token type!");
  case tok::char_constant:
  case tok::string_literal:
  case tok::utf8_char_constant:
  case tok::utf8_string_literal:
    return Target.getCharWidth();
  case tok::wide_char_constant:
  case tok::wide_string_literal:
    return Target.getWCharWidth();
  case tok::utf16_char_constant:
  case tok::utf16_string_literal:
    return Target.getChar16Width();
  case tok::utf32_char_constant:
  case tok::utf32_string_literal:
    return Target.getChar32Width();
  }
}

static unsigned getEncodingPrefixLen(tok::TokenKind kind) {
  switch (kind) {
  default:
    llvm_unreachable("Unknown token type!");
  case tok::char_constant:
  case tok::string_literal:
    return 0;
  case tok::utf8_char_constant:
  case tok::utf8_string_literal:
    return 2;
  case tok::wide_char_constant:
  case tok::wide_string_literal:
  case tok::utf16_char_constant:
  case tok::utf16_string_literal:
  case tok::utf32_char_constant:
  case tok::utf32_string_literal:
    return 1;
  }
}

static CharSourceRange MakeCharSourceRange(const LangOptions &Features,
                                           FullSourceLoc TokLoc,
                                           const char *TokBegin,
                                           const char *TokRangeBegin,
                                           const char *TokRangeEnd) {
  SourceLocation Begin =
    Lexer::AdvanceToTokenCharacter(TokLoc, TokRangeBegin - TokBegin,
                                   TokLoc.getManager(), Features);
  SourceLocation End =
    Lexer::AdvanceToTokenCharacter(Begin, TokRangeEnd - TokRangeBegin,
                                   TokLoc.getManager(), Features);
  return CharSourceRange::getCharRange(Begin, End);
}

/// Produce a diagnostic highlighting some portion of a literal.
///
/// Emits the diagnostic \p DiagID, highlighting the range of characters from
/// \p TokRangeBegin (inclusive) to \p TokRangeEnd (exclusive), which must be
/// a substring of a spelling buffer for the token beginning at \p TokBegin.
static DiagnosticBuilder Diag(DiagnosticsEngine *Diags,
                              const LangOptions &Features, FullSourceLoc TokLoc,
                              const char *TokBegin, const char *TokRangeBegin,
                              const char *TokRangeEnd, unsigned DiagID) {
  SourceLocation Begin =
    Lexer::AdvanceToTokenCharacter(TokLoc, TokRangeBegin - TokBegin,
                                   TokLoc.getManager(), Features);
  return Diags->Report(Begin, DiagID) <<
    MakeCharSourceRange(Features, TokLoc, TokBegin, TokRangeBegin, TokRangeEnd);
}

static bool IsEscapeValidInUnevaluatedStringLiteral(char Escape) {
  switch (Escape) {
  case '\'':
  case '"':
  case '?':
  case '\\':
  case 'a':
  case 'b':
  case 'f':
  case 'n':
  case 'r':
  case 't':
  case 'v':
    return true;
  }
  return false;
}

/// ProcessCharEscape - Parse a standard C escape sequence, which can occur in
/// either a character or a string literal.
static unsigned ProcessCharEscape(const char *ThisTokBegin,
                                  const char *&ThisTokBuf,
                                  const char *ThisTokEnd, bool &HadError,
                                  FullSourceLoc Loc, unsigned CharWidth,
                                  DiagnosticsEngine *Diags,
                                  const LangOptions &Features,
                                  StringLiteralEvalMethod EvalMethod) {
  const char *EscapeBegin = ThisTokBuf;
  bool Delimited = false;
  bool EndDelimiterFound = false;

  // Skip the '\' char.
  ++ThisTokBuf;

  // We know that this character can't be off the end of the buffer, because
  // that would have been \", which would not have been the end of string.
  unsigned ResultChar = *ThisTokBuf++;
  char Escape = ResultChar;
  switch (ResultChar) {
  // These map to themselves.
  case '\\': case '\'': case '"': case '?': break;

    // These have fixed mappings.
  case 'a':
    // TODO: K&R: the meaning of '\\a' is different in traditional C
    ResultChar = 7;
    break;
  case 'b':
    ResultChar = 8;
    break;
  case 'e':
    if (Diags)
      Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
           diag::ext_nonstandard_escape) << "e";
    ResultChar = 27;
    break;
  case 'E':
    if (Diags)
      Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
           diag::ext_nonstandard_escape) << "E";
    ResultChar = 27;
    break;
  case 'f':
    ResultChar = 12;
    break;
  case 'n':
    ResultChar = 10;
    break;
  case 'r':
    ResultChar = 13;
    break;
  case 't':
    ResultChar = 9;
    break;
  case 'v':
    ResultChar = 11;
    break;
  case 'x': { // Hex escape.
    ResultChar = 0;
    if (ThisTokBuf != ThisTokEnd && *ThisTokBuf == '{') {
      Delimited = true;
      ThisTokBuf++;
      if (*ThisTokBuf == '}') {
        Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
             diag::err_delimited_escape_empty);
        return ResultChar;
      }
    } else if (ThisTokBuf == ThisTokEnd || !isHexDigit(*ThisTokBuf)) {
      if (Diags)
        Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
             diag::err_hex_escape_no_digits) << "x";
      return ResultChar;
    }

    // Hex escapes are a maximal series of hex digits.
    bool Overflow = false;
    for (; ThisTokBuf != ThisTokEnd; ++ThisTokBuf) {
      if (Delimited && *ThisTokBuf == '}') {
        ThisTokBuf++;
        EndDelimiterFound = true;
        break;
      }
      int CharVal = llvm::hexDigitValue(*ThisTokBuf);
      if (CharVal == -1) {
        // Non delimited hex escape sequences stop at the first non-hex digit.
        if (!Delimited)
          break;
        HadError = true;
        if (Diags)
          Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
               diag::err_delimited_escape_invalid)
              << StringRef(ThisTokBuf, 1);
        continue;
      }
      // About to shift out a digit?
      if (ResultChar & 0xF0000000)
        Overflow = true;
      ResultChar <<= 4;
      ResultChar |= CharVal;
    }
    // See if any bits will be truncated when evaluated as a character.
    if (CharWidth != 32 && (ResultChar >> CharWidth) != 0) {
      Overflow = true;
      ResultChar &= ~0U >> (32-CharWidth);
    }

    // Check for overflow.
    if (!HadError && Overflow) { // Too many digits to fit in
      HadError = true;
      if (Diags)
        Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
             diag::err_escape_too_large)
            << 0;
    }
    break;
  }
  case '0': case '1': case '2': case '3':
  case '4': case '5': case '6': case '7': {
    // Octal escapes.
    --ThisTokBuf;
    ResultChar = 0;

    // Octal escapes are a series of octal digits with maximum length 3.
    // "\0123" is a two digit sequence equal to "\012" "3".
    unsigned NumDigits = 0;
    do {
      ResultChar <<= 3;
      ResultChar |= *ThisTokBuf++ - '0';
      ++NumDigits;
    } while (ThisTokBuf != ThisTokEnd && NumDigits < 3 &&
             ThisTokBuf[0] >= '0' && ThisTokBuf[0] <= '7');

    // Check for overflow.  Reject '\777', but not L'\777'.
    if (CharWidth != 32 && (ResultChar >> CharWidth) != 0) {
      if (Diags)
        Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
             diag::err_escape_too_large) << 1;
      ResultChar &= ~0U >> (32-CharWidth);
    }
    break;
  }
  case 'o': {
    bool Overflow = false;
    if (ThisTokBuf == ThisTokEnd || *ThisTokBuf != '{') {
      HadError = true;
      if (Diags)
        Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
             diag::err_delimited_escape_missing_brace)
            << "o";

      break;
    }
    ResultChar = 0;
    Delimited = true;
    ++ThisTokBuf;
    if (*ThisTokBuf == '}') {
      Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
           diag::err_delimited_escape_empty);
      return ResultChar;
    }

    while (ThisTokBuf != ThisTokEnd) {
      if (*ThisTokBuf == '}') {
        EndDelimiterFound = true;
        ThisTokBuf++;
        break;
      }
      if (*ThisTokBuf < '0' || *ThisTokBuf > '7') {
        HadError = true;
        if (Diags)
          Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
               diag::err_delimited_escape_invalid)
              << StringRef(ThisTokBuf, 1);
        ThisTokBuf++;
        continue;
      }
      // Check if one of the top three bits is set before shifting them out.
      if (ResultChar & 0xE0000000)
        Overflow = true;

      ResultChar <<= 3;
      ResultChar |= *ThisTokBuf++ - '0';
    }
    // Check for overflow.  Reject '\777', but not L'\777'.
    if (!HadError &&
        (Overflow || (CharWidth != 32 && (ResultChar >> CharWidth) != 0))) {
      HadError = true;
      if (Diags)
        Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
             diag::err_escape_too_large)
            << 1;
      ResultChar &= ~0U >> (32 - CharWidth);
    }
    break;
  }
    // Otherwise, these are not valid escapes.
  case '(': case '{': case '[': case '%':
    // GCC accepts these as extensions.  We warn about them as such though.
    if (Diags)
      Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
           diag::ext_nonstandard_escape)
        << std::string(1, ResultChar);
    break;
  default:
    if (!Diags)
      break;

    if (isPrintable(ResultChar))
      Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
           diag::ext_unknown_escape)
        << std::string(1, ResultChar);
    else
      Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
           diag::ext_unknown_escape)
        << "x" + llvm::utohexstr(ResultChar);
    break;
  }

  if (Delimited && Diags) {
    if (!EndDelimiterFound)
      Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
           diag::err_expected)
          << tok::r_brace;
    else if (!HadError) {
      Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
           Features.CPlusPlus23 ? diag::warn_cxx23_delimited_escape_sequence
                                : diag::ext_delimited_escape_sequence)
          << /*delimited*/ 0 << (Features.CPlusPlus ? 1 : 0);
    }
  }

  if (EvalMethod == StringLiteralEvalMethod::Unevaluated &&
      !IsEscapeValidInUnevaluatedStringLiteral(Escape)) {
    Diag(Diags, Features, Loc, ThisTokBegin, EscapeBegin, ThisTokBuf,
         diag::err_unevaluated_string_invalid_escape_sequence)
        << StringRef(EscapeBegin, ThisTokBuf - EscapeBegin);
    HadError = true;
  }

  return ResultChar;
}

static void appendCodePoint(unsigned Codepoint,
                            llvm::SmallVectorImpl<char> &Str) {
  char ResultBuf[4];
  char *ResultPtr = ResultBuf;
  if (llvm::ConvertCodePointToUTF8(Codepoint, ResultPtr))
    Str.append(ResultBuf, ResultPtr);
}

void clang::expandUCNs(SmallVectorImpl<char> &Buf, StringRef Input) {
  for (StringRef::iterator I = Input.begin(), E = Input.end(); I != E; ++I) {
    if (*I != '\\') {
      Buf.push_back(*I);
      continue;
    }

    ++I;
    char Kind = *I;
    ++I;

    assert(Kind == 'u' || Kind == 'U' || Kind == 'N');
    uint32_t CodePoint = 0;

    if (Kind == 'u' && *I == '{') {
      for (++I; *I != '}'; ++I) {
        unsigned Value = llvm::hexDigitValue(*I);
        assert(Value != -1U);
        CodePoint <<= 4;
        CodePoint += Value;
      }
      appendCodePoint(CodePoint, Buf);
      continue;
    }

    if (Kind == 'N') {
      assert(*I == '{');
      ++I;
      auto Delim = std::find(I, Input.end(), '}');
      assert(Delim != Input.end());
      StringRef Name(I, std::distance(I, Delim));
      std::optional<llvm::sys::unicode::LooseMatchingResult> Res =
          llvm::sys::unicode::nameToCodepointLooseMatching(Name);
      assert(Res && "could not find a codepoint that was previously found");
      CodePoint = Res->CodePoint;
      assert(CodePoint != 0xFFFFFFFF);
      appendCodePoint(CodePoint, Buf);
      I = Delim;
      continue;
    }

    unsigned NumHexDigits;
    if (Kind == 'u')
      NumHexDigits = 4;
    else
      NumHexDigits = 8;

    assert(I + NumHexDigits <= E);

    for (; NumHexDigits != 0; ++I, --NumHexDigits) {
      unsigned Value = llvm::hexDigitValue(*I);
      assert(Value != -1U);

      CodePoint <<= 4;
      CodePoint += Value;
    }

    appendCodePoint(CodePoint, Buf);
    --I;
  }
}

bool clang::isFunctionLocalStringLiteralMacro(tok::TokenKind K,
                                              const LangOptions &LO) {
  return LO.MicrosoftExt &&
         (K == tok::kw___FUNCTION__ || K == tok::kw_L__FUNCTION__ ||
          K == tok::kw___FUNCSIG__ || K == tok::kw_L__FUNCSIG__ ||
          K == tok::kw___FUNCDNAME__);
}

bool clang::tokenIsLikeStringLiteral(const Token &Tok, const LangOptions &LO) {
  return tok::isStringLiteral(Tok.getKind()) ||
         isFunctionLocalStringLiteralMacro(Tok.getKind(), LO);
}

static bool ProcessNumericUCNEscape(const char *ThisTokBegin,
                                    const char *&ThisTokBuf,
                                    const char *ThisTokEnd, uint32_t &UcnVal,
                                    unsigned short &UcnLen, bool &Delimited,
                                    FullSourceLoc Loc, DiagnosticsEngine *Diags,
                                    const LangOptions &Features,
                                    bool in_char_string_literal = false) {
  const char *UcnBegin = ThisTokBuf;
  bool HasError = false;
  bool EndDelimiterFound = false;

  // Skip the '\u' char's.
  ThisTokBuf += 2;
  Delimited = false;
  if (UcnBegin[1] == 'u' && in_char_string_literal &&
      ThisTokBuf != ThisTokEnd && *ThisTokBuf == '{') {
    Delimited = true;
    ThisTokBuf++;
  } else if (ThisTokBuf == ThisTokEnd || !isHexDigit(*ThisTokBuf)) {
    if (Diags)
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           diag::err_hex_escape_no_digits)
          << StringRef(&ThisTokBuf[-1], 1);
    return false;
  }
  UcnLen = (ThisTokBuf[-1] == 'u' ? 4 : 8);

  bool Overflow = false;
  unsigned short Count = 0;
  for (; ThisTokBuf != ThisTokEnd && (Delimited || Count != UcnLen);
       ++ThisTokBuf) {
    if (Delimited && *ThisTokBuf == '}') {
      ++ThisTokBuf;
      EndDelimiterFound = true;
      break;
    }
    int CharVal = llvm::hexDigitValue(*ThisTokBuf);
    if (CharVal == -1) {
      HasError = true;
      if (!Delimited)
        break;
      if (Diags) {
        Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
             diag::err_delimited_escape_invalid)
            << StringRef(ThisTokBuf, 1);
      }
      Count++;
      continue;
    }
    if (UcnVal & 0xF0000000) {
      Overflow = true;
      continue;
    }
    UcnVal <<= 4;
    UcnVal |= CharVal;
    Count++;
  }

  if (Overflow) {
    if (Diags)
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           diag::err_escape_too_large)
          << 0;
    return false;
  }

  if (Delimited && !EndDelimiterFound) {
    if (Diags) {
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           diag::err_expected)
          << tok::r_brace;
    }
    return false;
  }

  // If we didn't consume the proper number of digits, there is a problem.
  if (Count == 0 || (!Delimited && Count != UcnLen)) {
    if (Diags)
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           Delimited ? diag::err_delimited_escape_empty
                     : diag::err_ucn_escape_incomplete);
    return false;
  }
  return !HasError;
}

static void DiagnoseInvalidUnicodeCharacterName(
    DiagnosticsEngine *Diags, const LangOptions &Features, FullSourceLoc Loc,
    const char *TokBegin, const char *TokRangeBegin, const char *TokRangeEnd,
    llvm::StringRef Name) {

  Diag(Diags, Features, Loc, TokBegin, TokRangeBegin, TokRangeEnd,
       diag::err_invalid_ucn_name)
      << Name;

  namespace u = llvm::sys::unicode;

  std::optional<u::LooseMatchingResult> Res =
      u::nameToCodepointLooseMatching(Name);
  if (Res) {
    Diag(Diags, Features, Loc, TokBegin, TokRangeBegin, TokRangeEnd,
         diag::note_invalid_ucn_name_loose_matching)
        << FixItHint::CreateReplacement(
               MakeCharSourceRange(Features, Loc, TokBegin, TokRangeBegin,
                                   TokRangeEnd),
               Res->Name);
    return;
  }

  unsigned Distance = 0;
  SmallVector<u::MatchForCodepointName> Matches =
      u::nearestMatchesForCodepointName(Name, 5);
  assert(!Matches.empty() && "No unicode characters found");

  for (const auto &Match : Matches) {
    if (Distance == 0)
      Distance = Match.Distance;
    if (std::max(Distance, Match.Distance) -
            std::min(Distance, Match.Distance) >
        3)
      break;
    Distance = Match.Distance;

    std::string Str;
    llvm::UTF32 V = Match.Value;
    bool Converted =
        llvm::convertUTF32ToUTF8String(llvm::ArrayRef<llvm::UTF32>(&V, 1), Str);
    (void)Converted;
    assert(Converted && "Found a match wich is not a unicode character");

    Diag(Diags, Features, Loc, TokBegin, TokRangeBegin, TokRangeEnd,
         diag::note_invalid_ucn_name_candidate)
        << Match.Name << llvm::utohexstr(Match.Value)
        << Str // FIXME: Fix the rendering of non printable characters
        << FixItHint::CreateReplacement(
               MakeCharSourceRange(Features, Loc, TokBegin, TokRangeBegin,
                                   TokRangeEnd),
               Match.Name);
  }
}

static bool ProcessNamedUCNEscape(const char *ThisTokBegin,
                                  const char *&ThisTokBuf,
                                  const char *ThisTokEnd, uint32_t &UcnVal,
                                  unsigned short &UcnLen, FullSourceLoc Loc,
                                  DiagnosticsEngine *Diags,
                                  const LangOptions &Features) {
  const char *UcnBegin = ThisTokBuf;
  assert(UcnBegin[0] == '\\' && UcnBegin[1] == 'N');
  ThisTokBuf += 2;
  if (ThisTokBuf == ThisTokEnd || *ThisTokBuf != '{') {
    if (Diags) {
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           diag::err_delimited_escape_missing_brace)
          << StringRef(&ThisTokBuf[-1], 1);
    }
    return false;
  }
  ThisTokBuf++;
  const char *ClosingBrace = std::find_if(ThisTokBuf, ThisTokEnd, [](char C) {
    return C == '}' || isVerticalWhitespace(C);
  });
  bool Incomplete = ClosingBrace == ThisTokEnd;
  bool Empty = ClosingBrace == ThisTokBuf;
  if (Incomplete || Empty) {
    if (Diags) {
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           Incomplete ? diag::err_ucn_escape_incomplete
                      : diag::err_delimited_escape_empty)
          << StringRef(&UcnBegin[1], 1);
    }
    ThisTokBuf = ClosingBrace == ThisTokEnd ? ClosingBrace : ClosingBrace + 1;
    return false;
  }
  StringRef Name(ThisTokBuf, ClosingBrace - ThisTokBuf);
  ThisTokBuf = ClosingBrace + 1;
  std::optional<char32_t> Res = llvm::sys::unicode::nameToCodepointStrict(Name);
  if (!Res) {
    if (Diags)
      DiagnoseInvalidUnicodeCharacterName(Diags, Features, Loc, ThisTokBegin,
                                          &UcnBegin[3], ClosingBrace, Name);
    return false;
  }
  UcnVal = *Res;
  UcnLen = UcnVal > 0xFFFF ? 8 : 4;
  return true;
}

/// ProcessUCNEscape - Read the Universal Character Name, check constraints and
/// return the UTF32.
static bool ProcessUCNEscape(const char *ThisTokBegin, const char *&ThisTokBuf,
                             const char *ThisTokEnd, uint32_t &UcnVal,
                             unsigned short &UcnLen, FullSourceLoc Loc,
                             DiagnosticsEngine *Diags,
                             const LangOptions &Features,
                             bool in_char_string_literal = false) {

  bool HasError;
  const char *UcnBegin = ThisTokBuf;
  bool IsDelimitedEscapeSequence = false;
  bool IsNamedEscapeSequence = false;
  if (ThisTokBuf[1] == 'N') {
    IsNamedEscapeSequence = true;
    HasError = !ProcessNamedUCNEscape(ThisTokBegin, ThisTokBuf, ThisTokEnd,
                                      UcnVal, UcnLen, Loc, Diags, Features);
  } else {
    HasError =
        !ProcessNumericUCNEscape(ThisTokBegin, ThisTokBuf, ThisTokEnd, UcnVal,
                                 UcnLen, IsDelimitedEscapeSequence, Loc, Diags,
                                 Features, in_char_string_literal);
  }
  if (HasError)
    return false;

  // Check UCN constraints (C99 6.4.3p2) [C++11 lex.charset p2]
  if ((0xD800 <= UcnVal && UcnVal <= 0xDFFF) || // surrogate codepoints
      UcnVal > 0x10FFFF) {                      // maximum legal UTF32 value
    if (Diags)
      Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
           diag::err_ucn_escape_invalid);
    return false;
  }

  // C23 and C++11 allow UCNs that refer to control characters
  // and basic source characters inside character and string literals
  if (UcnVal < 0xa0 &&
      // $, @, ` are allowed in all language modes
      (UcnVal != 0x24 && UcnVal != 0x40 && UcnVal != 0x60)) {
    bool IsError =
        (!(Features.CPlusPlus11 || Features.C23) || !in_char_string_literal);
    if (Diags) {
      char BasicSCSChar = UcnVal;
      if (UcnVal >= 0x20 && UcnVal < 0x7f)
        Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
             IsError ? diag::err_ucn_escape_basic_scs
             : Features.CPlusPlus
                 ? diag::warn_cxx98_compat_literal_ucn_escape_basic_scs
                 : diag::warn_c23_compat_literal_ucn_escape_basic_scs)
            << StringRef(&BasicSCSChar, 1);
      else
        Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
             IsError ? diag::err_ucn_control_character
             : Features.CPlusPlus
                 ? diag::warn_cxx98_compat_literal_ucn_control_character
                 : diag::warn_c23_compat_literal_ucn_control_character);
    }
    if (IsError)
      return false;
  }

  if (!Features.CPlusPlus && !Features.C99 && Diags)
    Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
         diag::warn_ucn_not_valid_in_c89_literal);

  if ((IsDelimitedEscapeSequence || IsNamedEscapeSequence) && Diags)
    Diag(Diags, Features, Loc, ThisTokBegin, UcnBegin, ThisTokBuf,
         Features.CPlusPlus23 ? diag::warn_cxx23_delimited_escape_sequence
                              : diag::ext_delimited_escape_sequence)
        << (IsNamedEscapeSequence ? 1 : 0) << (Features.CPlusPlus ? 1 : 0);

  return true;
}

/// MeasureUCNEscape - Determine the number of bytes within the resulting string
/// which this UCN will occupy.
static int MeasureUCNEscape(const char *ThisTokBegin, const char *&ThisTokBuf,
                            const char *ThisTokEnd, unsigned CharByteWidth,
                            const LangOptions &Features, bool &HadError) {
  // UTF-32: 4 bytes per escape.
  if (CharByteWidth == 4)
    return 4;

  uint32_t UcnVal = 0;
  unsigned short UcnLen = 0;
  FullSourceLoc Loc;

  if (!ProcessUCNEscape(ThisTokBegin, ThisTokBuf, ThisTokEnd, UcnVal,
                        UcnLen, Loc, nullptr, Features, true)) {
    HadError = true;
    return 0;
  }

  // UTF-16: 2 bytes for BMP, 4 bytes otherwise.
  if (CharByteWidth == 2)
    return UcnVal <= 0xFFFF ? 2 : 4;

  // UTF-8.
  if (UcnVal < 0x80)
    return 1;
  if (UcnVal < 0x800)
    return 2;
  if (UcnVal < 0x10000)
    return 3;
  return 4;
}

/// EncodeUCNEscape - Read the Universal Character Name, check constraints and
/// convert the UTF32 to UTF8 or UTF16. This is a subroutine of
/// StringLiteralParser. When we decide to implement UCN's for identifiers,
/// we will likely rework our support for UCN's.
static void EncodeUCNEscape(const char *ThisTokBegin, const char *&ThisTokBuf,
                            const char *ThisTokEnd,
                            char *&ResultBuf, bool &HadError,
                            FullSourceLoc Loc, unsigned CharByteWidth,
                            DiagnosticsEngine *Diags,
                            const LangOptions &Features) {
  typedef uint32_t UTF32;
  UTF32 UcnVal = 0;
  unsigned short UcnLen = 0;
  if (!ProcessUCNEscape(ThisTokBegin, ThisTokBuf, ThisTokEnd, UcnVal, UcnLen,
                        Loc, Diags, Features, true)) {
    HadError = true;
    return;
  }

  assert((CharByteWidth == 1 || CharByteWidth == 2 || CharByteWidth == 4) &&
         "only character widths of 1, 2, or 4 bytes supported");

  (void)UcnLen;
  assert((UcnLen== 4 || UcnLen== 8) && "only ucn length of 4 or 8 supported");

  if (CharByteWidth == 4) {
    // FIXME: Make the type of the result buffer correct instead of
    // using reinterpret_cast.
    llvm::UTF32 *ResultPtr = reinterpret_cast<llvm::UTF32*>(ResultBuf);
    *ResultPtr = UcnVal;
    ResultBuf += 4;
    return;
  }

  if (CharByteWidth == 2) {
    // FIXME: Make the type of the result buffer correct instead of
    // using reinterpret_cast.
    llvm::UTF16 *ResultPtr = reinterpret_cast<llvm::UTF16*>(ResultBuf);

    if (UcnVal <= (UTF32)0xFFFF) {
      *ResultPtr = UcnVal;
      ResultBuf += 2;
      return;
    }

    // Convert to UTF16.
    UcnVal -= 0x10000;
    *ResultPtr     = 0xD800 + (UcnVal >> 10);
    *(ResultPtr+1) = 0xDC00 + (UcnVal & 0x3FF);
    ResultBuf += 4;
    return;
  }

  assert(CharByteWidth == 1 && "UTF-8 encoding is only for 1 byte characters");

  // Now that we've parsed/checked the UCN, we convert from UTF32->UTF8.
  // The conversion below was inspired by:
  //   http://www.unicode.org/Public/PROGRAMS/CVTUTF/ConvertUTF.c
  // First, we determine how many bytes the result will require.
  typedef uint8_t UTF8;

  unsigned short bytesToWrite = 0;
  if (UcnVal < (UTF32)0x80)
    bytesToWrite = 1;
  else if (UcnVal < (UTF32)0x800)
    bytesToWrite = 2;
  else if (UcnVal < (UTF32)0x10000)
    bytesToWrite = 3;
  else
    bytesToWrite = 4;

  const unsigned byteMask = 0xBF;
  const unsigned byteMark = 0x80;

  // Once the bits are split out into bytes of UTF8, this is a mask OR-ed
  // into the first byte, depending on how many bytes follow.
  static const UTF8 firstByteMark[5] = {
    0x00, 0x00, 0xC0, 0xE0, 0xF0
  };
  // Finally, we write the bytes into ResultBuf.
  ResultBuf += bytesToWrite;
  switch (bytesToWrite) { // note: everything falls through.
  case 4:
    *--ResultBuf = (UTF8)((UcnVal | byteMark) & byteMask); UcnVal >>= 6;
    [[fallthrough]];
  case 3:
    *--ResultBuf = (UTF8)((UcnVal | byteMark) & byteMask); UcnVal >>= 6;
    [[fallthrough]];
  case 2:
    *--ResultBuf = (UTF8)((UcnVal | byteMark) & byteMask); UcnVal >>= 6;
    [[fallthrough]];
  case 1:
    *--ResultBuf = (UTF8) (UcnVal | firstByteMark[bytesToWrite]);
  }
  // Update the buffer.
  ResultBuf += bytesToWrite;
}

///       integer-constant: [C99 6.4.4.1]
///         decimal-constant integer-suffix
///         octal-constant integer-suffix
///         hexadecimal-constant integer-suffix
///         binary-literal integer-suffix [GNU, C++1y]
///       user-defined-integer-literal: [C++11 lex.ext]
///         decimal-literal ud-suffix
///         octal-literal ud-suffix
///         hexadecimal-literal ud-suffix
///         binary-literal ud-suffix [GNU, C++1y]
///       decimal-constant:
///         nonzero-digit
///         decimal-constant digit
///       octal-constant:
///         0
///         octal-constant octal-digit
///       hexadecimal-constant:
///         hexadecimal-prefix hexadecimal-digit
///         hexadecimal-constant hexadecimal-digit
///       hexadecimal-prefix: one of
///         0x 0X
///       binary-literal:
///         0b binary-digit
///         0B binary-digit
///         binary-literal binary-digit
///       integer-suffix:
///         unsigned-suffix [long-suffix]
///         unsigned-suffix [long-long-suffix]
///         long-suffix [unsigned-suffix]
///         long-long-suffix [unsigned-sufix]
///       nonzero-digit:
///         1 2 3 4 5 6 7 8 9
///       octal-digit:
///         0 1 2 3 4 5 6 7
///       hexadecimal-digit:
///         0 1 2 3 4 5 6 7 8 9
///         a b c d e f
///         A B C D E F
///       binary-digit:
///         0
///         1
///       unsigned-suffix: one of
///         u U
///       long-suffix: one of
///         l L
///       long-long-suffix: one of
///         ll LL
///
///       floating-constant: [C99 6.4.4.2]
///         TODO: add rules...
///
NumericLiteralParser::NumericLiteralParser(StringRef TokSpelling,
                                           SourceLocation TokLoc,
                                           const SourceManager &SM,
                                           const LangOptions &LangOpts,
                                           const TargetInfo &Target,
                                           DiagnosticsEngine &Diags)
    : SM(SM), LangOpts(LangOpts), Diags(Diags),
      ThisTokBegin(TokSpelling.begin()), ThisTokEnd(TokSpelling.end()) {

  s = DigitsBegin = ThisTokBegin;
  saw_exponent = false;
  saw_period = false;
  saw_ud_suffix = false;
  saw_fixed_point_suffix = false;
  isLong = false;
  isUnsigned = false;
  isLongLong = false;
  isSizeT = false;
  isHalf = false;
  isFloat = false;
  isImaginary = false;
  isFloat16 = false;
  isFloat128 = false;
  MicrosoftInteger = 0;
  isFract = false;
  isAccum = false;
  hadError = false;
  isBitInt = false;

  // This routine assumes that the range begin/end matches the regex for integer
  // and FP constants (specifically, the 'pp-number' regex), and assumes that
  // the byte at "*end" is both valid and not part of the regex.  Because of
  // this, it doesn't have to check for 'overscan' in various places.
  // Note: For HLSL, the end token is allowed to be '.' which would be in the
  // 'pp-number' regex. This is required to support vector swizzles on numeric
  // constants (i.e. 1.xx or 1.5f.rrr).
  if (isPreprocessingNumberBody(*ThisTokEnd) &&
      !(LangOpts.HLSL && *ThisTokEnd == '.')) {
    Diags.Report(TokLoc, diag::err_lexing_numeric);
    hadError = true;
    return;
  }

  if (*s == '0') { // parse radix
    ParseNumberStartingWithZero(TokLoc);
    if (hadError)
      return;
  } else { // the first digit is non-zero
    radix = 10;
    s = SkipDigits(s);
    if (s == ThisTokEnd) {
      // Done.
    } else {
      ParseDecimalOrOctalCommon(TokLoc);
      if (hadError)
        return;
    }
  }

  SuffixBegin = s;
  checkSeparator(TokLoc, s, CSK_AfterDigits);

  // Initial scan to lookahead for fixed point suffix.
  if (LangOpts.FixedPoint) {
    for (const char *c = s; c != ThisTokEnd; ++c) {
      if (*c == 'r' || *c == 'k' || *c == 'R' || *c == 'K') {
        saw_fixed_point_suffix = true;
        break;
      }
    }
  }

  // Parse the suffix.  At this point we can classify whether we have an FP or
  // integer constant.
  bool isFixedPointConstant = isFixedPointLiteral();
  bool isFPConstant = isFloatingLiteral();
  bool HasSize = false;
  bool DoubleUnderscore = false;

  // Loop over all of the characters of the suffix.  If we see something bad,
  // we break out of the loop.
  for (; s != ThisTokEnd; ++s) {
    switch (*s) {
    case 'R':
    case 'r':
      if (!LangOpts.FixedPoint)
        break;
      if (isFract || isAccum) break;
      if (!(saw_period || saw_exponent)) break;
      isFract = true;
      continue;
    case 'K':
    case 'k':
      if (!LangOpts.FixedPoint)
        break;
      if (isFract || isAccum) break;
      if (!(saw_period || saw_exponent)) break;
      isAccum = true;
      continue;
    case 'h':      // FP Suffix for "half".
    case 'H':
      // OpenCL Extension v1.2 s9.5 - h or H suffix for half type.
      if (!(LangOpts.Half || LangOpts.FixedPoint))
        break;
      if (isIntegerLiteral()) break;  // Error for integer constant.
      if (HasSize)
        break;
      HasSize = true;
      isHalf = true;
      continue;  // Success.
    case 'f':      // FP Suffix for "float"
    case 'F':
      if (!isFPConstant) break;  // Error for integer constant.
      if (HasSize)
        break;
      HasSize = true;

      // CUDA host and device may have different _Float16 support, therefore
      // allows f16 literals to avoid false alarm.
      // When we compile for OpenMP target offloading on NVPTX, f16 suffix
      // should also be supported.
      // ToDo: more precise check for CUDA.
      // TODO: AMDGPU might also support it in the future.
      if ((Target.hasFloat16Type() || LangOpts.CUDA ||
           (LangOpts.OpenMPIsTargetDevice && Target.getTriple().isNVPTX())) &&
          s + 2 < ThisTokEnd && s[1] == '1' && s[2] == '6') {
        s += 2; // success, eat up 2 characters.
        isFloat16 = true;
        continue;
      }

      isFloat = true;
      continue;  // Success.
    case 'q':    // FP Suffix for "__float128"
    case 'Q':
      if (!isFPConstant) break;  // Error for integer constant.
      if (HasSize)
        break;
      HasSize = true;
      isFloat128 = true;
      continue;  // Success.
    case 'u':
    case 'U':
      if (isFPConstant) break;  // Error for floating constant.
      if (isUnsigned) break;    // Cannot be repeated.
      isUnsigned = true;
      continue;  // Success.
    case 'l':
    case 'L':
      if (HasSize)
        break;
      HasSize = true;

      // Check for long long.  The L's need to be adjacent and the same case.
      if (s[1] == s[0]) {
        assert(s + 1 < ThisTokEnd && "didn't maximally munch?");
        if (isFPConstant) break;        // long long invalid for floats.
        isLongLong = true;
        ++s;  // Eat both of them.
      } else {
        isLong = true;
      }
      continue; // Success.
    case 'z':
    case 'Z':
      if (isFPConstant)
        break; // Invalid for floats.
      if (HasSize)
        break;
      HasSize = true;
      isSizeT = true;
      continue;
    case 'i':
    case 'I':
      if (LangOpts.MicrosoftExt && !isFPConstant) {
        // Allow i8, i16, i32, and i64. First, look ahead and check if
        // suffixes are Microsoft integers and not the imaginary unit.
        uint8_t Bits = 0;
        size_t ToSkip = 0;
        switch (s[1]) {
        case '8': // i8 suffix
          Bits = 8;
          ToSkip = 2;
          break;
        case '1':
          if (s[2] == '6') { // i16 suffix
            Bits = 16;
            ToSkip = 3;
          }
          break;
        case '3':
          if (s[2] == '2') { // i32 suffix
            Bits = 32;
            ToSkip = 3;
          }
          break;
        case '6':
          if (s[2] == '4') { // i64 suffix
            Bits = 64;
            ToSkip = 3;
          }
          break;
        default:
          break;
        }
        if (Bits) {
          if (HasSize)
            break;
          HasSize = true;
          MicrosoftInteger = Bits;
          s += ToSkip;
          assert(s <= ThisTokEnd && "didn't maximally munch?");
          break;
        }
      }
      [[fallthrough]];
    case 'j':
    case 'J':
      if (isImaginary) break;   // Cannot be repeated.
      isImaginary = true;
      continue;  // Success.
    case '_':
      if (isFPConstant)
        break; // Invalid for floats
      if (HasSize)
        break;
      // There is currently no way to reach this with DoubleUnderscore set.
      // If new double underscope literals are added handle it here as above.
      assert(!DoubleUnderscore && "unhandled double underscore case");
      if (LangOpts.CPlusPlus && s + 2 < ThisTokEnd &&
          s[1] == '_') { // s + 2 < ThisTokEnd to ensure some character exists
                         // after __
        DoubleUnderscore = true;
        s += 2; // Skip both '_'
        if (s + 1 < ThisTokEnd &&
            (*s == 'u' || *s == 'U')) { // Ensure some character after 'u'/'U'
          isUnsigned = true;
          ++s;
        }
        if (s + 1 < ThisTokEnd &&
            ((*s == 'w' && *(++s) == 'b') || (*s == 'W' && *(++s) == 'B'))) {
          isBitInt = true;
          HasSize = true;
          continue;
        }
      }
      break;
    case 'w':
    case 'W':
      if (isFPConstant)
        break; // Invalid for floats.
      if (HasSize)
        break; // Invalid if we already have a size for the literal.

      // wb and WB are allowed, but a mixture of cases like Wb or wB is not. We
      // explicitly do not support the suffix in C++ as an extension because a
      // library-based UDL that resolves to a library type may be more
      // appropriate there. The same rules apply for __wb/__WB.
      if ((!LangOpts.CPlusPlus || DoubleUnderscore) && s + 1 < ThisTokEnd &&
          ((s[0] == 'w' && s[1] == 'b') || (s[0] == 'W' && s[1] == 'B'))) {
        isBitInt = true;
        HasSize = true;
        ++s; // Skip both characters (2nd char skipped on continue).
        continue; // Success.
      }
    }
    // If we reached here, there was an error or a ud-suffix.
    break;
  }

  // "i", "if", and "il" are user-defined suffixes in C++1y.
  if (s != ThisTokEnd || isImaginary) {
    // FIXME: Don't bother expanding UCNs if !tok.hasUCN().
    expandUCNs(UDSuffixBuf, StringRef(SuffixBegin, ThisTokEnd - SuffixBegin));
    if (isValidUDSuffix(LangOpts, UDSuffixBuf)) {
      if (!isImaginary) {
        // Any suffix pieces we might have parsed are actually part of the
        // ud-suffix.
        isLong = false;
        isUnsigned = false;
        isLongLong = false;
        isSizeT = false;
        isFloat = false;
        isFloat16 = false;
        isHalf = false;
        isImaginary = false;
        isBitInt = false;
        MicrosoftInteger = 0;
        saw_fixed_point_suffix = false;
        isFract = false;
        isAccum = false;
      }

      saw_ud_suffix = true;
      return;
    }

    if (s != ThisTokEnd) {
      // Report an error if there are any.
      Diags.Report(Lexer::AdvanceToTokenCharacter(
                       TokLoc, SuffixBegin - ThisTokBegin, SM, LangOpts),
                   diag::err_invalid_suffix_constant)
          << StringRef(SuffixBegin, ThisTokEnd - SuffixBegin)
          << (isFixedPointConstant ? 2 : isFPConstant);
      hadError = true;
    }
  }

  if (!hadError && saw_fixed_point_suffix) {
    assert(isFract || isAccum);
  }
}

/// ParseDecimalOrOctalCommon - This method is called for decimal or octal
/// numbers. It issues an error for illegal digits, and handles floating point
/// parsing. If it detects a floating point number, the radix is set to 10.
void NumericLiteralParser::ParseDecimalOrOctalCommon(SourceLocation TokLoc){
  assert((radix == 8 || radix == 10) && "Unexpected radix");

  // If we have a hex digit other than 'e' (which denotes a FP exponent) then
  // the code is using an incorrect base.
  if (isHexDigit(*s) && *s != 'e' && *s != 'E' &&
      !isValidUDSuffix(LangOpts, StringRef(s, ThisTokEnd - s))) {
    Diags.Report(
        Lexer::AdvanceToTokenCharacter(TokLoc, s - ThisTokBegin, SM, LangOpts),
        diag::err_invalid_digit)
        << StringRef(s, 1) << (radix == 8 ? 1 : 0);
    hadError = true;
    return;
  }

  if (*s == '.') {
    checkSeparator(TokLoc, s, CSK_AfterDigits);
    s++;
    radix = 10;
    saw_period = true;
    checkSeparator(TokLoc, s, CSK_BeforeDigits);
    s = SkipDigits(s); // Skip suffix.
  }
  if (*s == 'e' || *s == 'E') { // exponent
    checkSeparator(TokLoc, s, CSK_AfterDigits);
    const char *Exponent = s;
    s++;
    radix = 10;
    saw_exponent = true;
    if (s != ThisTokEnd && (*s == '+' || *s == '-'))  s++; // sign
    const char *first_non_digit = SkipDigits(s);
    if (containsDigits(s, first_non_digit)) {
      checkSeparator(TokLoc, s, CSK_BeforeDigits);
      s = first_non_digit;
    } else {
      if (!hadError) {
        Diags.Report(Lexer::AdvanceToTokenCharacter(
                         TokLoc, Exponent - ThisTokBegin, SM, LangOpts),
                     diag::err_exponent_has_no_digits);
        hadError = true;
      }
      return;
    }
  }
}

/// Determine whether a suffix is a valid ud-suffix. We avoid treating reserved
/// suffixes as ud-suffixes, because the diagnostic experience is better if we
/// treat it as an invalid suffix.
bool NumericLiteralParser::isValidUDSuffix(const LangOptions &LangOpts,
                                           StringRef Suffix) {
  if (!LangOpts.CPlusPlus11 || Suffix.empty())
    return false;

  // By C++11 [lex.ext]p10, ud-suffixes starting with an '_' are always valid.
  // Suffixes starting with '__' (double underscore) are for use by
  // the implementation.
  if (Suffix.starts_with("_") && !Suffix.starts_with("__"))
    return true;

  // In C++11, there are no library suffixes.
  if (!LangOpts.CPlusPlus14)
    return false;

  // In C++14, "s", "h", "min", "ms", "us", and "ns" are used in the library.
  // Per tweaked N3660, "il", "i", and "if" are also used in the library.
  // In C++2a "d" and "y" are used in the library.
  return llvm::StringSwitch<bool>(Suffix)
      .Cases("h", "min", "s", true)
      .Cases("ms", "us", "ns", true)
      .Cases("il", "i", "if", true)
      .Cases("d", "y", LangOpts.CPlusPlus20)
      .Default(false);
}

void NumericLiteralParser::checkSeparator(SourceLocation TokLoc,
                                          const char *Pos,
                                          CheckSeparatorKind IsAfterDigits) {
  if (IsAfterDigits == CSK_AfterDigits) {
    if (Pos == ThisTokBegin)
      return;
    --Pos;
  } else if (Pos == ThisTokEnd)
    return;

  if (isDigitSeparator(*Pos)) {
    Diags.Report(Lexer::AdvanceToTokenCharacter(TokLoc, Pos - ThisTokBegin, SM,
                                                LangOpts),
                 diag::err_digit_separator_not_between_digits)
        << IsAfterDigits;
    hadError = true;
  }
}

/// ParseNumberStartingWithZero - This method is called when the first character
/// of the number is found to be a zero.  This means it is either an octal
/// number (like '04') or a hex number ('0x123a') a binary number ('0b1010') or
/// a floating point number (01239.123e4).  Eat the prefix, determining the
/// radix etc.
void NumericLiteralParser::ParseNumberStartingWithZero(SourceLocation TokLoc) {
  assert(s[0] == '0' && "Invalid method call");
  s++;

  int c1 = s[0];

  // Handle a hex number like 0x1234.
  if ((c1 == 'x' || c1 == 'X') && (isHexDigit(s[1]) || s[1] == '.')) {
    s++;
    assert(s < ThisTokEnd && "didn't maximally munch?");
    radix = 16;
    DigitsBegin = s;
    s = SkipHexDigits(s);
    bool HasSignificandDigits = containsDigits(DigitsBegin, s);
    if (s == ThisTokEnd) {
      // Done.
    } else if (*s == '.') {
      s++;
      saw_period = true;
      const char *floatDigitsBegin = s;
      s = SkipHexDigits(s);
      if (containsDigits(floatDigitsBegin, s))
        HasSignificandDigits = true;
      if (HasSignificandDigits)
        checkSeparator(TokLoc, floatDigitsBegin, CSK_BeforeDigits);
    }

    if (!HasSignificandDigits) {
      Diags.Report(Lexer::AdvanceToTokenCharacter(TokLoc, s - ThisTokBegin, SM,
                                                  LangOpts),
                   diag::err_hex_constant_requires)
          << LangOpts.CPlusPlus << 1;
      hadError = true;
      return;
    }

    // A binary exponent can appear with or with a '.'. If dotted, the
    // binary exponent is required.
    if (*s == 'p' || *s == 'P') {
      checkSeparator(TokLoc, s, CSK_AfterDigits);
      const char *Exponent = s;
      s++;
      saw_exponent = true;
      if (s != ThisTokEnd && (*s == '+' || *s == '-'))  s++; // sign
      const char *first_non_digit = SkipDigits(s);
      if (!containsDigits(s, first_non_digit)) {
        if (!hadError) {
          Diags.Report(Lexer::AdvanceToTokenCharacter(
                           TokLoc, Exponent - ThisTokBegin, SM, LangOpts),
                       diag::err_exponent_has_no_digits);
          hadError = true;
        }
        return;
      }
      checkSeparator(TokLoc, s, CSK_BeforeDigits);
      s = first_non_digit;

      if (!LangOpts.HexFloats)
        Diags.Report(TokLoc, LangOpts.CPlusPlus
                                 ? diag::ext_hex_literal_invalid
                                 : diag::ext_hex_constant_invalid);
      else if (LangOpts.CPlusPlus17)
        Diags.Report(TokLoc, diag::warn_cxx17_hex_literal);
    } else if (saw_period) {
      Diags.Report(Lexer::AdvanceToTokenCharacter(TokLoc, s - ThisTokBegin, SM,
                                                  LangOpts),
                   diag::err_hex_constant_requires)
          << LangOpts.CPlusPlus << 0;
      hadError = true;
    }
    return;
  }

  // Handle simple binary numbers 0b01010
  if ((c1 == 'b' || c1 == 'B') && (s[1] == '0' || s[1] == '1')) {
    // 0b101010 is a C++14 and C23 extension.
    unsigned DiagId;
    if (LangOpts.CPlusPlus14)
      DiagId = diag::warn_cxx11_compat_binary_literal;
    else if (LangOpts.C23)
      DiagId = diag::warn_c23_compat_binary_literal;
    else if (LangOpts.CPlusPlus)
      DiagId = diag::ext_binary_literal_cxx14;
    else
      DiagId = diag::ext_binary_literal;
    Diags.Report(TokLoc, DiagId);
    ++s;
    assert(s < ThisTokEnd && "didn't maximally munch?");
    radix = 2;
    DigitsBegin = s;
    s = SkipBinaryDigits(s);
    if (s == ThisTokEnd) {
      // Done.
    } else if (isHexDigit(*s) &&
               !isValidUDSuffix(LangOpts, StringRef(s, ThisTokEnd - s))) {
      Diags.Report(Lexer::AdvanceToTokenCharacter(TokLoc, s - ThisTokBegin, SM,
                                                  LangOpts),
                   diag::err_invalid_digit)
          << StringRef(s, 1) << 2;
      hadError = true;
    }
    // Other suffixes will be diagnosed by the caller.
    return;
  }

  // For now, the radix is set to 8. If we discover that we have a
  // floating point constant, the radix will change to 10. Octal floating
  // point constants are not permitted (only decimal and hexadecimal).
  radix = 8;
  const char *PossibleNewDigitStart = s;
  s = SkipOctalDigits(s);
  // When the value is 0 followed by a suffix (like 0wb), we want to leave 0
  // as the start of the digits. So if skipping octal digits does not skip
  // anything, we leave the digit start where it was.
  if (s != PossibleNewDigitStart)
    DigitsBegin = PossibleNewDigitStart;

  if (s == ThisTokEnd)
    return; // Done, simple octal number like 01234

  // If we have some other non-octal digit that *is* a decimal digit, see if
  // this is part of a floating point number like 094.123 or 09e1.
  if (isDigit(*s)) {
    const char *EndDecimal = SkipDigits(s);
    if (EndDecimal[0] == '.' || EndDecimal[0] == 'e' || EndDecimal[0] == 'E') {
      s = EndDecimal;
      radix = 10;
    }
  }

  ParseDecimalOrOctalCommon(TokLoc);
}

static bool alwaysFitsInto64Bits(unsigned Radix, unsigned NumDigits) {
  switch (Radix) {
  case 2:
    return NumDigits <= 64;
  case 8:
    return NumDigits <= 64 / 3; // Digits are groups of 3 bits.
  case 10:
    return NumDigits <= 19; // floor(log10(2^64))
  case 16:
    return NumDigits <= 64 / 4; // Digits are groups of 4 bits.
  default:
    llvm_unreachable("impossible Radix");
  }
}

/// GetIntegerValue - Convert this numeric literal value to an APInt that
/// matches Val's input width.  If there is an overflow, set Val to the low bits
/// of the result and return true.  Otherwise, return false.
bool NumericLiteralParser::GetIntegerValue(llvm::APInt &Val) {
  // Fast path: Compute a conservative bound on the maximum number of
  // bits per digit in this radix. If we can't possibly overflow a
  // uint64 based on that bound then do the simple conversion to
  // integer. This avoids the expensive overflow checking below, and
  // handles the common cases that matter (small decimal integers and
  // hex/octal values which don't overflow).
  const unsigned NumDigits = SuffixBegin - DigitsBegin;
  if (alwaysFitsInto64Bits(radix, NumDigits)) {
    uint64_t N = 0;
    for (const char *Ptr = DigitsBegin; Ptr != SuffixBegin; ++Ptr)
      if (!isDigitSeparator(*Ptr))
        N = N * radix + llvm::hexDigitValue(*Ptr);

    // This will truncate the value to Val's input width. Simply check
    // for overflow by comparing.
    Val = N;
    return Val.getZExtValue() != N;
  }

  Val = 0;
  const char *Ptr = DigitsBegin;

  llvm::APInt RadixVal(Val.getBitWidth(), radix);
  llvm::APInt CharVal(Val.getBitWidth(), 0);
  llvm::APInt OldVal = Val;

  bool OverflowOccurred = false;
  while (Ptr < SuffixBegin) {
    if (isDigitSeparator(*Ptr)) {
      ++Ptr;
      continue;
    }

    unsigned C = llvm::hexDigitValue(*Ptr++);

    // If this letter is out of bound for this radix, reject it.
    assert(C < radix && "NumericLiteralParser ctor should have rejected this");

    CharVal = C;

    // Add the digit to the value in the appropriate radix.  If adding in digits
    // made the value smaller, then this overflowed.
    OldVal = Val;

    // Multiply by radix, did overflow occur on the multiply?
    Val *= RadixVal;
    OverflowOccurred |= Val.udiv(RadixVal) != OldVal;

    // Add value, did overflow occur on the value?
    //   (a + b) ult b  <=> overflow
    Val += CharVal;
    OverflowOccurred |= Val.ult(CharVal);
  }
  return OverflowOccurred;
}

llvm::APFloat::opStatus
NumericLiteralParser::GetFloatValue(llvm::APFloat &Result,
                                    llvm::RoundingMode RM) {
  using llvm::APFloat;

  unsigned n = std::min(SuffixBegin - ThisTokBegin, ThisTokEnd - ThisTokBegin);

  llvm::SmallString<16> Buffer;
  StringRef Str(ThisTokBegin, n);
  if (Str.contains('\'')) {
    Buffer.reserve(n);
    std::remove_copy_if(Str.begin(), Str.end(), std::back_inserter(Buffer),
                        &isDigitSeparator);
    Str = Buffer;
  }

  auto StatusOrErr = Result.convertFromString(Str, RM);
  assert(StatusOrErr && "Invalid floating point representation");
  return !errorToBool(StatusOrErr.takeError()) ? *StatusOrErr
                                               : APFloat::opInvalidOp;
}

static inline bool IsExponentPart(char c, bool isHex) {
  if (isHex)
    return c == 'p' || c == 'P';
  return c == 'e' || c == 'E';
}

bool NumericLiteralParser::GetFixedPointValue(llvm::APInt &StoreVal, unsigned Scale) {
  assert(radix == 16 || radix == 10);

  // Find how many digits are needed to store the whole literal.
  unsigned NumDigits = SuffixBegin - DigitsBegin;
  if (saw_period) --NumDigits;

  // Initial scan of the exponent if it exists
  bool ExpOverflowOccurred = false;
  bool NegativeExponent = false;
  const char *ExponentBegin;
  uint64_t Exponent = 0;
  int64_t BaseShift = 0;
  if (saw_exponent) {
    const char *Ptr = DigitsBegin;

    while (!IsExponentPart(*Ptr, radix == 16))
      ++Ptr;
    ExponentBegin = Ptr;
    ++Ptr;
    NegativeExponent = *Ptr == '-';
    if (NegativeExponent) ++Ptr;

    unsigned NumExpDigits = SuffixBegin - Ptr;
    if (alwaysFitsInto64Bits(radix, NumExpDigits)) {
      llvm::StringRef ExpStr(Ptr, NumExpDigits);
      llvm::APInt ExpInt(/*numBits=*/64, ExpStr, /*radix=*/10);
      Exponent = ExpInt.getZExtValue();
    } else {
      ExpOverflowOccurred = true;
    }

    if (NegativeExponent) BaseShift -= Exponent;
    else BaseShift += Exponent;
  }

  // Number of bits needed for decimal literal is
  //   ceil(NumDigits * log2(10))       Integral part
  // + Scale                            Fractional part
  // + ceil(Exponent * log2(10))        Exponent
  // --------------------------------------------------
  //   ceil((NumDigits + Exponent) * log2(10)) + Scale
  //
  // But for simplicity in handling integers, we can round up log2(10) to 4,
  // making:
  // 4 * (NumDigits + Exponent) + Scale
  //
  // Number of digits needed for hexadecimal literal is
  //   4 * NumDigits                    Integral part
  // + Scale                            Fractional part
  // + Exponent                         Exponent
  // --------------------------------------------------
  //   (4 * NumDigits) + Scale + Exponent
  uint64_t NumBitsNeeded;
  if (radix == 10)
    NumBitsNeeded = 4 * (NumDigits + Exponent) + Scale;
  else
    NumBitsNeeded = 4 * NumDigits + Exponent + Scale;

  if (NumBitsNeeded > std::numeric_limits<unsigned>::max())
    ExpOverflowOccurred = true;
  llvm::APInt Val(static_cast<unsigned>(NumBitsNeeded), 0, /*isSigned=*/false);

  bool FoundDecimal = false;

  int64_t FractBaseShift = 0;
  const char *End = saw_exponent ? ExponentBegin : SuffixBegin;
  for (const char *Ptr = DigitsBegin; Ptr < End; ++Ptr) {
    if (*Ptr == '.') {
      FoundDecimal = true;
      continue;
    }

    // Normal reading of an integer
    unsigned C = llvm::hexDigitValue(*Ptr);
    assert(C < radix && "NumericLiteralParser ctor should have rejected this");

    Val *= radix;
    Val += C;

    if (FoundDecimal)
      // Keep track of how much we will need to adjust this value by from the
      // number of digits past the radix point.
      --FractBaseShift;
  }

  // For a radix of 16, we will be multiplying by 2 instead of 16.
  if (radix == 16) FractBaseShift *= 4;
  BaseShift += FractBaseShift;

  Val <<= Scale;

  uint64_t Base = (radix == 16) ? 2 : 10;
  if (BaseShift > 0) {
    for (int64_t i = 0; i < BaseShift; ++i) {
      Val *= Base;
    }
  } else if (BaseShift < 0) {
    for (int64_t i = BaseShift; i < 0 && !Val.isZero(); ++i)
      Val = Val.udiv(Base);
  }

  bool IntOverflowOccurred = false;
  auto MaxVal = llvm::APInt::getMaxValue(StoreVal.getBitWidth());
  if (Val.getBitWidth() > StoreVal.getBitWidth()) {
    IntOverflowOccurred |= Val.ugt(MaxVal.zext(Val.getBitWidth()));
    StoreVal = Val.trunc(StoreVal.getBitWidth());
  } else if (Val.getBitWidth() < StoreVal.getBitWidth()) {
    IntOverflowOccurred |= Val.zext(MaxVal.getBitWidth()).ugt(MaxVal);
    StoreVal = Val.zext(StoreVal.getBitWidth());
  } else {
    StoreVal = Val;
  }

  return IntOverflowOccurred || ExpOverflowOccurred;
}

/// \verbatim
///       user-defined-character-literal: [C++11 lex.ext]
///         character-literal ud-suffix
///       ud-suffix:
///         identifier
///       character-literal: [C++11 lex.ccon]
///         ' c-char-sequence '
///         u' c-char-sequence '
///         U' c-char-sequence '
///         L' c-char-sequence '
///         u8' c-char-sequence ' [C++1z lex.ccon]
///       c-char-sequence:
///         c-char
///         c-char-sequence c-char
///       c-char:
///         any member of the source character set except the single-quote ',
///           backslash \, or new-line character
///         escape-sequence
///         universal-character-name
///       escape-sequence:
///         simple-escape-sequence
///         octal-escape-sequence
///         hexadecimal-escape-sequence
///       simple-escape-sequence:
///         one of \' \" \? \\ \a \b \f \n \r \t \v
///       octal-escape-sequence:
///         \ octal-digit
///         \ octal-digit octal-digit
///         \ octal-digit octal-digit octal-digit
///       hexadecimal-escape-sequence:
///         \x hexadecimal-digit
///         hexadecimal-escape-sequence hexadecimal-digit
///       universal-character-name: [C++11 lex.charset]
///         \u hex-quad
///         \U hex-quad hex-quad
///       hex-quad:
///         hex-digit hex-digit hex-digit hex-digit
/// \endverbatim
///
CharLiteralParser::CharLiteralParser(const char *begin, const char *end,
                                     SourceLocation Loc, Preprocessor &PP,
                                     tok::TokenKind kind) {
  // At this point we know that the character matches the regex "(L|u|U)?'.*'".
  HadError = false;

  Kind = kind;

  const char *TokBegin = begin;

  // Skip over wide character determinant.
  if (Kind != tok::char_constant)
    ++begin;
  if (Kind == tok::utf8_char_constant)
    ++begin;

  // Skip over the entry quote.
  if (begin[0] != '\'') {
    PP.Diag(Loc, diag::err_lexing_char);
    HadError = true;
    return;
  }

  ++begin;

  // Remove an optional ud-suffix.
  if (end[-1] != '\'') {
    const char *UDSuffixEnd = end;
    do {
      --end;
    } while (end[-1] != '\'');
    // FIXME: Don't bother with this if !tok.hasUCN().
    expandUCNs(UDSuffixBuf, StringRef(end, UDSuffixEnd - end));
    UDSuffixOffset = end - TokBegin;
  }

  // Trim the ending quote.
  assert(end != begin && "Invalid token lexed");
  --end;

  // FIXME: The "Value" is an uint64_t so we can handle char literals of
  // up to 64-bits.
  // FIXME: This extensively assumes that 'char' is 8-bits.
  assert(PP.getTargetInfo().getCharWidth() == 8 &&
         "Assumes char is 8 bits");
  assert(PP.getTargetInfo().getIntWidth() <= 64 &&
         (PP.getTargetInfo().getIntWidth() & 7) == 0 &&
         "Assumes sizeof(int) on target is <= 64 and a multiple of char");
  assert(PP.getTargetInfo().getWCharWidth() <= 64 &&
         "Assumes sizeof(wchar) on target is <= 64");

  SmallVector<uint32_t, 4> codepoint_buffer;
  codepoint_buffer.resize(end - begin);
  uint32_t *buffer_begin = &codepoint_buffer.front();
  uint32_t *buffer_end = buffer_begin + codepoint_buffer.size();

  // Unicode escapes representing characters that cannot be correctly
  // represented in a single code unit are disallowed in character literals
  // by this implementation.
  uint32_t largest_character_for_kind;
  if (tok::wide_char_constant == Kind) {
    largest_character_for_kind =
        0xFFFFFFFFu >> (32-PP.getTargetInfo().getWCharWidth());
  } else if (tok::utf8_char_constant == Kind) {
    largest_character_for_kind = 0x7F;
  } else if (tok::utf16_char_constant == Kind) {
    largest_character_for_kind = 0xFFFF;
  } else if (tok::utf32_char_constant == Kind) {
    largest_character_for_kind = 0x10FFFF;
  } else {
    largest_character_for_kind = 0x7Fu;
  }

  while (begin != end) {
    // Is this a span of non-escape characters?
    if (begin[0] != '\\') {
      char const *start = begin;
      do {
        ++begin;
      } while (begin != end && *begin != '\\');

      char const *tmp_in_start = start;
      uint32_t *tmp_out_start = buffer_begin;
      llvm::ConversionResult res =
          llvm::ConvertUTF8toUTF32(reinterpret_cast<llvm::UTF8 const **>(&start),
                             reinterpret_cast<llvm::UTF8 const *>(begin),
                             &buffer_begin, buffer_end, llvm::strictConversion);
      if (res != llvm::conversionOK) {
        // If we see bad encoding for unprefixed character literals, warn and
        // simply copy the byte values, for compatibility with gcc and
        // older versions of clang.
        bool NoErrorOnBadEncoding = isOrdinary();
        unsigned Msg = diag::err_bad_character_encoding;
        if (NoErrorOnBadEncoding)
          Msg = diag::warn_bad_character_encoding;
        PP.Diag(Loc, Msg);
        if (NoErrorOnBadEncoding) {
          start = tmp_in_start;
          buffer_begin = tmp_out_start;
          for (; start != begin; ++start, ++buffer_begin)
            *buffer_begin = static_cast<uint8_t>(*start);
        } else {
          HadError = true;
        }
      } else {
        for (; tmp_out_start < buffer_begin; ++tmp_out_start) {
          if (*tmp_out_start > largest_character_for_kind) {
            HadError = true;
            PP.Diag(Loc, diag::err_character_too_large);
          }
        }
      }

      continue;
    }
    // Is this a Universal Character Name escape?
    if (begin[1] == 'u' || begin[1] == 'U' || begin[1] == 'N') {
      unsigned short UcnLen = 0;
      if (!ProcessUCNEscape(TokBegin, begin, end, *buffer_begin, UcnLen,
                            FullSourceLoc(Loc, PP.getSourceManager()),
                            &PP.getDiagnostics(), PP.getLangOpts(), true)) {
        HadError = true;
      } else if (*buffer_begin > largest_character_for_kind) {
        HadError = true;
        PP.Diag(Loc, diag::err_character_too_large);
      }

      ++buffer_begin;
      continue;
    }
    unsigned CharWidth = getCharWidth(Kind, PP.getTargetInfo());
    uint64_t result =
        ProcessCharEscape(TokBegin, begin, end, HadError,
                          FullSourceLoc(Loc, PP.getSourceManager()), CharWidth,
                          &PP.getDiagnostics(), PP.getLangOpts(),
                          StringLiteralEvalMethod::Evaluated);
    *buffer_begin++ = result;
  }

  unsigned NumCharsSoFar = buffer_begin - &codepoint_buffer.front();

  if (NumCharsSoFar > 1) {
    if (isOrdinary() && NumCharsSoFar == 4)
      PP.Diag(Loc, diag::warn_four_char_character_literal);
    else if (isOrdinary())
      PP.Diag(Loc, diag::warn_multichar_character_literal);
    else {
      PP.Diag(Loc, diag::err_multichar_character_literal) << (isWide() ? 0 : 1);
      HadError = true;
    }
    IsMultiChar = true;
  } else {
    IsMultiChar = false;
  }

  llvm::APInt LitVal(PP.getTargetInfo().getIntWidth(), 0);

  // Narrow character literals act as though their value is concatenated
  // in this implementation, but warn on overflow.
  bool multi_char_too_long = false;
  if (isOrdinary() && isMultiChar()) {
    LitVal = 0;
    for (size_t i = 0; i < NumCharsSoFar; ++i) {
      // check for enough leading zeros to shift into
      multi_char_too_long |= (LitVal.countl_zero() < 8);
      LitVal <<= 8;
      LitVal = LitVal + (codepoint_buffer[i] & 0xFF);
    }
  } else if (NumCharsSoFar > 0) {
    // otherwise just take the last character
    LitVal = buffer_begin[-1];
  }

  if (!HadError && multi_char_too_long) {
    PP.Diag(Loc, diag::warn_char_constant_too_large);
  }

  // Transfer the value from APInt to uint64_t
  Value = LitVal.getZExtValue();

  // If this is a single narrow character, sign extend it (e.g. '\xFF' is "-1")
  // if 'char' is signed for this target (C99 6.4.4.4p10).  Note that multiple
  // character constants are not sign extended in the this implementation:
  // '\xFF\xFF' = 65536 and '\x0\xFF' = 255, which matches GCC.
  if (isOrdinary() && NumCharsSoFar == 1 && (Value & 128) &&
      PP.getLangOpts().CharIsSigned)
    Value = (signed char)Value;
}

/// \verbatim
///       string-literal: [C++0x lex.string]
///         encoding-prefix " [s-char-sequence] "
///         encoding-prefix R raw-string
///       encoding-prefix:
///         u8
///         u
///         U
///         L
///       s-char-sequence:
///         s-char
///         s-char-sequence s-char
///       s-char:
///         any member of the source character set except the double-quote ",
///           backslash \, or new-line character
///         escape-sequence
///         universal-character-name
///       raw-string:
///         " d-char-sequence ( r-char-sequence ) d-char-sequence "
///       r-char-sequence:
///         r-char
///         r-char-sequence r-char
///       r-char:
///         any member of the source character set, except a right parenthesis )
///           followed by the initial d-char-sequence (which may be empty)
///           followed by a double quote ".
///       d-char-sequence:
///         d-char
///         d-char-sequence d-char
///       d-char:
///         any member of the basic source character set except:
///           space, the left parenthesis (, the right parenthesis ),
///           the backslash \, and the control characters representing horizontal
///           tab, vertical tab, form feed, and newline.
///       escape-sequence: [C++0x lex.ccon]
///         simple-escape-sequence
///         octal-escape-sequence
///         hexadecimal-escape-sequence
///       simple-escape-sequence:
///         one of \' \" \? \\ \a \b \f \n \r \t \v
///       octal-escape-sequence:
///         \ octal-digit
///         \ octal-digit octal-digit
///         \ octal-digit octal-digit octal-digit
///       hexadecimal-escape-sequence:
///         \x hexadecimal-digit
///         hexadecimal-escape-sequence hexadecimal-digit
///       universal-character-name:
///         \u hex-quad
///         \U hex-quad hex-quad
///       hex-quad:
///         hex-digit hex-digit hex-digit hex-digit
/// \endverbatim
///
StringLiteralParser::StringLiteralParser(ArrayRef<Token> StringToks,
                                         Preprocessor &PP,
                                         StringLiteralEvalMethod EvalMethod)
    : SM(PP.getSourceManager()), Features(PP.getLangOpts()),
      Target(PP.getTargetInfo()), Diags(&PP.getDiagnostics()),
      MaxTokenLength(0), SizeBound(0), CharByteWidth(0), Kind(tok::unknown),
      ResultPtr(ResultBuf.data()), EvalMethod(EvalMethod), hadError(false),
      Pascal(false) {
  init(StringToks);
}

void StringLiteralParser::init(ArrayRef<Token> StringToks){
  // The literal token may have come from an invalid source location (e.g. due
  // to a PCH error), in which case the token length will be 0.
  if (StringToks.empty() || StringToks[0].getLength() < 2)
    return DiagnoseLexingError(SourceLocation());

  // Scan all of the string portions, remember the max individual token length,
  // computing a bound on the concatenated string length, and see whether any
  // piece is a wide-string.  If any of the string portions is a wide-string
  // literal, the result is a wide-string literal [C99 6.4.5p4].
  assert(!StringToks.empty() && "expected at least one token");
  MaxTokenLength = StringToks[0].getLength();
  assert(StringToks[0].getLength() >= 2 && "literal token is invalid!");
  SizeBound = StringToks[0].getLength() - 2; // -2 for "".
  hadError = false;

  // Determines the kind of string from the prefix
  Kind = tok::string_literal;

  /// (C99 5.1.1.2p1).  The common case is only one string fragment.
  for (const Token &Tok : StringToks) {
    if (Tok.getLength() < 2)
      return DiagnoseLexingError(Tok.getLocation());

    // The string could be shorter than this if it needs cleaning, but this is a
    // reasonable bound, which is all we need.
    assert(Tok.getLength() >= 2 && "literal token is invalid!");
    SizeBound += Tok.getLength() - 2; // -2 for "".

    // Remember maximum string piece length.
    if (Tok.getLength() > MaxTokenLength)
      MaxTokenLength = Tok.getLength();

    // Remember if we see any wide or utf-8/16/32 strings.
    // Also check for illegal concatenations.
    if (isUnevaluated() && Tok.getKind() != tok::string_literal) {
      if (Diags) {
        SourceLocation PrefixEndLoc = Lexer::AdvanceToTokenCharacter(
            Tok.getLocation(), getEncodingPrefixLen(Tok.getKind()), SM,
            Features);
        CharSourceRange Range =
            CharSourceRange::getCharRange({Tok.getLocation(), PrefixEndLoc});
        StringRef Prefix(SM.getCharacterData(Tok.getLocation()),
                         getEncodingPrefixLen(Tok.getKind()));
        Diags->Report(Tok.getLocation(),
                      Features.CPlusPlus26
                          ? diag::err_unevaluated_string_prefix
                          : diag::warn_unevaluated_string_prefix)
            << Prefix << Features.CPlusPlus << FixItHint::CreateRemoval(Range);
      }
      if (Features.CPlusPlus26)
        hadError = true;
    } else if (Tok.isNot(Kind) && Tok.isNot(tok::string_literal)) {
      if (isOrdinary()) {
        Kind = Tok.getKind();
      } else {
        if (Diags)
          Diags->Report(Tok.getLocation(), diag::err_unsupported_string_concat);
        hadError = true;
      }
    }
  }

  // Include space for the null terminator.
  ++SizeBound;

  // TODO: K&R warning: "traditional C rejects string constant concatenation"

  // Get the width in bytes of char/wchar_t/char16_t/char32_t
  CharByteWidth = getCharWidth(Kind, Target);
  assert((CharByteWidth & 7) == 0 && "Assumes character size is byte multiple");
  CharByteWidth /= 8;

  // The output buffer size needs to be large enough to hold wide characters.
  // This is a worst-case assumption which basically corresponds to L"" "long".
  SizeBound *= CharByteWidth;

  // Size the temporary buffer to hold the result string data.
  ResultBuf.resize(SizeBound);

  // Likewise, but for each string piece.
  SmallString<512> TokenBuf;
  TokenBuf.resize(MaxTokenLength);

  // Loop over all the strings, getting their spelling, and expanding them to
  // wide strings as appropriate.
  ResultPtr = &ResultBuf[0];   // Next byte to fill in.

  Pascal = false;

  SourceLocation UDSuffixTokLoc;

  for (unsigned i = 0, e = StringToks.size(); i != e; ++i) {
    const char *ThisTokBuf = &TokenBuf[0];
    // Get the spelling of the token, which eliminates trigraphs, etc.  We know
    // that ThisTokBuf points to a buffer that is big enough for the whole token
    // and 'spelled' tokens can only shrink.
    bool StringInvalid = false;
    unsigned ThisTokLen =
      Lexer::getSpelling(StringToks[i], ThisTokBuf, SM, Features,
                         &StringInvalid);
    if (StringInvalid)
      return DiagnoseLexingError(StringToks[i].getLocation());

    const char *ThisTokBegin = ThisTokBuf;
    const char *ThisTokEnd = ThisTokBuf+ThisTokLen;

    // Remove an optional ud-suffix.
    if (ThisTokEnd[-1] != '"') {
      const char *UDSuffixEnd = ThisTokEnd;
      do {
        --ThisTokEnd;
      } while (ThisTokEnd[-1] != '"');

      StringRef UDSuffix(ThisTokEnd, UDSuffixEnd - ThisTokEnd);

      if (UDSuffixBuf.empty()) {
        if (StringToks[i].hasUCN())
          expandUCNs(UDSuffixBuf, UDSuffix);
        else
          UDSuffixBuf.assign(UDSuffix);
        UDSuffixToken = i;
        UDSuffixOffset = ThisTokEnd - ThisTokBuf;
        UDSuffixTokLoc = StringToks[i].getLocation();
      } else {
        SmallString<32> ExpandedUDSuffix;
        if (StringToks[i].hasUCN()) {
          expandUCNs(ExpandedUDSuffix, UDSuffix);
          UDSuffix = ExpandedUDSuffix;
        }

        // C++11 [lex.ext]p8: At the end of phase 6, if a string literal is the
        // result of a concatenation involving at least one user-defined-string-
        // literal, all the participating user-defined-string-literals shall
        // have the same ud-suffix.
        bool UnevaluatedStringHasUDL = isUnevaluated() && !UDSuffix.empty();
        if (UDSuffixBuf != UDSuffix || UnevaluatedStringHasUDL) {
          if (Diags) {
            SourceLocation TokLoc = StringToks[i].getLocation();
            if (UnevaluatedStringHasUDL) {
              Diags->Report(TokLoc, diag::err_unevaluated_string_udl)
                  << SourceRange(TokLoc, TokLoc);
            } else {
              Diags->Report(TokLoc, diag::err_string_concat_mixed_suffix)
                  << UDSuffixBuf << UDSuffix
                  << SourceRange(UDSuffixTokLoc, UDSuffixTokLoc);
            }
          }
          hadError = true;
        }
      }
    }

    // Strip the end quote.
    --ThisTokEnd;

    // TODO: Input character set mapping support.

    // Skip marker for wide or unicode strings.
    if (ThisTokBuf[0] == 'L' || ThisTokBuf[0] == 'u' || ThisTokBuf[0] == 'U') {
      ++ThisTokBuf;
      // Skip 8 of u8 marker for utf8 strings.
      if (ThisTokBuf[0] == '8')
        ++ThisTokBuf;
    }

    // Check for raw string
    if (ThisTokBuf[0] == 'R') {
      if (ThisTokBuf[1] != '"') {
        // The file may have come from PCH and then changed after loading the
        // PCH; Fail gracefully.
        return DiagnoseLexingError(StringToks[i].getLocation());
      }
      ThisTokBuf += 2; // skip R"

      // C++11 [lex.string]p2: A `d-char-sequence` shall consist of at most 16
      // characters.
      constexpr unsigned MaxRawStrDelimLen = 16;

      const char *Prefix = ThisTokBuf;
      while (static_cast<unsigned>(ThisTokBuf - Prefix) < MaxRawStrDelimLen &&
             ThisTokBuf[0] != '(')
        ++ThisTokBuf;
      if (ThisTokBuf[0] != '(')
        return DiagnoseLexingError(StringToks[i].getLocation());
      ++ThisTokBuf; // skip '('

      // Remove same number of characters from the end
      ThisTokEnd -= ThisTokBuf - Prefix;
      if (ThisTokEnd < ThisTokBuf)
        return DiagnoseLexingError(StringToks[i].getLocation());

      // C++14 [lex.string]p4: A source-file new-line in a raw string literal
      // results in a new-line in the resulting execution string-literal.
      StringRef RemainingTokenSpan(ThisTokBuf, ThisTokEnd - ThisTokBuf);
      while (!RemainingTokenSpan.empty()) {
        // Split the string literal on \r\n boundaries.
        size_t CRLFPos = RemainingTokenSpan.find("\r\n");
        StringRef BeforeCRLF = RemainingTokenSpan.substr(0, CRLFPos);
        StringRef AfterCRLF = RemainingTokenSpan.substr(CRLFPos);

        // Copy everything before the \r\n sequence into the string literal.
        if (CopyStringFragment(StringToks[i], ThisTokBegin, BeforeCRLF))
          hadError = true;

        // Point into the \n inside the \r\n sequence and operate on the
        // remaining portion of the literal.
        RemainingTokenSpan = AfterCRLF.substr(1);
      }
    } else {
      if (ThisTokBuf[0] != '"') {
        // The file may have come from PCH and then changed after loading the
        // PCH; Fail gracefully.
        return DiagnoseLexingError(StringToks[i].getLocation());
      }
      ++ThisTokBuf; // skip "

      // Check if this is a pascal string
      if (!isUnevaluated() && Features.PascalStrings &&
          ThisTokBuf + 1 != ThisTokEnd && ThisTokBuf[0] == '\\' &&
          ThisTokBuf[1] == 'p') {

        // If the \p sequence is found in the first token, we have a pascal string
        // Otherwise, if we already have a pascal string, ignore the first \p
        if (i == 0) {
          ++ThisTokBuf;
          Pascal = true;
        } else if (Pascal)
          ThisTokBuf += 2;
      }

      while (ThisTokBuf != ThisTokEnd) {
        // Is this a span of non-escape characters?
        if (ThisTokBuf[0] != '\\') {
          const char *InStart = ThisTokBuf;
          do {
            ++ThisTokBuf;
          } while (ThisTokBuf != ThisTokEnd && ThisTokBuf[0] != '\\');

          // Copy the character span over.
          if (CopyStringFragment(StringToks[i], ThisTokBegin,
                                 StringRef(InStart, ThisTokBuf - InStart)))
            hadError = true;
          continue;
        }
        // Is this a Universal Character Name escape?
        if (ThisTokBuf[1] == 'u' || ThisTokBuf[1] == 'U' ||
            ThisTokBuf[1] == 'N') {
          EncodeUCNEscape(ThisTokBegin, ThisTokBuf, ThisTokEnd,
                          ResultPtr, hadError,
                          FullSourceLoc(StringToks[i].getLocation(), SM),
                          CharByteWidth, Diags, Features);
          continue;
        }
        // Otherwise, this is a non-UCN escape character.  Process it.
        unsigned ResultChar =
            ProcessCharEscape(ThisTokBegin, ThisTokBuf, ThisTokEnd, hadError,
                              FullSourceLoc(StringToks[i].getLocation(), SM),
                              CharByteWidth * 8, Diags, Features, EvalMethod);

        if (CharByteWidth == 4) {
          // FIXME: Make the type of the result buffer correct instead of
          // using reinterpret_cast.
          llvm::UTF32 *ResultWidePtr = reinterpret_cast<llvm::UTF32*>(ResultPtr);
          *ResultWidePtr = ResultChar;
          ResultPtr += 4;
        } else if (CharByteWidth == 2) {
          // FIXME: Make the type of the result buffer correct instead of
          // using reinterpret_cast.
          llvm::UTF16 *ResultWidePtr = reinterpret_cast<llvm::UTF16*>(ResultPtr);
          *ResultWidePtr = ResultChar & 0xFFFF;
          ResultPtr += 2;
        } else {
          assert(CharByteWidth == 1 && "Unexpected char width");
          *ResultPtr++ = ResultChar & 0xFF;
        }
      }
    }
  }

  assert((!Pascal || !isUnevaluated()) &&
         "Pascal string in unevaluated context");
  if (Pascal) {
    if (CharByteWidth == 4) {
      // FIXME: Make the type of the result buffer correct instead of
      // using reinterpret_cast.
      llvm::UTF32 *ResultWidePtr = reinterpret_cast<llvm::UTF32*>(ResultBuf.data());
      ResultWidePtr[0] = GetNumStringChars() - 1;
    } else if (CharByteWidth == 2) {
      // FIXME: Make the type of the result buffer correct instead of
      // using reinterpret_cast.
      llvm::UTF16 *ResultWidePtr = reinterpret_cast<llvm::UTF16*>(ResultBuf.data());
      ResultWidePtr[0] = GetNumStringChars() - 1;
    } else {
      assert(CharByteWidth == 1 && "Unexpected char width");
      ResultBuf[0] = GetNumStringChars() - 1;
    }

    // Verify that pascal strings aren't too large.
    if (GetStringLength() > 256) {
      if (Diags)
        Diags->Report(StringToks.front().getLocation(),
                      diag::err_pascal_string_too_long)
          << SourceRange(StringToks.front().getLocation(),
                         StringToks.back().getLocation());
      hadError = true;
      return;
    }
  } else if (Diags) {
    // Complain if this string literal has too many characters.
    unsigned MaxChars = Features.CPlusPlus? 65536 : Features.C99 ? 4095 : 509;

    if (GetNumStringChars() > MaxChars)
      Diags->Report(StringToks.front().getLocation(),
                    diag::ext_string_too_long)
        << GetNumStringChars() << MaxChars
        << (Features.CPlusPlus ? 2 : Features.C99 ? 1 : 0)
        << SourceRange(StringToks.front().getLocation(),
                       StringToks.back().getLocation());
  }
}

static const char *resyncUTF8(const char *Err, const char *End) {
  if (Err == End)
    return End;
  End = Err + std::min<unsigned>(llvm::getNumBytesForUTF8(*Err), End-Err);
  while (++Err != End && (*Err & 0xC0) == 0x80)
    ;
  return Err;
}

/// This function copies from Fragment, which is a sequence of bytes
/// within Tok's contents (which begin at TokBegin) into ResultPtr.
/// Performs widening for multi-byte characters.
bool StringLiteralParser::CopyStringFragment(const Token &Tok,
                                             const char *TokBegin,
                                             StringRef Fragment) {
  const llvm::UTF8 *ErrorPtrTmp;
  if (ConvertUTF8toWide(CharByteWidth, Fragment, ResultPtr, ErrorPtrTmp))
    return false;

  // If we see bad encoding for unprefixed string literals, warn and
  // simply copy the byte values, for compatibility with gcc and older
  // versions of clang.
  bool NoErrorOnBadEncoding = isOrdinary();
  if (NoErrorOnBadEncoding) {
    memcpy(ResultPtr, Fragment.data(), Fragment.size());
    ResultPtr += Fragment.size();
  }

  if (Diags) {
    const char *ErrorPtr = reinterpret_cast<const char *>(ErrorPtrTmp);

    FullSourceLoc SourceLoc(Tok.getLocation(), SM);
    const DiagnosticBuilder &Builder =
      Diag(Diags, Features, SourceLoc, TokBegin,
           ErrorPtr, resyncUTF8(ErrorPtr, Fragment.end()),
           NoErrorOnBadEncoding ? diag::warn_bad_string_encoding
                                : diag::err_bad_string_encoding);

    const char *NextStart = resyncUTF8(ErrorPtr, Fragment.end());
    StringRef NextFragment(NextStart, Fragment.end()-NextStart);

    // Decode into a dummy buffer.
    SmallString<512> Dummy;
    Dummy.reserve(Fragment.size() * CharByteWidth);
    char *Ptr = Dummy.data();

    while (!ConvertUTF8toWide(CharByteWidth, NextFragment, Ptr, ErrorPtrTmp)) {
      const char *ErrorPtr = reinterpret_cast<const char *>(ErrorPtrTmp);
      NextStart = resyncUTF8(ErrorPtr, Fragment.end());
      Builder << MakeCharSourceRange(Features, SourceLoc, TokBegin,
                                     ErrorPtr, NextStart);
      NextFragment = StringRef(NextStart, Fragment.end()-NextStart);
    }
  }
  return !NoErrorOnBadEncoding;
}

void StringLiteralParser::DiagnoseLexingError(SourceLocation Loc) {
  hadError = true;
  if (Diags)
    Diags->Report(Loc, diag::err_lexing_string);
}

/// getOffsetOfStringByte - This function returns the offset of the
/// specified byte of the string data represented by Token.  This handles
/// advancing over escape sequences in the string.
unsigned StringLiteralParser::getOffsetOfStringByte(const Token &Tok,
                                                    unsigned ByteNo) const {
  // Get the spelling of the token.
  SmallString<32> SpellingBuffer;
  SpellingBuffer.resize(Tok.getLength());

  bool StringInvalid = false;
  const char *SpellingPtr = &SpellingBuffer[0];
  unsigned TokLen = Lexer::getSpelling(Tok, SpellingPtr, SM, Features,
                                       &StringInvalid);
  if (StringInvalid)
    return 0;

  const char *SpellingStart = SpellingPtr;
  const char *SpellingEnd = SpellingPtr+TokLen;

  // Handle UTF-8 strings just like narrow strings.
  if (SpellingPtr[0] == 'u' && SpellingPtr[1] == '8')
    SpellingPtr += 2;

  assert(SpellingPtr[0] != 'L' && SpellingPtr[0] != 'u' &&
         SpellingPtr[0] != 'U' && "Doesn't handle wide or utf strings yet");

  // For raw string literals, this is easy.
  if (SpellingPtr[0] == 'R') {
    assert(SpellingPtr[1] == '"' && "Should be a raw string literal!");
    // Skip 'R"'.
    SpellingPtr += 2;
    while (*SpellingPtr != '(') {
      ++SpellingPtr;
      assert(SpellingPtr < SpellingEnd && "Missing ( for raw string literal");
    }
    // Skip '('.
    ++SpellingPtr;
    return SpellingPtr - SpellingStart + ByteNo;
  }

  // Skip over the leading quote
  assert(SpellingPtr[0] == '"' && "Should be a string literal!");
  ++SpellingPtr;

  // Skip over bytes until we find the offset we're looking for.
  while (ByteNo) {
    assert(SpellingPtr < SpellingEnd && "Didn't find byte offset!");

    // Step over non-escapes simply.
    if (*SpellingPtr != '\\') {
      ++SpellingPtr;
      --ByteNo;
      continue;
    }

    // Otherwise, this is an escape character.  Advance over it.
    bool HadError = false;
    if (SpellingPtr[1] == 'u' || SpellingPtr[1] == 'U' ||
        SpellingPtr[1] == 'N') {
      const char *EscapePtr = SpellingPtr;
      unsigned Len = MeasureUCNEscape(SpellingStart, SpellingPtr, SpellingEnd,
                                      1, Features, HadError);
      if (Len > ByteNo) {
        // ByteNo is somewhere within the escape sequence.
        SpellingPtr = EscapePtr;
        break;
      }
      ByteNo -= Len;
    } else {
      ProcessCharEscape(SpellingStart, SpellingPtr, SpellingEnd, HadError,
                        FullSourceLoc(Tok.getLocation(), SM), CharByteWidth * 8,
                        Diags, Features, StringLiteralEvalMethod::Evaluated);
      --ByteNo;
    }
    assert(!HadError && "This method isn't valid on erroneous strings");
  }

  return SpellingPtr-SpellingStart;
}

/// Determine whether a suffix is a valid ud-suffix. We avoid treating reserved
/// suffixes as ud-suffixes, because the diagnostic experience is better if we
/// treat it as an invalid suffix.
bool StringLiteralParser::isValidUDSuffix(const LangOptions &LangOpts,
                                          StringRef Suffix) {
  return NumericLiteralParser::isValidUDSuffix(LangOpts, Suffix) ||
         Suffix == "sv";
}
