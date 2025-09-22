//===-- PPCMCExpr.cpp - PPC specific MC expression classes ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PPCMCExpr.h"
#include "PPCFixupKinds.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectStreamer.h"

using namespace llvm;

#define DEBUG_TYPE "ppcmcexpr"

const PPCMCExpr *PPCMCExpr::create(VariantKind Kind, const MCExpr *Expr,
                                   MCContext &Ctx) {
  return new (Ctx) PPCMCExpr(Kind, Expr);
}

void PPCMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  getSubExpr()->print(OS, MAI);

  switch (Kind) {
  default:
    llvm_unreachable("Invalid kind!");
  case VK_PPC_LO:
    OS << "@l";
    break;
  case VK_PPC_HI:
    OS << "@h";
    break;
  case VK_PPC_HA:
    OS << "@ha";
    break;
  case VK_PPC_HIGH:
    OS << "@high";
    break;
  case VK_PPC_HIGHA:
    OS << "@higha";
    break;
  case VK_PPC_HIGHER:
    OS << "@higher";
    break;
  case VK_PPC_HIGHERA:
    OS << "@highera";
    break;
  case VK_PPC_HIGHEST:
    OS << "@highest";
    break;
  case VK_PPC_HIGHESTA:
    OS << "@highesta";
    break;
  }
}

bool
PPCMCExpr::evaluateAsConstant(int64_t &Res) const {
  MCValue Value;

  if (!getSubExpr()->evaluateAsRelocatable(Value, nullptr, nullptr))
    return false;

  if (!Value.isAbsolute())
    return false;

  Res = evaluateAsInt64(Value.getConstant());
  return true;
}

int64_t
PPCMCExpr::evaluateAsInt64(int64_t Value) const {
  switch (Kind) {
    case VK_PPC_LO:
      return Value & 0xffff;
    case VK_PPC_HI:
      return (Value >> 16) & 0xffff;
    case VK_PPC_HA:
      return ((Value + 0x8000) >> 16) & 0xffff;
    case VK_PPC_HIGH:
      return (Value >> 16) & 0xffff;
    case VK_PPC_HIGHA:
      return ((Value + 0x8000) >> 16) & 0xffff;
    case VK_PPC_HIGHER:
      return (Value >> 32) & 0xffff;
    case VK_PPC_HIGHERA:
      return ((Value + 0x8000) >> 32) & 0xffff;
    case VK_PPC_HIGHEST:
      return (Value >> 48) & 0xffff;
    case VK_PPC_HIGHESTA:
      return ((Value + 0x8000) >> 48) & 0xffff;
    case VK_PPC_None:
      break;
  }
  llvm_unreachable("Invalid kind!");
}

bool PPCMCExpr::evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                          const MCFixup *Fixup) const {
  MCValue Value;

  if (!getSubExpr()->evaluateAsRelocatable(Value, Asm, Fixup))
    return false;

  if (Value.isAbsolute()) {
    int64_t Result = evaluateAsInt64(Value.getConstant());
    bool IsHalf16 = Fixup && Fixup->getTargetKind() == PPC::fixup_ppc_half16;
    bool IsHalf16DS =
        Fixup && Fixup->getTargetKind() == PPC::fixup_ppc_half16ds;
    bool IsHalf16DQ =
        Fixup && Fixup->getTargetKind() == PPC::fixup_ppc_half16dq;
    bool IsHalf = IsHalf16 || IsHalf16DS || IsHalf16DQ;

    if (!IsHalf && Result >= 0x8000)
      return false;
    if ((IsHalf16DS && (Result & 0x3)) || (IsHalf16DQ && (Result & 0xf)))
      return false;

    Res = MCValue::get(Result);
  } else {
    if (!Asm || !Asm->hasLayout())
      return false;

    MCContext &Context = Asm->getContext();
    const MCSymbolRefExpr *Sym = Value.getSymA();
    MCSymbolRefExpr::VariantKind Modifier = Sym->getKind();
    if (Modifier != MCSymbolRefExpr::VK_None)
      return false;
    switch (Kind) {
      default:
        llvm_unreachable("Invalid kind!");
      case VK_PPC_LO:
        Modifier = MCSymbolRefExpr::VK_PPC_LO;
        break;
      case VK_PPC_HI:
        Modifier = MCSymbolRefExpr::VK_PPC_HI;
        break;
      case VK_PPC_HA:
        Modifier = MCSymbolRefExpr::VK_PPC_HA;
        break;
      case VK_PPC_HIGH:
        Modifier = MCSymbolRefExpr::VK_PPC_HIGH;
        break;
      case VK_PPC_HIGHA:
        Modifier = MCSymbolRefExpr::VK_PPC_HIGHA;
        break;
      case VK_PPC_HIGHERA:
        Modifier = MCSymbolRefExpr::VK_PPC_HIGHERA;
        break;
      case VK_PPC_HIGHER:
        Modifier = MCSymbolRefExpr::VK_PPC_HIGHER;
        break;
      case VK_PPC_HIGHEST:
        Modifier = MCSymbolRefExpr::VK_PPC_HIGHEST;
        break;
      case VK_PPC_HIGHESTA:
        Modifier = MCSymbolRefExpr::VK_PPC_HIGHESTA;
        break;
    }
    Sym = MCSymbolRefExpr::create(&Sym->getSymbol(), Modifier, Context);
    Res = MCValue::get(Sym, Value.getSymB(), Value.getConstant());
  }

  return true;
}

void PPCMCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*getSubExpr());
}
