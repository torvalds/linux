//===- llvm/MC/MCWinCOFFStreamer.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains an implementation of a Windows COFF object file streamer.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCWinCOFFStreamer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSymbolCOFF.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCWinCOFFObjectWriter.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "WinCOFFStreamer"

MCWinCOFFStreamer::MCWinCOFFStreamer(MCContext &Context,
                                     std::unique_ptr<MCAsmBackend> MAB,
                                     std::unique_ptr<MCCodeEmitter> CE,
                                     std::unique_ptr<MCObjectWriter> OW)
    : MCObjectStreamer(Context, std::move(MAB), std::move(OW), std::move(CE)),
      CurSymbol(nullptr) {
  auto *TO = Context.getTargetOptions();
  if (TO && TO->MCIncrementalLinkerCompatible)
    getWriter().setIncrementalLinkerCompatible(true);
}

WinCOFFObjectWriter &MCWinCOFFStreamer::getWriter() {
  return static_cast<WinCOFFObjectWriter &>(getAssembler().getWriter());
}

void MCWinCOFFStreamer::emitInstToData(const MCInst &Inst,
                                       const MCSubtargetInfo &STI) {
  MCDataFragment *DF = getOrCreateDataFragment();

  SmallVector<MCFixup, 4> Fixups;
  SmallString<256> Code;
  getAssembler().getEmitter().encodeInstruction(Inst, Code, Fixups, STI);

  // Add the fixups and data.
  for (unsigned i = 0, e = Fixups.size(); i != e; ++i) {
    Fixups[i].setOffset(Fixups[i].getOffset() + DF->getContents().size());
    DF->getFixups().push_back(Fixups[i]);
  }
  DF->setHasInstructions(STI);
  DF->getContents().append(Code.begin(), Code.end());
}

void MCWinCOFFStreamer::initSections(bool NoExecStack,
                                     const MCSubtargetInfo &STI) {
  // FIXME: this is identical to the ELF one.
  // This emulates the same behavior of GNU as. This makes it easier
  // to compare the output as the major sections are in the same order.
  switchSection(getContext().getObjectFileInfo()->getTextSection());
  emitCodeAlignment(Align(4), &STI);

  switchSection(getContext().getObjectFileInfo()->getDataSection());
  emitCodeAlignment(Align(4), &STI);

  switchSection(getContext().getObjectFileInfo()->getBSSSection());
  emitCodeAlignment(Align(4), &STI);

  switchSection(getContext().getObjectFileInfo()->getTextSection());
}

void MCWinCOFFStreamer::changeSection(MCSection *Section, uint32_t Subsection) {
  changeSectionImpl(Section, Subsection);
  // Ensure that the first and the second symbols relative to the section are
  // the section symbol and the COMDAT symbol.
  getAssembler().registerSymbol(*Section->getBeginSymbol());
  if (auto *Sym = cast<MCSectionCOFF>(Section)->getCOMDATSymbol())
    getAssembler().registerSymbol(*Sym);
}

void MCWinCOFFStreamer::emitLabel(MCSymbol *S, SMLoc Loc) {
  auto *Symbol = cast<MCSymbolCOFF>(S);
  MCObjectStreamer::emitLabel(Symbol, Loc);
}

void MCWinCOFFStreamer::emitAssemblerFlag(MCAssemblerFlag Flag) {
  // Let the target do whatever target specific stuff it needs to do.
  getAssembler().getBackend().handleAssemblerFlag(Flag);

  switch (Flag) {
  // None of these require COFF specific handling.
  case MCAF_SyntaxUnified:
  case MCAF_Code16:
  case MCAF_Code32:
  case MCAF_Code64:
    break;
  case MCAF_SubsectionsViaSymbols:
    llvm_unreachable("COFF doesn't support .subsections_via_symbols");
  }
}

void MCWinCOFFStreamer::emitThumbFunc(MCSymbol *Func) {
  llvm_unreachable("not implemented");
}

bool MCWinCOFFStreamer::emitSymbolAttribute(MCSymbol *S,
                                            MCSymbolAttr Attribute) {
  auto *Symbol = cast<MCSymbolCOFF>(S);
  getAssembler().registerSymbol(*Symbol);

  switch (Attribute) {
  default: return false;
  case MCSA_WeakReference:
  case MCSA_Weak:
    Symbol->setWeakExternalCharacteristics(COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS);
    Symbol->setExternal(true);
    break;
  case MCSA_WeakAntiDep:
    Symbol->setWeakExternalCharacteristics(COFF::IMAGE_WEAK_EXTERN_ANTI_DEPENDENCY);
    Symbol->setExternal(true);
    Symbol->setIsWeakExternal(true);
    break;
  case MCSA_Global:
    Symbol->setExternal(true);
    break;
  case MCSA_AltEntry:
    llvm_unreachable("COFF doesn't support the .alt_entry attribute");
  }

  return true;
}

void MCWinCOFFStreamer::emitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) {
  llvm_unreachable("not implemented");
}

