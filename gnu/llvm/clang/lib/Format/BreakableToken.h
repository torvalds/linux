//===--- BreakableToken.h - Format C++ code ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Declares BreakableToken, BreakableStringLiteral, BreakableComment,
/// BreakableBlockComment and BreakableLineCommentSection classes, that contain
/// token type-specific logic to break long lines in tokens and reflow content
/// between tokens.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_BREAKABLETOKEN_H
#define LLVM_CLANG_LIB_FORMAT_BREAKABLETOKEN_H

#include "Encoding.h"
#include "WhitespaceManager.h"
#include "llvm/ADT/StringSet.h"

namespace clang {
namespace format {

/// Checks if \p Token switches formatting, like /* clang-format off */.
/// \p Token must be a comment.
bool switchesFormatting(const FormatToken &Token);

struct FormatStyle;

/// Base class for tokens / ranges of tokens that can allow breaking
/// within the tokens - for example, to avoid whitespace beyond the column
/// limit, or to reflow text.
///
/// Generally, a breakable token consists of logical lines, addressed by a line
/// index. For example, in a sequence of line comments, each line comment is its
/// own logical line; similarly, for a block comment, each line in the block
/// comment is on its own logical line.
///
/// There are two methods to compute the layout of the token:
/// - getRangeLength measures the number of columns needed for a range of text
///   within a logical line, and
/// - getContentStartColumn returns the start column at which we want the
///   content of a logical line to start (potentially after introducing a line
///   break).
///
/// The mechanism to adapt the layout of the breakable token is organised
/// around the concept of a \c Split, which is a whitespace range that signifies
/// a position of the content of a token where a reformatting might be done.
///
/// Operating with splits is divided into two operations:
/// - getSplit, for finding a split starting at a position,
/// - insertBreak, for executing the split using a whitespace manager.
///
/// There is a pair of operations that are used to compress a long whitespace
/// range with a single space if that will bring the line length under the
/// column limit:
/// - getLineLengthAfterCompression, for calculating the size in columns of the
///   line after a whitespace range has been compressed, and
/// - compressWhitespace, for executing the whitespace compression using a
///   whitespace manager; note that the compressed whitespace may be in the
///   middle of the original line and of the reformatted line.
///
/// For tokens where the whitespace before each line needs to be also
/// reformatted, for example for tokens supporting reflow, there are analogous
/// operations that might be executed before the main line breaking occurs:
/// - getReflowSplit, for finding a split such that the content preceding it
///   needs to be specially reflown,
/// - reflow, for executing the split using a whitespace manager,
/// - introducesBreakBefore, for checking if reformatting the beginning
///   of the content introduces a line break before it,
/// - adaptStartOfLine, for executing the reflow using a whitespace
///   manager.
///
/// For tokens that require the whitespace after the last line to be
/// reformatted, for example in multiline jsdoc comments that require the
/// trailing '*/' to be on a line of itself, there are analogous operations
/// that might be executed after the last line has been reformatted:
/// - getSplitAfterLastLine, for finding a split after the last line that needs
///   to be reflown,
/// - replaceWhitespaceAfterLastLine, for executing the reflow using a
///   whitespace manager.
///
class BreakableToken {
public:
  /// Contains starting character index and length of split.
  typedef std::pair<StringRef::size_type, unsigned> Split;

  virtual ~BreakableToken() {}

  /// Returns the number of lines in this token in the original code.
  virtual unsigned getLineCount() const = 0;

  /// Returns the number of columns required to format the text in the
  /// byte range [\p Offset, \p Offset \c + \p Length).
  ///
  /// \p Offset is the byte offset from the start of the content of the line
  ///    at \p LineIndex.
  ///
  /// \p StartColumn is the column at which the text starts in the formatted
  ///    file, needed to compute tab stops correctly.
  virtual unsigned getRangeLength(unsigned LineIndex, unsigned Offset,
                                  StringRef::size_type Length,
                                  unsigned StartColumn) const = 0;

  /// Returns the number of columns required to format the text following
  /// the byte \p Offset in the line \p LineIndex, including potentially
  /// unbreakable sequences of tokens following after the end of the token.
  ///
  /// \p Offset is the byte offset from the start of the content of the line
  ///    at \p LineIndex.
  ///
  /// \p StartColumn is the column at which the text starts in the formatted
  ///    file, needed to compute tab stops correctly.
  ///
  /// For breakable tokens that never use extra space at the end of a line, this
  /// is equivalent to getRangeLength with a Length of StringRef::npos.
  virtual unsigned getRemainingLength(unsigned LineIndex, unsigned Offset,
                                      unsigned StartColumn) const {
    return getRangeLength(LineIndex, Offset, StringRef::npos, StartColumn);
  }

