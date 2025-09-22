//===--- UnwrappedLineFormatter.cpp - Format C++ code ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UnwrappedLineFormatter.h"
#include "FormatToken.h"
#include "NamespaceEndCommentsFixer.h"
#include "WhitespaceManager.h"
#include "llvm/Support/Debug.h"
#include <queue>

#define DEBUG_TYPE "format-formatter"

namespace clang {
namespace format {

namespace {

bool startsExternCBlock(const AnnotatedLine &Line) {
  const FormatToken *Next = Line.First->getNextNonComment();
  const FormatToken *NextNext = Next ? Next->getNextNonComment() : nullptr;
  return Line.startsWith(tok::kw_extern) && Next && Next->isStringLiteral() &&
         NextNext && NextNext->is(tok::l_brace);
}

bool isRecordLBrace(const FormatToken &Tok) {
  return Tok.isOneOf(TT_ClassLBrace, TT_EnumLBrace, TT_RecordLBrace,
                     TT_StructLBrace, TT_UnionLBrace);
}

/// Tracks the indent level of \c AnnotatedLines across levels.
///
/// \c nextLine must be called for each \c AnnotatedLine, after which \c
/// getIndent() will return the indent for the last line \c nextLine was called
/// with.
/// If the line is not formatted (and thus the indent does not change), calling
/// \c adjustToUnmodifiedLine after the call to \c nextLine will cause
/// subsequent lines on the same level to be indented at the same level as the
/// given line.
class LevelIndentTracker {
public:
  LevelIndentTracker(const FormatStyle &Style,
                     const AdditionalKeywords &Keywords, unsigned StartLevel,
                     int AdditionalIndent)
      : Style(Style), Keywords(Keywords), AdditionalIndent(AdditionalIndent) {
    for (unsigned i = 0; i != StartLevel; ++i)
      IndentForLevel.push_back(Style.IndentWidth * i + AdditionalIndent);
  }

  /// Returns the indent for the current line.
  unsigned getIndent() const { return Indent; }

  /// Update the indent state given that \p Line is going to be formatted
  /// next.
  void nextLine(const AnnotatedLine &Line) {
    Offset = getIndentOffset(Line);
    // Update the indent level cache size so that we can rely on it
    // having the right size in adjustToUnmodifiedline.
    if (Line.Level >= IndentForLevel.size())
      IndentForLevel.resize(Line.Level + 1, -1);
    if (Style.IndentPPDirectives != FormatStyle::PPDIS_None &&
        (Line.InPPDirective ||
         (Style.IndentPPDirectives == FormatStyle::PPDIS_BeforeHash &&
          Line.Type == LT_CommentAbovePPDirective))) {
      unsigned PPIndentWidth =
          (Style.PPIndentWidth >= 0) ? Style.PPIndentWidth : Style.IndentWidth;
      Indent = Line.InMacroBody
                   ? Line.PPLevel * PPIndentWidth +
                         (Line.Level - Line.PPLevel) * Style.IndentWidth
                   : Line.Level * PPIndentWidth;
      Indent += AdditionalIndent;
    } else {
      // When going to lower levels, forget previous higher levels so that we
      // recompute future higher levels. But don't forget them if we enter a PP
      // directive, since these do not terminate a C++ code block.
      if (!Line.InPPDirective) {
        assert(Line.Level <= IndentForLevel.size());
        IndentForLevel.resize(Line.Level + 1);
      }
      Indent = getIndent(Line.Level);
    }
    if (static_cast<int>(Indent) + Offset >= 0)
      Indent += Offset;
    if (Line.IsContinuation)
      Indent = Line.Level * Style.IndentWidth + Style.ContinuationIndentWidth;
  }

  /// Update the level indent to adapt to the given \p Line.
  ///
  /// When a line is not formatted, we move the subsequent lines on the same
  /// level to the same indent.
  /// Note that \c nextLine must have been called before this method.
  void adjustToUnmodifiedLine(const AnnotatedLine &Line) {
    if (Line.InPPDirective || Line.IsContinuation)
      return;
    assert(Line.Level < IndentForLevel.size());
    if (Line.First->is(tok::comment) && IndentForLevel[Line.Level] != -1)
      return;
    unsigned LevelIndent = Line.First->OriginalColumn;
    if (static_cast<int>(LevelIndent) - Offset >= 0)
      LevelIndent -= Offset;
    IndentForLevel[Line.Level] = LevelIndent;
  }

private:
  /// Get the offset of the line relatively to the level.
  ///
  /// For example, 'public:' labels in classes are offset by 1 or 2
  /// characters to the left from their level.
  int getIndentOffset(const AnnotatedLine &Line) {
    if (Style.Language == FormatStyle::LK_Java || Style.isJavaScript() ||
        Style.isCSharp()) {
      return 0;
    }

    auto IsAccessModifier = [&](const FormatToken &RootToken) {
      if (Line.Type == LT_AccessModifier || RootToken.isObjCAccessSpecifier())
        return true;

      const auto *Next = RootToken.Next;

      // Handle Qt signals.
      if (RootToken.isOneOf(Keywords.kw_signals, Keywords.kw_qsignals) &&
          Next && Next->is(tok::colon)) {
        return true;
      }

      if (Next && Next->isOneOf(Keywords.kw_slots, Keywords.kw_qslots) &&
          Next->Next && Next->Next->is(tok::colon)) {
        return true;
      }

      // Handle malformed access specifier e.g. 'private' without trailing ':'.
      return !Next && RootToken.isAccessSpecifier(false);
    };

    if (IsAccessModifier(*Line.First)) {
      // The AccessModifierOffset may be overridden by IndentAccessModifiers,
      // in which case we take a negative value of the IndentWidth to simulate
      // the upper indent level.
      return Style.IndentAccessModifiers ? -Style.IndentWidth
                                         : Style.AccessModifierOffset;
    }

    return 0;
  }

  /// Get the indent of \p Level from \p IndentForLevel.
  ///
  /// \p IndentForLevel must contain the indent for the level \c l
  /// at \p IndentForLevel[l], or a value < 0 if the indent for
  /// that level is unknown.
  unsigned getIndent(unsigned Level) const {
    assert(Level < IndentForLevel.size());
    if (IndentForLevel[Level] != -1)
      return IndentForLevel[Level];
    if (Level == 0)
      return 0;
    return getIndent(Level - 1) + Style.IndentWidth;
  }

  const FormatStyle &Style;
  const AdditionalKeywords &Keywords;
  const unsigned AdditionalIndent;

  /// The indent in characters for each level. It remembers the indent of
  /// previous lines (that are not PP directives) of equal or lower levels. This
  /// is used to align formatted lines to the indent of previous non-formatted
  /// lines. Think about the --lines parameter of clang-format.
  SmallVector<int> IndentForLevel;

  /// Offset of the current line relative to the indent level.
  ///
  /// For example, the 'public' keywords is often indented with a negative
  /// offset.
  int Offset = 0;

  /// The current line's indent.
  unsigned Indent = 0;
};

const FormatToken *getMatchingNamespaceToken(
    const AnnotatedLine *Line,
    const SmallVectorImpl<AnnotatedLine *> &AnnotatedLines) {
  if (!Line->startsWith(tok::r_brace))
    return nullptr;
  size_t StartLineIndex = Line->MatchingOpeningBlockLineIndex;
  if (StartLineIndex == UnwrappedLine::kInvalidIndex)
    return nullptr;
  assert(StartLineIndex < AnnotatedLines.size());
  return AnnotatedLines[StartLineIndex]->First->getNamespaceToken();
}

StringRef getNamespaceTokenText(const AnnotatedLine *Line) {
  const FormatToken *NamespaceToken = Line->First->getNamespaceToken();
  return NamespaceToken ? NamespaceToken->TokenText : StringRef();
}

StringRef getMatchingNamespaceTokenText(
    const AnnotatedLine *Line,
    const SmallVectorImpl<AnnotatedLine *> &AnnotatedLines) {
  const FormatToken *NamespaceToken =
      getMatchingNamespaceToken(Line, AnnotatedLines);
  return NamespaceToken ? NamespaceToken->TokenText : StringRef();
}

class LineJoiner {
public:
  LineJoiner(const FormatStyle &Style, const AdditionalKeywords &Keywords,
             const SmallVectorImpl<AnnotatedLine *> &Lines)
      : Style(Style), Keywords(Keywords), End(Lines.end()), Next(Lines.begin()),
        AnnotatedLines(Lines) {}

