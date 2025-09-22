//===--- IntegerLiteralSeparatorFixer.cpp -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements IntegerLiteralSeparatorFixer that fixes C++ integer
/// literal separators.
///
//===----------------------------------------------------------------------===//

#include "IntegerLiteralSeparatorFixer.h"

namespace clang {
namespace format {

enum class Base { Binary, Decimal, Hex, Other };

static Base getBase(const StringRef IntegerLiteral) {
  assert(IntegerLiteral.size() > 1);

  if (IntegerLiteral[0] > '0') {
    assert(IntegerLiteral[0] <= '9');
    return Base::Decimal;
  }

  assert(IntegerLiteral[0] == '0');

  switch (IntegerLiteral[1]) {
  case 'b':
  case 'B':
    return Base::Binary;
  case 'x':
  case 'X':
    return Base::Hex;
  default:
    return Base::Other;
  }
}

std::pair<tooling::Replacements, unsigned>
IntegerLiteralSeparatorFixer::process(const Environment &Env,
                                      const FormatStyle &Style) {
  switch (Style.Language) {
  case FormatStyle::LK_Cpp:
  case FormatStyle::LK_ObjC:
    Separator = '\'';
    break;
  case FormatStyle::LK_CSharp:
  case FormatStyle::LK_Java:
  case FormatStyle::LK_JavaScript:
    Separator = '_';
    break;
  default:
    return {};
  }

  const auto &Option = Style.IntegerLiteralSeparator;
  const auto Binary = Option.Binary;
  const auto Decimal = Option.Decimal;
  const auto Hex = Option.Hex;
  const bool SkipBinary = Binary == 0;
  const bool SkipDecimal = Decimal == 0;
  const bool SkipHex = Hex == 0;

  if (SkipBinary && SkipDecimal && SkipHex)
    return {};

  const auto BinaryMinDigits =
      std::max((int)Option.BinaryMinDigits, Binary + 1);
  const auto DecimalMinDigits =
      std::max((int)Option.DecimalMinDigits, Decimal + 1);
  const auto HexMinDigits = std::max((int)Option.HexMinDigits, Hex + 1);

  const auto &SourceMgr = Env.getSourceManager();
  AffectedRangeManager AffectedRangeMgr(SourceMgr, Env.getCharRanges());

  const auto ID = Env.getFileID();
  const auto LangOpts = getFormattingLangOpts(Style);
  Lexer Lex(ID, SourceMgr.getBufferOrFake(ID), SourceMgr, LangOpts);
  Lex.SetCommentRetentionState(true);

  Token Tok;
  tooling::Replacements Result;

  for (bool Skip = false; !Lex.LexFromRawLexer(Tok);) {
    auto Length = Tok.getLength();
    if (Length < 2)
      continue;
    auto Location = Tok.getLocation();
    auto Text = StringRef(SourceMgr.getCharacterData(Location), Length);
    if (Tok.is(tok::comment)) {
      if (isClangFormatOff(Text))
        Skip = true;
      else if (isClangFormatOn(Text))
        Skip = false;
      continue;
    }
    if (Skip || Tok.isNot(tok::numeric_constant) || Text[0] == '.' ||
        !AffectedRangeMgr.affectsCharSourceRange(
            CharSourceRange::getCharRange(Location, Tok.getEndLoc()))) {
      continue;
    }
    const auto B = getBase(Text);
    const bool IsBase2 = B == Base::Binary;
    const bool IsBase10 = B == Base::Decimal;
    const bool IsBase16 = B == Base::Hex;
    if ((IsBase2 && SkipBinary) || (IsBase10 && SkipDecimal) ||
        (IsBase16 && SkipHex) || B == Base::Other) {
      continue;
    }
    if (Style.isCpp()) {
      // Hex alpha digits a-f/A-F must be at the end of the string literal.
      StringRef Suffixes = "_himnsuyd";
      if (const auto Pos =
              Text.find_first_of(IsBase16 ? Suffixes.drop_back() : Suffixes);
          Pos != StringRef::npos) {
        Text = Text.substr(0, Pos);
        Length = Pos;
      }
    }
    if ((IsBase10 && Text.find_last_of(".eEfFdDmM") != StringRef::npos) ||
        (IsBase16 && Text.find_last_of(".pP") != StringRef::npos)) {
      continue;
    }
    const auto Start = Text[0] == '0' ? 2 : 0;
    auto End = Text.find_first_of("uUlLzZn", Start);
    if (End == StringRef::npos)
      End = Length;
    if (Start > 0 || End < Length) {
      Length = End - Start;
      Text = Text.substr(Start, Length);
    }
    auto DigitsPerGroup = Decimal;
    auto MinDigits = DecimalMinDigits;
    if (IsBase2) {
      DigitsPerGroup = Binary;
      MinDigits = BinaryMinDigits;
    } else if (IsBase16) {
      DigitsPerGroup = Hex;
      MinDigits = HexMinDigits;
    }
    const auto SeparatorCount = Text.count(Separator);
    const int DigitCount = Length - SeparatorCount;
    const bool RemoveSeparator = DigitsPerGroup < 0 || DigitCount < MinDigits;
    if (RemoveSeparator && SeparatorCount == 0)
      continue;
    if (!RemoveSeparator && SeparatorCount > 0 &&
        checkSeparator(Text, DigitsPerGroup)) {
      continue;
    }
    const auto &Formatted =
        format(Text, DigitsPerGroup, DigitCount, RemoveSeparator);
    assert(Formatted != Text);
    if (Start > 0)
      Location = Location.getLocWithOffset(Start);
    cantFail(Result.add(
        tooling::Replacement(SourceMgr, Location, Length, Formatted)));
  }

  return {Result, 0};
}

bool IntegerLiteralSeparatorFixer::checkSeparator(
    const StringRef IntegerLiteral, int DigitsPerGroup) const {
  assert(DigitsPerGroup > 0);

  int I = 0;
  for (auto C : llvm::reverse(IntegerLiteral)) {
    if (C == Separator) {
      if (I < DigitsPerGroup)
        return false;
      I = 0;
    } else {
      if (I == DigitsPerGroup)
        return false;
      ++I;
    }
  }

  return true;
}

std::string IntegerLiteralSeparatorFixer::format(const StringRef IntegerLiteral,
                                                 int DigitsPerGroup,
                                                 int DigitCount,
                                                 bool RemoveSeparator) const {
  assert(DigitsPerGroup != 0);

  std::string Formatted;

  if (RemoveSeparator) {
    for (auto C : IntegerLiteral)
      if (C != Separator)
        Formatted.push_back(C);
    return Formatted;
  }

  int Remainder = DigitCount % DigitsPerGroup;

  int I = 0;
  for (auto C : IntegerLiteral) {
    if (C == Separator)
      continue;
    if (I == (Remainder > 0 ? Remainder : DigitsPerGroup)) {
      Formatted.push_back(Separator);
      I = 0;
      Remainder = 0;
    }
    Formatted.push_back(C);
    ++I;
  }

  return Formatted;
}

} // namespace format
} // namespace clang