  /// Returns the column at which content in line \p LineIndex starts,
  /// assuming no reflow.
  ///
  /// If \p Break is true, returns the column at which the line should start
  /// after the line break.
  /// If \p Break is false, returns the column at which the line itself will
  /// start.
  virtual unsigned getContentStartColumn(unsigned LineIndex,
                                         bool Break) const = 0;

  /// Returns additional content indent required for the second line after the
  /// content at line \p LineIndex is broken.
  ///
  // (Next lines do not start with `///` since otherwise -Wdocumentation picks
  // up the example annotations and generates warnings for them)
  // For example, Javadoc @param annotations require and indent of 4 spaces and
  // in this example getContentIndex(1) returns 4.
  // /**
  //  * @param loooooooooooooong line
  //  *     continuation
  //  */
  virtual unsigned getContentIndent(unsigned LineIndex) const { return 0; }

  /// Returns a range (offset, length) at which to break the line at
  /// \p LineIndex, if previously broken at \p TailOffset. If possible, do not
  /// violate \p ColumnLimit, assuming the text starting at \p TailOffset in
  /// the token is formatted starting at ContentStartColumn in the reformatted
  /// file.
  virtual Split getSplit(unsigned LineIndex, unsigned TailOffset,
                         unsigned ColumnLimit, unsigned ContentStartColumn,
                         const llvm::Regex &CommentPragmasRegex) const = 0;

  /// Emits the previously retrieved \p Split via \p Whitespaces.
  virtual void insertBreak(unsigned LineIndex, unsigned TailOffset, Split Split,
                           unsigned ContentIndent,
                           WhitespaceManager &Whitespaces) const = 0;

  /// Returns the number of columns needed to format
  /// \p RemainingTokenColumns, assuming that Split is within the range measured
  /// by \p RemainingTokenColumns, and that the whitespace in Split is reduced
  /// to a single space.
  unsigned getLengthAfterCompression(unsigned RemainingTokenColumns,
                                     Split Split) const;

  /// Replaces the whitespace range described by \p Split with a single
  /// space.
  virtual void compressWhitespace(unsigned LineIndex, unsigned TailOffset,
                                  Split Split,
                                  WhitespaceManager &Whitespaces) const = 0;

  /// Returns whether the token supports reflowing text.
  virtual bool supportsReflow() const { return false; }

  /// Returns a whitespace range (offset, length) of the content at \p
  /// LineIndex such that the content of that line is reflown to the end of the
  /// previous one.
  ///
  /// Returning (StringRef::npos, 0) indicates reflowing is not possible.
  ///
  /// The range will include any whitespace preceding the specified line's
  /// content.
  ///
  /// If the split is not contained within one token, for example when reflowing
  /// line comments, returns (0, <length>).
  virtual Split getReflowSplit(unsigned LineIndex,
                               const llvm::Regex &CommentPragmasRegex) const {
    return Split(StringRef::npos, 0);
  }

  /// Reflows the current line into the end of the previous one.
  virtual void reflow(unsigned LineIndex,
                      WhitespaceManager &Whitespaces) const {}

  /// Returns whether there will be a line break at the start of the
  /// token.
  virtual bool introducesBreakBeforeToken() const { return false; }

  /// Replaces the whitespace between \p LineIndex-1 and \p LineIndex.
  virtual void adaptStartOfLine(unsigned LineIndex,
                                WhitespaceManager &Whitespaces) const {}

  /// Returns a whitespace range (offset, length) of the content at
  /// the last line that needs to be reformatted after the last line has been
  /// reformatted.
  ///
  /// A result having offset == StringRef::npos means that no reformat is
  /// necessary.
  virtual Split getSplitAfterLastLine(unsigned TailOffset) const {
    return Split(StringRef::npos, 0);
  }

  /// Replaces the whitespace from \p SplitAfterLastLine on the last line
  /// after the last line has been formatted by performing a reformatting.
  void replaceWhitespaceAfterLastLine(unsigned TailOffset,
                                      Split SplitAfterLastLine,
                                      WhitespaceManager &Whitespaces) const {
    insertBreak(getLineCount() - 1, TailOffset, SplitAfterLastLine,
                /*ContentIndent=*/0, Whitespaces);
  }

