//===--- TextDiagnostic.cpp - Text Diagnostic Pretty-Printing -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/TextDiagnostic.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Locale.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <optional>

using namespace clang;

static const enum raw_ostream::Colors noteColor = raw_ostream::CYAN;
static const enum raw_ostream::Colors remarkColor =
  raw_ostream::BLUE;
static const enum raw_ostream::Colors fixitColor =
  raw_ostream::GREEN;
static const enum raw_ostream::Colors caretColor =
  raw_ostream::GREEN;
static const enum raw_ostream::Colors warningColor =
  raw_ostream::MAGENTA;
static const enum raw_ostream::Colors templateColor =
  raw_ostream::CYAN;
static const enum raw_ostream::Colors errorColor = raw_ostream::RED;
static const enum raw_ostream::Colors fatalColor = raw_ostream::RED;
// Used for changing only the bold attribute.
static const enum raw_ostream::Colors savedColor =
  raw_ostream::SAVEDCOLOR;

// Magenta is taken for 'warning'. Red is already 'error' and 'cyan'
// is already taken for 'note'. Green is already used to underline
// source ranges. White and black are bad because of the usual
// terminal backgrounds. Which leaves us only with TWO options.
static constexpr raw_ostream::Colors CommentColor = raw_ostream::YELLOW;
static constexpr raw_ostream::Colors LiteralColor = raw_ostream::GREEN;
static constexpr raw_ostream::Colors KeywordColor = raw_ostream::BLUE;

/// Add highlights to differences in template strings.
static void applyTemplateHighlighting(raw_ostream &OS, StringRef Str,
                                      bool &Normal, bool Bold) {
  while (true) {
    size_t Pos = Str.find(ToggleHighlight);
    OS << Str.slice(0, Pos);
    if (Pos == StringRef::npos)
      break;

    Str = Str.substr(Pos + 1);
    if (Normal)
      OS.changeColor(templateColor, true);
    else {
      OS.resetColor();
      if (Bold)
        OS.changeColor(savedColor, true);
    }
    Normal = !Normal;
  }
}

/// Number of spaces to indent when word-wrapping.
const unsigned WordWrapIndentation = 6;

static int bytesSincePreviousTabOrLineBegin(StringRef SourceLine, size_t i) {
  int bytes = 0;
  while (0<i) {
    if (SourceLine[--i]=='\t')
      break;
    ++bytes;
  }
  return bytes;
}

/// returns a printable representation of first item from input range
///
/// This function returns a printable representation of the next item in a line
///  of source. If the next byte begins a valid and printable character, that
///  character is returned along with 'true'.
///
/// Otherwise, if the next byte begins a valid, but unprintable character, a
///  printable, escaped representation of the character is returned, along with
///  'false'. Otherwise a printable, escaped representation of the next byte
///  is returned along with 'false'.
///
/// \note The index is updated to be used with a subsequent call to
///        printableTextForNextCharacter.
///
/// \param SourceLine The line of source
/// \param I Pointer to byte index,
/// \param TabStop used to expand tabs
/// \return pair(printable text, 'true' iff original text was printable)
///
static std::pair<SmallString<16>, bool>
printableTextForNextCharacter(StringRef SourceLine, size_t *I,
                              unsigned TabStop) {
  assert(I && "I must not be null");
  assert(*I < SourceLine.size() && "must point to a valid index");

  if (SourceLine[*I] == '\t') {
    assert(0 < TabStop && TabStop <= DiagnosticOptions::MaxTabStop &&
           "Invalid -ftabstop value");
    unsigned Col = bytesSincePreviousTabOrLineBegin(SourceLine, *I);
    unsigned NumSpaces = TabStop - (Col % TabStop);
    assert(0 < NumSpaces && NumSpaces <= TabStop
           && "Invalid computation of space amt");
    ++(*I);

    SmallString<16> ExpandedTab;
    ExpandedTab.assign(NumSpaces, ' ');
    return std::make_pair(ExpandedTab, true);
  }

  const unsigned char *Begin = SourceLine.bytes_begin() + *I;

  // Fast path for the common ASCII case.
  if (*Begin < 0x80 && llvm::sys::locale::isPrint(*Begin)) {
    ++(*I);
    return std::make_pair(SmallString<16>(Begin, Begin + 1), true);
  }
  unsigned CharSize = llvm::getNumBytesForUTF8(*Begin);
  const unsigned char *End = Begin + CharSize;

  // Convert it to UTF32 and check if it's printable.
  if (End <= SourceLine.bytes_end() && llvm::isLegalUTF8Sequence(Begin, End)) {
    llvm::UTF32 C;
    llvm::UTF32 *CPtr = &C;

    // Begin and end before conversion.
    unsigned char const *OriginalBegin = Begin;
    llvm::ConversionResult Res = llvm::ConvertUTF8toUTF32(
        &Begin, End, &CPtr, CPtr + 1, llvm::strictConversion);
    (void)Res;
    assert(Res == llvm::conversionOK);
    assert(OriginalBegin < Begin);
    assert(unsigned(Begin - OriginalBegin) == CharSize);

    (*I) += (Begin - OriginalBegin);

    // Valid, multi-byte, printable UTF8 character.
    if (llvm::sys::locale::isPrint(C))
      return std::make_pair(SmallString<16>(OriginalBegin, End), true);

    // Valid but not printable.
    SmallString<16> Str("<U+>");
    while (C) {
      Str.insert(Str.begin() + 3, llvm::hexdigit(C % 16));
      C /= 16;
    }
    while (Str.size() < 8)
      Str.insert(Str.begin() + 3, llvm::hexdigit(0));
    return std::make_pair(Str, false);
  }

  // Otherwise, not printable since it's not valid UTF8.
  SmallString<16> ExpandedByte("<XX>");
  unsigned char Byte = SourceLine[*I];
  ExpandedByte[1] = llvm::hexdigit(Byte / 16);
  ExpandedByte[2] = llvm::hexdigit(Byte % 16);
  ++(*I);
  return std::make_pair(ExpandedByte, false);
}

static void expandTabs(std::string &SourceLine, unsigned TabStop) {
  size_t I = SourceLine.size();
  while (I > 0) {
    I--;
    if (SourceLine[I] != '\t')
      continue;
    size_t TmpI = I;
    auto [Str, Printable] =
        printableTextForNextCharacter(SourceLine, &TmpI, TabStop);
    SourceLine.replace(I, 1, Str.c_str());
  }
}

