//===- MCExpr.cpp - Assembly Level Expression Implementation --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCExpr.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "mcexpr"

namespace {
namespace stats {

STATISTIC(MCExprEvaluate, "Number of MCExpr evaluations");

} // end namespace stats
} // end anonymous namespace

void MCExpr::print(raw_ostream &OS, const MCAsmInfo *MAI, bool InParens) const {
  switch (getKind()) {
  case MCExpr::Target:
    return cast<MCTargetExpr>(this)->printImpl(OS, MAI);
  case MCExpr::Constant: {
    auto Value = cast<MCConstantExpr>(*this).getValue();
    auto PrintInHex = cast<MCConstantExpr>(*this).useHexFormat();
    auto SizeInBytes = cast<MCConstantExpr>(*this).getSizeInBytes();
    if (Value < 0 && MAI && !MAI->supportsSignedData())
      PrintInHex = true;
    if (PrintInHex)
      switch (SizeInBytes) {
      default:
        OS << "0x" << Twine::utohexstr(Value);
        break;
      case 1:
        OS << format("0x%02" PRIx64, Value);
        break;
      case 2:
        OS << format("0x%04" PRIx64, Value);
        break;
      case 4:
        OS << format("0x%08" PRIx64, Value);
        break;
      case 8:
        OS << format("0x%016" PRIx64, Value);
        break;
      }
    else
      OS << Value;
    return;
  }
  case MCExpr::SymbolRef: {
    const MCSymbolRefExpr &SRE = cast<MCSymbolRefExpr>(*this);
    const MCSymbol &Sym = SRE.getSymbol();
    // Parenthesize names that start with $ so that they don't look like
    // absolute names.
    bool UseParens = MAI && MAI->useParensForDollarSignNames() && !InParens &&
                     Sym.getName().starts_with('$');

    if (UseParens) {
      OS << '(';
      Sym.print(OS, MAI);
      OS << ')';
    } else
      Sym.print(OS, MAI);

    const MCSymbolRefExpr::VariantKind Kind = SRE.getKind();
    if (Kind != MCSymbolRefExpr::VK_None) {
      if (MAI && MAI->useParensForSymbolVariant()) // ARM
        OS << '(' << MCSymbolRefExpr::getVariantKindName(Kind) << ')';
      else
        OS << '@' << MCSymbolRefExpr::getVariantKindName(Kind);
    }

    return;
  }

  case MCExpr::Unary: {
    const MCUnaryExpr &UE = cast<MCUnaryExpr>(*this);
    switch (UE.getOpcode()) {
    case MCUnaryExpr::LNot:  OS << '!'; break;
    case MCUnaryExpr::Minus: OS << '-'; break;
    case MCUnaryExpr::Not:   OS << '~'; break;
    case MCUnaryExpr::Plus:  OS << '+'; break;
    }
    bool Binary = UE.getSubExpr()->getKind() == MCExpr::Binary;
    if (Binary) OS << "(";
    UE.getSubExpr()->print(OS, MAI);
    if (Binary) OS << ")";
    return;
  }

  case MCExpr::Binary: {
    const MCBinaryExpr &BE = cast<MCBinaryExpr>(*this);

    // Only print parens around the LHS if it is non-trivial.
    if (isa<MCConstantExpr>(BE.getLHS()) || isa<MCSymbolRefExpr>(BE.getLHS())) {
      BE.getLHS()->print(OS, MAI);
    } else {
      OS << '(';
      BE.getLHS()->print(OS, MAI);
      OS << ')';
    }

    switch (BE.getOpcode()) {
    case MCBinaryExpr::Add:
      // Print "X-42" instead of "X+-42".
      if (const MCConstantExpr *RHSC = dyn_cast<MCConstantExpr>(BE.getRHS())) {
        if (RHSC->getValue() < 0) {
          OS << RHSC->getValue();
          return;
        }
      }

      OS <<  '+';
      break;
    case MCBinaryExpr::AShr: OS << ">>"; break;
    case MCBinaryExpr::And:  OS <<  '&'; break;
    case MCBinaryExpr::Div:  OS <<  '/'; break;
    case MCBinaryExpr::EQ:   OS << "=="; break;
    case MCBinaryExpr::GT:   OS <<  '>'; break;
    case MCBinaryExpr::GTE:  OS << ">="; break;
    case MCBinaryExpr::LAnd: OS << "&&"; break;
    case MCBinaryExpr::LOr:  OS << "||"; break;
    case MCBinaryExpr::LShr: OS << ">>"; break;
    case MCBinaryExpr::LT:   OS <<  '<'; break;
    case MCBinaryExpr::LTE:  OS << "<="; break;
    case MCBinaryExpr::Mod:  OS <<  '%'; break;
    case MCBinaryExpr::Mul:  OS <<  '*'; break;
    case MCBinaryExpr::NE:   OS << "!="; break;
    case MCBinaryExpr::Or:   OS <<  '|'; break;
    case MCBinaryExpr::OrNot: OS << '!'; break;
    case MCBinaryExpr::Shl:  OS << "<<"; break;
    case MCBinaryExpr::Sub:  OS <<  '-'; break;
    case MCBinaryExpr::Xor:  OS <<  '^'; break;
    }

    // Only print parens around the LHS if it is non-trivial.
    if (isa<MCConstantExpr>(BE.getRHS()) || isa<MCSymbolRefExpr>(BE.getRHS())) {
      BE.getRHS()->print(OS, MAI);
    } else {
      OS << '(';
      BE.getRHS()->print(OS, MAI);
      OS << ')';
    }
    return;
  }
  }

  llvm_unreachable("Invalid expression kind!");
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MCExpr::dump() const {
  dbgs() << *this;
  dbgs() << '\n';
}
#endif

/* *** */

const MCBinaryExpr *MCBinaryExpr::create(Opcode Opc, const MCExpr *LHS,
                                         const MCExpr *RHS, MCContext &Ctx,
                                         SMLoc Loc) {
  return new (Ctx) MCBinaryExpr(Opc, LHS, RHS, Loc);
}

const MCUnaryExpr *MCUnaryExpr::create(Opcode Opc, const MCExpr *Expr,
                                       MCContext &Ctx, SMLoc Loc) {
  return new (Ctx) MCUnaryExpr(Opc, Expr, Loc);
}

const MCConstantExpr *MCConstantExpr::create(int64_t Value, MCContext &Ctx,
                                             bool PrintInHex,
                                             unsigned SizeInBytes) {
  return new (Ctx) MCConstantExpr(Value, PrintInHex, SizeInBytes);
}

/* *** */

MCSymbolRefExpr::MCSymbolRefExpr(const MCSymbol *Symbol, VariantKind Kind,
                                 const MCAsmInfo *MAI, SMLoc Loc)
    : MCExpr(MCExpr::SymbolRef, Loc,
             encodeSubclassData(Kind, MAI->hasSubsectionsViaSymbols())),
      Symbol(Symbol) {
  assert(Symbol);
}

const MCSymbolRefExpr *MCSymbolRefExpr::create(const MCSymbol *Sym,
                                               VariantKind Kind,
                                               MCContext &Ctx, SMLoc Loc) {
  return new (Ctx) MCSymbolRefExpr(Sym, Kind, Ctx.getAsmInfo(), Loc);
}

const MCSymbolRefExpr *MCSymbolRefExpr::create(StringRef Name, VariantKind Kind,
                                               MCContext &Ctx) {
  return create(Ctx.getOrCreateSymbol(Name), Kind, Ctx);
}

StringRef MCSymbolRefExpr::getVariantKindName(VariantKind Kind) {
  switch (Kind) {
    // clang-format off
  case VK_Invalid: return "<<invalid>>";
  case VK_None: return "<<none>>";

  case VK_DTPOFF: return "DTPOFF";
  case VK_DTPREL: return "DTPREL";
  case VK_GOT: return "GOT";
  case VK_GOTOFF: return "GOTOFF";
  case VK_GOTREL: return "GOTREL";
  case VK_PCREL: return "PCREL";
  case VK_GOTPCREL: return "GOTPCREL";
  case VK_GOTPCREL_NORELAX: return "GOTPCREL_NORELAX";
  case VK_GOTTPOFF: return "GOTTPOFF";
  case VK_GOTTPOFF_FDPIC: return "gottpoff_fdpic";
  case VK_INDNTPOFF: return "INDNTPOFF";
  case VK_NTPOFF: return "NTPOFF";
  case VK_GOTNTPOFF: return "GOTNTPOFF";
  case VK_PLT: return "PLT";
  case VK_TLSGD: return "TLSGD";
  case VK_TLSGD_FDPIC: return "tlsgd_fdpic";
  case VK_TLSLD: return "TLSLD";
  case VK_TLSLDM: return "TLSLDM";
  case VK_TLSLDM_FDPIC: return "tlsldm_fdpic";
  case VK_TPOFF: return "TPOFF";
  case VK_TPREL: return "TPREL";
  case VK_TLSCALL: return "tlscall";
  case VK_TLSDESC: return "tlsdesc";
  case VK_TLVP: return "TLVP";
  case VK_TLVPPAGE: return "TLVPPAGE";
  case VK_TLVPPAGEOFF: return "TLVPPAGEOFF";
  case VK_PAGE: return "PAGE";
  case VK_PAGEOFF: return "PAGEOFF";
  case VK_GOTPAGE: return "GOTPAGE";
  case VK_GOTPAGEOFF: return "GOTPAGEOFF";
  case VK_SECREL: return "SECREL32";
  case VK_SIZE: return "SIZE";
  case VK_WEAKREF: return "WEAKREF";
  case VK_FUNCDESC: return "FUNCDESC";
  case VK_GOTFUNCDESC: return "GOTFUNCDESC";
  case VK_GOTOFFFUNCDESC: return "GOTOFFFUNCDESC";
  case VK_X86_ABS8: return "ABS8";
  case VK_X86_PLTOFF: return "PLTOFF";
  case VK_ARM_NONE: return "none";
  case VK_ARM_GOT_PREL: return "GOT_PREL";
  case VK_ARM_TARGET1: return "target1";
  case VK_ARM_TARGET2: return "target2";
  case VK_ARM_PREL31: return "prel31";
  case VK_ARM_SBREL: return "sbrel";
  case VK_ARM_TLSLDO: return "tlsldo";
  case VK_ARM_TLSDESCSEQ: return "tlsdescseq";
  case VK_AVR_NONE: return "none";
  case VK_AVR_LO8: return "lo8";
  case VK_AVR_HI8: return "hi8";
  case VK_AVR_HLO8: return "hlo8";
  case VK_AVR_DIFF8: return "diff8";
  case VK_AVR_DIFF16: return "diff16";
  case VK_AVR_DIFF32: return "diff32";
  case VK_AVR_PM: return "pm";
  case VK_PPC_LO: return "l";
  case VK_PPC_HI: return "h";
  case VK_PPC_HA: return "ha";
  case VK_PPC_HIGH: return "high";
  case VK_PPC_HIGHA: return "higha";
  case VK_PPC_HIGHER: return "higher";
  case VK_PPC_HIGHERA: return "highera";
  case VK_PPC_HIGHEST: return "highest";
  case VK_PPC_HIGHESTA: return "highesta";
  case VK_PPC_GOT_LO: return "got@l";
  case VK_PPC_GOT_HI: return "got@h";
  case VK_PPC_GOT_HA: return "got@ha";
  case VK_PPC_TOCBASE: return "tocbase";
  case VK_PPC_TOC: return "toc";
  case VK_PPC_TOC_LO: return "toc@l";
  case VK_PPC_TOC_HI: return "toc@h";
  case VK_PPC_TOC_HA: return "toc@ha";
  case VK_PPC_U: return "u";
  case VK_PPC_L: return "l";
  case VK_PPC_DTPMOD: return "dtpmod";
  case VK_PPC_TPREL_LO: return "tprel@l";
  case VK_PPC_TPREL_HI: return "tprel@h";
  case VK_PPC_TPREL_HA: return "tprel@ha";
  case VK_PPC_TPREL_HIGH: return "tprel@high";
  case VK_PPC_TPREL_HIGHA: return "tprel@higha";
  case VK_PPC_TPREL_HIGHER: return "tprel@higher";
  case VK_PPC_TPREL_HIGHERA: return "tprel@highera";
  case VK_PPC_TPREL_HIGHEST: return "tprel@highest";
  case VK_PPC_TPREL_HIGHESTA: return "tprel@highesta";
  case VK_PPC_DTPREL_LO: return "dtprel@l";
  case VK_PPC_DTPREL_HI: return "dtprel@h";
  case VK_PPC_DTPREL_HA: return "dtprel@ha";
  case VK_PPC_DTPREL_HIGH: return "dtprel@high";
  case VK_PPC_DTPREL_HIGHA: return "dtprel@higha";
  case VK_PPC_DTPREL_HIGHER: return "dtprel@higher";
  case VK_PPC_DTPREL_HIGHERA: return "dtprel@highera";
  case VK_PPC_DTPREL_HIGHEST: return "dtprel@highest";
  case VK_PPC_DTPREL_HIGHESTA: return "dtprel@highesta";
  case VK_PPC_GOT_TPREL: return "got@tprel";
  case VK_PPC_GOT_TPREL_LO: return "got@tprel@l";
  case VK_PPC_GOT_TPREL_HI: return "got@tprel@h";
  case VK_PPC_GOT_TPREL_HA: return "got@tprel@ha";
  case VK_PPC_GOT_DTPREL: return "got@dtprel";
  case VK_PPC_GOT_DTPREL_LO: return "got@dtprel@l";
  case VK_PPC_GOT_DTPREL_HI: return "got@dtprel@h";
  case VK_PPC_GOT_DTPREL_HA: return "got@dtprel@ha";
  case VK_PPC_TLS: return "tls";
  case VK_PPC_GOT_TLSGD: return "got@tlsgd";
  case VK_PPC_GOT_TLSGD_LO: return "got@tlsgd@l";
  case VK_PPC_GOT_TLSGD_HI: return "got@tlsgd@h";
  case VK_PPC_GOT_TLSGD_HA: return "got@tlsgd@ha";
  case VK_PPC_TLSGD: return "tlsgd";
  case VK_PPC_AIX_TLSGD:
    return "gd";
  case VK_PPC_AIX_TLSGDM:
    return "m";
  case VK_PPC_AIX_TLSIE:
    return "ie";
  case VK_PPC_AIX_TLSLE:
    return "le";
  case VK_PPC_AIX_TLSLD:
    return "ld";
  case VK_PPC_AIX_TLSML:
    return "ml";
  case VK_PPC_GOT_TLSLD: return "got@tlsld";
  case VK_PPC_GOT_TLSLD_LO: return "got@tlsld@l";
  case VK_PPC_GOT_TLSLD_HI: return "got@tlsld@h";
  case VK_PPC_GOT_TLSLD_HA: return "got@tlsld@ha";
  case VK_PPC_GOT_PCREL:
    return "got@pcrel";
  case VK_PPC_GOT_TLSGD_PCREL:
    return "got@tlsgd@pcrel";
  case VK_PPC_GOT_TLSLD_PCREL:
    return "got@tlsld@pcrel";
  case VK_PPC_GOT_TPREL_PCREL:
    return "got@tprel@pcrel";
  case VK_PPC_TLS_PCREL:
    return "tls@pcrel";
  case VK_PPC_TLSLD: return "tlsld";
  case VK_PPC_LOCAL: return "local";
  case VK_PPC_NOTOC: return "notoc";
  case VK_PPC_PCREL_OPT: return "<<invalid>>";
  case VK_COFF_IMGREL32: return "IMGREL";
  case VK_Hexagon_LO16: return "LO16";
  case VK_Hexagon_HI16: return "HI16";
  case VK_Hexagon_GPREL: return "GPREL";
  case VK_Hexagon_GD_GOT: return "GDGOT";
  case VK_Hexagon_LD_GOT: return "LDGOT";
  case VK_Hexagon_GD_PLT: return "GDPLT";
  case VK_Hexagon_LD_PLT: return "LDPLT";
  case VK_Hexagon_IE: return "IE";
  case VK_Hexagon_IE_GOT: return "IEGOT";
  case VK_WASM_TYPEINDEX: return "TYPEINDEX";
  case VK_WASM_MBREL: return "MBREL";
  case VK_WASM_TLSREL: return "TLSREL";
  case VK_WASM_TBREL: return "TBREL";
  case VK_WASM_GOT_TLS: return "GOT@TLS";
  case VK_WASM_FUNCINDEX: return "FUNCINDEX";
  case VK_AMDGPU_GOTPCREL32_LO: return "gotpcrel32@lo";
  case VK_AMDGPU_GOTPCREL32_HI: return "gotpcrel32@hi";
  case VK_AMDGPU_REL32_LO: return "rel32@lo";
  case VK_AMDGPU_REL32_HI: return "rel32@hi";
  case VK_AMDGPU_REL64: return "rel64";
  case VK_AMDGPU_ABS32_LO: return "abs32@lo";
  case VK_AMDGPU_ABS32_HI: return "abs32@hi";
  case VK_VE_HI32: return "hi";
  case VK_VE_LO32: return "lo";
  case VK_VE_PC_HI32: return "pc_hi";
  case VK_VE_PC_LO32: return "pc_lo";
  case VK_VE_GOT_HI32: return "got_hi";
  case VK_VE_GOT_LO32: return "got_lo";
  case VK_VE_GOTOFF_HI32: return "gotoff_hi";
  case VK_VE_GOTOFF_LO32: return "gotoff_lo";
  case VK_VE_PLT_HI32: return "plt_hi";
  case VK_VE_PLT_LO32: return "plt_lo";
  case VK_VE_TLS_GD_HI32: return "tls_gd_hi";
  case VK_VE_TLS_GD_LO32: return "tls_gd_lo";
  case VK_VE_TPOFF_HI32: return "tpoff_hi";
  case VK_VE_TPOFF_LO32: return "tpoff_lo";
    // clang-format on
  }
  llvm_unreachable("Invalid variant kind");
}

MCSymbolRefExpr::VariantKind
MCSymbolRefExpr::getVariantKindForName(StringRef Name) {
  return StringSwitch<VariantKind>(Name.lower())
    .Case("dtprel", VK_DTPREL)
    .Case("dtpoff", VK_DTPOFF)
    .Case("got", VK_GOT)
    .Case("gotoff", VK_GOTOFF)
    .Case("gotrel", VK_GOTREL)
    .Case("pcrel", VK_PCREL)
    .Case("gotpcrel", VK_GOTPCREL)
    .Case("gotpcrel_norelax", VK_GOTPCREL_NORELAX)
    .Case("gottpoff", VK_GOTTPOFF)
    .Case("indntpoff", VK_INDNTPOFF)
    .Case("ntpoff", VK_NTPOFF)
    .Case("gotntpoff", VK_GOTNTPOFF)
    .Case("plt", VK_PLT)
    .Case("tlscall", VK_TLSCALL)
    .Case("tlsdesc", VK_TLSDESC)
    .Case("tlsgd", VK_TLSGD)
    .Case("tlsld", VK_TLSLD)
    .Case("tlsldm", VK_TLSLDM)
    .Case("tpoff", VK_TPOFF)
    .Case("tprel", VK_TPREL)
    .Case("tlvp", VK_TLVP)
    .Case("tlvppage", VK_TLVPPAGE)
    .Case("tlvppageoff", VK_TLVPPAGEOFF)
    .Case("page", VK_PAGE)
    .Case("pageoff", VK_PAGEOFF)
    .Case("gotpage", VK_GOTPAGE)
    .Case("gotpageoff", VK_GOTPAGEOFF)
    .Case("imgrel", VK_COFF_IMGREL32)
    .Case("secrel32", VK_SECREL)
    .Case("size", VK_SIZE)
    .Case("abs8", VK_X86_ABS8)
    .Case("pltoff", VK_X86_PLTOFF)
    .Case("l", VK_PPC_LO)
    .Case("h", VK_PPC_HI)
    .Case("ha", VK_PPC_HA)
    .Case("high", VK_PPC_HIGH)
    .Case("higha", VK_PPC_HIGHA)
    .Case("higher", VK_PPC_HIGHER)
    .Case("highera", VK_PPC_HIGHERA)
    .Case("highest", VK_PPC_HIGHEST)
    .Case("highesta", VK_PPC_HIGHESTA)
    .Case("got@l", VK_PPC_GOT_LO)
    .Case("got@h", VK_PPC_GOT_HI)
    .Case("got@ha", VK_PPC_GOT_HA)
    .Case("local", VK_PPC_LOCAL)
    .Case("tocbase", VK_PPC_TOCBASE)
    .Case("toc", VK_PPC_TOC)
    .Case("toc@l", VK_PPC_TOC_LO)
    .Case("toc@h", VK_PPC_TOC_HI)
    .Case("toc@ha", VK_PPC_TOC_HA)
    .Case("u", VK_PPC_U)
    .Case("l", VK_PPC_L)
    .Case("tls", VK_PPC_TLS)
    .Case("dtpmod", VK_PPC_DTPMOD)
    .Case("tprel@l", VK_PPC_TPREL_LO)
    .Case("tprel@h", VK_PPC_TPREL_HI)
    .Case("tprel@ha", VK_PPC_TPREL_HA)
    .Case("tprel@high", VK_PPC_TPREL_HIGH)
    .Case("tprel@higha", VK_PPC_TPREL_HIGHA)
    .Case("tprel@higher", VK_PPC_TPREL_HIGHER)
    .Case("tprel@highera", VK_PPC_TPREL_HIGHERA)
    .Case("tprel@highest", VK_PPC_TPREL_HIGHEST)
    .Case("tprel@highesta", VK_PPC_TPREL_HIGHESTA)
    .Case("dtprel@l", VK_PPC_DTPREL_LO)
    .Case("dtprel@h", VK_PPC_DTPREL_HI)
    .Case("dtprel@ha", VK_PPC_DTPREL_HA)
    .Case("dtprel@high", VK_PPC_DTPREL_HIGH)
    .Case("dtprel@higha", VK_PPC_DTPREL_HIGHA)
    .Case("dtprel@higher", VK_PPC_DTPREL_HIGHER)
    .Case("dtprel@highera", VK_PPC_DTPREL_HIGHERA)
    .Case("dtprel@highest", VK_PPC_DTPREL_HIGHEST)
    .Case("dtprel@highesta", VK_PPC_DTPREL_HIGHESTA)
    .Case("got@tprel", VK_PPC_GOT_TPREL)
    .Case("got@tprel@l", VK_PPC_GOT_TPREL_LO)
    .Case("got@tprel@h", VK_PPC_GOT_TPREL_HI)
    .Case("got@tprel@ha", VK_PPC_GOT_TPREL_HA)
    .Case("got@dtprel", VK_PPC_GOT_DTPREL)
    .Case("got@dtprel@l", VK_PPC_GOT_DTPREL_LO)
    .Case("got@dtprel@h", VK_PPC_GOT_DTPREL_HI)
    .Case("got@dtprel@ha", VK_PPC_GOT_DTPREL_HA)
    .Case("got@tlsgd", VK_PPC_GOT_TLSGD)
    .Case("got@tlsgd@l", VK_PPC_GOT_TLSGD_LO)
    .Case("got@tlsgd@h", VK_PPC_GOT_TLSGD_HI)
    .Case("got@tlsgd@ha", VK_PPC_GOT_TLSGD_HA)
    .Case("got@tlsld", VK_PPC_GOT_TLSLD)
    .Case("got@tlsld@l", VK_PPC_GOT_TLSLD_LO)
    .Case("got@tlsld@h", VK_PPC_GOT_TLSLD_HI)
    .Case("got@tlsld@ha", VK_PPC_GOT_TLSLD_HA)
    .Case("got@pcrel", VK_PPC_GOT_PCREL)
    .Case("got@tlsgd@pcrel", VK_PPC_GOT_TLSGD_PCREL)
    .Case("got@tlsld@pcrel", VK_PPC_GOT_TLSLD_PCREL)
    .Case("got@tprel@pcrel", VK_PPC_GOT_TPREL_PCREL)
    .Case("tls@pcrel", VK_PPC_TLS_PCREL)
    .Case("notoc", VK_PPC_NOTOC)
    .Case("gdgot", VK_Hexagon_GD_GOT)
    .Case("gdplt", VK_Hexagon_GD_PLT)
    .Case("iegot", VK_Hexagon_IE_GOT)
    .Case("ie", VK_Hexagon_IE)
    .Case("ldgot", VK_Hexagon_LD_GOT)
    .Case("ldplt", VK_Hexagon_LD_PLT)
    .Case("lo8", VK_AVR_LO8)
    .Case("hi8", VK_AVR_HI8)
    .Case("hlo8", VK_AVR_HLO8)
    .Case("typeindex", VK_WASM_TYPEINDEX)
    .Case("tbrel", VK_WASM_TBREL)
    .Case("mbrel", VK_WASM_MBREL)
    .Case("tlsrel", VK_WASM_TLSREL)
    .Case("got@tls", VK_WASM_GOT_TLS)
    .Case("funcindex", VK_WASM_FUNCINDEX)
    .Case("gotpcrel32@lo", VK_AMDGPU_GOTPCREL32_LO)
    .Case("gotpcrel32@hi", VK_AMDGPU_GOTPCREL32_HI)
    .Case("rel32@lo", VK_AMDGPU_REL32_LO)
    .Case("rel32@hi", VK_AMDGPU_REL32_HI)
    .Case("rel64", VK_AMDGPU_REL64)
    .Case("abs32@lo", VK_AMDGPU_ABS32_LO)
    .Case("abs32@hi", VK_AMDGPU_ABS32_HI)
    .Case("hi", VK_VE_HI32)
    .Case("lo", VK_VE_LO32)
    .Case("pc_hi", VK_VE_PC_HI32)
    .Case("pc_lo", VK_VE_PC_LO32)
    .Case("got_hi", VK_VE_GOT_HI32)
    .Case("got_lo", VK_VE_GOT_LO32)
    .Case("gotoff_hi", VK_VE_GOTOFF_HI32)
    .Case("gotoff_lo", VK_VE_GOTOFF_LO32)
    .Case("plt_hi", VK_VE_PLT_HI32)
    .Case("plt_lo", VK_VE_PLT_LO32)
    .Case("tls_gd_hi", VK_VE_TLS_GD_HI32)
    .Case("tls_gd_lo", VK_VE_TLS_GD_LO32)
    .Case("tpoff_hi", VK_VE_TPOFF_HI32)
    .Case("tpoff_lo", VK_VE_TPOFF_LO32)
    .Default(VK_Invalid);
}

/* *** */

void MCTargetExpr::anchor() {}

/* *** */

bool MCExpr::evaluateAsAbsolute(int64_t &Res) const {
  return evaluateAsAbsolute(Res, nullptr, nullptr, false);
}

bool MCExpr::evaluateAsAbsolute(int64_t &Res, const MCAssembler &Asm,
                                const SectionAddrMap &Addrs) const {
  // Setting InSet causes us to absolutize differences across sections and that
  // is what the MachO writer uses Addrs for.
  return evaluateAsAbsolute(Res, &Asm, &Addrs, true);
}

bool MCExpr::evaluateAsAbsolute(int64_t &Res, const MCAssembler &Asm) const {
  return evaluateAsAbsolute(Res, &Asm, nullptr, false);
}

bool MCExpr::evaluateAsAbsolute(int64_t &Res, const MCAssembler *Asm) const {
  return evaluateAsAbsolute(Res, Asm, nullptr, false);
}

bool MCExpr::evaluateKnownAbsolute(int64_t &Res, const MCAssembler &Asm) const {
  return evaluateAsAbsolute(Res, &Asm, nullptr, true);
}

bool MCExpr::evaluateAsAbsolute(int64_t &Res, const MCAssembler *Asm,
                                const SectionAddrMap *Addrs, bool InSet) const {
  MCValue Value;

  // Fast path constants.
  if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(this)) {
    Res = CE->getValue();
    return true;
  }

  bool IsRelocatable =
      evaluateAsRelocatableImpl(Value, Asm, nullptr, Addrs, InSet);

  // Record the current value.
  Res = Value.getConstant();

  return IsRelocatable && Value.isAbsolute();
}