  /// Returns the next line, merging multiple lines into one if possible.
  const AnnotatedLine *getNextMergedLine(bool DryRun,
                                         LevelIndentTracker &IndentTracker) {
    if (Next == End)
      return nullptr;
    const AnnotatedLine *Current = *Next;
    IndentTracker.nextLine(*Current);
    unsigned MergedLines = tryFitMultipleLinesInOne(IndentTracker, Next, End);
    if (MergedLines > 0 && Style.ColumnLimit == 0) {
      // Disallow line merging if there is a break at the start of one of the
      // input lines.
      for (unsigned i = 0; i < MergedLines; ++i)
        if (Next[i + 1]->First->NewlinesBefore > 0)
          MergedLines = 0;
    }
    if (!DryRun)
      for (unsigned i = 0; i < MergedLines; ++i)
        join(*Next[0], *Next[i + 1]);
    Next = Next + MergedLines + 1;
    return Current;
  }

private:
  /// Calculates how many lines can be merged into 1 starting at \p I.
  unsigned
  tryFitMultipleLinesInOne(LevelIndentTracker &IndentTracker,
                           SmallVectorImpl<AnnotatedLine *>::const_iterator I,
                           SmallVectorImpl<AnnotatedLine *>::const_iterator E) {
    const unsigned Indent = IndentTracker.getIndent();

    // Can't join the last line with anything.
    if (I + 1 == E)
      return 0;
    // We can never merge stuff if there are trailing line comments.
    const AnnotatedLine *TheLine = *I;
    if (TheLine->Last->is(TT_LineComment))
      return 0;
    const auto &NextLine = *I[1];
    if (NextLine.Type == LT_Invalid || NextLine.First->MustBreakBefore)
      return 0;
    if (TheLine->InPPDirective &&
        (!NextLine.InPPDirective || NextLine.First->HasUnescapedNewline)) {
      return 0;
    }

    if (Style.ColumnLimit > 0 && Indent > Style.ColumnLimit)
      return 0;

    unsigned Limit =
        Style.ColumnLimit == 0 ? UINT_MAX : Style.ColumnLimit - Indent;
    // If we already exceed the column limit, we set 'Limit' to 0. The different
    // tryMerge..() functions can then decide whether to still do merging.
    Limit = TheLine->Last->TotalLength > Limit
                ? 0
                : Limit - TheLine->Last->TotalLength;

    if (TheLine->Last->is(TT_FunctionLBrace) &&
        TheLine->First == TheLine->Last &&
        !Style.BraceWrapping.SplitEmptyFunction &&
        NextLine.First->is(tok::r_brace)) {
      return tryMergeSimpleBlock(I, E, Limit);
    }

    const auto *PreviousLine = I != AnnotatedLines.begin() ? I[-1] : nullptr;
    // Handle empty record blocks where the brace has already been wrapped.
    if (PreviousLine && TheLine->Last->is(tok::l_brace) &&
        TheLine->First == TheLine->Last) {
      bool EmptyBlock = NextLine.First->is(tok::r_brace);

      const FormatToken *Tok = PreviousLine->First;
      if (Tok && Tok->is(tok::comment))
        Tok = Tok->getNextNonComment();

      if (Tok && Tok->getNamespaceToken()) {
        return !Style.BraceWrapping.SplitEmptyNamespace && EmptyBlock
                   ? tryMergeSimpleBlock(I, E, Limit)
                   : 0;
      }

      if (Tok && Tok->is(tok::kw_typedef))
        Tok = Tok->getNextNonComment();
      if (Tok && Tok->isOneOf(tok::kw_class, tok::kw_struct, tok::kw_union,
                              tok::kw_extern, Keywords.kw_interface)) {
        return !Style.BraceWrapping.SplitEmptyRecord && EmptyBlock
                   ? tryMergeSimpleBlock(I, E, Limit)
                   : 0;
      }

      if (Tok && Tok->is(tok::kw_template) &&
          Style.BraceWrapping.SplitEmptyRecord && EmptyBlock) {
        return 0;
      }
    }

    auto ShouldMergeShortFunctions = [this, &I, &NextLine, PreviousLine,
                                      TheLine]() {
      if (Style.AllowShortFunctionsOnASingleLine == FormatStyle::SFS_All)
        return true;
      if (Style.AllowShortFunctionsOnASingleLine >= FormatStyle::SFS_Empty &&
          NextLine.First->is(tok::r_brace)) {
        return true;
      }

      if (Style.AllowShortFunctionsOnASingleLine &
          FormatStyle::SFS_InlineOnly) {
        // Just checking TheLine->Level != 0 is not enough, because it
        // provokes treating functions inside indented namespaces as short.
        if (Style.isJavaScript() && TheLine->Last->is(TT_FunctionLBrace))
          return true;

        if (TheLine->Level != 0) {
          if (!PreviousLine)
            return false;

          // TODO: Use IndentTracker to avoid loop?
          // Find the last line with lower level.
          const AnnotatedLine *Line = nullptr;
          for (auto J = I - 1; J >= AnnotatedLines.begin(); --J) {
            assert(*J);
            if (!(*J)->InPPDirective && !(*J)->isComment() &&
                (*J)->Level < TheLine->Level) {
              Line = *J;
              break;
            }
          }

          if (!Line)
            return false;

          // Check if the found line starts a record.
          const auto *LastNonComment = Line->getLastNonComment();
          // There must be another token (usually `{`), because we chose a
          // non-PPDirective and non-comment line that has a smaller level.
          assert(LastNonComment);
          return isRecordLBrace(*LastNonComment);
        }
      }

      return false;
    };

    bool MergeShortFunctions = ShouldMergeShortFunctions();

    const auto *FirstNonComment = TheLine->getFirstNonComment();
    if (!FirstNonComment)
      return 0;
    // FIXME: There are probably cases where we should use FirstNonComment
    // instead of TheLine->First.

    if (Style.CompactNamespaces) {
      if (const auto *NSToken = TheLine->First->getNamespaceToken()) {
        int J = 1;
        assert(TheLine->MatchingClosingBlockLineIndex > 0);
        for (auto ClosingLineIndex = TheLine->MatchingClosingBlockLineIndex - 1;
             I + J != E && NSToken->TokenText == getNamespaceTokenText(I[J]) &&
             ClosingLineIndex == I[J]->MatchingClosingBlockLineIndex &&
             I[J]->Last->TotalLength < Limit;
             ++J, --ClosingLineIndex) {
          Limit -= I[J]->Last->TotalLength;

          // Reduce indent level for bodies of namespaces which were compacted,
          // but only if their content was indented in the first place.
          auto *ClosingLine = AnnotatedLines.begin() + ClosingLineIndex + 1;
          const int OutdentBy = I[J]->Level - TheLine->Level;
          assert(OutdentBy >= 0);
          for (auto *CompactedLine = I + J; CompactedLine <= ClosingLine;
               ++CompactedLine) {
            if (!(*CompactedLine)->InPPDirective) {
              const int Level = (*CompactedLine)->Level;
              (*CompactedLine)->Level = std::max(Level - OutdentBy, 0);
            }
          }
        }
        return J - 1;
      }

      if (auto nsToken = getMatchingNamespaceToken(TheLine, AnnotatedLines)) {
        int i = 0;
        unsigned openingLine = TheLine->MatchingOpeningBlockLineIndex - 1;
        for (; I + 1 + i != E &&
               nsToken->TokenText ==
                   getMatchingNamespaceTokenText(I[i + 1], AnnotatedLines) &&
               openingLine == I[i + 1]->MatchingOpeningBlockLineIndex;
             i++, --openingLine) {
          // No space between consecutive braces.
          I[i + 1]->First->SpacesRequiredBefore =
              I[i]->Last->isNot(tok::r_brace);

          // Indent like the outer-most namespace.
          IndentTracker.nextLine(*I[i + 1]);
        }
        return i;
      }
    }

    const auto *LastNonComment = TheLine->getLastNonComment();
    assert(LastNonComment);
    // FIXME: There are probably cases where we should use LastNonComment
    // instead of TheLine->Last.

    // Try to merge a function block with left brace unwrapped.
    if (LastNonComment->is(TT_FunctionLBrace) &&
        TheLine->First != LastNonComment) {
      return MergeShortFunctions ? tryMergeSimpleBlock(I, E, Limit) : 0;
    }
    // Try to merge a control statement block with left brace unwrapped.
    if (TheLine->Last->is(tok::l_brace) && FirstNonComment != TheLine->Last &&
        FirstNonComment->isOneOf(tok::kw_if, tok::kw_while, tok::kw_for,
                                 TT_ForEachMacro)) {
      return Style.AllowShortBlocksOnASingleLine != FormatStyle::SBS_Never
                 ? tryMergeSimpleBlock(I, E, Limit)
                 : 0;
    }
    // Try to merge a control statement block with left brace wrapped.
    if (NextLine.First->is(tok::l_brace)) {
      if ((TheLine->First->isOneOf(tok::kw_if, tok::kw_else, tok::kw_while,
                                   tok::kw_for, tok::kw_switch, tok::kw_try,
                                   tok::kw_do, TT_ForEachMacro) ||
           (TheLine->First->is(tok::r_brace) && TheLine->First->Next &&
            TheLine->First->Next->isOneOf(tok::kw_else, tok::kw_catch))) &&
          Style.BraceWrapping.AfterControlStatement ==
              FormatStyle::BWACS_MultiLine) {
        // If possible, merge the next line's wrapped left brace with the
        // current line. Otherwise, leave it on the next line, as this is a
        // multi-line control statement.
        return (Style.ColumnLimit == 0 || TheLine->Level * Style.IndentWidth +
                                                  TheLine->Last->TotalLength <=
                                              Style.ColumnLimit)
                   ? 1
                   : 0;
      }
      if (TheLine->First->isOneOf(tok::kw_if, tok::kw_else, tok::kw_while,
                                  tok::kw_for, TT_ForEachMacro)) {
        return (Style.BraceWrapping.AfterControlStatement ==
                FormatStyle::BWACS_Always)
                   ? tryMergeSimpleBlock(I, E, Limit)
                   : 0;
      }
      if (TheLine->First->isOneOf(tok::kw_else, tok::kw_catch) &&
          Style.BraceWrapping.AfterControlStatement ==
              FormatStyle::BWACS_MultiLine) {
        // This case if different from the upper BWACS_MultiLine processing
        // in that a preceding r_brace is not on the same line as else/catch
        // most likely because of BeforeElse/BeforeCatch set to true.
        // If the line length doesn't fit ColumnLimit, leave l_brace on the
        // next line to respect the BWACS_MultiLine.
        return (Style.ColumnLimit == 0 ||
                TheLine->Last->TotalLength <= Style.ColumnLimit)
                   ? 1
                   : 0;
      }
    }
    if (PreviousLine && TheLine->First->is(tok::l_brace)) {
      switch (PreviousLine->First->Tok.getKind()) {
      case tok::at:
        // Don't merge block with left brace wrapped after ObjC special blocks.
        if (PreviousLine->First->Next) {
          tok::ObjCKeywordKind kwId =
              PreviousLine->First->Next->Tok.getObjCKeywordID();
          if (kwId == tok::objc_autoreleasepool ||
              kwId == tok::objc_synchronized) {
            return 0;
          }
        }
        break;

      case tok::kw_case:
      case tok::kw_default:
        // Don't merge block with left brace wrapped after case labels.
        return 0;

      default:
        break;
      }
    }

    // Don't merge an empty template class or struct if SplitEmptyRecords
    // is defined.
    if (PreviousLine && Style.BraceWrapping.SplitEmptyRecord &&
        TheLine->Last->is(tok::l_brace) && PreviousLine->Last) {
      const FormatToken *Previous = PreviousLine->Last;
      if (Previous) {
        if (Previous->is(tok::comment))
          Previous = Previous->getPreviousNonComment();
        if (Previous) {
          if (Previous->is(tok::greater) && !PreviousLine->InPPDirective)
            return 0;
          if (Previous->is(tok::identifier)) {
            const FormatToken *PreviousPrevious =
                Previous->getPreviousNonComment();
            if (PreviousPrevious &&
                PreviousPrevious->isOneOf(tok::kw_class, tok::kw_struct)) {
              return 0;
            }
          }
        }
      }
    }

    if (TheLine->First->is(TT_SwitchExpressionLabel)) {
      return Style.AllowShortCaseExpressionOnASingleLine
                 ? tryMergeShortCaseLabels(I, E, Limit)
                 : 0;
    }

    if (TheLine->Last->is(tok::l_brace)) {
      bool ShouldMerge = false;
      // Try to merge records.
      if (TheLine->Last->is(TT_EnumLBrace)) {
        ShouldMerge = Style.AllowShortEnumsOnASingleLine;
      } else if (TheLine->Last->is(TT_RequiresExpressionLBrace)) {
        ShouldMerge = Style.AllowShortCompoundRequirementOnASingleLine;
      } else if (TheLine->Last->isOneOf(TT_ClassLBrace, TT_StructLBrace)) {
        // NOTE: We use AfterClass (whereas AfterStruct exists) for both classes
        // and structs, but it seems that wrapping is still handled correctly
        // elsewhere.
        ShouldMerge = !Style.BraceWrapping.AfterClass ||
                      (NextLine.First->is(tok::r_brace) &&
                       !Style.BraceWrapping.SplitEmptyRecord);
      } else if (TheLine->InPPDirective ||
                 !TheLine->First->isOneOf(tok::kw_class, tok::kw_enum,
                                          tok::kw_struct)) {
        // Try to merge a block with left brace unwrapped that wasn't yet
        // covered.
        ShouldMerge = !Style.BraceWrapping.AfterFunction ||
                      (NextLine.First->is(tok::r_brace) &&
                       !Style.BraceWrapping.SplitEmptyFunction);
      }
      return ShouldMerge ? tryMergeSimpleBlock(I, E, Limit) : 0;
    }

    // Try to merge a function block with left brace wrapped.
    if (NextLine.First->is(TT_FunctionLBrace) &&
        Style.BraceWrapping.AfterFunction) {
      if (NextLine.Last->is(TT_LineComment))
        return 0;

      // Check for Limit <= 2 to account for the " {".
      if (Limit <= 2 || (Style.ColumnLimit == 0 && containsMustBreak(TheLine)))
        return 0;
      Limit -= 2;

      unsigned MergedLines = 0;
      if (MergeShortFunctions ||
          (Style.AllowShortFunctionsOnASingleLine >= FormatStyle::SFS_Empty &&
           NextLine.First == NextLine.Last && I + 2 != E &&
           I[2]->First->is(tok::r_brace))) {
        MergedLines = tryMergeSimpleBlock(I + 1, E, Limit);
        // If we managed to merge the block, count the function header, which is
        // on a separate line.
        if (MergedLines > 0)
          ++MergedLines;
      }
      return MergedLines;
    }
    auto IsElseLine = [&TheLine]() -> bool {
      const FormatToken *First = TheLine->First;
      if (First->is(tok::kw_else))
        return true;

      return First->is(tok::r_brace) && First->Next &&
             First->Next->is(tok::kw_else);
    };
    if (TheLine->First->is(tok::kw_if) ||
        (IsElseLine() && (Style.AllowShortIfStatementsOnASingleLine ==
                          FormatStyle::SIS_AllIfsAndElse))) {
      return Style.AllowShortIfStatementsOnASingleLine
                 ? tryMergeSimpleControlStatement(I, E, Limit)
                 : 0;
    }
    if (TheLine->First->isOneOf(tok::kw_for, tok::kw_while, tok::kw_do,
                                TT_ForEachMacro)) {
      return Style.AllowShortLoopsOnASingleLine
                 ? tryMergeSimpleControlStatement(I, E, Limit)
                 : 0;
    }
    if (TheLine->First->isOneOf(tok::kw_case, tok::kw_default)) {
      return Style.AllowShortCaseLabelsOnASingleLine
                 ? tryMergeShortCaseLabels(I, E, Limit)
                 : 0;
    }
    if (TheLine->InPPDirective &&
        (TheLine->First->HasUnescapedNewline || TheLine->First->IsFirst)) {
      return tryMergeSimplePPDirective(I, E, Limit);
    }
    return 0;
  }

