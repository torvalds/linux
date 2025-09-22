//===-- NVPTXMCExpr.h - NVPTX specific MC expression classes ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Modeled after ARMMCExpr

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXMCEXPR_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXMCEXPR_H

#include "llvm/ADT/APFloat.h"
#include "llvm/MC/MCExpr.h"
#include <utility>

namespace llvm {

class NVPTXFloatMCExpr : public MCTargetExpr {
public:
  enum VariantKind {
    VK_NVPTX_None,
    VK_NVPTX_BFLOAT_PREC_FLOAT, // FP constant in bfloat-precision
    VK_NVPTX_HALF_PREC_FLOAT,   // FP constant in half-precision
    VK_NVPTX_SINGLE_PREC_FLOAT, // FP constant in single-precision
    VK_NVPTX_DOUBLE_PREC_FLOAT  // FP constant in double-precision
  };

private:
  const VariantKind Kind;
  const APFloat Flt;

  explicit NVPTXFloatMCExpr(VariantKind Kind, APFloat Flt)
      : Kind(Kind), Flt(std::move(Flt)) {}

public:
  /// @name Construction
  /// @{

  static const NVPTXFloatMCExpr *create(VariantKind Kind, const APFloat &Flt,
                                        MCContext &Ctx);

  static const NVPTXFloatMCExpr *createConstantBFPHalf(const APFloat &Flt,
                                                       MCContext &Ctx) {
    return create(VK_NVPTX_BFLOAT_PREC_FLOAT, Flt, Ctx);
  }

  static const NVPTXFloatMCExpr *createConstantFPHalf(const APFloat &Flt,
                                                        MCContext &Ctx) {
    return create(VK_NVPTX_HALF_PREC_FLOAT, Flt, Ctx);
  }

  static const NVPTXFloatMCExpr *createConstantFPSingle(const APFloat &Flt,
                                                        MCContext &Ctx) {
    return create(VK_NVPTX_SINGLE_PREC_FLOAT, Flt, Ctx);
  }

  static const NVPTXFloatMCExpr *createConstantFPDouble(const APFloat &Flt,
                                                        MCContext &Ctx) {
    return create(VK_NVPTX_DOUBLE_PREC_FLOAT, Flt, Ctx);
  }

  /// @}
  /// @name Accessors
  /// @{

  /// getOpcode - Get the kind of this expression.
  VariantKind getKind() const { return Kind; }

  /// getSubExpr - Get the child of this expression.
  APFloat getAPFloat() const { return Flt; }

/// @}

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                 const MCFixup *Fixup) const override {
    return false;
  }
  void visitUsedExpr(MCStreamer &Streamer) const override {};
  MCFragment *findAssociatedFragment() const override { return nullptr; }

  // There are no TLS NVPTXMCExprs at the moment.
  void fixELFSymbolsInTLSFixups(MCAssembler &Asm) const override {}

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
};

/// A wrapper for MCSymbolRefExpr that tells the assembly printer that the
/// symbol should be enclosed by generic().
class NVPTXGenericMCSymbolRefExpr : public MCTargetExpr {
private:
  const MCSymbolRefExpr *SymExpr;

  explicit NVPTXGenericMCSymbolRefExpr(const MCSymbolRefExpr *_SymExpr)
      : SymExpr(_SymExpr) {}

public:
  /// @name Construction
  /// @{

  static const NVPTXGenericMCSymbolRefExpr
  *create(const MCSymbolRefExpr *SymExpr, MCContext &Ctx);

  /// @}
  /// @name Accessors
  /// @{

  /// getOpcode - Get the kind of this expression.
  const MCSymbolRefExpr *getSymbolExpr() const { return SymExpr; }

  /// @}

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                 const MCFixup *Fixup) const override {
    return false;
  }
  void visitUsedExpr(MCStreamer &Streamer) const override {};
  MCFragment *findAssociatedFragment() const override { return nullptr; }

  // There are no TLS NVPTXMCExprs at the moment.
  void fixELFSymbolsInTLSFixups(MCAssembler &Asm) const override {}

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
  };
} // end namespace llvm

#endif
