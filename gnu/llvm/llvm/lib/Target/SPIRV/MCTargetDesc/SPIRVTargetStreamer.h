//===-- SPIRVTargetStreamer.h - SPIRV Target Streamer ----------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIB_TARGET_SPIRV_MCTARGETDESC_SPIRVTARGETSTREAMER_H
#define LIB_TARGET_SPIRV_MCTARGETDESC_SPIRVTARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

namespace llvm {

class MCSection;

class SPIRVTargetStreamer : public MCTargetStreamer {
public:
  SPIRVTargetStreamer(MCStreamer &S);
  ~SPIRVTargetStreamer() override;

  void changeSection(const MCSection *CurSection, MCSection *Section,
                     uint32_t SubSection, raw_ostream &OS) override {}
};
} // namespace llvm

#endif // LIB_TARGET_SPIRV_MCTARGETDESC_SPIRVTARGETSTREAMER_H_
