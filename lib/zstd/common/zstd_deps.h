/* SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause */
/*
 * Copyright (c) Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

/*
 * This file provides common libc dependencies that zstd requires.
 * The purpose is to allow replacing this file with a custom implementation
 * to compile zstd without libc support.
 */

/* Need:
 * NULL
 * INT_MAX
 * UINT_MAX
 * ZSTD_memcpy()
 * ZSTD_memset()
 * ZSTD_memmove()
 */
#ifndef ZSTD_DEPS_COMMON
#define ZSTD_DEPS_COMMON

#include <linux/limits.h>
#include <linux/stddef.h>

#define ZSTD_memcpy(d,s,n) __builtin_memcpy((d),(s),(n))
#define ZSTD_memmove(d,s,n) __builtin_memmove((d),(s),(n))
#define ZSTD_memset(d,s,n) __builtin_memset((d),(s),(n))

#endif /* ZSTD_DEPS_COMMON */

/*
 * Define malloc as always failing. That means the user must
 * either use ZSTD_customMem or statically allocate memory.
 * Need:
 * ZSTD_malloc()
 * ZSTD_free()
 * ZSTD_calloc()
 */
#ifdef ZSTD_DEPS_NEED_MALLOC
#ifndef ZSTD_DEPS_MALLOC
#define ZSTD_DEPS_MALLOC

#define ZSTD_malloc(s) ({ (void)(s); NULL; })
#define ZSTD_free(p) ((void)(p))
#define ZSTD_calloc(n,s) ({ (void)(n); (void)(s); NULL; })

#endif /* ZSTD_DEPS_MALLOC */
#endif /* ZSTD_DEPS_NEED_MALLOC */

/*
 * Provides 64-bit math support.
 * Need:
 * U64 ZSTD_div64(U64 dividend, U32 divisor)
 */
#ifdef ZSTD_DEPS_NEED_MATH64
#ifndef ZSTD_DEPS_MATH64
#define ZSTD_DEPS_MATH64

#include <linux/math64.h>

static uint64_t ZSTD_div64(uint64_t dividend, uint32_t divisor) {
  return div_u64(dividend, divisor);
}

#endif /* ZSTD_DEPS_MATH64 */
#endif /* ZSTD_DEPS_NEED_MATH64 */

/*
 * This is only requested when DEBUGLEVEL >= 1, meaning
 * it is disabled in production.
 * Need:
 * assert()
 */
#ifdef ZSTD_DEPS_NEED_ASSERT
#ifndef ZSTD_DEPS_ASSERT
#define ZSTD_DEPS_ASSERT

#include <linux/kernel.h>

#define assert(x) WARN_ON((x))

#endif /* ZSTD_DEPS_ASSERT */
#endif /* ZSTD_DEPS_NEED_ASSERT */

/*
 * This is only requested when DEBUGLEVEL >= 2, meaning
 * it is disabled in production.
 * Need:
 * ZSTD_DEBUG_PRINT()
 */
#ifdef ZSTD_DEPS_NEED_IO
#ifndef ZSTD_DEPS_IO
#define ZSTD_DEPS_IO

#include <linux/printk.h>

#define ZSTD_DEBUG_PRINT(...) pr_debug(__VA_ARGS__)

#endif /* ZSTD_DEPS_IO */
#endif /* ZSTD_DEPS_NEED_IO */

/*
 * Only requested when MSAN is enabled.
 * Need:
 * intptr_t
 */
#ifdef ZSTD_DEPS_NEED_STDINT
#ifndef ZSTD_DEPS_STDINT
#define ZSTD_DEPS_STDINT

/*
 * The Linux Kernel doesn't provide intptr_t, only uintptr_t, which
 * is an unsigned long.
 */
typedef long intptr_t;

#endif /* ZSTD_DEPS_STDINT */
#endif /* ZSTD_DEPS_NEED_STDINT */
