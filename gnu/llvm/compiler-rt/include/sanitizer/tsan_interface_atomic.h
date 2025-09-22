//===-- tsan_interface_atomic.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Public interface header for TSan atomics.
//===----------------------------------------------------------------------===//
#ifndef TSAN_INTERFACE_ATOMIC_H
#define TSAN_INTERFACE_ATOMIC_H

#include <sanitizer/common_interface_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef char __tsan_atomic8;
typedef short __tsan_atomic16;
typedef int __tsan_atomic32;
typedef long __tsan_atomic64;
#if defined(__SIZEOF_INT128__) ||                                              \
    (__clang_major__ * 100 + __clang_minor__ >= 302)
__extension__ typedef __int128 __tsan_atomic128;
#define __TSAN_HAS_INT128 1
#else
#define __TSAN_HAS_INT128 0
#endif

// Part of ABI, do not change.
// https://github.com/llvm/llvm-project/blob/main/libcxx/include/atomic
typedef enum {
  __tsan_memory_order_relaxed,
  __tsan_memory_order_consume,
  __tsan_memory_order_acquire,
  __tsan_memory_order_release,
  __tsan_memory_order_acq_rel,
  __tsan_memory_order_seq_cst
} __tsan_memory_order;

__tsan_atomic8 SANITIZER_CDECL
__tsan_atomic8_load(const volatile __tsan_atomic8 *a, __tsan_memory_order mo);
__tsan_atomic16 SANITIZER_CDECL
__tsan_atomic16_load(const volatile __tsan_atomic16 *a, __tsan_memory_order mo);
__tsan_atomic32 SANITIZER_CDECL
__tsan_atomic32_load(const volatile __tsan_atomic32 *a, __tsan_memory_order mo);
__tsan_atomic64 SANITIZER_CDECL
__tsan_atomic64_load(const volatile __tsan_atomic64 *a, __tsan_memory_order mo);
#if __TSAN_HAS_INT128
__tsan_atomic128 SANITIZER_CDECL __tsan_atomic128_load(
    const volatile __tsan_atomic128 *a, __tsan_memory_order mo);
#endif

void SANITIZER_CDECL __tsan_atomic8_store(volatile __tsan_atomic8 *a,
                                          __tsan_atomic8 v,
                                          __tsan_memory_order mo);
void SANITIZER_CDECL __tsan_atomic16_store(volatile __tsan_atomic16 *a,
                                           __tsan_atomic16 v,
                                           __tsan_memory_order mo);
void SANITIZER_CDECL __tsan_atomic32_store(volatile __tsan_atomic32 *a,
                                           __tsan_atomic32 v,
                                           __tsan_memory_order mo);
void SANITIZER_CDECL __tsan_atomic64_store(volatile __tsan_atomic64 *a,
                                           __tsan_atomic64 v,
                                           __tsan_memory_order mo);
#if __TSAN_HAS_INT128
void SANITIZER_CDECL __tsan_atomic128_store(volatile __tsan_atomic128 *a,
                                            __tsan_atomic128 v,
                                            __tsan_memory_order mo);
#endif

__tsan_atomic8 SANITIZER_CDECL __tsan_atomic8_exchange(
    volatile __tsan_atomic8 *a, __tsan_atomic8 v, __tsan_memory_order mo);
__tsan_atomic16 SANITIZER_CDECL __tsan_atomic16_exchange(
    volatile __tsan_atomic16 *a, __tsan_atomic16 v, __tsan_memory_order mo);
__tsan_atomic32 SANITIZER_CDECL __tsan_atomic32_exchange(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo);
__tsan_atomic64 SANITIZER_CDECL __tsan_atomic64_exchange(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo);
#if __TSAN_HAS_INT128
__tsan_atomic128 SANITIZER_CDECL __tsan_atomic128_exchange(
    volatile __tsan_atomic128 *a, __tsan_atomic128 v, __tsan_memory_order mo);
#endif

__tsan_atomic8 SANITIZER_CDECL __tsan_atomic8_fetch_add(
    volatile __tsan_atomic8 *a, __tsan_atomic8 v, __tsan_memory_order mo);
__tsan_atomic16 SANITIZER_CDECL __tsan_atomic16_fetch_add(
    volatile __tsan_atomic16 *a, __tsan_atomic16 v, __tsan_memory_order mo);
