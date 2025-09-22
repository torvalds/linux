/*===---- riscv_bitmanip.h - RISC-V Zb* intrinsics --------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __RISCV_BITMANIP_H
#define __RISCV_BITMANIP_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__riscv_zbb)
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_orc_b_32(uint32_t __x) {
  return __builtin_riscv_orc_b_32(__x);
}

static __inline__ unsigned __attribute__((__always_inline__, __nodebug__))
__riscv_clz_32(uint32_t __x) {
  return __builtin_riscv_clz_32(__x);
}

static __inline__ unsigned __attribute__((__always_inline__, __nodebug__))
__riscv_ctz_32(uint32_t __x) {
  return __builtin_riscv_ctz_32(__x);
}

static __inline__ unsigned __attribute__((__always_inline__, __nodebug__))
__riscv_cpop_32(uint32_t __x) {
  return __builtin_popcount(__x);
}

#if __riscv_xlen == 64
static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_orc_b_64(uint64_t __x) {
  return __builtin_riscv_orc_b_64(__x);
}

static __inline__ unsigned __attribute__((__always_inline__, __nodebug__))
__riscv_clz_64(uint64_t __x) {
  return __builtin_riscv_clz_64(__x);
}

static __inline__ unsigned __attribute__((__always_inline__, __nodebug__))
__riscv_ctz_64(uint64_t __x) {
  return __builtin_riscv_ctz_64(__x);
}

static __inline__ unsigned __attribute__((__always_inline__, __nodebug__))
__riscv_cpop_64(uint64_t __x) {
  return __builtin_popcountll(__x);
}
#endif
#endif // defined(__riscv_zbb)

#if defined(__riscv_zbb) || defined(__riscv_zbkb)
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_rev8_32(uint32_t __x) {
  return __builtin_bswap32(__x);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_rol_32(uint32_t __x, uint32_t __y) {
  return __builtin_rotateleft32(__x, __y);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_ror_32(uint32_t __x, uint32_t __y) {
  return __builtin_rotateright32(__x, __y);
}

#if __riscv_xlen == 64
static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_rev8_64(uint64_t __x) {
  return __builtin_bswap64(__x);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_rol_64(uint64_t __x, uint32_t __y) {
  return __builtin_rotateleft64(__x, __y);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_ror_64(uint64_t __x, uint32_t __y) {
  return __builtin_rotateright64(__x, __y);
}
#endif
#endif // defined(__riscv_zbb) || defined(__riscv_zbkb)

#if defined(__riscv_zbkb)
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_brev8_32(uint32_t __x) {
  return __builtin_riscv_brev8_32(__x);
}

#if __riscv_xlen == 64
static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_brev8_64(uint64_t __x) {
  return __builtin_riscv_brev8_64(__x);
}
#endif

#if __riscv_xlen == 32
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_unzip_32(uint32_t __x) {
  return __builtin_riscv_unzip_32(__x);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_zip_32(uint32_t __x) {
  return __builtin_riscv_zip_32(__x);
}
#endif
#endif // defined(__riscv_zbkb)

#if defined(__riscv_zbc)
#if __riscv_xlen == 32
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_clmulr_32(uint32_t __x, uint32_t __y) {
  return __builtin_riscv_clmulr_32(__x, __y);
}
#endif

#if __riscv_xlen == 64
static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_clmulr_64(uint64_t __x, uint64_t __y) {
  return __builtin_riscv_clmulr_64(__x, __y);
}
#endif
#endif // defined(__riscv_zbc)

#if defined(__riscv_zbkc) || defined(__riscv_zbc)
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_clmul_32(uint32_t __x, uint32_t __y) {
  return __builtin_riscv_clmul_32(__x, __y);
}

#if __riscv_xlen == 32
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_clmulh_32(uint32_t __x, uint32_t __y) {
  return __builtin_riscv_clmulh_32(__x, __y);
}
#endif

#if __riscv_xlen == 64
static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_clmul_64(uint64_t __x, uint64_t __y) {
  return __builtin_riscv_clmul_64(__x, __y);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_clmulh_64(uint64_t __x, uint64_t __y) {
  return __builtin_riscv_clmulh_64(__x, __y);
}
#endif
#endif // defined(__riscv_zbkc) || defined(__riscv_zbc)

#if defined(__riscv_zbkx)
#if __riscv_xlen == 32
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_xperm4_32(uint32_t __x, uint32_t __y) {
  return __builtin_riscv_xperm4_32(__x, __y);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_xperm8_32(uint32_t __x, uint32_t __y) {
  return __builtin_riscv_xperm8_32(__x, __y);
}
#endif

#if __riscv_xlen == 64
static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_xperm4_64(uint64_t __x, uint64_t __y) {
  return __builtin_riscv_xperm4_64(__x, __y);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_xperm8_64(uint64_t __x, uint64_t __y) {
  return __builtin_riscv_xperm8_64(__x, __y);
}
#endif
#endif // defined(__riscv_zbkx)

#if defined(__cplusplus)
}
#endif

#endif
