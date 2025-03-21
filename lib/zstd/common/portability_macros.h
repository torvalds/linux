/*
 * Copyright (c) Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_PORTABILITY_MACROS_H
#define ZSTD_PORTABILITY_MACROS_H

/*
 * This header file contains macro defintions to support portability.
 * This header is shared between C and ASM code, so it MUST only
 * contain macro definitions. It MUST not contain any C code.
 *
 * This header ONLY defines macros to detect platforms/feature support.
 *
 */


/* compat. with non-clang compilers */
#ifndef __has_attribute
  #define __has_attribute(x) 0
#endif

/* compat. with non-clang compilers */
#ifndef __has_builtin
#  define __has_builtin(x) 0
#endif

/* compat. with non-clang compilers */
#ifndef __has_feature
#  define __has_feature(x) 0
#endif

/* detects whether we are being compiled under msan */

/* detects whether we are being compiled under asan */

/* detects whether we are being compiled under dfsan */

/* Mark the internal assembly functions as hidden  */
#ifdef __ELF__
# define ZSTD_HIDE_ASM_FUNCTION(func) .hidden func
#else
# define ZSTD_HIDE_ASM_FUNCTION(func)
#endif

/* Enable runtime BMI2 dispatch based on the CPU.
 * Enabled for clang & gcc >=4.8 on x86 when BMI2 isn't enabled by default.
 */
#ifndef DYNAMIC_BMI2
  #if ((defined(__clang__) && __has_attribute(__target__)) \
      || (defined(__GNUC__) \
          && (__GNUC__ >= 11))) \
      && (defined(__x86_64__) || defined(_M_X64)) \
      && !defined(__BMI2__)
  #  define DYNAMIC_BMI2 1
  #else
  #  define DYNAMIC_BMI2 0
  #endif
#endif

/*
 * Only enable assembly for GNUC comptabile compilers,
 * because other platforms may not support GAS assembly syntax.
 *
 * Only enable assembly for Linux / MacOS, other platforms may
 * work, but they haven't been tested. This could likely be
 * extended to BSD systems.
 *
 * Disable assembly when MSAN is enabled, because MSAN requires
 * 100% of code to be instrumented to work.
 */
#define ZSTD_ASM_SUPPORTED 1

/*
 * Determines whether we should enable assembly for x86-64
 * with BMI2.
 *
 * Enable if all of the following conditions hold:
 * - ASM hasn't been explicitly disabled by defining ZSTD_DISABLE_ASM
 * - Assembly is supported
 * - We are compiling for x86-64 and either:
 *   - DYNAMIC_BMI2 is enabled
 *   - BMI2 is supported at compile time
 */
#define ZSTD_ENABLE_ASM_X86_64_BMI2 0

#endif /* ZSTD_PORTABILITY_MACROS_H */
