/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
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

#ifndef _ARCH_H
#define _ARCH_H

#include <stdbool.h>
#include "elf.h"

#define INSN_FP_SAVE		1
#define INSN_FP_SETUP		2
#define INSN_FP_RESTORE		3
#define INSN_JUMP_CONDITIONAL	4
#define INSN_JUMP_UNCONDITIONAL	5
#define INSN_JUMP_DYNAMIC	6
#define INSN_CALL		7
#define INSN_CALL_DYNAMIC	8
#define INSN_RETURN		9
#define INSN_CONTEXT_SWITCH	10
#define INSN_BUG		11
#define INSN_NOP		12
#define INSN_OTHER		13
#define INSN_LAST		INSN_OTHER

int arch_decode_instruction(struct elf *elf, struct section *sec,
			    unsigned long offset, unsigned int maxlen,
			    unsigned int *len, unsigned char *type,
			    unsigned long *displacement);

#endif /* _ARCH_H */
