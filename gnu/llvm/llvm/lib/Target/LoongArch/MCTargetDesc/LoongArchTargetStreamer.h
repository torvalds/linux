//===-- LoongArchTargetStreamer.h - LoongArch Target Streamer --*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LOONGARCH_MCTARGETDESC_LOONGARCHTARGETSTREAMER_H
#define LLVM_LIB_TARGET_LOONGARCH_MCTARGETDESC_LOONGARCHTARGETSTREAMER_H

#include "LoongArch.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"

namespace llvm {
class LoongArchTargetStreamer : public MCTargetStreamer {
  LoongArchABI::ABI TargetABI = LoongArchABI::ABI_Unknown;

public:
  LoongArchTargetStreamer(MCStreamer &S);
  void setTargetABI(LoongArchABI::ABI ABI);
  LoongArchABI::ABI getTargetABI() const { return TargetABI; }
};

} // end namespace llvm
#endif
