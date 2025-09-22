//===-- XCoreTargetStreamer.h - XCore Target Streamer ----------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XCORE_XCORETARGETSTREAMER_H
#define LLVM_LIB_TARGET_XCORE_XCORETARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

namespace llvm {
class XCoreTargetStreamer : public MCTargetStreamer {
public:
  XCoreTargetStreamer(MCStreamer &S);
  ~XCoreTargetStreamer() override;
  virtual void emitCCTopData(StringRef Name){};
  virtual void emitCCTopFunction(StringRef Name){};
  virtual void emitCCBottomData(StringRef Name){};
  virtual void emitCCBottomFunction(StringRef Name){};
};
}

#endif