  unsigned
  tryMergeSimplePPDirective(SmallVectorImpl<AnnotatedLine *>::const_iterator I,
                            SmallVectorImpl<AnnotatedLine *>::const_iterator E,
                            unsigned Limit) {
    if (Limit == 0)
      return 0;
    if (I + 2 != E && I[2]->InPPDirective && !I[2]->First->HasUnescapedNewline)
      return 0;
    if (1 + I[1]->Last->TotalLength > Limit)
      return 0;
    return 1;
  }

  unsigned tryMergeSimpleControlStatement(
      SmallVectorImpl<AnnotatedLine *>::const_iterator I,
      SmallVectorImpl<AnnotatedLine *>::const_iterator E, unsigned Limit) {
    if (Limit == 0)
      return 0;
    if (Style.BraceWrapping.AfterControlStatement ==
            FormatStyle::BWACS_Always &&
        I[1]->First->is(tok::l_brace) &&
        Style.AllowShortBlocksOnASingleLine == FormatStyle::SBS_Never) {
      return 0;
    }
    if (I[1]->InPPDirective != (*I)->InPPDirective ||
        (I[1]->InPPDirective && I[1]->First->HasUnescapedNewline)) {
      return 0;
    }
    Limit = limitConsideringMacros(I + 1, E, Limit);
    AnnotatedLine &Line = **I;
    if (Line.First->isNot(tok::kw_do) && Line.First->isNot(tok::kw_else) &&
        Line.Last->isNot(tok::kw_else) && Line.Last->isNot(tok::r_paren)) {
      return 0;
    }
    // Only merge `do while` if `do` is the only statement on the line.
    if (Line.First->is(tok::kw_do) && Line.Last->isNot(tok::kw_do))
      return 0;
    if (1 + I[1]->Last->TotalLength > Limit)
      return 0;
    // Don't merge with loops, ifs, a single semicolon or a line comment.
    if (I[1]->First->isOneOf(tok::semi, tok::kw_if, tok::kw_for, tok::kw_while,
                             TT_ForEachMacro, TT_LineComment)) {
      return 0;
    }
    // Only inline simple if's (no nested if or else), unless specified
    if (Style.AllowShortIfStatementsOnASingleLine ==
        FormatStyle::SIS_WithoutElse) {
      if (I + 2 != E && Line.startsWith(tok::kw_if) &&
          I[2]->First->is(tok::kw_else)) {
        return 0;
      }
    }
    return 1;
  }