  /// Updates the next token of \p State to the next token after this
  /// one. This can be used when this token manages a set of underlying tokens
  /// as a unit and is responsible for the formatting of the them.
  virtual void updateNextToken(LineState &State) const {}

  /// Adds replacements that are needed when the token is broken. Such as
  /// wrapping a JavaScript string in parentheses after it gets broken with plus
  /// signs.
  virtual void updateAfterBroken(WhitespaceManager &Whitespaces) const {}

protected:
  BreakableToken(const FormatToken &Tok, bool InPPDirective,
                 encoding::Encoding Encoding, const FormatStyle &Style)
      : Tok(Tok), InPPDirective(InPPDirective), Encoding(Encoding),
        Style(Style) {}

  const FormatToken &Tok;
  const bool InPPDirective;
  const encoding::Encoding Encoding;
  const FormatStyle &Style;
};

class BreakableStringLiteral : public BreakableToken {
public:
  /// Creates a breakable token for a single line string literal.
  ///
  /// \p StartColumn specifies the column in which the token will start
  /// after formatting.
  BreakableStringLiteral(const FormatToken &Tok, unsigned StartColumn,
                         StringRef Prefix, StringRef Postfix,
                         unsigned UnbreakableTailLength, bool InPPDirective,
                         encoding::Encoding Encoding, const FormatStyle &Style);

  Split getSplit(unsigned LineIndex, unsigned TailOffset, unsigned ColumnLimit,
                 unsigned ContentStartColumn,
                 const llvm::Regex &CommentPragmasRegex) const override;
  void insertBreak(unsigned LineIndex, unsigned TailOffset, Split Split,
                   unsigned ContentIndent,
                   WhitespaceManager &Whitespaces) const override;
  void compressWhitespace(unsigned LineIndex, unsigned TailOffset, Split Split,
                          WhitespaceManager &Whitespaces) const override {}
  unsigned getLineCount() const override;
  unsigned getRangeLength(unsigned LineIndex, unsigned Offset,
                          StringRef::size_type Length,
                          unsigned StartColumn) const override;
  unsigned getRemainingLength(unsigned LineIndex, unsigned Offset,
                              unsigned StartColumn) const override;
  unsigned getContentStartColumn(unsigned LineIndex, bool Break) const override;

protected:
  // The column in which the token starts.
  unsigned StartColumn;
  // The prefix a line needs after a break in the token.
  StringRef Prefix;
  // The postfix a line needs before introducing a break.
  StringRef Postfix;
  // The token text excluding the prefix and postfix.
  StringRef Line;
  // Length of the sequence of tokens after this string literal that cannot
  // contain line breaks.
  unsigned UnbreakableTailLength;
};

class BreakableStringLiteralUsingOperators : public BreakableStringLiteral {
public:
  enum QuoteStyleType {
    DoubleQuotes,   // The string is quoted with double quotes.
    SingleQuotes,   // The JavaScript string is quoted with single quotes.
    AtDoubleQuotes, // The C# verbatim string is quoted with the at sign and
                    // double quotes.
  };
  /// Creates a breakable token for a single line string literal for C#, Java,
  /// JavaScript, or Verilog.
  ///
  /// \p StartColumn specifies the column in which the token will start
  /// after formatting.
  BreakableStringLiteralUsingOperators(
      const FormatToken &Tok, QuoteStyleType QuoteStyle, bool UnindentPlus,
      unsigned StartColumn, unsigned UnbreakableTailLength, bool InPPDirective,
      encoding::Encoding Encoding, const FormatStyle &Style);
  unsigned getRemainingLength(unsigned LineIndex, unsigned Offset,
                              unsigned StartColumn) const override;
  unsigned getContentStartColumn(unsigned LineIndex, bool Break) const override;
  void insertBreak(unsigned LineIndex, unsigned TailOffset, Split Split,
                   unsigned ContentIndent,
                   WhitespaceManager &Whitespaces) const override;
  void updateAfterBroken(WhitespaceManager &Whitespaces) const override;

protected:
  // Whether braces or parentheses should be inserted around the string to form
  // a concatenation.
  bool BracesNeeded;
  QuoteStyleType QuoteStyle;
  // The braces or parentheses along with the first character which they
  // replace, either a quote or at sign.
  StringRef LeftBraceQuote;
  StringRef RightBraceQuote;
  // Width added to the left due to the added brace or parenthesis. Does not
  // apply to the first line.
  int ContinuationIndent;
};

class BreakableComment : public BreakableToken {
protected:
  /// Creates a breakable token for a comment.
  ///
  /// \p StartColumn specifies the column in which the comment will start after
  /// formatting.
  BreakableComment(const FormatToken &Token, unsigned StartColumn,
                   bool InPPDirective, encoding::Encoding Encoding,
                   const FormatStyle &Style);

public:
  bool supportsReflow() const override { return true; }
  unsigned getLineCount() const override;
  Split getSplit(unsigned LineIndex, unsigned TailOffset, unsigned ColumnLimit,
                 unsigned ContentStartColumn,
                 const llvm::Regex &CommentPragmasRegex) const override;
  void compressWhitespace(unsigned LineIndex, unsigned TailOffset, Split Split,
                          WhitespaceManager &Whitespaces) const override;

protected:
  // Returns the token containing the line at LineIndex.
  const FormatToken &tokenAt(unsigned LineIndex) const;

