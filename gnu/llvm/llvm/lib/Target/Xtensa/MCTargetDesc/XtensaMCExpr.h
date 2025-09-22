//===-- XtensaMCExpr.h - Xtensa specific MC expression classes --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes Xtensa-specific MCExprs
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XTENSA_MCTARGETDESC_XTENSAMCEXPR_H
#define LLVM_LIB_TARGET_XTENSA_MCTARGETDESC_XTENSAMCEXPR_H

#include "llvm/MC/MCExpr.h"

namespace llvm {

class StringRef;
class XtensaMCExpr : public MCTargetExpr {
public:
  enum VariantKind { VK_Xtensa_None, VK_Xtensa_Invalid };

private:
  const MCExpr *Expr;
  const VariantKind Kind;

  explicit XtensaMCExpr(const MCExpr *Expr, VariantKind Kind)
      : Expr(Expr), Kind(Kind) {}

public:
  static const XtensaMCExpr *create(const MCExpr *Expr, VariantKind Kind,
                                    MCContext &Ctx);

  VariantKind getKind() const { return Kind; }

  const MCExpr *getSubExpr() const { return Expr; }

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                 const MCFixup *Fixup) const override;
  void visitUsedExpr(MCStreamer &Streamer) const override;
  MCFragment *findAssociatedFragment() const override {
    return getSubExpr()->findAssociatedFragment();
  }

  void fixELFSymbolsInTLSFixups(MCAssembler &Asm) const override {}

  static VariantKind getVariantKindForName(StringRef name);
  static StringRef getVariantKindName(VariantKind Kind);
};

} // end namespace llvm.

#endif // LLVM_LIB_TARGET_XTENSA_MCTARGETDESC_XTENSAMCEXPR_H
