//====- SparcMCExpr.h - Sparc specific MC expression classes --*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes Sparc-specific MCExprs, used for modifiers like
// "%hi" or "%lo" etc.,
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPARC_MCTARGETDESC_SPARCMCEXPR_H
#define LLVM_LIB_TARGET_SPARC_MCTARGETDESC_SPARCMCEXPR_H

#include "SparcFixupKinds.h"
#include "llvm/MC/MCExpr.h"

namespace llvm {

class StringRef;
class SparcMCExpr : public MCTargetExpr {
public:
  enum VariantKind {
    VK_Sparc_None,
    VK_Sparc_LO,
    VK_Sparc_HI,
    VK_Sparc_H44,
    VK_Sparc_M44,
    VK_Sparc_L44,
    VK_Sparc_HH,
    VK_Sparc_HM,
    VK_Sparc_LM,
    VK_Sparc_PC22,
    VK_Sparc_PC10,
    VK_Sparc_GOT22,
    VK_Sparc_GOT10,
    VK_Sparc_GOT13,
    VK_Sparc_13,
    VK_Sparc_WPLT30,
    VK_Sparc_WDISP30,
    VK_Sparc_R_DISP32,
    VK_Sparc_TLS_GD_HI22,
    VK_Sparc_TLS_GD_LO10,
    VK_Sparc_TLS_GD_ADD,
    VK_Sparc_TLS_GD_CALL,
    VK_Sparc_TLS_LDM_HI22,
    VK_Sparc_TLS_LDM_LO10,
    VK_Sparc_TLS_LDM_ADD,
    VK_Sparc_TLS_LDM_CALL,
    VK_Sparc_TLS_LDO_HIX22,
    VK_Sparc_TLS_LDO_LOX10,
    VK_Sparc_TLS_LDO_ADD,
    VK_Sparc_TLS_IE_HI22,
    VK_Sparc_TLS_IE_LO10,
    VK_Sparc_TLS_IE_LD,
    VK_Sparc_TLS_IE_LDX,
    VK_Sparc_TLS_IE_ADD,
    VK_Sparc_TLS_LE_HIX22,
    VK_Sparc_TLS_LE_LOX10,
    VK_Sparc_HIX22,
    VK_Sparc_LOX10,
    VK_Sparc_GOTDATA_HIX22,
    VK_Sparc_GOTDATA_LOX10,
    VK_Sparc_GOTDATA_OP,
  };

private:
  const VariantKind Kind;
  const MCExpr *Expr;

  explicit SparcMCExpr(VariantKind Kind, const MCExpr *Expr)
      : Kind(Kind), Expr(Expr) {}

public:
  /// @name Construction
  /// @{

  static const SparcMCExpr *create(VariantKind Kind, const MCExpr *Expr,
                                 MCContext &Ctx);
  /// @}
  /// @name Accessors
  /// @{

  /// getOpcode - Get the kind of this expression.
  VariantKind getKind() const { return Kind; }

  /// getSubExpr - Get the child of this expression.
  const MCExpr *getSubExpr() const { return Expr; }

  /// getFixupKind - Get the fixup kind of this expression.
  Sparc::Fixups getFixupKind() const { return getFixupKind(Kind); }

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
  static Sparc::Fixups getFixupKind(VariantKind Kind);
};

} // end namespace llvm.

#endif
