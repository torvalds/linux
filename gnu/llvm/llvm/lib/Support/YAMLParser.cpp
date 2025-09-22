//===- YAMLParser.cpp - Simple YAML parser --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements a YAML parser.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/YAMLParser.h"
#include "llvm/ADT/AllocatorList.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/Unicode.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

using namespace llvm;
using namespace yaml;

enum UnicodeEncodingForm {
  UEF_UTF32_LE, ///< UTF-32 Little Endian
  UEF_UTF32_BE, ///< UTF-32 Big Endian
  UEF_UTF16_LE, ///< UTF-16 Little Endian
  UEF_UTF16_BE, ///< UTF-16 Big Endian
  UEF_UTF8,     ///< UTF-8 or ascii.
  UEF_Unknown   ///< Not a valid Unicode encoding.
};

/// EncodingInfo - Holds the encoding type and length of the byte order mark if
///                it exists. Length is in {0, 2, 3, 4}.
using EncodingInfo = std::pair<UnicodeEncodingForm, unsigned>;

/// getUnicodeEncoding - Reads up to the first 4 bytes to determine the Unicode
///                      encoding form of \a Input.
///
/// @param Input A string of length 0 or more.
/// @returns An EncodingInfo indicating the Unicode encoding form of the input
///          and how long the byte order mark is if one exists.
static EncodingInfo getUnicodeEncoding(StringRef Input) {
  if (Input.empty())
    return std::make_pair(UEF_Unknown, 0);

  switch (uint8_t(Input[0])) {
  case 0x00:
    if (Input.size() >= 4) {
      if (  Input[1] == 0
         && uint8_t(Input[2]) == 0xFE
         && uint8_t(Input[3]) == 0xFF)
        return std::make_pair(UEF_UTF32_BE, 4);
      if (Input[1] == 0 && Input[2] == 0 && Input[3] != 0)
        return std::make_pair(UEF_UTF32_BE, 0);
    }

    if (Input.size() >= 2 && Input[1] != 0)
      return std::make_pair(UEF_UTF16_BE, 0);
    return std::make_pair(UEF_Unknown, 0);
  case 0xFF:
    if (  Input.size() >= 4
       && uint8_t(Input[1]) == 0xFE
       && Input[2] == 0
       && Input[3] == 0)
      return std::make_pair(UEF_UTF32_LE, 4);

    if (Input.size() >= 2 && uint8_t(Input[1]) == 0xFE)
      return std::make_pair(UEF_UTF16_LE, 2);
    return std::make_pair(UEF_Unknown, 0);
  case 0xFE:
    if (Input.size() >= 2 && uint8_t(Input[1]) == 0xFF)
      return std::make_pair(UEF_UTF16_BE, 2);
    return std::make_pair(UEF_Unknown, 0);
  case 0xEF:
    if (  Input.size() >= 3
       && uint8_t(Input[1]) == 0xBB
       && uint8_t(Input[2]) == 0xBF)
      return std::make_pair(UEF_UTF8, 3);
    return std::make_pair(UEF_Unknown, 0);
  }

  // It could still be utf-32 or utf-16.
  if (Input.size() >= 4 && Input[1] == 0 && Input[2] == 0 && Input[3] == 0)
    return std::make_pair(UEF_UTF32_LE, 0);

  if (Input.size() >= 2 && Input[1] == 0)
    return std::make_pair(UEF_UTF16_LE, 0);

  return std::make_pair(UEF_UTF8, 0);
}

/// Pin the vtables to this file.
void Node::anchor() {}
void NullNode::anchor() {}
void ScalarNode::anchor() {}
void BlockScalarNode::anchor() {}
void KeyValueNode::anchor() {}
void MappingNode::anchor() {}
void SequenceNode::anchor() {}
void AliasNode::anchor() {}

namespace llvm {
namespace yaml {

/// Token - A single YAML token.
struct Token {
  enum TokenKind {
    TK_Error, // Uninitialized token.
    TK_StreamStart,
    TK_StreamEnd,
    TK_VersionDirective,
    TK_TagDirective,
    TK_DocumentStart,
    TK_DocumentEnd,
    TK_BlockEntry,
    TK_BlockEnd,
    TK_BlockSequenceStart,
    TK_BlockMappingStart,
    TK_FlowEntry,
    TK_FlowSequenceStart,
    TK_FlowSequenceEnd,
    TK_FlowMappingStart,
    TK_FlowMappingEnd,
    TK_Key,
    TK_Value,
    TK_Scalar,
    TK_BlockScalar,
    TK_Alias,
    TK_Anchor,
    TK_Tag
  } Kind = TK_Error;

  /// A string of length 0 or more whose begin() points to the logical location
  /// of the token in the input.
  StringRef Range;

  /// The value of a block scalar node.
  std::string Value;

  Token() = default;
};

} // end namespace yaml
} // end namespace llvm

using TokenQueueT = BumpPtrList<Token>;

namespace {

/// This struct is used to track simple keys.
///
/// Simple keys are handled by creating an entry in SimpleKeys for each Token
/// which could legally be the start of a simple key. When peekNext is called,
/// if the Token To be returned is referenced by a SimpleKey, we continue
/// tokenizing until that potential simple key has either been found to not be
/// a simple key (we moved on to the next line or went further than 1024 chars).
/// Or when we run into a Value, and then insert a Key token (and possibly
/// others) before the SimpleKey's Tok.
struct SimpleKey {
  TokenQueueT::iterator Tok;
  unsigned Column = 0;
  unsigned Line = 0;
  unsigned FlowLevel = 0;
  bool IsRequired = false;

  bool operator ==(const SimpleKey &Other) {
    return Tok == Other.Tok;
  }
};

} // end anonymous namespace

/// The Unicode scalar value of a UTF-8 minimal well-formed code unit
///        subsequence and the subsequence's length in code units (uint8_t).
///        A length of 0 represents an error.
using UTF8Decoded = std::pair<uint32_t, unsigned>;

static UTF8Decoded decodeUTF8(StringRef Range) {
  StringRef::iterator Position= Range.begin();
  StringRef::iterator End = Range.end();
  // 1 byte: [0x00, 0x7f]
  // Bit pattern: 0xxxxxxx
  if (Position < End && (*Position & 0x80) == 0) {
    return std::make_pair(*Position, 1);
  }
  // 2 bytes: [0x80, 0x7ff]
  // Bit pattern: 110xxxxx 10xxxxxx
  if (Position + 1 < End && ((*Position & 0xE0) == 0xC0) &&
      ((*(Position + 1) & 0xC0) == 0x80)) {
    uint32_t codepoint = ((*Position & 0x1F) << 6) |
                          (*(Position + 1) & 0x3F);
    if (codepoint >= 0x80)
      return std::make_pair(codepoint, 2);
  }
  // 3 bytes: [0x8000, 0xffff]
  // Bit pattern: 1110xxxx 10xxxxxx 10xxxxxx
  if (Position + 2 < End && ((*Position & 0xF0) == 0xE0) &&
      ((*(Position + 1) & 0xC0) == 0x80) &&
      ((*(Position + 2) & 0xC0) == 0x80)) {
    uint32_t codepoint = ((*Position & 0x0F) << 12) |
                         ((*(Position + 1) & 0x3F) << 6) |
                          (*(Position + 2) & 0x3F);
    // Codepoints between 0xD800 and 0xDFFF are invalid, as
    // they are high / low surrogate halves used by UTF-16.
    if (codepoint >= 0x800 &&
        (codepoint < 0xD800 || codepoint > 0xDFFF))
      return std::make_pair(codepoint, 3);
  }
  // 4 bytes: [0x10000, 0x10FFFF]
  // Bit pattern: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
  if (Position + 3 < End && ((*Position & 0xF8) == 0xF0) &&
      ((*(Position + 1) & 0xC0) == 0x80) &&
      ((*(Position + 2) & 0xC0) == 0x80) &&
      ((*(Position + 3) & 0xC0) == 0x80)) {
    uint32_t codepoint = ((*Position & 0x07) << 18) |
                         ((*(Position + 1) & 0x3F) << 12) |
                         ((*(Position + 2) & 0x3F) << 6) |
                          (*(Position + 3) & 0x3F);
    if (codepoint >= 0x10000 && codepoint <= 0x10FFFF)
      return std::make_pair(codepoint, 4);
  }
  return std::make_pair(0, 0);
}

namespace llvm {
namespace yaml {

/// Scans YAML tokens from a MemoryBuffer.
class Scanner {
public:
  Scanner(StringRef Input, SourceMgr &SM, bool ShowColors = true,
          std::error_code *EC = nullptr);
  Scanner(MemoryBufferRef Buffer, SourceMgr &SM_, bool ShowColors = true,
          std::error_code *EC = nullptr);

  /// Parse the next token and return it without popping it.
  Token &peekNext();

  /// Parse the next token and pop it from the queue.
  Token getNext();

  void printError(SMLoc Loc, SourceMgr::DiagKind Kind, const Twine &Message,
                  ArrayRef<SMRange> Ranges = std::nullopt) {
    SM.PrintMessage(Loc, Kind, Message, Ranges, /* FixIts= */ std::nullopt,
                    ShowColors);
  }

  void setError(const Twine &Message, StringRef::iterator Position) {
    if (Position >= End)
      Position = End - 1;

    // propagate the error if possible
    if (EC)
      *EC = make_error_code(std::errc::invalid_argument);

    // Don't print out more errors after the first one we encounter. The rest
    // are just the result of the first, and have no meaning.
    if (!Failed)
      printError(SMLoc::getFromPointer(Position), SourceMgr::DK_Error, Message);
    Failed = true;
  }

  /// Returns true if an error occurred while parsing.
  bool failed() {
    return Failed;
  }

private:
  void init(MemoryBufferRef Buffer);

  StringRef currentInput() {
    return StringRef(Current, End - Current);
  }

  /// Decode a UTF-8 minimal well-formed code unit subsequence starting
  ///        at \a Position.
  ///
  /// If the UTF-8 code units starting at Position do not form a well-formed
  /// code unit subsequence, then the Unicode scalar value is 0, and the length
  /// is 0.
  UTF8Decoded decodeUTF8(StringRef::iterator Position) {
    return ::decodeUTF8(StringRef(Position, End - Position));
  }

