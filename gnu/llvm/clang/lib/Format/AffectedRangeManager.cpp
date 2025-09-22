//===--- AffectedRangeManager.cpp - Format C++ code -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements AffectRangeManager class.
///
//===----------------------------------------------------------------------===//

#include "AffectedRangeManager.h"

#include "FormatToken.h"
#include "TokenAnnotator.h"

namespace clang {
namespace format {

bool AffectedRangeManager::computeAffectedLines(
    SmallVectorImpl<AnnotatedLine *> &Lines) {
  SmallVectorImpl<AnnotatedLine *>::iterator I = Lines.begin();
  SmallVectorImpl<AnnotatedLine *>::iterator E = Lines.end();
  bool SomeLineAffected = false;
  const AnnotatedLine *PreviousLine = nullptr;
  while (I != E) {
    AnnotatedLine *Line = *I;
    assert(Line->First);
    Line->LeadingEmptyLinesAffected = affectsLeadingEmptyLines(*Line->First);

    // If a line is part of a preprocessor directive, it needs to be formatted
    // if any token within the directive is affected.
    if (Line->InPPDirective) {
      FormatToken *Last = Line->Last;
      SmallVectorImpl<AnnotatedLine *>::iterator PPEnd = I + 1;
      while (PPEnd != E && !(*PPEnd)->First->HasUnescapedNewline) {
        Last = (*PPEnd)->Last;
        ++PPEnd;
      }

      if (affectsTokenRange(*Line->First, *Last,
                            /*IncludeLeadingNewlines=*/false)) {
        SomeLineAffected = true;
        markAllAsAffected(I, PPEnd);
      }
      I = PPEnd;
      continue;
    }

    if (nonPPLineAffected(Line, PreviousLine, Lines))
      SomeLineAffected = true;

    PreviousLine = Line;
    ++I;
  }
  return SomeLineAffected;
}

bool AffectedRangeManager::affectsCharSourceRange(
    const CharSourceRange &Range) {
  for (const CharSourceRange &R : Ranges) {
    if (!SourceMgr.isBeforeInTranslationUnit(Range.getEnd(), R.getBegin()) &&
        !SourceMgr.isBeforeInTranslationUnit(R.getEnd(), Range.getBegin())) {
      return true;
    }
  }
  return false;
}

bool AffectedRangeManager::affectsTokenRange(const FormatToken &First,
                                             const FormatToken &Last,
                                             bool IncludeLeadingNewlines) {
  SourceLocation Start = First.WhitespaceRange.getBegin();
  if (!IncludeLeadingNewlines)
    Start = Start.getLocWithOffset(First.LastNewlineOffset);
  SourceLocation End = Last.getStartOfNonWhitespace();
  End = End.getLocWithOffset(Last.TokenText.size());
  CharSourceRange Range = CharSourceRange::getCharRange(Start, End);
  return affectsCharSourceRange(Range);
}

bool AffectedRangeManager::affectsLeadingEmptyLines(const FormatToken &Tok) {
  CharSourceRange EmptyLineRange = CharSourceRange::getCharRange(
      Tok.WhitespaceRange.getBegin(),
      Tok.WhitespaceRange.getBegin().getLocWithOffset(Tok.LastNewlineOffset));
  return affectsCharSourceRange(EmptyLineRange);
}

void AffectedRangeManager::markAllAsAffected(
    SmallVectorImpl<AnnotatedLine *>::iterator I,
    SmallVectorImpl<AnnotatedLine *>::iterator E) {
  while (I != E) {
    (*I)->Affected = true;
    markAllAsAffected((*I)->Children.begin(), (*I)->Children.end());
    ++I;
  }
}

bool AffectedRangeManager::nonPPLineAffected(
    AnnotatedLine *Line, const AnnotatedLine *PreviousLine,
    SmallVectorImpl<AnnotatedLine *> &Lines) {
  bool SomeLineAffected = false;
  Line->ChildrenAffected = computeAffectedLines(Line->Children);
  if (Line->ChildrenAffected)
    SomeLineAffected = true;

  // Stores whether one of the line's tokens is directly affected.
  bool SomeTokenAffected = false;
  // Stores whether we need to look at the leading newlines of the next token
  // in order to determine whether it was affected.
  bool IncludeLeadingNewlines = false;

  // Stores whether the first child line of any of this line's tokens is
  // affected.
  bool SomeFirstChildAffected = false;

  assert(Line->First);
  for (FormatToken *Tok = Line->First; Tok; Tok = Tok->Next) {
    // Determine whether 'Tok' was affected.
    if (affectsTokenRange(*Tok, *Tok, IncludeLeadingNewlines))
      SomeTokenAffected = true;

    // Determine whether the first child of 'Tok' was affected.
    if (!Tok->Children.empty() && Tok->Children.front()->Affected)
      SomeFirstChildAffected = true;

    IncludeLeadingNewlines = Tok->Children.empty();
  }

  // Was this line moved, i.e. has it previously been on the same line as an
  // affected line?
  bool LineMoved = PreviousLine && PreviousLine->Affected &&
                   Line->First->NewlinesBefore == 0;

  bool IsContinuedComment =
      Line->First->is(tok::comment) && !Line->First->Next &&
      Line->First->NewlinesBefore < 2 && PreviousLine &&
      PreviousLine->Affected && PreviousLine->Last->is(tok::comment);

  bool IsAffectedClosingBrace =
      Line->First->is(tok::r_brace) &&
      Line->MatchingOpeningBlockLineIndex != UnwrappedLine::kInvalidIndex &&
      Lines[Line->MatchingOpeningBlockLineIndex]->Affected;

  if (SomeTokenAffected || SomeFirstChildAffected || LineMoved ||
      IsContinuedComment || IsAffectedClosingBrace) {
    Line->Affected = true;
    SomeLineAffected = true;
  }
  return SomeLineAffected;
}

} // namespace format
} // namespace clang
