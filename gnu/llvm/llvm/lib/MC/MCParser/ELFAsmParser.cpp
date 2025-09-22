//===- ELFAsmParser.cpp - ELF Assembly Parser -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SMLoc.h"
#include <cassert>
#include <cstdint>
#include <utility>

using namespace llvm;

namespace {

class ELFAsmParser : public MCAsmParserExtension {
  template<bool (ELFAsmParser::*HandlerMethod)(StringRef, SMLoc)>
  void addDirectiveHandler(StringRef Directive) {
    MCAsmParser::ExtensionDirectiveHandler Handler = std::make_pair(
        this, HandleDirective<ELFAsmParser, HandlerMethod>);

    getParser().addDirectiveHandler(Directive, Handler);
  }

  bool ParseSectionSwitch(StringRef Section, unsigned Type, unsigned Flags,
                          SectionKind Kind);

public:
  ELFAsmParser() { BracketExpressionsSupported = true; }

  void Initialize(MCAsmParser &Parser) override {
    // Call the base implementation.
    this->MCAsmParserExtension::Initialize(Parser);

    addDirectiveHandler<&ELFAsmParser::ParseSectionDirectiveData>(".data");
    addDirectiveHandler<&ELFAsmParser::ParseSectionDirectiveText>(".text");
    addDirectiveHandler<&ELFAsmParser::ParseSectionDirectiveBSS>(".bss");
    addDirectiveHandler<&ELFAsmParser::ParseSectionDirectiveRoData>(".rodata");
    addDirectiveHandler<&ELFAsmParser::ParseSectionDirectiveTData>(".tdata");
    addDirectiveHandler<&ELFAsmParser::ParseSectionDirectiveTBSS>(".tbss");
    addDirectiveHandler<
      &ELFAsmParser::ParseSectionDirectiveDataRel>(".data.rel");
    addDirectiveHandler<
      &ELFAsmParser::ParseSectionDirectiveDataRelRo>(".data.rel.ro");
    addDirectiveHandler<
      &ELFAsmParser::ParseSectionDirectiveEhFrame>(".eh_frame");
    addDirectiveHandler<&ELFAsmParser::ParseDirectiveSection>(".section");
    addDirectiveHandler<
      &ELFAsmParser::ParseDirectivePushSection>(".pushsection");
    addDirectiveHandler<&ELFAsmParser::ParseDirectivePopSection>(".popsection");
    addDirectiveHandler<&ELFAsmParser::ParseDirectiveSize>(".size");
    addDirectiveHandler<&ELFAsmParser::ParseDirectivePrevious>(".previous");
    addDirectiveHandler<&ELFAsmParser::ParseDirectiveType>(".type");
    addDirectiveHandler<&ELFAsmParser::ParseDirectiveIdent>(".ident");
    addDirectiveHandler<&ELFAsmParser::ParseDirectiveSymver>(".symver");
    addDirectiveHandler<&ELFAsmParser::ParseDirectiveVersion>(".version");
    addDirectiveHandler<&ELFAsmParser::ParseDirectiveWeakref>(".weakref");
    addDirectiveHandler<&ELFAsmParser::ParseDirectiveSymbolAttribute>(".weak");
    addDirectiveHandler<&ELFAsmParser::ParseDirectiveSymbolAttribute>(".local");
    addDirectiveHandler<
      &ELFAsmParser::ParseDirectiveSymbolAttribute>(".protected");
    addDirectiveHandler<
      &ELFAsmParser::ParseDirectiveSymbolAttribute>(".internal");
    addDirectiveHandler<
      &ELFAsmParser::ParseDirectiveSymbolAttribute>(".hidden");
    addDirectiveHandler<&ELFAsmParser::ParseDirectiveSubsection>(".subsection");
    addDirectiveHandler<&ELFAsmParser::ParseDirectiveCGProfile>(".cg_profile");
  }

