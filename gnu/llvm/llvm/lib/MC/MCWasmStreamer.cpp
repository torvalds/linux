//===- lib/MC/MCWasmStreamer.cpp - Wasm Object Output ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file assembles .s files and emits Wasm .o object files.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCWasmStreamer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class MCContext;
class MCStreamer;
class MCSubtargetInfo;
} // namespace llvm

using namespace llvm;

MCWasmStreamer::~MCWasmStreamer() = default; // anchor.

void MCWasmStreamer::emitLabel(MCSymbol *S, SMLoc Loc) {
  auto *Symbol = cast<MCSymbolWasm>(S);
  MCObjectStreamer::emitLabel(Symbol, Loc);

  const MCSectionWasm &Section =
      static_cast<const MCSectionWasm &>(*getCurrentSectionOnly());
  if (Section.getSegmentFlags() & wasm::WASM_SEG_FLAG_TLS)
    Symbol->setTLS();
}

void MCWasmStreamer::emitLabelAtPos(MCSymbol *S, SMLoc Loc, MCDataFragment &F,
                                    uint64_t Offset) {
  auto *Symbol = cast<MCSymbolWasm>(S);
  MCObjectStreamer::emitLabelAtPos(Symbol, Loc, F, Offset);

  const MCSectionWasm &Section =
      static_cast<const MCSectionWasm &>(*getCurrentSectionOnly());
  if (Section.getSegmentFlags() & wasm::WASM_SEG_FLAG_TLS)
    Symbol->setTLS();
}

void MCWasmStreamer::emitAssemblerFlag(MCAssemblerFlag Flag) {
  // Let the target do whatever target specific stuff it needs to do.
  getAssembler().getBackend().handleAssemblerFlag(Flag);

  // Do any generic stuff we need to do.
  llvm_unreachable("invalid assembler flag!");
}

void MCWasmStreamer::changeSection(MCSection *Section, uint32_t Subsection) {
  MCAssembler &Asm = getAssembler();
  auto *SectionWasm = cast<MCSectionWasm>(Section);
  const MCSymbol *Grp = SectionWasm->getGroup();
  if (Grp)
    Asm.registerSymbol(*Grp);

  this->MCObjectStreamer::changeSection(Section, Subsection);
  Asm.registerSymbol(*Section->getBeginSymbol());
}

void MCWasmStreamer::emitWeakReference(MCSymbol *Alias,
                                       const MCSymbol *Symbol) {
  getAssembler().registerSymbol(*Symbol);
  const MCExpr *Value = MCSymbolRefExpr::create(
      Symbol, MCSymbolRefExpr::VK_WEAKREF, getContext());
  Alias->setVariableValue(Value);
}

bool MCWasmStreamer::emitSymbolAttribute(MCSymbol *S, MCSymbolAttr Attribute) {
  assert(Attribute != MCSA_IndirectSymbol && "indirect symbols not supported");

  auto *Symbol = cast<MCSymbolWasm>(S);

  // Adding a symbol attribute always introduces the symbol; note that an
  // important side effect of calling registerSymbol here is to register the
  // symbol with the assembler.
  getAssembler().registerSymbol(*Symbol);

  switch (Attribute) {
  case MCSA_LazyReference:
  case MCSA_Reference:
  case MCSA_SymbolResolver:
  case MCSA_PrivateExtern:
  case MCSA_WeakDefinition:
  case MCSA_WeakDefAutoPrivate:
  case MCSA_Invalid:
  case MCSA_IndirectSymbol:
  case MCSA_Protected:
  case MCSA_Exported:
    return false;

  case MCSA_Hidden:
    Symbol->setHidden(true);
    break;

  case MCSA_Weak:
  case MCSA_WeakReference:
    Symbol->setWeak(true);
    Symbol->setExternal(true);
    break;

  case MCSA_Global:
    Symbol->setExternal(true);
    break;

  case MCSA_ELF_TypeFunction:
    Symbol->setType(wasm::WASM_SYMBOL_TYPE_FUNCTION);
    break;

  case MCSA_ELF_TypeTLS:
    Symbol->setTLS();
    break;

  case MCSA_ELF_TypeObject:
  case MCSA_Cold:
    break;

  case MCSA_NoDeadStrip:
    Symbol->setNoStrip();
    break;

  default:
    // unrecognized directive
    llvm_unreachable("unexpected MCSymbolAttr");
    return false;
  }

  return true;
}

void MCWasmStreamer::emitCommonSymbol(MCSymbol *S, uint64_t Size,
                                      Align ByteAlignment) {
  llvm_unreachable("Common symbols are not yet implemented for Wasm");
}

