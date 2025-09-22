//===-- LoongArchMCExpr.cpp - LoongArch specific MC expression classes ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the assembly expression modifiers
// accepted by the LoongArch architecture.
//
//===----------------------------------------------------------------------===//

#include "LoongArchMCExpr.h"
#include "LoongArchAsmBackend.h"
#include "LoongArchFixupKinds.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "loongarch-mcexpr"

const LoongArchMCExpr *LoongArchMCExpr::create(const MCExpr *Expr,
                                               VariantKind Kind, MCContext &Ctx,
                                               bool Hint) {
  return new (Ctx) LoongArchMCExpr(Expr, Kind, Hint);
}

void LoongArchMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  VariantKind Kind = getKind();
  bool HasVariant =
      ((Kind != VK_LoongArch_None) && (Kind != VK_LoongArch_CALL));

  if (HasVariant)
    OS << '%' << getVariantKindName(getKind()) << '(';
  Expr->print(OS, MAI);
  if (HasVariant)
    OS << ')';
}

bool LoongArchMCExpr::evaluateAsRelocatableImpl(MCValue &Res,
                                                const MCAssembler *Asm,
                                                const MCFixup *Fixup) const {
  // Explicitly drop the layout and assembler to prevent any symbolic folding in
  // the expression handling.  This is required to preserve symbolic difference
  // expressions to emit the paired relocations.
  if (!getSubExpr()->evaluateAsRelocatable(Res, nullptr, nullptr))
    return false;

  Res =
      MCValue::get(Res.getSymA(), Res.getSymB(), Res.getConstant(), getKind());
  // Custom fixup types are not valid with symbol difference expressions.
  return Res.getSymB() ? getKind() == VK_LoongArch_None : true;
}

void LoongArchMCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*getSubExpr());
}

StringRef LoongArchMCExpr::getVariantKindName(VariantKind Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("Invalid ELF symbol kind");
  case VK_LoongArch_CALL_PLT:
    return "plt";
  case VK_LoongArch_B16:
    return "b16";
  case VK_LoongArch_B21:
    return "b21";
  case VK_LoongArch_B26:
    return "b26";
  case VK_LoongArch_ABS_HI20:
    return "abs_hi20";
  case VK_LoongArch_ABS_LO12:
    return "abs_lo12";
  case VK_LoongArch_ABS64_LO20:
    return "abs64_lo20";
  case VK_LoongArch_ABS64_HI12:
    return "abs64_hi12";
  case VK_LoongArch_PCALA_HI20:
    return "pc_hi20";
  case VK_LoongArch_PCALA_LO12:
    return "pc_lo12";
  case VK_LoongArch_PCALA64_LO20:
    return "pc64_lo20";
  case VK_LoongArch_PCALA64_HI12:
    return "pc64_hi12";
  case VK_LoongArch_GOT_PC_HI20:
    return "got_pc_hi20";
  case VK_LoongArch_GOT_PC_LO12:
    return "got_pc_lo12";
  case VK_LoongArch_GOT64_PC_LO20:
    return "got64_pc_lo20";
  case VK_LoongArch_GOT64_PC_HI12:
    return "got64_pc_hi12";
  case VK_LoongArch_GOT_HI20:
    return "got_hi20";
  case VK_LoongArch_GOT_LO12:
    return "got_lo12";
  case VK_LoongArch_GOT64_LO20:
    return "got64_lo20";
  case VK_LoongArch_GOT64_HI12:
    return "got64_hi12";
  case VK_LoongArch_TLS_LE_HI20:
    return "le_hi20";
  case VK_LoongArch_TLS_LE_LO12:
    return "le_lo12";
  case VK_LoongArch_TLS_LE64_LO20:
    return "le64_lo20";
  case VK_LoongArch_TLS_LE64_HI12:
    return "le64_hi12";
  case VK_LoongArch_TLS_IE_PC_HI20:
    return "ie_pc_hi20";
  case VK_LoongArch_TLS_IE_PC_LO12:
    return "ie_pc_lo12";
  case VK_LoongArch_TLS_IE64_PC_LO20:
    return "ie64_pc_lo20";
  case VK_LoongArch_TLS_IE64_PC_HI12:
    return "ie64_pc_hi12";
  case VK_LoongArch_TLS_IE_HI20:
    return "ie_hi20";
  case VK_LoongArch_TLS_IE_LO12:
    return "ie_lo12";
  case VK_LoongArch_TLS_IE64_LO20:
    return "ie64_lo20";
  case VK_LoongArch_TLS_IE64_HI12:
    return "ie64_hi12";
  case VK_LoongArch_TLS_LD_PC_HI20:
    return "ld_pc_hi20";
  case VK_LoongArch_TLS_LD_HI20:
    return "ld_hi20";
  case VK_LoongArch_TLS_GD_PC_HI20:
    return "gd_pc_hi20";
  case VK_LoongArch_TLS_GD_HI20:
    return "gd_hi20";
  case VK_LoongArch_CALL36:
    return "call36";
  case VK_LoongArch_TLS_DESC_PC_HI20:
    return "desc_pc_hi20";
  case VK_LoongArch_TLS_DESC_PC_LO12:
    return "desc_pc_lo12";
  case VK_LoongArch_TLS_DESC64_PC_LO20:
    return "desc64_pc_lo20";
  case VK_LoongArch_TLS_DESC64_PC_HI12:
    return "desc64_pc_hi12";
  case VK_LoongArch_TLS_DESC_HI20:
    return "desc_hi20";
  case VK_LoongArch_TLS_DESC_LO12:
    return "desc_lo12";
  case VK_LoongArch_TLS_DESC64_LO20:
    return "desc64_lo20";
  case VK_LoongArch_TLS_DESC64_HI12:
    return "desc64_hi12";
  case VK_LoongArch_TLS_DESC_LD:
    return "desc_ld";
  case VK_LoongArch_TLS_DESC_CALL:
    return "desc_call";
  case VK_LoongArch_TLS_LE_HI20_R:
    return "le_hi20_r";
  case VK_LoongArch_TLS_LE_ADD_R:
    return "le_add_r";
  case VK_LoongArch_TLS_LE_LO12_R:
    return "le_lo12_r";
  case VK_LoongArch_PCREL20_S2:
    return "pcrel_20";
  case VK_LoongArch_TLS_LD_PCREL20_S2:
    return "ld_pcrel_20";
  case VK_LoongArch_TLS_GD_PCREL20_S2:
    return "gd_pcrel_20";
  case VK_LoongArch_TLS_DESC_PCREL20_S2:
    return "desc_pcrel_20";
  }
}

