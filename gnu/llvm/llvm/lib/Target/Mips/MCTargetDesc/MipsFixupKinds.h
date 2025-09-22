//===-- MipsFixupKinds.h - Mips Specific Fixup Entries ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MCTARGETDESC_MIPSFIXUPKINDS_H
#define LLVM_LIB_TARGET_MIPS_MCTARGETDESC_MIPSFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace Mips {
  // Although most of the current fixup types reflect a unique relocation
  // one can have multiple fixup types for a given relocation and thus need
  // to be uniquely named.
  //
  // This table *must* be in the same order of
  // MCFixupKindInfo Infos[Mips::NumTargetFixupKinds]
  // in MipsAsmBackend.cpp.
  //
  enum Fixups {
    // Branch fixups resulting in R_MIPS_16.
    fixup_Mips_16 = FirstTargetFixupKind,

    // Pure 32 bit data fixup resulting in - R_MIPS_32.
    fixup_Mips_32,

    // Full 32 bit data relative data fixup resulting in - R_MIPS_REL32.
    fixup_Mips_REL32,

    // Jump 26 bit fixup resulting in - R_MIPS_26.
    fixup_Mips_26,

    // Pure upper 16 bit fixup resulting in - R_MIPS_HI16.
    fixup_Mips_HI16,

    // Pure lower 16 bit fixup resulting in - R_MIPS_LO16.
    fixup_Mips_LO16,

    // 16 bit fixup for GP offest resulting in - R_MIPS_GPREL16.
    fixup_Mips_GPREL16,

    // 16 bit literal fixup resulting in - R_MIPS_LITERAL.
    fixup_Mips_LITERAL,

    // Symbol fixup resulting in - R_MIPS_GOT16.
    fixup_Mips_GOT,

    // PC relative branch fixup resulting in - R_MIPS_PC16.
    fixup_Mips_PC16,

    // resulting in - R_MIPS_CALL16.
    fixup_Mips_CALL16,

    // resulting in - R_MIPS_GPREL32.
    fixup_Mips_GPREL32,

    // resulting in - R_MIPS_SHIFT5.
    fixup_Mips_SHIFT5,

    // resulting in - R_MIPS_SHIFT6.
    fixup_Mips_SHIFT6,

    // Pure 64 bit data fixup resulting in - R_MIPS_64.
    fixup_Mips_64,

    // resulting in - R_MIPS_TLS_GD.
    fixup_Mips_TLSGD,

    // resulting in - R_MIPS_TLS_GOTTPREL.
    fixup_Mips_GOTTPREL,

    // resulting in - R_MIPS_TLS_TPREL_HI16.
    fixup_Mips_TPREL_HI,

    // resulting in - R_MIPS_TLS_TPREL_LO16.
    fixup_Mips_TPREL_LO,

    // resulting in - R_MIPS_TLS_LDM.
    fixup_Mips_TLSLDM,

    // resulting in - R_MIPS_TLS_DTPREL_HI16.
    fixup_Mips_DTPREL_HI,

    // resulting in - R_MIPS_TLS_DTPREL_LO16.
    fixup_Mips_DTPREL_LO,

    // PC relative branch fixup resulting in - R_MIPS_PC16
    fixup_Mips_Branch_PCRel,

    // resulting in - R_MIPS_GPREL16/R_MIPS_SUB/R_MIPS_HI16
    //                R_MICROMIPS_GPREL16/R_MICROMIPS_SUB/R_MICROMIPS_HI16
    fixup_Mips_GPOFF_HI,
    fixup_MICROMIPS_GPOFF_HI,

    // resulting in - R_MIPS_GPREL16/R_MIPS_SUB/R_MIPS_LO16
    //                R_MICROMIPS_GPREL16/R_MICROMIPS_SUB/R_MICROMIPS_LO16
    fixup_Mips_GPOFF_LO,
    fixup_MICROMIPS_GPOFF_LO,

    // resulting in - R_MIPS_PAGE
    fixup_Mips_GOT_PAGE,

    // resulting in - R_MIPS_GOT_OFST
    fixup_Mips_GOT_OFST,

    // resulting in - R_MIPS_GOT_DISP
    fixup_Mips_GOT_DISP,

    // resulting in - R_MIPS_HIGHER/R_MICROMIPS_HIGHER
    fixup_Mips_HIGHER,
    fixup_MICROMIPS_HIGHER,

    // resulting in - R_MIPS_HIGHEST/R_MICROMIPS_HIGHEST
    fixup_Mips_HIGHEST,
    fixup_MICROMIPS_HIGHEST,

    // resulting in - R_MIPS_GOT_HI16
    fixup_Mips_GOT_HI16,

    // resulting in - R_MIPS_GOT_LO16
    fixup_Mips_GOT_LO16,

    // resulting in - R_MIPS_CALL_HI16
    fixup_Mips_CALL_HI16,

    // resulting in - R_MIPS_CALL_LO16
    fixup_Mips_CALL_LO16,

    // resulting in - R_MIPS_PC18_S3
    fixup_MIPS_PC18_S3,

    // resulting in - R_MIPS_PC19_S2
    fixup_MIPS_PC19_S2,

    // resulting in - R_MIPS_PC21_S2
    fixup_MIPS_PC21_S2,

    // resulting in - R_MIPS_PC26_S2
    fixup_MIPS_PC26_S2,

    // resulting in - R_MIPS_PCHI16
    fixup_MIPS_PCHI16,

    // resulting in - R_MIPS_PCLO16
    fixup_MIPS_PCLO16,

    // resulting in - R_MICROMIPS_26_S1
    fixup_MICROMIPS_26_S1,

    // resulting in - R_MICROMIPS_HI16
    fixup_MICROMIPS_HI16,

    // resulting in - R_MICROMIPS_LO16
    fixup_MICROMIPS_LO16,

    // resulting in - R_MICROMIPS_GOT16
    fixup_MICROMIPS_GOT16,

    // resulting in - R_MICROMIPS_PC7_S1
    fixup_MICROMIPS_PC7_S1,

    // resulting in - R_MICROMIPS_PC10_S1
    fixup_MICROMIPS_PC10_S1,

    // resulting in - R_MICROMIPS_PC16_S1
    fixup_MICROMIPS_PC16_S1,

    // resulting in - R_MICROMIPS_PC26_S1
    fixup_MICROMIPS_PC26_S1,

    // resulting in - R_MICROMIPS_PC19_S2
    fixup_MICROMIPS_PC19_S2,

    // resulting in - R_MICROMIPS_PC18_S3
    fixup_MICROMIPS_PC18_S3,

    // resulting in - R_MICROMIPS_PC21_S1
    fixup_MICROMIPS_PC21_S1,

    // resulting in - R_MICROMIPS_CALL16
    fixup_MICROMIPS_CALL16,

    // resulting in - R_MICROMIPS_GOT_DISP
    fixup_MICROMIPS_GOT_DISP,

    // resulting in - R_MICROMIPS_GOT_PAGE
    fixup_MICROMIPS_GOT_PAGE,

    // resulting in - R_MICROMIPS_GOT_OFST
    fixup_MICROMIPS_GOT_OFST,

    // resulting in - R_MICROMIPS_TLS_GD
    fixup_MICROMIPS_TLS_GD,

    // resulting in - R_MICROMIPS_TLS_LDM
    fixup_MICROMIPS_TLS_LDM,

    // resulting in - R_MICROMIPS_TLS_DTPREL_HI16
    fixup_MICROMIPS_TLS_DTPREL_HI16,

    // resulting in - R_MICROMIPS_TLS_DTPREL_LO16
    fixup_MICROMIPS_TLS_DTPREL_LO16,

    // resulting in - R_MICROMIPS_TLS_GOTTPREL.
    fixup_MICROMIPS_GOTTPREL,

    // resulting in - R_MICROMIPS_TLS_TPREL_HI16
    fixup_MICROMIPS_TLS_TPREL_HI16,

    // resulting in - R_MICROMIPS_TLS_TPREL_LO16
    fixup_MICROMIPS_TLS_TPREL_LO16,

    // resulting in - R_MIPS_SUB/R_MICROMIPS_SUB
    fixup_Mips_SUB,
    fixup_MICROMIPS_SUB,

    // resulting in - R_MIPS_JALR/R_MICROMIPS_JALR
    fixup_Mips_JALR,
    fixup_MICROMIPS_JALR,

    // Marker
    LastTargetFixupKind,
    NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
  };
} // namespace Mips
} // namespace llvm


#endif