void MCWasmStreamer::emitELFSize(MCSymbol *Symbol, const MCExpr *Value) {
  cast<MCSymbolWasm>(Symbol)->setSize(Value);
}

void MCWasmStreamer::emitLocalCommonSymbol(MCSymbol *S, uint64_t Size,
                                           Align ByteAlignment) {
  llvm_unreachable("Local common symbols are not yet implemented for Wasm");
}

void MCWasmStreamer::emitIdent(StringRef IdentString) {
  // TODO(sbc): Add the ident section once we support mergable strings
  // sections in the object format
}

void MCWasmStreamer::emitInstToFragment(const MCInst &Inst,
                                        const MCSubtargetInfo &STI) {
  this->MCObjectStreamer::emitInstToFragment(Inst, STI);
  MCRelaxableFragment &F = *cast<MCRelaxableFragment>(getCurrentFragment());

  for (auto &Fixup : F.getFixups())
    fixSymbolsInTLSFixups(Fixup.getValue());
}

void MCWasmStreamer::emitInstToData(const MCInst &Inst,
                                    const MCSubtargetInfo &STI) {
  MCAssembler &Assembler = getAssembler();
  SmallVector<MCFixup, 4> Fixups;
  SmallString<256> Code;
  Assembler.getEmitter().encodeInstruction(Inst, Code, Fixups, STI);

  for (auto &Fixup : Fixups)
    fixSymbolsInTLSFixups(Fixup.getValue());

  // Append the encoded instruction to the current data fragment (or create a
  // new such fragment if the current fragment is not a data fragment).
  MCDataFragment *DF = getOrCreateDataFragment();

  // Add the fixups and data.
  for (unsigned I = 0, E = Fixups.size(); I != E; ++I) {
    Fixups[I].setOffset(Fixups[I].getOffset() + DF->getContents().size());
    DF->getFixups().push_back(Fixups[I]);
  }
  DF->setHasInstructions(STI);
  DF->getContents().append(Code.begin(), Code.end());
}

void MCWasmStreamer::finishImpl() {
  emitFrames(nullptr);

  this->MCObjectStreamer::finishImpl();
}

void MCWasmStreamer::fixSymbolsInTLSFixups(const MCExpr *expr) {
  switch (expr->getKind()) {
  case MCExpr::Target:
  case MCExpr::Constant:
    break;

  case MCExpr::Binary: {
    const MCBinaryExpr *be = cast<MCBinaryExpr>(expr);
    fixSymbolsInTLSFixups(be->getLHS());
    fixSymbolsInTLSFixups(be->getRHS());
    break;
  }

  case MCExpr::SymbolRef: {
    const MCSymbolRefExpr &symRef = *cast<MCSymbolRefExpr>(expr);
    switch (symRef.getKind()) {
    case MCSymbolRefExpr::VK_WASM_TLSREL:
    case MCSymbolRefExpr::VK_WASM_GOT_TLS:
      getAssembler().registerSymbol(symRef.getSymbol());
      cast<MCSymbolWasm>(symRef.getSymbol()).setTLS();
      break;
    default:
      break;
    }
    break;
  }

  case MCExpr::Unary:
    fixSymbolsInTLSFixups(cast<MCUnaryExpr>(expr)->getSubExpr());
    break;
  }
}

void MCWasmStreamer::emitThumbFunc(MCSymbol *Func) {
  llvm_unreachable("Generic Wasm doesn't support this directive");
}

void MCWasmStreamer::emitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) {
  llvm_unreachable("Wasm doesn't support this directive");
}

void MCWasmStreamer::emitZerofill(MCSection *Section, MCSymbol *Symbol,
                                  uint64_t Size, Align ByteAlignment,
                                  SMLoc Loc) {
  llvm_unreachable("Wasm doesn't support this directive");
}

void MCWasmStreamer::emitTBSSSymbol(MCSection *Section, MCSymbol *Symbol,
                                    uint64_t Size, Align ByteAlignment) {
  llvm_unreachable("Wasm doesn't support this directive");
}

MCStreamer *llvm::createWasmStreamer(MCContext &Context,
                                     std::unique_ptr<MCAsmBackend> &&MAB,
                                     std::unique_ptr<MCObjectWriter> &&OW,
                                     std::unique_ptr<MCCodeEmitter> &&CE) {
  MCWasmStreamer *S =
      new MCWasmStreamer(Context, std::move(MAB), std::move(OW), std::move(CE));
  return S;
}
