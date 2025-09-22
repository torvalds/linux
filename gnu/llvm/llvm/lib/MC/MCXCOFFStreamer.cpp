//===- lib/MC/MCXCOFFStreamer.cpp - XCOFF Object Output -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file assembles .s files and emits XCOFF .o object files.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCXCOFFStreamer.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSectionXCOFF.h"
#include "llvm/MC/MCSymbolXCOFF.h"
#include "llvm/MC/MCXCOFFObjectWriter.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

MCXCOFFStreamer::MCXCOFFStreamer(MCContext &Context,
                                 std::unique_ptr<MCAsmBackend> MAB,
                                 std::unique_ptr<MCObjectWriter> OW,
                                 std::unique_ptr<MCCodeEmitter> Emitter)
    : MCObjectStreamer(Context, std::move(MAB), std::move(OW),
                       std::move(Emitter)) {}

bool MCXCOFFStreamer::emitSymbolAttribute(MCSymbol *Sym,
                                          MCSymbolAttr Attribute) {
  auto *Symbol = cast<MCSymbolXCOFF>(Sym);
  getAssembler().registerSymbol(*Symbol);

  switch (Attribute) {
  // XCOFF doesn't support the cold feature.
  case MCSA_Cold:
    return false;

  case MCSA_Global:
  case MCSA_Extern:
    Symbol->setStorageClass(XCOFF::C_EXT);
    Symbol->setExternal(true);
    break;
  case MCSA_LGlobal:
    Symbol->setStorageClass(XCOFF::C_HIDEXT);
    Symbol->setExternal(true);
    break;
  case llvm::MCSA_Weak:
    Symbol->setStorageClass(XCOFF::C_WEAKEXT);
    Symbol->setExternal(true);
    break;
  case llvm::MCSA_Hidden:
    Symbol->setVisibilityType(XCOFF::SYM_V_HIDDEN);
    break;
  case llvm::MCSA_Protected:
    Symbol->setVisibilityType(XCOFF::SYM_V_PROTECTED);
    break;
  case llvm::MCSA_Exported:
    Symbol->setVisibilityType(XCOFF::SYM_V_EXPORTED);
    break;
  default:
    report_fatal_error("Not implemented yet.");
  }
  return true;
}

void MCXCOFFStreamer::emitXCOFFSymbolLinkageWithVisibility(
    MCSymbol *Symbol, MCSymbolAttr Linkage, MCSymbolAttr Visibility) {

  emitSymbolAttribute(Symbol, Linkage);

  // When the caller passes `MCSA_Invalid` for the visibility, do not emit one.
  if (Visibility == MCSA_Invalid)
    return;

  emitSymbolAttribute(Symbol, Visibility);
}

void MCXCOFFStreamer::emitXCOFFRefDirective(const MCSymbol *Symbol) {
  // Add a Fixup here to later record a relocation of type R_REF to prevent the
  // ref symbol from being garbage collected (by the binder).
  MCDataFragment *DF = getOrCreateDataFragment();
  const MCSymbolRefExpr *SRE = MCSymbolRefExpr::create(Symbol, getContext());
  std::optional<MCFixupKind> MaybeKind =
      getAssembler().getBackend().getFixupKind("R_REF");
  if (!MaybeKind)
    report_fatal_error("failed to get fixup kind for R_REF relocation");

  MCFixupKind Kind = *MaybeKind;
  MCFixup Fixup = MCFixup::create(DF->getContents().size(), SRE, Kind);
  DF->getFixups().push_back(Fixup);
}

void MCXCOFFStreamer::emitXCOFFRenameDirective(const MCSymbol *Name,
                                               StringRef Rename) {
  const MCSymbolXCOFF *Symbol = cast<const MCSymbolXCOFF>(Name);
  if (!Symbol->hasRename())
    report_fatal_error("Only explicit .rename is supported for XCOFF.");
}

void MCXCOFFStreamer::emitXCOFFExceptDirective(const MCSymbol *Symbol,
                                               const MCSymbol *Trap,
                                               unsigned Lang, unsigned Reason,
                                               unsigned FunctionSize,
                                               bool hasDebug) {
  // TODO: Export XCOFFObjectWriter to llvm/MC/MCXCOFFObjectWriter.h and access
  // it from MCXCOFFStreamer.
  XCOFF::addExceptionEntry(getAssembler().getWriter(), Symbol, Trap, Lang,
                           Reason, FunctionSize, hasDebug);
}

void MCXCOFFStreamer::emitXCOFFCInfoSym(StringRef Name, StringRef Metadata) {
  XCOFF::addCInfoSymEntry(getAssembler().getWriter(), Name, Metadata);
}

void MCXCOFFStreamer::emitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                       Align ByteAlignment) {
  getAssembler().registerSymbol(*Symbol);
  Symbol->setExternal(cast<MCSymbolXCOFF>(Symbol)->getStorageClass() !=
                      XCOFF::C_HIDEXT);
  Symbol->setCommon(Size, ByteAlignment);

  // Default csect align is 4, but common symbols have explicit alignment values
  // and we should honor it.
  cast<MCSymbolXCOFF>(Symbol)->getRepresentedCsect()->setAlignment(
      ByteAlignment);

  // Emit the alignment and storage for the variable to the section.
  emitValueToAlignment(ByteAlignment);
  emitZeros(Size);
}

void MCXCOFFStreamer::emitZerofill(MCSection *Section, MCSymbol *Symbol,
                                   uint64_t Size, Align ByteAlignment,
                                   SMLoc Loc) {
  report_fatal_error("Zero fill not implemented for XCOFF.");
}

void MCXCOFFStreamer::emitInstToData(const MCInst &Inst,
                                     const MCSubtargetInfo &STI) {
  MCAssembler &Assembler = getAssembler();
  SmallVector<MCFixup, 4> Fixups;
  SmallString<256> Code;
  Assembler.getEmitter().encodeInstruction(Inst, Code, Fixups, STI);

  // Add the fixups and data.
  MCDataFragment *DF = getOrCreateDataFragment(&STI);
  const size_t ContentsSize = DF->getContents().size();
  auto &DataFragmentFixups = DF->getFixups();
  for (auto &Fixup : Fixups) {
    Fixup.setOffset(Fixup.getOffset() + ContentsSize);
    DataFragmentFixups.push_back(Fixup);
  }

  DF->setHasInstructions(STI);
  DF->getContents().append(Code.begin(), Code.end());
}

void MCXCOFFStreamer::emitXCOFFLocalCommonSymbol(MCSymbol *LabelSym,
                                                 uint64_t Size,
                                                 MCSymbol *CsectSym,
                                                 Align Alignment) {
  emitCommonSymbol(CsectSym, Size, Alignment);
}
