//===- COFFMasmParser.cpp - COFF MASM Assembly Parser ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCAsmMacro.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolCOFF.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/SMLoc.h"
#include <cstdint>
#include <utility>

using namespace llvm;

namespace {

class COFFMasmParser : public MCAsmParserExtension {
  template <bool (COFFMasmParser::*HandlerMethod)(StringRef, SMLoc)>
  void addDirectiveHandler(StringRef Directive) {
    MCAsmParser::ExtensionDirectiveHandler Handler =
        std::make_pair(this, HandleDirective<COFFMasmParser, HandlerMethod>);
    getParser().addDirectiveHandler(Directive, Handler);
  }

  bool ParseSectionSwitch(StringRef SectionName, unsigned Characteristics);

  bool ParseSectionSwitch(StringRef SectionName, unsigned Characteristics,
                          StringRef COMDATSymName, COFF::COMDATType Type,
                          Align Alignment);

  bool ParseDirectiveProc(StringRef, SMLoc);
  bool ParseDirectiveEndProc(StringRef, SMLoc);
  bool ParseDirectiveSegment(StringRef, SMLoc);
  bool ParseDirectiveSegmentEnd(StringRef, SMLoc);
  bool ParseDirectiveIncludelib(StringRef, SMLoc);
  bool ParseDirectiveOption(StringRef, SMLoc);

  bool ParseDirectiveAlias(StringRef, SMLoc);

  bool ParseSEHDirectiveAllocStack(StringRef, SMLoc);
  bool ParseSEHDirectiveEndProlog(StringRef, SMLoc);

  bool IgnoreDirective(StringRef, SMLoc) {
    while (!getLexer().is(AsmToken::EndOfStatement)) {
      Lex();
    }
    return false;
  }