  // FIXME: Part of this logic is duplicated in the MCELFStreamer. What is
  // the best way for us to get access to it?
  bool ParseSectionDirectiveData(StringRef, SMLoc) {
    return ParseSectionSwitch(".data", ELF::SHT_PROGBITS,
                              ELF::SHF_WRITE | ELF::SHF_ALLOC,
                              SectionKind::getData());
  }
  bool ParseSectionDirectiveText(StringRef, SMLoc) {
    return ParseSectionSwitch(".text", ELF::SHT_PROGBITS,
                              ELF::SHF_EXECINSTR |
                              ELF::SHF_ALLOC, SectionKind::getText());
  }
  bool ParseSectionDirectiveBSS(StringRef, SMLoc) {
    return ParseSectionSwitch(".bss", ELF::SHT_NOBITS,
                              ELF::SHF_WRITE |
                              ELF::SHF_ALLOC, SectionKind::getBSS());
  }
  bool ParseSectionDirectiveRoData(StringRef, SMLoc) {
    return ParseSectionSwitch(".rodata", ELF::SHT_PROGBITS,
                              ELF::SHF_ALLOC,
                              SectionKind::getReadOnly());
  }
  bool ParseSectionDirectiveTData(StringRef, SMLoc) {
    return ParseSectionSwitch(".tdata", ELF::SHT_PROGBITS,
                              ELF::SHF_ALLOC |
                              ELF::SHF_TLS | ELF::SHF_WRITE,
                              SectionKind::getThreadData());
  }
  bool ParseSectionDirectiveTBSS(StringRef, SMLoc) {
    return ParseSectionSwitch(".tbss", ELF::SHT_NOBITS,
                              ELF::SHF_ALLOC |
                              ELF::SHF_TLS | ELF::SHF_WRITE,
                              SectionKind::getThreadBSS());
  }
  bool ParseSectionDirectiveDataRel(StringRef, SMLoc) {
    return ParseSectionSwitch(".data.rel", ELF::SHT_PROGBITS,
                              ELF::SHF_ALLOC | ELF::SHF_WRITE,
                              SectionKind::getData());
  }
  bool ParseSectionDirectiveDataRelRo(StringRef, SMLoc) {
    return ParseSectionSwitch(".data.rel.ro", ELF::SHT_PROGBITS,
                              ELF::SHF_ALLOC |
                              ELF::SHF_WRITE,
                              SectionKind::getReadOnlyWithRel());
  }
  bool ParseSectionDirectiveEhFrame(StringRef, SMLoc) {
    return ParseSectionSwitch(".eh_frame", ELF::SHT_PROGBITS,
                              ELF::SHF_ALLOC | ELF::SHF_WRITE,
                              SectionKind::getData());
  }
  bool ParseDirectivePushSection(StringRef, SMLoc);
  bool ParseDirectivePopSection(StringRef, SMLoc);
  bool ParseDirectiveSection(StringRef, SMLoc);
  bool ParseDirectiveSize(StringRef, SMLoc);
  bool ParseDirectivePrevious(StringRef, SMLoc);
  bool ParseDirectiveType(StringRef, SMLoc);
  bool ParseDirectiveIdent(StringRef, SMLoc);
  bool ParseDirectiveSymver(StringRef, SMLoc);
  bool ParseDirectiveVersion(StringRef, SMLoc);
  bool ParseDirectiveWeakref(StringRef, SMLoc);
  bool ParseDirectiveSymbolAttribute(StringRef, SMLoc);
  bool ParseDirectiveSubsection(StringRef, SMLoc);
  bool ParseDirectiveCGProfile(StringRef, SMLoc);

private:
  bool ParseSectionName(StringRef &SectionName);
  bool ParseSectionArguments(bool IsPush, SMLoc loc);
  unsigned parseSunStyleSectionFlags();
  bool maybeParseSectionType(StringRef &TypeName);
  bool parseMergeSize(int64_t &Size);
  bool parseGroup(StringRef &GroupName, bool &IsComdat);
  bool parseLinkedToSym(MCSymbolELF *&LinkedToSym);
  bool maybeParseUniqueID(int64_t &UniqueID);
};

} // end anonymous namespace

/// ParseDirectiveSymbolAttribute
///  ::= { ".local", ".weak", ... } [ identifier ( , identifier )* ]
bool ELFAsmParser::ParseDirectiveSymbolAttribute(StringRef Directive, SMLoc) {
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
        return TokError("expected identifier");

      if (getParser().discardLTOSymbol(Name)) {
        if (getLexer().is(AsmToken::EndOfStatement))
          break;
        continue;
      }

      MCSymbol *Sym = getContext().getOrCreateSymbol(Name);

      getStreamer().emitSymbolAttribute(Sym, Attr);

      if (getLexer().is(AsmToken::EndOfStatement))
        break;

      if (getLexer().isNot(AsmToken::Comma))
        return TokError("expected comma");
      Lex();
    }
  }

  Lex();
  return false;
}

