// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Mapping of DWARF debug register numbers into register names.
 *
 * Copyright (C) 2010 Matt Fleming <matt@console-pimps.org>
 */

#include <stddef.h>
#include <dwarf-regs.h>

/*
 * Generic dwarf analysis helpers
 */

#define SH_MAX_REGS 18
const char *sh_regs_table[SH_MAX_REGS] = {
	"r0",
	"r1",
	"r2",
	"r3",
	"r4",
	"r5",
	"r6",
	"r7",
	"r8",
	"r9",
	"r10",
	"r11",
	"r12",
	"r13",
	"r14",
	"r15",
	"pc",
	"pr",
};

/* Return architecture dependent register string (for kprobe-tracer) */
const char *get_arch_regstr(unsigned int n)
{
	return (n < SH_MAX_REGS) ? sh_regs_table[n] : NULL;
}
