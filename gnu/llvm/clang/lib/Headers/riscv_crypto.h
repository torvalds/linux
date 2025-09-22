/*===---- riscv_crypto.h - RISC-V Zk* intrinsics ---------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __RISCV_CRYPTO_H
#define __RISCV_CRYPTO_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__riscv_zknd)
#if __riscv_xlen == 32
#define __riscv_aes32dsi(x, y, bs) __builtin_riscv_aes32dsi(x, y, bs)
#define __riscv_aes32dsmi(x, y, bs) __builtin_riscv_aes32dsmi(x, y, bs)
#endif

#if __riscv_xlen == 64
static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_aes64ds(uint64_t __x, uint64_t __y) {
  return __builtin_riscv_aes64ds(__x, __y);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_aes64dsm(uint64_t __x, uint64_t __y) {
  return __builtin_riscv_aes64dsm(__x, __y);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_aes64im(uint64_t __x) {
  return __builtin_riscv_aes64im(__x);
}
#endif
#endif // defined(__riscv_zknd)

#if defined(__riscv_zkne)
#if __riscv_xlen == 32
#define __riscv_aes32esi(x, y, bs) __builtin_riscv_aes32esi(x, y, bs)
#define __riscv_aes32esmi(x, y, bs) __builtin_riscv_aes32esmi(x, y, bs)
#endif

#if __riscv_xlen == 64
static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_aes64es(uint64_t __x, uint64_t __y) {
  return __builtin_riscv_aes64es(__x, __y);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_aes64esm(uint64_t __x, uint64_t __y) {
  return __builtin_riscv_aes64esm(__x, __y);
}
#endif
#endif // defined(__riscv_zkne)

#if defined(__riscv_zknd) || defined(__riscv_zkne)
#if __riscv_xlen == 64
#define __riscv_aes64ks1i(x, rnum) __builtin_riscv_aes64ks1i(x, rnum)

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_aes64ks2(uint64_t __x, uint64_t __y) {
  return __builtin_riscv_aes64ks2(__x, __y);
}
#endif
#endif // defined(__riscv_zknd) || defined(__riscv_zkne)

#if defined(__riscv_zknh)
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha256sig0(uint32_t __x) {
  return __builtin_riscv_sha256sig0(__x);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha256sig1(uint32_t __x) {
  return __builtin_riscv_sha256sig1(__x);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha256sum0(uint32_t __x) {
  return __builtin_riscv_sha256sum0(__x);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha256sum1(uint32_t __x) {
  return __builtin_riscv_sha256sum1(__x);
}

#if __riscv_xlen == 32
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha512sig0h(uint32_t __x, uint32_t __y) {
  return __builtin_riscv_sha512sig0h(__x, __y);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha512sig0l(uint32_t __x, uint32_t __y) {
  return __builtin_riscv_sha512sig0l(__x, __y);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha512sig1h(uint32_t __x, uint32_t __y) {
  return __builtin_riscv_sha512sig1h(__x, __y);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha512sig1l(uint32_t __x, uint32_t __y) {
  return __builtin_riscv_sha512sig1l(__x, __y);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha512sum0r(uint32_t __x, uint32_t __y) {
  return __builtin_riscv_sha512sum0r(__x, __y);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha512sum1r(uint32_t __x, uint32_t __y) {
  return __builtin_riscv_sha512sum1r(__x, __y);
}
#endif

#if __riscv_xlen == 64
static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha512sig0(uint64_t __x) {
  return __builtin_riscv_sha512sig0(__x);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha512sig1(uint64_t __x) {
  return __builtin_riscv_sha512sig1(__x);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha512sum0(uint64_t __x) {
  return __builtin_riscv_sha512sum0(__x);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__riscv_sha512sum1(uint64_t __x) {
  return __builtin_riscv_sha512sum1(__x);
}
#endif
#endif // defined(__riscv_zknh)

#if defined(__riscv_zksh)
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_sm3p0(uint32_t __x) {
  return __builtin_riscv_sm3p0(__x);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__riscv_sm3p1(uint32_t __x) {
  return __builtin_riscv_sm3p1(__x);
}
#endif // defined(__riscv_zksh)

#if defined(__riscv_zksed)
#define __riscv_sm4ed(x, y, bs) __builtin_riscv_sm4ed(x, y, bs);
#define __riscv_sm4ks(x, y, bs) __builtin_riscv_sm4ks(x, y, bs);
#endif // defined(__riscv_zksed)

#if defined(__cplusplus)
}
#endif

#endif