/// \p BytesOut:
///  A mapping from columns to the byte of the source line that produced the
///  character displaying at that column. This is the inverse of \p ColumnsOut.
///
/// The last element in the array is the number of bytes in the source string.
///
/// example: (given a tabstop of 8)
///
///    "a \t \u3042" -> {0,1,2,-1,-1,-1,-1,-1,3,4,-1,7}
///
///  (\\u3042 is represented in UTF-8 by three bytes and takes two columns to
///   display)
///
/// \p ColumnsOut:
///  A mapping from the bytes
///  of the printable representation of the line to the columns those printable
///  characters will appear at (numbering the first column as 0).
///
/// If a byte 'i' corresponds to multiple columns (e.g. the byte contains a tab
///  character) then the array will map that byte to the first column the
///  tab appears at and the next value in the map will have been incremented
///  more than once.
///
/// If a byte is the first in a sequence of bytes that together map to a single
///  entity in the output, then the array will map that byte to the appropriate
///  column while the subsequent bytes will be -1.
///
/// The last element in the array does not correspond to any byte in the input
///  and instead is the number of columns needed to display the source
///
/// example: (given a tabstop of 8)
///
///    "a \t \u3042" -> {0,1,2,8,9,-1,-1,11}
///
///  (\\u3042 is represented in UTF-8 by three bytes and takes two columns to
///   display)
static void genColumnByteMapping(StringRef SourceLine, unsigned TabStop,
                                 SmallVectorImpl<int> &BytesOut,
                                 SmallVectorImpl<int> &ColumnsOut) {
  assert(BytesOut.empty());
  assert(ColumnsOut.empty());

  if (SourceLine.empty()) {
    BytesOut.resize(1u, 0);
    ColumnsOut.resize(1u, 0);
    return;
  }

  ColumnsOut.resize(SourceLine.size() + 1, -1);

  int Columns = 0;
  size_t I = 0;
  while (I < SourceLine.size()) {
    ColumnsOut[I] = Columns;
    BytesOut.resize(Columns + 1, -1);
    BytesOut.back() = I;
    auto [Str, Printable] =
        printableTextForNextCharacter(SourceLine, &I, TabStop);
    Columns += llvm::sys::locale::columnWidth(Str);
  }

  ColumnsOut.back() = Columns;
  BytesOut.resize(Columns + 1, -1);
  BytesOut.back() = I;
}

namespace {
struct SourceColumnMap {
  SourceColumnMap(StringRef SourceLine, unsigned TabStop)
  : m_SourceLine(SourceLine) {

    genColumnByteMapping(SourceLine, TabStop, m_columnToByte, m_byteToColumn);

    assert(m_byteToColumn.size()==SourceLine.size()+1);
    assert(0 < m_byteToColumn.size() && 0 < m_columnToByte.size());
    assert(m_byteToColumn.size()
           == static_cast<unsigned>(m_columnToByte.back()+1));
    assert(static_cast<unsigned>(m_byteToColumn.back()+1)
           == m_columnToByte.size());
  }
  int columns() const { return m_byteToColumn.back(); }
  int bytes() const { return m_columnToByte.back(); }

  /// Map a byte to the column which it is at the start of, or return -1
  /// if it is not at the start of a column (for a UTF-8 trailing byte).
  int byteToColumn(int n) const {
    assert(0<=n && n<static_cast<int>(m_byteToColumn.size()));
    return m_byteToColumn[n];
  }

  /// Map a byte to the first column which contains it.
  int byteToContainingColumn(int N) const {
    assert(0 <= N && N < static_cast<int>(m_byteToColumn.size()));
    while (m_byteToColumn[N] == -1)
      --N;
    return m_byteToColumn[N];
  }

  /// Map a column to the byte which starts the column, or return -1 if
  /// the column the second or subsequent column of an expanded tab or similar
  /// multi-column entity.
  int columnToByte(int n) const {
    assert(0<=n && n<static_cast<int>(m_columnToByte.size()));
    return m_columnToByte[n];
  }

  /// Map from a byte index to the next byte which starts a column.
  int startOfNextColumn(int N) const {
    assert(0 <= N && N < static_cast<int>(m_byteToColumn.size() - 1));
    while (byteToColumn(++N) == -1) {}
    return N;
  }

  /// Map from a byte index to the previous byte which starts a column.
  int startOfPreviousColumn(int N) const {
    assert(0 < N && N < static_cast<int>(m_byteToColumn.size()));
    while (byteToColumn(--N) == -1) {}
    return N;
  }

  StringRef getSourceLine() const {
    return m_SourceLine;
  }

private:
  const std::string m_SourceLine;
  SmallVector<int,200> m_byteToColumn;
  SmallVector<int,200> m_columnToByte;
};
} // end anonymous namespace