  unsigned
  tryMergeShortCaseLabels(SmallVectorImpl<AnnotatedLine *>::const_iterator I,
                          SmallVectorImpl<AnnotatedLine *>::const_iterator E,
                          unsigned Limit) {
    if (Limit == 0 || I + 1 == E ||
        I[1]->First->isOneOf(tok::kw_case, tok::kw_default)) {
      return 0;
    }
    if (I[0]->Last->is(tok::l_brace) || I[1]->First->is(tok::l_brace))
      return 0;
    unsigned NumStmts = 0;
    unsigned Length = 0;
    bool EndsWithComment = false;
    bool InPPDirective = I[0]->InPPDirective;
    bool InMacroBody = I[0]->InMacroBody;
    const unsigned Level = I[0]->Level;
    for (; NumStmts < 3; ++NumStmts) {
      if (I + 1 + NumStmts == E)
        break;
      const AnnotatedLine *Line = I[1 + NumStmts];
      if (Line->InPPDirective != InPPDirective)
        break;
      if (Line->InMacroBody != InMacroBody)
        break;
      if (Line->First->isOneOf(tok::kw_case, tok::kw_default, tok::r_brace))
        break;
      if (Line->First->isOneOf(tok::kw_if, tok::kw_for, tok::kw_switch,
                               tok::kw_while) ||
          EndsWithComment) {
        return 0;
      }
      if (Line->First->is(tok::comment)) {
        if (Level != Line->Level)
          return 0;
        SmallVectorImpl<AnnotatedLine *>::const_iterator J = I + 2 + NumStmts;
        for (; J != E; ++J) {
          Line = *J;
          if (Line->InPPDirective != InPPDirective)
            break;
          if (Line->First->isOneOf(tok::kw_case, tok::kw_default, tok::r_brace))
            break;
          if (Line->First->isNot(tok::comment) || Level != Line->Level)
            return 0;
        }
        break;
      }
      if (Line->Last->is(tok::comment))
        EndsWithComment = true;
      Length += I[1 + NumStmts]->Last->TotalLength + 1; // 1 for the space.
    }
    if (NumStmts == 0 || NumStmts == 3 || Length > Limit)
      return 0;
    return NumStmts;
  }

  unsigned
  tryMergeSimpleBlock(SmallVectorImpl<AnnotatedLine *>::const_iterator I,
                      SmallVectorImpl<AnnotatedLine *>::const_iterator E,
                      unsigned Limit) {
    // Don't merge with a preprocessor directive.
    if (I[1]->Type == LT_PreprocessorDirective)
      return 0;

    AnnotatedLine &Line = **I;

    // Don't merge ObjC @ keywords and methods.
    // FIXME: If an option to allow short exception handling clauses on a single
    // line is added, change this to not return for @try and friends.
    if (Style.Language != FormatStyle::LK_Java &&
        Line.First->isOneOf(tok::at, tok::minus, tok::plus)) {
      return 0;
    }

    // Check that the current line allows merging. This depends on whether we
    // are in a control flow statements as well as several style flags.
    if (Line.First->is(tok::kw_case) ||
        (Line.First->Next && Line.First->Next->is(tok::kw_else))) {
      return 0;
    }
    // default: in switch statement
    if (Line.First->is(tok::kw_default)) {
      const FormatToken *Tok = Line.First->getNextNonComment();
      if (Tok && Tok->is(tok::colon))
        return 0;
    }

    auto IsCtrlStmt = [](const auto &Line) {
      return Line.First->isOneOf(tok::kw_if, tok::kw_else, tok::kw_while,
                                 tok::kw_do, tok::kw_for, TT_ForEachMacro);
    };

    const bool IsSplitBlock =
        Style.AllowShortBlocksOnASingleLine == FormatStyle::SBS_Never ||
        (Style.AllowShortBlocksOnASingleLine == FormatStyle::SBS_Empty &&
         I[1]->First->isNot(tok::r_brace));

    if (IsCtrlStmt(Line) ||
        Line.First->isOneOf(tok::kw_try, tok::kw___try, tok::kw_catch,
                            tok::kw___finally, tok::r_brace,
                            Keywords.kw___except)) {
      if (IsSplitBlock)
        return 0;
      // Don't merge when we can't except the case when
      // the control statement block is empty
      if (!Style.AllowShortIfStatementsOnASingleLine &&
          Line.First->isOneOf(tok::kw_if, tok::kw_else) &&
          !Style.BraceWrapping.AfterControlStatement &&
          I[1]->First->isNot(tok::r_brace)) {
        return 0;
      }
      if (!Style.AllowShortIfStatementsOnASingleLine &&
          Line.First->isOneOf(tok::kw_if, tok::kw_else) &&
          Style.BraceWrapping.AfterControlStatement ==
              FormatStyle::BWACS_Always &&
          I + 2 != E && I[2]->First->isNot(tok::r_brace)) {
        return 0;
      }
      if (!Style.AllowShortLoopsOnASingleLine &&
          Line.First->isOneOf(tok::kw_while, tok::kw_do, tok::kw_for,
                              TT_ForEachMacro) &&
          !Style.BraceWrapping.AfterControlStatement &&
          I[1]->First->isNot(tok::r_brace)) {
        return 0;
      }
      if (!Style.AllowShortLoopsOnASingleLine &&
          Line.First->isOneOf(tok::kw_while, tok::kw_do, tok::kw_for,
                              TT_ForEachMacro) &&
          Style.BraceWrapping.AfterControlStatement ==
              FormatStyle::BWACS_Always &&
          I + 2 != E && I[2]->First->isNot(tok::r_brace)) {
        return 0;
      }
      // FIXME: Consider an option to allow short exception handling clauses on
      // a single line.
      // FIXME: This isn't covered by tests.
      // FIXME: For catch, __except, __finally the first token on the line
      // is '}', so this isn't correct here.
      if (Line.First->isOneOf(tok::kw_try, tok::kw___try, tok::kw_catch,
                              Keywords.kw___except, tok::kw___finally)) {
        return 0;
      }
    }

    if (Line.endsWith(tok::l_brace)) {
      if (Style.AllowShortBlocksOnASingleLine == FormatStyle::SBS_Never &&
          Line.First->is(TT_BlockLBrace)) {
        return 0;
      }

      if (IsSplitBlock && Line.First == Line.Last &&
          I > AnnotatedLines.begin() &&
          (I[-1]->endsWith(tok::kw_else) || IsCtrlStmt(*I[-1]))) {
        return 0;
      }
      FormatToken *Tok = I[1]->First;
      auto ShouldMerge = [Tok]() {
        if (Tok->isNot(tok::r_brace) || Tok->MustBreakBefore)
          return false;
        const FormatToken *Next = Tok->getNextNonComment();
        return !Next || Next->is(tok::semi);
      };

      if (ShouldMerge()) {
        // We merge empty blocks even if the line exceeds the column limit.
        Tok->SpacesRequiredBefore =
            (Style.SpaceInEmptyBlock || Line.Last->is(tok::comment)) ? 1 : 0;
        Tok->CanBreakBefore = true;
        return 1;
      } else if (Limit != 0 && !Line.startsWithNamespace() &&
                 !startsExternCBlock(Line)) {
        // We don't merge short records.
        if (isRecordLBrace(*Line.Last))
          return 0;

        // Check that we still have three lines and they fit into the limit.
        if (I + 2 == E || I[2]->Type == LT_Invalid)
          return 0;
        Limit = limitConsideringMacros(I + 2, E, Limit);

        if (!nextTwoLinesFitInto(I, Limit))
          return 0;

        // Second, check that the next line does not contain any braces - if it
        // does, readability declines when putting it into a single line.
        if (I[1]->Last->is(TT_LineComment))
          return 0;
        do {
          if (Tok->is(tok::l_brace) && Tok->isNot(BK_BracedInit))
            return 0;
          Tok = Tok->Next;
        } while (Tok);

        // Last, check that the third line starts with a closing brace.
        Tok = I[2]->First;
        if (Tok->isNot(tok::r_brace))
          return 0;

        // Don't merge "if (a) { .. } else {".
        if (Tok->Next && Tok->Next->is(tok::kw_else))
          return 0;

        // Don't merge a trailing multi-line control statement block like:
        // } else if (foo &&
        //            bar)
        // { <-- current Line
        //   baz();
        // }
        if (Line.First == Line.Last && Line.First->isNot(TT_FunctionLBrace) &&
            Style.BraceWrapping.AfterControlStatement ==
                FormatStyle::BWACS_MultiLine) {
          return 0;
        }

        return 2;
      }
    } else if (I[1]->First->is(tok::l_brace)) {
      if (I[1]->Last->is(TT_LineComment))
        return 0;

      // Check for Limit <= 2 to account for the " {".
      if (Limit <= 2 || (Style.ColumnLimit == 0 && containsMustBreak(*I)))
        return 0;
      Limit -= 2;
      unsigned MergedLines = 0;
      if (Style.AllowShortBlocksOnASingleLine != FormatStyle::SBS_Never ||
          (I[1]->First == I[1]->Last && I + 2 != E &&
           I[2]->First->is(tok::r_brace))) {
        MergedLines = tryMergeSimpleBlock(I + 1, E, Limit);
        // If we managed to merge the block, count the statement header, which
        // is on a separate line.
        if (MergedLines > 0)
          ++MergedLines;
      }
      return MergedLines;
    }
    return 0;
  }