LoongArchMCExpr::VariantKind
LoongArchMCExpr::getVariantKindForName(StringRef name) {
  return StringSwitch<LoongArchMCExpr::VariantKind>(name)
      .Case("plt", VK_LoongArch_CALL_PLT)
      .Case("b16", VK_LoongArch_B16)
      .Case("b21", VK_LoongArch_B21)
      .Case("b26", VK_LoongArch_B26)
      .Case("abs_hi20", VK_LoongArch_ABS_HI20)
      .Case("abs_lo12", VK_LoongArch_ABS_LO12)
      .Case("abs64_lo20", VK_LoongArch_ABS64_LO20)
      .Case("abs64_hi12", VK_LoongArch_ABS64_HI12)
      .Case("pc_hi20", VK_LoongArch_PCALA_HI20)
      .Case("pc_lo12", VK_LoongArch_PCALA_LO12)
      .Case("pc64_lo20", VK_LoongArch_PCALA64_LO20)
      .Case("pc64_hi12", VK_LoongArch_PCALA64_HI12)
      .Case("got_pc_hi20", VK_LoongArch_GOT_PC_HI20)
      .Case("got_pc_lo12", VK_LoongArch_GOT_PC_LO12)
      .Case("got64_pc_lo20", VK_LoongArch_GOT64_PC_LO20)
      .Case("got64_pc_hi12", VK_LoongArch_GOT64_PC_HI12)
      .Case("got_hi20", VK_LoongArch_GOT_HI20)
      .Case("got_lo12", VK_LoongArch_GOT_LO12)
      .Case("got64_lo20", VK_LoongArch_GOT64_LO20)
      .Case("got64_hi12", VK_LoongArch_GOT64_HI12)
      .Case("le_hi20", VK_LoongArch_TLS_LE_HI20)
      .Case("le_lo12", VK_LoongArch_TLS_LE_LO12)
      .Case("le64_lo20", VK_LoongArch_TLS_LE64_LO20)
      .Case("le64_hi12", VK_LoongArch_TLS_LE64_HI12)
      .Case("ie_pc_hi20", VK_LoongArch_TLS_IE_PC_HI20)
      .Case("ie_pc_lo12", VK_LoongArch_TLS_IE_PC_LO12)
      .Case("ie64_pc_lo20", VK_LoongArch_TLS_IE64_PC_LO20)
      .Case("ie64_pc_hi12", VK_LoongArch_TLS_IE64_PC_HI12)
      .Case("ie_hi20", VK_LoongArch_TLS_IE_HI20)
      .Case("ie_lo12", VK_LoongArch_TLS_IE_LO12)
      .Case("ie64_lo20", VK_LoongArch_TLS_IE64_LO20)
      .Case("ie64_hi12", VK_LoongArch_TLS_IE64_HI12)
      .Case("ld_pc_hi20", VK_LoongArch_TLS_LD_PC_HI20)
      .Case("ld_hi20", VK_LoongArch_TLS_LD_HI20)
      .Case("gd_pc_hi20", VK_LoongArch_TLS_GD_PC_HI20)
      .Case("gd_hi20", VK_LoongArch_TLS_GD_HI20)
      .Case("call36", VK_LoongArch_CALL36)
      .Case("desc_pc_hi20", VK_LoongArch_TLS_DESC_PC_HI20)
      .Case("desc_pc_lo12", VK_LoongArch_TLS_DESC_PC_LO12)
      .Case("desc64_pc_lo20", VK_LoongArch_TLS_DESC64_PC_LO20)
      .Case("desc64_pc_hi12", VK_LoongArch_TLS_DESC64_PC_HI12)
      .Case("desc_hi20", VK_LoongArch_TLS_DESC_HI20)
      .Case("desc_lo12", VK_LoongArch_TLS_DESC_LO12)
      .Case("desc64_lo20", VK_LoongArch_TLS_DESC64_LO20)
      .Case("desc64_hi12", VK_LoongArch_TLS_DESC64_HI12)
      .Case("desc_ld", VK_LoongArch_TLS_DESC_LD)
      .Case("desc_call", VK_LoongArch_TLS_DESC_CALL)
      .Case("le_hi20_r", VK_LoongArch_TLS_LE_HI20_R)
      .Case("le_add_r", VK_LoongArch_TLS_LE_ADD_R)
      .Case("le_lo12_r", VK_LoongArch_TLS_LE_LO12_R)
      .Case("pcrel_20", VK_LoongArch_PCREL20_S2)
      .Case("ld_pcrel_20", VK_LoongArch_TLS_LD_PCREL20_S2)
      .Case("gd_pcrel_20", VK_LoongArch_TLS_GD_PCREL20_S2)
      .Case("desc_pcrel_20", VK_LoongArch_TLS_DESC_PCREL20_S2)
      .Default(VK_LoongArch_Invalid);
}