__tsan_atomic32 SANITIZER_CDECL __tsan_atomic32_fetch_add(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo);
__tsan_atomic64 SANITIZER_CDECL __tsan_atomic64_fetch_add(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo);
#if __TSAN_HAS_INT128
__tsan_atomic128 SANITIZER_CDECL __tsan_atomic128_fetch_add(
    volatile __tsan_atomic128 *a, __tsan_atomic128 v, __tsan_memory_order mo);
#endif

__tsan_atomic8 SANITIZER_CDECL __tsan_atomic8_fetch_sub(
    volatile __tsan_atomic8 *a, __tsan_atomic8 v, __tsan_memory_order mo);
__tsan_atomic16 SANITIZER_CDECL __tsan_atomic16_fetch_sub(
    volatile __tsan_atomic16 *a, __tsan_atomic16 v, __tsan_memory_order mo);
__tsan_atomic32 SANITIZER_CDECL __tsan_atomic32_fetch_sub(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo);
__tsan_atomic64 SANITIZER_CDECL __tsan_atomic64_fetch_sub(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo);
#if __TSAN_HAS_INT128
__tsan_atomic128 SANITIZER_CDECL __tsan_atomic128_fetch_sub(
    volatile __tsan_atomic128 *a, __tsan_atomic128 v, __tsan_memory_order mo);
#endif

__tsan_atomic8 SANITIZER_CDECL __tsan_atomic8_fetch_and(
    volatile __tsan_atomic8 *a, __tsan_atomic8 v, __tsan_memory_order mo);
__tsan_atomic16 SANITIZER_CDECL __tsan_atomic16_fetch_and(
    volatile __tsan_atomic16 *a, __tsan_atomic16 v, __tsan_memory_order mo);
__tsan_atomic32 SANITIZER_CDECL __tsan_atomic32_fetch_and(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo);
__tsan_atomic64 SANITIZER_CDECL __tsan_atomic64_fetch_and(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo);
#if __TSAN_HAS_INT128
__tsan_atomic128 SANITIZER_CDECL __tsan_atomic128_fetch_and(
    volatile __tsan_atomic128 *a, __tsan_atomic128 v, __tsan_memory_order mo);
#endif

__tsan_atomic8 SANITIZER_CDECL __tsan_atomic8_fetch_or(
    volatile __tsan_atomic8 *a, __tsan_atomic8 v, __tsan_memory_order mo);
__tsan_atomic16 SANITIZER_CDECL __tsan_atomic16_fetch_or(
    volatile __tsan_atomic16 *a, __tsan_atomic16 v, __tsan_memory_order mo);
__tsan_atomic32 SANITIZER_CDECL __tsan_atomic32_fetch_or(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo);
__tsan_atomic64 SANITIZER_CDECL __tsan_atomic64_fetch_or(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo);
#if __TSAN_HAS_INT128
__tsan_atomic128 SANITIZER_CDECL __tsan_atomic128_fetch_or(
    volatile __tsan_atomic128 *a, __tsan_atomic128 v, __tsan_memory_order mo);
#endif

__tsan_atomic8 SANITIZER_CDECL __tsan_atomic8_fetch_xor(
    volatile __tsan_atomic8 *a, __tsan_atomic8 v, __tsan_memory_order mo);
__tsan_atomic16 SANITIZER_CDECL __tsan_atomic16_fetch_xor(
    volatile __tsan_atomic16 *a, __tsan_atomic16 v, __tsan_memory_order mo);
__tsan_atomic32 SANITIZER_CDECL __tsan_atomic32_fetch_xor(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo);
__tsan_atomic64 SANITIZER_CDECL __tsan_atomic64_fetch_xor(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo);
#if __TSAN_HAS_INT128
__tsan_atomic128 SANITIZER_CDECL __tsan_atomic128_fetch_xor(
    volatile __tsan_atomic128 *a, __tsan_atomic128 v, __tsan_memory_order mo);
#endif

__tsan_atomic8 SANITIZER_CDECL __tsan_atomic8_fetch_nand(
    volatile __tsan_atomic8 *a, __tsan_atomic8 v, __tsan_memory_order mo);
