/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
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

#ifndef _ORC_TYPES_H
#define _ORC_TYPES_H

#include <linux/types.h>
#include <linux/compiler.h>

/*
 * The ORC_REG_* registers are base registers which are used to find other
 * registers on the stack.
 *
 * ORC_REG_PREV_SP, also known as DWARF Call Frame Address (CFA), is the
 * address of the previous frame: the caller's SP before it called the current
 * function.
 *
 * ORC_REG_UNDEFINED means the corresponding register's value didn't change in
 * the current frame.
 *
 * The most commonly used base registers are SP and BP -- which the previous SP
 * is usually based on -- and PREV_SP and UNDEFINED -- which the previous BP is
 * usually based on.
 *
 * The rest of the base registers are needed for special cases like entry code
 * and GCC realigned stacks.
 */
#define ORC_REG_UNDEFINED		0
#define ORC_REG_PREV_SP			1
#define ORC_REG_DX			2
#define ORC_REG_DI			3
#define ORC_REG_BP			4
#define ORC_REG_SP			5
#define ORC_REG_R10			6
#define ORC_REG_R13			7
#define ORC_REG_BP_INDIRECT		8
#define ORC_REG_SP_INDIRECT		9
#define ORC_REG_MAX			15

/*
 * ORC_TYPE_CALL: Indicates that sp_reg+sp_offset resolves to PREV_SP (the
 * caller's SP right before it made the call).  Used for all callable
 * functions, i.e. all C code and all callable asm functions.
 *
 * ORC_TYPE_REGS: Used in entry code to indicate that sp_reg+sp_offset points
 * to a fully populated pt_regs from a syscall, interrupt, or exception.
 *
 * ORC_TYPE_REGS_IRET: Used in entry code to indicate that sp_reg+sp_offset
 * points to the iret return frame.
 */
#define ORC_TYPE_CALL			0
#define ORC_TYPE_REGS			1
#define ORC_TYPE_REGS_IRET		2

/*
 * This struct is more or less a vastly simplified version of the DWARF Call
 * Frame Information standard.  It contains only the necessary parts of DWARF
 * CFI, simplified for ease of access by the in-kernel unwinder.  It tells the
 * unwinder how to find the previous SP and BP (and sometimes entry regs) on
 * the stack for a given code address.  Each instance of the struct corresponds
 * to one or more code locations.
 */
struct orc_entry {
	s16		sp_offset;
	s16		bp_offset;
	unsigned	sp_reg:4;
	unsigned	bp_reg:4;
	unsigned	type:2;
} __packed;

#endif /* _ORC_TYPES_H */
