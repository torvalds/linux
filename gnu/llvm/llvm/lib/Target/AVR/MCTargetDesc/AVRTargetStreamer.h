//===-- AVRTargetStreamer.h - AVR Target Streamer --------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_AVR_TARGET_STREAMER_H
#define LLVM_AVR_TARGET_STREAMER_H

#include "llvm/MC/MCELFStreamer.h"

namespace llvm {
class MCStreamer;

/// A generic AVR target output stream.
class AVRTargetStreamer : public MCTargetStreamer {
public:
  explicit AVRTargetStreamer(MCStreamer &S);
};

/// A target streamer for textual AVR assembly code.
class AVRTargetAsmStreamer : public AVRTargetStreamer {
public:
  explicit AVRTargetAsmStreamer(MCStreamer &S);
};

} // end namespace llvm

#endif // LLVM_AVR_TARGET_STREAMER_H
