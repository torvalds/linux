/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_OBJTOOL_TYPES_H
#define _LINUX_OBJTOOL_TYPES_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

/*
 * This struct is used by asm and inline asm code to manually annotate the
 * location of registers on the stack.
 */
struct unwind_hint {
	u32		ip;
	s16		sp_offset;
	u8		sp_reg;
	u8		type;
	u8		signal;
	u8		end;
};

#endif /* __ASSEMBLY__ */

/*
 * UNWIND_HINT_TYPE_CALL: Indicates that sp_reg+sp_offset resolves to PREV_SP
 * (the caller's SP right before it made the call).  Used for all callable
 * functions, i.e. all C code and all callable asm functions.
 *
 * UNWIND_HINT_TYPE_REGS: Used in entry code to indicate that sp_reg+sp_offset
 * points to a fully populated pt_regs from a syscall, interrupt, or exception.
 *
 * UNWIND_HINT_TYPE_REGS_PARTIAL: Used in entry code to indicate that
 * sp_reg+sp_offset points to the iret return frame.
 *
 * UNWIND_HINT_FUNC: Generate the unwind metadata of a callable function.
 * Useful for code which doesn't have an ELF function annotation.
 *
 * UNWIND_HINT_ENTRY: machine entry without stack, SYSCALL/SYSENTER etc.
 */
#define UNWIND_HINT_TYPE_CALL		0
#define UNWIND_HINT_TYPE_REGS		1
#define UNWIND_HINT_TYPE_REGS_PARTIAL	2
#define UNWIND_HINT_TYPE_FUNC		3
#define UNWIND_HINT_TYPE_ENTRY		4
#define UNWIND_HINT_TYPE_SAVE		5
#define UNWIND_HINT_TYPE_RESTORE	6

#endif /* _LINUX_OBJTOOL_TYPES_H */
