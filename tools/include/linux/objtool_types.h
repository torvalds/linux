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
};

#endif /* __ASSEMBLY__ */

/*
 * UNWIND_HINT_TYPE_UNDEFINED: A blind spot in ORC coverage which can result in
 * a truncated and unreliable stack unwind.
 *
 * UNWIND_HINT_TYPE_END_OF_STACK: The end of the kernel stack unwind before
 * hitting user entry, boot code, or fork entry (when there are no pt_regs
 * available).
 *
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
 * UNWIND_HINT_TYPE_FUNC: Generate the unwind metadata of a callable function.
 * Useful for code which doesn't have an ELF function annotation.
 *
 * UNWIND_HINT_TYPE_{SAVE,RESTORE}: Save the unwind metadata at a certain
 * location so that it can be restored later.
 */
#define UNWIND_HINT_TYPE_UNDEFINED	0
#define UNWIND_HINT_TYPE_END_OF_STACK	1
#define UNWIND_HINT_TYPE_CALL		2
#define UNWIND_HINT_TYPE_REGS		3
#define UNWIND_HINT_TYPE_REGS_PARTIAL	4
/* The below hint types don't have corresponding ORC types */
#define UNWIND_HINT_TYPE_FUNC		5
#define UNWIND_HINT_TYPE_SAVE		6
#define UNWIND_HINT_TYPE_RESTORE	7

#endif /* _LINUX_OBJTOOL_TYPES_H */
