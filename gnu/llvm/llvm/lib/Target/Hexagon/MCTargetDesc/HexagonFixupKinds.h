//===-- HexagonFixupKinds.h - Hexagon Specific Fixup Entries --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_HEXAGON_HEXAGONFIXUPKINDS_H
#define LLVM_HEXAGON_HEXAGONFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace Hexagon {
enum Fixups {
  // Branch fixups for R_HEX_B{22,15,7}_PCREL.
  fixup_Hexagon_B22_PCREL = FirstTargetFixupKind,
  fixup_Hexagon_B15_PCREL,
  fixup_Hexagon_B7_PCREL,
  fixup_Hexagon_LO16,
  fixup_Hexagon_HI16,
  fixup_Hexagon_32,
  fixup_Hexagon_16,
  fixup_Hexagon_8,
  fixup_Hexagon_GPREL16_0,
  fixup_Hexagon_GPREL16_1,
  fixup_Hexagon_GPREL16_2,
  fixup_Hexagon_GPREL16_3,
  fixup_Hexagon_HL16,
  fixup_Hexagon_B13_PCREL,
  fixup_Hexagon_B9_PCREL,
  fixup_Hexagon_B32_PCREL_X,
  fixup_Hexagon_32_6_X,
  fixup_Hexagon_B22_PCREL_X,
  fixup_Hexagon_B15_PCREL_X,
  fixup_Hexagon_B13_PCREL_X,
  fixup_Hexagon_B9_PCREL_X,
  fixup_Hexagon_B7_PCREL_X,
  fixup_Hexagon_16_X,
  fixup_Hexagon_12_X,
  fixup_Hexagon_11_X,
  fixup_Hexagon_10_X,
  fixup_Hexagon_9_X,
  fixup_Hexagon_8_X,
  fixup_Hexagon_7_X,
  fixup_Hexagon_6_X,
  fixup_Hexagon_32_PCREL,
  fixup_Hexagon_COPY,
  fixup_Hexagon_GLOB_DAT,
  fixup_Hexagon_JMP_SLOT,
  fixup_Hexagon_RELATIVE,
  fixup_Hexagon_PLT_B22_PCREL,
  fixup_Hexagon_GOTREL_LO16,
  fixup_Hexagon_GOTREL_HI16,
  fixup_Hexagon_GOTREL_32,
  fixup_Hexagon_GOT_LO16,
  fixup_Hexagon_GOT_HI16,
  fixup_Hexagon_GOT_32,
  fixup_Hexagon_GOT_16,
  fixup_Hexagon_DTPMOD_32,
  fixup_Hexagon_DTPREL_LO16,
  fixup_Hexagon_DTPREL_HI16,
  fixup_Hexagon_DTPREL_32,
  fixup_Hexagon_DTPREL_16,
  fixup_Hexagon_GD_PLT_B22_PCREL,
  fixup_Hexagon_LD_PLT_B22_PCREL,
  fixup_Hexagon_GD_GOT_LO16,
  fixup_Hexagon_GD_GOT_HI16,
  fixup_Hexagon_GD_GOT_32,
  fixup_Hexagon_GD_GOT_16,
  fixup_Hexagon_LD_GOT_LO16,
  fixup_Hexagon_LD_GOT_HI16,
  fixup_Hexagon_LD_GOT_32,
  fixup_Hexagon_LD_GOT_16,
  fixup_Hexagon_IE_LO16,
  fixup_Hexagon_IE_HI16,
  fixup_Hexagon_IE_32,
  fixup_Hexagon_IE_16,
  fixup_Hexagon_IE_GOT_LO16,
  fixup_Hexagon_IE_GOT_HI16,
  fixup_Hexagon_IE_GOT_32,
  fixup_Hexagon_IE_GOT_16,
  fixup_Hexagon_TPREL_LO16,
  fixup_Hexagon_TPREL_HI16,
  fixup_Hexagon_TPREL_32,
  fixup_Hexagon_TPREL_16,
  fixup_Hexagon_6_PCREL_X,
  fixup_Hexagon_GOTREL_32_6_X,
  fixup_Hexagon_GOTREL_16_X,
  fixup_Hexagon_GOTREL_11_X,
  fixup_Hexagon_GOT_32_6_X,
  fixup_Hexagon_GOT_16_X,
  fixup_Hexagon_GOT_11_X,
  fixup_Hexagon_DTPREL_32_6_X,
  fixup_Hexagon_DTPREL_16_X,
  fixup_Hexagon_DTPREL_11_X,
  fixup_Hexagon_GD_GOT_32_6_X,
  fixup_Hexagon_GD_GOT_16_X,
  fixup_Hexagon_GD_GOT_11_X,
  fixup_Hexagon_LD_GOT_32_6_X,
  fixup_Hexagon_LD_GOT_16_X,
  fixup_Hexagon_LD_GOT_11_X,
  fixup_Hexagon_IE_32_6_X,
  fixup_Hexagon_IE_16_X,
  fixup_Hexagon_IE_GOT_32_6_X,
  fixup_Hexagon_IE_GOT_16_X,
  fixup_Hexagon_IE_GOT_11_X,
  fixup_Hexagon_TPREL_32_6_X,
  fixup_Hexagon_TPREL_16_X,
  fixup_Hexagon_TPREL_11_X,
  fixup_Hexagon_23_REG,
  fixup_Hexagon_27_REG,
  fixup_Hexagon_GD_PLT_B22_PCREL_X,
  fixup_Hexagon_GD_PLT_B32_PCREL_X,
  fixup_Hexagon_LD_PLT_B22_PCREL_X,
  fixup_Hexagon_LD_PLT_B32_PCREL_X,

  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};
enum FixupBitmaps : unsigned {
  Word8 = 0xff,
  Word16 = 0xffff,
  Word32 = 0xffffffff,
  Word32_LO = 0x00c03fff,
  Word32_HL = 0x0, // Not Implemented
  Word32_GP = 0x0, // Not Implemented
  Word32_B7 = 0x00001f18,
  Word32_B9 = 0x003000fe,
  Word32_B13 = 0x00202ffe,
  Word32_B15 = 0x00df20fe,
  Word32_B22 = 0x01ff3ffe,
  Word32_R6 = 0x000007e0,
  Word32_U6 = 0x0,  // Not Implemented
  Word32_U16 = 0x0, // Not Implemented
  Word32_X26 = 0x0fff3fff
};
} // namespace Hexagon
} // namespace llvm

#endif // LLVM_HEXAGON_HEXAGONFIXUPKINDS_H
