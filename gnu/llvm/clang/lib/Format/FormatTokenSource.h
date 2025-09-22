//===--- FormatTokenSource.h - Format C++ code ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the \c FormatTokenSource interface, which provides a token
/// stream as well as the ability to manipulate the token stream.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_FORMATTOKENSOURCE_H
#define LLVM_CLANG_LIB_FORMAT_FORMATTOKENSOURCE_H

#include "UnwrappedLineParser.h"

#define DEBUG_TYPE "format-token-source"

namespace clang {
namespace format {

// Navigate a token stream.
//
// Enables traversal of a token stream, resetting the position in a token
// stream, as well as inserting new tokens.
class FormatTokenSource {
public:
  virtual ~FormatTokenSource() {}

  // Returns the next token in the token stream.
  virtual FormatToken *getNextToken() = 0;

  // Returns the token preceding the token returned by the last call to
  // getNextToken() in the token stream, or nullptr if no such token exists.
  //
  // Must not be called directly at the position directly after insertTokens()
  // is called.
  virtual FormatToken *getPreviousToken() = 0;

  // Returns the token that would be returned by the next call to
  // getNextToken().
  virtual FormatToken *peekNextToken(bool SkipComment = false) = 0;

  // Returns whether we are at the end of the file.
  // This can be different from whether getNextToken() returned an eof token
  // when the FormatTokenSource is a view on a part of the token stream.
  virtual bool isEOF() = 0;

  // Gets the current position in the token stream, to be used by setPosition().
  //
  // Note that the value of the position is not meaningful, and specifically
  // should not be used to get relative token positions.
  virtual unsigned getPosition() = 0;

  // Resets the token stream to the state it was in when getPosition() returned
  // Position, and return the token at that position in the stream.
  virtual FormatToken *setPosition(unsigned Position) = 0;

  // Insert the given tokens before the current position.
  // Returns the first token in \c Tokens.
  // The next returned token will be the second token in \c Tokens.
  // Requires the last token in Tokens to be EOF; once the EOF token is reached,
  // the next token will be the last token returned by getNextToken();
  //
  // For example, given the token sequence 'a1 a2':
  // getNextToken() -> a1
  // insertTokens('b1 b2') -> b1
  // getNextToken() -> b2
  // getNextToken() -> a1
  // getNextToken() -> a2
  virtual FormatToken *insertTokens(ArrayRef<FormatToken *> Tokens) = 0;

  [[nodiscard]] FormatToken *getNextNonComment() {
    FormatToken *Tok;
    do {
      Tok = getNextToken();
      assert(Tok);
    } while (Tok->is(tok::comment));
    return Tok;
  }
};

class IndexedTokenSource : public FormatTokenSource {
public:
  IndexedTokenSource(ArrayRef<FormatToken *> Tokens)
      : Tokens(Tokens), Position(-1) {}

  FormatToken *getNextToken() override {
    if (Position >= 0 && isEOF()) {
      LLVM_DEBUG({
        llvm::dbgs() << "Next ";
        dbgToken(Position);
      });
      return Tokens[Position];
    }
    Position = successor(Position);
    LLVM_DEBUG({
      llvm::dbgs() << "Next ";
      dbgToken(Position);
    });
    return Tokens[Position];
  }

  FormatToken *getPreviousToken() override {
    assert(Position <= 0 || Tokens[Position - 1]->isNot(tok::eof));
    return Position > 0 ? Tokens[Position - 1] : nullptr;
  }

  FormatToken *peekNextToken(bool SkipComment = false) override {
    if (isEOF())
      return Tokens[Position];
    int Next = successor(Position);
    if (SkipComment)
      while (Tokens[Next]->is(tok::comment))
        Next = successor(Next);
    LLVM_DEBUG({
      llvm::dbgs() << "Peeking ";
      dbgToken(Next);
    });
    return Tokens[Next];
  }

  bool isEOF() override {
    return Position == -1 ? false : Tokens[Position]->is(tok::eof);
  }

  unsigned getPosition() override {
    LLVM_DEBUG(llvm::dbgs() << "Getting Position: " << Position << "\n");
    assert(Position >= 0);
    return Position;
  }

  FormatToken *setPosition(unsigned P) override {
    LLVM_DEBUG(llvm::dbgs() << "Setting Position: " << P << "\n");
    Position = P;
    return Tokens[Position];
  }

