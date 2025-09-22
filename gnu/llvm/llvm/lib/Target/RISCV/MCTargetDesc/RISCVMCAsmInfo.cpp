//===-- RISCVMCAsmInfo.cpp - RISC-V Asm properties ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the RISCVMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "RISCVMCAsmInfo.h"
#include "MCTargetDesc/RISCVMCExpr.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/TargetParser/Triple.h"
using namespace llvm;

void RISCVMCAsmInfo::anchor() {}

RISCVMCAsmInfo::RISCVMCAsmInfo(const Triple &TT) {
  CodePointerSize = CalleeSaveStackSlotSize = TT.isArch64Bit() ? 8 : 4;
  CommentString = "#";
  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
  Data16bitsDirective = "\t.half\t";
  Data32bitsDirective = "\t.word\t";
}

const MCExpr *RISCVMCAsmInfo::getExprForFDESymbol(const MCSymbol *Sym,
                                                  unsigned Encoding,
                                                  MCStreamer &Streamer) const {
  if (!(Encoding & dwarf::DW_EH_PE_pcrel))
    return MCAsmInfo::getExprForFDESymbol(Sym, Encoding, Streamer);

  // The default symbol subtraction results in an ADD/SUB relocation pair.
  // Processing this relocation pair is problematic when linker relaxation is
  // enabled, so we follow binutils in using the R_RISCV_32_PCREL relocation
  // for the FDE initial location.
  MCContext &Ctx = Streamer.getContext();
  const MCExpr *ME =
      MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None, Ctx);
  assert(Encoding & dwarf::DW_EH_PE_sdata4 && "Unexpected encoding");
  return RISCVMCExpr::create(ME, RISCVMCExpr::VK_RISCV_32_PCREL, Ctx);
}
