//===- LLLexer.h - Lexer for LLVM Assembly Files ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class represents the Lexer for .ll files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_LLLEXER_H
#define LLVM_ASMPARSER_LLLEXER_H

#include "LLToken.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/SMLoc.h"
#include <string>

namespace llvm {
  class Type;
  class SMDiagnostic;
  class SourceMgr;
  class LLVMContext;

  class LLLexer {
    const char *CurPtr;
    StringRef CurBuf;
    SMDiagnostic &ErrorInfo;
    SourceMgr &SM;
    LLVMContext &Context;

    // Information about the current token.
    const char *TokStart;
    lltok::Kind CurKind;
    std::string StrVal;
    unsigned UIntVal = 0;
    Type *TyVal = nullptr;
    APFloat APFloatVal{0.0};
    APSInt APSIntVal{0};

    // When false (default), an identifier ending in ':' is a label token.
    // When true, the ':' is treated as a separate token.
    bool IgnoreColonInIdentifiers = false;

  public:
    explicit LLLexer(StringRef StartBuf, SourceMgr &SM, SMDiagnostic &,
                     LLVMContext &C);

    lltok::Kind Lex() {
      return CurKind = LexToken();
    }

    typedef SMLoc LocTy;
    LocTy getLoc() const { return SMLoc::getFromPointer(TokStart); }
    lltok::Kind getKind() const { return CurKind; }
    const std::string &getStrVal() const { return StrVal; }
    Type *getTyVal() const { return TyVal; }
    unsigned getUIntVal() const { return UIntVal; }
    const APSInt &getAPSIntVal() const { return APSIntVal; }
    const APFloat &getAPFloatVal() const { return APFloatVal; }

    void setIgnoreColonInIdentifiers(bool val) {
      IgnoreColonInIdentifiers = val;
    }

    bool Error(LocTy ErrorLoc, const Twine &Msg) const;
    bool Error(const Twine &Msg) const { return Error(getLoc(), Msg); }

    void Warning(LocTy WarningLoc, const Twine &Msg) const;
    void Warning(const Twine &Msg) const { return Warning(getLoc(), Msg); }

  private:
    lltok::Kind LexToken();

    int getNextChar();
    void SkipLineComment();
    lltok::Kind ReadString(lltok::Kind kind);
    bool ReadVarName();

    lltok::Kind LexIdentifier();
    lltok::Kind LexDigitOrNegative();
    lltok::Kind LexPositive();
    lltok::Kind LexAt();
    lltok::Kind LexDollar();
    lltok::Kind LexExclaim();
    lltok::Kind LexPercent();
    lltok::Kind LexUIntID(lltok::Kind Token);
    lltok::Kind LexVar(lltok::Kind Var, lltok::Kind VarID);
    lltok::Kind LexQuote();
    lltok::Kind Lex0x();
    lltok::Kind LexHash();
    lltok::Kind LexCaret();

    uint64_t atoull(const char *Buffer, const char *End);
    uint64_t HexIntToVal(const char *Buffer, const char *End);
    void HexToIntPair(const char *Buffer, const char *End, uint64_t Pair[2]);
    void FP80HexToIntPair(const char *Buffer, const char *End, uint64_t Pair[2]);
  };
} // end namespace llvm

#endif
