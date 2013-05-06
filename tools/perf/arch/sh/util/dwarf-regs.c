/*
 * Mapping of DWARF debug register numbers into register names.
 *
 * Copyright (C) 2010 Matt Fleming <matt@console-pimps.org>
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
	return (n <= SH_MAX_REGS) ? sh_regs_table[n] : NULL;
}
