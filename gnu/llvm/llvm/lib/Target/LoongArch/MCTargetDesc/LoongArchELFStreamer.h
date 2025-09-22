//==-- LoongArchELFStreamer.h - LoongArch ELF Target Streamer --*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LOONGARCH_MCTARGETDESC_LOONGARCHELFSTREAMER_H
#define LLVM_LIB_TARGET_LOONGARCH_MCTARGETDESC_LOONGARCHELFSTREAMER_H

#include "LoongArchTargetStreamer.h"
#include "llvm/MC/MCELFStreamer.h"

namespace llvm {

class LoongArchTargetELFStreamer : public LoongArchTargetStreamer {
public:
  MCELFStreamer &getStreamer();
  LoongArchTargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

  void finish() override;
};

MCELFStreamer *createLoongArchELFStreamer(MCContext &C,
                                          std::unique_ptr<MCAsmBackend> MAB,
                                          std::unique_ptr<MCObjectWriter> MOW,
                                          std::unique_ptr<MCCodeEmitter> MCE);
} // end namespace llvm
#endif