/// When the source code line we want to print is too long for
/// the terminal, select the "interesting" region.
static void selectInterestingSourceRegion(std::string &SourceLine,
                                          std::string &CaretLine,
                                          std::string &FixItInsertionLine,
                                          unsigned Columns,
                                          const SourceColumnMap &map) {
  unsigned CaretColumns = CaretLine.size();
  unsigned FixItColumns = llvm::sys::locale::columnWidth(FixItInsertionLine);
  unsigned MaxColumns = std::max(static_cast<unsigned>(map.columns()),
                                 std::max(CaretColumns, FixItColumns));
  // if the number of columns is less than the desired number we're done
  if (MaxColumns <= Columns)
    return;

  // No special characters are allowed in CaretLine.
  assert(llvm::none_of(CaretLine, [](char c) { return c < ' ' || '~' < c; }));

  // Find the slice that we need to display the full caret line
  // correctly.
  unsigned CaretStart = 0, CaretEnd = CaretLine.size();
  for (; CaretStart != CaretEnd; ++CaretStart)
    if (!isWhitespace(CaretLine[CaretStart]))
      break;

  for (; CaretEnd != CaretStart; --CaretEnd)
    if (!isWhitespace(CaretLine[CaretEnd - 1]))
      break;

  // caret has already been inserted into CaretLine so the above whitespace
  // check is guaranteed to include the caret

  // If we have a fix-it line, make sure the slice includes all of the
  // fix-it information.
  if (!FixItInsertionLine.empty()) {
    unsigned FixItStart = 0, FixItEnd = FixItInsertionLine.size();
    for (; FixItStart != FixItEnd; ++FixItStart)
      if (!isWhitespace(FixItInsertionLine[FixItStart]))
        break;

    for (; FixItEnd != FixItStart; --FixItEnd)
      if (!isWhitespace(FixItInsertionLine[FixItEnd - 1]))
        break;

    // We can safely use the byte offset FixItStart as the column offset
    // because the characters up until FixItStart are all ASCII whitespace
    // characters.
    unsigned FixItStartCol = FixItStart;
    unsigned FixItEndCol
      = llvm::sys::locale::columnWidth(FixItInsertionLine.substr(0, FixItEnd));

    CaretStart = std::min(FixItStartCol, CaretStart);
    CaretEnd = std::max(FixItEndCol, CaretEnd);
  }

  // CaretEnd may have been set at the middle of a character
  // If it's not at a character's first column then advance it past the current
  //   character.
  while (static_cast<int>(CaretEnd) < map.columns() &&
         -1 == map.columnToByte(CaretEnd))
    ++CaretEnd;

  assert((static_cast<int>(CaretStart) > map.columns() ||
          -1!=map.columnToByte(CaretStart)) &&
         "CaretStart must not point to a column in the middle of a source"
         " line character");
  assert((static_cast<int>(CaretEnd) > map.columns() ||
          -1!=map.columnToByte(CaretEnd)) &&
         "CaretEnd must not point to a column in the middle of a source line"
         " character");

  // CaretLine[CaretStart, CaretEnd) contains all of the interesting
  // parts of the caret line. While this slice is smaller than the
  // number of columns we have, try to grow the slice to encompass
  // more context.

  unsigned SourceStart = map.columnToByte(std::min<unsigned>(CaretStart,
                                                             map.columns()));
  unsigned SourceEnd = map.columnToByte(std::min<unsigned>(CaretEnd,
                                                           map.columns()));

  unsigned CaretColumnsOutsideSource = CaretEnd-CaretStart
    - (map.byteToColumn(SourceEnd)-map.byteToColumn(SourceStart));

  char const *front_ellipse = "  ...";
  char const *front_space   = "     ";
  char const *back_ellipse = "...";
  unsigned ellipses_space = strlen(front_ellipse) + strlen(back_ellipse);

  unsigned TargetColumns = Columns;
  // Give us extra room for the ellipses
  //  and any of the caret line that extends past the source
  if (TargetColumns > ellipses_space+CaretColumnsOutsideSource)
    TargetColumns -= ellipses_space+CaretColumnsOutsideSource;

  while (SourceStart>0 || SourceEnd<SourceLine.size()) {
    bool ExpandedRegion = false;

    if (SourceStart>0) {
      unsigned NewStart = map.startOfPreviousColumn(SourceStart);

      // Skip over any whitespace we see here; we're looking for
      // another bit of interesting text.
      // FIXME: Detect non-ASCII whitespace characters too.
      while (NewStart && isWhitespace(SourceLine[NewStart]))
        NewStart = map.startOfPreviousColumn(NewStart);

      // Skip over this bit of "interesting" text.
      while (NewStart) {
        unsigned Prev = map.startOfPreviousColumn(NewStart);
        if (isWhitespace(SourceLine[Prev]))
          break;
        NewStart = Prev;
      }

      assert(map.byteToColumn(NewStart) != -1);
      unsigned NewColumns = map.byteToColumn(SourceEnd) -
                              map.byteToColumn(NewStart);
      if (NewColumns <= TargetColumns) {
        SourceStart = NewStart;
        ExpandedRegion = true;
      }
    }

    if (SourceEnd<SourceLine.size()) {
      unsigned NewEnd = map.startOfNextColumn(SourceEnd);

      // Skip over any whitespace we see here; we're looking for
      // another bit of interesting text.
      // FIXME: Detect non-ASCII whitespace characters too.
      while (NewEnd < SourceLine.size() && isWhitespace(SourceLine[NewEnd]))
        NewEnd = map.startOfNextColumn(NewEnd);

      // Skip over this bit of "interesting" text.
      while (NewEnd < SourceLine.size() && isWhitespace(SourceLine[NewEnd]))
        NewEnd = map.startOfNextColumn(NewEnd);

      assert(map.byteToColumn(NewEnd) != -1);
      unsigned NewColumns = map.byteToColumn(NewEnd) -
                              map.byteToColumn(SourceStart);
      if (NewColumns <= TargetColumns) {
        SourceEnd = NewEnd;
        ExpandedRegion = true;
      }
    }

    if (!ExpandedRegion)
      break;
  }

  CaretStart = map.byteToColumn(SourceStart);
  CaretEnd = map.byteToColumn(SourceEnd) + CaretColumnsOutsideSource;

  // [CaretStart, CaretEnd) is the slice we want. Update the various
  // output lines to show only this slice.
  assert(CaretStart!=(unsigned)-1 && CaretEnd!=(unsigned)-1 &&
         SourceStart!=(unsigned)-1 && SourceEnd!=(unsigned)-1);
  assert(SourceStart <= SourceEnd);
  assert(CaretStart <= CaretEnd);

  unsigned BackColumnsRemoved
    = map.byteToColumn(SourceLine.size())-map.byteToColumn(SourceEnd);
  unsigned FrontColumnsRemoved = CaretStart;
  unsigned ColumnsKept = CaretEnd-CaretStart;

  // We checked up front that the line needed truncation
  assert(FrontColumnsRemoved+ColumnsKept+BackColumnsRemoved > Columns);

  // The line needs some truncation, and we'd prefer to keep the front
  //  if possible, so remove the back
  if (BackColumnsRemoved > strlen(back_ellipse))
    SourceLine.replace(SourceEnd, std::string::npos, back_ellipse);

  // If that's enough then we're done
  if (FrontColumnsRemoved+ColumnsKept <= Columns)
    return;

  // Otherwise remove the front as well
  if (FrontColumnsRemoved > strlen(front_ellipse)) {
    SourceLine.replace(0, SourceStart, front_ellipse);
    CaretLine.replace(0, CaretStart, front_space);
    if (!FixItInsertionLine.empty())
      FixItInsertionLine.replace(0, CaretStart, front_space);
  }
}

/// Skip over whitespace in the string, starting at the given
/// index.
///
/// \returns The index of the first non-whitespace character that is
/// greater than or equal to Idx or, if no such character exists,
/// returns the end of the string.
static unsigned skipWhitespace(unsigned Idx, StringRef Str, unsigned Length) {
  while (Idx < Length && isWhitespace(Str[Idx]))
    ++Idx;
  return Idx;
}

/// If the given character is the start of some kind of
/// balanced punctuation (e.g., quotes or parentheses), return the
/// character that will terminate the punctuation.
///
/// \returns The ending punctuation character, if any, or the NULL
/// character if the input character does not start any punctuation.
static inline char findMatchingPunctuation(char c) {
  switch (c) {
  case '\'': return '\'';
  case '`': return '\'';
  case '"':  return '"';
  case '(':  return ')';
  case '[': return ']';
  case '{': return '}';
  default: break;
  }

  return 0;
}

/// Find the end of the word starting at the given offset
/// within a string.
///
/// \returns the index pointing one character past the end of the
/// word.
static unsigned findEndOfWord(unsigned Start, StringRef Str,
                              unsigned Length, unsigned Column,
                              unsigned Columns) {
  assert(Start < Str.size() && "Invalid start position!");
  unsigned End = Start + 1;

  // If we are already at the end of the string, take that as the word.
  if (End == Str.size())
    return End;

  // Determine if the start of the string is actually opening
  // punctuation, e.g., a quote or parentheses.
  char EndPunct = findMatchingPunctuation(Str[Start]);
  if (!EndPunct) {
    // This is a normal word. Just find the first space character.
    while (End < Length && !isWhitespace(Str[End]))
      ++End;
    return End;
  }

  // We have the start of a balanced punctuation sequence (quotes,
  // parentheses, etc.). Determine the full sequence is.
  SmallString<16> PunctuationEndStack;
  PunctuationEndStack.push_back(EndPunct);
  while (End < Length && !PunctuationEndStack.empty()) {
    if (Str[End] == PunctuationEndStack.back())
      PunctuationEndStack.pop_back();
    else if (char SubEndPunct = findMatchingPunctuation(Str[End]))
      PunctuationEndStack.push_back(SubEndPunct);

    ++End;
  }

  // Find the first space character after the punctuation ended.
  while (End < Length && !isWhitespace(Str[End]))
    ++End;

  unsigned PunctWordLength = End - Start;
  if (// If the word fits on this line
      Column + PunctWordLength <= Columns ||
      // ... or the word is "short enough" to take up the next line
      // without too much ugly white space
      PunctWordLength < Columns/3)
    return End; // Take the whole thing as a single "word".

  // The whole quoted/parenthesized string is too long to print as a
  // single "word". Instead, find the "word" that starts just after
  // the punctuation and use that end-point instead. This will recurse
  // until it finds something small enough to consider a word.
  return findEndOfWord(Start + 1, Str, Length, Column + 1, Columns);
}

