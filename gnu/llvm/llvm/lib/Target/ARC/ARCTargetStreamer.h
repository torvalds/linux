//===- ARCTargetStreamer.h - ARC Target Streamer ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_ARCTARGETSTREAMER_H
#define LLVM_LIB_TARGET_ARC_ARCTARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

namespace llvm {

class ARCTargetStreamer : public MCTargetStreamer {
public:
  ARCTargetStreamer(MCStreamer &S);
  ~ARCTargetStreamer() override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARC_ARCTARGETSTREAMER_H