  // Checks if the content of line LineIndex may be reflown with the previous
  // line.
  virtual bool mayReflow(unsigned LineIndex,
                         const llvm::Regex &CommentPragmasRegex) const = 0;

  // Contains the original text of the lines of the block comment.
  //
  // In case of a block comments, excludes the leading /* in the first line and
  // trailing */ in the last line. In case of line comments, excludes the
  // leading // and spaces.
  SmallVector<StringRef, 16> Lines;

  // Contains the text of the lines excluding all leading and trailing
  // whitespace between the lines. Note that the decoration (if present) is also
  // not considered part of the text.
  SmallVector<StringRef, 16> Content;

  // Tokens[i] contains a reference to the token containing Lines[i] if the
  // whitespace range before that token is managed by this block.
  // Otherwise, Tokens[i] is a null pointer.
  SmallVector<FormatToken *, 16> Tokens;

  // ContentColumn[i] is the target column at which Content[i] should be.
  // Note that this excludes a leading "* " or "*" in case of block comments
  // where all lines have a "*" prefix, or the leading "// " or "//" in case of
  // line comments.
  //
  // In block comments, the first line's target column is always positive. The
  // remaining lines' target columns are relative to the first line to allow
  // correct indentation of comments in \c WhitespaceManager. Thus they can be
  // negative as well (in case the first line needs to be unindented more than
  // there's actual whitespace in another line).
  SmallVector<int, 16> ContentColumn;

  // The intended start column of the first line of text from this section.
  unsigned StartColumn;

  // The prefix to use in front a line that has been reflown up.
  // For example, when reflowing the second line after the first here:
  // // comment 1
  // // comment 2
  // we expect:
  // // comment 1 comment 2
  // and not:
  // // comment 1comment 2
  StringRef ReflowPrefix = " ";
};

class BreakableBlockComment : public BreakableComment {
public:
  BreakableBlockComment(const FormatToken &Token, unsigned StartColumn,
                        unsigned OriginalStartColumn, bool FirstInLine,
                        bool InPPDirective, encoding::Encoding Encoding,
                        const FormatStyle &Style, bool UseCRLF);

  Split getSplit(unsigned LineIndex, unsigned TailOffset, unsigned ColumnLimit,
                 unsigned ContentStartColumn,
                 const llvm::Regex &CommentPragmasRegex) const override;
  unsigned getRangeLength(unsigned LineIndex, unsigned Offset,
                          StringRef::size_type Length,
                          unsigned StartColumn) const override;
  unsigned getRemainingLength(unsigned LineIndex, unsigned Offset,
                              unsigned StartColumn) const override;
  unsigned getContentStartColumn(unsigned LineIndex, bool Break) const override;
  unsigned getContentIndent(unsigned LineIndex) const override;
  void insertBreak(unsigned LineIndex, unsigned TailOffset, Split Split,
                   unsigned ContentIndent,
                   WhitespaceManager &Whitespaces) const override;
  Split getReflowSplit(unsigned LineIndex,
                       const llvm::Regex &CommentPragmasRegex) const override;
  void reflow(unsigned LineIndex,
              WhitespaceManager &Whitespaces) const override;
  bool introducesBreakBeforeToken() const override;
  void adaptStartOfLine(unsigned LineIndex,
                        WhitespaceManager &Whitespaces) const override;
  Split getSplitAfterLastLine(unsigned TailOffset) const override;