  // The following functions are based on the gramar rules in the YAML spec. The
  // style of the function names it meant to closely match how they are written
  // in the spec. The number within the [] is the number of the grammar rule in
  // the spec.
  //
  // See 4.2 [Production Naming Conventions] for the meaning of the prefixes.
  //
  // c-
  //   A production starting and ending with a special character.
  // b-
  //   A production matching a single line break.
  // nb-
  //   A production starting and ending with a non-break character.
  // s-
  //   A production starting and ending with a white space character.
  // ns-
  //   A production starting and ending with a non-space character.
  // l-
  //   A production matching complete line(s).

  /// Skip a single nb-char[27] starting at Position.
  ///
  /// A nb-char is 0x9 | [0x20-0x7E] | 0x85 | [0xA0-0xD7FF] | [0xE000-0xFEFE]
  ///                  | [0xFF00-0xFFFD] | [0x10000-0x10FFFF]
  ///
  /// @returns The code unit after the nb-char, or Position if it's not an
  ///          nb-char.
  StringRef::iterator skip_nb_char(StringRef::iterator Position);

  /// Skip a single b-break[28] starting at Position.
  ///
  /// A b-break is 0xD 0xA | 0xD | 0xA
  ///
  /// @returns The code unit after the b-break, or Position if it's not a
  ///          b-break.
  StringRef::iterator skip_b_break(StringRef::iterator Position);

  /// Skip a single s-space[31] starting at Position.
  ///
  /// An s-space is 0x20
  ///
  /// @returns The code unit after the s-space, or Position if it's not a
  ///          s-space.
  StringRef::iterator skip_s_space(StringRef::iterator Position);

  /// Skip a single s-white[33] starting at Position.
  ///
  /// A s-white is 0x20 | 0x9
  ///
  /// @returns The code unit after the s-white, or Position if it's not a
  ///          s-white.
  StringRef::iterator skip_s_white(StringRef::iterator Position);

  /// Skip a single ns-char[34] starting at Position.
  ///
  /// A ns-char is nb-char - s-white
  ///
  /// @returns The code unit after the ns-char, or Position if it's not a
  ///          ns-char.
  StringRef::iterator skip_ns_char(StringRef::iterator Position);

  using SkipWhileFunc = StringRef::iterator (Scanner::*)(StringRef::iterator);

  /// Skip minimal well-formed code unit subsequences until Func
  ///        returns its input.
  ///
  /// @returns The code unit after the last minimal well-formed code unit
  ///          subsequence that Func accepted.
  StringRef::iterator skip_while( SkipWhileFunc Func
                                , StringRef::iterator Position);

  /// Skip minimal well-formed code unit subsequences until Func returns its
  /// input.
  void advanceWhile(SkipWhileFunc Func);

  /// Scan ns-uri-char[39]s starting at Cur.
  ///
  /// This updates Cur and Column while scanning.
  void scan_ns_uri_char();

  /// Consume a minimal well-formed code unit subsequence starting at
  ///        \a Cur. Return false if it is not the same Unicode scalar value as
  ///        \a Expected. This updates \a Column.
  bool consume(uint32_t Expected);

  /// Skip \a Distance UTF-8 code units. Updates \a Cur and \a Column.
  void skip(uint32_t Distance);

  /// Return true if the minimal well-formed code unit subsequence at
  ///        Pos is whitespace or a new line
  bool isBlankOrBreak(StringRef::iterator Position);

  /// Return true if the minimal well-formed code unit subsequence at
  ///        Pos is considered a "safe" character for plain scalars.
  bool isPlainSafeNonBlank(StringRef::iterator Position);

  /// Return true if the line is a line break, false otherwise.
  bool isLineEmpty(StringRef Line);

  /// Consume a single b-break[28] if it's present at the current position.
  ///
  /// Return false if the code unit at the current position isn't a line break.
  bool consumeLineBreakIfPresent();

  /// If IsSimpleKeyAllowed, create and push_back a new SimpleKey.
  void saveSimpleKeyCandidate( TokenQueueT::iterator Tok
                             , unsigned AtColumn
                             , bool IsRequired);

  /// Remove simple keys that can no longer be valid simple keys.
  ///
  /// Invalid simple keys are not on the current line or are further than 1024
  /// columns back.
  void removeStaleSimpleKeyCandidates();

  /// Remove all simple keys on FlowLevel \a Level.
  void removeSimpleKeyCandidatesOnFlowLevel(unsigned Level);

  /// Unroll indentation in \a Indents back to \a Col. Creates BlockEnd
  ///        tokens if needed.
  bool unrollIndent(int ToColumn);

  /// Increase indent to \a Col. Creates \a Kind token at \a InsertPoint
  ///        if needed.
  bool rollIndent( int ToColumn
                 , Token::TokenKind Kind
                 , TokenQueueT::iterator InsertPoint);

  /// Skip a single-line comment when the comment starts at the current
  /// position of the scanner.
  void skipComment();

  /// Skip whitespace and comments until the start of the next token.
  void scanToNextToken();

  /// Must be the first token generated.
  bool scanStreamStart();

  /// Generate tokens needed to close out the stream.
  bool scanStreamEnd();

  /// Scan a %BLAH directive.
  bool scanDirective();

  /// Scan a ... or ---.
  bool scanDocumentIndicator(bool IsStart);

  /// Scan a [ or { and generate the proper flow collection start token.
  bool scanFlowCollectionStart(bool IsSequence);

  /// Scan a ] or } and generate the proper flow collection end token.
  bool scanFlowCollectionEnd(bool IsSequence);

  /// Scan the , that separates entries in a flow collection.
  bool scanFlowEntry();

  /// Scan the - that starts block sequence entries.
  bool scanBlockEntry();

  /// Scan an explicit ? indicating a key.
  bool scanKey();

  /// Scan an explicit : indicating a value.
  bool scanValue();

  /// Scan a quoted scalar.
  bool scanFlowScalar(bool IsDoubleQuoted);

  /// Scan an unquoted scalar.
  bool scanPlainScalar();

  /// Scan an Alias or Anchor starting with * or &.
  bool scanAliasOrAnchor(bool IsAlias);

  /// Scan a block scalar starting with | or >.
  bool scanBlockScalar(bool IsLiteral);

  /// Scan a block scalar style indicator and header.
  ///
  /// Note: This is distinct from scanBlockScalarHeader to mirror the fact that
  /// YAML does not consider the style indicator to be a part of the header.
  ///
  /// Return false if an error occurred.
  bool scanBlockScalarIndicators(char &StyleIndicator, char &ChompingIndicator,
                                 unsigned &IndentIndicator, bool &IsDone);

  /// Scan a style indicator in a block scalar header.
  char scanBlockStyleIndicator();

  /// Scan a chomping indicator in a block scalar header.
  char scanBlockChompingIndicator();

  /// Scan an indentation indicator in a block scalar header.
  unsigned scanBlockIndentationIndicator();

  /// Scan a block scalar header.
  ///
  /// Return false if an error occurred.
  bool scanBlockScalarHeader(char &ChompingIndicator, unsigned &IndentIndicator,
                             bool &IsDone);

  /// Look for the indentation level of a block scalar.
  ///
  /// Return false if an error occurred.
  bool findBlockScalarIndent(unsigned &BlockIndent, unsigned BlockExitIndent,
                             unsigned &LineBreaks, bool &IsDone);

  /// Scan the indentation of a text line in a block scalar.
  ///
  /// Return false if an error occurred.
  bool scanBlockScalarIndent(unsigned BlockIndent, unsigned BlockExitIndent,
                             bool &IsDone);

  /// Scan a tag of the form !stuff.
  bool scanTag();

  /// Dispatch to the next scanning function based on \a *Cur.
  bool fetchMoreTokens();

  /// The SourceMgr used for diagnostics and buffer management.
  SourceMgr &SM;

  /// The original input.
  MemoryBufferRef InputBuffer;

  /// The current position of the scanner.
  StringRef::iterator Current;

  /// The end of the input (one past the last character).
  StringRef::iterator End;

  /// Current YAML indentation level in spaces.
  int Indent;

  /// Current column number in Unicode code points.
  unsigned Column;

  /// Current line number.
  unsigned Line;

  /// How deep we are in flow style containers. 0 Means at block level.
  unsigned FlowLevel;

  /// Are we at the start of the stream?
  bool IsStartOfStream;

  /// Can the next token be the start of a simple key?
  bool IsSimpleKeyAllowed;

  /// Can the next token be a value indicator even if it does not have a
  /// trailing space?
  bool IsAdjacentValueAllowedInFlow;

  /// True if an error has occurred.
  bool Failed;

  /// Should colors be used when printing out the diagnostic messages?
  bool ShowColors;

  /// Queue of tokens. This is required to queue up tokens while looking
  ///        for the end of a simple key. And for cases where a single character
  ///        can produce multiple tokens (e.g. BlockEnd).
  TokenQueueT TokenQueue;

  /// Indentation levels.
  SmallVector<int, 4> Indents;

  /// Potential simple keys.
  SmallVector<SimpleKey, 4> SimpleKeys;

  std::error_code *EC;
};

} // end namespace yaml
} // end namespace llvm

/// encodeUTF8 - Encode \a UnicodeScalarValue in UTF-8 and append it to result.
static void encodeUTF8( uint32_t UnicodeScalarValue
                      , SmallVectorImpl<char> &Result) {
  if (UnicodeScalarValue <= 0x7F) {
    Result.push_back(UnicodeScalarValue & 0x7F);
  } else if (UnicodeScalarValue <= 0x7FF) {
    uint8_t FirstByte = 0xC0 | ((UnicodeScalarValue & 0x7C0) >> 6);
    uint8_t SecondByte = 0x80 | (UnicodeScalarValue & 0x3F);
    Result.push_back(FirstByte);
    Result.push_back(SecondByte);
  } else if (UnicodeScalarValue <= 0xFFFF) {
    uint8_t FirstByte = 0xE0 | ((UnicodeScalarValue & 0xF000) >> 12);
    uint8_t SecondByte = 0x80 | ((UnicodeScalarValue & 0xFC0) >> 6);
    uint8_t ThirdByte = 0x80 | (UnicodeScalarValue & 0x3F);
    Result.push_back(FirstByte);
    Result.push_back(SecondByte);
    Result.push_back(ThirdByte);
  } else if (UnicodeScalarValue <= 0x10FFFF) {
    uint8_t FirstByte = 0xF0 | ((UnicodeScalarValue & 0x1F0000) >> 18);
    uint8_t SecondByte = 0x80 | ((UnicodeScalarValue & 0x3F000) >> 12);
    uint8_t ThirdByte = 0x80 | ((UnicodeScalarValue & 0xFC0) >> 6);
    uint8_t FourthByte = 0x80 | (UnicodeScalarValue & 0x3F);
    Result.push_back(FirstByte);
    Result.push_back(SecondByte);
    Result.push_back(ThirdByte);
    Result.push_back(FourthByte);
  }
}

