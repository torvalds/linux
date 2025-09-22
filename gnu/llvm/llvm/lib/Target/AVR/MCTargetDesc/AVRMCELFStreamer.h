//===--------- AVRMCELFStreamer.h - AVR subclass of MCELFStreamer ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AVR_MCTARGETDESC_AVRMCELFSTREAMER_H
#define LLVM_LIB_TARGET_AVR_MCTARGETDESC_AVRMCELFSTREAMER_H

#include "MCTargetDesc/AVRMCExpr.h"
#include "MCTargetDesc/AVRMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"

namespace llvm {

const int SIZE_LONG = 4;
const int SIZE_WORD = 2;

class AVRMCELFStreamer : public MCELFStreamer {
  std::unique_ptr<MCInstrInfo> MCII;

public:
  AVRMCELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                   std::unique_ptr<MCObjectWriter> OW,
                   std::unique_ptr<MCCodeEmitter> Emitter)
      : MCELFStreamer(Context, std::move(TAB), std::move(OW),
                      std::move(Emitter)),
        MCII(createAVRMCInstrInfo()) {}

  AVRMCELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                   std::unique_ptr<MCObjectWriter> OW,
                   std::unique_ptr<MCCodeEmitter> Emitter,
                   MCAssembler *Assembler)
      : MCELFStreamer(Context, std::move(TAB), std::move(OW),
                      std::move(Emitter)),
        MCII(createAVRMCInstrInfo()) {}

  void emitValueForModiferKind(
      const MCSymbol *Sym, unsigned SizeInBytes, SMLoc Loc = SMLoc(),
      AVRMCExpr::VariantKind ModifierKind = AVRMCExpr::VK_AVR_None);
};

MCStreamer *createAVRELFStreamer(Triple const &TT, MCContext &Context,
                                 std::unique_ptr<MCAsmBackend> MAB,
                                 std::unique_ptr<MCObjectWriter> OW,
                                 std::unique_ptr<MCCodeEmitter> CE);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AVR_MCTARGETDESC_AVRMCELFSTREAMER_H
