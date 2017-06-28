/*
 * Copyright (C) 2015-2017 Josh Poimboeuf <jpoimboe@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _OBJTOOL_CFI_H
#define _OBJTOOL_CFI_H

#define CFI_UNDEFINED		-1
#define CFI_CFA			-2
#define CFI_SP_INDIRECT		-3
#define CFI_BP_INDIRECT		-4

#define CFI_AX			0
#define CFI_DX			1
#define CFI_CX			2
#define CFI_BX			3
#define CFI_SI			4
#define CFI_DI			5
#define CFI_BP			6
#define CFI_SP			7
#define CFI_R8			8
#define CFI_R9			9
#define CFI_R10			10
#define CFI_R11			11
#define CFI_R12			12
#define CFI_R13			13
#define CFI_R14			14
#define CFI_R15			15
#define CFI_RA			16
#define CFI_NUM_REGS	17

struct cfi_reg {
	int base;
	int offset;
};

struct cfi_state {
	struct cfi_reg cfa;
	struct cfi_reg regs[CFI_NUM_REGS];
};

#endif /* _OBJTOOL_CFI_H */