bool ELFAsmParser::ParseSectionSwitch(StringRef Section, unsigned Type,
                                      unsigned Flags, SectionKind Kind) {
  const MCExpr *Subsection = nullptr;
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    if (getParser().parseExpression(Subsection))
      return true;
  }
  Lex();

  getStreamer().switchSection(getContext().getELFSection(Section, Type, Flags),
                              Subsection);

  return false;
}

bool ELFAsmParser::ParseDirectiveSize(StringRef, SMLoc) {
  StringRef Name;
  if (getParser().parseIdentifier(Name))
    return TokError("expected identifier");
  MCSymbolELF *Sym = cast<MCSymbolELF>(getContext().getOrCreateSymbol(Name));

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("expected comma");
  Lex();

  const MCExpr *Expr;
  if (getParser().parseExpression(Expr))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token");
  Lex();

  getStreamer().emitELFSize(Sym, Expr);
  return false;
}

bool ELFAsmParser::ParseSectionName(StringRef &SectionName) {
  // A section name can contain -, so we cannot just use
  // parseIdentifier.
  SMLoc FirstLoc = getLexer().getLoc();
  unsigned Size = 0;

  if (getLexer().is(AsmToken::String)) {
    SectionName = getTok().getIdentifier();
    Lex();
    return false;
  }

  while (!getParser().hasPendingError()) {
    SMLoc PrevLoc = getLexer().getLoc();
    if (getLexer().is(AsmToken::Comma) ||
      getLexer().is(AsmToken::EndOfStatement))
      break;

    unsigned CurSize;
    if (getLexer().is(AsmToken::String)) {
      CurSize = getTok().getIdentifier().size() + 2;
      Lex();
    } else if (getLexer().is(AsmToken::Identifier)) {
      CurSize = getTok().getIdentifier().size();
      Lex();
    } else {
      CurSize = getTok().getString().size();
      Lex();
    }
    Size += CurSize;
    SectionName = StringRef(FirstLoc.getPointer(), Size);

    // Make sure the following token is adjacent.
    if (PrevLoc.getPointer() + CurSize != getTok().getLoc().getPointer())
      break;
  }
  if (Size == 0)
    return true;

  return false;
}

static unsigned parseSectionFlags(const Triple &TT, StringRef flagsStr,
                                  bool *UseLastGroup) {
  unsigned flags = 0;

  // If a valid numerical value is set for the section flag, use it verbatim
  if (!flagsStr.getAsInteger(0, flags))
    return flags;

  for (char i : flagsStr) {
    switch (i) {
    case 'a':
      flags |= ELF::SHF_ALLOC;
      break;
    case 'e':
      flags |= ELF::SHF_EXCLUDE;
      break;
    case 'x':
      flags |= ELF::SHF_EXECINSTR;
      break;
    case 'w':
      flags |= ELF::SHF_WRITE;
      break;
    case 'o':
      flags |= ELF::SHF_LINK_ORDER;
      break;
    case 'M':
      flags |= ELF::SHF_MERGE;
      break;
    case 'S':
      flags |= ELF::SHF_STRINGS;
      break;
    case 'T':
      flags |= ELF::SHF_TLS;
      break;
    case 'c':
      if (TT.getArch() != Triple::xcore)
        return -1U;
      flags |= ELF::XCORE_SHF_CP_SECTION;
      break;
    case 'd':
      if (TT.getArch() != Triple::xcore)
        return -1U;
      flags |= ELF::XCORE_SHF_DP_SECTION;
      break;
    case 'y':
      if (!(TT.isARM() || TT.isThumb()))
        return -1U;
      flags |= ELF::SHF_ARM_PURECODE;
      break;
    case 's':
      if (TT.getArch() != Triple::hexagon)
        return -1U;
      flags |= ELF::SHF_HEX_GPREL;
      break;
    case 'G':
      flags |= ELF::SHF_GROUP;
      break;
    case 'l':
      if (TT.getArch() != Triple::x86_64)
        return -1U;
      flags |= ELF::SHF_X86_64_LARGE;
      break;
    case 'R':
      if (TT.isOSSolaris())
        flags |= ELF::SHF_SUNW_NODISCARD;
      else
        flags |= ELF::SHF_GNU_RETAIN;
      break;
    case '?':
      *UseLastGroup = true;
      break;
    default:
      return -1U;
    }
  }

  return flags;
}

