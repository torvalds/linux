/*
 * dwarf-regs.c : Mapping of DWARF debug register numbers into register names.
 * Extracted from probe-finder.c
 *
 * Written by Masami Hiramatsu <mhiramat@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <stddef.h>
#include <errno.h> /* for EINVAL */
#include <string.h> /* for strcmp */
#include <linux/ptrace.h> /* for struct pt_regs */
#include <linux/kernel.h> /* for offsetof */
#include <dwarf-regs.h>

/*
 * See arch/x86/kernel/ptrace.c.
 * Different from it:
 *
 *  - Since struct pt_regs is defined differently for user and kernel,
 *    but we want to use 'ax, bx' instead of 'rax, rbx' (which is struct
 *    field name of user's pt_regs), we make REG_OFFSET_NAME to accept
 *    both string name and reg field name.
 *
 *  - Since accessing x86_32's pt_regs from x86_64 building is difficult
 *    and vise versa, we simply fill offset with -1, so
 *    get_arch_regstr() still works but regs_query_register_offset()
 *    returns error.
 *    The only inconvenience caused by it now is that we are not allowed
 *    to generate BPF prologue for a x86_64 kernel if perf is built for
 *    x86_32. This is really a rare usecase.
 *
 *  - Order is different from kernel's ptrace.c for get_arch_regstr(). Use
 *    the order defined by dwarf.
 */

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_END {.name = NULL, .offset = 0}

#ifdef __x86_64__
# define REG_OFFSET_NAME_64(n, r) {.name = n, .offset = offsetof(struct pt_regs, r)}
# define REG_OFFSET_NAME_32(n, r) {.name = n, .offset = -1}
#else
# define REG_OFFSET_NAME_64(n, r) {.name = n, .offset = -1}
# define REG_OFFSET_NAME_32(n, r) {.name = n, .offset = offsetof(struct pt_regs, r)}
#endif

static const struct pt_regs_offset x86_32_regoffset_table[] = {
	REG_OFFSET_NAME_32("%ax",	eax),
	REG_OFFSET_NAME_32("%cx",	ecx),
	REG_OFFSET_NAME_32("%dx",	edx),
	REG_OFFSET_NAME_32("%bx",	ebx),
	REG_OFFSET_NAME_32("$stack",	esp),	/* Stack address instead of %sp */
	REG_OFFSET_NAME_32("%bp",	ebp),
	REG_OFFSET_NAME_32("%si",	esi),
	REG_OFFSET_NAME_32("%di",	edi),
	REG_OFFSET_END,
};

static const struct pt_regs_offset x86_64_regoffset_table[] = {
	REG_OFFSET_NAME_64("%ax",	rax),
	REG_OFFSET_NAME_64("%dx",	rdx),
	REG_OFFSET_NAME_64("%cx",	rcx),
	REG_OFFSET_NAME_64("%bx",	rbx),
	REG_OFFSET_NAME_64("%si",	rsi),
	REG_OFFSET_NAME_64("%di",	rdi),
	REG_OFFSET_NAME_64("%bp",	rbp),
	REG_OFFSET_NAME_64("%sp",	rsp),
	REG_OFFSET_NAME_64("%r8",	r8),
	REG_OFFSET_NAME_64("%r9",	r9),
	REG_OFFSET_NAME_64("%r10",	r10),
	REG_OFFSET_NAME_64("%r11",	r11),
	REG_OFFSET_NAME_64("%r12",	r12),
	REG_OFFSET_NAME_64("%r13",	r13),
	REG_OFFSET_NAME_64("%r14",	r14),
	REG_OFFSET_NAME_64("%r15",	r15),
	REG_OFFSET_END,
};

/* TODO: switching by dwarf address size */
#ifdef __x86_64__
#define regoffset_table x86_64_regoffset_table
#else
#define regoffset_table x86_32_regoffset_table
#endif

/* Minus 1 for the ending REG_OFFSET_END */
#define ARCH_MAX_REGS ((sizeof(regoffset_table) / sizeof(regoffset_table[0])) - 1)

/* Return architecture dependent register string (for kprobe-tracer) */
const char *get_arch_regstr(unsigned int n)
{
	return (n < ARCH_MAX_REGS) ? regoffset_table[n].name : NULL;
}

/* Reuse code from arch/x86/kernel/ptrace.c */
/**
 * regs_query_register_offset() - query register offset from its name
 * @name:	the name of a register
 *
 * regs_query_register_offset() returns the offset of a register in struct
 * pt_regs from its name. If the name is invalid, this returns -EINVAL;
 */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}
