//===--- ContinuationIndenter.h - Format C++ code ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements an indenter that manages the indentation of
/// continuations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_CONTINUATIONINDENTER_H
#define LLVM_CLANG_LIB_FORMAT_CONTINUATIONINDENTER_H

#include "Encoding.h"
#include "FormatToken.h"

namespace clang {
class SourceManager;

namespace format {

class AnnotatedLine;
class BreakableToken;
struct FormatToken;
struct LineState;
struct ParenState;
struct RawStringFormatStyleManager;
class WhitespaceManager;

struct RawStringFormatStyleManager {
  llvm::StringMap<FormatStyle> DelimiterStyle;
  llvm::StringMap<FormatStyle> EnclosingFunctionStyle;

  RawStringFormatStyleManager(const FormatStyle &CodeStyle);

  std::optional<FormatStyle> getDelimiterStyle(StringRef Delimiter) const;

  std::optional<FormatStyle>
  getEnclosingFunctionStyle(StringRef EnclosingFunction) const;
};

class ContinuationIndenter {
public:
  /// Constructs a \c ContinuationIndenter to format \p Line starting in
  /// column \p FirstIndent.
  ContinuationIndenter(const FormatStyle &Style,
                       const AdditionalKeywords &Keywords,
                       const SourceManager &SourceMgr,
                       WhitespaceManager &Whitespaces,
                       encoding::Encoding Encoding,
                       bool BinPackInconclusiveFunctions);

  /// Get the initial state, i.e. the state after placing \p Line's
  /// first token at \p FirstIndent. When reformatting a fragment of code, as in
  /// the case of formatting inside raw string literals, \p FirstStartColumn is
  /// the column at which the state of the parent formatter is.
  LineState getInitialState(unsigned FirstIndent, unsigned FirstStartColumn,
                            const AnnotatedLine *Line, bool DryRun);

  // FIXME: canBreak and mustBreak aren't strictly indentation-related. Find a
  // better home.
  /// Returns \c true, if a line break after \p State is allowed.
  bool canBreak(const LineState &State);

  /// Returns \c true, if a line break after \p State is mandatory.
  bool mustBreak(const LineState &State);

  /// Appends the next token to \p State and updates information
  /// necessary for indentation.
  ///
  /// Puts the token on the current line if \p Newline is \c false and adds a
  /// line break and necessary indentation otherwise.
  ///
  /// If \p DryRun is \c false, also creates and stores the required
  /// \c Replacement.
  unsigned addTokenToState(LineState &State, bool Newline, bool DryRun,
                           unsigned ExtraSpaces = 0);

  /// Get the column limit for this line. This is the style's column
  /// limit, potentially reduced for preprocessor definitions.
  unsigned getColumnLimit(const LineState &State) const;

private:
  /// Mark the next token as consumed in \p State and modify its stacks
  /// accordingly.
  unsigned moveStateToNextToken(LineState &State, bool DryRun, bool Newline);

  /// Update 'State' according to the next token's fake left parentheses.
  void moveStatePastFakeLParens(LineState &State, bool Newline);
  /// Update 'State' according to the next token's fake r_parens.
  void moveStatePastFakeRParens(LineState &State);

  /// Update 'State' according to the next token being one of "(<{[".
  void moveStatePastScopeOpener(LineState &State, bool Newline);
  /// Update 'State' according to the next token being one of ")>}]".
  void moveStatePastScopeCloser(LineState &State);
  /// Update 'State' with the next token opening a nested block.
  void moveStateToNewBlock(LineState &State, bool NewLine);

  /// Reformats a raw string literal.
  ///
  /// \returns An extra penalty induced by reformatting the token.
  unsigned reformatRawStringLiteral(const FormatToken &Current,
                                    LineState &State,
                                    const FormatStyle &RawStringStyle,
                                    bool DryRun, bool Newline);

  /// If the current token is at the end of the current line, handle
  /// the transition to the next line.
  unsigned handleEndOfLine(const FormatToken &Current, LineState &State,
                           bool DryRun, bool AllowBreak, bool Newline);

  /// If \p Current is a raw string that is configured to be reformatted,
  /// return the style to be used.
  std::optional<FormatStyle> getRawStringStyle(const FormatToken &Current,
                                               const LineState &State);