/// Print the given string to a stream, word-wrapping it to
/// some number of columns in the process.
///
/// \param OS the stream to which the word-wrapping string will be
/// emitted.
/// \param Str the string to word-wrap and output.
/// \param Columns the number of columns to word-wrap to.
/// \param Column the column number at which the first character of \p
/// Str will be printed. This will be non-zero when part of the first
/// line has already been printed.
/// \param Bold if the current text should be bold
/// \returns true if word-wrapping was required, or false if the
/// string fit on the first line.
static bool printWordWrapped(raw_ostream &OS, StringRef Str, unsigned Columns,
                             unsigned Column, bool Bold) {
  const unsigned Length = std::min(Str.find('\n'), Str.size());
  bool TextNormal = true;

  bool Wrapped = false;
  for (unsigned WordStart = 0, WordEnd; WordStart < Length;
       WordStart = WordEnd) {
    // Find the beginning of the next word.
    WordStart = skipWhitespace(WordStart, Str, Length);
    if (WordStart == Length)
      break;

    // Find the end of this word.
    WordEnd = findEndOfWord(WordStart, Str, Length, Column, Columns);

    // Does this word fit on the current line?
    unsigned WordLength = WordEnd - WordStart;
    if (Column + WordLength < Columns) {
      // This word fits on the current line; print it there.
      if (WordStart) {
        OS << ' ';
        Column += 1;
      }
      applyTemplateHighlighting(OS, Str.substr(WordStart, WordLength),
                                TextNormal, Bold);
      Column += WordLength;
      continue;
    }

    // This word does not fit on the current line, so wrap to the next
    // line.
    OS << '\n';
    OS.indent(WordWrapIndentation);
    applyTemplateHighlighting(OS, Str.substr(WordStart, WordLength),
                              TextNormal, Bold);
    Column = WordWrapIndentation + WordLength;
    Wrapped = true;
  }

  // Append any remaning text from the message with its existing formatting.
  applyTemplateHighlighting(OS, Str.substr(Length), TextNormal, Bold);

  assert(TextNormal && "Text highlighted at end of diagnostic message.");

  return Wrapped;
}

TextDiagnostic::TextDiagnostic(raw_ostream &OS, const LangOptions &LangOpts,
                               DiagnosticOptions *DiagOpts,
                               const Preprocessor *PP)
    : DiagnosticRenderer(LangOpts, DiagOpts), OS(OS), PP(PP) {}

TextDiagnostic::~TextDiagnostic() {}

void TextDiagnostic::emitDiagnosticMessage(
    FullSourceLoc Loc, PresumedLoc PLoc, DiagnosticsEngine::Level Level,
    StringRef Message, ArrayRef<clang::CharSourceRange> Ranges,
    DiagOrStoredDiag D) {
  uint64_t StartOfLocationInfo = OS.tell();

  // Emit the location of this particular diagnostic.
  if (Loc.isValid())
    emitDiagnosticLoc(Loc, PLoc, Level, Ranges);

  if (DiagOpts->ShowColors)
    OS.resetColor();

  if (DiagOpts->ShowLevel)
    printDiagnosticLevel(OS, Level, DiagOpts->ShowColors);
  printDiagnosticMessage(OS,
                         /*IsSupplemental*/ Level == DiagnosticsEngine::Note,
                         Message, OS.tell() - StartOfLocationInfo,
                         DiagOpts->MessageLength, DiagOpts->ShowColors);
}

/*static*/ void
TextDiagnostic::printDiagnosticLevel(raw_ostream &OS,
                                     DiagnosticsEngine::Level Level,
                                     bool ShowColors) {
  if (ShowColors) {
    // Print diagnostic category in bold and color
    switch (Level) {
    case DiagnosticsEngine::Ignored:
      llvm_unreachable("Invalid diagnostic type");
    case DiagnosticsEngine::Note:    OS.changeColor(noteColor, true); break;
    case DiagnosticsEngine::Remark:  OS.changeColor(remarkColor, true); break;
    case DiagnosticsEngine::Warning: OS.changeColor(warningColor, true); break;
    case DiagnosticsEngine::Error:   OS.changeColor(errorColor, true); break;
    case DiagnosticsEngine::Fatal:   OS.changeColor(fatalColor, true); break;
    }
  }

  switch (Level) {
  case DiagnosticsEngine::Ignored:
    llvm_unreachable("Invalid diagnostic type");
  case DiagnosticsEngine::Note:    OS << "note: "; break;
  case DiagnosticsEngine::Remark:  OS << "remark: "; break;
  case DiagnosticsEngine::Warning: OS << "warning: "; break;
  case DiagnosticsEngine::Error:   OS << "error: "; break;
  case DiagnosticsEngine::Fatal:   OS << "fatal error: "; break;
  }

  if (ShowColors)
    OS.resetColor();
}

/*static*/
void TextDiagnostic::printDiagnosticMessage(raw_ostream &OS,
                                            bool IsSupplemental,
                                            StringRef Message,
                                            unsigned CurrentColumn,
                                            unsigned Columns, bool ShowColors) {
  bool Bold = false;
  if (ShowColors && !IsSupplemental) {
    // Print primary diagnostic messages in bold and without color, to visually
    // indicate the transition from continuation notes and other output.
    OS.changeColor(savedColor, true);
    Bold = true;
  }

  if (Columns)
    printWordWrapped(OS, Message, Columns, CurrentColumn, Bold);
  else {
    bool Normal = true;
    applyTemplateHighlighting(OS, Message, Normal, Bold);
    assert(Normal && "Formatting should have returned to normal");
  }

  if (ShowColors)
    OS.resetColor();
  OS << '\n';
}

void TextDiagnostic::emitFilename(StringRef Filename, const SourceManager &SM) {
#ifdef _WIN32
  SmallString<4096> TmpFilename;
#endif
  if (DiagOpts->AbsolutePath) {
    auto File = SM.getFileManager().getOptionalFileRef(Filename);
    if (File) {
      // We want to print a simplified absolute path, i. e. without "dots".
      //
      // The hardest part here are the paths like "<part1>/<link>/../<part2>".
      // On Unix-like systems, we cannot just collapse "<link>/..", because
      // paths are resolved sequentially, and, thereby, the path
      // "<part1>/<part2>" may point to a different location. That is why
      // we use FileManager::getCanonicalName(), which expands all indirections
      // with llvm::sys::fs::real_path() and caches the result.
      //
      // On the other hand, it would be better to preserve as much of the
      // original path as possible, because that helps a user to recognize it.
      // real_path() expands all links, which sometimes too much. Luckily,
      // on Windows we can just use llvm::sys::path::remove_dots(), because,
      // on that system, both aforementioned paths point to the same place.
#ifdef _WIN32
      TmpFilename = File->getName();
      llvm::sys::fs::make_absolute(TmpFilename);
      llvm::sys::path::native(TmpFilename);
      llvm::sys::path::remove_dots(TmpFilename, /* remove_dot_dot */ true);
      Filename = StringRef(TmpFilename.data(), TmpFilename.size());
#else
      Filename = SM.getFileManager().getCanonicalName(*File);
#endif
    }
  }

  OS << Filename;
}

