//===- WasmAsmParser.cpp - Wasm Assembly Parser -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// --
//
// Note, this is for wasm, the binary format (analogous to ELF), not wasm,
// the instruction set (analogous to x86), for which parsing code lives in
// WebAssemblyAsmParser.
//
// This file contains processing for generic directives implemented using
// MCTargetStreamer, the ones that depend on WebAssemblyTargetStreamer are in
// WebAssemblyAsmParser.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/Support/Casting.h"
#include <optional>

using namespace llvm;

namespace {

class WasmAsmParser : public MCAsmParserExtension {
  MCAsmParser *Parser = nullptr;
  MCAsmLexer *Lexer = nullptr;

  template<bool (WasmAsmParser::*HandlerMethod)(StringRef, SMLoc)>
  void addDirectiveHandler(StringRef Directive) {
    MCAsmParser::ExtensionDirectiveHandler Handler = std::make_pair(
        this, HandleDirective<WasmAsmParser, HandlerMethod>);

    getParser().addDirectiveHandler(Directive, Handler);
  }

public:
  WasmAsmParser() { BracketExpressionsSupported = true; }

  void Initialize(MCAsmParser &P) override {
    Parser = &P;
    Lexer = &Parser->getLexer();
    // Call the base implementation.
    this->MCAsmParserExtension::Initialize(*Parser);

    addDirectiveHandler<&WasmAsmParser::parseSectionDirectiveText>(".text");
    addDirectiveHandler<&WasmAsmParser::parseSectionDirectiveData>(".data");
    addDirectiveHandler<&WasmAsmParser::parseSectionDirective>(".section");
    addDirectiveHandler<&WasmAsmParser::parseDirectiveSize>(".size");
    addDirectiveHandler<&WasmAsmParser::parseDirectiveType>(".type");
    addDirectiveHandler<&WasmAsmParser::ParseDirectiveIdent>(".ident");
    addDirectiveHandler<
      &WasmAsmParser::ParseDirectiveSymbolAttribute>(".weak");
    addDirectiveHandler<
      &WasmAsmParser::ParseDirectiveSymbolAttribute>(".local");
    addDirectiveHandler<
      &WasmAsmParser::ParseDirectiveSymbolAttribute>(".internal");
    addDirectiveHandler<
      &WasmAsmParser::ParseDirectiveSymbolAttribute>(".hidden");
  }

  bool error(const StringRef &Msg, const AsmToken &Tok) {
    return Parser->Error(Tok.getLoc(), Msg + Tok.getString());
  }

  bool isNext(AsmToken::TokenKind Kind) {
    auto Ok = Lexer->is(Kind);
    if (Ok)
      Lex();
    return Ok;
  }

  bool expect(AsmToken::TokenKind Kind, const char *KindName) {
    if (!isNext(Kind))
      return error(std::string("Expected ") + KindName + ", instead got: ",
                   Lexer->getTok());
    return false;
  }

  bool parseSectionDirectiveText(StringRef, SMLoc) {
    // FIXME: .text currently no-op.
    return false;
  }

  bool parseSectionDirectiveData(StringRef, SMLoc) {
    auto *S = getContext().getObjectFileInfo()->getDataSection();
    getStreamer().switchSection(S);
    return false;
  }

  uint32_t parseSectionFlags(StringRef FlagStr, bool &Passive, bool &Group) {
    uint32_t flags = 0;
    for (char C : FlagStr) {
      switch (C) {
      case 'p':
        Passive = true;
        break;
      case 'G':
        Group = true;
        break;
      case 'T':
        flags |= wasm::WASM_SEG_FLAG_TLS;
        break;
      case 'S':
        flags |= wasm::WASM_SEG_FLAG_STRINGS;
        break;
      case 'R':
        flags |= wasm::WASM_SEG_FLAG_RETAIN;
        break;
      default:
        return -1U;
      }
    }
    return flags;
  }

  bool parseGroup(StringRef &GroupName) {
    if (Lexer->isNot(AsmToken::Comma))
      return TokError("expected group name");
    Lex();
    if (Lexer->is(AsmToken::Integer)) {
      GroupName = getTok().getString();
      Lex();
    } else if (Parser->parseIdentifier(GroupName)) {
      return TokError("invalid group name");
    }
    if (Lexer->is(AsmToken::Comma)) {
      Lex();
      StringRef Linkage;
      if (Parser->parseIdentifier(Linkage))
        return TokError("invalid linkage");
      if (Linkage != "comdat")
        return TokError("Linkage must be 'comdat'");
    }
    return false;
  }

