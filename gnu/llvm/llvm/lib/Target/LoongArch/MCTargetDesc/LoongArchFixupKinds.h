//===- LoongArchFixupKinds.h - LoongArch Specific Fixup Entries -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LOONGARCH_MCTARGETDESC_LOONGARCHFIXUPKINDS_H
#define LLVM_LIB_TARGET_LOONGARCH_MCTARGETDESC_LOONGARCHFIXUPKINDS_H

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCFixup.h"

#undef LoongArch

namespace llvm {
namespace LoongArch {
//
// This table *must* be in the same order of
// MCFixupKindInfo Infos[LoongArch::NumTargetFixupKinds] in
// LoongArchAsmBackend.cpp.
//
enum Fixups {
  // Define fixups can be handled by LoongArchAsmBackend::applyFixup.
  // 16-bit fixup corresponding to %b16(foo) for instructions like bne.
  fixup_loongarch_b16 = FirstTargetFixupKind,
  // 21-bit fixup corresponding to %b21(foo) for instructions like bnez.
  fixup_loongarch_b21,
  // 26-bit fixup corresponding to %b26(foo)/%plt(foo) for instructions b/bl.
  fixup_loongarch_b26,
  // 20-bit fixup corresponding to %abs_hi20(foo) for instruction lu12i.w.
  fixup_loongarch_abs_hi20,
  // 12-bit fixup corresponding to %abs_lo12(foo) for instruction ori.
  fixup_loongarch_abs_lo12,
  // 20-bit fixup corresponding to %abs64_lo20(foo) for instruction lu32i.d.
  fixup_loongarch_abs64_lo20,
  // 12-bit fixup corresponding to %abs_hi12(foo) for instruction lu52i.d.
  fixup_loongarch_abs64_hi12,
  // 20-bit fixup corresponding to %le_hi20(foo) for instruction lu12i.w.
  fixup_loongarch_tls_le_hi20,
  // 12-bit fixup corresponding to %le_lo12(foo) for instruction ori.
  fixup_loongarch_tls_le_lo12,
  // 20-bit fixup corresponding to %le64_lo20(foo) for instruction lu32i.d.
  fixup_loongarch_tls_le64_lo20,
  // 12-bit fixup corresponding to %le64_hi12(foo) for instruction lu52i.d.
  fixup_loongarch_tls_le64_hi12,
  // TODO: Add more fixup kind.

  // Used as a sentinel, must be the last of the fixup which can be handled by
  // LoongArchAsmBackend::applyFixup.
  fixup_loongarch_invalid,
  NumTargetFixupKinds = fixup_loongarch_invalid - FirstTargetFixupKind,