/// Print out the file/line/column information and include trace.
///
/// This method handles the emission of the diagnostic location information.
/// This includes extracting as much location information as is present for
/// the diagnostic and printing it, as well as any include stack or source
/// ranges necessary.
void TextDiagnostic::emitDiagnosticLoc(FullSourceLoc Loc, PresumedLoc PLoc,
                                       DiagnosticsEngine::Level Level,
                                       ArrayRef<CharSourceRange> Ranges) {
  if (PLoc.isInvalid()) {
    // At least print the file name if available:
    if (FileID FID = Loc.getFileID(); FID.isValid()) {
      if (OptionalFileEntryRef FE = Loc.getFileEntryRef()) {
        emitFilename(FE->getName(), Loc.getManager());
        OS << ": ";
      }
    }
    return;
  }
  unsigned LineNo = PLoc.getLine();

  if (!DiagOpts->ShowLocation)
    return;

  if (DiagOpts->ShowColors)
    OS.changeColor(savedColor, true);

  emitFilename(PLoc.getFilename(), Loc.getManager());
  switch (DiagOpts->getFormat()) {
  case DiagnosticOptions::SARIF:
  case DiagnosticOptions::Clang:
    if (DiagOpts->ShowLine)
      OS << ':' << LineNo;
    break;
  case DiagnosticOptions::MSVC:  OS << '('  << LineNo; break;
  case DiagnosticOptions::Vi:    OS << " +" << LineNo; break;
  }

  if (DiagOpts->ShowColumn)
    // Compute the column number.
    if (unsigned ColNo = PLoc.getColumn()) {
      if (DiagOpts->getFormat() == DiagnosticOptions::MSVC) {
        OS << ',';
        // Visual Studio 2010 or earlier expects column number to be off by one
        if (LangOpts.MSCompatibilityVersion &&
            !LangOpts.isCompatibleWithMSVC(LangOptions::MSVC2012))
          ColNo--;
      } else
        OS << ':';
      OS << ColNo;
    }
  switch (DiagOpts->getFormat()) {
  case DiagnosticOptions::SARIF:
  case DiagnosticOptions::Clang:
  case DiagnosticOptions::Vi:    OS << ':';    break;
  case DiagnosticOptions::MSVC:
    // MSVC2013 and before print 'file(4) : error'. MSVC2015 gets rid of the
    // space and prints 'file(4): error'.
    OS << ')';
    if (LangOpts.MSCompatibilityVersion &&
        !LangOpts.isCompatibleWithMSVC(LangOptions::MSVC2015))
      OS << ' ';
    OS << ':';
    break;
  }

  if (DiagOpts->ShowSourceRanges && !Ranges.empty()) {
    FileID CaretFileID = Loc.getExpansionLoc().getFileID();
    bool PrintedRange = false;
    const SourceManager &SM = Loc.getManager();

    for (const auto &R : Ranges) {
      // Ignore invalid ranges.
      if (!R.isValid())
        continue;

      SourceLocation B = SM.getExpansionLoc(R.getBegin());
      CharSourceRange ERange = SM.getExpansionRange(R.getEnd());
      SourceLocation E = ERange.getEnd();

      // If the start or end of the range is in another file, just
      // discard it.
      if (SM.getFileID(B) != CaretFileID || SM.getFileID(E) != CaretFileID)
        continue;

      // Add in the length of the token, so that we cover multi-char
      // tokens.
      unsigned TokSize = 0;
      if (ERange.isTokenRange())
        TokSize = Lexer::MeasureTokenLength(E, SM, LangOpts);

      FullSourceLoc BF(B, SM), EF(E, SM);
      OS << '{'
         << BF.getLineNumber() << ':' << BF.getColumnNumber() << '-'
         << EF.getLineNumber() << ':' << (EF.getColumnNumber() + TokSize)
         << '}';
      PrintedRange = true;
    }

    if (PrintedRange)
      OS << ':';
  }
  OS << ' ';
}

void TextDiagnostic::emitIncludeLocation(FullSourceLoc Loc, PresumedLoc PLoc) {
  if (DiagOpts->ShowLocation && PLoc.isValid()) {
    OS << "In file included from ";
    emitFilename(PLoc.getFilename(), Loc.getManager());
    OS << ':' << PLoc.getLine() << ":\n";
  } else
    OS << "In included file:\n";
}

void TextDiagnostic::emitImportLocation(FullSourceLoc Loc, PresumedLoc PLoc,
                                        StringRef ModuleName) {
  if (DiagOpts->ShowLocation && PLoc.isValid())
    OS << "In module '" << ModuleName << "' imported from "
       << PLoc.getFilename() << ':' << PLoc.getLine() << ":\n";
  else
    OS << "In module '" << ModuleName << "':\n";
}

void TextDiagnostic::emitBuildingModuleLocation(FullSourceLoc Loc,
                                                PresumedLoc PLoc,
                                                StringRef ModuleName) {
  if (DiagOpts->ShowLocation && PLoc.isValid())
    OS << "While building module '" << ModuleName << "' imported from "
      << PLoc.getFilename() << ':' << PLoc.getLine() << ":\n";
  else
    OS << "While building module '" << ModuleName << "':\n";
}

/// Find the suitable set of lines to show to include a set of ranges.
static std::optional<std::pair<unsigned, unsigned>>
findLinesForRange(const CharSourceRange &R, FileID FID,
                  const SourceManager &SM) {
  if (!R.isValid())
    return std::nullopt;

  SourceLocation Begin = R.getBegin();
  SourceLocation End = R.getEnd();
  if (SM.getFileID(Begin) != FID || SM.getFileID(End) != FID)
    return std::nullopt;

  return std::make_pair(SM.getExpansionLineNumber(Begin),
                        SM.getExpansionLineNumber(End));
}

/// Add as much of range B into range A as possible without exceeding a maximum
/// size of MaxRange. Ranges are inclusive.
static std::pair<unsigned, unsigned>
maybeAddRange(std::pair<unsigned, unsigned> A, std::pair<unsigned, unsigned> B,
              unsigned MaxRange) {
  // If A is already the maximum size, we're done.
  unsigned Slack = MaxRange - (A.second - A.first + 1);
  if (Slack == 0)
    return A;

  // Easy case: merge succeeds within MaxRange.
  unsigned Min = std::min(A.first, B.first);
  unsigned Max = std::max(A.second, B.second);
  if (Max - Min + 1 <= MaxRange)
    return {Min, Max};

  // If we can't reach B from A within MaxRange, there's nothing to do.
  // Don't add lines to the range that contain nothing interesting.
  if ((B.first > A.first && B.first - A.first + 1 > MaxRange) ||
      (B.second < A.second && A.second - B.second + 1 > MaxRange))
    return A;

  // Otherwise, expand A towards B to produce a range of size MaxRange. We
  // attempt to expand by the same amount in both directions if B strictly
  // contains A.

  // Expand downwards by up to half the available amount, then upwards as
  // much as possible, then downwards as much as possible.
  A.second = std::min(A.second + (Slack + 1) / 2, Max);
  Slack = MaxRange - (A.second - A.first + 1);
  A.first = std::max(Min + Slack, A.first) - Slack;
  A.second = std::min(A.first + MaxRange - 1, Max);
  return A;
}

struct LineRange {
  unsigned LineNo;
  unsigned StartCol;
  unsigned EndCol;
};

/// Highlight \p R (with ~'s) on the current source line.
static void highlightRange(const LineRange &R, const SourceColumnMap &Map,
                           std::string &CaretLine) {
  // Pick the first non-whitespace column.
  unsigned StartColNo = R.StartCol;
  while (StartColNo < Map.getSourceLine().size() &&
         (Map.getSourceLine()[StartColNo] == ' ' ||
          Map.getSourceLine()[StartColNo] == '\t'))
    StartColNo = Map.startOfNextColumn(StartColNo);

  // Pick the last non-whitespace column.
  unsigned EndColNo =
      std::min(static_cast<size_t>(R.EndCol), Map.getSourceLine().size());
  while (EndColNo && (Map.getSourceLine()[EndColNo - 1] == ' ' ||
                      Map.getSourceLine()[EndColNo - 1] == '\t'))
    EndColNo = Map.startOfPreviousColumn(EndColNo);

  // If the start/end passed each other, then we are trying to highlight a
  // range that just exists in whitespace. That most likely means we have
  // a multi-line highlighting range that covers a blank line.
  if (StartColNo > EndColNo)
    return;

  // Fill the range with ~'s.
  StartColNo = Map.byteToContainingColumn(StartColNo);
  EndColNo = Map.byteToContainingColumn(EndColNo);

  assert(StartColNo <= EndColNo && "Invalid range!");
  if (CaretLine.size() < EndColNo)
    CaretLine.resize(EndColNo, ' ');
  std::fill(CaretLine.begin() + StartColNo, CaretLine.begin() + EndColNo, '~');
}