void MCWinCOFFStreamer::beginCOFFSymbolDef(MCSymbol const *S) {
  auto *Symbol = cast<MCSymbolCOFF>(S);
  if (CurSymbol)
    Error("starting a new symbol definition without completing the "
          "previous one");
  CurSymbol = Symbol;
}

void MCWinCOFFStreamer::emitCOFFSymbolStorageClass(int StorageClass) {
  if (!CurSymbol) {
    Error("storage class specified outside of symbol definition");
    return;
  }

  if (StorageClass & ~COFF::SSC_Invalid) {
    Error("storage class value '" + Twine(StorageClass) +
               "' out of range");
    return;
  }

  getAssembler().registerSymbol(*CurSymbol);
  cast<MCSymbolCOFF>(CurSymbol)->setClass((uint16_t)StorageClass);
}

void MCWinCOFFStreamer::emitCOFFSymbolType(int Type) {
  if (!CurSymbol) {
    Error("symbol type specified outside of a symbol definition");
    return;
  }

  if (Type & ~0xffff) {
    Error("type value '" + Twine(Type) + "' out of range");
    return;
  }

  getAssembler().registerSymbol(*CurSymbol);
  cast<MCSymbolCOFF>(CurSymbol)->setType((uint16_t)Type);
}

void MCWinCOFFStreamer::endCOFFSymbolDef() {
  if (!CurSymbol)
    Error("ending symbol definition without starting one");
  CurSymbol = nullptr;
}

void MCWinCOFFStreamer::emitCOFFSafeSEH(MCSymbol const *Symbol) {
  // SafeSEH is a feature specific to 32-bit x86.  It does not exist (and is
  // unnecessary) on all platforms which use table-based exception dispatch.
  if (getContext().getTargetTriple().getArch() != Triple::x86)
    return;

  const MCSymbolCOFF *CSymbol = cast<MCSymbolCOFF>(Symbol);
  if (CSymbol->isSafeSEH())
    return;

  MCSection *SXData = getContext().getObjectFileInfo()->getSXDataSection();
  changeSection(SXData);
  SXData->ensureMinAlignment(Align(4));

  insert(getContext().allocFragment<MCSymbolIdFragment>(Symbol));
  getAssembler().registerSymbol(*Symbol);
  CSymbol->setIsSafeSEH();

  // The Microsoft linker requires that the symbol type of a handler be
  // function. Go ahead and oblige it here.
  CSymbol->setType(COFF::IMAGE_SYM_DTYPE_FUNCTION
                   << COFF::SCT_COMPLEX_TYPE_SHIFT);
}

void MCWinCOFFStreamer::emitCOFFSymbolIndex(MCSymbol const *Symbol) {
  MCSection *Sec = getCurrentSectionOnly();
  Sec->ensureMinAlignment(Align(4));

  insert(getContext().allocFragment<MCSymbolIdFragment>(Symbol));
  getAssembler().registerSymbol(*Symbol);
}

void MCWinCOFFStreamer::emitCOFFSectionIndex(const MCSymbol *Symbol) {
  visitUsedSymbol(*Symbol);
  MCDataFragment *DF = getOrCreateDataFragment();
  const MCSymbolRefExpr *SRE = MCSymbolRefExpr::create(Symbol, getContext());
  MCFixup Fixup = MCFixup::create(DF->getContents().size(), SRE, FK_SecRel_2);
  DF->getFixups().push_back(Fixup);
  DF->getContents().resize(DF->getContents().size() + 2, 0);
}

void MCWinCOFFStreamer::emitCOFFSecRel32(const MCSymbol *Symbol,
                                         uint64_t Offset) {
  visitUsedSymbol(*Symbol);
  MCDataFragment *DF = getOrCreateDataFragment();
  // Create Symbol A for the relocation relative reference.
  const MCExpr *MCE = MCSymbolRefExpr::create(Symbol, getContext());
  // Add the constant offset, if given.
  if (Offset)
    MCE = MCBinaryExpr::createAdd(
        MCE, MCConstantExpr::create(Offset, getContext()), getContext());
  // Build the secrel32 relocation.
  MCFixup Fixup = MCFixup::create(DF->getContents().size(), MCE, FK_SecRel_4);
  // Record the relocation.
  DF->getFixups().push_back(Fixup);
  // Emit 4 bytes (zeros) to the object file.
  DF->getContents().resize(DF->getContents().size() + 4, 0);
}

void MCWinCOFFStreamer::emitCOFFImgRel32(const MCSymbol *Symbol,
                                         int64_t Offset) {
  visitUsedSymbol(*Symbol);
  MCDataFragment *DF = getOrCreateDataFragment();
  // Create Symbol A for the relocation relative reference.
  const MCExpr *MCE = MCSymbolRefExpr::create(
      Symbol, MCSymbolRefExpr::VK_COFF_IMGREL32, getContext());
  // Add the constant offset, if given.
  if (Offset)
    MCE = MCBinaryExpr::createAdd(
        MCE, MCConstantExpr::create(Offset, getContext()), getContext());
  // Build the imgrel relocation.
  MCFixup Fixup = MCFixup::create(DF->getContents().size(), MCE, FK_Data_4);
  // Record the relocation.
  DF->getFixups().push_back(Fixup);
  // Emit 4 bytes (zeros) to the object file.
  DF->getContents().resize(DF->getContents().size() + 4, 0);
}

