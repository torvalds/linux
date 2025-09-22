//===- MCAsmBackend.cpp - Target MC Assembly Backend ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCDXContainerWriter.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCGOFFObjectWriter.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSPIRVObjectWriter.h"
#include "llvm/MC/MCWasmObjectWriter.h"
#include "llvm/MC/MCWinCOFFObjectWriter.h"
#include "llvm/MC/MCXCOFFObjectWriter.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace llvm;

MCAsmBackend::MCAsmBackend(llvm::endianness Endian, unsigned RelaxFixupKind)
    : Endian(Endian), RelaxFixupKind(RelaxFixupKind) {}

MCAsmBackend::~MCAsmBackend() = default;

std::unique_ptr<MCObjectWriter>
MCAsmBackend::createObjectWriter(raw_pwrite_stream &OS) const {
  auto TW = createObjectTargetWriter();
  bool IsLE = Endian == llvm::endianness::little;
  switch (TW->getFormat()) {
  case Triple::MachO:
    return createMachObjectWriter(cast<MCMachObjectTargetWriter>(std::move(TW)),
                                  OS, IsLE);
  case Triple::COFF:
    return createWinCOFFObjectWriter(
        cast<MCWinCOFFObjectTargetWriter>(std::move(TW)), OS);
  case Triple::ELF:
    return std::make_unique<ELFObjectWriter>(
        cast<MCELFObjectTargetWriter>(std::move(TW)), OS, IsLE);
  case Triple::SPIRV:
    return createSPIRVObjectWriter(
        cast<MCSPIRVObjectTargetWriter>(std::move(TW)), OS);
  case Triple::Wasm:
    return createWasmObjectWriter(cast<MCWasmObjectTargetWriter>(std::move(TW)),
                                  OS);
  case Triple::GOFF:
    return createGOFFObjectWriter(cast<MCGOFFObjectTargetWriter>(std::move(TW)),
                                  OS);
  case Triple::XCOFF:
    return createXCOFFObjectWriter(
        cast<MCXCOFFObjectTargetWriter>(std::move(TW)), OS);
  case Triple::DXContainer:
    return createDXContainerObjectWriter(
        cast<MCDXContainerTargetWriter>(std::move(TW)), OS);
  default:
    llvm_unreachable("unexpected object format");
  }
}

std::unique_ptr<MCObjectWriter>
MCAsmBackend::createDwoObjectWriter(raw_pwrite_stream &OS,
                                    raw_pwrite_stream &DwoOS) const {
  auto TW = createObjectTargetWriter();
  switch (TW->getFormat()) {
  case Triple::COFF:
    return createWinCOFFDwoObjectWriter(
        cast<MCWinCOFFObjectTargetWriter>(std::move(TW)), OS, DwoOS);
  case Triple::ELF:
    return std::make_unique<ELFObjectWriter>(
        cast<MCELFObjectTargetWriter>(std::move(TW)), OS, DwoOS,
        Endian == llvm::endianness::little);
  case Triple::Wasm:
    return createWasmDwoObjectWriter(
        cast<MCWasmObjectTargetWriter>(std::move(TW)), OS, DwoOS);
  default:
    report_fatal_error("dwo only supported with COFF, ELF, and Wasm");
  }
}

std::optional<MCFixupKind> MCAsmBackend::getFixupKind(StringRef Name) const {
  return std::nullopt;
}

const MCFixupKindInfo &MCAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  static const MCFixupKindInfo Builtins[] = {
      {"FK_NONE", 0, 0, 0},
      {"FK_Data_1", 0, 8, 0},
      {"FK_Data_2", 0, 16, 0},
      {"FK_Data_4", 0, 32, 0},
      {"FK_Data_8", 0, 64, 0},
      {"FK_Data_leb128", 0, 0, 0},
      {"FK_PCRel_1", 0, 8, MCFixupKindInfo::FKF_IsPCRel},
      {"FK_PCRel_2", 0, 16, MCFixupKindInfo::FKF_IsPCRel},
      {"FK_PCRel_4", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"FK_PCRel_8", 0, 64, MCFixupKindInfo::FKF_IsPCRel},
      {"FK_GPRel_1", 0, 8, 0},
      {"FK_GPRel_2", 0, 16, 0},
      {"FK_GPRel_4", 0, 32, 0},
      {"FK_GPRel_8", 0, 64, 0},
      {"FK_DTPRel_4", 0, 32, 0},
      {"FK_DTPRel_8", 0, 64, 0},
      {"FK_TPRel_4", 0, 32, 0},
      {"FK_TPRel_8", 0, 64, 0},
      {"FK_SecRel_1", 0, 8, 0},
      {"FK_SecRel_2", 0, 16, 0},
      {"FK_SecRel_4", 0, 32, 0},
      {"FK_SecRel_8", 0, 64, 0},
  };

  assert((size_t)Kind <= std::size(Builtins) && "Unknown fixup kind");
  return Builtins[Kind];
}

bool MCAsmBackend::fixupNeedsRelaxationAdvanced(const MCAssembler &Asm,
                                                const MCFixup &Fixup,
                                                bool Resolved, uint64_t Value,
                                                const MCRelaxableFragment *DF,
                                                const bool WasForced) const {
  if (!Resolved)
    return true;
  return fixupNeedsRelaxation(Fixup, Value);
}

bool MCAsmBackend::isDarwinCanonicalPersonality(const MCSymbol *Sym) const {
  // Consider a NULL personality (ie., no personality encoding) to be canonical
  // because it's always at 0.
  if (!Sym)
    return true;

  if (!Sym->isMachO())
    llvm_unreachable("Expected MachO symbols only");

  StringRef name = Sym->getName();
  // XXX: We intentionally leave out "___gcc_personality_v0" because, despite
  // being system-defined like these two, it is not very commonly-used.
  // Reserving an empty slot for it seems silly.
  return name == "___gxx_personality_v0" || name == "___objc_personality_v0";
}
