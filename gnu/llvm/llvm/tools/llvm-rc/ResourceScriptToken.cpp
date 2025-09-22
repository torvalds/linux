//===-- ResourceScriptToken.cpp ---------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file implements an interface defined in ResourceScriptToken.h.
// In particular, it defines an .rc script tokenizer.
//
//===---------------------------------------------------------------------===//

#include "ResourceScriptToken.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <utility>

using namespace llvm;

using Kind = RCToken::Kind;

// Checks if Representation is a correct description of an RC integer.
// It should be a 32-bit unsigned integer, either decimal, octal (0[0-7]+),
// or hexadecimal (0x[0-9a-f]+). It might be followed by a single 'L'
// character (that is the difference between our representation and
// StringRef's one). If Representation is correct, 'true' is returned and
// the return value is put back in Num.
static bool rcGetAsInteger(StringRef Representation, uint32_t &Num) {
  size_t Length = Representation.size();
  if (Length == 0)
    return false;
  // Strip the last 'L' if unnecessary.
  if (std::toupper(Representation.back()) == 'L')
    Representation = Representation.drop_back(1);

  return !Representation.getAsInteger<uint32_t>(0, Num);
}

RCToken::RCToken(RCToken::Kind RCTokenKind, StringRef Value)
    : TokenKind(RCTokenKind), TokenValue(Value) {}

uint32_t RCToken::intValue() const {
  assert(TokenKind == Kind::Int);
  // We assume that the token already is a correct integer (checked by
  // rcGetAsInteger).
  uint32_t Result;
  bool IsSuccess = rcGetAsInteger(TokenValue, Result);
  assert(IsSuccess);
  (void)IsSuccess;  // Silence the compiler warning when -DNDEBUG flag is on.
  return Result;
}

bool RCToken::isLongInt() const {
  return TokenKind == Kind::Int && std::toupper(TokenValue.back()) == 'L';
}

StringRef RCToken::value() const { return TokenValue; }

Kind RCToken::kind() const { return TokenKind; }

bool RCToken::isBinaryOp() const {
  switch (TokenKind) {
  case Kind::Plus:
  case Kind::Minus:
  case Kind::Pipe:
  case Kind::Amp:
    return true;
  default:
    return false;
  }
}

static Error getStringError(const Twine &message) {
  return make_error<StringError>("Error parsing file: " + message,
                                 inconvertibleErrorCode());
}

namespace {

class Tokenizer {
public:
  Tokenizer(StringRef Input) : Data(Input), DataLength(Input.size()), Pos(0) {}

  Expected<std::vector<RCToken>> run();

private:
  // All 'advancing' methods return boolean values; if they're equal to false,
  // the stream has ended or failed.
  bool advance(size_t Amount = 1);
  bool skipWhitespaces();

  // Consumes a token. If any problem occurred, a non-empty Error is returned.
  Error consumeToken(const Kind TokenKind);

  // Check if tokenizer is about to read FollowingChars.
  bool willNowRead(StringRef FollowingChars) const;

  // Check if tokenizer can start reading an identifier at current position.
  // The original tool did non specify the rules to determine what is a correct
  // identifier. We assume they should follow the C convention:
  // [a-zA-Z_][a-zA-Z0-9_]*.
  bool canStartIdentifier() const;
  // Check if tokenizer can continue reading an identifier.
  bool canContinueIdentifier() const;

  // Check if tokenizer can start reading an integer.
  // A correct integer always starts with a 0-9 digit,
  // can contain characters 0-9A-Fa-f (digits),
  // Ll (marking the integer is 32-bit), Xx (marking the representation
  // is hexadecimal). As some kind of separator should come after the
  // integer, we can consume the integer until a non-alphanumeric
  // character.
  bool canStartInt() const;
  bool canContinueInt() const;

  bool canStartString() const;

  // Check if tokenizer can start reading a single line comment (e.g. a comment
  // that begins with '//')
  bool canStartLineComment() const;

  // Check if tokenizer can start or finish reading a block comment (e.g. a
  // comment that begins with '/*' and ends with '*/')
  bool canStartBlockComment() const;

  // Throw away all remaining characters on the current line.
  void skipCurrentLine();

  bool streamEof() const;

  // Classify the token that is about to be read from the current position.
  Kind classifyCurrentToken() const;

  // Process the Kind::Identifier token - check if it is
  // an identifier describing a block start or end.
  void processIdentifier(RCToken &token) const;

