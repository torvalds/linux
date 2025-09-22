//===-- MipsMCExpr.cpp - Mips specific MC expression classes --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MipsMCExpr.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "mipsmcexpr"

const MipsMCExpr *MipsMCExpr::create(MipsMCExpr::MipsExprKind Kind,
                                     const MCExpr *Expr, MCContext &Ctx) {
  return new (Ctx) MipsMCExpr(Kind, Expr);
}

const MipsMCExpr *MipsMCExpr::createGpOff(MipsMCExpr::MipsExprKind Kind,
                                          const MCExpr *Expr, MCContext &Ctx) {
  return create(Kind, create(MEK_NEG, create(MEK_GPREL, Expr, Ctx), Ctx), Ctx);
}

void MipsMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  int64_t AbsVal;

  switch (Kind) {
  case MEK_None:
  case MEK_Special:
    llvm_unreachable("MEK_None and MEK_Special are invalid");
    break;
  case MEK_DTPREL:
    // MEK_DTPREL is used for marking TLS DIEExpr only
    // and contains a regular sub-expression.
    getSubExpr()->print(OS, MAI, true);
    return;
  case MEK_CALL_HI16:
    OS << "%call_hi";
    break;
  case MEK_CALL_LO16:
    OS << "%call_lo";
    break;
  case MEK_DTPREL_HI:
    OS << "%dtprel_hi";
    break;
  case MEK_DTPREL_LO:
    OS << "%dtprel_lo";
    break;
  case MEK_GOT:
    OS << "%got";
    break;
  case MEK_GOTTPREL:
    OS << "%gottprel";
    break;
  case MEK_GOT_CALL:
    OS << "%call16";
    break;
  case MEK_GOT_DISP:
    OS << "%got_disp";
    break;
  case MEK_GOT_HI16:
    OS << "%got_hi";
    break;
  case MEK_GOT_LO16:
    OS << "%got_lo";
    break;
  case MEK_GOT_PAGE:
    OS << "%got_page";
    break;
  case MEK_GOT_OFST:
    OS << "%got_ofst";
    break;
  case MEK_GPREL:
    OS << "%gp_rel";
    break;
  case MEK_HI:
    OS << "%hi";
    break;
  case MEK_HIGHER:
    OS << "%higher";
    break;
  case MEK_HIGHEST:
    OS << "%highest";
    break;
  case MEK_LO:
    OS << "%lo";
    break;
  case MEK_NEG:
    OS << "%neg";
    break;
  case MEK_PCREL_HI16:
    OS << "%pcrel_hi";
    break;
  case MEK_PCREL_LO16:
    OS << "%pcrel_lo";
    break;
  case MEK_TLSGD:
    OS << "%tlsgd";
    break;
  case MEK_TLSLDM:
    OS << "%tlsldm";
    break;
  case MEK_TPREL_HI:
    OS << "%tprel_hi";
    break;
  case MEK_TPREL_LO:
    OS << "%tprel_lo";
    break;
  }

  OS << '(';
  if (Expr->evaluateAsAbsolute(AbsVal))
    OS << AbsVal;
  else
    Expr->print(OS, MAI, true);
  OS << ')';
}

