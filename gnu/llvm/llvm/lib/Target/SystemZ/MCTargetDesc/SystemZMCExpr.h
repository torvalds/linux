//===-- SystemZMCExpr.h - SystemZ specific MC expression classes -*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_MCTARGETDESC_SYSTEMZMCEXPR_H
#define LLVM_LIB_TARGET_SYSTEMZ_MCTARGETDESC_SYSTEMZMCEXPR_H

#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCValue.h"

namespace llvm {

class SystemZMCExpr : public MCTargetExpr {
public:
// HLASM docs for address constants:
// https://www.ibm.com/docs/en/hla-and-tf/1.6?topic=value-address-constants
  enum VariantKind {
    VK_SystemZ_None,
    VK_SystemZ_RCon,            // Address of ADA of symbol.
    VK_SystemZ_VCon,            // Address of external function symbol.
  };

private:
  const VariantKind Kind;
  const MCExpr *Expr;

  explicit SystemZMCExpr(VariantKind Kind, const MCExpr *Expr)
      : Kind(Kind), Expr(Expr) {}

public:
  static const SystemZMCExpr *create(VariantKind Kind, const MCExpr *Expr,
                                     MCContext &Ctx);

  /// getOpcode - Get the kind of this expression.
  VariantKind getKind() const { return Kind; }

  /// getSubExpr - Get the child of this expression.
  const MCExpr *getSubExpr() const { return Expr; }

  StringRef getVariantKindName() const;

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                 const MCFixup *Fixup) const override;
  void visitUsedExpr(MCStreamer &Streamer) const override {
    Streamer.visitUsedExpr(*getSubExpr());
  }
  MCFragment *findAssociatedFragment() const override {
    return getSubExpr()->findAssociatedFragment();
  }

  // There are no TLS SystemZMCExprs at the moment.
  void fixELFSymbolsInTLSFixups(MCAssembler &Asm) const override {}

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
};
} // end namespace llvm

#endif