  /// If the current token sticks out over the end of the line, break
  /// it if possible.
  ///
  /// \returns A pair (penalty, exceeded), where penalty is the extra penalty
  /// when tokens are broken or lines exceed the column limit, and exceeded
  /// indicates whether the algorithm purposefully left lines exceeding the
  /// column limit.
  ///
  /// The returned penalty will cover the cost of the additional line breaks
  /// and column limit violation in all lines except for the last one. The
  /// penalty for the column limit violation in the last line (and in single
  /// line tokens) is handled in \c addNextStateToQueue.
  ///
  /// \p Strict indicates whether reflowing is allowed to leave characters
  /// protruding the column limit; if true, lines will be split strictly within
  /// the column limit where possible; if false, words are allowed to protrude
  /// over the column limit as long as the penalty is less than the penalty
  /// of a break.
  std::pair<unsigned, bool> breakProtrudingToken(const FormatToken &Current,
                                                 LineState &State,
                                                 bool AllowBreak, bool DryRun,
                                                 bool Strict);

  /// Returns the \c BreakableToken starting at \p Current, or nullptr
  /// if the current token cannot be broken.
  std::unique_ptr<BreakableToken>
  createBreakableToken(const FormatToken &Current, LineState &State,
                       bool AllowBreak);

  /// Appends the next token to \p State and updates information
  /// necessary for indentation.
  ///
  /// Puts the token on the current line.
  ///
  /// If \p DryRun is \c false, also creates and stores the required
  /// \c Replacement.
  void addTokenOnCurrentLine(LineState &State, bool DryRun,
                             unsigned ExtraSpaces);

  /// Appends the next token to \p State and updates information
  /// necessary for indentation.
  ///
  /// Adds a line break and necessary indentation.
  ///
  /// If \p DryRun is \c false, also creates and stores the required
  /// \c Replacement.
  unsigned addTokenOnNewLine(LineState &State, bool DryRun);

  /// Calculate the new column for a line wrap before the next token.
  unsigned getNewLineColumn(const LineState &State);

  /// Adds a multiline token to the \p State.
  ///
  /// \returns Extra penalty for the first line of the literal: last line is
  /// handled in \c addNextStateToQueue, and the penalty for other lines doesn't
  /// matter, as we don't change them.
  unsigned addMultilineToken(const FormatToken &Current, LineState &State);

  /// Returns \c true if the next token starts a multiline string
  /// literal.
  ///
  /// This includes implicitly concatenated strings, strings that will be broken
  /// by clang-format and string literals with escaped newlines.
  bool nextIsMultilineString(const LineState &State);

  FormatStyle Style;
  const AdditionalKeywords &Keywords;
  const SourceManager &SourceMgr;
  WhitespaceManager &Whitespaces;
  encoding::Encoding Encoding;
  bool BinPackInconclusiveFunctions;
  llvm::Regex CommentPragmasRegex;
  const RawStringFormatStyleManager RawStringFormats;
};

struct ParenState {
  ParenState(const FormatToken *Tok, unsigned Indent, unsigned LastSpace,
             bool AvoidBinPacking, bool NoLineBreak)
      : Tok(Tok), Indent(Indent), LastSpace(LastSpace),
        NestedBlockIndent(Indent), IsAligned(false),
        BreakBeforeClosingBrace(false), BreakBeforeClosingParen(false),
        AvoidBinPacking(AvoidBinPacking), BreakBeforeParameter(false),
        NoLineBreak(NoLineBreak), NoLineBreakInOperand(false),
        LastOperatorWrapped(true), ContainsLineBreak(false),
        ContainsUnwrappedBuilder(false), AlignColons(true),
        ObjCSelectorNameFound(false), HasMultipleNestedBlocks(false),
        NestedBlockInlined(false), IsInsideObjCArrayLiteral(false),
        IsCSharpGenericTypeConstraint(false), IsChainedConditional(false),
        IsWrappedConditional(false), UnindentOperator(false) {}

  /// \brief The token opening this parenthesis level, or nullptr if this level
  /// is opened by fake parenthesis.
  ///
  /// Not considered for memoization as it will always have the same value at
  /// the same token.
  const FormatToken *Tok;

  /// The position to which a specific parenthesis level needs to be
  /// indented.
  unsigned Indent;

  /// The position of the last space on each level.
  ///
  /// Used e.g. to break like:
  /// functionCall(Parameter, otherCall(
  ///                             OtherParameter));
  unsigned LastSpace;

  /// If a block relative to this parenthesis level gets wrapped, indent
  /// it this much.
  unsigned NestedBlockIndent;

  /// The position the first "<<" operator encountered on each level.
  ///
  /// Used to align "<<" operators. 0 if no such operator has been encountered
  /// on a level.
  unsigned FirstLessLess = 0;

  /// The column of a \c ? in a conditional expression;
  unsigned QuestionColumn = 0;

