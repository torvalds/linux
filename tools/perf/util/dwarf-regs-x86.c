// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dwarf-regs.c : Mapping of DWARF debug register numbers into register names.
 * Extracted from probe-finder.c
 *
 * Written by Masami Hiramatsu <mhiramat@redhat.com>
 */

#include <errno.h> /* for EINVAL */
#include <string.h> /* for strcmp */
#include <linux/kernel.h> /* for ARRAY_SIZE */
#include <dwarf-regs.h>

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

int get_x86_regnum(const char *name)
{
	unsigned int i;

	if (*name != '%')
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(x86_regidx_table); i++)
		if (!strcmp(x86_regidx_table[i].name, name + 1))
			return x86_regidx_table[i].idx;
	return -ENOENT;
}
