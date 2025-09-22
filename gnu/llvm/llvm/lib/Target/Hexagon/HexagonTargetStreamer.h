//===-- HexagonTargetStreamer.h - Hexagon Target Streamer ------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef HEXAGONTARGETSTREAMER_H
#define HEXAGONTARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

namespace llvm {
class HexagonTargetStreamer : public MCTargetStreamer {
public:
  HexagonTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}
  virtual void emitCodeAlignment(Align Alignment, const MCSubtargetInfo *STI,
                                 unsigned MaxBytesToEmit = 0){};
  virtual void emitFAlign(unsigned Size, unsigned MaxBytesToEmit){};
  virtual void emitCommonSymbolSorted(MCSymbol *Symbol, uint64_t Size,
                                      unsigned ByteAlignment,
                                      unsigned AccessGranularity){};
  virtual void emitLocalCommonSymbolSorted(MCSymbol *Symbol, uint64_t Size,
                                           unsigned ByteAlign,
                                           unsigned AccessGranularity){};
  void finish() override {}

  virtual void finishAttributeSection() {}

  virtual void emitAttribute(unsigned Attribute, unsigned Value) {}

  void emitTargetAttributes(const MCSubtargetInfo &STI);

  virtual void reset() {}
};
}

#endif
