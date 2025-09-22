//===-- RISCVELFStreamer.h - RISC-V ELF Target Streamer ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVELFSTREAMER_H
#define LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVELFSTREAMER_H

#include "RISCVTargetStreamer.h"
#include "llvm/MC/MCELFStreamer.h"

using namespace llvm;

class RISCVELFStreamer : public MCELFStreamer {
  void reset() override;
  void emitDataMappingSymbol();
  void emitInstructionsMappingSymbol();
  void emitMappingSymbol(StringRef Name);

  enum ElfMappingSymbol { EMS_None, EMS_Instructions, EMS_Data };

  DenseMap<const MCSection *, ElfMappingSymbol> LastMappingSymbols;
  ElfMappingSymbol LastEMS = EMS_None;

public:
  RISCVELFStreamer(MCContext &C, std::unique_ptr<MCAsmBackend> MAB,
                   std::unique_ptr<MCObjectWriter> MOW,
                   std::unique_ptr<MCCodeEmitter> MCE)
      : MCELFStreamer(C, std::move(MAB), std::move(MOW), std::move(MCE)) {}

  void changeSection(MCSection *Section, uint32_t Subsection) override;
  void emitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI) override;
  void emitBytes(StringRef Data) override;
  void emitFill(const MCExpr &NumBytes, uint64_t FillValue, SMLoc Loc) override;
  void emitValueImpl(const MCExpr *Value, unsigned Size, SMLoc Loc) override;
};

namespace llvm {

class RISCVTargetELFStreamer : public RISCVTargetStreamer {
private:
  StringRef CurrentVendor;

  MCSection *AttributeSection = nullptr;

  void emitAttribute(unsigned Attribute, unsigned Value) override;
  void emitTextAttribute(unsigned Attribute, StringRef String) override;
  void emitIntTextAttribute(unsigned Attribute, unsigned IntValue,
                            StringRef StringValue) override;
  void finishAttributeSection() override;

  void reset() override;

public:
  RISCVELFStreamer &getStreamer();
  RISCVTargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

  void emitDirectiveOptionPush() override;
  void emitDirectiveOptionPop() override;
  void emitDirectiveOptionPIC() override;
  void emitDirectiveOptionNoPIC() override;
  void emitDirectiveOptionRVC() override;
  void emitDirectiveOptionNoRVC() override;
  void emitDirectiveOptionRelax() override;
  void emitDirectiveOptionNoRelax() override;
  void emitDirectiveVariantCC(MCSymbol &Symbol) override;

  void finish() override;
};

MCELFStreamer *createRISCVELFStreamer(MCContext &C,
                                      std::unique_ptr<MCAsmBackend> MAB,
                                      std::unique_ptr<MCObjectWriter> MOW,
                                      std::unique_ptr<MCCodeEmitter> MCE);
}
#endif
