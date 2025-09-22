//===-- CSKYMCExpr.h - CSKY specific MC expression classes -*- C++ -*----===//
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

class CSKYMCExpr : public MCTargetExpr {
public:
  enum VariantKind {
    VK_CSKY_None,
    VK_CSKY_ADDR,
    VK_CSKY_ADDR_HI16,
    VK_CSKY_ADDR_LO16,
    VK_CSKY_PCREL,
    VK_CSKY_GOT,
    VK_CSKY_GOT_IMM18_BY4,
    VK_CSKY_GOTPC,
    VK_CSKY_GOTOFF,
    VK_CSKY_PLT,
    VK_CSKY_PLT_IMM18_BY4,
    VK_CSKY_TLSIE,
    VK_CSKY_TLSLE,
    VK_CSKY_TLSGD,
    VK_CSKY_TLSLDO,
    VK_CSKY_TLSLDM,
    VK_CSKY_Invalid
  };

private:
  const VariantKind Kind;
  const MCExpr *Expr;

  explicit CSKYMCExpr(VariantKind Kind, const MCExpr *Expr)
      : Kind(Kind), Expr(Expr) {}

public:
  static const CSKYMCExpr *create(const MCExpr *Expr, VariantKind Kind,
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

  void fixELFSymbolsInTLSFixups(MCAssembler &Asm) const override;

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }

  static StringRef getVariantKindName(VariantKind Kind);
};
} // end namespace llvm

#endif