  StringRef Data;
  size_t DataLength, Pos;
};

void Tokenizer::skipCurrentLine() {
  Pos = Data.find_first_of("\r\n", Pos);
  Pos = Data.find_first_not_of("\r\n", Pos);

  if (Pos == StringRef::npos)
    Pos = DataLength;
}

Expected<std::vector<RCToken>> Tokenizer::run() {
  Pos = 0;
  std::vector<RCToken> Result;

  // Consume an optional UTF-8 Byte Order Mark.
  if (willNowRead("\xef\xbb\xbf"))
    advance(3);

  while (!streamEof()) {
    if (!skipWhitespaces())
      break;

    Kind TokenKind = classifyCurrentToken();
    if (TokenKind == Kind::Invalid)
      return getStringError("Invalid token found at position " + Twine(Pos));

    const size_t TokenStart = Pos;
    if (Error TokenError = consumeToken(TokenKind))
      return std::move(TokenError);

    // Comments are just deleted, don't bother saving them.
    if (TokenKind == Kind::LineComment || TokenKind == Kind::StartComment)
      continue;

    RCToken Token(TokenKind, Data.take_front(Pos).drop_front(TokenStart));
    if (TokenKind == Kind::Identifier) {
      processIdentifier(Token);
    } else if (TokenKind == Kind::Int) {
      uint32_t TokenInt;
      if (!rcGetAsInteger(Token.value(), TokenInt)) {
        // The integer has incorrect format or cannot be represented in
        // a 32-bit integer.
        return getStringError("Integer invalid or too large: " +
                              Token.value().str());
      }
    }

    Result.push_back(Token);
  }

  return Result;
}

bool Tokenizer::advance(size_t Amount) {
  Pos += Amount;
  return !streamEof();
}

bool Tokenizer::skipWhitespaces() {
  while (!streamEof() && isSpace(Data[Pos]))
    advance();
  return !streamEof();
}

Error Tokenizer::consumeToken(const Kind TokenKind) {
  switch (TokenKind) {
  // One-character token consumption.
#define TOKEN(Name)
#define SHORT_TOKEN(Name, Ch) case Kind::Name:
#include "ResourceScriptTokenList.def"
    advance();
    return Error::success();

  case Kind::LineComment:
    advance(2);
    skipCurrentLine();
    return Error::success();

  case Kind::StartComment: {
    advance(2);
    auto EndPos = Data.find("*/", Pos);
    if (EndPos == StringRef::npos)
      return getStringError(
          "Unclosed multi-line comment beginning at position " + Twine(Pos));
    advance(EndPos - Pos);
    advance(2);
    return Error::success();
  }
  case Kind::Identifier:
    while (!streamEof() && canContinueIdentifier())
      advance();
    return Error::success();

  case Kind::Int:
    while (!streamEof() && canContinueInt())
      advance();
    return Error::success();

  case Kind::String:
    // Consume the preceding 'L', if there is any.
    if (std::toupper(Data[Pos]) == 'L')
      advance();
    // Consume the double-quote.
    advance();

    // Consume the characters until the end of the file, line or string.
    while (true) {
      if (streamEof()) {
        return getStringError("Unterminated string literal.");
      } else if (Data[Pos] == '"') {
        // Consume the ending double-quote.
        advance();
        // However, if another '"' follows this double-quote, the string didn't
        // end and we just included '"' into the string.
        if (!willNowRead("\""))
          return Error::success();
      } else if (Data[Pos] == '\n') {
        return getStringError("String literal not terminated in the line.");
      }

      advance();
    }

  case Kind::Invalid:
    assert(false && "Cannot consume an invalid token.");
  }

  llvm_unreachable("Unknown RCToken::Kind");
}

bool Tokenizer::willNowRead(StringRef FollowingChars) const {
  return Data.drop_front(Pos).starts_with(FollowingChars);
}

bool Tokenizer::canStartIdentifier() const {
  assert(!streamEof());

  const char CurChar = Data[Pos];
  return std::isalpha(CurChar) || CurChar == '_' || CurChar == '.';
}

bool Tokenizer::canContinueIdentifier() const {
  assert(!streamEof());
  const char CurChar = Data[Pos];
  return std::isalnum(CurChar) || CurChar == '_' || CurChar == '.' ||
         CurChar == '/' || CurChar == '\\' || CurChar == '-';
}

bool Tokenizer::canStartInt() const {
  assert(!streamEof());
  return std::isdigit(Data[Pos]);
}

bool Tokenizer::canStartBlockComment() const {
  assert(!streamEof());
  return Data.drop_front(Pos).starts_with("/*");
}

bool Tokenizer::canStartLineComment() const {
  assert(!streamEof());
  return Data.drop_front(Pos).starts_with("//");
}

bool Tokenizer::canContinueInt() const {
  assert(!streamEof());
  return std::isalnum(Data[Pos]);
}

bool Tokenizer::canStartString() const {
  return willNowRead("\"") || willNowRead("L\"") || willNowRead("l\"");
}

bool Tokenizer::streamEof() const { return Pos == DataLength; }

Kind Tokenizer::classifyCurrentToken() const {
  if (canStartBlockComment())
    return Kind::StartComment;
  if (canStartLineComment())
    return Kind::LineComment;

  if (canStartInt())
    return Kind::Int;
  if (canStartString())
    return Kind::String;
  // BEGIN and END are at this point of lexing recognized as identifiers.
  if (canStartIdentifier())
    return Kind::Identifier;

  const char CurChar = Data[Pos];

  switch (CurChar) {
  // One-character token classification.
#define TOKEN(Name)
#define SHORT_TOKEN(Name, Ch)                                                  \
  case Ch:                                                                     \
    return Kind::Name;
#include "ResourceScriptTokenList.def"

  default:
    return Kind::Invalid;
  }
}

void Tokenizer::processIdentifier(RCToken &Token) const {
  assert(Token.kind() == Kind::Identifier);
  StringRef Name = Token.value();

  if (Name.equals_insensitive("begin"))
    Token = RCToken(Kind::BlockBegin, Name);
  else if (Name.equals_insensitive("end"))
    Token = RCToken(Kind::BlockEnd, Name);
}

} // anonymous namespace

namespace llvm {

Expected<std::vector<RCToken>> tokenizeRC(StringRef Input) {
  return Tokenizer(Input).run();
}

} // namespace llvm