static void fixELFSymbolsInTLSFixupsImpl(const MCExpr *Expr, MCAssembler &Asm) {
  switch (Expr->getKind()) {
  case MCExpr::Target:
    llvm_unreachable("Can't handle nested target expression");
    break;
  case MCExpr::Constant:
    break;
  case MCExpr::Unary:
    fixELFSymbolsInTLSFixupsImpl(cast<MCUnaryExpr>(Expr)->getSubExpr(), Asm);
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
  }
}

void LoongArchMCExpr::fixELFSymbolsInTLSFixups(MCAssembler &Asm) const {
  switch (getKind()) {
  default:
    return;
  case VK_LoongArch_TLS_LE_HI20:
  case VK_LoongArch_TLS_IE_PC_HI20:
  case VK_LoongArch_TLS_IE_HI20:
  case VK_LoongArch_TLS_LD_PC_HI20:
  case VK_LoongArch_TLS_LD_HI20:
  case VK_LoongArch_TLS_GD_PC_HI20:
  case VK_LoongArch_TLS_GD_HI20:
  case VK_LoongArch_TLS_DESC_PC_HI20:
  case VK_LoongArch_TLS_DESC_HI20:
  case VK_LoongArch_TLS_LD_PCREL20_S2:
  case VK_LoongArch_TLS_GD_PCREL20_S2:
  case VK_LoongArch_TLS_DESC_PCREL20_S2:
    break;
  }
  fixELFSymbolsInTLSFixupsImpl(getSubExpr(), Asm);
}
