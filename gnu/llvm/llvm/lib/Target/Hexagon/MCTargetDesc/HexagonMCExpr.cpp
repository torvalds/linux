//===-- HexagonMCExpr.cpp - Hexagon specific MC expression classes
//----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HexagonMCExpr.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "hexagon-mcexpr"

HexagonMCExpr *HexagonMCExpr::create(MCExpr const *Expr, MCContext &Ctx) {
  return new (Ctx) HexagonMCExpr(Expr);
}

bool HexagonMCExpr::evaluateAsRelocatableImpl(MCValue &Res,
                                              const MCAssembler *Asm,
                                              MCFixup const *Fixup) const {
  return Expr->evaluateAsRelocatable(Res, Asm, Fixup);
}

void HexagonMCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*Expr);
}

MCFragment *llvm::HexagonMCExpr::findAssociatedFragment() const {
  return Expr->findAssociatedFragment();
}

static void fixELFSymbolsInTLSFixupsImpl(const MCExpr *Expr, MCAssembler &Asm) {
  switch (Expr->getKind()) {
  case MCExpr::Target:
    llvm_unreachable("Cannot handle nested target MCExpr");
    break;
  case MCExpr::Constant:
    break;

  case MCExpr::Binary: {
    const MCBinaryExpr *be = cast<MCBinaryExpr>(Expr);
    fixELFSymbolsInTLSFixupsImpl(be->getLHS(), Asm);
    fixELFSymbolsInTLSFixupsImpl(be->getRHS(), Asm);
    break;
  }
  case MCExpr::SymbolRef: {
    const MCSymbolRefExpr &symRef = *cast<MCSymbolRefExpr>(Expr);
    switch (symRef.getKind()) {
    default:
      return;
    case MCSymbolRefExpr::VK_Hexagon_GD_GOT:
    case MCSymbolRefExpr::VK_Hexagon_LD_GOT:
    case MCSymbolRefExpr::VK_Hexagon_GD_PLT:
    case MCSymbolRefExpr::VK_Hexagon_LD_PLT:
    case MCSymbolRefExpr::VK_Hexagon_IE:
    case MCSymbolRefExpr::VK_Hexagon_IE_GOT:
    case MCSymbolRefExpr::VK_TPREL:
      break;
    }
    cast<MCSymbolELF>(symRef.getSymbol()).setType(ELF::STT_TLS);
    break;
  }
  case MCExpr::Unary:
    fixELFSymbolsInTLSFixupsImpl(cast<MCUnaryExpr>(Expr)->getSubExpr(), Asm);
    break;
  }
}

void HexagonMCExpr::fixELFSymbolsInTLSFixups(MCAssembler &Asm) const {
  auto expr = getExpr();
  fixELFSymbolsInTLSFixupsImpl(expr, Asm);
}

MCExpr const *HexagonMCExpr::getExpr() const { return Expr; }

void HexagonMCExpr::setMustExtend(bool Val) {
  assert((!Val || !MustNotExtend) && "Extension contradiction");
  MustExtend = Val;
}

bool HexagonMCExpr::mustExtend() const { return MustExtend; }
void HexagonMCExpr::setMustNotExtend(bool Val) {
  assert((!Val || !MustExtend) && "Extension contradiction");
  MustNotExtend = Val;
}
bool HexagonMCExpr::mustNotExtend() const { return MustNotExtend; }

bool HexagonMCExpr::s27_2_reloc() const { return S27_2_reloc; }
void HexagonMCExpr::setS27_2_reloc(bool Val) {
  S27_2_reloc = Val;
}

HexagonMCExpr::HexagonMCExpr(MCExpr const *Expr)
    : Expr(Expr), MustNotExtend(false), MustExtend(false), S27_2_reloc(false),
      SignMismatch(false) {}

void HexagonMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  Expr->print(OS, MAI);
}

void HexagonMCExpr::setSignMismatch(bool Val) {
  SignMismatch = Val;
}

bool HexagonMCExpr::signMismatch() const {
  return SignMismatch;
}