/// Helper method for \see EvaluateSymbolAdd().
static void AttemptToFoldSymbolOffsetDifference(
    const MCAssembler *Asm, const SectionAddrMap *Addrs, bool InSet,
    const MCSymbolRefExpr *&A, const MCSymbolRefExpr *&B, int64_t &Addend) {
  if (!A || !B)
    return;

  const MCSymbol &SA = A->getSymbol();
  const MCSymbol &SB = B->getSymbol();

  if (SA.isUndefined() || SB.isUndefined())
    return;

  if (!Asm->getWriter().isSymbolRefDifferenceFullyResolved(*Asm, A, B, InSet))
    return;

  auto FinalizeFolding = [&]() {
    // Pointers to Thumb symbols need to have their low-bit set to allow
    // for interworking.
    if (Asm->isThumbFunc(&SA))
      Addend |= 1;

    // Clear the symbol expr pointers to indicate we have folded these
    // operands.
    A = B = nullptr;
  };

  const MCFragment *FA = SA.getFragment();
  const MCFragment *FB = SB.getFragment();
  const MCSection &SecA = *FA->getParent();
  const MCSection &SecB = *FB->getParent();
  if ((&SecA != &SecB) && !Addrs)
    return;

  // When layout is available, we can generally compute the difference using the
  // getSymbolOffset path, which also avoids the possible slow fragment walk.
  // However, linker relaxation may cause incorrect fold of A-B if A and B are
  // separated by a linker-relaxable instruction. If the section contains
  // instructions and InSet is false (not expressions in directive like
  // .size/.fill), disable the fast path.
  bool Layout = Asm->hasLayout();
  if (Layout && (InSet || !SecA.hasInstructions() ||
                 !(Asm->getContext().getTargetTriple().isRISCV() ||
                   Asm->getContext().getTargetTriple().isLoongArch()))) {
    // If both symbols are in the same fragment, return the difference of their
    // offsets. canGetFragmentOffset(FA) may be false.
    if (FA == FB && !SA.isVariable() && !SB.isVariable()) {
      Addend += SA.getOffset() - SB.getOffset();
      return FinalizeFolding();
    }

    // Eagerly evaluate when layout is finalized.
    Addend += Asm->getSymbolOffset(A->getSymbol()) -
              Asm->getSymbolOffset(B->getSymbol());
    if (Addrs && (&SecA != &SecB))
      Addend += (Addrs->lookup(&SecA) - Addrs->lookup(&SecB));

    FinalizeFolding();
  } else {
    // When layout is not finalized, our ability to resolve differences between
    // symbols is limited to specific cases where the fragments between two
    // symbols (including the fragments the symbols are defined in) are
    // fixed-size fragments so the difference can be calculated. For example,
    // this is important when the Subtarget is changed and a new MCDataFragment
    // is created in the case of foo: instr; .arch_extension ext; instr .if . -
    // foo.
    if (SA.isVariable() || SB.isVariable())
      return;

    // Try to find a constant displacement from FA to FB, add the displacement
    // between the offset in FA of SA and the offset in FB of SB.
    bool Reverse = false;
    if (FA == FB)
      Reverse = SA.getOffset() < SB.getOffset();
    else
      Reverse = FA->getLayoutOrder() < FB->getLayoutOrder();

    uint64_t SAOffset = SA.getOffset(), SBOffset = SB.getOffset();
    int64_t Displacement = SA.getOffset() - SB.getOffset();
    if (Reverse) {
      std::swap(FA, FB);
      std::swap(SAOffset, SBOffset);
      Displacement *= -1;
    }

    // Track whether B is before a relaxable instruction and whether A is after
    // a relaxable instruction. If SA and SB are separated by a linker-relaxable
    // instruction, the difference cannot be resolved as it may be changed by
    // the linker.
    bool BBeforeRelax = false, AAfterRelax = false;
    for (auto FI = FB; FI; FI = FI->getNext()) {
      auto DF = dyn_cast<MCDataFragment>(FI);
      if (DF && DF->isLinkerRelaxable()) {
        if (&*FI != FB || SBOffset != DF->getContents().size())
          BBeforeRelax = true;
        if (&*FI != FA || SAOffset == DF->getContents().size())
          AAfterRelax = true;
        if (BBeforeRelax && AAfterRelax)
          return;
      }
      if (&*FI == FA) {
        // If FA and FB belong to the same subsection, the loop will find FA and
        // we can resolve the difference.
        Addend += Reverse ? -Displacement : Displacement;
        FinalizeFolding();
        return;
      }

      int64_t Num;
      unsigned Count;
      if (DF) {
        Displacement += DF->getContents().size();
      } else if (auto *AF = dyn_cast<MCAlignFragment>(FI);
                 AF && Layout && AF->hasEmitNops() &&
                 !Asm->getBackend().shouldInsertExtraNopBytesForCodeAlign(
                     *AF, Count)) {
        Displacement += Asm->computeFragmentSize(*AF);
      } else if (auto *FF = dyn_cast<MCFillFragment>(FI);
                 FF && FF->getNumValues().evaluateAsAbsolute(Num)) {
        Displacement += Num * FF->getValueSize();
      } else {
        return;
      }
    }
  }
}