  bool mayReflow(unsigned LineIndex,
                 const llvm::Regex &CommentPragmasRegex) const override;

  // Contains Javadoc annotations that require additional indent when continued
  // on multiple lines.
  static const llvm::StringSet<> ContentIndentingJavadocAnnotations;

private:
  // Rearranges the whitespace between Lines[LineIndex-1] and Lines[LineIndex].
  //
  // Updates Content[LineIndex-1] and Content[LineIndex] by stripping off
  // leading and trailing whitespace.
  //
  // Sets ContentColumn to the intended column in which the text at
  // Lines[LineIndex] starts (note that the decoration, if present, is not
  // considered part of the text).
  void adjustWhitespace(unsigned LineIndex, int IndentDelta);

  // The column at which the text of a broken line should start.
  // Note that an optional decoration would go before that column.
  // IndentAtLineBreak is a uniform position for all lines in a block comment,
  // regardless of their relative position.
  // FIXME: Revisit the decision to do this; the main reason was to support
  // patterns like
  // /**************//**
  //  * Comment
  // We could also support such patterns by special casing the first line
  // instead.
  unsigned IndentAtLineBreak;

  // This is to distinguish between the case when the last line was empty and
  // the case when it started with a decoration ("*" or "* ").
  bool LastLineNeedsDecoration;

  // Either "* " if all lines begin with a "*", or empty.
  StringRef Decoration;

  // If this block comment has decorations, this is the column of the start of
  // the decorations.
  unsigned DecorationColumn;

  // If true, make sure that the opening '/**' and the closing '*/' ends on a
  // line of itself. Styles like jsdoc require this for multiline comments.
  bool DelimitersOnNewline;

  // Length of the sequence of tokens after this string literal that cannot
  // contain line breaks.
  unsigned UnbreakableTailLength;
};

class BreakableLineCommentSection : public BreakableComment {
public:
  BreakableLineCommentSection(const FormatToken &Token, unsigned StartColumn,
                              bool InPPDirective, encoding::Encoding Encoding,
                              const FormatStyle &Style);

  unsigned getRangeLength(unsigned LineIndex, unsigned Offset,
                          StringRef::size_type Length,
                          unsigned StartColumn) const override;
  unsigned getContentStartColumn(unsigned LineIndex, bool Break) const override;
  void insertBreak(unsigned LineIndex, unsigned TailOffset, Split Split,
                   unsigned ContentIndent,
                   WhitespaceManager &Whitespaces) const override;
  Split getReflowSplit(unsigned LineIndex,
                       const llvm::Regex &CommentPragmasRegex) const override;
  void reflow(unsigned LineIndex,
              WhitespaceManager &Whitespaces) const override;
  void adaptStartOfLine(unsigned LineIndex,
                        WhitespaceManager &Whitespaces) const override;
  void updateNextToken(LineState &State) const override;
  bool mayReflow(unsigned LineIndex,
                 const llvm::Regex &CommentPragmasRegex) const override;

private:
  // OriginalPrefix[i] contains the original prefix of line i, including
  // trailing whitespace before the start of the content. The indentation
  // preceding the prefix is not included.
  // For example, if the line is:
  // // content
  // then the original prefix is "// ".
  SmallVector<StringRef, 16> OriginalPrefix;

  /// Prefix[i] + SpacesToAdd[i] contains the intended leading "//" with
  /// trailing spaces to account for the indentation of content within the
  /// comment at line i after formatting. It can be different than the original
  /// prefix.
  /// When the original line starts like this:
  /// //content
  /// Then the OriginalPrefix[i] is "//", but the Prefix[i] is "// " in the LLVM
  /// style.
  /// When the line starts like:
  /// // content
  /// And we want to remove the spaces the OriginalPrefix[i] is "// " and
  /// Prefix[i] is "//".
  SmallVector<std::string, 16> Prefix;

  /// How many spaces are added or removed from the OriginalPrefix to form
  /// Prefix.
  SmallVector<int, 16> PrefixSpaceChange;

  /// The token to which the last line of this breakable token belongs
  /// to; nullptr if that token is the initial token.
  ///
  /// The distinction is because if the token of the last line of this breakable
  /// token is distinct from the initial token, this breakable token owns the
  /// whitespace before the token of the last line, and the whitespace manager
  /// must be able to modify it.
  FormatToken *LastLineTok = nullptr;
};
} // namespace format
} // namespace clang

#endif