  /// Returns the modified column limit for \p I if it is inside a macro and
  /// needs a trailing '\'.
  unsigned
  limitConsideringMacros(SmallVectorImpl<AnnotatedLine *>::const_iterator I,
                         SmallVectorImpl<AnnotatedLine *>::const_iterator E,
                         unsigned Limit) {
    if (I[0]->InPPDirective && I + 1 != E &&
        !I[1]->First->HasUnescapedNewline && I[1]->First->isNot(tok::eof)) {
      return Limit < 2 ? 0 : Limit - 2;
    }
    return Limit;
  }

  bool nextTwoLinesFitInto(SmallVectorImpl<AnnotatedLine *>::const_iterator I,
                           unsigned Limit) {
    if (I[1]->First->MustBreakBefore || I[2]->First->MustBreakBefore)
      return false;
    return 1 + I[1]->Last->TotalLength + 1 + I[2]->Last->TotalLength <= Limit;
  }

  bool containsMustBreak(const AnnotatedLine *Line) {
    assert(Line->First);
    // Ignore the first token, because in this situation, it applies more to the
    // last token of the previous line.
    for (const FormatToken *Tok = Line->First->Next; Tok; Tok = Tok->Next)
      if (Tok->MustBreakBefore)
        return true;
    return false;
  }

  void join(AnnotatedLine &A, const AnnotatedLine &B) {
    assert(!A.Last->Next);
    assert(!B.First->Previous);
    if (B.Affected)
      A.Affected = true;
    A.Last->Next = B.First;
    B.First->Previous = A.Last;
    B.First->CanBreakBefore = true;
    unsigned LengthA = A.Last->TotalLength + B.First->SpacesRequiredBefore;
    for (FormatToken *Tok = B.First; Tok; Tok = Tok->Next) {
      Tok->TotalLength += LengthA;
      A.Last = Tok;
    }
  }

  const FormatStyle &Style;
  const AdditionalKeywords &Keywords;
  const SmallVectorImpl<AnnotatedLine *>::const_iterator End;

  SmallVectorImpl<AnnotatedLine *>::const_iterator Next;
  const SmallVectorImpl<AnnotatedLine *> &AnnotatedLines;
};

static void markFinalized(FormatToken *Tok) {
  if (Tok->is(tok::hash) && !Tok->Previous && Tok->Next &&
      Tok->Next->isOneOf(tok::pp_if, tok::pp_ifdef, tok::pp_ifndef,
                         tok::pp_elif, tok::pp_elifdef, tok::pp_elifndef,
                         tok::pp_else, tok::pp_endif)) {
    Tok = Tok->Next;
  }
  for (; Tok; Tok = Tok->Next) {
    if (Tok->MacroCtx && Tok->MacroCtx->Role == MR_ExpandedArg) {
      // In the first pass we format all macro arguments in the expanded token
      // stream. Instead of finalizing the macro arguments, we mark that they
      // will be modified as unexpanded arguments (as part of the macro call
      // formatting) in the next pass.
      Tok->MacroCtx->Role = MR_UnexpandedArg;
      // Reset whether spaces or a line break are required before this token, as
      // that is context dependent, and that context may change when formatting
      // the macro call.  For example, given M(x) -> 2 * x, and the macro call
      // M(var), the token 'var' will have SpacesRequiredBefore = 1 after being
      // formatted as part of the expanded macro, but SpacesRequiredBefore = 0
      // for its position within the macro call.
      Tok->SpacesRequiredBefore = 0;
      if (!Tok->MustBreakBeforeFinalized)
        Tok->MustBreakBefore = 0;
    } else {
      Tok->Finalized = true;
    }
  }
}

#ifndef NDEBUG
static void printLineState(const LineState &State) {
  llvm::dbgs() << "State: ";
  for (const ParenState &P : State.Stack) {
    llvm::dbgs() << (P.Tok ? P.Tok->TokenText : "F") << "|" << P.Indent << "|"
                 << P.LastSpace << "|" << P.NestedBlockIndent << " ";
  }
  llvm::dbgs() << State.NextToken->TokenText << "\n";
}
#endif

/// Base class for classes that format one \c AnnotatedLine.
class LineFormatter {
public:
  LineFormatter(ContinuationIndenter *Indenter, WhitespaceManager *Whitespaces,
                const FormatStyle &Style,
                UnwrappedLineFormatter *BlockFormatter)
      : Indenter(Indenter), Whitespaces(Whitespaces), Style(Style),
        BlockFormatter(BlockFormatter) {}
  virtual ~LineFormatter() {}