unsigned ELFAsmParser::parseSunStyleSectionFlags() {
  unsigned flags = 0;
  while (getLexer().is(AsmToken::Hash)) {
    Lex(); // Eat the #.

    if (!getLexer().is(AsmToken::Identifier))
      return -1U;

    StringRef flagId = getTok().getIdentifier();
    if (flagId == "alloc")
      flags |= ELF::SHF_ALLOC;
    else if (flagId == "execinstr")
      flags |= ELF::SHF_EXECINSTR;
    else if (flagId == "write")
      flags |= ELF::SHF_WRITE;
    else if (flagId == "tls")
      flags |= ELF::SHF_TLS;
    else
      return -1U;

    Lex(); // Eat the flag.

    if (!getLexer().is(AsmToken::Comma))
        break;
    Lex(); // Eat the comma.
  }
  return flags;
}


bool ELFAsmParser::ParseDirectivePushSection(StringRef s, SMLoc loc) {
  getStreamer().pushSection();

  if (ParseSectionArguments(/*IsPush=*/true, loc)) {
    getStreamer().popSection();
    return true;
  }

  return false;
}

bool ELFAsmParser::ParseDirectivePopSection(StringRef, SMLoc) {
  if (!getStreamer().popSection())
    return TokError(".popsection without corresponding .pushsection");
  return false;
}

bool ELFAsmParser::ParseDirectiveSection(StringRef, SMLoc loc) {
  return ParseSectionArguments(/*IsPush=*/false, loc);
}

bool ELFAsmParser::maybeParseSectionType(StringRef &TypeName) {
  MCAsmLexer &L = getLexer();
  if (L.isNot(AsmToken::Comma))
    return false;
  Lex();
  if (L.isNot(AsmToken::At) && L.isNot(AsmToken::Percent) &&
      L.isNot(AsmToken::String)) {
    if (L.getAllowAtInIdentifier())
      return TokError("expected '@<type>', '%<type>' or \"<type>\"");
    else
      return TokError("expected '%<type>' or \"<type>\"");
  }
  if (!L.is(AsmToken::String))
    Lex();
  if (L.is(AsmToken::Integer)) {
    TypeName = getTok().getString();
    Lex();
  } else if (getParser().parseIdentifier(TypeName))
    return TokError("expected identifier");
  return false;
}

bool ELFAsmParser::parseMergeSize(int64_t &Size) {
  if (getLexer().isNot(AsmToken::Comma))
    return TokError("expected the entry size");
  Lex();
  if (getParser().parseAbsoluteExpression(Size))
    return true;
  if (Size <= 0)
    return TokError("entry size must be positive");
  return false;
}

bool ELFAsmParser::parseGroup(StringRef &GroupName, bool &IsComdat) {
  MCAsmLexer &L = getLexer();
  if (L.isNot(AsmToken::Comma))
    return TokError("expected group name");
  Lex();
  if (L.is(AsmToken::Integer)) {
    GroupName = getTok().getString();
    Lex();
  } else if (getParser().parseIdentifier(GroupName)) {
    return TokError("invalid group name");
  }
  if (L.is(AsmToken::Comma)) {
    Lex();
    StringRef Linkage;
    if (getParser().parseIdentifier(Linkage))
      return TokError("invalid linkage");
    if (Linkage != "comdat")
      return TokError("Linkage must be 'comdat'");
    IsComdat = true;
  } else {
    IsComdat = false;
  }
  return false;
}

bool ELFAsmParser::parseLinkedToSym(MCSymbolELF *&LinkedToSym) {
  MCAsmLexer &L = getLexer();
  if (L.isNot(AsmToken::Comma))
    return TokError("expected linked-to symbol");
  Lex();
  StringRef Name;
  SMLoc StartLoc = L.getLoc();
  if (getParser().parseIdentifier(Name)) {
    if (getParser().getTok().getString() == "0") {
      getParser().Lex();
      LinkedToSym = nullptr;
      return false;
    }
    return TokError("invalid linked-to symbol");
  }
  LinkedToSym = dyn_cast_or_null<MCSymbolELF>(getContext().lookupSymbol(Name));
  if (!LinkedToSym || !LinkedToSym->isInSection())
    return Error(StartLoc, "linked-to symbol is not in a section: " + Name);
  return false;
}

