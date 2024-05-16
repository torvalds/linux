/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Facebook */
#ifndef __ASM_GOTO_WORKAROUND_H
#define __ASM_GOTO_WORKAROUND_H

/*
 * This will bring in asm_goto_output and asm_inline macro definitions
 * if enabled by compiler and config options.
 */
#include <linux/types.h>

#ifdef asm_goto_output
#undef asm_goto_output
#define asm_goto_output(x...) asm volatile("invalid use of asm_goto_output")
#endif

/*
 * asm_inline is defined as asm __inline in "include/linux/compiler_types.h"
 * if supported by the kernel's CC (i.e CONFIG_CC_HAS_ASM_INLINE) which is not
 * supported by CLANG.
 */
#ifdef asm_inline
#undef asm_inline
#define asm_inline asm
#endif

#define volatile(x...) volatile("")
#endif
