//===- MCAsmParserExtension.cpp - Asm Parser Hooks ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCStreamer.h"

using namespace llvm;

MCAsmParserExtension::MCAsmParserExtension() = default;

MCAsmParserExtension::~MCAsmParserExtension() = default;

void MCAsmParserExtension::Initialize(MCAsmParser &Parser) {
  this->Parser = &Parser;
}

/// ParseDirectiveCGProfile
///  ::= .cg_profile identifier, identifier, <number>
bool MCAsmParserExtension::ParseDirectiveCGProfile(StringRef, SMLoc) {
  StringRef From;
  SMLoc FromLoc = getLexer().getLoc();
  if (getParser().parseIdentifier(From))
    return TokError("expected identifier in directive");

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("expected a comma");
  Lex();

  StringRef To;
  SMLoc ToLoc = getLexer().getLoc();
  if (getParser().parseIdentifier(To))
    return TokError("expected identifier in directive");

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("expected a comma");
  Lex();

  int64_t Count;
  if (getParser().parseIntToken(
          Count, "expected integer count in '.cg_profile' directive"))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  MCSymbol *FromSym = getContext().getOrCreateSymbol(From);
  MCSymbol *ToSym = getContext().getOrCreateSymbol(To);

  getStreamer().emitCGProfileEntry(
      MCSymbolRefExpr::create(FromSym, MCSymbolRefExpr::VK_None, getContext(),
                              FromLoc),
      MCSymbolRefExpr::create(ToSym, MCSymbolRefExpr::VK_None, getContext(),
                              ToLoc),
      Count);
  return false;
}