bool MipsMCExpr::evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                           const MCFixup *Fixup) const {
  // Look for the %hi(%neg(%gp_rel(X))) and %lo(%neg(%gp_rel(X))) special cases.
  if (isGpOff()) {
    const MCExpr *SubExpr =
        cast<MipsMCExpr>(cast<MipsMCExpr>(getSubExpr())->getSubExpr())
            ->getSubExpr();
    if (!SubExpr->evaluateAsRelocatable(Res, Asm, Fixup))
      return false;

    Res = MCValue::get(Res.getSymA(), Res.getSymB(), Res.getConstant(),
                       MEK_Special);
    return true;
  }

  if (!getSubExpr()->evaluateAsRelocatable(Res, Asm, Fixup))
    return false;

  if (Res.getRefKind() != MCSymbolRefExpr::VK_None)
    return false;

  // evaluateAsAbsolute() and evaluateAsValue() require that we evaluate the
  // %hi/%lo/etc. here. Fixup is a null pointer when either of these is the
  // caller.
  if (Res.isAbsolute() && Fixup == nullptr) {
    int64_t AbsVal = Res.getConstant();
    switch (Kind) {
    case MEK_None:
    case MEK_Special:
      llvm_unreachable("MEK_None and MEK_Special are invalid");
    case MEK_DTPREL:
      // MEK_DTPREL is used for marking TLS DIEExpr only
      // and contains a regular sub-expression.
      return getSubExpr()->evaluateAsRelocatable(Res, Asm, Fixup);
    case MEK_DTPREL_HI:
    case MEK_DTPREL_LO:
    case MEK_GOT:
    case MEK_GOTTPREL:
    case MEK_GOT_CALL:
    case MEK_GOT_DISP:
    case MEK_GOT_HI16:
    case MEK_GOT_LO16:
    case MEK_GOT_OFST:
    case MEK_GOT_PAGE:
    case MEK_GPREL:
    case MEK_PCREL_HI16:
    case MEK_PCREL_LO16:
    case MEK_TLSGD:
    case MEK_TLSLDM:
    case MEK_TPREL_HI:
    case MEK_TPREL_LO:
      return false;
    case MEK_LO:
    case MEK_CALL_LO16:
      AbsVal = SignExtend64<16>(AbsVal);
      break;
    case MEK_CALL_HI16:
    case MEK_HI:
      AbsVal = SignExtend64<16>((AbsVal + 0x8000) >> 16);
      break;
    case MEK_HIGHER:
      AbsVal = SignExtend64<16>((AbsVal + 0x80008000LL) >> 32);
      break;
    case MEK_HIGHEST:
      AbsVal = SignExtend64<16>((AbsVal + 0x800080008000LL) >> 48);
      break;
    case MEK_NEG:
      AbsVal = -AbsVal;
      break;
    }
    Res = MCValue::get(AbsVal);
    return true;
  }

  // We want to defer it for relocatable expressions since the constant is
  // applied to the whole symbol value.
  //
  // The value of getKind() that is given to MCValue is only intended to aid
  // debugging when inspecting MCValue objects. It shouldn't be relied upon
  // for decision making.
  Res =
      MCValue::get(Res.getSymA(), Res.getSymB(), Res.getConstant(), getKind());

  return true;
}

void MipsMCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*getSubExpr());
}

static void fixELFSymbolsInTLSFixupsImpl(const MCExpr *Expr, MCAssembler &Asm) {
  switch (Expr->getKind()) {
  case MCExpr::Target:
    fixELFSymbolsInTLSFixupsImpl(cast<MipsMCExpr>(Expr)->getSubExpr(), Asm);
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

void MipsMCExpr::fixELFSymbolsInTLSFixups(MCAssembler &Asm) const {
  switch (getKind()) {
  case MEK_None:
  case MEK_Special:
    llvm_unreachable("MEK_None and MEK_Special are invalid");
    break;
  case MEK_CALL_HI16:
  case MEK_CALL_LO16:
  case MEK_GOT:
  case MEK_GOT_CALL:
  case MEK_GOT_DISP:
  case MEK_GOT_HI16:
  case MEK_GOT_LO16:
  case MEK_GOT_OFST:
  case MEK_GOT_PAGE:
  case MEK_GPREL:
  case MEK_HI:
  case MEK_HIGHER:
  case MEK_HIGHEST:
  case MEK_LO:
  case MEK_NEG:
  case MEK_PCREL_HI16:
  case MEK_PCREL_LO16:
    // If we do have nested target-specific expressions, they will be in
    // a consecutive chain.
    if (const MipsMCExpr *E = dyn_cast<const MipsMCExpr>(getSubExpr()))
      E->fixELFSymbolsInTLSFixups(Asm);
    break;
  case MEK_DTPREL:
  case MEK_DTPREL_HI:
  case MEK_DTPREL_LO:
  case MEK_TLSLDM:
  case MEK_TLSGD:
  case MEK_GOTTPREL:
  case MEK_TPREL_HI:
  case MEK_TPREL_LO:
    fixELFSymbolsInTLSFixupsImpl(getSubExpr(), Asm);
    break;
  }
}

bool MipsMCExpr::isGpOff(MipsExprKind &Kind) const {
  if (getKind() == MEK_HI || getKind() == MEK_LO) {
    if (const MipsMCExpr *S1 = dyn_cast<const MipsMCExpr>(getSubExpr())) {
      if (const MipsMCExpr *S2 = dyn_cast<const MipsMCExpr>(S1->getSubExpr())) {
        if (S1->getKind() == MEK_NEG && S2->getKind() == MEK_GPREL) {
          Kind = getKind();
          return true;
        }
      }
    }
  }
  return false;
}
