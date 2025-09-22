//===-- NVPTXMCExpr.cpp - NVPTX specific MC expression classes ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NVPTXMCExpr.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/Format.h"
using namespace llvm;

#define DEBUG_TYPE "nvptx-mcexpr"

const NVPTXFloatMCExpr *
NVPTXFloatMCExpr::create(VariantKind Kind, const APFloat &Flt, MCContext &Ctx) {
  return new (Ctx) NVPTXFloatMCExpr(Kind, Flt);
}

void NVPTXFloatMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  bool Ignored;
  unsigned NumHex;
  APFloat APF = getAPFloat();

  switch (Kind) {
  default: llvm_unreachable("Invalid kind!");
  case VK_NVPTX_HALF_PREC_FLOAT:
    // ptxas does not have a way to specify half-precision floats.
    // Instead we have to print and load fp16 constants as .b16
    OS << "0x";
    NumHex = 4;
    APF.convert(APFloat::IEEEhalf(), APFloat::rmNearestTiesToEven, &Ignored);
    break;
  case VK_NVPTX_BFLOAT_PREC_FLOAT:
    OS << "0x";
    NumHex = 4;
    APF.convert(APFloat::BFloat(), APFloat::rmNearestTiesToEven, &Ignored);
    break;
  case VK_NVPTX_SINGLE_PREC_FLOAT:
    OS << "0f";
    NumHex = 8;
    APF.convert(APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven, &Ignored);
    break;
  case VK_NVPTX_DOUBLE_PREC_FLOAT:
    OS << "0d";
    NumHex = 16;
    APF.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &Ignored);
    break;
  }

  APInt API = APF.bitcastToAPInt();
  OS << format_hex_no_prefix(API.getZExtValue(), NumHex, /*Upper=*/true);
}

const NVPTXGenericMCSymbolRefExpr*
NVPTXGenericMCSymbolRefExpr::create(const MCSymbolRefExpr *SymExpr,
                                    MCContext &Ctx) {
  return new (Ctx) NVPTXGenericMCSymbolRefExpr(SymExpr);
}

void NVPTXGenericMCSymbolRefExpr::printImpl(raw_ostream &OS,
                                            const MCAsmInfo *MAI) const {
  OS << "generic(";
  SymExpr->print(OS, MAI);
  OS << ")";
}