bool yaml::dumpTokens(StringRef Input, raw_ostream &OS) {
  SourceMgr SM;
  Scanner scanner(Input, SM);
  while (true) {
    Token T = scanner.getNext();
    switch (T.Kind) {
    case Token::TK_StreamStart:
      OS << "Stream-Start: ";
      break;
    case Token::TK_StreamEnd:
      OS << "Stream-End: ";
      break;
    case Token::TK_VersionDirective:
      OS << "Version-Directive: ";
      break;
    case Token::TK_TagDirective:
      OS << "Tag-Directive: ";
      break;
    case Token::TK_DocumentStart:
      OS << "Document-Start: ";
      break;
    case Token::TK_DocumentEnd:
      OS << "Document-End: ";
      break;
    case Token::TK_BlockEntry:
      OS << "Block-Entry: ";
      break;
    case Token::TK_BlockEnd:
      OS << "Block-End: ";
      break;
    case Token::TK_BlockSequenceStart:
      OS << "Block-Sequence-Start: ";
      break;
    case Token::TK_BlockMappingStart:
      OS << "Block-Mapping-Start: ";
      break;
    case Token::TK_FlowEntry:
      OS << "Flow-Entry: ";
      break;
    case Token::TK_FlowSequenceStart:
      OS << "Flow-Sequence-Start: ";
      break;
    case Token::TK_FlowSequenceEnd:
      OS << "Flow-Sequence-End: ";
      break;
    case Token::TK_FlowMappingStart:
      OS << "Flow-Mapping-Start: ";
      break;
    case Token::TK_FlowMappingEnd:
      OS << "Flow-Mapping-End: ";
      break;
    case Token::TK_Key:
      OS << "Key: ";
      break;
    case Token::TK_Value:
      OS << "Value: ";
      break;
    case Token::TK_Scalar:
      OS << "Scalar: ";
      break;
    case Token::TK_BlockScalar:
      OS << "Block Scalar: ";
      break;
    case Token::TK_Alias:
      OS << "Alias: ";
      break;
    case Token::TK_Anchor:
      OS << "Anchor: ";
      break;
    case Token::TK_Tag:
      OS << "Tag: ";
      break;
    case Token::TK_Error:
      break;
    }
    OS << T.Range << "\n";
    if (T.Kind == Token::TK_StreamEnd)
      break;
    else if (T.Kind == Token::TK_Error)
      return false;
  }
  return true;
}

bool yaml::scanTokens(StringRef Input) {
  SourceMgr SM;
  Scanner scanner(Input, SM);
  while (true) {
    Token T = scanner.getNext();
    if (T.Kind == Token::TK_StreamEnd)
      break;
    else if (T.Kind == Token::TK_Error)
      return false;
  }
  return true;
}

std::string yaml::escape(StringRef Input, bool EscapePrintable) {
  std::string EscapedInput;
  for (StringRef::iterator i = Input.begin(), e = Input.end(); i != e; ++i) {
    if (*i == '\\')
      EscapedInput += "\\\\";
    else if (*i == '"')
      EscapedInput += "\\\"";
    else if (*i == 0)
      EscapedInput += "\\0";
    else if (*i == 0x07)
      EscapedInput += "\\a";
    else if (*i == 0x08)
      EscapedInput += "\\b";
    else if (*i == 0x09)
      EscapedInput += "\\t";
    else if (*i == 0x0A)
      EscapedInput += "\\n";
    else if (*i == 0x0B)
      EscapedInput += "\\v";
    else if (*i == 0x0C)
      EscapedInput += "\\f";
    else if (*i == 0x0D)
      EscapedInput += "\\r";
    else if (*i == 0x1B)
      EscapedInput += "\\e";
    else if ((unsigned char)*i < 0x20) { // Control characters not handled above.
      std::string HexStr = utohexstr(*i);
      EscapedInput += "\\x" + std::string(2 - HexStr.size(), '0') + HexStr;
    } else if (*i & 0x80) { // UTF-8 multiple code unit subsequence.
      UTF8Decoded UnicodeScalarValue
        = decodeUTF8(StringRef(i, Input.end() - i));
      if (UnicodeScalarValue.second == 0) {
        // Found invalid char.
        SmallString<4> Val;
        encodeUTF8(0xFFFD, Val);
        llvm::append_range(EscapedInput, Val);
        // FIXME: Error reporting.
        return EscapedInput;
      }
      if (UnicodeScalarValue.first == 0x85)
        EscapedInput += "\\N";
      else if (UnicodeScalarValue.first == 0xA0)
        EscapedInput += "\\_";
      else if (UnicodeScalarValue.first == 0x2028)
        EscapedInput += "\\L";
      else if (UnicodeScalarValue.first == 0x2029)
        EscapedInput += "\\P";
      else if (!EscapePrintable &&
               sys::unicode::isPrintable(UnicodeScalarValue.first))
        EscapedInput += StringRef(i, UnicodeScalarValue.second);
      else {
        std::string HexStr = utohexstr(UnicodeScalarValue.first);
        if (HexStr.size() <= 2)
          EscapedInput += "\\x" + std::string(2 - HexStr.size(), '0') + HexStr;
        else if (HexStr.size() <= 4)
          EscapedInput += "\\u" + std::string(4 - HexStr.size(), '0') + HexStr;
        else if (HexStr.size() <= 8)
          EscapedInput += "\\U" + std::string(8 - HexStr.size(), '0') + HexStr;
      }
      i += UnicodeScalarValue.second - 1;
    } else
      EscapedInput.push_back(*i);
  }
  return EscapedInput;
}

std::optional<bool> yaml::parseBool(StringRef S) {
  switch (S.size()) {
  case 1:
    switch (S.front()) {
    case 'y':
    case 'Y':
      return true;
    case 'n':
    case 'N':
      return false;
    default:
      return std::nullopt;
    }
  case 2:
    switch (S.front()) {
    case 'O':
      if (S[1] == 'N') // ON
        return true;
      [[fallthrough]];
    case 'o':
      if (S[1] == 'n') //[Oo]n
        return true;
      return std::nullopt;
    case 'N':
      if (S[1] == 'O') // NO
        return false;
      [[fallthrough]];
    case 'n':
      if (S[1] == 'o') //[Nn]o
        return false;
      return std::nullopt;
    default:
      return std::nullopt;
    }
  case 3:
    switch (S.front()) {
    case 'O':
      if (S.drop_front() == "FF") // OFF
        return false;
      [[fallthrough]];
    case 'o':
      if (S.drop_front() == "ff") //[Oo]ff
        return false;
      return std::nullopt;
    case 'Y':
      if (S.drop_front() == "ES") // YES
        return true;
      [[fallthrough]];
    case 'y':
      if (S.drop_front() == "es") //[Yy]es
        return true;
      return std::nullopt;
    default:
      return std::nullopt;
    }
  case 4:
    switch (S.front()) {
    case 'T':
      if (S.drop_front() == "RUE") // TRUE
        return true;
      [[fallthrough]];
    case 't':
      if (S.drop_front() == "rue") //[Tt]rue
        return true;
      return std::nullopt;
    default:
      return std::nullopt;
    }
  case 5:
    switch (S.front()) {
    case 'F':
      if (S.drop_front() == "ALSE") // FALSE
        return false;
      [[fallthrough]];
    case 'f':
      if (S.drop_front() == "alse") //[Ff]alse
        return false;
      return std::nullopt;
    default:
      return std::nullopt;
    }
  default:
    return std::nullopt;
  }
}

Scanner::Scanner(StringRef Input, SourceMgr &sm, bool ShowColors,
                 std::error_code *EC)
    : SM(sm), ShowColors(ShowColors), EC(EC) {
  init(MemoryBufferRef(Input, "YAML"));
}

Scanner::Scanner(MemoryBufferRef Buffer, SourceMgr &SM_, bool ShowColors,
                 std::error_code *EC)
    : SM(SM_), ShowColors(ShowColors), EC(EC) {
  init(Buffer);
}

void Scanner::init(MemoryBufferRef Buffer) {
  InputBuffer = Buffer;
  Current = InputBuffer.getBufferStart();
  End = InputBuffer.getBufferEnd();
  Indent = -1;
  Column = 0;
  Line = 0;
  FlowLevel = 0;
  IsStartOfStream = true;
  IsSimpleKeyAllowed = true;
  IsAdjacentValueAllowedInFlow = false;
  Failed = false;
  std::unique_ptr<MemoryBuffer> InputBufferOwner =
      MemoryBuffer::getMemBuffer(Buffer, /*RequiresNullTerminator=*/false);
  SM.AddNewSourceBuffer(std::move(InputBufferOwner), SMLoc());
}

Token &Scanner::peekNext() {
  // If the current token is a possible simple key, keep parsing until we
  // can confirm.
  bool NeedMore = false;
  while (true) {
    if (TokenQueue.empty() || NeedMore) {
      if (!fetchMoreTokens()) {
        TokenQueue.clear();
        SimpleKeys.clear();
        TokenQueue.push_back(Token());
        return TokenQueue.front();
      }
    }
    assert(!TokenQueue.empty() &&
            "fetchMoreTokens lied about getting tokens!");

    removeStaleSimpleKeyCandidates();
    SimpleKey SK;
    SK.Tok = TokenQueue.begin();
    if (!is_contained(SimpleKeys, SK))
      break;
    else
      NeedMore = true;
  }
  return TokenQueue.front();
}

Token Scanner::getNext() {
  Token Ret = peekNext();
  // TokenQueue can be empty if there was an error getting the next token.
  if (!TokenQueue.empty())
    TokenQueue.pop_front();

  // There cannot be any referenced Token's if the TokenQueue is empty. So do a
  // quick deallocation of them all.
  if (TokenQueue.empty())
    TokenQueue.resetAlloc();

  return Ret;
}

StringRef::iterator Scanner::skip_nb_char(StringRef::iterator Position) {
  if (Position == End)
    return Position;
  // Check 7 bit c-printable - b-char.
  if (   *Position == 0x09
      || (*Position >= 0x20 && *Position <= 0x7E))
    return Position + 1;

  // Check for valid UTF-8.
  if (uint8_t(*Position) & 0x80) {
    UTF8Decoded u8d = decodeUTF8(Position);
    if (   u8d.second != 0
        && u8d.first != 0xFEFF
        && ( u8d.first == 0x85
          || ( u8d.first >= 0xA0
            && u8d.first <= 0xD7FF)
          || ( u8d.first >= 0xE000
            && u8d.first <= 0xFFFD)
          || ( u8d.first >= 0x10000
            && u8d.first <= 0x10FFFF)))
      return Position + u8d.second;
  }
  return Position;
}

