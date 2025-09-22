//=--- AArch64MCExpr.h - AArch64 specific MC expression classes ---*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes AArch64-specific MCExprs, used for modifiers like
// ":lo12:" or ":gottprel_g1:".
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64MCEXPR_H
#define LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64MCEXPR_H

#include "Utils/AArch64BaseInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm {

class AArch64MCExpr : public MCTargetExpr {
public:
  enum VariantKind {
    // Symbol locations specifying (roughly speaking) what calculation should be
    // performed to construct the final address for the relocated
    // symbol. E.g. direct, via the GOT, ...
    VK_ABS      = 0x001,
    VK_SABS     = 0x002,
    VK_PREL     = 0x003,
    VK_GOT      = 0x004,
    VK_DTPREL   = 0x005,
    VK_GOTTPREL = 0x006,
    VK_TPREL    = 0x007,
    VK_TLSDESC  = 0x008,
    VK_SECREL   = 0x009,
    VK_AUTH     = 0x00a,
    VK_AUTHADDR = 0x00b,
    VK_SymLocBits = 0x00f,

    // Variants specifying which part of the final address calculation is
    // used. E.g. the low 12 bits for an ADD/LDR, the middle 16 bits for a
    // MOVZ/MOVK.
    VK_PAGE     = 0x010,
    VK_PAGEOFF  = 0x020,
    VK_HI12     = 0x030,
    VK_G0       = 0x040,
    VK_G1       = 0x050,
    VK_G2       = 0x060,
    VK_G3       = 0x070,
    VK_LO15     = 0x080,
    VK_AddressFragBits = 0x0f0,

    // Whether the final relocation is a checked one (where a linker should
    // perform a range-check on the final address) or not. Note that this field
    // is unfortunately sometimes omitted from the assembly syntax. E.g. :lo12:
    // on its own is a non-checked relocation. We side with ELF on being
    // explicit about this!
    VK_NC       = 0x100,

    // Convenience definitions for referring to specific textual representations
    // of relocation specifiers. Note that this means the "_NC" is sometimes
    // omitted in line with assembly syntax here (VK_LO12 rather than VK_LO12_NC
    // since a user would write ":lo12:").
    VK_CALL              = VK_ABS,
    VK_ABS_PAGE          = VK_ABS      | VK_PAGE,
    VK_ABS_PAGE_NC       = VK_ABS      | VK_PAGE    | VK_NC,
    VK_ABS_G3            = VK_ABS      | VK_G3,
    VK_ABS_G2            = VK_ABS      | VK_G2,
    VK_ABS_G2_S          = VK_SABS     | VK_G2,
    VK_ABS_G2_NC         = VK_ABS      | VK_G2      | VK_NC,
    VK_ABS_G1            = VK_ABS      | VK_G1,
    VK_ABS_G1_S          = VK_SABS     | VK_G1,
    VK_ABS_G1_NC         = VK_ABS      | VK_G1      | VK_NC,
    VK_ABS_G0            = VK_ABS      | VK_G0,
    VK_ABS_G0_S          = VK_SABS     | VK_G0,
    VK_ABS_G0_NC         = VK_ABS      | VK_G0      | VK_NC,
    VK_LO12              = VK_ABS      | VK_PAGEOFF | VK_NC,
    VK_PREL_G3           = VK_PREL     | VK_G3,
    VK_PREL_G2           = VK_PREL     | VK_G2,
    VK_PREL_G2_NC        = VK_PREL     | VK_G2      | VK_NC,
    VK_PREL_G1           = VK_PREL     | VK_G1,
    VK_PREL_G1_NC        = VK_PREL     | VK_G1      | VK_NC,
    VK_PREL_G0           = VK_PREL     | VK_G0,
    VK_PREL_G0_NC        = VK_PREL     | VK_G0      | VK_NC,
    VK_GOT_LO12          = VK_GOT      | VK_PAGEOFF | VK_NC,
    VK_GOT_PAGE          = VK_GOT      | VK_PAGE,
    VK_GOT_PAGE_LO15     = VK_GOT      | VK_LO15    | VK_NC,
    VK_DTPREL_G2         = VK_DTPREL   | VK_G2,
    VK_DTPREL_G1         = VK_DTPREL   | VK_G1,
    VK_DTPREL_G1_NC      = VK_DTPREL   | VK_G1      | VK_NC,
    VK_DTPREL_G0         = VK_DTPREL   | VK_G0,
    VK_DTPREL_G0_NC      = VK_DTPREL   | VK_G0      | VK_NC,
    VK_DTPREL_HI12       = VK_DTPREL   | VK_HI12,
    VK_DTPREL_LO12       = VK_DTPREL   | VK_PAGEOFF,
    VK_DTPREL_LO12_NC    = VK_DTPREL   | VK_PAGEOFF | VK_NC,
    VK_GOTTPREL_PAGE     = VK_GOTTPREL | VK_PAGE,
    VK_GOTTPREL_LO12_NC  = VK_GOTTPREL | VK_PAGEOFF | VK_NC,
    VK_GOTTPREL_G1       = VK_GOTTPREL | VK_G1,
    VK_GOTTPREL_G0_NC    = VK_GOTTPREL | VK_G0      | VK_NC,
    VK_TPREL_G2          = VK_TPREL    | VK_G2,
    VK_TPREL_G1          = VK_TPREL    | VK_G1,
    VK_TPREL_G1_NC       = VK_TPREL    | VK_G1      | VK_NC,
    VK_TPREL_G0          = VK_TPREL    | VK_G0,
    VK_TPREL_G0_NC       = VK_TPREL    | VK_G0      | VK_NC,
    VK_TPREL_HI12        = VK_TPREL    | VK_HI12,
    VK_TPREL_LO12        = VK_TPREL    | VK_PAGEOFF,
    VK_TPREL_LO12_NC     = VK_TPREL    | VK_PAGEOFF | VK_NC,
    VK_TLSDESC_LO12      = VK_TLSDESC  | VK_PAGEOFF,
    VK_TLSDESC_PAGE      = VK_TLSDESC  | VK_PAGE,
    VK_SECREL_LO12       = VK_SECREL   | VK_PAGEOFF,
    VK_SECREL_HI12       = VK_SECREL   | VK_HI12,

