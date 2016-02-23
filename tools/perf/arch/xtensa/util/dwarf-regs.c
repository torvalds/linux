/*
 * Mapping of DWARF debug register numbers into register names.
 *
 * Copyright (c) 2015 Cadence Design Systems Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <stddef.h>
#include <dwarf-regs.h>

#define XTENSA_MAX_REGS 16

const char *xtensa_regs_table[XTENSA_MAX_REGS] = {
	"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
	"a8", "a9", "a10", "a11", "a12", "a13", "a14", "a15",
};

const char *get_arch_regstr(unsigned int n)
{
	return n < XTENSA_MAX_REGS ? xtensa_regs_table[n] : NULL;
}