StringRef::iterator Scanner::skip_b_break(StringRef::iterator Position) {
  if (Position == End)
    return Position;
  if (*Position == 0x0D) {
    if (Position + 1 != End && *(Position + 1) == 0x0A)
      return Position + 2;
    return Position + 1;
  }

  if (*Position == 0x0A)
    return Position + 1;
  return Position;
}

StringRef::iterator Scanner::skip_s_space(StringRef::iterator Position) {
  if (Position == End)
    return Position;
  if (*Position == ' ')
    return Position + 1;
  return Position;
}

StringRef::iterator Scanner::skip_s_white(StringRef::iterator Position) {
  if (Position == End)
    return Position;
  if (*Position == ' ' || *Position == '\t')
    return Position + 1;
  return Position;
}

StringRef::iterator Scanner::skip_ns_char(StringRef::iterator Position) {
  if (Position == End)
    return Position;
  if (*Position == ' ' || *Position == '\t')
    return Position;
  return skip_nb_char(Position);
}

StringRef::iterator Scanner::skip_while( SkipWhileFunc Func
                                       , StringRef::iterator Position) {
  while (true) {
    StringRef::iterator i = (this->*Func)(Position);
    if (i == Position)
      break;
    Position = i;
  }
  return Position;
}

void Scanner::advanceWhile(SkipWhileFunc Func) {
  auto Final = skip_while(Func, Current);
  Column += Final - Current;
  Current = Final;
}

static bool is_ns_hex_digit(const char C) { return isAlnum(C); }

static bool is_ns_word_char(const char C) { return C == '-' || isAlpha(C); }

void Scanner::scan_ns_uri_char() {
  while (true) {
    if (Current == End)
      break;
    if ((   *Current == '%'
          && Current + 2 < End
          && is_ns_hex_digit(*(Current + 1))
          && is_ns_hex_digit(*(Current + 2)))
        || is_ns_word_char(*Current)
        || StringRef(Current, 1).find_first_of("#;/?:@&=+$,_.!~*'()[]")
          != StringRef::npos) {
      ++Current;
      ++Column;
    } else
      break;
  }
}

bool Scanner::consume(uint32_t Expected) {
  if (Expected >= 0x80) {
    setError("Cannot consume non-ascii characters", Current);
    return false;
  }
  if (Current == End)
    return false;
  if (uint8_t(*Current) >= 0x80) {
    setError("Cannot consume non-ascii characters", Current);
    return false;
  }
  if (uint8_t(*Current) == Expected) {
    ++Current;
    ++Column;
    return true;
  }
  return false;
}

void Scanner::skip(uint32_t Distance) {
  Current += Distance;
  Column += Distance;
  assert(Current <= End && "Skipped past the end");
}

bool Scanner::isBlankOrBreak(StringRef::iterator Position) {
  if (Position == End)
    return false;
  return *Position == ' ' || *Position == '\t' || *Position == '\r' ||
         *Position == '\n';
}

bool Scanner::isPlainSafeNonBlank(StringRef::iterator Position) {
  if (Position == End || isBlankOrBreak(Position))
    return false;
  if (FlowLevel &&
      StringRef(Position, 1).find_first_of(",[]{}") != StringRef::npos)
    return false;
  return true;
}

bool Scanner::isLineEmpty(StringRef Line) {
  for (const auto *Position = Line.begin(); Position != Line.end(); ++Position)
    if (!isBlankOrBreak(Position))
      return false;
  return true;
}

bool Scanner::consumeLineBreakIfPresent() {
  auto Next = skip_b_break(Current);
  if (Next == Current)
    return false;
  Column = 0;
  ++Line;
  Current = Next;
  return true;
}

void Scanner::saveSimpleKeyCandidate( TokenQueueT::iterator Tok
                                    , unsigned AtColumn
                                    , bool IsRequired) {
  if (IsSimpleKeyAllowed) {
    SimpleKey SK;
    SK.Tok = Tok;
    SK.Line = Line;
    SK.Column = AtColumn;
    SK.IsRequired = IsRequired;
    SK.FlowLevel = FlowLevel;
    SimpleKeys.push_back(SK);
  }
}

void Scanner::removeStaleSimpleKeyCandidates() {
  for (SmallVectorImpl<SimpleKey>::iterator i = SimpleKeys.begin();
                                            i != SimpleKeys.end();) {
    if (i->Line != Line || i->Column + 1024 < Column) {
      if (i->IsRequired)
        setError( "Could not find expected : for simple key"
                , i->Tok->Range.begin());
      i = SimpleKeys.erase(i);
    } else
      ++i;
  }
}

void Scanner::removeSimpleKeyCandidatesOnFlowLevel(unsigned Level) {
  if (!SimpleKeys.empty() && (SimpleKeys.end() - 1)->FlowLevel == Level)
    SimpleKeys.pop_back();
}

bool Scanner::unrollIndent(int ToColumn) {
  Token T;
  // Indentation is ignored in flow.
  if (FlowLevel != 0)
    return true;

  while (Indent > ToColumn) {
    T.Kind = Token::TK_BlockEnd;
    T.Range = StringRef(Current, 1);
    TokenQueue.push_back(T);
    Indent = Indents.pop_back_val();
  }

  return true;
}

bool Scanner::rollIndent( int ToColumn
                        , Token::TokenKind Kind
                        , TokenQueueT::iterator InsertPoint) {
  if (FlowLevel)
    return true;
  if (Indent < ToColumn) {
    Indents.push_back(Indent);
    Indent = ToColumn;

    Token T;
    T.Kind = Kind;
    T.Range = StringRef(Current, 0);
    TokenQueue.insert(InsertPoint, T);
  }
  return true;
}

void Scanner::skipComment() {
  if (Current == End || *Current != '#')
    return;
  while (true) {
    // This may skip more than one byte, thus Column is only incremented
    // for code points.
    StringRef::iterator I = skip_nb_char(Current);
    if (I == Current)
      break;
    Current = I;
    ++Column;
  }
}

void Scanner::scanToNextToken() {
  while (true) {
    while (Current != End && (*Current == ' ' || *Current == '\t')) {
      skip(1);
    }

    skipComment();

    // Skip EOL.
    StringRef::iterator i = skip_b_break(Current);
    if (i == Current)
      break;
    Current = i;
    ++Line;
    Column = 0;
    // New lines may start a simple key.
    if (!FlowLevel)
      IsSimpleKeyAllowed = true;
  }
}

bool Scanner::scanStreamStart() {
  IsStartOfStream = false;

  EncodingInfo EI = getUnicodeEncoding(currentInput());

  Token T;
  T.Kind = Token::TK_StreamStart;
  T.Range = StringRef(Current, EI.second);
  TokenQueue.push_back(T);
  Current += EI.second;
  return true;
}

bool Scanner::scanStreamEnd() {
  // Force an ending new line if one isn't present.
  if (Column != 0) {
    Column = 0;
    ++Line;
  }

  unrollIndent(-1);
  SimpleKeys.clear();
  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = false;

  Token T;
  T.Kind = Token::TK_StreamEnd;
  T.Range = StringRef(Current, 0);
  TokenQueue.push_back(T);
  return true;
}

bool Scanner::scanDirective() {
  // Reset the indentation level.
  unrollIndent(-1);
  SimpleKeys.clear();
  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = false;

  StringRef::iterator Start = Current;
  consume('%');
  StringRef::iterator NameStart = Current;
  Current = skip_while(&Scanner::skip_ns_char, Current);
  StringRef Name(NameStart, Current - NameStart);
  Current = skip_while(&Scanner::skip_s_white, Current);

  Token T;
  if (Name == "YAML") {
    Current = skip_while(&Scanner::skip_ns_char, Current);
    T.Kind = Token::TK_VersionDirective;
    T.Range = StringRef(Start, Current - Start);
    TokenQueue.push_back(T);
    return true;
  } else if(Name == "TAG") {
    Current = skip_while(&Scanner::skip_ns_char, Current);
    Current = skip_while(&Scanner::skip_s_white, Current);
    Current = skip_while(&Scanner::skip_ns_char, Current);
    T.Kind = Token::TK_TagDirective;
    T.Range = StringRef(Start, Current - Start);
    TokenQueue.push_back(T);
    return true;
  }
  return false;
}

bool Scanner::scanDocumentIndicator(bool IsStart) {
  unrollIndent(-1);
  SimpleKeys.clear();
  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = false;

  Token T;
  T.Kind = IsStart ? Token::TK_DocumentStart : Token::TK_DocumentEnd;
  T.Range = StringRef(Current, 3);
  skip(3);
  TokenQueue.push_back(T);
  return true;
}

bool Scanner::scanFlowCollectionStart(bool IsSequence) {
  Token T;
  T.Kind = IsSequence ? Token::TK_FlowSequenceStart
                      : Token::TK_FlowMappingStart;
  T.Range = StringRef(Current, 1);
  skip(1);
  TokenQueue.push_back(T);

  // [ and { may begin a simple key.
  saveSimpleKeyCandidate(--TokenQueue.end(), Column - 1, false);

  // And may also be followed by a simple key.
  IsSimpleKeyAllowed = true;
  // Adjacent values are allowed in flows only after JSON-style keys.
  IsAdjacentValueAllowedInFlow = false;
  ++FlowLevel;
  return true;
}

bool Scanner::scanFlowCollectionEnd(bool IsSequence) {
  removeSimpleKeyCandidatesOnFlowLevel(FlowLevel);
  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = true;
  Token T;
  T.Kind = IsSequence ? Token::TK_FlowSequenceEnd
                      : Token::TK_FlowMappingEnd;
  T.Range = StringRef(Current, 1);
  skip(1);
  TokenQueue.push_back(T);
  if (FlowLevel)
    --FlowLevel;
  return true;
}

bool Scanner::scanFlowEntry() {
  removeSimpleKeyCandidatesOnFlowLevel(FlowLevel);
  IsSimpleKeyAllowed = true;
  IsAdjacentValueAllowedInFlow = false;
  Token T;
  T.Kind = Token::TK_FlowEntry;
  T.Range = StringRef(Current, 1);
  skip(1);
  TokenQueue.push_back(T);
  return true;
}