static std::string buildFixItInsertionLine(FileID FID,
                                           unsigned LineNo,
                                           const SourceColumnMap &map,
                                           ArrayRef<FixItHint> Hints,
                                           const SourceManager &SM,
                                           const DiagnosticOptions *DiagOpts) {
  std::string FixItInsertionLine;
  if (Hints.empty() || !DiagOpts->ShowFixits)
    return FixItInsertionLine;
  unsigned PrevHintEndCol = 0;

  for (const auto &H : Hints) {
    if (H.CodeToInsert.empty())
      continue;

    // We have an insertion hint. Determine whether the inserted
    // code contains no newlines and is on the same line as the caret.
    std::pair<FileID, unsigned> HintLocInfo =
        SM.getDecomposedExpansionLoc(H.RemoveRange.getBegin());
    if (FID == HintLocInfo.first &&
        LineNo == SM.getLineNumber(HintLocInfo.first, HintLocInfo.second) &&
        StringRef(H.CodeToInsert).find_first_of("\n\r") == StringRef::npos) {
      // Insert the new code into the line just below the code
      // that the user wrote.
      // Note: When modifying this function, be very careful about what is a
      // "column" (printed width, platform-dependent) and what is a
      // "byte offset" (SourceManager "column").
      unsigned HintByteOffset =
          SM.getColumnNumber(HintLocInfo.first, HintLocInfo.second) - 1;

      // The hint must start inside the source or right at the end
      assert(HintByteOffset < static_cast<unsigned>(map.bytes()) + 1);
      unsigned HintCol = map.byteToContainingColumn(HintByteOffset);

      // If we inserted a long previous hint, push this one forwards, and add
      // an extra space to show that this is not part of the previous
      // completion. This is sort of the best we can do when two hints appear
      // to overlap.
      //
      // Note that if this hint is located immediately after the previous
      // hint, no space will be added, since the location is more important.
      if (HintCol < PrevHintEndCol)
        HintCol = PrevHintEndCol + 1;

      // This should NOT use HintByteOffset, because the source might have
      // Unicode characters in earlier columns.
      unsigned NewFixItLineSize = FixItInsertionLine.size() +
                                  (HintCol - PrevHintEndCol) +
                                  H.CodeToInsert.size();
      if (NewFixItLineSize > FixItInsertionLine.size())
        FixItInsertionLine.resize(NewFixItLineSize, ' ');

      std::copy(H.CodeToInsert.begin(), H.CodeToInsert.end(),
                FixItInsertionLine.end() - H.CodeToInsert.size());

      PrevHintEndCol = HintCol + llvm::sys::locale::columnWidth(H.CodeToInsert);
    }
  }

  expandTabs(FixItInsertionLine, DiagOpts->TabStop);

  return FixItInsertionLine;
}

static unsigned getNumDisplayWidth(unsigned N) {
  unsigned L = 1u, M = 10u;
  while (M <= N && ++L != std::numeric_limits<unsigned>::digits10 + 1)
    M *= 10u;

  return L;
}

/// Filter out invalid ranges, ranges that don't fit into the window of
/// source lines we will print, and ranges from other files.
///
/// For the remaining ranges, convert them to simple LineRange structs,
/// which only cover one line at a time.
static SmallVector<LineRange>
prepareAndFilterRanges(const SmallVectorImpl<CharSourceRange> &Ranges,
                       const SourceManager &SM,
                       const std::pair<unsigned, unsigned> &Lines, FileID FID,
                       const LangOptions &LangOpts) {
  SmallVector<LineRange> LineRanges;

  for (const CharSourceRange &R : Ranges) {
    if (R.isInvalid())
      continue;
    SourceLocation Begin = R.getBegin();
    SourceLocation End = R.getEnd();

    unsigned StartLineNo = SM.getExpansionLineNumber(Begin);
    if (StartLineNo > Lines.second || SM.getFileID(Begin) != FID)
      continue;

    unsigned EndLineNo = SM.getExpansionLineNumber(End);
    if (EndLineNo < Lines.first || SM.getFileID(End) != FID)
      continue;

    unsigned StartColumn = SM.getExpansionColumnNumber(Begin);
    unsigned EndColumn = SM.getExpansionColumnNumber(End);
    if (R.isTokenRange())
      EndColumn += Lexer::MeasureTokenLength(End, SM, LangOpts);

    // Only a single line.
    if (StartLineNo == EndLineNo) {
      LineRanges.push_back({StartLineNo, StartColumn - 1, EndColumn - 1});
      continue;
    }

    // Start line.
    LineRanges.push_back({StartLineNo, StartColumn - 1, ~0u});

    // Middle lines.
    for (unsigned S = StartLineNo + 1; S != EndLineNo; ++S)
      LineRanges.push_back({S, 0, ~0u});

    // End line.
    LineRanges.push_back({EndLineNo, 0, EndColumn - 1});
  }

  return LineRanges;
}

