// SPDX-License-Identifier: GPL-2.0
/*
 * dwarf-regs.c : Mapping of DWARF debug register numbers into register names.
 *
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <stdio.h>
#include <errno.h> /* for EINVAL */
#include <string.h> /* for strcmp */
#include <dwarf-regs.h>

struct pt_regs_dwarfnum {
	const char *name;
	unsigned int dwarfnum;
};

static struct pt_regs_dwarfnum loongarch_gpr_table[] = {
	{"%r0", 0}, {"%r1", 1}, {"%r2", 2}, {"%r3", 3},
	{"%r4", 4}, {"%r5", 5}, {"%r6", 6}, {"%r7", 7},
	{"%r8", 8}, {"%r9", 9}, {"%r10", 10}, {"%r11", 11},
	{"%r12", 12}, {"%r13", 13}, {"%r14", 14}, {"%r15", 15},
	{"%r16", 16}, {"%r17", 17}, {"%r18", 18}, {"%r19", 19},
	{"%r20", 20}, {"%r21", 21}, {"%r22", 22}, {"%r23", 23},
	{"%r24", 24}, {"%r25", 25}, {"%r26", 26}, {"%r27", 27},
	{"%r28", 28}, {"%r29", 29}, {"%r30", 30}, {"%r31", 31},
	{NULL, 0}
};

const char *get_arch_regstr(unsigned int n)
{
	n %= 32;
	return loongarch_gpr_table[n].name;
}

int regs_query_register_offset(const char *name)
{
	const struct pt_regs_dwarfnum *roff;

	for (roff = loongarch_gpr_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->dwarfnum;
	return -EINVAL;
}
