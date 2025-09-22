//===- MCAsmInfo.cpp - Asm Info -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines target asm properties related what form asm statements
// should take.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

namespace {
enum DefaultOnOff { Default, Enable, Disable };
}
static cl::opt<DefaultOnOff> DwarfExtendedLoc(
    "dwarf-extended-loc", cl::Hidden,
    cl::desc("Disable emission of the extended flags in .loc directives."),
    cl::values(clEnumVal(Default, "Default for platform"),
               clEnumVal(Enable, "Enabled"), clEnumVal(Disable, "Disabled")),
    cl::init(Default));

namespace llvm {
cl::opt<cl::boolOrDefault> UseLEB128Directives(
    "use-leb128-directives", cl::Hidden,
    cl::desc(
        "Disable the usage of LEB128 directives, and generate .byte instead."),
    cl::init(cl::BOU_UNSET));
}

MCAsmInfo::MCAsmInfo() {
  SeparatorString = ";";
  CommentString = "#";
  LabelSuffix = ":";
  PrivateGlobalPrefix = "L";
  PrivateLabelPrefix = PrivateGlobalPrefix;
  LinkerPrivateGlobalPrefix = "";
  InlineAsmStart = "APP";
  InlineAsmEnd = "NO_APP";
  Code16Directive = ".code16";
  Code32Directive = ".code32";
  Code64Directive = ".code64";
  ZeroDirective = "\t.zero\t";
  AsciiDirective = "\t.ascii\t";
  AscizDirective = "\t.asciz\t";
  Data8bitsDirective = "\t.byte\t";
  Data16bitsDirective = "\t.short\t";
  Data32bitsDirective = "\t.long\t";
  Data64bitsDirective = "\t.quad\t";
  GlobalDirective = "\t.globl\t";
  WeakDirective = "\t.weak\t";
  if (DwarfExtendedLoc != Default)
    SupportsExtendedDwarfLocDirective = DwarfExtendedLoc == Enable;
  if (UseLEB128Directives != cl::BOU_UNSET)
    HasLEB128Directives = UseLEB128Directives == cl::BOU_TRUE;
  UseIntegratedAssembler = true;
  ParseInlineAsmUsingAsmParser = false;
  PreserveAsmComments = true;
  PPCUseFullRegisterNames = false;
}

MCAsmInfo::~MCAsmInfo() = default;

void MCAsmInfo::addInitialFrameState(const MCCFIInstruction &Inst) {
  InitialFrameState.push_back(Inst);
}

const MCExpr *
MCAsmInfo::getExprForPersonalitySymbol(const MCSymbol *Sym,
                                       unsigned Encoding,
                                       MCStreamer &Streamer) const {
  return getExprForFDESymbol(Sym, Encoding, Streamer);
}

const MCExpr *
MCAsmInfo::getExprForFDESymbol(const MCSymbol *Sym,
                               unsigned Encoding,
                               MCStreamer &Streamer) const {
  if (!(Encoding & dwarf::DW_EH_PE_pcrel))
    return MCSymbolRefExpr::create(Sym, Streamer.getContext());

  MCContext &Context = Streamer.getContext();
  const MCExpr *Res = MCSymbolRefExpr::create(Sym, Context);
  MCSymbol *PCSym = Context.createTempSymbol();
  Streamer.emitLabel(PCSym);
  const MCExpr *PC = MCSymbolRefExpr::create(PCSym, Context);
  return MCBinaryExpr::createSub(Res, PC, Context);
}

bool MCAsmInfo::isAcceptableChar(char C) const {
  if (C == '@')
    return doesAllowAtInName();

  return isAlnum(C) || C == '_' || C == '$' || C == '.';
}

bool MCAsmInfo::isValidUnquotedName(StringRef Name) const {
  if (Name.empty())
    return false;

  // If any of the characters in the string is an unacceptable character, force
  // quotes.
  for (char C : Name) {
    if (!isAcceptableChar(C))
      return false;
  }

  return true;
}

bool MCAsmInfo::shouldOmitSectionDirective(StringRef SectionName) const {
  // FIXME: Does .section .bss/.data/.text work everywhere??
  return SectionName == ".text" || SectionName == ".data" ||
        (SectionName == ".bss" && !usesELFSectionDirectiveForBSS());
}