/// Creates syntax highlighting information in form of StyleRanges.
///
/// The returned unique ptr has always exactly size
/// (\p EndLineNumber - \p StartLineNumber + 1). Each SmallVector in there
/// corresponds to syntax highlighting information in one line. In each line,
/// the StyleRanges are non-overlapping and sorted from start to end of the
/// line.
static std::unique_ptr<llvm::SmallVector<TextDiagnostic::StyleRange>[]>
highlightLines(StringRef FileData, unsigned StartLineNumber,
               unsigned EndLineNumber, const Preprocessor *PP,
               const LangOptions &LangOpts, bool ShowColors, FileID FID,
               const SourceManager &SM) {
  assert(StartLineNumber <= EndLineNumber);
  auto SnippetRanges =
      std::make_unique<SmallVector<TextDiagnostic::StyleRange>[]>(
          EndLineNumber - StartLineNumber + 1);

  if (!PP || !ShowColors)
    return SnippetRanges;

  // Might cause emission of another diagnostic.
  if (PP->getIdentifierTable().getExternalIdentifierLookup())
    return SnippetRanges;

  auto Buff = llvm::MemoryBuffer::getMemBuffer(FileData);
  Lexer L{FID, *Buff, SM, LangOpts};
  L.SetKeepWhitespaceMode(true);

  const char *FirstLineStart =
      FileData.data() +
      SM.getDecomposedLoc(SM.translateLineCol(FID, StartLineNumber, 1)).second;
  if (const char *CheckPoint = PP->getCheckPoint(FID, FirstLineStart)) {
    assert(CheckPoint >= Buff->getBufferStart() &&
           CheckPoint <= Buff->getBufferEnd());
    assert(CheckPoint <= FirstLineStart);
    size_t Offset = CheckPoint - Buff->getBufferStart();
    L.seek(Offset, /*IsAtStartOfLine=*/false);
  }

  // Classify the given token and append it to the given vector.
  auto appendStyle =
      [PP, &LangOpts](SmallVector<TextDiagnostic::StyleRange> &Vec,
                      const Token &T, unsigned Start, unsigned Length) -> void {
    if (T.is(tok::raw_identifier)) {
      StringRef RawIdent = T.getRawIdentifier();
      // Special case true/false/nullptr/... literals, since they will otherwise
      // be treated as keywords.
      // FIXME: It would be good to have a programmatic way of getting this
      // list.
      if (llvm::StringSwitch<bool>(RawIdent)
              .Case("true", true)
              .Case("false", true)
              .Case("nullptr", true)
              .Case("__func__", true)
              .Case("__objc_yes__", true)
              .Case("__objc_no__", true)
              .Case("__null", true)
              .Case("__FUNCDNAME__", true)
              .Case("__FUNCSIG__", true)
              .Case("__FUNCTION__", true)
              .Case("__FUNCSIG__", true)
              .Default(false)) {
        Vec.emplace_back(Start, Start + Length, LiteralColor);
      } else {
        const IdentifierInfo *II = PP->getIdentifierInfo(RawIdent);
        assert(II);
        if (II->isKeyword(LangOpts))
          Vec.emplace_back(Start, Start + Length, KeywordColor);
      }
    } else if (tok::isLiteral(T.getKind())) {
      Vec.emplace_back(Start, Start + Length, LiteralColor);
    } else {
      assert(T.is(tok::comment));
      Vec.emplace_back(Start, Start + Length, CommentColor);
    }
  };

  bool Stop = false;
  while (!Stop) {
    Token T;
    Stop = L.LexFromRawLexer(T);
    if (T.is(tok::unknown))
      continue;

    // We are only interested in identifiers, literals and comments.
    if (!T.is(tok::raw_identifier) && !T.is(tok::comment) &&
        !tok::isLiteral(T.getKind()))
      continue;

    bool Invalid = false;
    unsigned TokenEndLine = SM.getSpellingLineNumber(T.getEndLoc(), &Invalid);
    if (Invalid || TokenEndLine < StartLineNumber)
      continue;

    assert(TokenEndLine >= StartLineNumber);

    unsigned TokenStartLine =
        SM.getSpellingLineNumber(T.getLocation(), &Invalid);
    if (Invalid)
      continue;
    // If this happens, we're done.
    if (TokenStartLine > EndLineNumber)
      break;

    unsigned StartCol =
        SM.getSpellingColumnNumber(T.getLocation(), &Invalid) - 1;
    if (Invalid)
      continue;

    // Simple tokens.
    if (TokenStartLine == TokenEndLine) {
      SmallVector<TextDiagnostic::StyleRange> &LineRanges =
          SnippetRanges[TokenStartLine - StartLineNumber];
      appendStyle(LineRanges, T, StartCol, T.getLength());
      continue;
    }
    assert((TokenEndLine - TokenStartLine) >= 1);

    // For tokens that span multiple lines (think multiline comments), we
    // divide them into multiple StyleRanges.
    unsigned EndCol = SM.getSpellingColumnNumber(T.getEndLoc(), &Invalid) - 1;
    if (Invalid)
      continue;

    std::string Spelling = Lexer::getSpelling(T, SM, LangOpts);

    unsigned L = TokenStartLine;
    unsigned LineLength = 0;
    for (unsigned I = 0; I <= Spelling.size(); ++I) {
      // This line is done.
      if (I == Spelling.size() || isVerticalWhitespace(Spelling[I])) {
        SmallVector<TextDiagnostic::StyleRange> &LineRanges =
            SnippetRanges[L - StartLineNumber];

        if (L >= StartLineNumber) {
          if (L == TokenStartLine) // First line
            appendStyle(LineRanges, T, StartCol, LineLength);
          else if (L == TokenEndLine) // Last line
            appendStyle(LineRanges, T, 0, EndCol);
          else
            appendStyle(LineRanges, T, 0, LineLength);
        }

        ++L;
        if (L > EndLineNumber)
          break;
        LineLength = 0;
        continue;
      }
      ++LineLength;
    }
  }

  return SnippetRanges;
}