  bool parseSectionDirective(StringRef, SMLoc loc) {
    StringRef Name;
    if (Parser->parseIdentifier(Name))
      return TokError("expected identifier in directive");

    if (expect(AsmToken::Comma, ","))
      return true;

    if (Lexer->isNot(AsmToken::String))
      return error("expected string in directive, instead got: ", Lexer->getTok());

    auto Kind = StringSwitch<std::optional<SectionKind>>(Name)
                    .StartsWith(".data", SectionKind::getData())
                    .StartsWith(".tdata", SectionKind::getThreadData())
                    .StartsWith(".tbss", SectionKind::getThreadBSS())
                    .StartsWith(".rodata", SectionKind::getReadOnly())
                    .StartsWith(".text", SectionKind::getText())
                    .StartsWith(".custom_section", SectionKind::getMetadata())
                    .StartsWith(".bss", SectionKind::getBSS())
                    // See use of .init_array in WasmObjectWriter and
                    // TargetLoweringObjectFileWasm
                    .StartsWith(".init_array", SectionKind::getData())
                    .StartsWith(".debug_", SectionKind::getMetadata())
                    .Default(SectionKind::getData());

    // Update section flags if present in this .section directive
    bool Passive = false;
    bool Group = false;
    uint32_t Flags =
        parseSectionFlags(getTok().getStringContents(), Passive, Group);
    if (Flags == -1U)
      return TokError("unknown flag");

    Lex();

    if (expect(AsmToken::Comma, ",") || expect(AsmToken::At, "@"))
      return true;

    StringRef GroupName;
    if (Group && parseGroup(GroupName))
      return true;

    if (expect(AsmToken::EndOfStatement, "eol"))
      return true;

    // TODO: Parse UniqueID
    MCSectionWasm *WS = getContext().getWasmSection(
        Name, *Kind, Flags, GroupName, MCContext::GenericSectionID);

    if (WS->getSegmentFlags() != Flags)
      Parser->Error(loc, "changed section flags for " + Name +
                             ", expected: 0x" +
                             utohexstr(WS->getSegmentFlags()));

    if (Passive) {
      if (!WS->isWasmData())
        return Parser->Error(loc, "Only data sections can be passive");
      WS->setPassive();
    }

    getStreamer().switchSection(WS);
    return false;
  }

  // TODO: This function is almost the same as ELFAsmParser::ParseDirectiveSize
  // so maybe could be shared somehow.
  bool parseDirectiveSize(StringRef, SMLoc Loc) {
    StringRef Name;
    if (Parser->parseIdentifier(Name))
      return TokError("expected identifier in directive");
    auto Sym = getContext().getOrCreateSymbol(Name);
    if (expect(AsmToken::Comma, ","))
      return true;
    const MCExpr *Expr;
    if (Parser->parseExpression(Expr))
      return true;
    if (expect(AsmToken::EndOfStatement, "eol"))
      return true;
    auto WasmSym = cast<MCSymbolWasm>(Sym);
    if (WasmSym->isFunction()) {
      // Ignore .size directives for function symbols.  They get their size
      // set automatically based on their content.
      Warning(Loc, ".size directive ignored for function symbols");
    } else {
      getStreamer().emitELFSize(Sym, Expr);
    }
    return false;
  }

  bool parseDirectiveType(StringRef, SMLoc) {
    // This could be the start of a function, check if followed by
    // "label,@function"
    if (!Lexer->is(AsmToken::Identifier))
      return error("Expected label after .type directive, got: ",
                   Lexer->getTok());
    auto WasmSym = cast<MCSymbolWasm>(
                     getStreamer().getContext().getOrCreateSymbol(
                       Lexer->getTok().getString()));
    Lex();
    if (!(isNext(AsmToken::Comma) && isNext(AsmToken::At) &&
          Lexer->is(AsmToken::Identifier)))
      return error("Expected label,@type declaration, got: ", Lexer->getTok());
    auto TypeName = Lexer->getTok().getString();
    if (TypeName == "function") {
      WasmSym->setType(wasm::WASM_SYMBOL_TYPE_FUNCTION);
      auto *Current =
          cast<MCSectionWasm>(getStreamer().getCurrentSectionOnly());
      if (Current->getGroup())
        WasmSym->setComdat(true);
    } else if (TypeName == "global")
      WasmSym->setType(wasm::WASM_SYMBOL_TYPE_GLOBAL);
    else if (TypeName == "object")
      WasmSym->setType(wasm::WASM_SYMBOL_TYPE_DATA);
    else
      return error("Unknown WASM symbol type: ", Lexer->getTok());
    Lex();
    return expect(AsmToken::EndOfStatement, "EOL");
  }

  // FIXME: Shared with ELF.
  /// ParseDirectiveIdent
  ///  ::= .ident string
  bool ParseDirectiveIdent(StringRef, SMLoc) {
    if (getLexer().isNot(AsmToken::String))
      return TokError("unexpected token in '.ident' directive");
    StringRef Data = getTok().getIdentifier();
    Lex();
    if (getLexer().isNot(AsmToken::EndOfStatement))
      return TokError("unexpected token in '.ident' directive");
    Lex();
    getStreamer().emitIdent(Data);
    return false;
  }

  // FIXME: Shared with ELF.
  /// ParseDirectiveSymbolAttribute
  ///  ::= { ".local", ".weak", ... } [ identifier ( , identifier )* ]
  bool ParseDirectiveSymbolAttribute(StringRef Directive, SMLoc) {
    MCSymbolAttr Attr = StringSwitch<MCSymbolAttr>(Directive)
      .Case(".weak", MCSA_Weak)
      .Case(".local", MCSA_Local)
      .Case(".hidden", MCSA_Hidden)
      .Case(".internal", MCSA_Internal)
      .Case(".protected", MCSA_Protected)
      .Default(MCSA_Invalid);
    assert(Attr != MCSA_Invalid && "unexpected symbol attribute directive!");
    if (getLexer().isNot(AsmToken::EndOfStatement)) {
      while (true) {
        StringRef Name;
        if (getParser().parseIdentifier(Name))
          return TokError("expected identifier in directive");
        MCSymbol *Sym = getContext().getOrCreateSymbol(Name);
        getStreamer().emitSymbolAttribute(Sym, Attr);
        if (getLexer().is(AsmToken::EndOfStatement))
          break;
        if (getLexer().isNot(AsmToken::Comma))
          return TokError("unexpected token in directive");
        Lex();
      }
    }
    Lex();
    return false;
  }
};

} // end anonymous namespace

namespace llvm {

MCAsmParserExtension *createWasmAsmParser() {
  return new WasmAsmParser;
}

} // end namespace llvm
