//===-- checksum.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_CHECKSUM_H_
#define SCUDO_CHECKSUM_H_

#include "internal_defs.h"

// Hardware CRC32 is supported at compilation via the following:
// - for i386 & x86_64: -mcrc32 (earlier: -msse4.2)
// - for ARM & AArch64: -march=armv8-a+crc or -mcrc
// An additional check must be performed at runtime as well to make sure the
// emitted instructions are valid on the target host.

#if defined(__CRC32__)
// NB: clang has <crc32intrin.h> but GCC does not
#include <smmintrin.h>
#define CRC32_INTRINSIC                                                        \
  FIRST_32_SECOND_64(__builtin_ia32_crc32si, __builtin_ia32_crc32di)
#elif defined(__SSE4_2__)
#include <smmintrin.h>
#define CRC32_INTRINSIC FIRST_32_SECOND_64(_mm_crc32_u32, _mm_crc32_u64)
#endif
#ifdef __ARM_FEATURE_CRC32
#include <arm_acle.h>
#define CRC32_INTRINSIC FIRST_32_SECOND_64(__crc32cw, __crc32cd)
#endif
#ifdef __loongarch__
#include <larchintrin.h>
#define CRC32_INTRINSIC FIRST_32_SECOND_64(__crcc_w_w_w, __crcc_w_d_w)
#endif

namespace scudo {

enum class Checksum : u8 {
  BSD = 0,
  HardwareCRC32 = 1,
};

// BSD checksum, unlike a software CRC32, doesn't use any array lookup. We save
// significantly on memory accesses, as well as 1K of CRC32 table, on platforms
// that do no support hardware CRC32. The checksum itself is 16-bit, which is at
// odds with CRC32, but enough for our needs.
inline u16 computeBSDChecksum(u16 Sum, uptr Data) {
  for (u8 I = 0; I < sizeof(Data); I++) {
    Sum = static_cast<u16>((Sum >> 1) | ((Sum & 1) << 15));
    Sum = static_cast<u16>(Sum + (Data & 0xff));
    Data >>= 8;
  }
  return Sum;
}

bool hasHardwareCRC32();
WEAK u32 computeHardwareCRC32(u32 Crc, uptr Data);

} // namespace scudo

#endif // SCUDO_CHECKSUM_H_