/// Emit a code snippet and caret line.
///
/// This routine emits a single line's code snippet and caret line..
///
/// \param Loc The location for the caret.
/// \param Ranges The underlined ranges for this code snippet.
/// \param Hints The FixIt hints active for this diagnostic.
void TextDiagnostic::emitSnippetAndCaret(
    FullSourceLoc Loc, DiagnosticsEngine::Level Level,
    SmallVectorImpl<CharSourceRange> &Ranges, ArrayRef<FixItHint> Hints) {
  assert(Loc.isValid() && "must have a valid source location here");
  assert(Loc.isFileID() && "must have a file location here");

  // If caret diagnostics are enabled and we have location, we want to
  // emit the caret.  However, we only do this if the location moved
  // from the last diagnostic, if the last diagnostic was a note that
  // was part of a different warning or error diagnostic, or if the
  // diagnostic has ranges.  We don't want to emit the same caret
  // multiple times if one loc has multiple diagnostics.
  if (!DiagOpts->ShowCarets)
    return;
  if (Loc == LastLoc && Ranges.empty() && Hints.empty() &&
      (LastLevel != DiagnosticsEngine::Note || Level == LastLevel))
    return;

  FileID FID = Loc.getFileID();
  const SourceManager &SM = Loc.getManager();

  // Get information about the buffer it points into.
  bool Invalid = false;
  StringRef BufData = Loc.getBufferData(&Invalid);
  if (Invalid)
    return;
  const char *BufStart = BufData.data();
  const char *BufEnd = BufStart + BufData.size();

  unsigned CaretLineNo = Loc.getLineNumber();
  unsigned CaretColNo = Loc.getColumnNumber();

  // Arbitrarily stop showing snippets when the line is too long.
  static const size_t MaxLineLengthToPrint = 4096;
  if (CaretColNo > MaxLineLengthToPrint)
    return;

  // Find the set of lines to include.
  const unsigned MaxLines = DiagOpts->SnippetLineLimit;
  std::pair<unsigned, unsigned> Lines = {CaretLineNo, CaretLineNo};
  unsigned DisplayLineNo = Loc.getPresumedLoc().getLine();
  for (const auto &I : Ranges) {
    if (auto OptionalRange = findLinesForRange(I, FID, SM))
      Lines = maybeAddRange(Lines, *OptionalRange, MaxLines);

    DisplayLineNo =
        std::min(DisplayLineNo, SM.getPresumedLineNumber(I.getBegin()));
  }

  // Our line numbers look like:
  // " [number] | "
  // Where [number] is MaxLineNoDisplayWidth columns
  // and the full thing is therefore MaxLineNoDisplayWidth + 4 columns.
  unsigned MaxLineNoDisplayWidth =
      DiagOpts->ShowLineNumbers
          ? std::max(4u, getNumDisplayWidth(DisplayLineNo + MaxLines))
          : 0;
  auto indentForLineNumbers = [&] {
    if (MaxLineNoDisplayWidth > 0)
      OS.indent(MaxLineNoDisplayWidth + 2) << "| ";
  };

  // Prepare source highlighting information for the lines we're about to
  // emit, starting from the first line.
  std::unique_ptr<SmallVector<StyleRange>[]> SourceStyles =
      highlightLines(BufData, Lines.first, Lines.second, PP, LangOpts,
                     DiagOpts->ShowColors, FID, SM);

  SmallVector<LineRange> LineRanges =
      prepareAndFilterRanges(Ranges, SM, Lines, FID, LangOpts);

  for (unsigned LineNo = Lines.first; LineNo != Lines.second + 1;
       ++LineNo, ++DisplayLineNo) {
    // Rewind from the current position to the start of the line.
    const char *LineStart =
        BufStart +
        SM.getDecomposedLoc(SM.translateLineCol(FID, LineNo, 1)).second;
    if (LineStart == BufEnd)
      break;

    // Compute the line end.
    const char *LineEnd = LineStart;
    while (*LineEnd != '\n' && *LineEnd != '\r' && LineEnd != BufEnd)
      ++LineEnd;

    // Arbitrarily stop showing snippets when the line is too long.
    // FIXME: Don't print any lines in this case.
    if (size_t(LineEnd - LineStart) > MaxLineLengthToPrint)
      return;

    // Copy the line of code into an std::string for ease of manipulation.
    std::string SourceLine(LineStart, LineEnd);
    // Remove trailing null bytes.
    while (!SourceLine.empty() && SourceLine.back() == '\0' &&
           (LineNo != CaretLineNo || SourceLine.size() > CaretColNo))
      SourceLine.pop_back();

    // Build the byte to column map.
    const SourceColumnMap sourceColMap(SourceLine, DiagOpts->TabStop);

    std::string CaretLine;
    // Highlight all of the characters covered by Ranges with ~ characters.
    for (const auto &LR : LineRanges) {
      if (LR.LineNo == LineNo)
        highlightRange(LR, sourceColMap, CaretLine);
    }

    // Next, insert the caret itself.
    if (CaretLineNo == LineNo) {
      size_t Col = sourceColMap.byteToContainingColumn(CaretColNo - 1);
      CaretLine.resize(std::max(Col + 1, CaretLine.size()), ' ');
      CaretLine[Col] = '^';
    }

    std::string FixItInsertionLine = buildFixItInsertionLine(
        FID, LineNo, sourceColMap, Hints, SM, DiagOpts.get());

    // If the source line is too long for our terminal, select only the
    // "interesting" source region within that line.
    unsigned Columns = DiagOpts->MessageLength;
    if (Columns)
      selectInterestingSourceRegion(SourceLine, CaretLine, FixItInsertionLine,
                                    Columns, sourceColMap);

    // If we are in -fdiagnostics-print-source-range-info mode, we are trying
    // to produce easily machine parsable output.  Add a space before the
    // source line and the caret to make it trivial to tell the main diagnostic
    // line from what the user is intended to see.
    if (DiagOpts->ShowSourceRanges && !SourceLine.empty()) {
      SourceLine = ' ' + SourceLine;
      CaretLine = ' ' + CaretLine;
    }

    // Emit what we have computed.
    emitSnippet(SourceLine, MaxLineNoDisplayWidth, LineNo, DisplayLineNo,
                SourceStyles[LineNo - Lines.first]);

    if (!CaretLine.empty()) {
      indentForLineNumbers();
      if (DiagOpts->ShowColors)
        OS.changeColor(caretColor, true);
      OS << CaretLine << '\n';
      if (DiagOpts->ShowColors)
        OS.resetColor();
    }

    if (!FixItInsertionLine.empty()) {
      indentForLineNumbers();
      if (DiagOpts->ShowColors)
        // Print fixit line in color
        OS.changeColor(fixitColor, false);
      if (DiagOpts->ShowSourceRanges)
        OS << ' ';
      OS << FixItInsertionLine << '\n';
      if (DiagOpts->ShowColors)
        OS.resetColor();
    }
  }

  // Print out any parseable fixit information requested by the options.
  emitParseableFixits(Hints, SM);
}

void TextDiagnostic::emitSnippet(StringRef SourceLine,
                                 unsigned MaxLineNoDisplayWidth,
                                 unsigned LineNo, unsigned DisplayLineNo,
                                 ArrayRef<StyleRange> Styles) {
  // Emit line number.
  if (MaxLineNoDisplayWidth > 0) {
    unsigned LineNoDisplayWidth = getNumDisplayWidth(DisplayLineNo);
    OS.indent(MaxLineNoDisplayWidth - LineNoDisplayWidth + 1)
        << DisplayLineNo << " | ";
  }

  // Print the source line one character at a time.
  bool PrintReversed = false;
  std::optional<llvm::raw_ostream::Colors> CurrentColor;
  size_t I = 0;
  while (I < SourceLine.size()) {
    auto [Str, WasPrintable] =
        printableTextForNextCharacter(SourceLine, &I, DiagOpts->TabStop);

    // Toggle inverted colors on or off for this character.
    if (DiagOpts->ShowColors) {
      if (WasPrintable == PrintReversed) {
        PrintReversed = !PrintReversed;
        if (PrintReversed)
          OS.reverseColor();
        else {
          OS.resetColor();
          CurrentColor = std::nullopt;
        }
      }

      // Apply syntax highlighting information.
      const auto *CharStyle = llvm::find_if(Styles, [I](const StyleRange &R) {
        return (R.Start < I && R.End >= I);
      });

      if (CharStyle != Styles.end()) {
        if (!CurrentColor ||
            (CurrentColor && *CurrentColor != CharStyle->Color)) {
          OS.changeColor(CharStyle->Color, false);
          CurrentColor = CharStyle->Color;
        }
      } else if (CurrentColor) {
        OS.resetColor();
        CurrentColor = std::nullopt;
      }
    }

    OS << Str;
  }

  if (DiagOpts->ShowColors)
    OS.resetColor();

  OS << '\n';
}

void TextDiagnostic::emitParseableFixits(ArrayRef<FixItHint> Hints,
                                         const SourceManager &SM) {
  if (!DiagOpts->ShowParseableFixits)
    return;

  // We follow FixItRewriter's example in not (yet) handling
  // fix-its in macros.
  for (const auto &H : Hints) {
    if (H.RemoveRange.isInvalid() || H.RemoveRange.getBegin().isMacroID() ||
        H.RemoveRange.getEnd().isMacroID())
      return;
  }

  for (const auto &H : Hints) {
    SourceLocation BLoc = H.RemoveRange.getBegin();
    SourceLocation ELoc = H.RemoveRange.getEnd();

    std::pair<FileID, unsigned> BInfo = SM.getDecomposedLoc(BLoc);
    std::pair<FileID, unsigned> EInfo = SM.getDecomposedLoc(ELoc);

    // Adjust for token ranges.
    if (H.RemoveRange.isTokenRange())
      EInfo.second += Lexer::MeasureTokenLength(ELoc, SM, LangOpts);

    // We specifically do not do word-wrapping or tab-expansion here,
    // because this is supposed to be easy to parse.
    PresumedLoc PLoc = SM.getPresumedLoc(BLoc);
    if (PLoc.isInvalid())
      break;

    OS << "fix-it:\"";
    OS.write_escaped(PLoc.getFilename());
    OS << "\":{" << SM.getLineNumber(BInfo.first, BInfo.second)
      << ':' << SM.getColumnNumber(BInfo.first, BInfo.second)
      << '-' << SM.getLineNumber(EInfo.first, EInfo.second)
      << ':' << SM.getColumnNumber(EInfo.first, EInfo.second)
      << "}:\"";
    OS.write_escaped(H.CodeToInsert);
    OS << "\"\n";
  }
}
