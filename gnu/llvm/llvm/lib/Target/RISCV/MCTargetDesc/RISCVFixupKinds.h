//===-- RISCVFixupKinds.h - RISC-V Specific Fixup Entries -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVFIXUPKINDS_H
#define LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVFIXUPKINDS_H

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCFixup.h"
#include <utility>

#undef RISCV

namespace llvm::RISCV {
enum Fixups {
  // 20-bit fixup corresponding to %hi(foo) for instructions like lui
  fixup_riscv_hi20 = FirstTargetFixupKind,
  // 12-bit fixup corresponding to %lo(foo) for instructions like addi
  fixup_riscv_lo12_i,
  // 12-bit fixup corresponding to foo-bar for instructions like addi
  fixup_riscv_12_i,
  // 12-bit fixup corresponding to %lo(foo) for the S-type store instructions
  fixup_riscv_lo12_s,
  // 20-bit fixup corresponding to %pcrel_hi(foo) for instructions like auipc
  fixup_riscv_pcrel_hi20,
  // 12-bit fixup corresponding to %pcrel_lo(foo) for instructions like addi
  fixup_riscv_pcrel_lo12_i,
  // 12-bit fixup corresponding to %pcrel_lo(foo) for the S-type store
  // instructions
  fixup_riscv_pcrel_lo12_s,
  // 20-bit fixup corresponding to %got_pcrel_hi(foo) for instructions like
  // auipc
  fixup_riscv_got_hi20,
  // 20-bit fixup corresponding to %tprel_hi(foo) for instructions like lui
  fixup_riscv_tprel_hi20,
  // 12-bit fixup corresponding to %tprel_lo(foo) for instructions like addi
  fixup_riscv_tprel_lo12_i,
  // 12-bit fixup corresponding to %tprel_lo(foo) for the S-type store
  // instructions
  fixup_riscv_tprel_lo12_s,
  // Fixup corresponding to %tprel_add(foo) for PseudoAddTPRel, used as a linker
  // hint
  fixup_riscv_tprel_add,
  // 20-bit fixup corresponding to %tls_ie_pcrel_hi(foo) for instructions like
  // auipc
  fixup_riscv_tls_got_hi20,
  // 20-bit fixup corresponding to %tls_gd_pcrel_hi(foo) for instructions like
  // auipc
  fixup_riscv_tls_gd_hi20,
  // 20-bit fixup for symbol references in the jal instruction
  fixup_riscv_jal,
  // 12-bit fixup for symbol references in the branch instructions
  fixup_riscv_branch,
  // 11-bit fixup for symbol references in the compressed jump instruction
  fixup_riscv_rvc_jump,
  // 8-bit fixup for symbol references in the compressed branch instruction
  fixup_riscv_rvc_branch,
  // Fixup representing a legacy no-pic function call attached to the auipc
  // instruction in a pair composed of adjacent auipc+jalr instructions.
  fixup_riscv_call,
  // Fixup representing a function call attached to the auipc instruction in a
  // pair composed of adjacent auipc+jalr instructions.
  fixup_riscv_call_plt,
  // Used to generate an R_RISCV_RELAX relocation, which indicates the linker
  // may relax the instruction pair.
  fixup_riscv_relax,
  // Used to generate an R_RISCV_ALIGN relocation, which indicates the linker
  // should fixup the alignment after linker relaxation.
  fixup_riscv_align,
  // Fixups indicating a TLS descriptor code sequence, corresponding to auipc,
  // lw/ld, addi, and jalr, respectively.
  fixup_riscv_tlsdesc_hi20,
  fixup_riscv_tlsdesc_load_lo12,
  fixup_riscv_tlsdesc_add_lo12,
  fixup_riscv_tlsdesc_call,

  // Used as a sentinel, must be the last
  fixup_riscv_invalid,
  NumTargetFixupKinds = fixup_riscv_invalid - FirstTargetFixupKind
};

static inline std::pair<MCFixupKind, MCFixupKind>
getRelocPairForSize(unsigned Size) {
  switch (Size) {
  default:
    llvm_unreachable("unsupported fixup size");
  case 1:
    return std::make_pair(
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_RISCV_ADD8),
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_RISCV_SUB8));
  case 2:
    return std::make_pair(
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_RISCV_ADD16),
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_RISCV_SUB16));
  case 4:
    return std::make_pair(
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_RISCV_ADD32),
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_RISCV_SUB32));
  case 8:
    return std::make_pair(
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_RISCV_ADD64),
        MCFixupKind(FirstLiteralRelocationKind + ELF::R_RISCV_SUB64));
  }
}

} // end namespace llvm::RISCV

#endif