  /// The position of the colon in an ObjC method declaration/call.
  unsigned ColonPos = 0;

  /// The start of the most recent function in a builder-type call.
  unsigned StartOfFunctionCall = 0;

  /// Contains the start of array subscript expressions, so that they
  /// can be aligned.
  unsigned StartOfArraySubscripts = 0;

  /// If a nested name specifier was broken over multiple lines, this
  /// contains the start column of the second line. Otherwise 0.
  unsigned NestedNameSpecifierContinuation = 0;

  /// If a call expression was broken over multiple lines, this
  /// contains the start column of the second line. Otherwise 0.
  unsigned CallContinuation = 0;

  /// The column of the first variable name in a variable declaration.
  ///
  /// Used to align further variables if necessary.
  unsigned VariablePos = 0;

  /// Whether this block's indentation is used for alignment.
  bool IsAligned : 1;

  /// Whether a newline needs to be inserted before the block's closing
  /// brace.
  ///
  /// We only want to insert a newline before the closing brace if there also
  /// was a newline after the beginning left brace.
  bool BreakBeforeClosingBrace : 1;

  /// Whether a newline needs to be inserted before the block's closing
  /// paren.
  ///
  /// We only want to insert a newline before the closing paren if there also
  /// was a newline after the beginning left paren.
  bool BreakBeforeClosingParen : 1;

  /// Avoid bin packing, i.e. multiple parameters/elements on multiple
  /// lines, in this context.
  bool AvoidBinPacking : 1;

  /// Break after the next comma (or all the commas in this context if
  /// \c AvoidBinPacking is \c true).
  bool BreakBeforeParameter : 1;

  /// Line breaking in this context would break a formatting rule.
  bool NoLineBreak : 1;

  /// Same as \c NoLineBreak, but is restricted until the end of the
  /// operand (including the next ",").
  bool NoLineBreakInOperand : 1;

  /// True if the last binary operator on this level was wrapped to the
  /// next line.
  bool LastOperatorWrapped : 1;

  /// \c true if this \c ParenState already contains a line-break.
  ///
  /// The first line break in a certain \c ParenState causes extra penalty so
  /// that clang-format prefers similar breaks, i.e. breaks in the same
  /// parenthesis.
  bool ContainsLineBreak : 1;

  /// \c true if this \c ParenState contains multiple segments of a
  /// builder-type call on one line.
  bool ContainsUnwrappedBuilder : 1;

  /// \c true if the colons of the curren ObjC method expression should
  /// be aligned.
  ///
  /// Not considered for memoization as it will always have the same value at
  /// the same token.
  bool AlignColons : 1;

  /// \c true if at least one selector name was found in the current
  /// ObjC method expression.
  ///
  /// Not considered for memoization as it will always have the same value at
  /// the same token.
  bool ObjCSelectorNameFound : 1;

  /// \c true if there are multiple nested blocks inside these parens.
  ///
  /// Not considered for memoization as it will always have the same value at
  /// the same token.
  bool HasMultipleNestedBlocks : 1;

  /// The start of a nested block (e.g. lambda introducer in C++ or
  /// "function" in JavaScript) is not wrapped to a new line.
  bool NestedBlockInlined : 1;

  /// \c true if the current \c ParenState represents an Objective-C
  /// array literal.
  bool IsInsideObjCArrayLiteral : 1;

  bool IsCSharpGenericTypeConstraint : 1;

  /// \brief true if the current \c ParenState represents the false branch of
  /// a chained conditional expression (e.g. else-if)
  bool IsChainedConditional : 1;

  /// \brief true if there conditionnal was wrapped on the first operator (the
  /// question mark)
  bool IsWrappedConditional : 1;

  /// \brief Indicates the indent should be reduced by the length of the
  /// operator.
  bool UnindentOperator : 1;

