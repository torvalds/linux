// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dwarf-regs.c : Mapping of DWARF debug register numbers into register names.
 * Extracted from probe-finder.c
 *
 * Written by Masami Hiramatsu <mhiramat@redhat.com>
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

/* TODO: switching by dwarf address size */
#ifndef __x86_64__
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

#define regoffset_table x86_32_regoffset_table
#else
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

#define regoffset_table x86_64_regoffset_table
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

struct dwarf_regs_idx {
	const char *name;
	int idx;
};

static const struct dwarf_regs_idx x86_regidx_table[] = {
	{ "rax", 0 }, { "eax", 0 }, { "ax", 0 }, { "al", 0 },
	{ "rdx", 1 }, { "edx", 1 }, { "dx", 1 }, { "dl", 1 },
	{ "rcx", 2 }, { "ecx", 2 }, { "cx", 2 }, { "cl", 2 },
	{ "rbx", 3 }, { "edx", 3 }, { "bx", 3 }, { "bl", 3 },
	{ "rsi", 4 }, { "esi", 4 }, { "si", 4 }, { "sil", 4 },
	{ "rdi", 5 }, { "edi", 5 }, { "di", 5 }, { "dil", 5 },
	{ "rbp", 6 }, { "ebp", 6 }, { "bp", 6 }, { "bpl", 6 },
	{ "rsp", 7 }, { "esp", 7 }, { "sp", 7 }, { "spl", 7 },
	{ "r8", 8 }, { "r8d", 8 }, { "r8w", 8 }, { "r8b", 8 },
	{ "r9", 9 }, { "r9d", 9 }, { "r9w", 9 }, { "r9b", 9 },
	{ "r10", 10 }, { "r10d", 10 }, { "r10w", 10 }, { "r10b", 10 },
	{ "r11", 11 }, { "r11d", 11 }, { "r11w", 11 }, { "r11b", 11 },
	{ "r12", 12 }, { "r12d", 12 }, { "r12w", 12 }, { "r12b", 12 },
	{ "r13", 13 }, { "r13d", 13 }, { "r13w", 13 }, { "r13b", 13 },
	{ "r14", 14 }, { "r14d", 14 }, { "r14w", 14 }, { "r14b", 14 },
	{ "r15", 15 }, { "r15d", 15 }, { "r15w", 15 }, { "r15b", 15 },
	{ "rip", DWARF_REG_PC },
};

int get_arch_regnum(const char *name)
{
	unsigned int i;

	if (*name != '%')
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(x86_regidx_table); i++)
		if (!strcmp(x86_regidx_table[i].name, name + 1))
			return x86_regidx_table[i].idx;
	return -ENOENT;
}