  FormatToken *insertTokens(ArrayRef<FormatToken *> New) override {
    assert(Position != -1);
    assert((*New.rbegin())->Tok.is(tok::eof));
    int Next = Tokens.size();
    Tokens.append(New.begin(), New.end());
    LLVM_DEBUG({
      llvm::dbgs() << "Inserting:\n";
      for (int I = Next, E = Tokens.size(); I != E; ++I)
        dbgToken(I, "  ");
      llvm::dbgs() << "  Jump from: " << (Tokens.size() - 1) << " -> "
                   << Position << "\n";
    });
    Jumps[Tokens.size() - 1] = Position;
    Position = Next;
    LLVM_DEBUG({
      llvm::dbgs() << "At inserted token ";
      dbgToken(Position);
    });
    return Tokens[Position];
  }

  void reset() { Position = -1; }

private:
  int successor(int Current) const {
    int Next = Current + 1;
    auto it = Jumps.find(Next);
    if (it != Jumps.end()) {
      Next = it->second;
      assert(!Jumps.contains(Next));
    }
    return Next;
  }

  void dbgToken(int Position, StringRef Indent = "") {
    FormatToken *Tok = Tokens[Position];
    llvm::dbgs() << Indent << "[" << Position
                 << "] Token: " << Tok->Tok.getName() << " / " << Tok->TokenText
                 << ", Macro: " << !!Tok->MacroCtx << "\n";
  }

  SmallVector<FormatToken *> Tokens;
  int Position;

  // Maps from position a to position b, so that when we reach a, the token
  // stream continues at position b instead.
  llvm::DenseMap<int, int> Jumps;
};

class ScopedMacroState : public FormatTokenSource {
public:
  ScopedMacroState(UnwrappedLine &Line, FormatTokenSource *&TokenSource,
                   FormatToken *&ResetToken)
      : Line(Line), TokenSource(TokenSource), ResetToken(ResetToken),
        PreviousLineLevel(Line.Level), PreviousTokenSource(TokenSource),
        Token(nullptr), PreviousToken(nullptr) {
    FakeEOF.Tok.startToken();
    FakeEOF.Tok.setKind(tok::eof);
    TokenSource = this;
    Line.Level = 0;
    Line.InPPDirective = true;
    // InMacroBody gets set after the `#define x` part.
  }

  ~ScopedMacroState() override {
    TokenSource = PreviousTokenSource;
    ResetToken = Token;
    Line.InPPDirective = false;
    Line.InMacroBody = false;
    Line.Level = PreviousLineLevel;
  }

  FormatToken *getNextToken() override {
    // The \c UnwrappedLineParser guards against this by never calling
    // \c getNextToken() after it has encountered the first eof token.
    assert(!eof());
    PreviousToken = Token;
    Token = PreviousTokenSource->getNextToken();
    if (eof())
      return &FakeEOF;
    return Token;
  }

  FormatToken *getPreviousToken() override {
    return PreviousTokenSource->getPreviousToken();
  }

  FormatToken *peekNextToken(bool SkipComment) override {
    if (eof())
      return &FakeEOF;
    return PreviousTokenSource->peekNextToken(SkipComment);
  }

  bool isEOF() override { return PreviousTokenSource->isEOF(); }

  unsigned getPosition() override { return PreviousTokenSource->getPosition(); }

  FormatToken *setPosition(unsigned Position) override {
    PreviousToken = nullptr;
    Token = PreviousTokenSource->setPosition(Position);
    return Token;
  }

  FormatToken *insertTokens(ArrayRef<FormatToken *> Tokens) override {
    llvm_unreachable("Cannot insert tokens while parsing a macro.");
    return nullptr;
  }

private:
  bool eof() {
    return Token && Token->HasUnescapedNewline &&
           !continuesLineComment(*Token, PreviousToken,
                                 /*MinColumnToken=*/PreviousToken);
  }

  FormatToken FakeEOF;
  UnwrappedLine &Line;
  FormatTokenSource *&TokenSource;
  FormatToken *&ResetToken;
  unsigned PreviousLineLevel;
  FormatTokenSource *PreviousTokenSource;

  FormatToken *Token;
  FormatToken *PreviousToken;
};

} // namespace format
} // namespace clang

#undef DEBUG_TYPE

#endif