bool Scanner::scanBlockEntry() {
  rollIndent(Column, Token::TK_BlockSequenceStart, TokenQueue.end());
  removeSimpleKeyCandidatesOnFlowLevel(FlowLevel);
  IsSimpleKeyAllowed = true;
  IsAdjacentValueAllowedInFlow = false;
  Token T;
  T.Kind = Token::TK_BlockEntry;
  T.Range = StringRef(Current, 1);
  skip(1);
  TokenQueue.push_back(T);
  return true;
}

bool Scanner::scanKey() {
  if (!FlowLevel)
    rollIndent(Column, Token::TK_BlockMappingStart, TokenQueue.end());

  removeSimpleKeyCandidatesOnFlowLevel(FlowLevel);
  IsSimpleKeyAllowed = !FlowLevel;
  IsAdjacentValueAllowedInFlow = false;

  Token T;
  T.Kind = Token::TK_Key;
  T.Range = StringRef(Current, 1);
  skip(1);
  TokenQueue.push_back(T);
  return true;
}

bool Scanner::scanValue() {
  // If the previous token could have been a simple key, insert the key token
  // into the token queue.
  if (!SimpleKeys.empty()) {
    SimpleKey SK = SimpleKeys.pop_back_val();
    Token T;
    T.Kind = Token::TK_Key;
    T.Range = SK.Tok->Range;
    TokenQueueT::iterator i, e;
    for (i = TokenQueue.begin(), e = TokenQueue.end(); i != e; ++i) {
      if (i == SK.Tok)
        break;
    }
    if (i == e) {
      Failed = true;
      return false;
    }
    i = TokenQueue.insert(i, T);

    // We may also need to add a Block-Mapping-Start token.
    rollIndent(SK.Column, Token::TK_BlockMappingStart, i);

    IsSimpleKeyAllowed = false;
  } else {
    if (!FlowLevel)
      rollIndent(Column, Token::TK_BlockMappingStart, TokenQueue.end());
    IsSimpleKeyAllowed = !FlowLevel;
  }
  IsAdjacentValueAllowedInFlow = false;

  Token T;
  T.Kind = Token::TK_Value;
  T.Range = StringRef(Current, 1);
  skip(1);
  TokenQueue.push_back(T);
  return true;
}

// Forbidding inlining improves performance by roughly 20%.
// FIXME: Remove once llvm optimizes this to the faster version without hints.
LLVM_ATTRIBUTE_NOINLINE static bool
wasEscaped(StringRef::iterator First, StringRef::iterator Position);

// Returns whether a character at 'Position' was escaped with a leading '\'.
// 'First' specifies the position of the first character in the string.
static bool wasEscaped(StringRef::iterator First,
                       StringRef::iterator Position) {
  assert(Position - 1 >= First);
  StringRef::iterator I = Position - 1;
  // We calculate the number of consecutive '\'s before the current position
  // by iterating backwards through our string.
  while (I >= First && *I == '\\') --I;
  // (Position - 1 - I) now contains the number of '\'s before the current
  // position. If it is odd, the character at 'Position' was escaped.
  return (Position - 1 - I) % 2 == 1;
}

bool Scanner::scanFlowScalar(bool IsDoubleQuoted) {
  StringRef::iterator Start = Current;
  unsigned ColStart = Column;
  if (IsDoubleQuoted) {
    do {
      ++Current;
      while (Current != End && *Current != '"')
        ++Current;
      // Repeat until the previous character was not a '\' or was an escaped
      // backslash.
    } while (   Current != End
             && *(Current - 1) == '\\'
             && wasEscaped(Start + 1, Current));
  } else {
    skip(1);
    while (Current != End) {
      // Skip a ' followed by another '.
      if (Current + 1 < End && *Current == '\'' && *(Current + 1) == '\'') {
        skip(2);
        continue;
      } else if (*Current == '\'')
        break;
      StringRef::iterator i = skip_nb_char(Current);
      if (i == Current) {
        i = skip_b_break(Current);
        if (i == Current)
          break;
        Current = i;
        Column = 0;
        ++Line;
      } else {
        if (i == End)
          break;
        Current = i;
        ++Column;
      }
    }
  }

  if (Current == End) {
    setError("Expected quote at end of scalar", Current);
    return false;
  }

  skip(1); // Skip ending quote.
  Token T;
  T.Kind = Token::TK_Scalar;
  T.Range = StringRef(Start, Current - Start);
  TokenQueue.push_back(T);

  saveSimpleKeyCandidate(--TokenQueue.end(), ColStart, false);

  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = true;

  return true;
}

bool Scanner::scanPlainScalar() {
  StringRef::iterator Start = Current;
  unsigned ColStart = Column;
  unsigned LeadingBlanks = 0;
  assert(Indent >= -1 && "Indent must be >= -1 !");
  unsigned indent = static_cast<unsigned>(Indent + 1);
  while (Current != End) {
    if (*Current == '#')
      break;

    while (Current != End &&
           ((*Current != ':' && isPlainSafeNonBlank(Current)) ||
            (*Current == ':' && isPlainSafeNonBlank(Current + 1)))) {
      StringRef::iterator i = skip_nb_char(Current);
      if (i == Current)
        break;
      Current = i;
      ++Column;
    }

    // Are we at the end?
    if (!isBlankOrBreak(Current))
      break;

    // Eat blanks.
    StringRef::iterator Tmp = Current;
    while (isBlankOrBreak(Tmp)) {
      StringRef::iterator i = skip_s_white(Tmp);
      if (i != Tmp) {
        if (LeadingBlanks && (Column < indent) && *Tmp == '\t') {
          setError("Found invalid tab character in indentation", Tmp);
          return false;
        }
        Tmp = i;
        ++Column;
      } else {
        i = skip_b_break(Tmp);
        if (!LeadingBlanks)
          LeadingBlanks = 1;
        Tmp = i;
        Column = 0;
        ++Line;
      }
    }

    if (!FlowLevel && Column < indent)
      break;

    Current = Tmp;
  }
  if (Start == Current) {
    setError("Got empty plain scalar", Start);
    return false;
  }
  Token T;
  T.Kind = Token::TK_Scalar;
  T.Range = StringRef(Start, Current - Start);
  TokenQueue.push_back(T);

  // Plain scalars can be simple keys.
  saveSimpleKeyCandidate(--TokenQueue.end(), ColStart, false);

  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = false;

  return true;
}

bool Scanner::scanAliasOrAnchor(bool IsAlias) {
  StringRef::iterator Start = Current;
  unsigned ColStart = Column;
  skip(1);
  while (Current != End) {
    if (   *Current == '[' || *Current == ']'
        || *Current == '{' || *Current == '}'
        || *Current == ','
        || *Current == ':')
      break;
    StringRef::iterator i = skip_ns_char(Current);
    if (i == Current)
      break;
    Current = i;
    ++Column;
  }

  if (Start + 1 == Current) {
    setError("Got empty alias or anchor", Start);
    return false;
  }

  Token T;
  T.Kind = IsAlias ? Token::TK_Alias : Token::TK_Anchor;
  T.Range = StringRef(Start, Current - Start);
  TokenQueue.push_back(T);

  // Alias and anchors can be simple keys.
  saveSimpleKeyCandidate(--TokenQueue.end(), ColStart, false);

  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = false;

  return true;
}

bool Scanner::scanBlockScalarIndicators(char &StyleIndicator,
                                        char &ChompingIndicator,
                                        unsigned &IndentIndicator,
                                        bool &IsDone) {
  StyleIndicator = scanBlockStyleIndicator();
  if (!scanBlockScalarHeader(ChompingIndicator, IndentIndicator, IsDone))
    return false;
  return true;
}

char Scanner::scanBlockStyleIndicator() {
  char Indicator = ' ';
  if (Current != End && (*Current == '>' || *Current == '|')) {
    Indicator = *Current;
    skip(1);
  }
  return Indicator;
}

char Scanner::scanBlockChompingIndicator() {
  char Indicator = ' ';
  if (Current != End && (*Current == '+' || *Current == '-')) {
    Indicator = *Current;
    skip(1);
  }
  return Indicator;
}

/// Get the number of line breaks after chomping.
///
/// Return the number of trailing line breaks to emit, depending on
/// \p ChompingIndicator.
static unsigned getChompedLineBreaks(char ChompingIndicator,
                                     unsigned LineBreaks, StringRef Str) {
  if (ChompingIndicator == '-') // Strip all line breaks.
    return 0;
  if (ChompingIndicator == '+') // Keep all line breaks.
    return LineBreaks;
  // Clip trailing lines.
  return Str.empty() ? 0 : 1;
}

unsigned Scanner::scanBlockIndentationIndicator() {
  unsigned Indent = 0;
  if (Current != End && (*Current >= '1' && *Current <= '9')) {
    Indent = unsigned(*Current - '0');
    skip(1);
  }
  return Indent;
}

bool Scanner::scanBlockScalarHeader(char &ChompingIndicator,
                                    unsigned &IndentIndicator, bool &IsDone) {
  auto Start = Current;

  ChompingIndicator = scanBlockChompingIndicator();
  IndentIndicator = scanBlockIndentationIndicator();
  // Check for the chomping indicator once again.
  if (ChompingIndicator == ' ')
    ChompingIndicator = scanBlockChompingIndicator();
  Current = skip_while(&Scanner::skip_s_white, Current);
  skipComment();

  if (Current == End) { // EOF, we have an empty scalar.
    Token T;
    T.Kind = Token::TK_BlockScalar;
    T.Range = StringRef(Start, Current - Start);
    TokenQueue.push_back(T);
    IsDone = true;
    return true;
  }

  if (!consumeLineBreakIfPresent()) {
    setError("Expected a line break after block scalar header", Current);
    return false;
  }
  return true;
}

bool Scanner::findBlockScalarIndent(unsigned &BlockIndent,
                                    unsigned BlockExitIndent,
                                    unsigned &LineBreaks, bool &IsDone) {
  unsigned MaxAllSpaceLineCharacters = 0;
  StringRef::iterator LongestAllSpaceLine;

  while (true) {
    advanceWhile(&Scanner::skip_s_space);
    if (skip_nb_char(Current) != Current) {
      // This line isn't empty, so try and find the indentation.
      if (Column <= BlockExitIndent) { // End of the block literal.
        IsDone = true;
        return true;
      }
      // We found the block's indentation.
      BlockIndent = Column;
      if (MaxAllSpaceLineCharacters > BlockIndent) {
        setError(
            "Leading all-spaces line must be smaller than the block indent",
            LongestAllSpaceLine);
        return false;
      }
      return true;
    }
    if (skip_b_break(Current) != Current &&
        Column > MaxAllSpaceLineCharacters) {
      // Record the longest all-space line in case it's longer than the
      // discovered block indent.
      MaxAllSpaceLineCharacters = Column;
      LongestAllSpaceLine = Current;
    }

    // Check for EOF.
    if (Current == End) {
      IsDone = true;
      return true;
    }

    if (!consumeLineBreakIfPresent()) {
      IsDone = true;
      return true;
    }
    ++LineBreaks;
  }
  return true;
}

