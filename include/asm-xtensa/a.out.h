/*
 * include/asm-xtensa/a.out.h
 *
 * Dummy a.out file. Xtensa does not support the a.out format, but the kernel
 * seems to depend on it.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_A_OUT_H
#define _XTENSA_A_OUT_H

/* Note: the kernel needs the a.out definitions, even if only ELF is used. */

#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX	STACK_TOP

struct exec
{
  unsigned long a_info;
  unsigned a_text;
  unsigned a_data;
  unsigned a_bss;
  unsigned a_syms;
  unsigned a_entry;
  unsigned a_trsize;
  unsigned a_drsize;
};

#endif /* _XTENSA_A_OUT_H */