void MCWinCOFFStreamer::emitCommonSymbol(MCSymbol *S, uint64_t Size,
                                         Align ByteAlignment) {
  auto *Symbol = cast<MCSymbolCOFF>(S);

  const Triple &T = getContext().getTargetTriple();
  if (T.isWindowsMSVCEnvironment()) {
    if (ByteAlignment > 32)
      report_fatal_error("alignment is limited to 32-bytes");

    // Round size up to alignment so that we will honor the alignment request.
    Size = std::max(Size, ByteAlignment.value());
  }

  getAssembler().registerSymbol(*Symbol);
  Symbol->setExternal(true);
  Symbol->setCommon(Size, ByteAlignment);

  if (!T.isWindowsMSVCEnvironment() && ByteAlignment > 1) {
    SmallString<128> Directive;
    raw_svector_ostream OS(Directive);
    const MCObjectFileInfo *MFI = getContext().getObjectFileInfo();

    OS << " -aligncomm:\"" << Symbol->getName() << "\","
       << Log2_32_Ceil(ByteAlignment.value());

    pushSection();
    switchSection(MFI->getDrectveSection());
    emitBytes(Directive);
    popSection();
  }
}

void MCWinCOFFStreamer::emitLocalCommonSymbol(MCSymbol *S, uint64_t Size,
                                              Align ByteAlignment) {
  auto *Symbol = cast<MCSymbolCOFF>(S);

  MCSection *Section = getContext().getObjectFileInfo()->getBSSSection();
  pushSection();
  switchSection(Section);
  emitValueToAlignment(ByteAlignment, 0, 1, 0);
  emitLabel(Symbol);
  Symbol->setExternal(false);
  emitZeros(Size);
  popSection();
}

void MCWinCOFFStreamer::emitWeakReference(MCSymbol *AliasS,
                                          const MCSymbol *Symbol) {
  auto *Alias = cast<MCSymbolCOFF>(AliasS);
  emitSymbolAttribute(Alias, MCSA_Weak);

  getAssembler().registerSymbol(*Symbol);
  Alias->setVariableValue(MCSymbolRefExpr::create(
      Symbol, MCSymbolRefExpr::VK_WEAKREF, getContext()));
}

void MCWinCOFFStreamer::emitZerofill(MCSection *Section, MCSymbol *Symbol,
                                     uint64_t Size, Align ByteAlignment,
                                     SMLoc Loc) {
  llvm_unreachable("not implemented");
}

void MCWinCOFFStreamer::emitTBSSSymbol(MCSection *Section, MCSymbol *Symbol,
                                       uint64_t Size, Align ByteAlignment) {
  llvm_unreachable("not implemented");
}

// TODO: Implement this if you want to emit .comment section in COFF obj files.
void MCWinCOFFStreamer::emitIdent(StringRef IdentString) {
  llvm_unreachable("not implemented");
}

void MCWinCOFFStreamer::emitWinEHHandlerData(SMLoc Loc) {
  llvm_unreachable("not implemented");
}

void MCWinCOFFStreamer::emitCGProfileEntry(const MCSymbolRefExpr *From,
                                           const MCSymbolRefExpr *To,
                                           uint64_t Count) {
  // Ignore temporary symbols for now.
  if (!From->getSymbol().isTemporary() && !To->getSymbol().isTemporary())
    getWriter().getCGProfile().push_back({From, To, Count});
}

void MCWinCOFFStreamer::finalizeCGProfileEntry(const MCSymbolRefExpr *&SRE) {
  const MCSymbol *S = &SRE->getSymbol();
  if (getAssembler().registerSymbol(*S))
    cast<MCSymbolCOFF>(S)->setExternal(true);
}

void MCWinCOFFStreamer::finishImpl() {
  MCAssembler &Asm = getAssembler();
  if (Asm.getWriter().getEmitAddrsigSection()) {
    // Register the section.
    switchSection(Asm.getContext().getCOFFSection(".llvm_addrsig",
                                                  COFF::IMAGE_SCN_LNK_REMOVE));
  }
  if (!Asm.getWriter().getCGProfile().empty()) {
    for (auto &E : Asm.getWriter().getCGProfile()) {
      finalizeCGProfileEntry(E.From);
      finalizeCGProfileEntry(E.To);
    }
    switchSection(Asm.getContext().getCOFFSection(".llvm.call-graph-profile",
                                                  COFF::IMAGE_SCN_LNK_REMOVE));
  }

  MCObjectStreamer::finishImpl();
}

void MCWinCOFFStreamer::Error(const Twine &Msg) const {
  getContext().reportError(SMLoc(), Msg);
}