    VK_INVALID  = 0xfff
  };

private:
  const MCExpr *Expr;
  const VariantKind Kind;

protected:
  explicit AArch64MCExpr(const MCExpr *Expr, VariantKind Kind)
    : Expr(Expr), Kind(Kind) {}

public:
  /// @name Construction
  /// @{

  static const AArch64MCExpr *create(const MCExpr *Expr, VariantKind Kind,
                                   MCContext &Ctx);

  /// @}
  /// @name Accessors
  /// @{

  /// Get the kind of this expression.
  VariantKind getKind() const { return Kind; }

  /// Get the expression this modifier applies to.
  const MCExpr *getSubExpr() const { return Expr; }

  /// @}
  /// @name VariantKind information extractors.
  /// @{

  static VariantKind getSymbolLoc(VariantKind Kind) {
    return static_cast<VariantKind>(Kind & VK_SymLocBits);
  }

  static VariantKind getAddressFrag(VariantKind Kind) {
    return static_cast<VariantKind>(Kind & VK_AddressFragBits);
  }

  static bool isNotChecked(VariantKind Kind) { return Kind & VK_NC; }

  /// @}

  /// Convert the variant kind into an ELF-appropriate modifier
  /// (e.g. ":got:", ":lo12:").
  StringRef getVariantKindName() const;

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;

  void visitUsedExpr(MCStreamer &Streamer) const override;

  MCFragment *findAssociatedFragment() const override;

  bool evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                 const MCFixup *Fixup) const override;

  void fixELFSymbolsInTLSFixups(MCAssembler &Asm) const override;

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
};

class AArch64AuthMCExpr final : public AArch64MCExpr {
  uint16_t Discriminator;
  AArch64PACKey::ID Key;

  explicit AArch64AuthMCExpr(const MCExpr *Expr, uint16_t Discriminator,
                             AArch64PACKey::ID Key, bool HasAddressDiversity)
      : AArch64MCExpr(Expr, HasAddressDiversity ? VK_AUTHADDR : VK_AUTH),
        Discriminator(Discriminator), Key(Key) {}

public:
  static const AArch64AuthMCExpr *
  create(const MCExpr *Expr, uint16_t Discriminator, AArch64PACKey::ID Key,
         bool HasAddressDiversity, MCContext &Ctx);

  AArch64PACKey::ID getKey() const { return Key; }
  uint16_t getDiscriminator() const { return Discriminator; }
  bool hasAddressDiversity() const { return getKind() == VK_AUTHADDR; }

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;

  void visitUsedExpr(MCStreamer &Streamer) const override;

  MCFragment *findAssociatedFragment() const override;

  bool evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                 const MCFixup *Fixup) const override;

  static bool classof(const MCExpr *E) {
    return isa<AArch64MCExpr>(E) && classof(cast<AArch64MCExpr>(E));
  }

  static bool classof(const AArch64MCExpr *E) {
    return E->getKind() == VK_AUTH || E->getKind() == VK_AUTHADDR;
  }
};
} // end namespace llvm

#endif
