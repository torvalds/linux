//===-- VETargetStreamer.h - VE Target Streamer ----------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VE_VETARGETSTREAMER_H
#define LLVM_LIB_TARGET_VE_VETARGETSTREAMER_H

#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/FormattedStream.h"

namespace llvm {
class VETargetStreamer : public MCTargetStreamer {
  virtual void anchor();

public:
  VETargetStreamer(MCStreamer &S);
  /// Emit ".register <reg>, #ignore".
  virtual void emitVERegisterIgnore(unsigned reg){};
  /// Emit ".register <reg>, #scratch".
  virtual void emitVERegisterScratch(unsigned reg){};
};

// This part is for ascii assembly output
class VETargetAsmStreamer : public VETargetStreamer {
  formatted_raw_ostream &OS;

public:
  VETargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);
  void emitVERegisterIgnore(unsigned reg) override;
  void emitVERegisterScratch(unsigned reg) override;
};

// This part is for ELF object output
class VETargetELFStreamer : public VETargetStreamer {
public:
  VETargetELFStreamer(MCStreamer &S);
  MCELFStreamer &getStreamer();
  void emitVERegisterIgnore(unsigned reg) override {}
  void emitVERegisterScratch(unsigned reg) override {}
};
} // namespace llvm

#endif
