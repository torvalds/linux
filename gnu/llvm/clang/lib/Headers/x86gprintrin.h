/*===--------------- x86gprintrin.h - X86 GPR intrinsics ------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __X86GPRINTRIN_H
#define __X86GPRINTRIN_H

#if !defined(__SCE__) || __has_feature(modules) || defined(__HRESET__)
#include <hresetintrin.h>
#endif

#if !defined(__SCE__) || __has_feature(modules) || defined(__UINTR__)
#include <uintrintrin.h>
#endif

#if !defined(__SCE__) || __has_feature(modules) || defined(__USERMSR__)
#include <usermsrintrin.h>
#endif

#if !defined(__SCE__) || __has_feature(modules) || defined(__CRC32__)
#include <crc32intrin.h>
#endif

#if !defined(__SCE__) || __has_feature(modules) || defined(__PRFCHI__)
#include <prfchiintrin.h>
#endif

#if !defined(__SCE__) || __has_feature(modules) || defined(__RAOINT__)
#include <raointintrin.h>
#endif

#if !defined(__SCE__) || __has_feature(modules) || defined(__CMPCCXADD__)
#include <cmpccxaddintrin.h>
#endif

#if defined(__i386__)
#define __SAVE_GPRBX "mov {%%ebx, %%eax |eax, ebx};"
#define __RESTORE_GPRBX "mov {%%eax, %%ebx |ebx, eax};"
#define __TMPGPR "eax"
#else
// When in 64-bit target, the 32-bit operands generate a 32-bit result,
// zero-extended to a 64-bit result in the destination general-purpose,
// It means "mov x %ebx" will clobber the higher 32 bits of rbx, so we
// should preserve the 64-bit register rbx.
#define __SAVE_GPRBX "mov {%%rbx, %%rax |rax, rbx};"
#define __RESTORE_GPRBX "mov {%%rax, %%rbx |rbx, rax};"
#define __TMPGPR "rax"
#endif

#define __SSC_MARK(__Tag)                                                      \
  __asm__ __volatile__( __SAVE_GPRBX                                           \
                       "mov {%0, %%ebx|ebx, %0}; "                             \
                       ".byte 0x64, 0x67, 0x90; "                              \
                        __RESTORE_GPRBX                                        \
                       ::"i"(__Tag)                                            \
                       :  __TMPGPR );

#endif /* __X86GPRINTRIN_H */
