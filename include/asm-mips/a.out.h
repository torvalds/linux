/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2003 by Ralf Baechle
 */
#ifndef _ASM_A_OUT_H
#define _ASM_A_OUT_H

#ifdef __KERNEL__

#include <linux/config.h>

#endif

struct exec
{
	unsigned long a_info;	/* Use macros N_MAGIC, etc for access */
	unsigned a_text;	/* length of text, in bytes */
	unsigned a_data;	/* length of data, in bytes */
	unsigned a_bss;		/* length of uninitialized data area for
				    file, in bytes */
	unsigned a_syms;	/* length of symbol table data in file,
				   in bytes */
	unsigned a_entry;	/* start address */
	unsigned a_trsize;	/* length of relocation info for text, in
				    bytes */
	unsigned a_drsize;	/* length of relocation info for data, in bytes */
};

#define N_TRSIZE(a)	((a).a_trsize)
#define N_DRSIZE(a)	((a).a_drsize)
#define N_SYMSIZE(a)	((a).a_syms)

#ifdef __KERNEL__

#ifdef CONFIG_MIPS32
#define STACK_TOP	TASK_SIZE
#endif
#ifdef CONFIG_MIPS64
#define STACK_TOP	(current->thread.mflags & MF_32BIT_ADDR ? TASK_SIZE32 : TASK_SIZE)
#endif

#endif

#endif /* _ASM_A_OUT_H */
