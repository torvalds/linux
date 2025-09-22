//===- MCELFExtras.h - Extra functions for ELF ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCELFEXTRAS_H
#define LLVM_MC_MCELFEXTRAS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/bit.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdint>
#include <type_traits>

namespace llvm::ELF {
// Encode relocations as CREL to OS. ToCrel is responsible for converting a
// const RelocsTy & to an Elf_Crel.
template <bool Is64, class RelocsTy, class F>
void encodeCrel(raw_ostream &OS, RelocsTy Relocs, F ToCrel) {
  using uint = std::conditional_t<Is64, uint64_t, uint32_t>;
  uint OffsetMask = 8, Offset = 0, Addend = 0;
  uint32_t SymIdx = 0, Type = 0;
  for (const auto &R : Relocs)
    OffsetMask |= ToCrel(R).r_offset;
  const int Shift = llvm::countr_zero(OffsetMask);
  encodeULEB128(Relocs.size() * 8 + ELF::CREL_HDR_ADDEND + Shift, OS);
  for (const auto &R : Relocs) {
    auto CR = ToCrel(R);
    auto DeltaOffset = static_cast<uint>((CR.r_offset - Offset) >> Shift);
    Offset = CR.r_offset;
    uint8_t B = (DeltaOffset << 3) + (SymIdx != CR.r_symidx) +
                (Type != CR.r_type ? 2 : 0) +
                (Addend != uint(CR.r_addend) ? 4 : 0);
    if (DeltaOffset < 0x10) {
      OS << char(B);
    } else {
      OS << char(B | 0x80);
      encodeULEB128(DeltaOffset >> 4, OS);
    }
    // Delta symidx/type/addend members (SLEB128).
    if (B & 1) {
      encodeSLEB128(static_cast<int32_t>(CR.r_symidx - SymIdx), OS);
      SymIdx = CR.r_symidx;
    }
    if (B & 2) {
      encodeSLEB128(static_cast<int32_t>(CR.r_type - Type), OS);
      Type = CR.r_type;
    }
    if (B & 4) {
      encodeSLEB128(std::make_signed_t<uint>(CR.r_addend - Addend), OS);
      Addend = CR.r_addend;
    }
  }
}
} // namespace llvm::ELF

#endif