bool Scanner::scanBlockScalarIndent(unsigned BlockIndent,
                                    unsigned BlockExitIndent, bool &IsDone) {
  // Skip the indentation.
  while (Column < BlockIndent) {
    auto I = skip_s_space(Current);
    if (I == Current)
      break;
    Current = I;
    ++Column;
  }

  if (skip_nb_char(Current) == Current)
    return true;

  if (Column <= BlockExitIndent) { // End of the block literal.
    IsDone = true;
    return true;
  }

  if (Column < BlockIndent) {
    if (Current != End && *Current == '#') { // Trailing comment.
      IsDone = true;
      return true;
    }
    setError("A text line is less indented than the block scalar", Current);
    return false;
  }
  return true; // A normal text line.
}

bool Scanner::scanBlockScalar(bool IsLiteral) {
  assert(*Current == '|' || *Current == '>');
  char StyleIndicator;
  char ChompingIndicator;
  unsigned BlockIndent;
  bool IsDone = false;
  if (!scanBlockScalarIndicators(StyleIndicator, ChompingIndicator, BlockIndent,
                                 IsDone))
    return false;
  if (IsDone)
    return true;
  bool IsFolded = StyleIndicator == '>';

  const auto *Start = Current;
  unsigned BlockExitIndent = Indent < 0 ? 0 : (unsigned)Indent;
  unsigned LineBreaks = 0;
  if (BlockIndent == 0) {
    if (!findBlockScalarIndent(BlockIndent, BlockExitIndent, LineBreaks,
                               IsDone))
      return false;
  }

  // Scan the block's scalars body.
  SmallString<256> Str;
  while (!IsDone) {
    if (!scanBlockScalarIndent(BlockIndent, BlockExitIndent, IsDone))
      return false;
    if (IsDone)
      break;

    // Parse the current line.
    auto LineStart = Current;
    advanceWhile(&Scanner::skip_nb_char);
    if (LineStart != Current) {
      if (LineBreaks && IsFolded && !Scanner::isLineEmpty(Str)) {
        // The folded style "folds" any single line break between content into a
        // single space, except when that content is "empty" (only contains
        // whitespace) in which case the line break is left as-is.
        if (LineBreaks == 1) {
          Str.append(LineBreaks,
                     isLineEmpty(StringRef(LineStart, Current - LineStart))
                         ? '\n'
                         : ' ');
        }
        // If we saw a single line break, we are completely replacing it and so
        // want `LineBreaks == 0`. Otherwise this decrement accounts for the
        // fact that the first line break is "trimmed", only being used to
        // signal a sequence of line breaks which should not be folded.
        LineBreaks--;
      }
      Str.append(LineBreaks, '\n');
      Str.append(StringRef(LineStart, Current - LineStart));
      LineBreaks = 0;
    }

    // Check for EOF.
    if (Current == End)
      break;

    if (!consumeLineBreakIfPresent())
      break;
    ++LineBreaks;
  }

  if (Current == End && !LineBreaks)
    // Ensure that there is at least one line break before the end of file.
    LineBreaks = 1;
  Str.append(getChompedLineBreaks(ChompingIndicator, LineBreaks, Str), '\n');

  // New lines may start a simple key.
  if (!FlowLevel)
    IsSimpleKeyAllowed = true;
  IsAdjacentValueAllowedInFlow = false;

  Token T;
  T.Kind = Token::TK_BlockScalar;
  T.Range = StringRef(Start, Current - Start);
  T.Value = std::string(Str);
  TokenQueue.push_back(T);
  return true;
}

bool Scanner::scanTag() {
  StringRef::iterator Start = Current;
  unsigned ColStart = Column;
  skip(1); // Eat !.
  if (Current == End || isBlankOrBreak(Current)); // An empty tag.
  else if (*Current == '<') {
    skip(1);
    scan_ns_uri_char();
    if (!consume('>'))
      return false;
  } else {
    // FIXME: Actually parse the c-ns-shorthand-tag rule.
    Current = skip_while(&Scanner::skip_ns_char, Current);
  }

  Token T;
  T.Kind = Token::TK_Tag;
  T.Range = StringRef(Start, Current - Start);
  TokenQueue.push_back(T);

  // Tags can be simple keys.
  saveSimpleKeyCandidate(--TokenQueue.end(), ColStart, false);

  IsSimpleKeyAllowed = false;
  IsAdjacentValueAllowedInFlow = false;

  return true;
}

bool Scanner::fetchMoreTokens() {
  if (IsStartOfStream)
    return scanStreamStart();

  scanToNextToken();

  if (Current == End)
    return scanStreamEnd();

  removeStaleSimpleKeyCandidates();

  unrollIndent(Column);

  if (Column == 0 && *Current == '%')
    return scanDirective();

  if (Column == 0 && Current + 4 <= End
      && *Current == '-'
      && *(Current + 1) == '-'
      && *(Current + 2) == '-'
      && (Current + 3 == End || isBlankOrBreak(Current + 3)))
    return scanDocumentIndicator(true);

  if (Column == 0 && Current + 4 <= End
      && *Current == '.'
      && *(Current + 1) == '.'
      && *(Current + 2) == '.'
      && (Current + 3 == End || isBlankOrBreak(Current + 3)))
    return scanDocumentIndicator(false);

  if (*Current == '[')
    return scanFlowCollectionStart(true);

  if (*Current == '{')
    return scanFlowCollectionStart(false);

  if (*Current == ']')
    return scanFlowCollectionEnd(true);

  if (*Current == '}')
    return scanFlowCollectionEnd(false);

  if (*Current == ',')
    return scanFlowEntry();

  if (*Current == '-' && (isBlankOrBreak(Current + 1) || Current + 1 == End))
    return scanBlockEntry();

  if (*Current == '?' && (Current + 1 == End || isBlankOrBreak(Current + 1)))
    return scanKey();

  if (*Current == ':' &&
      (!isPlainSafeNonBlank(Current + 1) || IsAdjacentValueAllowedInFlow))
    return scanValue();

  if (*Current == '*')
    return scanAliasOrAnchor(true);

  if (*Current == '&')
    return scanAliasOrAnchor(false);

  if (*Current == '!')
    return scanTag();

  if (*Current == '|' && !FlowLevel)
    return scanBlockScalar(true);

  if (*Current == '>' && !FlowLevel)
    return scanBlockScalar(false);

  if (*Current == '\'')
    return scanFlowScalar(false);

  if (*Current == '"')
    return scanFlowScalar(true);

  // Get a plain scalar.
  StringRef FirstChar(Current, 1);
  if ((!isBlankOrBreak(Current) &&
       FirstChar.find_first_of("-?:,[]{}#&*!|>'\"%@`") == StringRef::npos) ||
      (FirstChar.find_first_of("?:-") != StringRef::npos &&
       isPlainSafeNonBlank(Current + 1)))
    return scanPlainScalar();

  setError("Unrecognized character while tokenizing.", Current);
  return false;
}

Stream::Stream(StringRef Input, SourceMgr &SM, bool ShowColors,
               std::error_code *EC)
    : scanner(new Scanner(Input, SM, ShowColors, EC)) {}

Stream::Stream(MemoryBufferRef InputBuffer, SourceMgr &SM, bool ShowColors,
               std::error_code *EC)
    : scanner(new Scanner(InputBuffer, SM, ShowColors, EC)) {}

Stream::~Stream() = default;

bool Stream::failed() { return scanner->failed(); }

void Stream::printError(Node *N, const Twine &Msg, SourceMgr::DiagKind Kind) {
  printError(N ? N->getSourceRange() : SMRange(), Msg, Kind);
}

void Stream::printError(const SMRange &Range, const Twine &Msg,
                        SourceMgr::DiagKind Kind) {
  scanner->printError(Range.Start, Kind, Msg, Range);
}

document_iterator Stream::begin() {
  if (CurrentDoc)
    report_fatal_error("Can only iterate over the stream once");

  // Skip Stream-Start.
  scanner->getNext();

  CurrentDoc.reset(new Document(*this));
  return document_iterator(CurrentDoc);
}

document_iterator Stream::end() {
  return document_iterator();
}

void Stream::skip() {
  for (Document &Doc : *this)
    Doc.skip();
}

Node::Node(unsigned int Type, std::unique_ptr<Document> &D, StringRef A,
           StringRef T)
    : Doc(D), TypeID(Type), Anchor(A), Tag(T) {
  SMLoc Start = SMLoc::getFromPointer(peekNext().Range.begin());
  SourceRange = SMRange(Start, Start);
}

std::string Node::getVerbatimTag() const {
  StringRef Raw = getRawTag();
  if (!Raw.empty() && Raw != "!") {
    std::string Ret;
    if (Raw.find_last_of('!') == 0) {
      Ret = std::string(Doc->getTagMap().find("!")->second);
      Ret += Raw.substr(1);
      return Ret;
    } else if (Raw.starts_with("!!")) {
      Ret = std::string(Doc->getTagMap().find("!!")->second);
      Ret += Raw.substr(2);
      return Ret;
    } else {
      StringRef TagHandle = Raw.substr(0, Raw.find_last_of('!') + 1);
      std::map<StringRef, StringRef>::const_iterator It =
          Doc->getTagMap().find(TagHandle);
      if (It != Doc->getTagMap().end())
        Ret = std::string(It->second);
      else {
        Token T;
        T.Kind = Token::TK_Tag;
        T.Range = TagHandle;
        setError(Twine("Unknown tag handle ") + TagHandle, T);
      }
      Ret += Raw.substr(Raw.find_last_of('!') + 1);
      return Ret;
    }
  }

  switch (getType()) {
  case NK_Null:
    return "tag:yaml.org,2002:null";
  case NK_Scalar:
  case NK_BlockScalar:
    // TODO: Tag resolution.
    return "tag:yaml.org,2002:str";
  case NK_Mapping:
    return "tag:yaml.org,2002:map";
  case NK_Sequence:
    return "tag:yaml.org,2002:seq";
  }

  return "";
}