  /// Formats an \c AnnotatedLine and returns the penalty.
  ///
  /// If \p DryRun is \c false, directly applies the changes.
  virtual unsigned formatLine(const AnnotatedLine &Line, unsigned FirstIndent,
                              unsigned FirstStartColumn, bool DryRun) = 0;

protected:
  /// If the \p State's next token is an r_brace closing a nested block,
  /// format the nested block before it.
  ///
  /// Returns \c true if all children could be placed successfully and adapts
  /// \p Penalty as well as \p State. If \p DryRun is false, also directly
  /// creates changes using \c Whitespaces.
  ///
  /// The crucial idea here is that children always get formatted upon
  /// encountering the closing brace right after the nested block. Now, if we
  /// are currently trying to keep the "}" on the same line (i.e. \p NewLine is
  /// \c false), the entire block has to be kept on the same line (which is only
  /// possible if it fits on the line, only contains a single statement, etc.
  ///
  /// If \p NewLine is true, we format the nested block on separate lines, i.e.
  /// break after the "{", format all lines with correct indentation and the put
  /// the closing "}" on yet another new line.
  ///
  /// This enables us to keep the simple structure of the
  /// \c UnwrappedLineFormatter, where we only have two options for each token:
  /// break or don't break.
  bool formatChildren(LineState &State, bool NewLine, bool DryRun,
                      unsigned &Penalty) {
    const FormatToken *LBrace = State.NextToken->getPreviousNonComment();
    bool HasLBrace = LBrace && LBrace->is(tok::l_brace) && LBrace->is(BK_Block);
    FormatToken &Previous = *State.NextToken->Previous;
    if (Previous.Children.size() == 0 || (!HasLBrace && !LBrace->MacroParent)) {
      // The previous token does not open a block. Nothing to do. We don't
      // assert so that we can simply call this function for all tokens.
      return true;
    }

    if (NewLine || Previous.MacroParent) {
      const ParenState &P = State.Stack.back();

      int AdditionalIndent =
          P.Indent - Previous.Children[0]->Level * Style.IndentWidth;
      Penalty +=
          BlockFormatter->format(Previous.Children, DryRun, AdditionalIndent,
                                 /*FixBadIndentation=*/true);
      return true;
    }

    if (Previous.Children[0]->First->MustBreakBefore)
      return false;

    // Cannot merge into one line if this line ends on a comment.
    if (Previous.is(tok::comment))
      return false;

    // Cannot merge multiple statements into a single line.
    if (Previous.Children.size() > 1)
      return false;

    const AnnotatedLine *Child = Previous.Children[0];
    // We can't put the closing "}" on a line with a trailing comment.
    if (Child->Last->isTrailingComment())
      return false;

    // If the child line exceeds the column limit, we wouldn't want to merge it.
    // We add +2 for the trailing " }".
    if (Style.ColumnLimit > 0 &&
        Child->Last->TotalLength + State.Column + 2 > Style.ColumnLimit) {
      return false;
    }

    if (!DryRun) {
      Whitespaces->replaceWhitespace(
          *Child->First, /*Newlines=*/0, /*Spaces=*/1,
          /*StartOfTokenColumn=*/State.Column, /*IsAligned=*/false,
          State.Line->InPPDirective);
    }
    Penalty +=
        formatLine(*Child, State.Column + 1, /*FirstStartColumn=*/0, DryRun);
    if (!DryRun)
      markFinalized(Child->First);

    State.Column += 1 + Child->Last->TotalLength;
    return true;
  }

  ContinuationIndenter *Indenter;

private:
  WhitespaceManager *Whitespaces;
  const FormatStyle &Style;
  UnwrappedLineFormatter *BlockFormatter;
};

/// Formatter that keeps the existing line breaks.
class NoColumnLimitLineFormatter : public LineFormatter {
public:
  NoColumnLimitLineFormatter(ContinuationIndenter *Indenter,
                             WhitespaceManager *Whitespaces,
                             const FormatStyle &Style,
                             UnwrappedLineFormatter *BlockFormatter)
      : LineFormatter(Indenter, Whitespaces, Style, BlockFormatter) {}

  /// Formats the line, simply keeping all of the input's line breaking
  /// decisions.
  unsigned formatLine(const AnnotatedLine &Line, unsigned FirstIndent,
                      unsigned FirstStartColumn, bool DryRun) override {
    assert(!DryRun);
    LineState State = Indenter->getInitialState(FirstIndent, FirstStartColumn,
                                                &Line, /*DryRun=*/false);
    while (State.NextToken) {
      bool Newline =
          Indenter->mustBreak(State) ||
          (Indenter->canBreak(State) && State.NextToken->NewlinesBefore > 0);
      unsigned Penalty = 0;
      formatChildren(State, Newline, /*DryRun=*/false, Penalty);
      Indenter->addTokenToState(State, Newline, /*DryRun=*/false);
    }
    return 0;
  }
};

/// Formatter that puts all tokens into a single line without breaks.
class NoLineBreakFormatter : public LineFormatter {
public:
  NoLineBreakFormatter(ContinuationIndenter *Indenter,
                       WhitespaceManager *Whitespaces, const FormatStyle &Style,
                       UnwrappedLineFormatter *BlockFormatter)
      : LineFormatter(Indenter, Whitespaces, Style, BlockFormatter) {}

  /// Puts all tokens into a single line.
  unsigned formatLine(const AnnotatedLine &Line, unsigned FirstIndent,
                      unsigned FirstStartColumn, bool DryRun) override {
    unsigned Penalty = 0;
    LineState State =
        Indenter->getInitialState(FirstIndent, FirstStartColumn, &Line, DryRun);
    while (State.NextToken) {
      formatChildren(State, /*NewLine=*/false, DryRun, Penalty);
      Indenter->addTokenToState(
          State, /*Newline=*/State.NextToken->MustBreakBefore, DryRun);
    }
    return Penalty;
  }
};

/// Finds the best way to break lines.
class OptimizingLineFormatter : public LineFormatter {
public:
  OptimizingLineFormatter(ContinuationIndenter *Indenter,
                          WhitespaceManager *Whitespaces,
                          const FormatStyle &Style,
                          UnwrappedLineFormatter *BlockFormatter)
      : LineFormatter(Indenter, Whitespaces, Style, BlockFormatter) {}

  /// Formats the line by finding the best line breaks with line lengths
  /// below the column limit.
  unsigned formatLine(const AnnotatedLine &Line, unsigned FirstIndent,
                      unsigned FirstStartColumn, bool DryRun) override {
    LineState State =
        Indenter->getInitialState(FirstIndent, FirstStartColumn, &Line, DryRun);

    // If the ObjC method declaration does not fit on a line, we should format
    // it with one arg per line.
    if (State.Line->Type == LT_ObjCMethodDecl)
      State.Stack.back().BreakBeforeParameter = true;

    // Find best solution in solution space.
    return analyzeSolutionSpace(State, DryRun);
  }

private:
  struct CompareLineStatePointers {
    bool operator()(LineState *obj1, LineState *obj2) const {
      return *obj1 < *obj2;
    }
  };

  /// A pair of <penalty, count> that is used to prioritize the BFS on.
  ///
  /// In case of equal penalties, we want to prefer states that were inserted
  /// first. During state generation we make sure that we insert states first
  /// that break the line as late as possible.
  typedef std::pair<unsigned, unsigned> OrderedPenalty;

  /// An edge in the solution space from \c Previous->State to \c State,
  /// inserting a newline dependent on the \c NewLine.
  struct StateNode {
    StateNode(const LineState &State, bool NewLine, StateNode *Previous)
        : State(State), NewLine(NewLine), Previous(Previous) {}
    LineState State;
    bool NewLine;
    StateNode *Previous;
  };

  /// An item in the prioritized BFS search queue. The \c StateNode's
  /// \c State has the given \c OrderedPenalty.
  typedef std::pair<OrderedPenalty, StateNode *> QueueItem;

  /// The BFS queue type.
  typedef std::priority_queue<QueueItem, SmallVector<QueueItem>,
                              std::greater<QueueItem>>
      QueueType;

