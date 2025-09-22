//====- VEMCExpr.h - VE specific MC expression classes --------*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes VE-specific MCExprs, used for modifiers like
// "%hi" or "%lo" etc.,
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_VE_MCTARGETDESC_VEMCEXPR_H
#define LLVM_LIB_TARGET_VE_MCTARGETDESC_VEMCEXPR_H

#include "VEFixupKinds.h"
#include "llvm/MC/MCExpr.h"

namespace llvm {

class StringRef;
class VEMCExpr : public MCTargetExpr {
public:
  enum VariantKind {
    VK_VE_None,
    VK_VE_REFLONG,
    VK_VE_HI32,
    VK_VE_LO32,
    VK_VE_PC_HI32,
    VK_VE_PC_LO32,
    VK_VE_GOT_HI32,
    VK_VE_GOT_LO32,
    VK_VE_GOTOFF_HI32,
    VK_VE_GOTOFF_LO32,
    VK_VE_PLT_HI32,
    VK_VE_PLT_LO32,
    VK_VE_TLS_GD_HI32,
    VK_VE_TLS_GD_LO32,
    VK_VE_TPOFF_HI32,
    VK_VE_TPOFF_LO32,
  };

private:
  const VariantKind Kind;
  const MCExpr *Expr;

  explicit VEMCExpr(VariantKind Kind, const MCExpr *Expr)
      : Kind(Kind), Expr(Expr) {}

public:
  /// @name Construction
  /// @{

  static const VEMCExpr *create(VariantKind Kind, const MCExpr *Expr,
                                MCContext &Ctx);
  /// @}
  /// @name Accessors
  /// @{

  /// getOpcode - Get the kind of this expression.
  VariantKind getKind() const { return Kind; }

  /// getSubExpr - Get the child of this expression.
  const MCExpr *getSubExpr() const { return Expr; }

  /// getFixupKind - Get the fixup kind of this expression.
  VE::Fixups getFixupKind() const { return getFixupKind(Kind); }

  /// @}
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

  static VariantKind parseVariantKind(StringRef name);
  static bool printVariantKind(raw_ostream &OS, VariantKind Kind);
  static void printVariantKindSuffix(raw_ostream &OS, VariantKind Kind);
  static VE::Fixups getFixupKind(VariantKind Kind);
};

} // namespace llvm

#endif
