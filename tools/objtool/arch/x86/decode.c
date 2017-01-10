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

#include <stdio.h>
#include <stdlib.h>

#define unlikely(cond) (cond)
#include "insn/insn.h"
#include "insn/inat.c"
#include "insn/insn.c"

#include "../../elf.h"
#include "../../arch.h"
#include "../../warn.h"

static int is_x86_64(struct elf *elf)
{
	switch (elf->ehdr.e_machine) {
	case EM_X86_64:
		return 1;
	case EM_386:
		return 0;
	default:
		WARN("unexpected ELF machine type %d", elf->ehdr.e_machine);
		return -1;
	}
}

int arch_decode_instruction(struct elf *elf, struct section *sec,
			    unsigned long offset, unsigned int maxlen,
			    unsigned int *len, unsigned char *type,
			    unsigned long *immediate)
{
	struct insn insn;
	int x86_64;
	unsigned char op1, op2, ext;

	x86_64 = is_x86_64(elf);
	if (x86_64 == -1)
		return -1;

	insn_init(&insn, (void *)(sec->data + offset), maxlen, x86_64);
	insn_get_length(&insn);
	insn_get_opcode(&insn);
	insn_get_modrm(&insn);
	insn_get_immediate(&insn);

	if (!insn_complete(&insn)) {
		WARN_FUNC("can't decode instruction", sec, offset);
		return -1;
	}

	*len = insn.length;
	*type = INSN_OTHER;

	if (insn.vex_prefix.nbytes)
		return 0;

	op1 = insn.opcode.bytes[0];
	op2 = insn.opcode.bytes[1];

	switch (op1) {
	case 0x55:
		if (!insn.rex_prefix.nbytes)
			/* push rbp */
			*type = INSN_FP_SAVE;
		break;

	case 0x5d:
		if (!insn.rex_prefix.nbytes)
			/* pop rbp */
			*type = INSN_FP_RESTORE;
		break;

	case 0x70 ... 0x7f:
		*type = INSN_JUMP_CONDITIONAL;
		break;

	case 0x89:
		if (insn.rex_prefix.nbytes == 1 &&
		    insn.rex_prefix.bytes[0] == 0x48 &&
		    insn.modrm.nbytes && insn.modrm.bytes[0] == 0xe5)
			/* mov rsp, rbp */
			*type = INSN_FP_SETUP;
		break;

	case 0x8d:
		if (insn.rex_prefix.nbytes &&
		    insn.rex_prefix.bytes[0] == 0x48 &&
		    insn.modrm.nbytes && insn.modrm.bytes[0] == 0x2c &&
		    insn.sib.nbytes && insn.sib.bytes[0] == 0x24)
			/* lea %(rsp), %rbp */
			*type = INSN_FP_SETUP;
		break;

	case 0x90:
		*type = INSN_NOP;
		break;

	case 0x0f:
		if (op2 >= 0x80 && op2 <= 0x8f)
			*type = INSN_JUMP_CONDITIONAL;
		else if (op2 == 0x05 || op2 == 0x07 || op2 == 0x34 ||
			 op2 == 0x35)
			/* sysenter, sysret */
			*type = INSN_CONTEXT_SWITCH;
		else if (op2 == 0x0b || op2 == 0xb9)
			/* ud2 */
			*type = INSN_BUG;
		else if (op2 == 0x0d || op2 == 0x1f)
			/* nopl/nopw */
			*type = INSN_NOP;
		else if (op2 == 0x01 && insn.modrm.nbytes &&
			 (insn.modrm.bytes[0] == 0xc2 ||
			  insn.modrm.bytes[0] == 0xd8))
			/* vmlaunch, vmrun */
			*type = INSN_CONTEXT_SWITCH;

		break;

	case 0xc9: /* leave */
		*type = INSN_FP_RESTORE;
		break;

	case 0xe3: /* jecxz/jrcxz */
		*type = INSN_JUMP_CONDITIONAL;
		break;

	case 0xe9:
	case 0xeb:
		*type = INSN_JUMP_UNCONDITIONAL;
		break;

	case 0xc2:
	case 0xc3:
		*type = INSN_RETURN;
		break;

	case 0xc5: /* iret */
	case 0xca: /* retf */
	case 0xcb: /* retf */
		*type = INSN_CONTEXT_SWITCH;
		break;

	case 0xe8:
		*type = INSN_CALL;
		break;

	case 0xff:
		ext = X86_MODRM_REG(insn.modrm.bytes[0]);
		if (ext == 2 || ext == 3)
			*type = INSN_CALL_DYNAMIC;
		else if (ext == 4)
			*type = INSN_JUMP_DYNAMIC;
		else if (ext == 5) /*jmpf */
			*type = INSN_CONTEXT_SWITCH;

		break;

	default:
		break;
	}

	*immediate = insn.immediate.nbytes ? insn.immediate.value : 0;

	return 0;
}