__tsan_atomic16 SANITIZER_CDECL __tsan_atomic16_fetch_nand(
    volatile __tsan_atomic16 *a, __tsan_atomic16 v, __tsan_memory_order mo);
__tsan_atomic32 SANITIZER_CDECL __tsan_atomic32_fetch_nand(
    volatile __tsan_atomic32 *a, __tsan_atomic32 v, __tsan_memory_order mo);
__tsan_atomic64 SANITIZER_CDECL __tsan_atomic64_fetch_nand(
    volatile __tsan_atomic64 *a, __tsan_atomic64 v, __tsan_memory_order mo);
#if __TSAN_HAS_INT128
__tsan_atomic128 SANITIZER_CDECL __tsan_atomic128_fetch_nand(
    volatile __tsan_atomic128 *a, __tsan_atomic128 v, __tsan_memory_order mo);
#endif

int SANITIZER_CDECL __tsan_atomic8_compare_exchange_weak(
    volatile __tsan_atomic8 *a, __tsan_atomic8 *c, __tsan_atomic8 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
int SANITIZER_CDECL __tsan_atomic16_compare_exchange_weak(
    volatile __tsan_atomic16 *a, __tsan_atomic16 *c, __tsan_atomic16 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
int SANITIZER_CDECL __tsan_atomic32_compare_exchange_weak(
    volatile __tsan_atomic32 *a, __tsan_atomic32 *c, __tsan_atomic32 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
int SANITIZER_CDECL __tsan_atomic64_compare_exchange_weak(
    volatile __tsan_atomic64 *a, __tsan_atomic64 *c, __tsan_atomic64 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
#if __TSAN_HAS_INT128
int SANITIZER_CDECL __tsan_atomic128_compare_exchange_weak(
    volatile __tsan_atomic128 *a, __tsan_atomic128 *c, __tsan_atomic128 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
#endif

int SANITIZER_CDECL __tsan_atomic8_compare_exchange_strong(
    volatile __tsan_atomic8 *a, __tsan_atomic8 *c, __tsan_atomic8 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
int SANITIZER_CDECL __tsan_atomic16_compare_exchange_strong(
    volatile __tsan_atomic16 *a, __tsan_atomic16 *c, __tsan_atomic16 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
int SANITIZER_CDECL __tsan_atomic32_compare_exchange_strong(
    volatile __tsan_atomic32 *a, __tsan_atomic32 *c, __tsan_atomic32 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
int SANITIZER_CDECL __tsan_atomic64_compare_exchange_strong(
    volatile __tsan_atomic64 *a, __tsan_atomic64 *c, __tsan_atomic64 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
#if __TSAN_HAS_INT128
int SANITIZER_CDECL __tsan_atomic128_compare_exchange_strong(
    volatile __tsan_atomic128 *a, __tsan_atomic128 *c, __tsan_atomic128 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
#endif

__tsan_atomic8 SANITIZER_CDECL __tsan_atomic8_compare_exchange_val(
    volatile __tsan_atomic8 *a, __tsan_atomic8 c, __tsan_atomic8 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
__tsan_atomic16 SANITIZER_CDECL __tsan_atomic16_compare_exchange_val(
    volatile __tsan_atomic16 *a, __tsan_atomic16 c, __tsan_atomic16 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
__tsan_atomic32 SANITIZER_CDECL __tsan_atomic32_compare_exchange_val(
    volatile __tsan_atomic32 *a, __tsan_atomic32 c, __tsan_atomic32 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
__tsan_atomic64 SANITIZER_CDECL __tsan_atomic64_compare_exchange_val(
    volatile __tsan_atomic64 *a, __tsan_atomic64 c, __tsan_atomic64 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
#if __TSAN_HAS_INT128
__tsan_atomic128 SANITIZER_CDECL __tsan_atomic128_compare_exchange_val(
    volatile __tsan_atomic128 *a, __tsan_atomic128 c, __tsan_atomic128 v,
    __tsan_memory_order mo, __tsan_memory_order fail_mo);
#endif

void SANITIZER_CDECL __tsan_atomic_thread_fence(__tsan_memory_order mo);
void SANITIZER_CDECL __tsan_atomic_signal_fence(__tsan_memory_order mo);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // TSAN_INTERFACE_ATOMIC_H