  bool operator<(const ParenState &Other) const {
    if (Indent != Other.Indent)
      return Indent < Other.Indent;
    if (LastSpace != Other.LastSpace)
      return LastSpace < Other.LastSpace;
    if (NestedBlockIndent != Other.NestedBlockIndent)
      return NestedBlockIndent < Other.NestedBlockIndent;
    if (FirstLessLess != Other.FirstLessLess)
      return FirstLessLess < Other.FirstLessLess;
    if (IsAligned != Other.IsAligned)
      return IsAligned;
    if (BreakBeforeClosingBrace != Other.BreakBeforeClosingBrace)
      return BreakBeforeClosingBrace;
    if (BreakBeforeClosingParen != Other.BreakBeforeClosingParen)
      return BreakBeforeClosingParen;
    if (QuestionColumn != Other.QuestionColumn)
      return QuestionColumn < Other.QuestionColumn;
    if (AvoidBinPacking != Other.AvoidBinPacking)
      return AvoidBinPacking;
    if (BreakBeforeParameter != Other.BreakBeforeParameter)
      return BreakBeforeParameter;
    if (NoLineBreak != Other.NoLineBreak)
      return NoLineBreak;
    if (LastOperatorWrapped != Other.LastOperatorWrapped)
      return LastOperatorWrapped;
    if (ColonPos != Other.ColonPos)
      return ColonPos < Other.ColonPos;
    if (StartOfFunctionCall != Other.StartOfFunctionCall)
      return StartOfFunctionCall < Other.StartOfFunctionCall;
    if (StartOfArraySubscripts != Other.StartOfArraySubscripts)
      return StartOfArraySubscripts < Other.StartOfArraySubscripts;
    if (CallContinuation != Other.CallContinuation)
      return CallContinuation < Other.CallContinuation;
    if (VariablePos != Other.VariablePos)
      return VariablePos < Other.VariablePos;
    if (ContainsLineBreak != Other.ContainsLineBreak)
      return ContainsLineBreak;
    if (ContainsUnwrappedBuilder != Other.ContainsUnwrappedBuilder)
      return ContainsUnwrappedBuilder;
    if (NestedBlockInlined != Other.NestedBlockInlined)
      return NestedBlockInlined;
    if (IsCSharpGenericTypeConstraint != Other.IsCSharpGenericTypeConstraint)
      return IsCSharpGenericTypeConstraint;
    if (IsChainedConditional != Other.IsChainedConditional)
      return IsChainedConditional;
    if (IsWrappedConditional != Other.IsWrappedConditional)
      return IsWrappedConditional;
    if (UnindentOperator != Other.UnindentOperator)
      return UnindentOperator;
    return false;
  }
};

/// The current state when indenting a unwrapped line.
///
/// As the indenting tries different combinations this is copied by value.
struct LineState {
  /// The number of used columns in the current line.
  unsigned Column;

  /// The token that needs to be next formatted.
  FormatToken *NextToken;

  /// \c true if \p NextToken should not continue this line.
  bool NoContinuation;

  /// The \c NestingLevel at the start of this line.
  unsigned StartOfLineLevel;

  /// The lowest \c NestingLevel on the current line.
  unsigned LowestLevelOnLine;

  /// The start column of the string literal, if we're in a string
  /// literal sequence, 0 otherwise.
  unsigned StartOfStringLiteral;

  /// Disallow line breaks for this line.
  bool NoLineBreak;

  /// A stack keeping track of properties applying to parenthesis
  /// levels.
  SmallVector<ParenState> Stack;

  /// Ignore the stack of \c ParenStates for state comparison.
  ///
  /// In long and deeply nested unwrapped lines, the current algorithm can
  /// be insufficient for finding the best formatting with a reasonable amount
  /// of time and memory. Setting this flag will effectively lead to the
  /// algorithm not analyzing some combinations. However, these combinations
  /// rarely contain the optimal solution: In short, accepting a higher
  /// penalty early would need to lead to different values in the \c
  /// ParenState stack (in an otherwise identical state) and these different
  /// values would need to lead to a significant amount of avoided penalty
  /// later.
  ///
  /// FIXME: Come up with a better algorithm instead.
  bool IgnoreStackForComparison;

  /// The indent of the first token.
  unsigned FirstIndent;

  /// The line that is being formatted.
  ///
  /// Does not need to be considered for memoization because it doesn't change.
  const AnnotatedLine *Line;

  /// Comparison operator to be able to used \c LineState in \c map.
  bool operator<(const LineState &Other) const {
    if (NextToken != Other.NextToken)
      return NextToken < Other.NextToken;
    if (Column != Other.Column)
      return Column < Other.Column;
    if (NoContinuation != Other.NoContinuation)
      return NoContinuation;
    if (StartOfLineLevel != Other.StartOfLineLevel)
      return StartOfLineLevel < Other.StartOfLineLevel;
    if (LowestLevelOnLine != Other.LowestLevelOnLine)
      return LowestLevelOnLine < Other.LowestLevelOnLine;
    if (StartOfStringLiteral != Other.StartOfStringLiteral)
      return StartOfStringLiteral < Other.StartOfStringLiteral;
    if (IgnoreStackForComparison || Other.IgnoreStackForComparison)
      return false;
    return Stack < Other.Stack;
  }
};

} // end namespace format
} // end namespace clang

#endif