  void Initialize(MCAsmParser &Parser) override {
    // Call the base implementation.
    MCAsmParserExtension::Initialize(Parser);

    // x64 directives
    addDirectiveHandler<&COFFMasmParser::ParseSEHDirectiveAllocStack>(
        ".allocstack");
    addDirectiveHandler<&COFFMasmParser::ParseSEHDirectiveEndProlog>(
        ".endprolog");

    // Code label directives
    // label
    // org

    // Conditional control flow directives
    // .break
    // .continue
    // .else
    // .elseif
    // .endif
    // .endw
    // .if
    // .repeat
    // .until
    // .untilcxz
    // .while

    // Data allocation directives
    // align
    // even
    // mmword
    // tbyte
    // xmmword
    // ymmword

    // Listing control directives
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".cref");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".list");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".listall");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".listif");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".listmacro");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".listmacroall");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".nocref");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".nolist");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".nolistif");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".nolistmacro");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>("page");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>("subtitle");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".tfcond");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>("title");

    // Macro directives
    // goto

    // Miscellaneous directives
    addDirectiveHandler<&COFFMasmParser::ParseDirectiveAlias>("alias");
    // assume
    // .fpo
    addDirectiveHandler<&COFFMasmParser::ParseDirectiveIncludelib>(
        "includelib");
    addDirectiveHandler<&COFFMasmParser::ParseDirectiveOption>("option");
    // popcontext
    // pushcontext
    // .safeseh

    // Procedure directives
    addDirectiveHandler<&COFFMasmParser::ParseDirectiveEndProc>("endp");
    // invoke (32-bit only)
    addDirectiveHandler<&COFFMasmParser::ParseDirectiveProc>("proc");
    // proto

    // Processor directives; all ignored
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".386");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".386p");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".387");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".486");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".486p");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".586");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".586p");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".686");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".686p");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".k3d");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".mmx");
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".xmm");

    // Scope directives
    // comm
    // externdef

    // Segment directives
    // .alpha (32-bit only, order segments alphabetically)
    // .dosseg (32-bit only, order segments in DOS convention)
    // .seq (32-bit only, order segments sequentially)
    addDirectiveHandler<&COFFMasmParser::ParseDirectiveSegmentEnd>("ends");
    // group (32-bit only)
    addDirectiveHandler<&COFFMasmParser::ParseDirectiveSegment>("segment");

    // Simplified segment directives
    addDirectiveHandler<&COFFMasmParser::ParseSectionDirectiveCode>(".code");
    // .const
    addDirectiveHandler<
        &COFFMasmParser::ParseSectionDirectiveInitializedData>(".data");
    addDirectiveHandler<
        &COFFMasmParser::ParseSectionDirectiveUninitializedData>(".data?");
    // .exit
    // .fardata
    // .fardata?
    addDirectiveHandler<&COFFMasmParser::IgnoreDirective>(".model");
    // .stack
    // .startup

    // String directives, written <name> <directive> <params>
    // catstr (equivalent to <name> TEXTEQU <params>)
    // instr (equivalent to <name> = @InStr(<params>))
    // sizestr (equivalent to <name> = @SizeStr(<params>))
    // substr (equivalent to <name> TEXTEQU @SubStr(<params>))

    // Structure and record directives
    // record
    // typedef
  }

  bool ParseSectionDirectiveCode(StringRef, SMLoc) {
    return ParseSectionSwitch(".text", COFF::IMAGE_SCN_CNT_CODE |
                                           COFF::IMAGE_SCN_MEM_EXECUTE |
                                           COFF::IMAGE_SCN_MEM_READ);
  }

  bool ParseSectionDirectiveInitializedData(StringRef, SMLoc) {
    return ParseSectionSwitch(".data", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                           COFF::IMAGE_SCN_MEM_READ |
                                           COFF::IMAGE_SCN_MEM_WRITE);
  }

  bool ParseSectionDirectiveUninitializedData(StringRef, SMLoc) {
    return ParseSectionSwitch(".bss", COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA |
                                          COFF::IMAGE_SCN_MEM_READ |
                                          COFF::IMAGE_SCN_MEM_WRITE);
  }

  /// Stack of active procedure definitions.
  SmallVector<StringRef, 1> CurrentProcedures;
  SmallVector<bool, 1> CurrentProceduresFramed;

public:
  COFFMasmParser() = default;
};

} // end anonymous namespace.

bool COFFMasmParser::ParseSectionSwitch(StringRef SectionName,
                                        unsigned Characteristics) {
  return ParseSectionSwitch(SectionName, Characteristics, "",
                            (COFF::COMDATType)0, Align(16));
}

bool COFFMasmParser::ParseSectionSwitch(StringRef SectionName,
                                        unsigned Characteristics,
                                        StringRef COMDATSymName,
                                        COFF::COMDATType Type,
                                        Align Alignment) {
  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in section switching directive");
  Lex();

  MCSection *Section = getContext().getCOFFSection(SectionName, Characteristics,
                                                   COMDATSymName, Type);
  Section->setAlignment(Alignment);
  getStreamer().switchSection(Section);

  return false;
}

