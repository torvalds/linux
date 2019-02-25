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
#include <asm/insn.h>
#include "lib/inat.c"
#include "lib/insn.c"

#include "../../elf.h"
#include "../../arch.h"
#include "../../warn.h"

static unsigned char op_to_cfi_reg[][2] = {
	{CFI_AX, CFI_R8},
	{CFI_CX, CFI_R9},
	{CFI_DX, CFI_R10},
	{CFI_BX, CFI_R11},
	{CFI_SP, CFI_R12},
	{CFI_BP, CFI_R13},
	{CFI_SI, CFI_R14},
	{CFI_DI, CFI_R15},
};

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

bool arch_callee_saved_reg(unsigned char reg)
{
	switch (reg) {
	case CFI_BP:
	case CFI_BX:
	case CFI_R12:
	case CFI_R13:
	case CFI_R14:
	case CFI_R15:
		return true;

	case CFI_AX:
	case CFI_CX:
	case CFI_DX:
	case CFI_SI:
	case CFI_DI:
	case CFI_SP:
	case CFI_R8:
	case CFI_R9:
	case CFI_R10:
	case CFI_R11:
	case CFI_RA:
	default:
		return false;
	}
}

int arch_decode_instruction(struct elf *elf, struct section *sec,
			    unsigned long offset, unsigned int maxlen,
			    unsigned int *len, unsigned char *type,
			    unsigned long *immediate, struct stack_op *op)
{
	struct insn insn;
	int x86_64, sign;
	unsigned char op1, op2, rex = 0, rex_b = 0, rex_r = 0, rex_w = 0,
		      rex_x = 0, modrm = 0, modrm_mod = 0, modrm_rm = 0,
		      modrm_reg = 0, sib = 0;

	x86_64 = is_x86_64(elf);
	if (x86_64 == -1)
		return -1;

	insn_init(&insn, sec->data->d_buf + offset, maxlen, x86_64);
	insn_get_length(&insn);

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

	if (insn.rex_prefix.nbytes) {
		rex = insn.rex_prefix.bytes[0];
		rex_w = X86_REX_W(rex) >> 3;
		rex_r = X86_REX_R(rex) >> 2;
		rex_x = X86_REX_X(rex) >> 1;
		rex_b = X86_REX_B(rex);
	}

	if (insn.modrm.nbytes) {
		modrm = insn.modrm.bytes[0];
		modrm_mod = X86_MODRM_MOD(modrm);
		modrm_reg = X86_MODRM_REG(modrm);
		modrm_rm = X86_MODRM_RM(modrm);
	}

	if (insn.sib.nbytes)
		sib = insn.sib.bytes[0];

	switch (op1) {

	case 0x1:
	case 0x29:
		if (rex_w && !rex_b && modrm_mod == 3 && modrm_rm == 4) {

			/* add/sub reg, %rsp */
			*type = INSN_STACK;
			op->src.type = OP_SRC_ADD;
			op->src.reg = op_to_cfi_reg[modrm_reg][rex_r];
			op->dest.type = OP_DEST_REG;
			op->dest.reg = CFI_SP;
		}
		break;

	case 0x50 ... 0x57:

		/* push reg */
		*type = INSN_STACK;
		op->src.type = OP_SRC_REG;
		op->src.reg = op_to_cfi_reg[op1 & 0x7][rex_b];
		op->dest.type = OP_DEST_PUSH;

		break;

	case 0x58 ... 0x5f:

		/* pop reg */
		*type = INSN_STACK;
		op->src.type = OP_SRC_POP;
		op->dest.type = OP_DEST_REG;
		op->dest.reg = op_to_cfi_reg[op1 & 0x7][rex_b];

		break;

	case 0x68:
	case 0x6a:
		/* push immediate */
		*type = INSN_STACK;
		op->src.type = OP_SRC_CONST;
		op->dest.type = OP_DEST_PUSH;
		break;

	case 0x70 ... 0x7f:
		*type = INSN_JUMP_CONDITIONAL;
		break;

	case 0x81:
	case 0x83:
		if (rex != 0x48)
			break;

		if (modrm == 0xe4) {
			/* and imm, %rsp */
			*type = INSN_STACK;
			op->src.type = OP_SRC_AND;
			op->src.reg = CFI_SP;
			op->src.offset = insn.immediate.value;
			op->dest.type = OP_DEST_REG;
			op->dest.reg = CFI_SP;
			break;
		}

		if (modrm == 0xc4)
			sign = 1;
		else if (modrm == 0xec)
			sign = -1;
		else
			break;

		/* add/sub imm, %rsp */
		*type = INSN_STACK;
		op->src.type = OP_SRC_ADD;
		op->src.reg = CFI_SP;
		op->src.offset = insn.immediate.value * sign;
		op->dest.type = OP_DEST_REG;
		op->dest.reg = CFI_SP;
		break;

	case 0x89:
		if (rex_w && !rex_r && modrm_mod == 3 && modrm_reg == 4) {

			/* mov %rsp, reg */
			*type = INSN_STACK;
			op->src.type = OP_SRC_REG;
			op->src.reg = CFI_SP;
			op->dest.type = OP_DEST_REG;
			op->dest.reg = op_to_cfi_reg[modrm_rm][rex_b];
			break;
		}

		if (rex_w && !rex_b && modrm_mod == 3 && modrm_rm == 4) {

			/* mov reg, %rsp */
			*type = INSN_STACK;
			op->src.type = OP_SRC_REG;
			op->src.reg = op_to_cfi_reg[modrm_reg][rex_r];
			op->dest.type = OP_DEST_REG;
			op->dest.reg = CFI_SP;
			break;
		}

		/* fallthrough */
	case 0x88:
		if (!rex_b &&
		    (modrm_mod == 1 || modrm_mod == 2) && modrm_rm == 5) {

			/* mov reg, disp(%rbp) */
			*type = INSN_STACK;
			op->src.type = OP_SRC_REG;
			op->src.reg = op_to_cfi_reg[modrm_reg][rex_r];
			op->dest.type = OP_DEST_REG_INDIRECT;
			op->dest.reg = CFI_BP;
			op->dest.offset = insn.displacement.value;

		} else if (rex_w && !rex_b && modrm_rm == 4 && sib == 0x24) {

			/* mov reg, disp(%rsp) */
			*type = INSN_STACK;
			op->src.type = OP_SRC_REG;
			op->src.reg = op_to_cfi_reg[modrm_reg][rex_r];
			op->dest.type = OP_DEST_REG_INDIRECT;
			op->dest.reg = CFI_SP;
			op->dest.offset = insn.displacement.value;
		}

		break;

	case 0x8b:
		if (rex_w && !rex_b && modrm_mod == 1 && modrm_rm == 5) {

			/* mov disp(%rbp), reg */
			*type = INSN_STACK;
			op->src.type = OP_SRC_REG_INDIRECT;
			op->src.reg = CFI_BP;
			op->src.offset = insn.displacement.value;
			op->dest.type = OP_DEST_REG;
			op->dest.reg = op_to_cfi_reg[modrm_reg][rex_r];

		} else if (rex_w && !rex_b && sib == 0x24 &&
			   modrm_mod != 3 && modrm_rm == 4) {

			/* mov disp(%rsp), reg */
			*type = INSN_STACK;
			op->src.type = OP_SRC_REG_INDIRECT;
			op->src.reg = CFI_SP;
			op->src.offset = insn.displacement.value;
			op->dest.type = OP_DEST_REG;
			op->dest.reg = op_to_cfi_reg[modrm_reg][rex_r];
		}

		break;

	case 0x8d:
		if (sib == 0x24 && rex_w && !rex_b && !rex_x) {

			*type = INSN_STACK;
			if (!insn.displacement.value) {
				/* lea (%rsp), reg */
				op->src.type = OP_SRC_REG;
			} else {
				/* lea disp(%rsp), reg */
				op->src.type = OP_SRC_ADD;
				op->src.offset = insn.displacement.value;
			}
			op->src.reg = CFI_SP;
			op->dest.type = OP_DEST_REG;
			op->dest.reg = op_to_cfi_reg[modrm_reg][rex_r];

		} else if (rex == 0x48 && modrm == 0x65) {

			/* lea disp(%rbp), %rsp */
			*type = INSN_STACK;
			op->src.type = OP_SRC_ADD;
			op->src.reg = CFI_BP;
			op->src.offset = insn.displacement.value;
			op->dest.type = OP_DEST_REG;
			op->dest.reg = CFI_SP;

		} else if (rex == 0x49 && modrm == 0x62 &&
			   insn.displacement.value == -8) {

			/*
			 * lea -0x8(%r10), %rsp
			 *
			 * Restoring rsp back to its original value after a
			 * stack realignment.
			 */
			*type = INSN_STACK;
			op->src.type = OP_SRC_ADD;
			op->src.reg = CFI_R10;
			op->src.offset = -8;
			op->dest.type = OP_DEST_REG;
			op->dest.reg = CFI_SP;

		} else if (rex == 0x49 && modrm == 0x65 &&
			   insn.displacement.value == -16) {

			/*
			 * lea -0x10(%r13), %rsp
			 *
			 * Restoring rsp back to its original value after a
			 * stack realignment.
			 */
			*type = INSN_STACK;
			op->src.type = OP_SRC_ADD;
			op->src.reg = CFI_R13;
			op->src.offset = -16;
			op->dest.type = OP_DEST_REG;
			op->dest.reg = CFI_SP;
		}

		break;

	case 0x8f:
		/* pop to mem */
		*type = INSN_STACK;
		op->src.type = OP_SRC_POP;
		op->dest.type = OP_DEST_MEM;
		break;

	case 0x90:
		*type = INSN_NOP;
		break;

	case 0x9c:
		/* pushf */
		*type = INSN_STACK;
		op->src.type = OP_SRC_CONST;
		op->dest.type = OP_DEST_PUSHF;
		break;

	case 0x9d:
		/* popf */
		*type = INSN_STACK;
		op->src.type = OP_SRC_POPF;
		op->dest.type = OP_DEST_MEM;
		break;

	case 0x0f:

		if (op2 == 0x01) {

			if (modrm == 0xca)
				*type = INSN_CLAC;
			else if (modrm == 0xcb)
				*type = INSN_STAC;

		} else if (op2 >= 0x80 && op2 <= 0x8f) {

			*type = INSN_JUMP_CONDITIONAL;

		} else if (op2 == 0x05 || op2 == 0x07 || op2 == 0x34 ||
			   op2 == 0x35) {

			/* sysenter, sysret */
			*type = INSN_CONTEXT_SWITCH;

		} else if (op2 == 0x0b || op2 == 0xb9) {

			/* ud2 */
			*type = INSN_BUG;

		} else if (op2 == 0x0d || op2 == 0x1f) {

			/* nopl/nopw */
			*type = INSN_NOP;

		} else if (op2 == 0xa0 || op2 == 0xa8) {

			/* push fs/gs */
			*type = INSN_STACK;
			op->src.type = OP_SRC_CONST;
			op->dest.type = OP_DEST_PUSH;

		} else if (op2 == 0xa1 || op2 == 0xa9) {

			/* pop fs/gs */
			*type = INSN_STACK;
			op->src.type = OP_SRC_POP;
			op->dest.type = OP_DEST_MEM;
		}

		break;

	case 0xc9:
		/*
		 * leave
		 *
		 * equivalent to:
		 * mov bp, sp
		 * pop bp
		 */
		*type = INSN_STACK;
		op->dest.type = OP_DEST_LEAVE;

		break;

	case 0xe3:
		/* jecxz/jrcxz */
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

	case 0xca: /* retf */
	case 0xcb: /* retf */
	case 0xcf: /* iret */
		*type = INSN_CONTEXT_SWITCH;
		break;

	case 0xe8:
		*type = INSN_CALL;
		break;

	case 0xfc:
		*type = INSN_CLD;
		break;

	case 0xfd:
		*type = INSN_STD;
		break;

	case 0xff:
		if (modrm_reg == 2 || modrm_reg == 3)

			*type = INSN_CALL_DYNAMIC;

		else if (modrm_reg == 4)

			*type = INSN_JUMP_DYNAMIC;

		else if (modrm_reg == 5)

			/* jmpf */
			*type = INSN_CONTEXT_SWITCH;

		else if (modrm_reg == 6) {

			/* push from mem */
			*type = INSN_STACK;
			op->src.type = OP_SRC_CONST;
			op->dest.type = OP_DEST_PUSH;
		}

		break;

	default:
		break;
	}

	*immediate = insn.immediate.nbytes ? insn.immediate.value : 0;

	return 0;
}

void arch_initial_func_cfi_state(struct cfi_state *state)
{
	int i;

	for (i = 0; i < CFI_NUM_REGS; i++) {
		state->regs[i].base = CFI_UNDEFINED;
		state->regs[i].offset = 0;
	}

	/* initial CFA (call frame address) */
	state->cfa.base = CFI_SP;
	state->cfa.offset = 8;

	/* initial RA (return address) */
	state->regs[16].base = CFI_CFA;
	state->regs[16].offset = -8;
}
