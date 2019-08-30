/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Facebook */
#ifndef __ASM_GOTO_WORKAROUND_H
#define __ASM_GOTO_WORKAROUND_H

/* this will bring in asm_volatile_goto macro definition
 * if enabled by compiler and config options.
 */
#include <linux/types.h>

#ifdef asm_volatile_goto
#undef asm_volatile_goto
#define asm_volatile_goto(x...) asm volatile("invalid use of asm_volatile_goto")
#endif

#define volatile(x...) volatile("")
#endif