bool COFFMasmParser::ParseDirectiveSegment(StringRef Directive, SMLoc Loc) {
  StringRef SegmentName;
  if (!getLexer().is(AsmToken::Identifier))
    return TokError("expected identifier in directive");
  SegmentName = getTok().getIdentifier();
  Lex();

  StringRef SectionName = SegmentName;
  SmallVector<char, 247> SectionNameVector;

  StringRef Class;
  if (SegmentName == "_TEXT" || SegmentName.starts_with("_TEXT$")) {
    if (SegmentName.size() == 5) {
      SectionName = ".text";
    } else {
      SectionName =
          (".text$" + SegmentName.substr(6)).toStringRef(SectionNameVector);
    }
    Class = "CODE";
  }

  // Parse all options to end of statement.
  // Alignment defaults to PARA if unspecified.
  int64_t Alignment = 16;
  // Default flags are used only if no characteristics are set.
  bool DefaultCharacteristics = true;
  unsigned Flags = 0;
  // "obsolete" according to the documentation, but still supported.
  bool Readonly = false;
  while (getLexer().isNot(AsmToken::EndOfStatement)) {
    switch (getTok().getKind()) {
    default:
      break;
    case AsmToken::String: {
      // Class identifier; overrides Kind.
      Class = getTok().getStringContents();
      Lex();
      break;
    }
    case AsmToken::Identifier: {
      SMLoc KeywordLoc = getTok().getLoc();
      StringRef Keyword;
      if (getParser().parseIdentifier(Keyword)) {
        llvm_unreachable("failed to parse identifier at an identifier token");
      }
      if (Keyword.equals_insensitive("byte")) {
        Alignment = 1;
      } else if (Keyword.equals_insensitive("word")) {
        Alignment = 2;
      } else if (Keyword.equals_insensitive("dword")) {
        Alignment = 4;
      } else if (Keyword.equals_insensitive("para")) {
        Alignment = 16;
      } else if (Keyword.equals_insensitive("page")) {
        Alignment = 256;
      } else if (Keyword.equals_insensitive("align")) {
        if (getParser().parseToken(AsmToken::LParen) ||
            getParser().parseIntToken(Alignment,
                                      "Expected integer alignment") ||
            getParser().parseToken(AsmToken::RParen)) {
          return Error(getTok().getLoc(),
                       "Expected (n) following ALIGN in SEGMENT directive");
        }
        if (!isPowerOf2_64(Alignment) || Alignment > 8192) {
          return Error(KeywordLoc,
                       "ALIGN argument must be a power of 2 from 1 to 8192");
        }
      } else if (Keyword.equals_insensitive("alias")) {
        if (getParser().parseToken(AsmToken::LParen) ||
            !getTok().is(AsmToken::String))
          return Error(
              getTok().getLoc(),
              "Expected (string) following ALIAS in SEGMENT directive");
        SectionName = getTok().getStringContents();
        Lex();
        if (getParser().parseToken(AsmToken::RParen))
          return Error(
              getTok().getLoc(),
              "Expected (string) following ALIAS in SEGMENT directive");
      } else if (Keyword.equals_insensitive("readonly")) {
        Readonly = true;
      } else {
        unsigned Characteristic =
            StringSwitch<unsigned>(Keyword)
                .CaseLower("info", COFF::IMAGE_SCN_LNK_INFO)
                .CaseLower("read", COFF::IMAGE_SCN_MEM_READ)
                .CaseLower("write", COFF::IMAGE_SCN_MEM_WRITE)
                .CaseLower("execute", COFF::IMAGE_SCN_MEM_EXECUTE)
                .CaseLower("shared", COFF::IMAGE_SCN_MEM_SHARED)
                .CaseLower("nopage", COFF::IMAGE_SCN_MEM_NOT_PAGED)
                .CaseLower("nocache", COFF::IMAGE_SCN_MEM_NOT_CACHED)
                .CaseLower("discard", COFF::IMAGE_SCN_MEM_DISCARDABLE)
                .Default(-1);
        if (Characteristic == static_cast<unsigned>(-1)) {
          return Error(KeywordLoc,
                       "Expected characteristic in SEGMENT directive; found '" +
                           Keyword + "'");
        }
        Flags |= Characteristic;
        DefaultCharacteristics = false;
      }
    }
    }
  }

  SectionKind Kind = StringSwitch<SectionKind>(Class)
                         .CaseLower("data", SectionKind::getData())
                         .CaseLower("code", SectionKind::getText())
                         .CaseLower("const", SectionKind::getReadOnly())
                         .Default(SectionKind::getData());
  if (Kind.isText()) {
    if (DefaultCharacteristics) {
      Flags |= COFF::IMAGE_SCN_MEM_EXECUTE | COFF::IMAGE_SCN_MEM_READ;
    }
    Flags |= COFF::IMAGE_SCN_CNT_CODE;
  } else {
    if (DefaultCharacteristics) {
      Flags |= COFF::IMAGE_SCN_MEM_READ | COFF::IMAGE_SCN_MEM_WRITE;
    }
    Flags |= COFF::IMAGE_SCN_CNT_INITIALIZED_DATA;
  }
  if (Readonly) {
    Flags &= ~COFF::IMAGE_SCN_MEM_WRITE;
  }

  MCSection *Section = getContext().getCOFFSection(SectionName, Flags, "",
                                                   (COFF::COMDATType)(0));
  if (Alignment != 0) {
    Section->setAlignment(Align(Alignment));
  }
  getStreamer().switchSection(Section);
  return false;
}