Token &Node::peekNext() {
  return Doc->peekNext();
}

Token Node::getNext() {
  return Doc->getNext();
}

Node *Node::parseBlockNode() {
  return Doc->parseBlockNode();
}

BumpPtrAllocator &Node::getAllocator() {
  return Doc->NodeAllocator;
}

void Node::setError(const Twine &Msg, Token &Tok) const {
  Doc->setError(Msg, Tok);
}

bool Node::failed() const {
  return Doc->failed();
}

StringRef ScalarNode::getValue(SmallVectorImpl<char> &Storage) const {
  if (Value[0] == '"')
    return getDoubleQuotedValue(Value, Storage);
  if (Value[0] == '\'')
    return getSingleQuotedValue(Value, Storage);
  return getPlainValue(Value, Storage);
}

/// parseScalarValue - A common parsing routine for all flow scalar styles.
/// It handles line break characters by itself, adds regular content characters
/// to the result, and forwards escaped sequences to the provided routine for
/// the style-specific processing.
///
/// \param UnquotedValue - An input value without quotation marks.
/// \param Storage - A storage for the result if the input value is multiline or
/// contains escaped characters.
/// \param LookupChars - A set of special characters to search in the input
/// string. Should include line break characters and the escape character
/// specific for the processing scalar style, if any.
/// \param UnescapeCallback - This is called when the escape character is found
/// in the input.
/// \returns - The unfolded and unescaped value.
static StringRef
parseScalarValue(StringRef UnquotedValue, SmallVectorImpl<char> &Storage,
                 StringRef LookupChars,
                 std::function<StringRef(StringRef, SmallVectorImpl<char> &)>
                     UnescapeCallback) {
  size_t I = UnquotedValue.find_first_of(LookupChars);
  if (I == StringRef::npos)
    return UnquotedValue;

  Storage.clear();
  Storage.reserve(UnquotedValue.size());
  char LastNewLineAddedAs = '\0';
  for (; I != StringRef::npos; I = UnquotedValue.find_first_of(LookupChars)) {
    if (UnquotedValue[I] != '\r' && UnquotedValue[I] != '\n') {
      llvm::append_range(Storage, UnquotedValue.take_front(I));
      UnquotedValue = UnescapeCallback(UnquotedValue.drop_front(I), Storage);
      LastNewLineAddedAs = '\0';
      continue;
    }
    if (size_t LastNonSWhite = UnquotedValue.find_last_not_of(" \t", I);
        LastNonSWhite != StringRef::npos) {
      llvm::append_range(Storage, UnquotedValue.take_front(LastNonSWhite + 1));
      Storage.push_back(' ');
      LastNewLineAddedAs = ' ';
    } else {
      // Note: we can't just check if the last character in Storage is ' ',
      // '\n', or something else; that would give a wrong result for double
      // quoted values containing an escaped space character before a new-line
      // character.
      switch (LastNewLineAddedAs) {
      case ' ':
        assert(!Storage.empty() && Storage.back() == ' ');
        Storage.back() = '\n';
        LastNewLineAddedAs = '\n';
        break;
      case '\n':
        assert(!Storage.empty() && Storage.back() == '\n');
        Storage.push_back('\n');
        break;
      default:
        Storage.push_back(' ');
        LastNewLineAddedAs = ' ';
        break;
      }
    }
    // Handle Windows-style EOL
    if (UnquotedValue.substr(I, 2) == "\r\n")
      I++;
    UnquotedValue = UnquotedValue.drop_front(I + 1).ltrim(" \t");
  }
  llvm::append_range(Storage, UnquotedValue);
  return StringRef(Storage.begin(), Storage.size());
}

StringRef
ScalarNode::getDoubleQuotedValue(StringRef RawValue,
                                 SmallVectorImpl<char> &Storage) const {
  assert(RawValue.size() >= 2 && RawValue.front() == '"' &&
         RawValue.back() == '"');
  StringRef UnquotedValue = RawValue.substr(1, RawValue.size() - 2);

  auto UnescapeFunc = [this](StringRef UnquotedValue,
                             SmallVectorImpl<char> &Storage) {
    assert(UnquotedValue.take_front(1) == "\\");
    if (UnquotedValue.size() == 1) {
      Token T;
      T.Range = UnquotedValue;
      setError("Unrecognized escape code", T);
      Storage.clear();
      return StringRef();
    }
    UnquotedValue = UnquotedValue.drop_front(1);
    switch (UnquotedValue[0]) {
    default: {
      Token T;
      T.Range = UnquotedValue.take_front(1);
      setError("Unrecognized escape code", T);
      Storage.clear();
      return StringRef();
    }
    case '\r':
      // Shrink the Windows-style EOL.
      if (UnquotedValue.size() >= 2 && UnquotedValue[1] == '\n')
        UnquotedValue = UnquotedValue.drop_front(1);
      [[fallthrough]];
    case '\n':
      return UnquotedValue.drop_front(1).ltrim(" \t");
    case '0':
      Storage.push_back(0x00);
      break;
    case 'a':
      Storage.push_back(0x07);
      break;
    case 'b':
      Storage.push_back(0x08);
      break;
    case 't':
    case 0x09:
      Storage.push_back(0x09);
      break;
    case 'n':
      Storage.push_back(0x0A);
      break;
    case 'v':
      Storage.push_back(0x0B);
      break;
    case 'f':
      Storage.push_back(0x0C);
      break;
    case 'r':
      Storage.push_back(0x0D);
      break;
    case 'e':
      Storage.push_back(0x1B);
      break;
    case ' ':
      Storage.push_back(0x20);
      break;
    case '"':
      Storage.push_back(0x22);
      break;
    case '/':
      Storage.push_back(0x2F);
      break;
    case '\\':
      Storage.push_back(0x5C);
      break;
    case 'N':
      encodeUTF8(0x85, Storage);
      break;
    case '_':
      encodeUTF8(0xA0, Storage);
      break;
    case 'L':
      encodeUTF8(0x2028, Storage);
      break;
    case 'P':
      encodeUTF8(0x2029, Storage);
      break;
    case 'x': {
      if (UnquotedValue.size() < 3)
        // TODO: Report error.
        break;
      unsigned int UnicodeScalarValue;
      if (UnquotedValue.substr(1, 2).getAsInteger(16, UnicodeScalarValue))
        // TODO: Report error.
        UnicodeScalarValue = 0xFFFD;
      encodeUTF8(UnicodeScalarValue, Storage);
      return UnquotedValue.drop_front(3);
    }
    case 'u': {
      if (UnquotedValue.size() < 5)
        // TODO: Report error.
        break;
      unsigned int UnicodeScalarValue;
      if (UnquotedValue.substr(1, 4).getAsInteger(16, UnicodeScalarValue))
        // TODO: Report error.
        UnicodeScalarValue = 0xFFFD;
      encodeUTF8(UnicodeScalarValue, Storage);
      return UnquotedValue.drop_front(5);
    }
    case 'U': {
      if (UnquotedValue.size() < 9)
        // TODO: Report error.
        break;
      unsigned int UnicodeScalarValue;
      if (UnquotedValue.substr(1, 8).getAsInteger(16, UnicodeScalarValue))
        // TODO: Report error.
        UnicodeScalarValue = 0xFFFD;
      encodeUTF8(UnicodeScalarValue, Storage);
      return UnquotedValue.drop_front(9);
    }
    }
    return UnquotedValue.drop_front(1);
  };

  return parseScalarValue(UnquotedValue, Storage, "\\\r\n", UnescapeFunc);
}

StringRef ScalarNode::getSingleQuotedValue(StringRef RawValue,
                                           SmallVectorImpl<char> &Storage) {
  assert(RawValue.size() >= 2 && RawValue.front() == '\'' &&
         RawValue.back() == '\'');
  StringRef UnquotedValue = RawValue.substr(1, RawValue.size() - 2);

  auto UnescapeFunc = [](StringRef UnquotedValue,
                         SmallVectorImpl<char> &Storage) {
    assert(UnquotedValue.take_front(2) == "''");
    Storage.push_back('\'');
    return UnquotedValue.drop_front(2);
  };

  return parseScalarValue(UnquotedValue, Storage, "'\r\n", UnescapeFunc);
}

StringRef ScalarNode::getPlainValue(StringRef RawValue,
                                    SmallVectorImpl<char> &Storage) {
  // Trim trailing whitespace ('b-char' and 's-white').
  // NOTE: Alternatively we could change the scanner to not include whitespace
  //       here in the first place.
  RawValue = RawValue.rtrim("\r\n \t");
  return parseScalarValue(RawValue, Storage, "\r\n", nullptr);
}

Node *KeyValueNode::getKey() {
  if (Key)
    return Key;
  // Handle implicit null keys.
  {
    Token &t = peekNext();
    if (   t.Kind == Token::TK_BlockEnd
        || t.Kind == Token::TK_Value
        || t.Kind == Token::TK_Error) {
      return Key = new (getAllocator()) NullNode(Doc);
    }
    if (t.Kind == Token::TK_Key)
      getNext(); // skip TK_Key.
  }

  // Handle explicit null keys.
  Token &t = peekNext();
  if (t.Kind == Token::TK_BlockEnd || t.Kind == Token::TK_Value) {
    return Key = new (getAllocator()) NullNode(Doc);
  }

  // We've got a normal key.
  return Key = parseBlockNode();
}

Node *KeyValueNode::getValue() {
  if (Value)
    return Value;

  if (Node* Key = getKey())
    Key->skip();
  else {
    setError("Null key in Key Value.", peekNext());
    return Value = new (getAllocator()) NullNode(Doc);
  }

  if (failed())
    return Value = new (getAllocator()) NullNode(Doc);

  // Handle implicit null values.
  {
    Token &t = peekNext();
    if (   t.Kind == Token::TK_BlockEnd
        || t.Kind == Token::TK_FlowMappingEnd
        || t.Kind == Token::TK_Key
        || t.Kind == Token::TK_FlowEntry
        || t.Kind == Token::TK_Error) {
      return Value = new (getAllocator()) NullNode(Doc);
    }

    if (t.Kind != Token::TK_Value) {
      setError("Unexpected token in Key Value.", t);
      return Value = new (getAllocator()) NullNode(Doc);
    }
    getNext(); // skip TK_Value.
  }

  // Handle explicit null values.
  Token &t = peekNext();
  if (t.Kind == Token::TK_BlockEnd || t.Kind == Token::TK_Key) {
    return Value = new (getAllocator()) NullNode(Doc);
  }

  // We got a normal value.
  return Value = parseBlockNode();
}

