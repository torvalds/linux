//=== HexagonMCELFStreamer.cpp - Hexagon subclass of MCELFStreamer -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a stub that parses a MCInst bundle and passes the
// instructions on to the real streamer.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonMCELFStreamer.h"
#include "HexagonTargetStreamer.h"
#include "MCTargetDesc/HexagonMCChecker.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCShuffler.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/HexagonAttributes.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>

#define DEBUG_TYPE "hexagonmcelfstreamer"

using namespace llvm;

static cl::opt<unsigned> GPSize
  ("gpsize", cl::NotHidden,
   cl::desc("Global Pointer Addressing Size.  The default size is 8."),
   cl::Prefix,
   cl::init(8));

HexagonMCELFStreamer::HexagonMCELFStreamer(
    MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
    std::unique_ptr<MCObjectWriter> OW, std::unique_ptr<MCCodeEmitter> Emitter)
    : MCELFStreamer(Context, std::move(TAB), std::move(OW), std::move(Emitter)),
      MCII(createHexagonMCInstrInfo()) {}

HexagonMCELFStreamer::HexagonMCELFStreamer(
    MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
    std::unique_ptr<MCObjectWriter> OW, std::unique_ptr<MCCodeEmitter> Emitter,
    MCAssembler *Assembler)
    : MCELFStreamer(Context, std::move(TAB), std::move(OW), std::move(Emitter)),
      MCII(createHexagonMCInstrInfo()) {}

void HexagonMCELFStreamer::emitInstruction(const MCInst &MCB,
                                           const MCSubtargetInfo &STI) {
  assert(MCB.getOpcode() == Hexagon::BUNDLE);
  assert(HexagonMCInstrInfo::bundleSize(MCB) <= HEXAGON_PACKET_SIZE);
  assert(HexagonMCInstrInfo::bundleSize(MCB) > 0);

  // At this point, MCB is a bundle
  // Iterate through the bundle and assign addends for the instructions
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCB)) {
    MCInst *MCI = const_cast<MCInst *>(I.getInst());
    EmitSymbol(*MCI);
  }

  MCObjectStreamer::emitInstruction(MCB, STI);
}

void HexagonMCELFStreamer::EmitSymbol(const MCInst &Inst) {
  // Scan for values.
  for (unsigned i = Inst.getNumOperands(); i--;)
    if (Inst.getOperand(i).isExpr())
      visitUsedExpr(*Inst.getOperand(i).getExpr());
}

// EmitCommonSymbol and EmitLocalCommonSymbol are extended versions of the
// functions found in MCELFStreamer.cpp taking AccessSize as an additional
// parameter.
void HexagonMCELFStreamer::HexagonMCEmitCommonSymbol(MCSymbol *Symbol,
                                                     uint64_t Size,
                                                     Align ByteAlignment,
                                                     unsigned AccessSize) {
  getAssembler().registerSymbol(*Symbol);
  StringRef sbss[4] = {".sbss.1", ".sbss.2", ".sbss.4", ".sbss.8"};

  auto ELFSymbol = cast<MCSymbolELF>(Symbol);
  if (!ELFSymbol->isBindingSet())
    ELFSymbol->setBinding(ELF::STB_GLOBAL);

  ELFSymbol->setType(ELF::STT_OBJECT);

  if (ELFSymbol->getBinding() == ELF::STB_LOCAL) {
    StringRef SectionName =
        ((AccessSize == 0) || (Size == 0) || (Size > GPSize))
            ? ".bss"
            : sbss[(Log2_64(AccessSize))];
    MCSection &Section = *getAssembler().getContext().getELFSection(
        SectionName, ELF::SHT_NOBITS, ELF::SHF_WRITE | ELF::SHF_ALLOC);
    MCSectionSubPair P = getCurrentSection();
    switchSection(&Section);

    if (ELFSymbol->isUndefined()) {
      emitValueToAlignment(ByteAlignment, 0, 1, 0);
      emitLabel(Symbol);
      emitZeros(Size);
    }

    // Update the maximum alignment of the section if necessary.
    Section.ensureMinAlignment(ByteAlignment);

    switchSection(P.first, P.second);
  } else {
    if (ELFSymbol->declareCommon(Size, ByteAlignment))
      report_fatal_error("Symbol: " + Symbol->getName() +
                         " redeclared as different type");
    if ((AccessSize) && (Size <= GPSize)) {
      uint64_t SectionIndex =
          (AccessSize <= GPSize)
              ? ELF::SHN_HEXAGON_SCOMMON + llvm::bit_width(AccessSize)
              : (unsigned)ELF::SHN_HEXAGON_SCOMMON;
      ELFSymbol->setIndex(SectionIndex);
    }
  }

  ELFSymbol->setSize(MCConstantExpr::create(Size, getContext()));
}