bool ELFAsmParser::maybeParseUniqueID(int64_t &UniqueID) {
  MCAsmLexer &L = getLexer();
  if (L.isNot(AsmToken::Comma))
    return false;
  Lex();
  StringRef UniqueStr;
  if (getParser().parseIdentifier(UniqueStr))
    return TokError("expected identifier");
  if (UniqueStr != "unique")
    return TokError("expected 'unique'");
  if (L.isNot(AsmToken::Comma))
    return TokError("expected commma");
  Lex();
  if (getParser().parseAbsoluteExpression(UniqueID))
    return true;
  if (UniqueID < 0)
    return TokError("unique id must be positive");
  if (!isUInt<32>(UniqueID) || UniqueID == ~0U)
    return TokError("unique id is too large");
  return false;
}

static bool hasPrefix(StringRef SectionName, StringRef Prefix) {
  return SectionName.consume_front(Prefix) &&
         (SectionName.empty() || SectionName[0] == '.');
}

static bool allowSectionTypeMismatch(const Triple &TT, StringRef SectionName,
                                     unsigned Type) {
  if (TT.getArch() == Triple::x86_64) {
    // x86-64 psABI names SHT_X86_64_UNWIND as the canonical type for .eh_frame,
    // but GNU as emits SHT_PROGBITS .eh_frame for .cfi_* directives. Don't
    // error for SHT_PROGBITS .eh_frame
    return SectionName == ".eh_frame" && Type == ELF::SHT_PROGBITS;
  }
  if (TT.isMIPS()) {
    // MIPS .debug_* sections should have SHT_MIPS_DWARF section type to
    // distinguish among sections contain DWARF and ECOFF debug formats,
    // but in assembly files these sections have SHT_PROGBITS type.
    return SectionName.starts_with(".debug_") && Type == ELF::SHT_PROGBITS;
  }
  return false;
}

