//===-- ARMMCExpr.h - ARM specific MC expression classes --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMMCEXPR_H
#define LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMMCEXPR_H

#include "llvm/MC/MCExpr.h"

namespace llvm {

class ARMMCExpr : public MCTargetExpr {
public:
  enum VariantKind {
    VK_ARM_None,
    VK_ARM_HI16, // The R_ARM_MOVT_ABS relocation (:upper16: in the .s file)
    VK_ARM_LO16, // The R_ARM_MOVW_ABS_NC relocation (:lower16: in the .s file)

    VK_ARM_HI_8_15, // The R_ARM_THM_ALU_ABS_G3    relocation (:upper8_15: in
                    // the .s file)
    VK_ARM_HI_0_7,  // The R_ARM_THM_ALU_ABS_G2_NC relocation (:upper0_8: in the
                    // .s file)
    VK_ARM_LO_8_15, // The R_ARM_THM_ALU_ABS_G1_NC relocation (:lower8_15: in
                    // the .s file)
    VK_ARM_LO_0_7,  // The R_ARM_THM_ALU_ABS_G0_NC relocation (:lower0_7: in the
                    // .s file)
  };

private:
  const VariantKind Kind;
  const MCExpr *Expr;

  explicit ARMMCExpr(VariantKind Kind, const MCExpr *Expr)
      : Kind(Kind), Expr(Expr) {}

public:
  /// @name Construction
  /// @{

  static const ARMMCExpr *create(VariantKind Kind, const MCExpr *Expr,
                                      MCContext &Ctx);

  static const ARMMCExpr *createUpper16(const MCExpr *Expr, MCContext &Ctx) {
    return create(VK_ARM_HI16, Expr, Ctx);
  }

  static const ARMMCExpr *createLower16(const MCExpr *Expr, MCContext &Ctx) {
    return create(VK_ARM_LO16, Expr, Ctx);
  }

  static const ARMMCExpr *createUpper8_15(const MCExpr *Expr, MCContext &Ctx) {
    return create(VK_ARM_HI_8_15, Expr, Ctx);
  }

  static const ARMMCExpr *createUpper0_7(const MCExpr *Expr, MCContext &Ctx) {
    return create(VK_ARM_HI_0_7, Expr, Ctx);
  }

  static const ARMMCExpr *createLower8_15(const MCExpr *Expr, MCContext &Ctx) {
    return create(VK_ARM_LO_8_15, Expr, Ctx);
  }

  static const ARMMCExpr *createLower0_7(const MCExpr *Expr, MCContext &Ctx) {
    return create(VK_ARM_LO_0_7, Expr, Ctx);
  }

  /// @}
  /// @name Accessors
  /// @{

  /// getOpcode - Get the kind of this expression.
  VariantKind getKind() const { return Kind; }

  /// getSubExpr - Get the child of this expression.
  const MCExpr *getSubExpr() const { return Expr; }

  /// @}

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                 const MCFixup *Fixup) const override {
    return false;
  }
  void visitUsedExpr(MCStreamer &Streamer) const override;
  MCFragment *findAssociatedFragment() const override {
    return getSubExpr()->findAssociatedFragment();
  }

  // There are no TLS ARMMCExprs at the moment.
  void fixELFSymbolsInTLSFixups(MCAssembler &Asm) const override {}

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
};
} // end namespace llvm

#endif