void HexagonMCELFStreamer::HexagonMCEmitLocalCommonSymbol(MCSymbol *Symbol,
                                                          uint64_t Size,
                                                          Align ByteAlignment,
                                                          unsigned AccessSize) {
  getAssembler().registerSymbol(*Symbol);
  auto ELFSymbol = cast<MCSymbolELF>(Symbol);
  ELFSymbol->setBinding(ELF::STB_LOCAL);
  ELFSymbol->setExternal(false);
  HexagonMCEmitCommonSymbol(Symbol, Size, ByteAlignment, AccessSize);
}

static unsigned featureToArchVersion(unsigned Feature) {
  switch (Feature) {
  case Hexagon::ArchV5:
    return 5;
  case Hexagon::ArchV55:
    return 55;
  case Hexagon::ArchV60:
  case Hexagon::ExtensionHVXV60:
    return 60;
  case Hexagon::ArchV62:
  case Hexagon::ExtensionHVXV62:
    return 62;
  case Hexagon::ArchV65:
  case Hexagon::ExtensionHVXV65:
    return 65;
  case Hexagon::ArchV66:
  case Hexagon::ExtensionHVXV66:
    return 66;
  case Hexagon::ArchV67:
  case Hexagon::ExtensionHVXV67:
    return 67;
  case Hexagon::ArchV68:
  case Hexagon::ExtensionHVXV68:
    return 68;
  case Hexagon::ArchV69:
  case Hexagon::ExtensionHVXV69:
    return 69;
  case Hexagon::ArchV71:
  case Hexagon::ExtensionHVXV71:
    return 71;
  case Hexagon::ArchV73:
  case Hexagon::ExtensionHVXV73:
    return 73;
  }
  llvm_unreachable("Expected valid arch feature");
  return 0;
}

void HexagonTargetStreamer::emitTargetAttributes(const MCSubtargetInfo &STI) {
  auto Features = STI.getFeatureBits();
  unsigned Arch = featureToArchVersion(Hexagon_MC::getArchVersion(Features));
  std::optional<unsigned> HVXArch = Hexagon_MC::getHVXVersion(Features);
  emitAttribute(HexagonAttrs::ARCH, Arch);
  if (HVXArch)
    emitAttribute(HexagonAttrs::HVXARCH, featureToArchVersion(*HVXArch));
  if (Features.test(Hexagon::ExtensionHVXIEEEFP))
    emitAttribute(HexagonAttrs::HVXIEEEFP, 1);
  if (Features.test(Hexagon::ExtensionHVXQFloat))
    emitAttribute(HexagonAttrs::HVXQFLOAT, 1);
  if (Features.test(Hexagon::ExtensionZReg))
    emitAttribute(HexagonAttrs::ZREG, 1);
  if (Features.test(Hexagon::ExtensionAudio))
    emitAttribute(HexagonAttrs::AUDIO, 1);
  if (Features.test(Hexagon::FeatureCabac))
    emitAttribute(HexagonAttrs::CABAC, 1);
}

namespace llvm {
MCStreamer *createHexagonELFStreamer(Triple const &TT, MCContext &Context,
                                     std::unique_ptr<MCAsmBackend> MAB,
                                     std::unique_ptr<MCObjectWriter> OW,
                                     std::unique_ptr<MCCodeEmitter> CE) {
  return new HexagonMCELFStreamer(Context, std::move(MAB), std::move(OW),
                                  std::move(CE));
  }

} // end namespace llvm