bool ELFAsmParser::ParseSectionArguments(bool IsPush, SMLoc loc) {
  StringRef SectionName;

  if (ParseSectionName(SectionName))
    return TokError("expected identifier");

  StringRef TypeName;
  int64_t Size = 0;
  StringRef GroupName;
  bool IsComdat = false;
  unsigned Flags = 0;
  unsigned extraFlags = 0;
  const MCExpr *Subsection = nullptr;
  bool UseLastGroup = false;
  MCSymbolELF *LinkedToSym = nullptr;
  int64_t UniqueID = ~0;

  // Set the defaults first.
  if (hasPrefix(SectionName, ".rodata") || SectionName == ".rodata1")
    Flags |= ELF::SHF_ALLOC;
  else if (SectionName == ".fini" || SectionName == ".init" ||
           hasPrefix(SectionName, ".text"))
    Flags |= ELF::SHF_ALLOC | ELF::SHF_EXECINSTR;
  else if (hasPrefix(SectionName, ".data") || SectionName == ".data1" ||
           hasPrefix(SectionName, ".bss") ||
           hasPrefix(SectionName, ".init_array") ||
           hasPrefix(SectionName, ".fini_array") ||
           hasPrefix(SectionName, ".preinit_array"))
    Flags |= ELF::SHF_ALLOC | ELF::SHF_WRITE;
  else if (hasPrefix(SectionName, ".tdata") || hasPrefix(SectionName, ".tbss"))
    Flags |= ELF::SHF_ALLOC | ELF::SHF_WRITE | ELF::SHF_TLS;

  if (getLexer().is(AsmToken::Comma)) {
    Lex();

    if (IsPush && getLexer().isNot(AsmToken::String)) {
      if (getParser().parseExpression(Subsection))
        return true;
      if (getLexer().isNot(AsmToken::Comma))
        goto EndStmt;
      Lex();
    }

    if (getLexer().isNot(AsmToken::String)) {
      if (getLexer().isNot(AsmToken::Hash))
        return TokError("expected string");
      extraFlags = parseSunStyleSectionFlags();
    } else {
      StringRef FlagsStr = getTok().getStringContents();
      Lex();
      extraFlags = parseSectionFlags(getContext().getTargetTriple(), FlagsStr,
                                     &UseLastGroup);
    }

    if (extraFlags == -1U)
      return TokError("unknown flag");
    Flags |= extraFlags;

    bool Mergeable = Flags & ELF::SHF_MERGE;
    bool Group = Flags & ELF::SHF_GROUP;
    if (Group && UseLastGroup)
      return TokError("Section cannot specifiy a group name while also acting "
                      "as a member of the last group");

    if (maybeParseSectionType(TypeName))
      return true;

    MCAsmLexer &L = getLexer();
    if (TypeName.empty()) {
      if (Mergeable)
        return TokError("Mergeable section must specify the type");
      if (Group)
        return TokError("Group section must specify the type");
      if (L.isNot(AsmToken::EndOfStatement))
        return TokError("expected end of directive");
    }

    if (Mergeable)
      if (parseMergeSize(Size))
        return true;
    if (Flags & ELF::SHF_LINK_ORDER)
      if (parseLinkedToSym(LinkedToSym))
        return true;
    if (Group)
      if (parseGroup(GroupName, IsComdat))
        return true;
    if (maybeParseUniqueID(UniqueID))
      return true;
  }

EndStmt:
  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("expected end of directive");
  Lex();

  unsigned Type = ELF::SHT_PROGBITS;

  if (TypeName.empty()) {
    if (SectionName.starts_with(".note"))
      Type = ELF::SHT_NOTE;
    else if (hasPrefix(SectionName, ".init_array"))
      Type = ELF::SHT_INIT_ARRAY;
    else if (hasPrefix(SectionName, ".bss"))
      Type = ELF::SHT_NOBITS;
    else if (hasPrefix(SectionName, ".tbss"))
      Type = ELF::SHT_NOBITS;
    else if (hasPrefix(SectionName, ".fini_array"))
      Type = ELF::SHT_FINI_ARRAY;
    else if (hasPrefix(SectionName, ".preinit_array"))
      Type = ELF::SHT_PREINIT_ARRAY;
  } else {
    if (TypeName == "init_array")
      Type = ELF::SHT_INIT_ARRAY;
    else if (TypeName == "fini_array")
      Type = ELF::SHT_FINI_ARRAY;
    else if (TypeName == "preinit_array")
      Type = ELF::SHT_PREINIT_ARRAY;
    else if (TypeName == "nobits")
      Type = ELF::SHT_NOBITS;
    else if (TypeName == "progbits")
      Type = ELF::SHT_PROGBITS;
    else if (TypeName == "note")
      Type = ELF::SHT_NOTE;
    else if (TypeName == "unwind")
      Type = ELF::SHT_X86_64_UNWIND;
    else if (TypeName == "llvm_odrtab")
      Type = ELF::SHT_LLVM_ODRTAB;
    else if (TypeName == "llvm_linker_options")
      Type = ELF::SHT_LLVM_LINKER_OPTIONS;
    else if (TypeName == "llvm_call_graph_profile")
      Type = ELF::SHT_LLVM_CALL_GRAPH_PROFILE;
    else if (TypeName == "llvm_dependent_libraries")
      Type = ELF::SHT_LLVM_DEPENDENT_LIBRARIES;
    else if (TypeName == "llvm_sympart")
      Type = ELF::SHT_LLVM_SYMPART;
    else if (TypeName == "llvm_bb_addr_map")
      Type = ELF::SHT_LLVM_BB_ADDR_MAP;
    else if (TypeName == "llvm_offloading")
      Type = ELF::SHT_LLVM_OFFLOADING;
    else if (TypeName == "llvm_lto")
      Type = ELF::SHT_LLVM_LTO;
    else if (TypeName.getAsInteger(0, Type))
      return TokError("unknown section type");
  }

  if (UseLastGroup) {
    if (const MCSectionELF *Section =
            cast_or_null<MCSectionELF>(getStreamer().getCurrentSectionOnly()))
      if (const MCSymbol *Group = Section->getGroup()) {
        GroupName = Group->getName();
        IsComdat = Section->isComdat();
        Flags |= ELF::SHF_GROUP;
      }
  }

  MCSectionELF *Section =
      getContext().getELFSection(SectionName, Type, Flags, Size, GroupName,
                                 IsComdat, UniqueID, LinkedToSym);
  getStreamer().switchSection(Section, Subsection);
  // Check that flags are used consistently. However, the GNU assembler permits
  // to leave out in subsequent uses of the same sections; for compatibility,
  // do likewise.
  if (!TypeName.empty() && Section->getType() != Type &&
      !allowSectionTypeMismatch(getContext().getTargetTriple(), SectionName,
                                Type))
    Error(loc, "changed section type for " + SectionName + ", expected: 0x" +
                   utohexstr(Section->getType()));
  if ((extraFlags || Size || !TypeName.empty()) && Section->getFlags() != Flags)
    Error(loc, "changed section flags for " + SectionName + ", expected: 0x" +
                   utohexstr(Section->getFlags()));
  if ((extraFlags || Size || !TypeName.empty()) &&
      Section->getEntrySize() != Size)
    Error(loc, "changed section entsize for " + SectionName +
                   ", expected: " + Twine(Section->getEntrySize()));

  if (getContext().getGenDwarfForAssembly() &&
      (Section->getFlags() & ELF::SHF_ALLOC) &&
      (Section->getFlags() & ELF::SHF_EXECINSTR)) {
    bool InsertResult = getContext().addGenDwarfSection(Section);
    if (InsertResult && getContext().getDwarfVersion() <= 2)
      Warning(loc, "DWARF2 only supports one section per compilation unit");
  }

  return false;
}