/// ParseDirectiveSegmentEnd
///  ::= identifier "ends"
bool COFFMasmParser::ParseDirectiveSegmentEnd(StringRef Directive, SMLoc Loc) {
  StringRef SegmentName;
  if (!getLexer().is(AsmToken::Identifier))
    return TokError("expected identifier in directive");
  SegmentName = getTok().getIdentifier();

  // Ignore; no action necessary.
  Lex();
  return false;
}

/// ParseDirectiveIncludelib
///  ::= "includelib" identifier
bool COFFMasmParser::ParseDirectiveIncludelib(StringRef Directive, SMLoc Loc) {
  StringRef Lib;
  if (getParser().parseIdentifier(Lib))
    return TokError("expected identifier in includelib directive");

  unsigned Flags = COFF::IMAGE_SCN_MEM_PRELOAD | COFF::IMAGE_SCN_MEM_16BIT;
  getStreamer().pushSection();
  getStreamer().switchSection(getContext().getCOFFSection(
      ".drectve", Flags, "", (COFF::COMDATType)(0)));
  getStreamer().emitBytes("/DEFAULTLIB:");
  getStreamer().emitBytes(Lib);
  getStreamer().emitBytes(" ");
  getStreamer().popSection();
  return false;
}

/// ParseDirectiveOption
///  ::= "option" option-list
bool COFFMasmParser::ParseDirectiveOption(StringRef Directive, SMLoc Loc) {
  auto parseOption = [&]() -> bool {
    StringRef Option;
    if (getParser().parseIdentifier(Option))
      return TokError("expected identifier for option name");
    if (Option.equals_insensitive("prologue")) {
      StringRef MacroId;
      if (parseToken(AsmToken::Colon) || getParser().parseIdentifier(MacroId))
        return TokError("expected :macroId after OPTION PROLOGUE");
      if (MacroId.equals_insensitive("none")) {
        // Since we currently don't implement prologues/epilogues, NONE is our
        // default.
        return false;
      }
      return TokError("OPTION PROLOGUE is currently unsupported");
    }
    if (Option.equals_insensitive("epilogue")) {
      StringRef MacroId;
      if (parseToken(AsmToken::Colon) || getParser().parseIdentifier(MacroId))
        return TokError("expected :macroId after OPTION EPILOGUE");
      if (MacroId.equals_insensitive("none")) {
        // Since we currently don't implement prologues/epilogues, NONE is our
        // default.
        return false;
      }
      return TokError("OPTION EPILOGUE is currently unsupported");
    }
    return TokError("OPTION '" + Option + "' is currently unsupported");
  };

  if (parseMany(parseOption))
    return addErrorSuffix(" in OPTION directive");
  return false;
}

