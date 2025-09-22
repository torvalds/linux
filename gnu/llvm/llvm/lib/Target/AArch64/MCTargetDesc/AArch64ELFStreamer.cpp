//===- lib/MC/AArch64ELFStreamer.cpp - ELF Object Output for AArch64 ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file assembles .s files and emits AArch64 ELF .o object files. Different
// from generic ELF streamer in emitting mapping symbols ($x and $d) to delimit
// regions of data and code.
//
//===----------------------------------------------------------------------===//

#include "AArch64ELFStreamer.h"
#include "AArch64MCTargetDesc.h"
#include "AArch64TargetStreamer.h"
#include "AArch64WinCOFFStreamer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCWinCOFFStreamer.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace {

class AArch64ELFStreamer;

class AArch64TargetAsmStreamer : public AArch64TargetStreamer {
  formatted_raw_ostream &OS;

  void emitInst(uint32_t Inst) override;

  void emitDirectiveVariantPCS(MCSymbol *Symbol) override {
    OS << "\t.variant_pcs\t" << Symbol->getName() << "\n";
  }

  void emitARM64WinCFIAllocStack(unsigned Size) override {
    OS << "\t.seh_stackalloc\t" << Size << "\n";
  }
  void emitARM64WinCFISaveR19R20X(int Offset) override {
    OS << "\t.seh_save_r19r20_x\t" << Offset << "\n";
  }
  void emitARM64WinCFISaveFPLR(int Offset) override {
    OS << "\t.seh_save_fplr\t" << Offset << "\n";
  }
  void emitARM64WinCFISaveFPLRX(int Offset) override {
    OS << "\t.seh_save_fplr_x\t" << Offset << "\n";
  }
  void emitARM64WinCFISaveReg(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_reg\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveRegX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_reg_x\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveRegP(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_regp\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveRegPX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_regp_x\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveLRPair(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_lrpair\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveFReg(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_freg\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveFRegX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_freg_x\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveFRegP(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_fregp\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveFRegPX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_fregp_x\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISetFP() override { OS << "\t.seh_set_fp\n"; }
  void emitARM64WinCFIAddFP(unsigned Size) override {
    OS << "\t.seh_add_fp\t" << Size << "\n";
  }
  void emitARM64WinCFINop() override { OS << "\t.seh_nop\n"; }
  void emitARM64WinCFISaveNext() override { OS << "\t.seh_save_next\n"; }
  void emitARM64WinCFIPrologEnd() override { OS << "\t.seh_endprologue\n"; }
  void emitARM64WinCFIEpilogStart() override { OS << "\t.seh_startepilogue\n"; }
  void emitARM64WinCFIEpilogEnd() override { OS << "\t.seh_endepilogue\n"; }
  void emitARM64WinCFITrapFrame() override { OS << "\t.seh_trap_frame\n"; }
  void emitARM64WinCFIMachineFrame() override { OS << "\t.seh_pushframe\n"; }
  void emitARM64WinCFIContext() override { OS << "\t.seh_context\n"; }
  void emitARM64WinCFIECContext() override { OS << "\t.seh_ec_context\n"; }
  void emitARM64WinCFIClearUnwoundToCall() override {
    OS << "\t.seh_clear_unwound_to_call\n";
  }
  void emitARM64WinCFIPACSignLR() override {
    OS << "\t.seh_pac_sign_lr\n";
  }

  void emitARM64WinCFISaveAnyRegI(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegIP(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_p\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegD(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegDP(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_p\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegQ(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg\tq" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegQP(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_p\tq" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegIX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_x\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegIPX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_px\tx" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegDX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_x\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegDPX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_px\td" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegQX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_x\tq" << Reg << ", " << Offset << "\n";
  }
  void emitARM64WinCFISaveAnyRegQPX(unsigned Reg, int Offset) override {
    OS << "\t.seh_save_any_reg_px\tq" << Reg << ", " << Offset << "\n";
  }

public:
  AArch64TargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);
};

AArch64TargetAsmStreamer::AArch64TargetAsmStreamer(MCStreamer &S,
                                                   formatted_raw_ostream &OS)
  : AArch64TargetStreamer(S), OS(OS) {}

void AArch64TargetAsmStreamer::emitInst(uint32_t Inst) {
  OS << "\t.inst\t0x" << Twine::utohexstr(Inst) << "\n";
}

/// Extend the generic ELFStreamer class so that it can emit mapping symbols at
/// the appropriate points in the object files. These symbols are defined in the
/// AArch64 ELF ABI:
///    infocenter.arm.com/help/topic/com.arm.doc.ihi0056a/IHI0056A_aaelf64.pdf
///
/// In brief: $x or $d should be emitted at the start of each contiguous region
/// of A64 code or data in a section. In practice, this emission does not rely
/// on explicit assembler directives but on inherent properties of the
/// directives doing the emission (e.g. ".byte" is data, "add x0, x0, x0" an
/// instruction).
///
/// As a result this system is orthogonal to the DataRegion infrastructure used
/// by MachO. Beware!
class AArch64ELFStreamer : public MCELFStreamer {
public:
  AArch64ELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                     std::unique_ptr<MCObjectWriter> OW,
                     std::unique_ptr<MCCodeEmitter> Emitter)
      : MCELFStreamer(Context, std::move(TAB), std::move(OW),
                      std::move(Emitter)),
        LastEMS(EMS_None) {}

  void changeSection(MCSection *Section, uint32_t Subsection = 0) override {
    // We have to keep track of the mapping symbol state of any sections we
    // use. Each one should start off as EMS_None, which is provided as the
    // default constructor by DenseMap::lookup.
    LastMappingSymbols[getCurrentSection().first] = LastEMS;
    LastEMS = LastMappingSymbols.lookup(Section);

    MCELFStreamer::changeSection(Section, Subsection);
  }

  // Reset state between object emissions
  void reset() override {
    MCELFStreamer::reset();
    LastMappingSymbols.clear();
    LastEMS = EMS_None;
  }

  /// This function is the one used to emit instruction data into the ELF
  /// streamer. We override it to add the appropriate mapping symbol if
  /// necessary.
  void emitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo &STI) override {
    emitA64MappingSymbol();
    MCELFStreamer::emitInstruction(Inst, STI);
  }

  /// Emit a 32-bit value as an instruction. This is only used for the .inst
  /// directive, EmitInstruction should be used in other cases.
  void emitInst(uint32_t Inst) {
    char Buffer[4];

    // We can't just use EmitIntValue here, as that will emit a data mapping
    // symbol, and swap the endianness on big-endian systems (instructions are
    // always little-endian).
    for (char &C : Buffer) {
      C = uint8_t(Inst);
      Inst >>= 8;
    }

    emitA64MappingSymbol();
    MCELFStreamer::emitBytes(StringRef(Buffer, 4));
  }

  /// This is one of the functions used to emit data into an ELF section, so the
  /// AArch64 streamer overrides it to add the appropriate mapping symbol ($d)
  /// if necessary.
  void emitBytes(StringRef Data) override {
    emitDataMappingSymbol();
    MCELFStreamer::emitBytes(Data);
  }

  /// This is one of the functions used to emit data into an ELF section, so the
  /// AArch64 streamer overrides it to add the appropriate mapping symbol ($d)
  /// if necessary.
  void emitValueImpl(const MCExpr *Value, unsigned Size, SMLoc Loc) override {
    emitDataMappingSymbol();
    MCELFStreamer::emitValueImpl(Value, Size, Loc);
  }

  void emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                                  SMLoc Loc) override {
    emitDataMappingSymbol();
    MCObjectStreamer::emitFill(NumBytes, FillValue, Loc);
  }

private:
  enum ElfMappingSymbol {
    EMS_None,
    EMS_A64,
    EMS_Data
  };

  void emitDataMappingSymbol() {
    if (LastEMS == EMS_Data)
      return;
    emitMappingSymbol("$d");
    LastEMS = EMS_Data;
  }

  void emitA64MappingSymbol() {
    if (LastEMS == EMS_A64)
      return;
    emitMappingSymbol("$x");
    LastEMS = EMS_A64;
  }

  void emitMappingSymbol(StringRef Name) {
    auto *Symbol = cast<MCSymbolELF>(getContext().createLocalSymbol(Name));
    emitLabel(Symbol);
    Symbol->setType(ELF::STT_NOTYPE);
    Symbol->setBinding(ELF::STB_LOCAL);
  }

  DenseMap<const MCSection *, ElfMappingSymbol> LastMappingSymbols;
  ElfMappingSymbol LastEMS;
};
} // end anonymous namespace

AArch64ELFStreamer &AArch64TargetELFStreamer::getStreamer() {
  return static_cast<AArch64ELFStreamer &>(Streamer);
}

void AArch64TargetELFStreamer::emitInst(uint32_t Inst) {
  getStreamer().emitInst(Inst);
}

void AArch64TargetELFStreamer::emitDirectiveVariantPCS(MCSymbol *Symbol) {
  getStreamer().getAssembler().registerSymbol(*Symbol);
  cast<MCSymbolELF>(Symbol)->setOther(ELF::STO_AARCH64_VARIANT_PCS);
}

void AArch64TargetELFStreamer::finish() {
  AArch64TargetStreamer::finish();
  AArch64ELFStreamer &S = getStreamer();
  MCContext &Ctx = S.getContext();
  auto &Asm = S.getAssembler();
  MCSectionELF *MemtagSec = nullptr;
  for (const MCSymbol &Symbol : Asm.symbols()) {
    const auto &Sym = cast<MCSymbolELF>(Symbol);
    if (Sym.isMemtag()) {
      MemtagSec = Ctx.getELFSection(".memtag.globals.static",
                                    ELF::SHT_AARCH64_MEMTAG_GLOBALS_STATIC, 0);
      break;
    }
  }
  if (!MemtagSec)
    return;

  // switchSection registers the section symbol and invalidates symbols(). We
  // need a separate symbols() loop.
  S.switchSection(MemtagSec);
  const auto *Zero = MCConstantExpr::create(0, Ctx);
  for (const MCSymbol &Symbol : Asm.symbols()) {
    const auto &Sym = cast<MCSymbolELF>(Symbol);
    if (!Sym.isMemtag())
      continue;
    auto *SRE = MCSymbolRefExpr::create(&Sym, MCSymbolRefExpr::VK_None, Ctx);
    (void)S.emitRelocDirective(*Zero, "BFD_RELOC_NONE", SRE, SMLoc(),
                               *Ctx.getSubtargetInfo());
  }
}

MCTargetStreamer *
llvm::createAArch64AsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                                     MCInstPrinter *InstPrint) {
  return new AArch64TargetAsmStreamer(S, OS);
}

MCELFStreamer *
llvm::createAArch64ELFStreamer(MCContext &Context,
                               std::unique_ptr<MCAsmBackend> TAB,
                               std::unique_ptr<MCObjectWriter> OW,
                               std::unique_ptr<MCCodeEmitter> Emitter) {
  AArch64ELFStreamer *S = new AArch64ELFStreamer(
      Context, std::move(TAB), std::move(OW), std::move(Emitter));
  return S;
}