  /// Analyze the entire solution space starting from \p InitialState.
  ///
  /// This implements a variant of Dijkstra's algorithm on the graph that spans
  /// the solution space (\c LineStates are the nodes). The algorithm tries to
  /// find the shortest path (the one with lowest penalty) from \p InitialState
  /// to a state where all tokens are placed. Returns the penalty.
  ///
  /// If \p DryRun is \c false, directly applies the changes.
  unsigned analyzeSolutionSpace(LineState &InitialState, bool DryRun) {
    std::set<LineState *, CompareLineStatePointers> Seen;

    // Increasing count of \c StateNode items we have created. This is used to
    // create a deterministic order independent of the container.
    unsigned Count = 0;
    QueueType Queue;

    // Insert start element into queue.
    StateNode *RootNode =
        new (Allocator.Allocate()) StateNode(InitialState, false, nullptr);
    Queue.push(QueueItem(OrderedPenalty(0, Count), RootNode));
    ++Count;

    unsigned Penalty = 0;

    // While not empty, take first element and follow edges.
    while (!Queue.empty()) {
      // Quit if we still haven't found a solution by now.
      if (Count > 25'000'000)
        return 0;

      Penalty = Queue.top().first.first;
      StateNode *Node = Queue.top().second;
      if (!Node->State.NextToken) {
        LLVM_DEBUG(llvm::dbgs()
                   << "\n---\nPenalty for line: " << Penalty << "\n");
        break;
      }
      Queue.pop();

      // Cut off the analysis of certain solutions if the analysis gets too
      // complex. See description of IgnoreStackForComparison.
      if (Count > 50'000)
        Node->State.IgnoreStackForComparison = true;

      if (!Seen.insert(&Node->State).second) {
        // State already examined with lower penalty.
        continue;
      }

      FormatDecision LastFormat = Node->State.NextToken->getDecision();
      if (LastFormat == FD_Unformatted || LastFormat == FD_Continue)
        addNextStateToQueue(Penalty, Node, /*NewLine=*/false, &Count, &Queue);
      if (LastFormat == FD_Unformatted || LastFormat == FD_Break)
        addNextStateToQueue(Penalty, Node, /*NewLine=*/true, &Count, &Queue);
    }

    if (Queue.empty()) {
      // We were unable to find a solution, do nothing.
      // FIXME: Add diagnostic?
      LLVM_DEBUG(llvm::dbgs() << "Could not find a solution.\n");
      return 0;
    }

    // Reconstruct the solution.
    if (!DryRun)
      reconstructPath(InitialState, Queue.top().second);

    LLVM_DEBUG(llvm::dbgs()
               << "Total number of analyzed states: " << Count << "\n");
    LLVM_DEBUG(llvm::dbgs() << "---\n");

    return Penalty;
  }

  /// Add the following state to the analysis queue \c Queue.
  ///
  /// Assume the current state is \p PreviousNode and has been reached with a
  /// penalty of \p Penalty. Insert a line break if \p NewLine is \c true.
  void addNextStateToQueue(unsigned Penalty, StateNode *PreviousNode,
                           bool NewLine, unsigned *Count, QueueType *Queue) {
    if (NewLine && !Indenter->canBreak(PreviousNode->State))
      return;
    if (!NewLine && Indenter->mustBreak(PreviousNode->State))
      return;

    StateNode *Node = new (Allocator.Allocate())
        StateNode(PreviousNode->State, NewLine, PreviousNode);
    if (!formatChildren(Node->State, NewLine, /*DryRun=*/true, Penalty))
      return;

    Penalty += Indenter->addTokenToState(Node->State, NewLine, true);

    Queue->push(QueueItem(OrderedPenalty(Penalty, *Count), Node));
    ++(*Count);
  }

  /// Applies the best formatting by reconstructing the path in the
  /// solution space that leads to \c Best.
  void reconstructPath(LineState &State, StateNode *Best) {
    llvm::SmallVector<StateNode *> Path;
    // We do not need a break before the initial token.
    while (Best->Previous) {
      Path.push_back(Best);
      Best = Best->Previous;
    }
    for (const auto &Node : llvm::reverse(Path)) {
      unsigned Penalty = 0;
      formatChildren(State, Node->NewLine, /*DryRun=*/false, Penalty);
      Penalty += Indenter->addTokenToState(State, Node->NewLine, false);

      LLVM_DEBUG({
        printLineState(Node->Previous->State);
        if (Node->NewLine) {
          llvm::dbgs() << "Penalty for placing "
                       << Node->Previous->State.NextToken->Tok.getName()
                       << " on a new line: " << Penalty << "\n";
        }
      });
    }
  }

  llvm::SpecificBumpPtrAllocator<StateNode> Allocator;
};

} // anonymous namespace

unsigned UnwrappedLineFormatter::format(
    const SmallVectorImpl<AnnotatedLine *> &Lines, bool DryRun,
    int AdditionalIndent, bool FixBadIndentation, unsigned FirstStartColumn,
    unsigned NextStartColumn, unsigned LastStartColumn) {
  LineJoiner Joiner(Style, Keywords, Lines);

  // Try to look up already computed penalty in DryRun-mode.
  std::pair<const SmallVectorImpl<AnnotatedLine *> *, unsigned> CacheKey(
      &Lines, AdditionalIndent);
  auto CacheIt = PenaltyCache.find(CacheKey);
  if (DryRun && CacheIt != PenaltyCache.end())
    return CacheIt->second;

  assert(!Lines.empty());
  unsigned Penalty = 0;
  LevelIndentTracker IndentTracker(Style, Keywords, Lines[0]->Level,
                                   AdditionalIndent);
  const AnnotatedLine *PrevPrevLine = nullptr;
  const AnnotatedLine *PreviousLine = nullptr;
  const AnnotatedLine *NextLine = nullptr;

  // The minimum level of consecutive lines that have been formatted.
  unsigned RangeMinLevel = UINT_MAX;

  bool FirstLine = true;
  for (const AnnotatedLine *Line =
           Joiner.getNextMergedLine(DryRun, IndentTracker);
       Line; PrevPrevLine = PreviousLine, PreviousLine = Line, Line = NextLine,
                           FirstLine = false) {
    assert(Line->First);
    const AnnotatedLine &TheLine = *Line;
    unsigned Indent = IndentTracker.getIndent();

    // We continue formatting unchanged lines to adjust their indent, e.g. if a
    // scope was added. However, we need to carefully stop doing this when we
    // exit the scope of affected lines to prevent indenting the entire
    // remaining file if it currently missing a closing brace.
    bool PreviousRBrace =
        PreviousLine && PreviousLine->startsWith(tok::r_brace);
    bool ContinueFormatting =
        TheLine.Level > RangeMinLevel ||
        (TheLine.Level == RangeMinLevel && !PreviousRBrace &&
         !TheLine.startsWith(tok::r_brace));

    bool FixIndentation = (FixBadIndentation || ContinueFormatting) &&
                          Indent != TheLine.First->OriginalColumn;
    bool ShouldFormat = TheLine.Affected || FixIndentation;
    // We cannot format this line; if the reason is that the line had a
    // parsing error, remember that.
    if (ShouldFormat && TheLine.Type == LT_Invalid && Status) {
      Status->FormatComplete = false;
      Status->Line =
          SourceMgr.getSpellingLineNumber(TheLine.First->Tok.getLocation());
    }

    if (ShouldFormat && TheLine.Type != LT_Invalid) {
      if (!DryRun) {
        bool LastLine = TheLine.First->is(tok::eof);
        formatFirstToken(TheLine, PreviousLine, PrevPrevLine, Lines, Indent,
                         LastLine ? LastStartColumn : NextStartColumn + Indent);
      }

      NextLine = Joiner.getNextMergedLine(DryRun, IndentTracker);
      unsigned ColumnLimit = getColumnLimit(TheLine.InPPDirective, NextLine);
      bool FitsIntoOneLine =
          !TheLine.ContainsMacroCall &&
          (TheLine.Last->TotalLength + Indent <= ColumnLimit ||
           (TheLine.Type == LT_ImportStatement &&
            (!Style.isJavaScript() || !Style.JavaScriptWrapImports)) ||
           (Style.isCSharp() &&
            TheLine.InPPDirective)); // don't split #regions in C#
      if (Style.ColumnLimit == 0) {
        NoColumnLimitLineFormatter(Indenter, Whitespaces, Style, this)
            .formatLine(TheLine, NextStartColumn + Indent,
                        FirstLine ? FirstStartColumn : 0, DryRun);
      } else if (FitsIntoOneLine) {
        Penalty += NoLineBreakFormatter(Indenter, Whitespaces, Style, this)
                       .formatLine(TheLine, NextStartColumn + Indent,
                                   FirstLine ? FirstStartColumn : 0, DryRun);
      } else {
        Penalty += OptimizingLineFormatter(Indenter, Whitespaces, Style, this)
                       .formatLine(TheLine, NextStartColumn + Indent,
                                   FirstLine ? FirstStartColumn : 0, DryRun);
      }
      RangeMinLevel = std::min(RangeMinLevel, TheLine.Level);
    } else {
      // If no token in the current line is affected, we still need to format
      // affected children.
      if (TheLine.ChildrenAffected) {
        for (const FormatToken *Tok = TheLine.First; Tok; Tok = Tok->Next)
          if (!Tok->Children.empty())
            format(Tok->Children, DryRun);
      }

      // Adapt following lines on the current indent level to the same level
      // unless the current \c AnnotatedLine is not at the beginning of a line.
      bool StartsNewLine =
          TheLine.First->NewlinesBefore > 0 || TheLine.First->IsFirst;
      if (StartsNewLine)
        IndentTracker.adjustToUnmodifiedLine(TheLine);
      if (!DryRun) {
        bool ReformatLeadingWhitespace =
            StartsNewLine && ((PreviousLine && PreviousLine->Affected) ||
                              TheLine.LeadingEmptyLinesAffected);
        // Format the first token.
        if (ReformatLeadingWhitespace) {
          formatFirstToken(TheLine, PreviousLine, PrevPrevLine, Lines,
                           TheLine.First->OriginalColumn,
                           TheLine.First->OriginalColumn);
        } else {
          Whitespaces->addUntouchableToken(*TheLine.First,
                                           TheLine.InPPDirective);
        }

        // Notify the WhitespaceManager about the unchanged whitespace.
        for (FormatToken *Tok = TheLine.First->Next; Tok; Tok = Tok->Next)
          Whitespaces->addUntouchableToken(*Tok, TheLine.InPPDirective);
      }
      NextLine = Joiner.getNextMergedLine(DryRun, IndentTracker);
      RangeMinLevel = UINT_MAX;
    }
    if (!DryRun)
      markFinalized(TheLine.First);
  }
  PenaltyCache[CacheKey] = Penalty;
  return Penalty;
}

static auto computeNewlines(const AnnotatedLine &Line,
                            const AnnotatedLine *PreviousLine,
                            const AnnotatedLine *PrevPrevLine,
                            const SmallVectorImpl<AnnotatedLine *> &Lines,
                            const FormatStyle &Style) {
  const auto &RootToken = *Line.First;
  auto Newlines =
      std::min(RootToken.NewlinesBefore, Style.MaxEmptyLinesToKeep + 1);
  // Remove empty lines before "}" where applicable.
  if (RootToken.is(tok::r_brace) &&
      (!RootToken.Next ||
       (RootToken.Next->is(tok::semi) && !RootToken.Next->Next)) &&
      // Do not remove empty lines before namespace closing "}".
      !getNamespaceToken(&Line, Lines)) {
    Newlines = std::min(Newlines, 1u);
  }
  // Remove empty lines at the start of nested blocks (lambdas/arrow functions)
  if (!PreviousLine && Line.Level > 0)
    Newlines = std::min(Newlines, 1u);
  if (Newlines == 0 && !RootToken.IsFirst)
    Newlines = 1;
  if (RootToken.IsFirst &&
      (!Style.KeepEmptyLines.AtStartOfFile || !RootToken.HasUnescapedNewline)) {
    Newlines = 0;
  }

  // Remove empty lines after "{".
  if (!Style.KeepEmptyLines.AtStartOfBlock && PreviousLine &&
      PreviousLine->Last->is(tok::l_brace) &&
      !PreviousLine->startsWithNamespace() &&
      !(PrevPrevLine && PrevPrevLine->startsWithNamespace() &&
        PreviousLine->startsWith(tok::l_brace)) &&
      !startsExternCBlock(*PreviousLine)) {
    Newlines = 1;
  }

  // Insert or remove empty line before access specifiers.
  if (PreviousLine && RootToken.isAccessSpecifier()) {
    switch (Style.EmptyLineBeforeAccessModifier) {
    case FormatStyle::ELBAMS_Never:
      if (Newlines > 1)
        Newlines = 1;
      break;
    case FormatStyle::ELBAMS_Leave:
      Newlines = std::max(RootToken.NewlinesBefore, 1u);
      break;
    case FormatStyle::ELBAMS_LogicalBlock:
      if (PreviousLine->Last->isOneOf(tok::semi, tok::r_brace) && Newlines <= 1)
        Newlines = 2;
      if (PreviousLine->First->isAccessSpecifier())
        Newlines = 1; // Previous is an access modifier remove all new lines.
      break;
    case FormatStyle::ELBAMS_Always: {
      const FormatToken *previousToken;
      if (PreviousLine->Last->is(tok::comment))
        previousToken = PreviousLine->Last->getPreviousNonComment();
      else
        previousToken = PreviousLine->Last;
      if ((!previousToken || previousToken->isNot(tok::l_brace)) &&
          Newlines <= 1) {
        Newlines = 2;
      }
    } break;
    }
  }

  // Insert or remove empty line after access specifiers.
  if (PreviousLine && PreviousLine->First->isAccessSpecifier() &&
      (!PreviousLine->InPPDirective || !RootToken.HasUnescapedNewline)) {
    // EmptyLineBeforeAccessModifier is handling the case when two access
    // modifiers follow each other.
    if (!RootToken.isAccessSpecifier()) {
      switch (Style.EmptyLineAfterAccessModifier) {
      case FormatStyle::ELAAMS_Never:
        Newlines = 1;
        break;
      case FormatStyle::ELAAMS_Leave:
        Newlines = std::max(Newlines, 1u);
        break;
      case FormatStyle::ELAAMS_Always:
        if (RootToken.is(tok::r_brace)) // Do not add at end of class.
          Newlines = 1u;
        else
          Newlines = std::max(Newlines, 2u);
        break;
      }
    }
  }

  return Newlines;
}

void UnwrappedLineFormatter::formatFirstToken(
    const AnnotatedLine &Line, const AnnotatedLine *PreviousLine,
    const AnnotatedLine *PrevPrevLine,
    const SmallVectorImpl<AnnotatedLine *> &Lines, unsigned Indent,
    unsigned NewlineIndent) {
  FormatToken &RootToken = *Line.First;
  if (RootToken.is(tok::eof)) {
    unsigned Newlines = std::min(
        RootToken.NewlinesBefore,
        Style.KeepEmptyLines.AtEndOfFile ? Style.MaxEmptyLinesToKeep + 1 : 1);
    unsigned TokenIndent = Newlines ? NewlineIndent : 0;
    Whitespaces->replaceWhitespace(RootToken, Newlines, TokenIndent,
                                   TokenIndent);
    return;
  }

  if (RootToken.Newlines < 0) {
    RootToken.Newlines =
        computeNewlines(Line, PreviousLine, PrevPrevLine, Lines, Style);
    assert(RootToken.Newlines >= 0);
  }

  if (RootToken.Newlines > 0)
    Indent = NewlineIndent;

  // Preprocessor directives get indented before the hash only if specified. In
  // Javascript import statements are indented like normal statements.
  if (!Style.isJavaScript() &&
      Style.IndentPPDirectives != FormatStyle::PPDIS_BeforeHash &&
      (Line.Type == LT_PreprocessorDirective ||
       Line.Type == LT_ImportStatement)) {
    Indent = 0;
  }

  Whitespaces->replaceWhitespace(RootToken, RootToken.Newlines, Indent, Indent,
                                 /*IsAligned=*/false,
                                 Line.InPPDirective &&
                                     !RootToken.HasUnescapedNewline);
}

unsigned
UnwrappedLineFormatter::getColumnLimit(bool InPPDirective,
                                       const AnnotatedLine *NextLine) const {
  // In preprocessor directives reserve two chars for trailing " \" if the
  // next line continues the preprocessor directive.
  bool ContinuesPPDirective =
      InPPDirective &&
      // If there is no next line, this is likely a child line and the parent
      // continues the preprocessor directive.
      (!NextLine ||
       (NextLine->InPPDirective &&
        // If there is an unescaped newline between this line and the next, the
        // next line starts a new preprocessor directive.
        !NextLine->First->HasUnescapedNewline));
  return Style.ColumnLimit - (ContinuesPPDirective ? 2 : 0);
}

} // namespace format
} // namespace clang
