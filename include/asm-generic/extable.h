/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_EXTABLE_H
#define __ASM_GENERIC_EXTABLE_H

/*
 * The exception table consists of pairs of addresses: the first is the
 * address of an instruction that is allowed to fault, and the second is
 * the address at which the program should continue.  No registers are
 * modified, so it is entirely up to the continuation code to figure out
 * what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry
{
	unsigned long insn, fixup;
};


struct pt_regs;
extern int fixup_exception(struct pt_regs *regs);

#endif
