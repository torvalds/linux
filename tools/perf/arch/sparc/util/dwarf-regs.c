// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Mapping of DWARF debug register numbers into register names.
 *
 * Copyright (C) 2010 David S. Miller <davem@davemloft.net>
 */

#include <stddef.h>
#include <dwarf-regs.h>

#define SPARC_MAX_REGS	96

const char *sparc_regs_table[SPARC_MAX_REGS] = {
	"%g0", "%g1", "%g2", "%g3", "%g4", "%g5", "%g6", "%g7",
	"%o0", "%o1", "%o2", "%o3", "%o4", "%o5", "%sp", "%o7",
	"%l0", "%l1", "%l2", "%l3", "%l4", "%l5", "%l6", "%l7",
	"%i0", "%i1", "%i2", "%i3", "%i4", "%i5", "%fp", "%i7",
	"%f0", "%f1", "%f2", "%f3", "%f4", "%f5", "%f6", "%f7",
	"%f8", "%f9", "%f10", "%f11", "%f12", "%f13", "%f14", "%f15",
	"%f16", "%f17", "%f18", "%f19", "%f20", "%f21", "%f22", "%f23",
	"%f24", "%f25", "%f26", "%f27", "%f28", "%f29", "%f30", "%f31",
	"%f32", "%f33", "%f34", "%f35", "%f36", "%f37", "%f38", "%f39",
	"%f40", "%f41", "%f42", "%f43", "%f44", "%f45", "%f46", "%f47",
	"%f48", "%f49", "%f50", "%f51", "%f52", "%f53", "%f54", "%f55",
	"%f56", "%f57", "%f58", "%f59", "%f60", "%f61", "%f62", "%f63",
};

/**
 * get_arch_regstr() - lookup register name from it's DWARF register number
 * @n:	the DWARF register number
 *
 * get_arch_regstr() returns the name of the register in struct
 * regdwarfnum_table from it's DWARF register number. If the register is not
 * found in the table, this returns NULL;
 */
const char *get_arch_regstr(unsigned int n)
{
	return (n < SPARC_MAX_REGS) ? sparc_regs_table[n] : NULL;
}
