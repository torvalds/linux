//===--- DefinitionBlockSeparator.cpp ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements DefinitionBlockSeparator, a TokenAnalyzer that inserts
/// or removes empty lines separating definition blocks like classes, structs,
/// functions, enums, and namespaces in between.
///
//===----------------------------------------------------------------------===//

#include "DefinitionBlockSeparator.h"
#include "llvm/Support/Debug.h"
#define DEBUG_TYPE "definition-block-separator"

namespace clang {
namespace format {
std::pair<tooling::Replacements, unsigned> DefinitionBlockSeparator::analyze(
    TokenAnnotator &Annotator, SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
    FormatTokenLexer &Tokens) {
  assert(Style.SeparateDefinitionBlocks != FormatStyle::SDS_Leave);
  AffectedRangeMgr.computeAffectedLines(AnnotatedLines);
  tooling::Replacements Result;
  separateBlocks(AnnotatedLines, Result, Tokens);
  return {Result, 0};
}

void DefinitionBlockSeparator::separateBlocks(
    SmallVectorImpl<AnnotatedLine *> &Lines, tooling::Replacements &Result,
    FormatTokenLexer &Tokens) {
  const bool IsNeverStyle =
      Style.SeparateDefinitionBlocks == FormatStyle::SDS_Never;
  const AdditionalKeywords &ExtraKeywords = Tokens.getKeywords();
  auto GetBracketLevelChange = [](const FormatToken *Tok) {
    if (Tok->isOneOf(tok::l_brace, tok::l_paren, tok::l_square))
      return 1;
    if (Tok->isOneOf(tok::r_brace, tok::r_paren, tok::r_square))
      return -1;
    return 0;
  };
  auto LikelyDefinition = [&](const AnnotatedLine *Line,
                              bool ExcludeEnum = false) {
    if ((Line->MightBeFunctionDecl && Line->mightBeFunctionDefinition()) ||
        Line->startsWithNamespace()) {
      return true;
    }
    int BracketLevel = 0;
    for (const FormatToken *CurrentToken = Line->First; CurrentToken;
         CurrentToken = CurrentToken->Next) {
      if (BracketLevel == 0) {
        if (CurrentToken->isOneOf(tok::kw_class, tok::kw_struct,
                                  tok::kw_union) ||
            (Style.isJavaScript() &&
             CurrentToken->is(ExtraKeywords.kw_function))) {
          return true;
        }
        if (!ExcludeEnum && CurrentToken->is(tok::kw_enum))
          return true;
      }
      BracketLevel += GetBracketLevelChange(CurrentToken);
    }
    return false;
  };
  unsigned NewlineCount =
      (Style.SeparateDefinitionBlocks == FormatStyle::SDS_Always ? 1 : 0) + 1;
  WhitespaceManager Whitespaces(
      Env.getSourceManager(), Style,
      Style.LineEnding > FormatStyle::LE_CRLF
          ? WhitespaceManager::inputUsesCRLF(
                Env.getSourceManager().getBufferData(Env.getFileID()),
                Style.LineEnding == FormatStyle::LE_DeriveCRLF)
          : Style.LineEnding == FormatStyle::LE_CRLF);
  for (unsigned I = 0; I < Lines.size(); ++I) {
    const auto &CurrentLine = Lines[I];
    if (CurrentLine->InPPDirective)
      continue;
    FormatToken *TargetToken = nullptr;
    AnnotatedLine *TargetLine;
    auto OpeningLineIndex = CurrentLine->MatchingOpeningBlockLineIndex;
    AnnotatedLine *OpeningLine = nullptr;
    const auto IsAccessSpecifierToken = [](const FormatToken *Token) {
      return Token->isAccessSpecifier() || Token->isObjCAccessSpecifier();
    };
    const auto InsertReplacement = [&](const int NewlineToInsert) {
      assert(TargetLine);
      assert(TargetToken);

      // Do not handle EOF newlines.
      if (TargetToken->is(tok::eof))
        return;
      if (IsAccessSpecifierToken(TargetToken) ||
          (OpeningLineIndex > 0 &&
           IsAccessSpecifierToken(Lines[OpeningLineIndex - 1]->First))) {
        return;
      }
      if (!TargetLine->Affected)
        return;
      Whitespaces.replaceWhitespace(*TargetToken, NewlineToInsert,
                                    TargetToken->OriginalColumn,
                                    TargetToken->OriginalColumn);
    };
    const auto IsPPConditional = [&](const size_t LineIndex) {
      const auto &Line = Lines[LineIndex];
      return Line->First->is(tok::hash) && Line->First->Next &&
             Line->First->Next->isOneOf(tok::pp_if, tok::pp_ifdef, tok::pp_else,
                                        tok::pp_ifndef, tok::pp_elifndef,
                                        tok::pp_elifdef, tok::pp_elif,
                                        tok::pp_endif);
    };
    const auto FollowingOtherOpening = [&]() {
      return OpeningLineIndex == 0 ||
             Lines[OpeningLineIndex - 1]->Last->opensScope() ||
             IsPPConditional(OpeningLineIndex - 1);
    };
    const auto HasEnumOnLine = [&]() {
      bool FoundEnumKeyword = false;
      int BracketLevel = 0;
      for (const FormatToken *CurrentToken = CurrentLine->First; CurrentToken;
           CurrentToken = CurrentToken->Next) {
        if (BracketLevel == 0) {
          if (CurrentToken->is(tok::kw_enum))
            FoundEnumKeyword = true;
          else if (FoundEnumKeyword && CurrentToken->is(tok::l_brace))
            return true;
        }
        BracketLevel += GetBracketLevelChange(CurrentToken);
      }
      return FoundEnumKeyword && I + 1 < Lines.size() &&
             Lines[I + 1]->First->is(tok::l_brace);
    };

    bool IsDefBlock = false;
    const auto MayPrecedeDefinition = [&](const int Direction = -1) {
      assert(Direction >= -1);
      assert(Direction <= 1);
      const size_t OperateIndex = OpeningLineIndex + Direction;
      assert(OperateIndex < Lines.size());
      const auto &OperateLine = Lines[OperateIndex];
      if (LikelyDefinition(OperateLine))
        return false;

      if (const auto *Tok = OperateLine->First;
          Tok->is(tok::comment) && !isClangFormatOn(Tok->TokenText)) {
        return true;
      }

      // A single line identifier that is not in the last line.
      if (OperateLine->First->is(tok::identifier) &&
          OperateLine->First == OperateLine->Last &&
          OperateIndex + 1 < Lines.size()) {
        // UnwrappedLineParser's recognition of free-standing macro like
        // Q_OBJECT may also recognize some uppercased type names that may be
        // used as return type as that kind of macros, which is a bit hard to
        // distinguish one from another purely from token patterns. Here, we
        // try not to add new lines below those identifiers.
        AnnotatedLine *NextLine = Lines[OperateIndex + 1];
        if (NextLine->MightBeFunctionDecl &&
            NextLine->mightBeFunctionDefinition() &&
            NextLine->First->NewlinesBefore == 1 &&
            OperateLine->First->is(TT_FunctionLikeOrFreestandingMacro)) {
          return true;
        }
      }

      if (Style.isCSharp() && OperateLine->First->is(TT_AttributeSquare))
        return true;
      return false;
    };

    if (HasEnumOnLine() &&
        !LikelyDefinition(CurrentLine, /*ExcludeEnum=*/true)) {
      // We have no scope opening/closing information for enum.
      IsDefBlock = true;
      OpeningLineIndex = I;
      while (OpeningLineIndex > 0 && MayPrecedeDefinition())
        --OpeningLineIndex;
      OpeningLine = Lines[OpeningLineIndex];
      TargetLine = OpeningLine;
      TargetToken = TargetLine->First;
      if (!FollowingOtherOpening())
        InsertReplacement(NewlineCount);
      else if (IsNeverStyle)
        InsertReplacement(OpeningLineIndex != 0);
      TargetLine = CurrentLine;
      TargetToken = TargetLine->First;
      while (TargetToken && TargetToken->isNot(tok::r_brace))
        TargetToken = TargetToken->Next;
      if (!TargetToken)
        while (I < Lines.size() && Lines[I]->First->isNot(tok::r_brace))
          ++I;
    } else if (CurrentLine->First->closesScope()) {
      if (OpeningLineIndex > Lines.size())
        continue;
      // Handling the case that opening brace has its own line, with checking
      // whether the last line already had an opening brace to guard against
      // misrecognition.
      if (OpeningLineIndex > 0 &&
          Lines[OpeningLineIndex]->First->is(tok::l_brace) &&
          Lines[OpeningLineIndex - 1]->Last->isNot(tok::l_brace)) {
        --OpeningLineIndex;
      }
      OpeningLine = Lines[OpeningLineIndex];
      // Closing a function definition.
      if (LikelyDefinition(OpeningLine)) {
        IsDefBlock = true;
        while (OpeningLineIndex > 0 && MayPrecedeDefinition())
          --OpeningLineIndex;
        OpeningLine = Lines[OpeningLineIndex];
        TargetLine = OpeningLine;
        TargetToken = TargetLine->First;
        if (!FollowingOtherOpening()) {
          // Avoid duplicated replacement.
          if (TargetToken->isNot(tok::l_brace))
            InsertReplacement(NewlineCount);
        } else if (IsNeverStyle) {
          InsertReplacement(OpeningLineIndex != 0);
        }
      }
    }

    // Not the last token.
    if (IsDefBlock && I + 1 < Lines.size()) {
      OpeningLineIndex = I + 1;
      TargetLine = Lines[OpeningLineIndex];
      TargetToken = TargetLine->First;

      // No empty line for continuously closing scopes. The token will be
      // handled in another case if the line following is opening a
      // definition.
      if (!TargetToken->closesScope() && !IsPPConditional(OpeningLineIndex)) {
        // Check whether current line may precede a definition line.
        while (OpeningLineIndex + 1 < Lines.size() &&
               MayPrecedeDefinition(/*Direction=*/0)) {
          ++OpeningLineIndex;
        }
        TargetLine = Lines[OpeningLineIndex];
        if (!LikelyDefinition(TargetLine)) {
          OpeningLineIndex = I + 1;
          TargetLine = Lines[I + 1];
          TargetToken = TargetLine->First;
          InsertReplacement(NewlineCount);
        }
      } else if (IsNeverStyle) {
        InsertReplacement(/*NewlineToInsert=*/1);
      }
    }
  }
  for (const auto &R : Whitespaces.generateReplacements()) {
    // The add method returns an Error instance which simulates program exit
    // code through overloading boolean operator, thus false here indicates
    // success.
    if (Result.add(R))
      return;
  }
}
} // namespace format
} // namespace clang