bool ELFAsmParser::ParseDirectivePrevious(StringRef DirName, SMLoc) {
  MCSectionSubPair PreviousSection = getStreamer().getPreviousSection();
  if (PreviousSection.first == nullptr)
      return TokError(".previous without corresponding .section");
  getStreamer().switchSection(PreviousSection.first, PreviousSection.second);

  return false;
}

static MCSymbolAttr MCAttrForString(StringRef Type) {
  return StringSwitch<MCSymbolAttr>(Type)
          .Cases("STT_FUNC", "function", MCSA_ELF_TypeFunction)
          .Cases("STT_OBJECT", "object", MCSA_ELF_TypeObject)
          .Cases("STT_TLS", "tls_object", MCSA_ELF_TypeTLS)
          .Cases("STT_COMMON", "common", MCSA_ELF_TypeCommon)
          .Cases("STT_NOTYPE", "notype", MCSA_ELF_TypeNoType)
          .Cases("STT_GNU_IFUNC", "gnu_indirect_function",
                 MCSA_ELF_TypeIndFunction)
          .Case("gnu_unique_object", MCSA_ELF_TypeGnuUniqueObject)
          .Default(MCSA_Invalid);
}

/// ParseDirectiveELFType
///  ::= .type identifier , STT_<TYPE_IN_UPPER_CASE>
///  ::= .type identifier , #attribute
///  ::= .type identifier , @attribute
///  ::= .type identifier , %attribute
///  ::= .type identifier , "attribute"
bool ELFAsmParser::ParseDirectiveType(StringRef, SMLoc) {
  StringRef Name;
  if (getParser().parseIdentifier(Name))
    return TokError("expected identifier");

  // Handle the identifier as the key symbol.
  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);

  // NOTE the comma is optional in all cases.  It is only documented as being
  // optional in the first case, however, GAS will silently treat the comma as
  // optional in all cases.  Furthermore, although the documentation states that
  // the first form only accepts STT_<TYPE_IN_UPPER_CASE>, in reality, GAS
  // accepts both the upper case name as well as the lower case aliases.
  if (getLexer().is(AsmToken::Comma))
    Lex();

  if (getLexer().isNot(AsmToken::Identifier) &&
      getLexer().isNot(AsmToken::Hash) &&
      getLexer().isNot(AsmToken::Percent) &&
      getLexer().isNot(AsmToken::String)) {
    if (!getLexer().getAllowAtInIdentifier())
      return TokError("expected STT_<TYPE_IN_UPPER_CASE>, '#<type>', "
                      "'%<type>' or \"<type>\"");
    else if (getLexer().isNot(AsmToken::At))
      return TokError("expected STT_<TYPE_IN_UPPER_CASE>, '#<type>', '@<type>', "
                      "'%<type>' or \"<type>\"");
  }

  if (getLexer().isNot(AsmToken::String) &&
      getLexer().isNot(AsmToken::Identifier))
    Lex();

  SMLoc TypeLoc = getLexer().getLoc();

  StringRef Type;
  if (getParser().parseIdentifier(Type))
    return TokError("expected symbol type");

  MCSymbolAttr Attr = MCAttrForString(Type);
  if (Attr == MCSA_Invalid)
    return Error(TypeLoc, "unsupported attribute");

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("expected end of directive");
  Lex();

  getStreamer().emitSymbolAttribute(Sym, Attr);

  return false;
}

