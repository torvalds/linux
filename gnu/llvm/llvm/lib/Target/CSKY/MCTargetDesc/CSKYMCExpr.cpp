//===-- CSKYMCExpr.cpp - CSKY specific MC expression classes -*- C++ -*----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CSKYMCExpr.h"
#include "CSKYFixupKinds.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

#define DEBUG_TYPE "csky-mc-expr"

const CSKYMCExpr *CSKYMCExpr::create(const MCExpr *Expr, VariantKind Kind,
                                     MCContext &Ctx) {
  return new (Ctx) CSKYMCExpr(Kind, Expr);
}

StringRef CSKYMCExpr::getVariantKindName(VariantKind Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("Invalid ELF symbol kind");
  case VK_CSKY_None:
  case VK_CSKY_ADDR:
    return "";
  case VK_CSKY_ADDR_HI16:
    return "@HI16";
  case VK_CSKY_ADDR_LO16:
    return "@LO16";
  case VK_CSKY_GOT_IMM18_BY4:
  case VK_CSKY_GOT:
    return "@GOT";
  case VK_CSKY_GOTPC:
    return "@GOTPC";
  case VK_CSKY_GOTOFF:
    return "@GOTOFF";
  case VK_CSKY_PLT_IMM18_BY4:
  case VK_CSKY_PLT:
    return "@PLT";
  case VK_CSKY_TLSLE:
    return "@TPOFF";
  case VK_CSKY_TLSIE:
    return "@GOTTPOFF";
  case VK_CSKY_TLSGD:
    return "@TLSGD32";
  case VK_CSKY_TLSLDO:
    return "@TLSLDO32";
  case VK_CSKY_TLSLDM:
    return "@TLSLDM32";
  }
}

void CSKYMCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*getSubExpr());
}

void CSKYMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  Expr->print(OS, MAI);
  OS << getVariantKindName(getKind());
}

static void fixELFSymbolsInTLSFixupsImpl(const MCExpr *Expr, MCAssembler &Asm) {
  switch (Expr->getKind()) {
  case MCExpr::Target:
    llvm_unreachable("Can't handle nested target expression");
    break;
  case MCExpr::Constant:
    break;

  case MCExpr::Binary: {
    const MCBinaryExpr *BE = cast<MCBinaryExpr>(Expr);
    fixELFSymbolsInTLSFixupsImpl(BE->getLHS(), Asm);
    fixELFSymbolsInTLSFixupsImpl(BE->getRHS(), Asm);
    break;
  }

  case MCExpr::SymbolRef: {
    // We're known to be under a TLS fixup, so any symbol should be
    // modified. There should be only one.
    const MCSymbolRefExpr &SymRef = *cast<MCSymbolRefExpr>(Expr);
    cast<MCSymbolELF>(SymRef.getSymbol()).setType(ELF::STT_TLS);
    break;
  }

  case MCExpr::Unary:
    fixELFSymbolsInTLSFixupsImpl(cast<MCUnaryExpr>(Expr)->getSubExpr(), Asm);
    break;
  }
}

void CSKYMCExpr::fixELFSymbolsInTLSFixups(MCAssembler &Asm) const {
  switch (getKind()) {
  default:
    return;
  case VK_CSKY_TLSLE:
  case VK_CSKY_TLSIE:
  case VK_CSKY_TLSGD:
    break;
  }

  fixELFSymbolsInTLSFixupsImpl(getSubExpr(), Asm);
}

bool CSKYMCExpr::evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                           const MCFixup *Fixup) const {
  if (!getSubExpr()->evaluateAsRelocatable(Res, Asm, Fixup))
    return false;

  // Some custom fixup types are not valid with symbol difference expressions
  if (Res.getSymA() && Res.getSymB()) {
    switch (getKind()) {
    default:
      return true;
    case VK_CSKY_GOT:
    case VK_CSKY_GOT_IMM18_BY4:
    case VK_CSKY_GOTPC:
    case VK_CSKY_GOTOFF:
    case VK_CSKY_PLT:
    case VK_CSKY_PLT_IMM18_BY4:
    case VK_CSKY_TLSIE:
    case VK_CSKY_TLSLE:
    case VK_CSKY_TLSGD:
    case VK_CSKY_TLSLDO:
    case VK_CSKY_TLSLDM:
      return false;
    }
  }

  return true;
}