/// Evaluate the result of an add between (conceptually) two MCValues.
///
/// This routine conceptually attempts to construct an MCValue:
///   Result = (Result_A - Result_B + Result_Cst)
/// from two MCValue's LHS and RHS where
///   Result = LHS + RHS
/// and
///   Result = (LHS_A - LHS_B + LHS_Cst) + (RHS_A - RHS_B + RHS_Cst).
///
/// This routine attempts to aggressively fold the operands such that the result
/// is representable in an MCValue, but may not always succeed.
///
/// \returns True on success, false if the result is not representable in an
/// MCValue.

/// NOTE: It is really important to have both the Asm and Layout arguments.
/// They might look redundant, but this function can be used before layout
/// is done (see the object streamer for example) and having the Asm argument
/// lets us avoid relaxations early.
static bool evaluateSymbolicAdd(const MCAssembler *Asm,
                                const SectionAddrMap *Addrs, bool InSet,
                                const MCValue &LHS, const MCValue &RHS,
                                MCValue &Res) {
  // FIXME: This routine (and other evaluation parts) are *incredibly* sloppy
  // about dealing with modifiers. This will ultimately bite us, one day.
  const MCSymbolRefExpr *LHS_A = LHS.getSymA();
  const MCSymbolRefExpr *LHS_B = LHS.getSymB();
  int64_t LHS_Cst = LHS.getConstant();

  const MCSymbolRefExpr *RHS_A = RHS.getSymA();
  const MCSymbolRefExpr *RHS_B = RHS.getSymB();
  int64_t RHS_Cst = RHS.getConstant();

  if (LHS.getRefKind() != RHS.getRefKind())
    return false;

  // Fold the result constant immediately.
  int64_t Result_Cst = LHS_Cst + RHS_Cst;

  // If we have a layout, we can fold resolved differences.
  if (Asm) {
    // First, fold out any differences which are fully resolved. By
    // reassociating terms in
    //   Result = (LHS_A - LHS_B + LHS_Cst) + (RHS_A - RHS_B + RHS_Cst).
    // we have the four possible differences:
    //   (LHS_A - LHS_B),
    //   (LHS_A - RHS_B),
    //   (RHS_A - LHS_B),
    //   (RHS_A - RHS_B).
    // Since we are attempting to be as aggressive as possible about folding, we
    // attempt to evaluate each possible alternative.
    AttemptToFoldSymbolOffsetDifference(Asm, Addrs, InSet, LHS_A, LHS_B,
                                        Result_Cst);
    AttemptToFoldSymbolOffsetDifference(Asm, Addrs, InSet, LHS_A, RHS_B,
                                        Result_Cst);
    AttemptToFoldSymbolOffsetDifference(Asm, Addrs, InSet, RHS_A, LHS_B,
                                        Result_Cst);
    AttemptToFoldSymbolOffsetDifference(Asm, Addrs, InSet, RHS_A, RHS_B,
                                        Result_Cst);
  }

  // We can't represent the addition or subtraction of two symbols.
  if ((LHS_A && RHS_A) || (LHS_B && RHS_B))
    return false;

  // At this point, we have at most one additive symbol and one subtractive
  // symbol -- find them.
  const MCSymbolRefExpr *A = LHS_A ? LHS_A : RHS_A;
  const MCSymbolRefExpr *B = LHS_B ? LHS_B : RHS_B;

  Res = MCValue::get(A, B, Result_Cst);
  return true;
}

