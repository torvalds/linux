//===--- BreakableToken.cpp - Format C++ code -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Contains implementation of BreakableToken class and classes derived
/// from it.
///
//===----------------------------------------------------------------------===//

#include "BreakableToken.h"
#include "ContinuationIndenter.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Format/Format.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Debug.h"
#include <algorithm>

#define DEBUG_TYPE "format-token-breaker"

namespace clang {
namespace format {

static constexpr StringRef Blanks = " \t\v\f\r";
static bool IsBlank(char C) {
  switch (C) {
  case ' ':
  case '\t':
  case '\v':
  case '\f':
  case '\r':
    return true;
  default:
    return false;
  }
}

static StringRef getLineCommentIndentPrefix(StringRef Comment,
                                            const FormatStyle &Style) {
  static constexpr StringRef KnownCStylePrefixes[] = {"///<", "//!<", "///",
                                                      "//!",  "//:",  "//"};
  static constexpr StringRef KnownTextProtoPrefixes[] = {"####", "###", "##",
                                                         "//", "#"};
  ArrayRef<StringRef> KnownPrefixes(KnownCStylePrefixes);
  if (Style.Language == FormatStyle::LK_TextProto)
    KnownPrefixes = KnownTextProtoPrefixes;

  assert(
      llvm::is_sorted(KnownPrefixes, [](StringRef Lhs, StringRef Rhs) noexcept {
        return Lhs.size() > Rhs.size();
      }));

  for (StringRef KnownPrefix : KnownPrefixes) {
    if (Comment.starts_with(KnownPrefix)) {
      const auto PrefixLength =
          Comment.find_first_not_of(' ', KnownPrefix.size());
      return Comment.substr(0, PrefixLength);
    }
  }
  return {};
}

static BreakableToken::Split
getCommentSplit(StringRef Text, unsigned ContentStartColumn,
                unsigned ColumnLimit, unsigned TabWidth,
                encoding::Encoding Encoding, const FormatStyle &Style,
                bool DecorationEndsWithStar = false) {
  LLVM_DEBUG(llvm::dbgs() << "Comment split: \"" << Text
                          << "\", Column limit: " << ColumnLimit
                          << ", Content start: " << ContentStartColumn << "\n");
  if (ColumnLimit <= ContentStartColumn + 1)
    return BreakableToken::Split(StringRef::npos, 0);

  unsigned MaxSplit = ColumnLimit - ContentStartColumn + 1;
  unsigned MaxSplitBytes = 0;

  for (unsigned NumChars = 0;
       NumChars < MaxSplit && MaxSplitBytes < Text.size();) {
    unsigned BytesInChar =
        encoding::getCodePointNumBytes(Text[MaxSplitBytes], Encoding);
    NumChars += encoding::columnWidthWithTabs(
        Text.substr(MaxSplitBytes, BytesInChar), ContentStartColumn + NumChars,
        TabWidth, Encoding);
    MaxSplitBytes += BytesInChar;
  }

  // In JavaScript, some @tags can be followed by {, and machinery that parses
  // these comments will fail to understand the comment if followed by a line
  // break. So avoid ever breaking before a {.
  if (Style.isJavaScript()) {
    StringRef::size_type SpaceOffset =
        Text.find_first_of(Blanks, MaxSplitBytes);
    if (SpaceOffset != StringRef::npos && SpaceOffset + 1 < Text.size() &&
        Text[SpaceOffset + 1] == '{') {
      MaxSplitBytes = SpaceOffset + 1;
    }
  }

  StringRef::size_type SpaceOffset = Text.find_last_of(Blanks, MaxSplitBytes);

  static const auto kNumberedListRegexp = llvm::Regex("^[1-9][0-9]?\\.");
  // Some spaces are unacceptable to break on, rewind past them.
  while (SpaceOffset != StringRef::npos) {
    // If a line-comment ends with `\`, the next line continues the comment,
    // whether or not it starts with `//`. This is confusing and triggers
    // -Wcomment.
    // Avoid introducing multiline comments by not allowing a break right
    // after '\'.
    if (Style.isCpp()) {
      StringRef::size_type LastNonBlank =
          Text.find_last_not_of(Blanks, SpaceOffset);
      if (LastNonBlank != StringRef::npos && Text[LastNonBlank] == '\\') {
        SpaceOffset = Text.find_last_of(Blanks, LastNonBlank);
        continue;
      }
    }

    // Do not split before a number followed by a dot: this would be interpreted
    // as a numbered list, which would prevent re-flowing in subsequent passes.
    if (kNumberedListRegexp.match(Text.substr(SpaceOffset).ltrim(Blanks))) {
      SpaceOffset = Text.find_last_of(Blanks, SpaceOffset);
      continue;
    }

    // Avoid ever breaking before a @tag or a { in JavaScript.
    if (Style.isJavaScript() && SpaceOffset + 1 < Text.size() &&
        (Text[SpaceOffset + 1] == '{' || Text[SpaceOffset + 1] == '@')) {
      SpaceOffset = Text.find_last_of(Blanks, SpaceOffset);
      continue;
    }

    break;
  }

  if (SpaceOffset == StringRef::npos ||
      // Don't break at leading whitespace.
      Text.find_last_not_of(Blanks, SpaceOffset) == StringRef::npos) {
    // Make sure that we don't break at leading whitespace that
    // reaches past MaxSplit.
    StringRef::size_type FirstNonWhitespace = Text.find_first_not_of(Blanks);
    if (FirstNonWhitespace == StringRef::npos) {
      // If the comment is only whitespace, we cannot split.
      return BreakableToken::Split(StringRef::npos, 0);
    }
    SpaceOffset = Text.find_first_of(
        Blanks, std::max<unsigned>(MaxSplitBytes, FirstNonWhitespace));
  }
  if (SpaceOffset != StringRef::npos && SpaceOffset != 0) {
    // adaptStartOfLine will break after lines starting with /** if the comment
    // is broken anywhere. Avoid emitting this break twice here.
    // Example: in /** longtextcomesherethatbreaks */ (with ColumnLimit 20) will
    // insert a break after /**, so this code must not insert the same break.
    if (SpaceOffset == 1 && Text[SpaceOffset - 1] == '*')
      return BreakableToken::Split(StringRef::npos, 0);
    StringRef BeforeCut = Text.substr(0, SpaceOffset).rtrim(Blanks);
    StringRef AfterCut = Text.substr(SpaceOffset);
    // Don't trim the leading blanks if it would create a */ after the break.
    if (!DecorationEndsWithStar || AfterCut.size() <= 1 || AfterCut[1] != '/')
      AfterCut = AfterCut.ltrim(Blanks);
    return BreakableToken::Split(BeforeCut.size(),
                                 AfterCut.begin() - BeforeCut.end());
  }
  return BreakableToken::Split(StringRef::npos, 0);
}

static BreakableToken::Split
getStringSplit(StringRef Text, unsigned UsedColumns, unsigned ColumnLimit,
               unsigned TabWidth, encoding::Encoding Encoding) {
  // FIXME: Reduce unit test case.
  if (Text.empty())
    return BreakableToken::Split(StringRef::npos, 0);
  if (ColumnLimit <= UsedColumns)
    return BreakableToken::Split(StringRef::npos, 0);
  unsigned MaxSplit = ColumnLimit - UsedColumns;
  StringRef::size_type SpaceOffset = 0;
  StringRef::size_type SlashOffset = 0;
  StringRef::size_type WordStartOffset = 0;
  StringRef::size_type SplitPoint = 0;
  for (unsigned Chars = 0;;) {
    unsigned Advance;
    if (Text[0] == '\\') {
      Advance = encoding::getEscapeSequenceLength(Text);
      Chars += Advance;
    } else {
      Advance = encoding::getCodePointNumBytes(Text[0], Encoding);
      Chars += encoding::columnWidthWithTabs(
          Text.substr(0, Advance), UsedColumns + Chars, TabWidth, Encoding);
    }

    if (Chars > MaxSplit || Text.size() <= Advance)
      break;

    if (IsBlank(Text[0]))
      SpaceOffset = SplitPoint;
    if (Text[0] == '/')
      SlashOffset = SplitPoint;
    if (Advance == 1 && !isAlphanumeric(Text[0]))
      WordStartOffset = SplitPoint;

    SplitPoint += Advance;
    Text = Text.substr(Advance);
  }

  if (SpaceOffset != 0)
    return BreakableToken::Split(SpaceOffset + 1, 0);
  if (SlashOffset != 0)
    return BreakableToken::Split(SlashOffset + 1, 0);
  if (WordStartOffset != 0)
    return BreakableToken::Split(WordStartOffset + 1, 0);
  if (SplitPoint != 0)
    return BreakableToken::Split(SplitPoint, 0);
  return BreakableToken::Split(StringRef::npos, 0);
}

bool switchesFormatting(const FormatToken &Token) {
  assert((Token.is(TT_BlockComment) || Token.is(TT_LineComment)) &&
         "formatting regions are switched by comment tokens");
  StringRef Content = Token.TokenText.substr(2).ltrim();
  return Content.starts_with("clang-format on") ||
         Content.starts_with("clang-format off");
}

unsigned
BreakableToken::getLengthAfterCompression(unsigned RemainingTokenColumns,
                                          Split Split) const {
  // Example: consider the content
  // lala  lala
  // - RemainingTokenColumns is the original number of columns, 10;
  // - Split is (4, 2), denoting the two spaces between the two words;
  //
  // We compute the number of columns when the split is compressed into a single
  // space, like:
  // lala lala
  //
  // FIXME: Correctly measure the length of whitespace in Split.second so it
  // works with tabs.
  return RemainingTokenColumns + 1 - Split.second;
}

unsigned BreakableStringLiteral::getLineCount() const { return 1; }

unsigned BreakableStringLiteral::getRangeLength(unsigned LineIndex,
                                                unsigned Offset,
                                                StringRef::size_type Length,
                                                unsigned StartColumn) const {
  llvm_unreachable("Getting the length of a part of the string literal "
                   "indicates that the code tries to reflow it.");
}

unsigned
BreakableStringLiteral::getRemainingLength(unsigned LineIndex, unsigned Offset,
                                           unsigned StartColumn) const {
  return UnbreakableTailLength + Postfix.size() +
         encoding::columnWidthWithTabs(Line.substr(Offset), StartColumn,
                                       Style.TabWidth, Encoding);
}

unsigned BreakableStringLiteral::getContentStartColumn(unsigned LineIndex,
                                                       bool Break) const {
  return StartColumn + Prefix.size();
}

BreakableStringLiteral::BreakableStringLiteral(
    const FormatToken &Tok, unsigned StartColumn, StringRef Prefix,
    StringRef Postfix, unsigned UnbreakableTailLength, bool InPPDirective,
    encoding::Encoding Encoding, const FormatStyle &Style)
    : BreakableToken(Tok, InPPDirective, Encoding, Style),
      StartColumn(StartColumn), Prefix(Prefix), Postfix(Postfix),
      UnbreakableTailLength(UnbreakableTailLength) {
  assert(Tok.TokenText.starts_with(Prefix) && Tok.TokenText.ends_with(Postfix));
  Line = Tok.TokenText.substr(
      Prefix.size(), Tok.TokenText.size() - Prefix.size() - Postfix.size());
}

BreakableToken::Split BreakableStringLiteral::getSplit(
    unsigned LineIndex, unsigned TailOffset, unsigned ColumnLimit,
    unsigned ContentStartColumn, const llvm::Regex &CommentPragmasRegex) const {
  return getStringSplit(Line.substr(TailOffset), ContentStartColumn,
                        ColumnLimit - Postfix.size(), Style.TabWidth, Encoding);
}

void BreakableStringLiteral::insertBreak(unsigned LineIndex,
                                         unsigned TailOffset, Split Split,
                                         unsigned ContentIndent,
                                         WhitespaceManager &Whitespaces) const {
  Whitespaces.replaceWhitespaceInToken(
      Tok, Prefix.size() + TailOffset + Split.first, Split.second, Postfix,
      Prefix, InPPDirective, 1, StartColumn);
}

BreakableStringLiteralUsingOperators::BreakableStringLiteralUsingOperators(
    const FormatToken &Tok, QuoteStyleType QuoteStyle, bool UnindentPlus,
    unsigned StartColumn, unsigned UnbreakableTailLength, bool InPPDirective,
    encoding::Encoding Encoding, const FormatStyle &Style)
    : BreakableStringLiteral(
          Tok, StartColumn, /*Prefix=*/QuoteStyle == SingleQuotes ? "'"
                            : QuoteStyle == AtDoubleQuotes        ? "@\""
                                                                  : "\"",
          /*Postfix=*/QuoteStyle == SingleQuotes ? "'" : "\"",
          UnbreakableTailLength, InPPDirective, Encoding, Style),
      BracesNeeded(Tok.isNot(TT_StringInConcatenation)),
      QuoteStyle(QuoteStyle) {
  // Find the replacement text for inserting braces and quotes and line breaks.
  // We don't create an allocated string concatenated from parts here because it
  // has to outlive the BreakableStringliteral object.  The brace replacements
  // include a quote so that WhitespaceManager can tell it apart from whitespace
  // replacements between the string and surrounding tokens.

  // The option is not implemented in JavaScript.
  bool SignOnNewLine =
      !Style.isJavaScript() &&
      Style.BreakBeforeBinaryOperators != FormatStyle::BOS_None;

  if (Style.isVerilog()) {
    // In Verilog, all strings are quoted by double quotes, joined by commas,
    // and wrapped in braces.  The comma is always before the newline.
    assert(QuoteStyle == DoubleQuotes);
    LeftBraceQuote = Style.Cpp11BracedListStyle ? "{\"" : "{ \"";
    RightBraceQuote = Style.Cpp11BracedListStyle ? "\"}" : "\" }";
    Postfix = "\",";
    Prefix = "\"";
  } else {
    // The plus sign may be on either line.  And also C# and JavaScript have
    // several quoting styles.
    if (QuoteStyle == SingleQuotes) {
      LeftBraceQuote = Style.SpacesInParensOptions.Other ? "( '" : "('";
      RightBraceQuote = Style.SpacesInParensOptions.Other ? "' )" : "')";
      Postfix = SignOnNewLine ? "'" : "' +";
      Prefix = SignOnNewLine ? "+ '" : "'";
    } else {
      if (QuoteStyle == AtDoubleQuotes) {
        LeftBraceQuote = Style.SpacesInParensOptions.Other ? "( @" : "(@";
        Prefix = SignOnNewLine ? "+ @\"" : "@\"";
      } else {
        LeftBraceQuote = Style.SpacesInParensOptions.Other ? "( \"" : "(\"";
        Prefix = SignOnNewLine ? "+ \"" : "\"";
      }
      RightBraceQuote = Style.SpacesInParensOptions.Other ? "\" )" : "\")";
      Postfix = SignOnNewLine ? "\"" : "\" +";
    }
  }

  // Following lines are indented by the width of the brace and space if any.
  ContinuationIndent = BracesNeeded ? LeftBraceQuote.size() - 1 : 0;
  // The plus sign may need to be unindented depending on the style.
  // FIXME: Add support for DontAlign.
  if (!Style.isVerilog() && SignOnNewLine && !BracesNeeded && UnindentPlus &&
      Style.AlignOperands == FormatStyle::OAS_AlignAfterOperator) {
    ContinuationIndent -= 2;
  }
}

unsigned BreakableStringLiteralUsingOperators::getRemainingLength(
    unsigned LineIndex, unsigned Offset, unsigned StartColumn) const {
  return UnbreakableTailLength + (BracesNeeded ? RightBraceQuote.size() : 1) +
         encoding::columnWidthWithTabs(Line.substr(Offset), StartColumn,
                                       Style.TabWidth, Encoding);
}

unsigned
BreakableStringLiteralUsingOperators::getContentStartColumn(unsigned LineIndex,
                                                            bool Break) const {
  return std::max(
      0,
      static_cast<int>(StartColumn) +
          (Break ? ContinuationIndent + static_cast<int>(Prefix.size())
                 : (BracesNeeded ? static_cast<int>(LeftBraceQuote.size()) - 1
                                 : 0) +
                       (QuoteStyle == AtDoubleQuotes ? 2 : 1)));
}

void BreakableStringLiteralUsingOperators::insertBreak(
    unsigned LineIndex, unsigned TailOffset, Split Split,
    unsigned ContentIndent, WhitespaceManager &Whitespaces) const {
  Whitespaces.replaceWhitespaceInToken(
      Tok, /*Offset=*/(QuoteStyle == AtDoubleQuotes ? 2 : 1) + TailOffset +
               Split.first,
      /*ReplaceChars=*/Split.second, /*PreviousPostfix=*/Postfix,
      /*CurrentPrefix=*/Prefix, InPPDirective, /*NewLines=*/1,
      /*Spaces=*/
      std::max(0, static_cast<int>(StartColumn) + ContinuationIndent));
}

void BreakableStringLiteralUsingOperators::updateAfterBroken(
    WhitespaceManager &Whitespaces) const {
  // Add the braces required for breaking the token if they are needed.
  if (!BracesNeeded)
    return;

  // To add a brace or parenthesis, we replace the quote (or the at sign) with a
  // brace and another quote.  This is because the rest of the program requires
  // one replacement for each source range.  If we replace the empty strings
  // around the string, it may conflict with whitespace replacements between the
  // string and adjacent tokens.
  Whitespaces.replaceWhitespaceInToken(
      Tok, /*Offset=*/0, /*ReplaceChars=*/1, /*PreviousPostfix=*/"",
      /*CurrentPrefix=*/LeftBraceQuote, InPPDirective, /*NewLines=*/0,
      /*Spaces=*/0);
  Whitespaces.replaceWhitespaceInToken(
      Tok, /*Offset=*/Tok.TokenText.size() - 1, /*ReplaceChars=*/1,
      /*PreviousPostfix=*/RightBraceQuote,
      /*CurrentPrefix=*/"", InPPDirective, /*NewLines=*/0, /*Spaces=*/0);
}

BreakableComment::BreakableComment(const FormatToken &Token,
                                   unsigned StartColumn, bool InPPDirective,
                                   encoding::Encoding Encoding,
                                   const FormatStyle &Style)
    : BreakableToken(Token, InPPDirective, Encoding, Style),
      StartColumn(StartColumn) {}

unsigned BreakableComment::getLineCount() const { return Lines.size(); }

BreakableToken::Split
BreakableComment::getSplit(unsigned LineIndex, unsigned TailOffset,
                           unsigned ColumnLimit, unsigned ContentStartColumn,
                           const llvm::Regex &CommentPragmasRegex) const {
  // Don't break lines matching the comment pragmas regex.
  if (CommentPragmasRegex.match(Content[LineIndex]))
    return Split(StringRef::npos, 0);
  return getCommentSplit(Content[LineIndex].substr(TailOffset),
                         ContentStartColumn, ColumnLimit, Style.TabWidth,
                         Encoding, Style);
}

void BreakableComment::compressWhitespace(
    unsigned LineIndex, unsigned TailOffset, Split Split,
    WhitespaceManager &Whitespaces) const {
  StringRef Text = Content[LineIndex].substr(TailOffset);
  // Text is relative to the content line, but Whitespaces operates relative to
  // the start of the corresponding token, so compute the start of the Split
  // that needs to be compressed into a single space relative to the start of
  // its token.
  unsigned BreakOffsetInToken =
      Text.data() - tokenAt(LineIndex).TokenText.data() + Split.first;
  unsigned CharsToRemove = Split.second;
  Whitespaces.replaceWhitespaceInToken(
      tokenAt(LineIndex), BreakOffsetInToken, CharsToRemove, "", "",
      /*InPPDirective=*/false, /*Newlines=*/0, /*Spaces=*/1);
}

const FormatToken &BreakableComment::tokenAt(unsigned LineIndex) const {
  return Tokens[LineIndex] ? *Tokens[LineIndex] : Tok;
}

static bool mayReflowContent(StringRef Content) {
  Content = Content.trim(Blanks);
  // Lines starting with '@' or '\' commonly have special meaning.
  // Lines starting with '-', '-#', '+' or '*' are bulleted/numbered lists.
  bool hasSpecialMeaningPrefix = false;
  for (StringRef Prefix :
       {"@", "\\", "TODO", "FIXME", "XXX", "-# ", "- ", "+ ", "* "}) {
    if (Content.starts_with(Prefix)) {
      hasSpecialMeaningPrefix = true;
      break;
    }
  }

  // Numbered lists may also start with a number followed by '.'
  // To avoid issues if a line starts with a number which is actually the end
  // of a previous line, we only consider numbers with up to 2 digits.
  static const auto kNumberedListRegexp = llvm::Regex("^[1-9][0-9]?\\. ");
  hasSpecialMeaningPrefix =
      hasSpecialMeaningPrefix || kNumberedListRegexp.match(Content);

  // Simple heuristic for what to reflow: content should contain at least two
  // characters and either the first or second character must be
  // non-punctuation.
  return Content.size() >= 2 && !hasSpecialMeaningPrefix &&
         !Content.ends_with("\\") &&
         // Note that this is UTF-8 safe, since if isPunctuation(Content[0]) is
         // true, then the first code point must be 1 byte long.
         (!isPunctuation(Content[0]) || !isPunctuation(Content[1]));
}

BreakableBlockComment::BreakableBlockComment(
    const FormatToken &Token, unsigned StartColumn,
    unsigned OriginalStartColumn, bool FirstInLine, bool InPPDirective,
    encoding::Encoding Encoding, const FormatStyle &Style, bool UseCRLF)
    : BreakableComment(Token, StartColumn, InPPDirective, Encoding, Style),
      DelimitersOnNewline(false),
      UnbreakableTailLength(Token.UnbreakableTailLength) {
  assert(Tok.is(TT_BlockComment) &&
         "block comment section must start with a block comment");

  StringRef TokenText(Tok.TokenText);
  assert(TokenText.starts_with("/*") && TokenText.ends_with("*/"));
  TokenText.substr(2, TokenText.size() - 4)
      .split(Lines, UseCRLF ? "\r\n" : "\n");

  int IndentDelta = StartColumn - OriginalStartColumn;
  Content.resize(Lines.size());
  Content[0] = Lines[0];
  ContentColumn.resize(Lines.size());
  // Account for the initial '/*'.
  ContentColumn[0] = StartColumn + 2;
  Tokens.resize(Lines.size());
  for (size_t i = 1; i < Lines.size(); ++i)
    adjustWhitespace(i, IndentDelta);

  // Align decorations with the column of the star on the first line,
  // that is one column after the start "/*".
  DecorationColumn = StartColumn + 1;

  // Account for comment decoration patterns like this:
  //
  // /*
  // ** blah blah blah
  // */
  if (Lines.size() >= 2 && Content[1].starts_with("**") &&
      static_cast<unsigned>(ContentColumn[1]) == StartColumn) {
    DecorationColumn = StartColumn;
  }

  Decoration = "* ";
  if (Lines.size() == 1 && !FirstInLine) {
    // Comments for which FirstInLine is false can start on arbitrary column,
    // and available horizontal space can be too small to align consecutive
    // lines with the first one.
    // FIXME: We could, probably, align them to current indentation level, but
    // now we just wrap them without stars.
    Decoration = "";
  }
  for (size_t i = 1, e = Content.size(); i < e && !Decoration.empty(); ++i) {
    const StringRef &Text = Content[i];
    if (i + 1 == e) {
      // If the last line is empty, the closing "*/" will have a star.
      if (Text.empty())
        break;
    } else if (!Text.empty() && Decoration.starts_with(Text)) {
      continue;
    }
    while (!Text.starts_with(Decoration))
      Decoration = Decoration.drop_back(1);
  }

  LastLineNeedsDecoration = true;
  IndentAtLineBreak = ContentColumn[0] + 1;
  for (size_t i = 1, e = Lines.size(); i < e; ++i) {
    if (Content[i].empty()) {
      if (i + 1 == e) {
        // Empty last line means that we already have a star as a part of the
        // trailing */. We also need to preserve whitespace, so that */ is
        // correctly indented.
        LastLineNeedsDecoration = false;
        // Align the star in the last '*/' with the stars on the previous lines.
        if (e >= 2 && !Decoration.empty())
          ContentColumn[i] = DecorationColumn;
      } else if (Decoration.empty()) {
        // For all other lines, set the start column to 0 if they're empty, so
        // we do not insert trailing whitespace anywhere.
        ContentColumn[i] = 0;
      }
      continue;
    }

    // The first line already excludes the star.
    // The last line excludes the star if LastLineNeedsDecoration is false.
    // For all other lines, adjust the line to exclude the star and
    // (optionally) the first whitespace.
    unsigned DecorationSize = Decoration.starts_with(Content[i])
                                  ? Content[i].size()
                                  : Decoration.size();
    if (DecorationSize)
      ContentColumn[i] = DecorationColumn + DecorationSize;
    Content[i] = Content[i].substr(DecorationSize);
    if (!Decoration.starts_with(Content[i])) {
      IndentAtLineBreak =
          std::min<int>(IndentAtLineBreak, std::max(0, ContentColumn[i]));
    }
  }
  IndentAtLineBreak = std::max<unsigned>(IndentAtLineBreak, Decoration.size());

  // Detect a multiline jsdoc comment and set DelimitersOnNewline in that case.
  if (Style.isJavaScript() || Style.Language == FormatStyle::LK_Java) {
    if ((Lines[0] == "*" || Lines[0].starts_with("* ")) && Lines.size() > 1) {
      // This is a multiline jsdoc comment.
      DelimitersOnNewline = true;
    } else if (Lines[0].starts_with("* ") && Lines.size() == 1) {
      // Detect a long single-line comment, like:
      // /** long long long */
      // Below, '2' is the width of '*/'.
      unsigned EndColumn =
          ContentColumn[0] +
          encoding::columnWidthWithTabs(Lines[0], ContentColumn[0],
                                        Style.TabWidth, Encoding) +
          2;
      DelimitersOnNewline = EndColumn > Style.ColumnLimit;
    }
  }

  LLVM_DEBUG({
    llvm::dbgs() << "IndentAtLineBreak " << IndentAtLineBreak << "\n";
    llvm::dbgs() << "DelimitersOnNewline " << DelimitersOnNewline << "\n";
    for (size_t i = 0; i < Lines.size(); ++i) {
      llvm::dbgs() << i << " |" << Content[i] << "| "
                   << "CC=" << ContentColumn[i] << "| "
                   << "IN=" << (Content[i].data() - Lines[i].data()) << "\n";
    }
  });
}

BreakableToken::Split BreakableBlockComment::getSplit(
    unsigned LineIndex, unsigned TailOffset, unsigned ColumnLimit,
    unsigned ContentStartColumn, const llvm::Regex &CommentPragmasRegex) const {
  // Don't break lines matching the comment pragmas regex.
  if (CommentPragmasRegex.match(Content[LineIndex]))
    return Split(StringRef::npos, 0);
  return getCommentSplit(Content[LineIndex].substr(TailOffset),
                         ContentStartColumn, ColumnLimit, Style.TabWidth,
                         Encoding, Style, Decoration.ends_with("*"));
}

void BreakableBlockComment::adjustWhitespace(unsigned LineIndex,
                                             int IndentDelta) {
  // When in a preprocessor directive, the trailing backslash in a block comment
  // is not needed, but can serve a purpose of uniformity with necessary escaped
  // newlines outside the comment. In this case we remove it here before
  // trimming the trailing whitespace. The backslash will be re-added later when
  // inserting a line break.
  size_t EndOfPreviousLine = Lines[LineIndex - 1].size();
  if (InPPDirective && Lines[LineIndex - 1].ends_with("\\"))
    --EndOfPreviousLine;

  // Calculate the end of the non-whitespace text in the previous line.
  EndOfPreviousLine =
      Lines[LineIndex - 1].find_last_not_of(Blanks, EndOfPreviousLine);
  if (EndOfPreviousLine == StringRef::npos)
    EndOfPreviousLine = 0;
  else
    ++EndOfPreviousLine;
  // Calculate the start of the non-whitespace text in the current line.
  size_t StartOfLine = Lines[LineIndex].find_first_not_of(Blanks);
  if (StartOfLine == StringRef::npos)
    StartOfLine = Lines[LineIndex].size();

  StringRef Whitespace = Lines[LineIndex].substr(0, StartOfLine);
  // Adjust Lines to only contain relevant text.
  size_t PreviousContentOffset =
      Content[LineIndex - 1].data() - Lines[LineIndex - 1].data();
  Content[LineIndex - 1] = Lines[LineIndex - 1].substr(
      PreviousContentOffset, EndOfPreviousLine - PreviousContentOffset);
  Content[LineIndex] = Lines[LineIndex].substr(StartOfLine);

  // Adjust the start column uniformly across all lines.
  ContentColumn[LineIndex] =
      encoding::columnWidthWithTabs(Whitespace, 0, Style.TabWidth, Encoding) +
      IndentDelta;
}

unsigned BreakableBlockComment::getRangeLength(unsigned LineIndex,
                                               unsigned Offset,
                                               StringRef::size_type Length,
                                               unsigned StartColumn) const {
  return encoding::columnWidthWithTabs(
      Content[LineIndex].substr(Offset, Length), StartColumn, Style.TabWidth,
      Encoding);
}

unsigned BreakableBlockComment::getRemainingLength(unsigned LineIndex,
                                                   unsigned Offset,
                                                   unsigned StartColumn) const {
  unsigned LineLength =
      UnbreakableTailLength +
      getRangeLength(LineIndex, Offset, StringRef::npos, StartColumn);
  if (LineIndex + 1 == Lines.size()) {
    LineLength += 2;
    // We never need a decoration when breaking just the trailing "*/" postfix.
    bool HasRemainingText = Offset < Content[LineIndex].size();
    if (!HasRemainingText) {
      bool HasDecoration = Lines[LineIndex].ltrim().starts_with(Decoration);
      if (HasDecoration)
        LineLength -= Decoration.size();
    }
  }
  return LineLength;
}

unsigned BreakableBlockComment::getContentStartColumn(unsigned LineIndex,
                                                      bool Break) const {
  if (Break)
    return IndentAtLineBreak;
  return std::max(0, ContentColumn[LineIndex]);
}

const llvm::StringSet<>
    BreakableBlockComment::ContentIndentingJavadocAnnotations = {
        "@param", "@return",     "@returns", "@throws",  "@type", "@template",
        "@see",   "@deprecated", "@define",  "@exports", "@mods", "@private",
};

unsigned BreakableBlockComment::getContentIndent(unsigned LineIndex) const {
  if (Style.Language != FormatStyle::LK_Java && !Style.isJavaScript())
    return 0;
  // The content at LineIndex 0 of a comment like:
  // /** line 0 */
  // is "* line 0", so we need to skip over the decoration in that case.
  StringRef ContentWithNoDecoration = Content[LineIndex];
  if (LineIndex == 0 && ContentWithNoDecoration.starts_with("*"))
    ContentWithNoDecoration = ContentWithNoDecoration.substr(1).ltrim(Blanks);
  StringRef FirstWord = ContentWithNoDecoration.substr(
      0, ContentWithNoDecoration.find_first_of(Blanks));
  if (ContentIndentingJavadocAnnotations.contains(FirstWord))
    return Style.ContinuationIndentWidth;
  return 0;
}

void BreakableBlockComment::insertBreak(unsigned LineIndex, unsigned TailOffset,
                                        Split Split, unsigned ContentIndent,
                                        WhitespaceManager &Whitespaces) const {
  StringRef Text = Content[LineIndex].substr(TailOffset);
  StringRef Prefix = Decoration;
  // We need this to account for the case when we have a decoration "* " for all
  // the lines except for the last one, where the star in "*/" acts as a
  // decoration.
  unsigned LocalIndentAtLineBreak = IndentAtLineBreak;
  if (LineIndex + 1 == Lines.size() &&
      Text.size() == Split.first + Split.second) {
    // For the last line we need to break before "*/", but not to add "* ".
    Prefix = "";
    if (LocalIndentAtLineBreak >= 2)
      LocalIndentAtLineBreak -= 2;
  }
  // The split offset is from the beginning of the line. Convert it to an offset
  // from the beginning of the token text.
  unsigned BreakOffsetInToken =
      Text.data() - tokenAt(LineIndex).TokenText.data() + Split.first;
  unsigned CharsToRemove = Split.second;
  assert(LocalIndentAtLineBreak >= Prefix.size());
  std::string PrefixWithTrailingIndent = std::string(Prefix);
  PrefixWithTrailingIndent.append(ContentIndent, ' ');
  Whitespaces.replaceWhitespaceInToken(
      tokenAt(LineIndex), BreakOffsetInToken, CharsToRemove, "",
      PrefixWithTrailingIndent, InPPDirective, /*Newlines=*/1,
      /*Spaces=*/LocalIndentAtLineBreak + ContentIndent -
          PrefixWithTrailingIndent.size());
}

BreakableToken::Split BreakableBlockComment::getReflowSplit(
    unsigned LineIndex, const llvm::Regex &CommentPragmasRegex) const {
  if (!mayReflow(LineIndex, CommentPragmasRegex))
    return Split(StringRef::npos, 0);

  // If we're reflowing into a line with content indent, only reflow the next
  // line if its starting whitespace matches the content indent.
  size_t Trimmed = Content[LineIndex].find_first_not_of(Blanks);
  if (LineIndex) {
    unsigned PreviousContentIndent = getContentIndent(LineIndex - 1);
    if (PreviousContentIndent && Trimmed != StringRef::npos &&
        Trimmed != PreviousContentIndent) {
      return Split(StringRef::npos, 0);
    }
  }

  return Split(0, Trimmed != StringRef::npos ? Trimmed : 0);
}

bool BreakableBlockComment::introducesBreakBeforeToken() const {
  // A break is introduced when we want delimiters on newline.
  return DelimitersOnNewline &&
         Lines[0].substr(1).find_first_not_of(Blanks) != StringRef::npos;
}

void BreakableBlockComment::reflow(unsigned LineIndex,
                                   WhitespaceManager &Whitespaces) const {
  StringRef TrimmedContent = Content[LineIndex].ltrim(Blanks);
  // Here we need to reflow.
  assert(Tokens[LineIndex - 1] == Tokens[LineIndex] &&
         "Reflowing whitespace within a token");
  // This is the offset of the end of the last line relative to the start of
  // the token text in the token.
  unsigned WhitespaceOffsetInToken = Content[LineIndex - 1].data() +
                                     Content[LineIndex - 1].size() -
                                     tokenAt(LineIndex).TokenText.data();
  unsigned WhitespaceLength = TrimmedContent.data() -
                              tokenAt(LineIndex).TokenText.data() -
                              WhitespaceOffsetInToken;
  Whitespaces.replaceWhitespaceInToken(
      tokenAt(LineIndex), WhitespaceOffsetInToken,
      /*ReplaceChars=*/WhitespaceLength, /*PreviousPostfix=*/"",
      /*CurrentPrefix=*/ReflowPrefix, InPPDirective, /*Newlines=*/0,
      /*Spaces=*/0);
}

void BreakableBlockComment::adaptStartOfLine(
    unsigned LineIndex, WhitespaceManager &Whitespaces) const {
  if (LineIndex == 0) {
    if (DelimitersOnNewline) {
      // Since we're breaking at index 1 below, the break position and the
      // break length are the same.
      // Note: this works because getCommentSplit is careful never to split at
      // the beginning of a line.
      size_t BreakLength = Lines[0].substr(1).find_first_not_of(Blanks);
      if (BreakLength != StringRef::npos) {
        insertBreak(LineIndex, 0, Split(1, BreakLength), /*ContentIndent=*/0,
                    Whitespaces);
      }
    }
    return;
  }
  // Here no reflow with the previous line will happen.
  // Fix the decoration of the line at LineIndex.
  StringRef Prefix = Decoration;
  if (Content[LineIndex].empty()) {
    if (LineIndex + 1 == Lines.size()) {
      if (!LastLineNeedsDecoration) {
        // If the last line was empty, we don't need a prefix, as the */ will
        // line up with the decoration (if it exists).
        Prefix = "";
      }
    } else if (!Decoration.empty()) {
      // For other empty lines, if we do have a decoration, adapt it to not
      // contain a trailing whitespace.
      Prefix = Prefix.substr(0, 1);
    }
  } else if (ContentColumn[LineIndex] == 1) {
    // This line starts immediately after the decorating *.
    Prefix = Prefix.substr(0, 1);
  }
  // This is the offset of the end of the last line relative to the start of the
  // token text in the token.
  unsigned WhitespaceOffsetInToken = Content[LineIndex - 1].data() +
                                     Content[LineIndex - 1].size() -
                                     tokenAt(LineIndex).TokenText.data();
  unsigned WhitespaceLength = Content[LineIndex].data() -
                              tokenAt(LineIndex).TokenText.data() -
                              WhitespaceOffsetInToken;
  Whitespaces.replaceWhitespaceInToken(
      tokenAt(LineIndex), WhitespaceOffsetInToken, WhitespaceLength, "", Prefix,
      InPPDirective, /*Newlines=*/1, ContentColumn[LineIndex] - Prefix.size());
}

BreakableToken::Split
BreakableBlockComment::getSplitAfterLastLine(unsigned TailOffset) const {
  if (DelimitersOnNewline) {
    // Replace the trailing whitespace of the last line with a newline.
    // In case the last line is empty, the ending '*/' is already on its own
    // line.
    StringRef Line = Content.back().substr(TailOffset);
    StringRef TrimmedLine = Line.rtrim(Blanks);
    if (!TrimmedLine.empty())
      return Split(TrimmedLine.size(), Line.size() - TrimmedLine.size());
  }
  return Split(StringRef::npos, 0);
}

bool BreakableBlockComment::mayReflow(
    unsigned LineIndex, const llvm::Regex &CommentPragmasRegex) const {
  // Content[LineIndex] may exclude the indent after the '*' decoration. In that
  // case, we compute the start of the comment pragma manually.
  StringRef IndentContent = Content[LineIndex];
  if (Lines[LineIndex].ltrim(Blanks).starts_with("*"))
    IndentContent = Lines[LineIndex].ltrim(Blanks).substr(1);
  return LineIndex > 0 && !CommentPragmasRegex.match(IndentContent) &&
         mayReflowContent(Content[LineIndex]) && !Tok.Finalized &&
         !switchesFormatting(tokenAt(LineIndex));
}

BreakableLineCommentSection::BreakableLineCommentSection(
    const FormatToken &Token, unsigned StartColumn, bool InPPDirective,
    encoding::Encoding Encoding, const FormatStyle &Style)
    : BreakableComment(Token, StartColumn, InPPDirective, Encoding, Style) {
  assert(Tok.is(TT_LineComment) &&
         "line comment section must start with a line comment");
  FormatToken *LineTok = nullptr;
  const int Minimum = Style.SpacesInLineCommentPrefix.Minimum;
  // How many spaces we changed in the first line of the section, this will be
  // applied in all following lines
  int FirstLineSpaceChange = 0;
  for (const FormatToken *CurrentTok = &Tok;
       CurrentTok && CurrentTok->is(TT_LineComment);
       CurrentTok = CurrentTok->Next) {
    LastLineTok = LineTok;
    StringRef TokenText(CurrentTok->TokenText);
    assert((TokenText.starts_with("//") || TokenText.starts_with("#")) &&
           "unsupported line comment prefix, '//' and '#' are supported");
    size_t FirstLineIndex = Lines.size();
    TokenText.split(Lines, "\n");
    Content.resize(Lines.size());
    ContentColumn.resize(Lines.size());
    PrefixSpaceChange.resize(Lines.size());
    Tokens.resize(Lines.size());
    Prefix.resize(Lines.size());
    OriginalPrefix.resize(Lines.size());
    for (size_t i = FirstLineIndex, e = Lines.size(); i < e; ++i) {
      Lines[i] = Lines[i].ltrim(Blanks);
      StringRef IndentPrefix = getLineCommentIndentPrefix(Lines[i], Style);
      OriginalPrefix[i] = IndentPrefix;
      const int SpacesInPrefix = llvm::count(IndentPrefix, ' ');

      // This lambda also considers multibyte character that is not handled in
      // functions like isPunctuation provided by CharInfo.
      const auto NoSpaceBeforeFirstCommentChar = [&]() {
        assert(Lines[i].size() > IndentPrefix.size());
        const char FirstCommentChar = Lines[i][IndentPrefix.size()];
        const unsigned FirstCharByteSize =
            encoding::getCodePointNumBytes(FirstCommentChar, Encoding);
        if (encoding::columnWidth(
                Lines[i].substr(IndentPrefix.size(), FirstCharByteSize),
                Encoding) != 1) {
          return false;
        }
        // In C-like comments, add a space before #. For example this is useful
        // to preserve the relative indentation when commenting out code with
        // #includes.
        //
        // In languages using # as the comment leader such as proto, don't
        // add a space to support patterns like:
        // #########
        // # section
        // #########
        if (FirstCommentChar == '#' && !TokenText.starts_with("#"))
          return false;
        return FirstCommentChar == '\\' || isPunctuation(FirstCommentChar) ||
               isHorizontalWhitespace(FirstCommentChar);
      };

      // On the first line of the comment section we calculate how many spaces
      // are to be added or removed, all lines after that just get only the
      // change and we will not look at the maximum anymore. Additionally to the
      // actual first line, we calculate that when the non space Prefix changes,
      // e.g. from "///" to "//".
      if (i == 0 || OriginalPrefix[i].rtrim(Blanks) !=
                        OriginalPrefix[i - 1].rtrim(Blanks)) {
        if (SpacesInPrefix < Minimum && Lines[i].size() > IndentPrefix.size() &&
            !NoSpaceBeforeFirstCommentChar()) {
          FirstLineSpaceChange = Minimum - SpacesInPrefix;
        } else if (static_cast<unsigned>(SpacesInPrefix) >
                   Style.SpacesInLineCommentPrefix.Maximum) {
          FirstLineSpaceChange =
              Style.SpacesInLineCommentPrefix.Maximum - SpacesInPrefix;
        } else {
          FirstLineSpaceChange = 0;
        }
      }

      if (Lines[i].size() != IndentPrefix.size()) {
        PrefixSpaceChange[i] = FirstLineSpaceChange;

        if (SpacesInPrefix + PrefixSpaceChange[i] < Minimum) {
          PrefixSpaceChange[i] +=
              Minimum - (SpacesInPrefix + PrefixSpaceChange[i]);
        }

        assert(Lines[i].size() > IndentPrefix.size());
        const auto FirstNonSpace = Lines[i][IndentPrefix.size()];
        const bool IsFormatComment = LineTok && switchesFormatting(*LineTok);
        const bool LineRequiresLeadingSpace =
            !NoSpaceBeforeFirstCommentChar() ||
            (FirstNonSpace == '}' && FirstLineSpaceChange != 0);
        const bool AllowsSpaceChange =
            !IsFormatComment &&
            (SpacesInPrefix != 0 || LineRequiresLeadingSpace);

        if (PrefixSpaceChange[i] > 0 && AllowsSpaceChange) {
          Prefix[i] = IndentPrefix.str();
          Prefix[i].append(PrefixSpaceChange[i], ' ');
        } else if (PrefixSpaceChange[i] < 0 && AllowsSpaceChange) {
          Prefix[i] = IndentPrefix
                          .drop_back(std::min<std::size_t>(
                              -PrefixSpaceChange[i], SpacesInPrefix))
                          .str();
        } else {
          Prefix[i] = IndentPrefix.str();
        }
      } else {
        // If the IndentPrefix is the whole line, there is no content and we
        // drop just all space
        Prefix[i] = IndentPrefix.drop_back(SpacesInPrefix).str();
      }

      Tokens[i] = LineTok;
      Content[i] = Lines[i].substr(IndentPrefix.size());
      ContentColumn[i] =
          StartColumn + encoding::columnWidthWithTabs(Prefix[i], StartColumn,
                                                      Style.TabWidth, Encoding);

      // Calculate the end of the non-whitespace text in this line.
      size_t EndOfLine = Content[i].find_last_not_of(Blanks);
      if (EndOfLine == StringRef::npos)
        EndOfLine = Content[i].size();
      else
        ++EndOfLine;
      Content[i] = Content[i].substr(0, EndOfLine);
    }
    LineTok = CurrentTok->Next;
    if (CurrentTok->Next && !CurrentTok->Next->ContinuesLineCommentSection) {
      // A line comment section needs to broken by a line comment that is
      // preceded by at least two newlines. Note that we put this break here
      // instead of breaking at a previous stage during parsing, since that
      // would split the contents of the enum into two unwrapped lines in this
      // example, which is undesirable:
      // enum A {
      //   a, // comment about a
      //
      //   // comment about b
      //   b
      // };
      //
      // FIXME: Consider putting separate line comment sections as children to
      // the unwrapped line instead.
      break;
    }
  }
}

unsigned
BreakableLineCommentSection::getRangeLength(unsigned LineIndex, unsigned Offset,
                                            StringRef::size_type Length,
                                            unsigned StartColumn) const {
  return encoding::columnWidthWithTabs(
      Content[LineIndex].substr(Offset, Length), StartColumn, Style.TabWidth,
      Encoding);
}

unsigned
BreakableLineCommentSection::getContentStartColumn(unsigned LineIndex,
                                                   bool /*Break*/) const {
  return ContentColumn[LineIndex];
}

void BreakableLineCommentSection::insertBreak(
    unsigned LineIndex, unsigned TailOffset, Split Split,
    unsigned ContentIndent, WhitespaceManager &Whitespaces) const {
  StringRef Text = Content[LineIndex].substr(TailOffset);
  // Compute the offset of the split relative to the beginning of the token
  // text.
  unsigned BreakOffsetInToken =
      Text.data() - tokenAt(LineIndex).TokenText.data() + Split.first;
  unsigned CharsToRemove = Split.second;
  Whitespaces.replaceWhitespaceInToken(
      tokenAt(LineIndex), BreakOffsetInToken, CharsToRemove, "",
      Prefix[LineIndex], InPPDirective, /*Newlines=*/1,
      /*Spaces=*/ContentColumn[LineIndex] - Prefix[LineIndex].size());
}

BreakableComment::Split BreakableLineCommentSection::getReflowSplit(
    unsigned LineIndex, const llvm::Regex &CommentPragmasRegex) const {
  if (!mayReflow(LineIndex, CommentPragmasRegex))
    return Split(StringRef::npos, 0);

  size_t Trimmed = Content[LineIndex].find_first_not_of(Blanks);

  // In a line comment section each line is a separate token; thus, after a
  // split we replace all whitespace before the current line comment token
  // (which does not need to be included in the split), plus the start of the
  // line up to where the content starts.
  return Split(0, Trimmed != StringRef::npos ? Trimmed : 0);
}

void BreakableLineCommentSection::reflow(unsigned LineIndex,
                                         WhitespaceManager &Whitespaces) const {
  if (LineIndex > 0 && Tokens[LineIndex] != Tokens[LineIndex - 1]) {
    // Reflow happens between tokens. Replace the whitespace between the
    // tokens by the empty string.
    Whitespaces.replaceWhitespace(
        *Tokens[LineIndex], /*Newlines=*/0, /*Spaces=*/0,
        /*StartOfTokenColumn=*/StartColumn, /*IsAligned=*/true,
        /*InPPDirective=*/false);
  } else if (LineIndex > 0) {
    // In case we're reflowing after the '\' in:
    //
    //   // line comment \
    //   // line 2
    //
    // the reflow happens inside the single comment token (it is a single line
    // comment with an unescaped newline).
    // Replace the whitespace between the '\' and '//' with the empty string.
    //
    // Offset points to after the '\' relative to start of the token.
    unsigned Offset = Lines[LineIndex - 1].data() +
                      Lines[LineIndex - 1].size() -
                      tokenAt(LineIndex - 1).TokenText.data();
    // WhitespaceLength is the number of chars between the '\' and the '//' on
    // the next line.
    unsigned WhitespaceLength =
        Lines[LineIndex].data() - tokenAt(LineIndex).TokenText.data() - Offset;
    Whitespaces.replaceWhitespaceInToken(*Tokens[LineIndex], Offset,
                                         /*ReplaceChars=*/WhitespaceLength,
                                         /*PreviousPostfix=*/"",
                                         /*CurrentPrefix=*/"",
                                         /*InPPDirective=*/false,
                                         /*Newlines=*/0,
                                         /*Spaces=*/0);
  }
  // Replace the indent and prefix of the token with the reflow prefix.
  unsigned Offset =
      Lines[LineIndex].data() - tokenAt(LineIndex).TokenText.data();
  unsigned WhitespaceLength =
      Content[LineIndex].data() - Lines[LineIndex].data();
  Whitespaces.replaceWhitespaceInToken(*Tokens[LineIndex], Offset,
                                       /*ReplaceChars=*/WhitespaceLength,
                                       /*PreviousPostfix=*/"",
                                       /*CurrentPrefix=*/ReflowPrefix,
                                       /*InPPDirective=*/false,
                                       /*Newlines=*/0,
                                       /*Spaces=*/0);
}

void BreakableLineCommentSection::adaptStartOfLine(
    unsigned LineIndex, WhitespaceManager &Whitespaces) const {
  // If this is the first line of a token, we need to inform Whitespace Manager
  // about it: either adapt the whitespace range preceding it, or mark it as an
  // untouchable token.
  // This happens for instance here:
  // // line 1 \
  // // line 2
  if (LineIndex > 0 && Tokens[LineIndex] != Tokens[LineIndex - 1]) {
    // This is the first line for the current token, but no reflow with the
    // previous token is necessary. However, we still may need to adjust the
    // start column. Note that ContentColumn[LineIndex] is the expected
    // content column after a possible update to the prefix, hence the prefix
    // length change is included.
    unsigned LineColumn =
        ContentColumn[LineIndex] -
        (Content[LineIndex].data() - Lines[LineIndex].data()) +
        (OriginalPrefix[LineIndex].size() - Prefix[LineIndex].size());

    // We always want to create a replacement instead of adding an untouchable
    // token, even if LineColumn is the same as the original column of the
    // token. This is because WhitespaceManager doesn't align trailing
    // comments if they are untouchable.
    Whitespaces.replaceWhitespace(*Tokens[LineIndex],
                                  /*Newlines=*/1,
                                  /*Spaces=*/LineColumn,
                                  /*StartOfTokenColumn=*/LineColumn,
                                  /*IsAligned=*/true,
                                  /*InPPDirective=*/false);
  }
  if (OriginalPrefix[LineIndex] != Prefix[LineIndex]) {
    // Adjust the prefix if necessary.
    const auto SpacesToRemove = -std::min(PrefixSpaceChange[LineIndex], 0);
    const auto SpacesToAdd = std::max(PrefixSpaceChange[LineIndex], 0);
    Whitespaces.replaceWhitespaceInToken(
        tokenAt(LineIndex), OriginalPrefix[LineIndex].size() - SpacesToRemove,
        /*ReplaceChars=*/SpacesToRemove, "", "", /*InPPDirective=*/false,
        /*Newlines=*/0, /*Spaces=*/SpacesToAdd);
  }
}

void BreakableLineCommentSection::updateNextToken(LineState &State) const {
  if (LastLineTok)
    State.NextToken = LastLineTok->Next;
}

bool BreakableLineCommentSection::mayReflow(
    unsigned LineIndex, const llvm::Regex &CommentPragmasRegex) const {
  // Line comments have the indent as part of the prefix, so we need to
  // recompute the start of the line.
  StringRef IndentContent = Content[LineIndex];
  if (Lines[LineIndex].starts_with("//"))
    IndentContent = Lines[LineIndex].substr(2);
  // FIXME: Decide whether we want to reflow non-regular indents:
  // Currently, we only reflow when the OriginalPrefix[LineIndex] matches the
  // OriginalPrefix[LineIndex-1]. That means we don't reflow
  // // text that protrudes
  // //    into text with different indent
  // We do reflow in that case in block comments.
  return LineIndex > 0 && !CommentPragmasRegex.match(IndentContent) &&
         mayReflowContent(Content[LineIndex]) && !Tok.Finalized &&
         !switchesFormatting(tokenAt(LineIndex)) &&
         OriginalPrefix[LineIndex] == OriginalPrefix[LineIndex - 1];
}

} // namespace format
} // namespace clang
