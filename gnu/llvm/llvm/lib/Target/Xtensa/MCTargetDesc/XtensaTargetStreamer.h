//===-- XtensaTargetStreamer.h - Xtensa Target Streamer --------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XTENSA_XTENSATARGETSTREAMER_H
#define LLVM_LIB_TARGET_XTENSA_XTENSATARGETSTREAMER_H

#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/SMLoc.h"

namespace llvm {
class formatted_raw_ostream;

class XtensaTargetStreamer : public MCTargetStreamer {
public:
  XtensaTargetStreamer(MCStreamer &S);

  // Emit literal label and literal Value to the literal section. If literal
  // section is not switched yet (SwitchLiteralSection is true) then switch to
  // literal section.
  virtual void emitLiteral(MCSymbol *LblSym, const MCExpr *Value,
                           bool SwitchLiteralSection, SMLoc L = SMLoc()) = 0;

  virtual void emitLiteralPosition() = 0;

  // Switch to the literal section. The BaseSection name is used to construct
  // literal section name.
  virtual void startLiteralSection(MCSection *BaseSection) = 0;
};

class XtensaTargetAsmStreamer : public XtensaTargetStreamer {
  formatted_raw_ostream &OS;

public:
  XtensaTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);
  void emitLiteral(MCSymbol *LblSym, const MCExpr *Value,
                   bool SwitchLiteralSection, SMLoc L) override;
  void emitLiteralPosition() override;
  void startLiteralSection(MCSection *Section) override;
};

class XtensaTargetELFStreamer : public XtensaTargetStreamer {
public:
  XtensaTargetELFStreamer(MCStreamer &S);
  MCELFStreamer &getStreamer();
  void emitLiteral(MCSymbol *LblSym, const MCExpr *Value,
                   bool SwitchLiteralSection, SMLoc L) override;
  void emitLiteralPosition() override {}
  void startLiteralSection(MCSection *Section) override;
};
} // end namespace llvm

#endif // LLVM_LIB_TARGET_XTENSA_XTENSATARGETSTREAMER_H
