//===-- LanaiMCExpr.h - Lanai specific MC expression classes ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LANAI_MCTARGETDESC_LANAIMCEXPR_H
#define LLVM_LIB_TARGET_LANAI_MCTARGETDESC_LANAIMCEXPR_H

#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCValue.h"

namespace llvm {

class LanaiMCExpr : public MCTargetExpr {
public:
  enum VariantKind { VK_Lanai_None, VK_Lanai_ABS_HI, VK_Lanai_ABS_LO };

private:
  const VariantKind Kind;
  const MCExpr *Expr;

  explicit LanaiMCExpr(VariantKind Kind, const MCExpr *Expr)
      : Kind(Kind), Expr(Expr) {}

public:
  static const LanaiMCExpr *create(VariantKind Kind, const MCExpr *Expr,
                                   MCContext &Ctx);

  // Returns the kind of this expression.
  VariantKind getKind() const { return Kind; }

  // Returns the child of this expression.
  const MCExpr *getSubExpr() const { return Expr; }

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                 const MCFixup *Fixup) const override;
  void visitUsedExpr(MCStreamer &Streamer) const override;
  MCFragment *findAssociatedFragment() const override {
    return getSubExpr()->findAssociatedFragment();
  }

  // There are no TLS LanaiMCExprs at the moment.
  void fixELFSymbolsInTLSFixups(MCAssembler & /*Asm*/) const override {}

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
};
} // end namespace llvm

#endif