bool MCExpr::evaluateAsRelocatable(MCValue &Res, const MCAssembler *Asm,
                                   const MCFixup *Fixup) const {
  return evaluateAsRelocatableImpl(Res, Asm, Fixup, nullptr, false);
}

bool MCExpr::evaluateAsValue(MCValue &Res, const MCAssembler &Asm) const {
  return evaluateAsRelocatableImpl(Res, &Asm, nullptr, nullptr, true);
}

static bool canExpand(const MCSymbol &Sym, bool InSet) {
  if (Sym.isWeakExternal())
    return false;

  const MCExpr *Expr = Sym.getVariableValue();
  const auto *Inner = dyn_cast<MCSymbolRefExpr>(Expr);
  if (Inner) {
    if (Inner->getKind() == MCSymbolRefExpr::VK_WEAKREF)
      return false;
  }

  if (InSet)
    return true;
  return !Sym.isInSection();
}

bool MCExpr::evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                       const MCFixup *Fixup,
                                       const SectionAddrMap *Addrs,
                                       bool InSet) const {
  ++stats::MCExprEvaluate;
  switch (getKind()) {
  case Target:
    return cast<MCTargetExpr>(this)->evaluateAsRelocatableImpl(Res, Asm, Fixup);

  case Constant:
    Res = MCValue::get(cast<MCConstantExpr>(this)->getValue());
    return true;

  case SymbolRef: {
    const MCSymbolRefExpr *SRE = cast<MCSymbolRefExpr>(this);
    const MCSymbol &Sym = SRE->getSymbol();
    const auto Kind = SRE->getKind();
    bool Layout = Asm && Asm->hasLayout();

    // Evaluate recursively if this is a variable.
    if (Sym.isVariable() && (Kind == MCSymbolRefExpr::VK_None || Layout) &&
        canExpand(Sym, InSet)) {
      bool IsMachO = SRE->hasSubsectionsViaSymbols();
      if (Sym.getVariableValue()->evaluateAsRelocatableImpl(
              Res, Asm, Fixup, Addrs, InSet || IsMachO)) {
        if (Kind != MCSymbolRefExpr::VK_None) {
          if (Res.isAbsolute()) {
            Res = MCValue::get(SRE, nullptr, 0);
            return true;
          }
          // If the reference has a variant kind, we can only handle expressions
          // which evaluate exactly to a single unadorned symbol. Attach the
          // original VariantKind to SymA of the result.
          if (Res.getRefKind() != MCSymbolRefExpr::VK_None || !Res.getSymA() ||
              Res.getSymB() || Res.getConstant())
            return false;
          Res =
              MCValue::get(MCSymbolRefExpr::create(&Res.getSymA()->getSymbol(),
                                                   Kind, Asm->getContext()),
                           Res.getSymB(), Res.getConstant(), Res.getRefKind());
        }
        if (!IsMachO)
          return true;

        const MCSymbolRefExpr *A = Res.getSymA();
        const MCSymbolRefExpr *B = Res.getSymB();
        // FIXME: This is small hack. Given
        // a = b + 4
        // .long a
        // the OS X assembler will completely drop the 4. We should probably
        // include it in the relocation or produce an error if that is not
        // possible.
        // Allow constant expressions.
        if (!A && !B)
          return true;
        // Allows aliases with zero offset.
        if (Res.getConstant() == 0 && (!A || !B))
          return true;
      }
    }

    Res = MCValue::get(SRE, nullptr, 0);
    return true;
  }

  case Unary: {
    const MCUnaryExpr *AUE = cast<MCUnaryExpr>(this);
    MCValue Value;

    if (!AUE->getSubExpr()->evaluateAsRelocatableImpl(Value, Asm, Fixup, Addrs,
                                                      InSet))
      return false;

    switch (AUE->getOpcode()) {
    case MCUnaryExpr::LNot:
      if (!Value.isAbsolute())
        return false;
      Res = MCValue::get(!Value.getConstant());
      break;
    case MCUnaryExpr::Minus:
      /// -(a - b + const) ==> (b - a - const)
      if (Value.getSymA() && !Value.getSymB())
        return false;

      // The cast avoids undefined behavior if the constant is INT64_MIN.
      Res = MCValue::get(Value.getSymB(), Value.getSymA(),
                         -(uint64_t)Value.getConstant());
      break;
    case MCUnaryExpr::Not:
      if (!Value.isAbsolute())
        return false;
      Res = MCValue::get(~Value.getConstant());
      break;
    case MCUnaryExpr::Plus:
      Res = Value;
      break;
    }

    return true;
  }

  case Binary: {
    const MCBinaryExpr *ABE = cast<MCBinaryExpr>(this);
    MCValue LHSValue, RHSValue;

    if (!ABE->getLHS()->evaluateAsRelocatableImpl(LHSValue, Asm, Fixup, Addrs,
                                                  InSet) ||
        !ABE->getRHS()->evaluateAsRelocatableImpl(RHSValue, Asm, Fixup, Addrs,
                                                  InSet)) {
      // Check if both are Target Expressions, see if we can compare them.
      if (const MCTargetExpr *L = dyn_cast<MCTargetExpr>(ABE->getLHS())) {
        if (const MCTargetExpr *R = dyn_cast<MCTargetExpr>(ABE->getRHS())) {
          switch (ABE->getOpcode()) {
          case MCBinaryExpr::EQ:
            Res = MCValue::get(L->isEqualTo(R) ? -1 : 0);
            return true;
          case MCBinaryExpr::NE:
            Res = MCValue::get(L->isEqualTo(R) ? 0 : -1);
            return true;
          default:
            break;
          }
        }
      }
      return false;
    }

    // We only support a few operations on non-constant expressions, handle
    // those first.
    if (!LHSValue.isAbsolute() || !RHSValue.isAbsolute()) {
      switch (ABE->getOpcode()) {
      default:
        return false;
      case MCBinaryExpr::Sub:
        // Negate RHS and add.
        // The cast avoids undefined behavior if the constant is INT64_MIN.
        return evaluateSymbolicAdd(
            Asm, Addrs, InSet, LHSValue,
            MCValue::get(RHSValue.getSymB(), RHSValue.getSymA(),
                         -(uint64_t)RHSValue.getConstant(),
                         RHSValue.getRefKind()),
            Res);

      case MCBinaryExpr::Add:
        return evaluateSymbolicAdd(
            Asm, Addrs, InSet, LHSValue,
            MCValue::get(RHSValue.getSymA(), RHSValue.getSymB(),
                         RHSValue.getConstant(), RHSValue.getRefKind()),
            Res);
      }
    }

    // FIXME: We need target hooks for the evaluation. It may be limited in
    // width, and gas defines the result of comparisons differently from
    // Apple as.
    int64_t LHS = LHSValue.getConstant(), RHS = RHSValue.getConstant();
    int64_t Result = 0;
    auto Op = ABE->getOpcode();
    switch (Op) {
    case MCBinaryExpr::AShr: Result = LHS >> RHS; break;
    case MCBinaryExpr::Add:  Result = LHS + RHS; break;
    case MCBinaryExpr::And:  Result = LHS & RHS; break;
    case MCBinaryExpr::Div:
    case MCBinaryExpr::Mod:
      // Handle division by zero. gas just emits a warning and keeps going,
      // we try to be stricter.
      // FIXME: Currently the caller of this function has no way to understand
      // we're bailing out because of 'division by zero'. Therefore, it will
      // emit a 'expected relocatable expression' error. It would be nice to
      // change this code to emit a better diagnostic.
      if (RHS == 0)
        return false;
      if (ABE->getOpcode() == MCBinaryExpr::Div)
        Result = LHS / RHS;
      else
        Result = LHS % RHS;
      break;
    case MCBinaryExpr::EQ:   Result = LHS == RHS; break;
    case MCBinaryExpr::GT:   Result = LHS > RHS; break;
    case MCBinaryExpr::GTE:  Result = LHS >= RHS; break;
    case MCBinaryExpr::LAnd: Result = LHS && RHS; break;
    case MCBinaryExpr::LOr:  Result = LHS || RHS; break;
    case MCBinaryExpr::LShr: Result = uint64_t(LHS) >> uint64_t(RHS); break;
    case MCBinaryExpr::LT:   Result = LHS < RHS; break;
    case MCBinaryExpr::LTE:  Result = LHS <= RHS; break;
    case MCBinaryExpr::Mul:  Result = LHS * RHS; break;
    case MCBinaryExpr::NE:   Result = LHS != RHS; break;
    case MCBinaryExpr::Or:   Result = LHS | RHS; break;
    case MCBinaryExpr::OrNot: Result = LHS | ~RHS; break;
    case MCBinaryExpr::Shl:  Result = uint64_t(LHS) << uint64_t(RHS); break;
    case MCBinaryExpr::Sub:  Result = LHS - RHS; break;
    case MCBinaryExpr::Xor:  Result = LHS ^ RHS; break;
    }

    switch (Op) {
    default:
      Res = MCValue::get(Result);
      break;
    case MCBinaryExpr::EQ:
    case MCBinaryExpr::GT:
    case MCBinaryExpr::GTE:
    case MCBinaryExpr::LT:
    case MCBinaryExpr::LTE:
    case MCBinaryExpr::NE:
      // A comparison operator returns a -1 if true and 0 if false.
      Res = MCValue::get(Result ? -1 : 0);
      break;
    }

    return true;
  }
  }

  llvm_unreachable("Invalid assembly expression kind!");
}