/// ParseDirectiveProc
/// TODO(epastor): Implement parameters and other attributes.
///  ::= label "proc" [[distance]]
///          statements
///      label "endproc"
bool COFFMasmParser::ParseDirectiveProc(StringRef Directive, SMLoc Loc) {
  StringRef Label;
  if (getParser().parseIdentifier(Label))
    return Error(Loc, "expected identifier for procedure");
  if (getLexer().is(AsmToken::Identifier)) {
    StringRef nextVal = getTok().getString();
    SMLoc nextLoc = getTok().getLoc();
    if (nextVal.equals_insensitive("far")) {
      // TODO(epastor): Handle far procedure definitions.
      Lex();
      return Error(nextLoc, "far procedure definitions not yet supported");
    } else if (nextVal.equals_insensitive("near")) {
      Lex();
      nextVal = getTok().getString();
      nextLoc = getTok().getLoc();
    }
  }
  MCSymbolCOFF *Sym = cast<MCSymbolCOFF>(getContext().getOrCreateSymbol(Label));

  // Define symbol as simple external function
  Sym->setExternal(true);
  Sym->setType(COFF::IMAGE_SYM_DTYPE_FUNCTION << COFF::SCT_COMPLEX_TYPE_SHIFT);

  bool Framed = false;
  if (getLexer().is(AsmToken::Identifier) &&
      getTok().getString().equals_insensitive("frame")) {
    Lex();
    Framed = true;
    getStreamer().emitWinCFIStartProc(Sym, Loc);
  }
  getStreamer().emitLabel(Sym, Loc);

  CurrentProcedures.push_back(Label);
  CurrentProceduresFramed.push_back(Framed);
  return false;
}
bool COFFMasmParser::ParseDirectiveEndProc(StringRef Directive, SMLoc Loc) {
  StringRef Label;
  SMLoc LabelLoc = getTok().getLoc();
  if (getParser().parseIdentifier(Label))
    return Error(LabelLoc, "expected identifier for procedure end");

  if (CurrentProcedures.empty())
    return Error(Loc, "endp outside of procedure block");
  else if (!CurrentProcedures.back().equals_insensitive(Label))
    return Error(LabelLoc, "endp does not match current procedure '" +
                               CurrentProcedures.back() + "'");

  if (CurrentProceduresFramed.back()) {
    getStreamer().emitWinCFIEndProc(Loc);
  }
  CurrentProcedures.pop_back();
  CurrentProceduresFramed.pop_back();
  return false;
}

bool COFFMasmParser::ParseDirectiveAlias(StringRef Directive, SMLoc Loc) {
  std::string AliasName, ActualName;
  if (getTok().isNot(AsmToken::Less) ||
      getParser().parseAngleBracketString(AliasName))
    return Error(getTok().getLoc(), "expected <aliasName>");
  if (getParser().parseToken(AsmToken::Equal))
    return addErrorSuffix(" in " + Directive + " directive");
  if (getTok().isNot(AsmToken::Less) ||
      getParser().parseAngleBracketString(ActualName))
    return Error(getTok().getLoc(), "expected <actualName>");

  MCSymbol *Alias = getContext().getOrCreateSymbol(AliasName);
  MCSymbol *Actual = getContext().getOrCreateSymbol(ActualName);

  getStreamer().emitWeakReference(Alias, Actual);

  return false;
}

bool COFFMasmParser::ParseSEHDirectiveAllocStack(StringRef Directive,
                                                 SMLoc Loc) {
  int64_t Size;
  SMLoc SizeLoc = getTok().getLoc();
  if (getParser().parseAbsoluteExpression(Size))
    return Error(SizeLoc, "expected integer size");
  if (Size % 8 != 0)
    return Error(SizeLoc, "stack size must be a multiple of 8");
  getStreamer().emitWinCFIAllocStack(static_cast<unsigned>(Size), Loc);
  return false;
}

bool COFFMasmParser::ParseSEHDirectiveEndProlog(StringRef Directive,
                                                SMLoc Loc) {
  getStreamer().emitWinCFIEndProlog(Loc);
  return false;
}

namespace llvm {

MCAsmParserExtension *createCOFFMasmParser() { return new COFFMasmParser; }

} // end namespace llvm