  // Define fixups for force relocation as FirstLiteralRelocationKind+V
  // represents the relocation type with number V.
  // 20-bit fixup corresponding to %pc_hi20(foo) for instruction pcalau12i.
  fixup_loongarch_pcala_hi20 =
      FirstLiteralRelocationKind + ELF::R_LARCH_PCALA_HI20,
  // 12-bit fixup corresponding to %pc_lo12(foo) for instructions like addi.w/d.
  fixup_loongarch_pcala_lo12,
  // 20-bit fixup corresponding to %pc64_lo20(foo) for instruction lu32i.d.
  fixup_loongarch_pcala64_lo20,
  // 12-bit fixup corresponding to %pc64_hi12(foo) for instruction lu52i.d.
  fixup_loongarch_pcala64_hi12,
  // 20-bit fixup corresponding to %got_pc_hi20(foo) for instruction pcalau12i.
  fixup_loongarch_got_pc_hi20,
  // 12-bit fixup corresponding to %got_pc_lo12(foo) for instructions
  // ld.w/ld.d/add.d.
  fixup_loongarch_got_pc_lo12,
  // 20-bit fixup corresponding to %got64_pc_lo20(foo) for instruction lu32i.d.
  fixup_loongarch_got64_pc_lo20,
  // 12-bit fixup corresponding to %got64_pc_hi12(foo) for instruction lu52i.d.
  fixup_loongarch_got64_pc_hi12,
  // 20-bit fixup corresponding to %got_hi20(foo) for instruction lu12i.w.
  fixup_loongarch_got_hi20,
  // 12-bit fixup corresponding to %got_lo12(foo) for instruction ori.
  fixup_loongarch_got_lo12,
  // 20-bit fixup corresponding to %got64_lo20(foo) for instruction lu32i.d.
  fixup_loongarch_got64_lo20,
  // 12-bit fixup corresponding to %got64_hi12(foo) for instruction lu52i.d.
  fixup_loongarch_got64_hi12,
  // Skip R_LARCH_TLS_LE_*.
  // 20-bit fixup corresponding to %ie_pc_hi20(foo) for instruction pcalau12i.
  fixup_loongarch_tls_ie_pc_hi20 =
      FirstLiteralRelocationKind + ELF::R_LARCH_TLS_IE_PC_HI20,
  // 12-bit fixup corresponding to %ie_pc_lo12(foo) for instructions
  // ld.w/ld.d/add.d.
  fixup_loongarch_tls_ie_pc_lo12,
  // 20-bit fixup corresponding to %ie64_pc_lo20(foo) for instruction lu32i.d.
  fixup_loongarch_tls_ie64_pc_lo20,
  // 12-bit fixup corresponding to %ie64_pc_hi12(foo) for instruction lu52i.d.
  fixup_loongarch_tls_ie64_pc_hi12,
  // 20-bit fixup corresponding to %ie_hi20(foo) for instruction lu12i.w.
  fixup_loongarch_tls_ie_hi20,
  // 12-bit fixup corresponding to %ie_lo12(foo) for instruction ori.
  fixup_loongarch_tls_ie_lo12,
  // 20-bit fixup corresponding to %ie64_lo20(foo) for instruction lu32i.d.
  fixup_loongarch_tls_ie64_lo20,
  // 12-bit fixup corresponding to %ie64_hi12(foo) for instruction lu52i.d.
  fixup_loongarch_tls_ie64_hi12,
  // 20-bit fixup corresponding to %ld_pc_hi20(foo) for instruction pcalau12i.
  fixup_loongarch_tls_ld_pc_hi20,
  // 20-bit fixup corresponding to %ld_hi20(foo) for instruction lu12i.w.
  fixup_loongarch_tls_ld_hi20,
  // 20-bit fixup corresponding to %gd_pc_hi20(foo) for instruction pcalau12i.
  fixup_loongarch_tls_gd_pc_hi20,
  // 20-bit fixup corresponding to %gd_hi20(foo) for instruction lu12i.w.
  fixup_loongarch_tls_gd_hi20,
  // Generate an R_LARCH_RELAX which indicates the linker may relax here.
  fixup_loongarch_relax = FirstLiteralRelocationKind + ELF::R_LARCH_RELAX,
  // Generate an R_LARCH_ALIGN which indicates the linker may fixup align here.
  fixup_loongarch_align = FirstLiteralRelocationKind + ELF::R_LARCH_ALIGN,
  // 20-bit fixup corresponding to %pcrel_20(foo) for instruction pcaddi.
  fixup_loongarch_pcrel20_s2,
  // 36-bit fixup corresponding to %call36(foo) for a pair instructions:
  // pcaddu18i+jirl.
  fixup_loongarch_call36 = FirstLiteralRelocationKind + ELF::R_LARCH_CALL36,
  // 20-bit fixup corresponding to %desc_pc_hi20(foo) for instruction pcalau12i.
  fixup_loongarch_tls_desc_pc_hi20 =
      FirstLiteralRelocationKind + ELF::R_LARCH_TLS_DESC_PC_HI20,
  // 12-bit fixup corresponding to %desc_pc_lo12(foo) for instructions like
  // addi.w/d.
  fixup_loongarch_tls_desc_pc_lo12,
  // 20-bit fixup corresponding to %desc64_pc_lo20(foo) for instruction lu32i.d.
  fixup_loongarch_tls_desc64_pc_lo20,
  // 12-bit fixup corresponding to %desc64_pc_hi12(foo) for instruction lu52i.d.
  fixup_loongarch_tls_desc64_pc_hi12,
  // 20-bit fixup corresponding to %desc_hi20(foo) for instruction lu12i.w.
  fixup_loongarch_tls_desc_hi20,
  // 12-bit fixup corresponding to %desc_lo12(foo) for instruction ori.
  fixup_loongarch_tls_desc_lo12,
  // 20-bit fixup corresponding to %desc64_lo20(foo) for instruction lu32i.d.
  fixup_loongarch_tls_desc64_lo20,
  // 12-bit fixup corresponding to %desc64_hi12(foo) for instruction lu52i.d.
  fixup_loongarch_tls_desc64_hi12,
  // 12-bit fixup corresponding to %desc_ld(foo) for instruction ld.w/d.
  fixup_loongarch_tls_desc_ld,
  // 12-bit fixup corresponding to %desc_call(foo) for instruction jirl.
  fixup_loongarch_tls_desc_call,
  // 20-bit fixup corresponding to %le_hi20_r(foo) for instruction lu12i.w.
  fixup_loongarch_tls_le_hi20_r,
  // Fixup corresponding to %le_add_r(foo) for instruction PseudoAddTPRel_W/D.
  fixup_loongarch_tls_le_add_r,
  // 12-bit fixup corresponding to %le_lo12_r(foo) for instruction addi.w/d.
  fixup_loongarch_tls_le_lo12_r,
  // 20-bit fixup corresponding to %ld_pcrel_20(foo) for instruction pcaddi.
  fixup_loongarch_tls_ld_pcrel20_s2,
  // 20-bit fixup corresponding to %gd_pcrel_20(foo) for instruction pcaddi.
  fixup_loongarch_tls_gd_pcrel20_s2,
  // 20-bit fixup corresponding to %desc_pcrel_20(foo) for instruction pcaddi.
  fixup_loongarch_tls_desc_pcrel20_s2,
};
} // end namespace LoongArch
} // end namespace llvm

#endif