MCFragment *MCExpr::findAssociatedFragment() const {
  switch (getKind()) {
  case Target:
    // We never look through target specific expressions.
    return cast<MCTargetExpr>(this)->findAssociatedFragment();

  case Constant:
    return MCSymbol::AbsolutePseudoFragment;

  case SymbolRef: {
    const MCSymbolRefExpr *SRE = cast<MCSymbolRefExpr>(this);
    const MCSymbol &Sym = SRE->getSymbol();
    return Sym.getFragment();
  }

  case Unary:
    return cast<MCUnaryExpr>(this)->getSubExpr()->findAssociatedFragment();

  case Binary: {
    const MCBinaryExpr *BE = cast<MCBinaryExpr>(this);
    MCFragment *LHS_F = BE->getLHS()->findAssociatedFragment();
    MCFragment *RHS_F = BE->getRHS()->findAssociatedFragment();

    // If either is absolute, return the other.
    if (LHS_F == MCSymbol::AbsolutePseudoFragment)
      return RHS_F;
    if (RHS_F == MCSymbol::AbsolutePseudoFragment)
      return LHS_F;

    // Not always correct, but probably the best we can do without more context.
    if (BE->getOpcode() == MCBinaryExpr::Sub)
      return MCSymbol::AbsolutePseudoFragment;

    // Otherwise, return the first non-null fragment.
    return LHS_F ? LHS_F : RHS_F;
  }
  }

  llvm_unreachable("Invalid assembly expression kind!");
}
