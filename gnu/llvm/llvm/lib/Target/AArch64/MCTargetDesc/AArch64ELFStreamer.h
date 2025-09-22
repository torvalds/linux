//===-- AArch64ELFStreamer.h - ELF Streamer for AArch64 ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements ELF streamer information for the AArch64 backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64ELFSTREAMER_H
#define LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64ELFSTREAMER_H

#include "llvm/MC/MCELFStreamer.h"

namespace llvm {

MCELFStreamer *createAArch64ELFStreamer(MCContext &Context,
                                        std::unique_ptr<MCAsmBackend> TAB,
                                        std::unique_ptr<MCObjectWriter> OW,
                                        std::unique_ptr<MCCodeEmitter> Emitter);
}

#endif
