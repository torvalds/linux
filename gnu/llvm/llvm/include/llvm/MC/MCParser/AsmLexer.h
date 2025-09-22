//===- AsmLexer.h - Lexer for Assembly Files --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class declares the lexer for assembly files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_ASMLEXER_H
#define LLVM_MC_MCPARSER_ASMLEXER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include <string>

namespace llvm {

class MCAsmInfo;

/// AsmLexer - Lexer class for assembly files.
class AsmLexer : public MCAsmLexer {
  const MCAsmInfo &MAI;

  const char *CurPtr = nullptr;
  StringRef CurBuf;
  bool IsAtStartOfLine = true;
  bool IsAtStartOfStatement = true;
  bool IsPeeking = false;
  bool EndStatementAtEOF = true;

protected:
  /// LexToken - Read the next token and return its code.
  AsmToken LexToken() override;

public:
  AsmLexer(const MCAsmInfo &MAI);
  AsmLexer(const AsmLexer &) = delete;
  AsmLexer &operator=(const AsmLexer &) = delete;
  ~AsmLexer() override;

  void setBuffer(StringRef Buf, const char *ptr = nullptr,
                 bool EndStatementAtEOF = true);

  StringRef LexUntilEndOfStatement() override;

  size_t peekTokens(MutableArrayRef<AsmToken> Buf,
                    bool ShouldSkipSpace = true) override;

  const MCAsmInfo &getMAI() const { return MAI; }

private:
  bool isAtStartOfComment(const char *Ptr);
  bool isAtStatementSeparator(const char *Ptr);
  [[nodiscard]] int getNextChar();
  int peekNextChar();
  AsmToken ReturnError(const char *Loc, const std::string &Msg);

  AsmToken LexIdentifier();
  AsmToken LexSlash();
  AsmToken LexLineComment();
  AsmToken LexDigit();
  AsmToken LexSingleQuote();
  AsmToken LexQuote();
  AsmToken LexFloatLiteral();
  AsmToken LexHexFloatLiteral(bool NoIntDigits);

  StringRef LexUntilEndOfLine();
};

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_ASMLEXER_H