/// ParseDirectiveIdent
///  ::= .ident string
bool ELFAsmParser::ParseDirectiveIdent(StringRef, SMLoc) {
  if (getLexer().isNot(AsmToken::String))
    return TokError("expected string");

  StringRef Data = getTok().getIdentifier();

  Lex();

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("expected end of directive");
  Lex();

  getStreamer().emitIdent(Data);
  return false;
}

/// ParseDirectiveSymver
///  ::= .symver foo, bar2@zed
bool ELFAsmParser::ParseDirectiveSymver(StringRef, SMLoc) {
  StringRef OriginalName, Name, Action;
  if (getParser().parseIdentifier(OriginalName))
    return TokError("expected identifier");

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("expected a comma");

  // ARM assembly uses @ for a comment...
  // except when parsing the second parameter of the .symver directive.
  // Force the next symbol to allow @ in the identifier, which is
  // required for this directive and then reset it to its initial state.
  const bool AllowAtInIdentifier = getLexer().getAllowAtInIdentifier();
  getLexer().setAllowAtInIdentifier(true);
  Lex();
  getLexer().setAllowAtInIdentifier(AllowAtInIdentifier);

  if (getParser().parseIdentifier(Name))
    return TokError("expected identifier");

  if (!Name.contains('@'))
    return TokError("expected a '@' in the name");
  bool KeepOriginalSym = !Name.contains("@@@");
  if (parseOptionalToken(AsmToken::Comma)) {
    if (getParser().parseIdentifier(Action) || Action != "remove")
      return TokError("expected 'remove'");
    KeepOriginalSym = false;
  }
  (void)parseOptionalToken(AsmToken::EndOfStatement);

  getStreamer().emitELFSymverDirective(
      getContext().getOrCreateSymbol(OriginalName), Name, KeepOriginalSym);
  return false;
}

/// ParseDirectiveVersion
///  ::= .version string
bool ELFAsmParser::ParseDirectiveVersion(StringRef, SMLoc) {
  if (getLexer().isNot(AsmToken::String))
    return TokError("expected string");

  StringRef Data = getTok().getIdentifier();

  Lex();

  MCSection *Note = getContext().getELFSection(".note", ELF::SHT_NOTE, 0);

  getStreamer().pushSection();
  getStreamer().switchSection(Note);
  getStreamer().emitInt32(Data.size() + 1); // namesz
  getStreamer().emitInt32(0);               // descsz = 0 (no description).
  getStreamer().emitInt32(1);               // type = NT_VERSION
  getStreamer().emitBytes(Data);            // name
  getStreamer().emitInt8(0);                // NUL
  getStreamer().emitValueToAlignment(Align(4));
  getStreamer().popSection();
  return false;
}

/// ParseDirectiveWeakref
///  ::= .weakref foo, bar
bool ELFAsmParser::ParseDirectiveWeakref(StringRef, SMLoc) {
  // FIXME: Share code with the other alias building directives.

  StringRef AliasName;
  if (getParser().parseIdentifier(AliasName))
    return TokError("expected identifier");

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("expected a comma");

  Lex();

  StringRef Name;
  if (getParser().parseIdentifier(Name))
    return TokError("expected identifier");

  MCSymbol *Alias = getContext().getOrCreateSymbol(AliasName);

  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);

  getStreamer().emitWeakReference(Alias, Sym);
  return false;
}

bool ELFAsmParser::ParseDirectiveSubsection(StringRef, SMLoc) {
  const MCExpr *Subsection = MCConstantExpr::create(0, getContext());
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    if (getParser().parseExpression(Subsection))
     return true;
  }

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("expected end of directive");

  Lex();

  return getStreamer().switchSection(getStreamer().getCurrentSectionOnly(),
                                     Subsection);
}

bool ELFAsmParser::ParseDirectiveCGProfile(StringRef S, SMLoc Loc) {
  return MCAsmParserExtension::ParseDirectiveCGProfile(S, Loc);
}

namespace llvm {

MCAsmParserExtension *createELFAsmParser() {
  return new ELFAsmParser;
}

} // end namespace llvm
