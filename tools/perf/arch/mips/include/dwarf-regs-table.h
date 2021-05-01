/* SPDX-License-Identifier: GPL-2.0 */
/*
 * dwarf-regs-table.h : Mapping of DWARF debug register numbers into
 * register names.
 *
 * Copyright (C) 2013 Cavium, Inc.
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
 */

#ifdef DEFINE_DWARF_REGSTR_TABLE
#undef REG_DWARFNUM_NAME
#define REG_DWARFNUM_NAME(reg, idx)	[idx] = "$" #reg
static const char * const mips_regstr_tbl[] = {
	"$0", "$1", "$2", "$3", "$4", "$5", "$6", "$7", "$8", "$9",
	"$10", "$11", "$12", "$13", "$14", "$15", "$16", "$17", "$18", "$19",
	"$20", "$21", "$22", "$23", "$24", "$25", "$26", "$27", "$28", "%29",
	"$30", "$31",
	REG_DWARFNUM_NAME(hi, 64),
	REG_DWARFNUM_NAME(lo, 65),
};
#endif