void MappingNode::increment() {
  if (failed()) {
    IsAtEnd = true;
    CurrentEntry = nullptr;
    return;
  }
  if (CurrentEntry) {
    CurrentEntry->skip();
    if (Type == MT_Inline) {
      IsAtEnd = true;
      CurrentEntry = nullptr;
      return;
    }
  }
  Token T = peekNext();
  if (T.Kind == Token::TK_Key || T.Kind == Token::TK_Scalar) {
    // KeyValueNode eats the TK_Key. That way it can detect null keys.
    CurrentEntry = new (getAllocator()) KeyValueNode(Doc);
  } else if (Type == MT_Block) {
    switch (T.Kind) {
    case Token::TK_BlockEnd:
      getNext();
      IsAtEnd = true;
      CurrentEntry = nullptr;
      break;
    default:
      setError("Unexpected token. Expected Key or Block End", T);
      [[fallthrough]];
    case Token::TK_Error:
      IsAtEnd = true;
      CurrentEntry = nullptr;
    }
  } else {
    switch (T.Kind) {
    case Token::TK_FlowEntry:
      // Eat the flow entry and recurse.
      getNext();
      return increment();
    case Token::TK_FlowMappingEnd:
      getNext();
      [[fallthrough]];
    case Token::TK_Error:
      // Set this to end iterator.
      IsAtEnd = true;
      CurrentEntry = nullptr;
      break;
    default:
      setError( "Unexpected token. Expected Key, Flow Entry, or Flow "
                "Mapping End."
              , T);
      IsAtEnd = true;
      CurrentEntry = nullptr;
    }
  }
}

void SequenceNode::increment() {
  if (failed()) {
    IsAtEnd = true;
    CurrentEntry = nullptr;
    return;
  }
  if (CurrentEntry)
    CurrentEntry->skip();
  Token T = peekNext();
  if (SeqType == ST_Block) {
    switch (T.Kind) {
    case Token::TK_BlockEntry:
      getNext();
      CurrentEntry = parseBlockNode();
      if (!CurrentEntry) { // An error occurred.
        IsAtEnd = true;
        CurrentEntry = nullptr;
      }
      break;
    case Token::TK_BlockEnd:
      getNext();
      IsAtEnd = true;
      CurrentEntry = nullptr;
      break;
    default:
      setError( "Unexpected token. Expected Block Entry or Block End."
              , T);
      [[fallthrough]];
    case Token::TK_Error:
      IsAtEnd = true;
      CurrentEntry = nullptr;
    }
  } else if (SeqType == ST_Indentless) {
    switch (T.Kind) {
    case Token::TK_BlockEntry:
      getNext();
      CurrentEntry = parseBlockNode();
      if (!CurrentEntry) { // An error occurred.
        IsAtEnd = true;
        CurrentEntry = nullptr;
      }
      break;
    default:
    case Token::TK_Error:
      IsAtEnd = true;
      CurrentEntry = nullptr;
    }
  } else if (SeqType == ST_Flow) {
    switch (T.Kind) {
    case Token::TK_FlowEntry:
      // Eat the flow entry and recurse.
      getNext();
      WasPreviousTokenFlowEntry = true;
      return increment();
    case Token::TK_FlowSequenceEnd:
      getNext();
      [[fallthrough]];
    case Token::TK_Error:
      // Set this to end iterator.
      IsAtEnd = true;
      CurrentEntry = nullptr;
      break;
    case Token::TK_StreamEnd:
    case Token::TK_DocumentEnd:
    case Token::TK_DocumentStart:
      setError("Could not find closing ]!", T);
      // Set this to end iterator.
      IsAtEnd = true;
      CurrentEntry = nullptr;
      break;
    default:
      if (!WasPreviousTokenFlowEntry) {
        setError("Expected , between entries!", T);
        IsAtEnd = true;
        CurrentEntry = nullptr;
        break;
      }
      // Otherwise it must be a flow entry.
      CurrentEntry = parseBlockNode();
      if (!CurrentEntry) {
        IsAtEnd = true;
      }
      WasPreviousTokenFlowEntry = false;
      break;
    }
  }
}

Document::Document(Stream &S) : stream(S), Root(nullptr) {
  // Tag maps starts with two default mappings.
  TagMap["!"] = "!";
  TagMap["!!"] = "tag:yaml.org,2002:";

  if (parseDirectives())
    expectToken(Token::TK_DocumentStart);
  Token &T = peekNext();
  if (T.Kind == Token::TK_DocumentStart)
    getNext();
}

bool Document::skip()  {
  if (stream.scanner->failed())
    return false;
  if (!Root && !getRoot())
    return false;
  Root->skip();
  Token &T = peekNext();
  if (T.Kind == Token::TK_StreamEnd)
    return false;
  if (T.Kind == Token::TK_DocumentEnd) {
    getNext();
    return skip();
  }
  return true;
}

Token &Document::peekNext() {
  return stream.scanner->peekNext();
}

Token Document::getNext() {
  return stream.scanner->getNext();
}

void Document::setError(const Twine &Message, Token &Location) const {
  stream.scanner->setError(Message, Location.Range.begin());
}

bool Document::failed() const {
  return stream.scanner->failed();
}

Node *Document::parseBlockNode() {
  Token T = peekNext();
  // Handle properties.
  Token AnchorInfo;
  Token TagInfo;
parse_property:
  switch (T.Kind) {
  case Token::TK_Alias:
    getNext();
    return new (NodeAllocator) AliasNode(stream.CurrentDoc, T.Range.substr(1));
  case Token::TK_Anchor:
    if (AnchorInfo.Kind == Token::TK_Anchor) {
      setError("Already encountered an anchor for this node!", T);
      return nullptr;
    }
    AnchorInfo = getNext(); // Consume TK_Anchor.
    T = peekNext();
    goto parse_property;
  case Token::TK_Tag:
    if (TagInfo.Kind == Token::TK_Tag) {
      setError("Already encountered a tag for this node!", T);
      return nullptr;
    }
    TagInfo = getNext(); // Consume TK_Tag.
    T = peekNext();
    goto parse_property;
  default:
    break;
  }

  switch (T.Kind) {
  case Token::TK_BlockEntry:
    // We got an unindented BlockEntry sequence. This is not terminated with
    // a BlockEnd.
    // Don't eat the TK_BlockEntry, SequenceNode needs it.
    return new (NodeAllocator) SequenceNode( stream.CurrentDoc
                                           , AnchorInfo.Range.substr(1)
                                           , TagInfo.Range
                                           , SequenceNode::ST_Indentless);
  case Token::TK_BlockSequenceStart:
    getNext();
    return new (NodeAllocator)
      SequenceNode( stream.CurrentDoc
                  , AnchorInfo.Range.substr(1)
                  , TagInfo.Range
                  , SequenceNode::ST_Block);
  case Token::TK_BlockMappingStart:
    getNext();
    return new (NodeAllocator)
      MappingNode( stream.CurrentDoc
                 , AnchorInfo.Range.substr(1)
                 , TagInfo.Range
                 , MappingNode::MT_Block);
  case Token::TK_FlowSequenceStart:
    getNext();
    return new (NodeAllocator)
      SequenceNode( stream.CurrentDoc
                  , AnchorInfo.Range.substr(1)
                  , TagInfo.Range
                  , SequenceNode::ST_Flow);
  case Token::TK_FlowMappingStart:
    getNext();
    return new (NodeAllocator)
      MappingNode( stream.CurrentDoc
                 , AnchorInfo.Range.substr(1)
                 , TagInfo.Range
                 , MappingNode::MT_Flow);
  case Token::TK_Scalar:
    getNext();
    return new (NodeAllocator)
      ScalarNode( stream.CurrentDoc
                , AnchorInfo.Range.substr(1)
                , TagInfo.Range
                , T.Range);
  case Token::TK_BlockScalar: {
    getNext();
    StringRef NullTerminatedStr(T.Value.c_str(), T.Value.length() + 1);
    StringRef StrCopy = NullTerminatedStr.copy(NodeAllocator).drop_back();
    return new (NodeAllocator)
        BlockScalarNode(stream.CurrentDoc, AnchorInfo.Range.substr(1),
                        TagInfo.Range, StrCopy, T.Range);
  }
  case Token::TK_Key:
    // Don't eat the TK_Key, KeyValueNode expects it.
    return new (NodeAllocator)
      MappingNode( stream.CurrentDoc
                 , AnchorInfo.Range.substr(1)
                 , TagInfo.Range
                 , MappingNode::MT_Inline);
  case Token::TK_DocumentStart:
  case Token::TK_DocumentEnd:
  case Token::TK_StreamEnd:
  default:
    // TODO: Properly handle tags. "[!!str ]" should resolve to !!str "", not
    //       !!null null.
    return new (NodeAllocator) NullNode(stream.CurrentDoc);
  case Token::TK_FlowMappingEnd:
  case Token::TK_FlowSequenceEnd:
  case Token::TK_FlowEntry: {
    if (Root && (isa<MappingNode>(Root) || isa<SequenceNode>(Root)))
      return new (NodeAllocator) NullNode(stream.CurrentDoc);

    setError("Unexpected token", T);
    return nullptr;
  }
  case Token::TK_Error:
    return nullptr;
  }
  llvm_unreachable("Control flow shouldn't reach here.");
  return nullptr;
}

bool Document::parseDirectives() {
  bool isDirective = false;
  while (true) {
    Token T = peekNext();
    if (T.Kind == Token::TK_TagDirective) {
      parseTAGDirective();
      isDirective = true;
    } else if (T.Kind == Token::TK_VersionDirective) {
      parseYAMLDirective();
      isDirective = true;
    } else
      break;
  }
  return isDirective;
}

void Document::parseYAMLDirective() {
  getNext(); // Eat %YAML <version>
}

void Document::parseTAGDirective() {
  Token Tag = getNext(); // %TAG <handle> <prefix>
  StringRef T = Tag.Range;
  // Strip %TAG
  T = T.substr(T.find_first_of(" \t")).ltrim(" \t");
  std::size_t HandleEnd = T.find_first_of(" \t");
  StringRef TagHandle = T.substr(0, HandleEnd);
  StringRef TagPrefix = T.substr(HandleEnd).ltrim(" \t");
  TagMap[TagHandle] = TagPrefix;
}

bool Document::expectToken(int TK) {
  Token T = getNext();
  if (T.Kind != TK) {
    setError("Unexpected token", T);
    return false;
  }
  return true;
}
