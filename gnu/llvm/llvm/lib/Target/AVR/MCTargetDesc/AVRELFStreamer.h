//===----- AVRELFStreamer.h - AVR Target Streamer --------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_AVR_ELF_STREAMER_H
#define LLVM_AVR_ELF_STREAMER_H

#include "AVRTargetStreamer.h"

namespace llvm {

/// A target streamer for an AVR ELF object file.
class AVRELFStreamer : public AVRTargetStreamer {
public:
  AVRELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

  MCELFStreamer &getStreamer() {
    return static_cast<MCELFStreamer &>(Streamer);
  }
};

} // end namespace llvm

#endif
