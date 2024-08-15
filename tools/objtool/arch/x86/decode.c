// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#include <stdio.h>
#include <stdlib.h>

#define unlikely(cond) (cond)
#include <asm/insn.h>
#include "../../../arch/x86/lib/inat.c"
#include "../../../arch/x86/lib/insn.c"

#define CONFIG_64BIT 1
#include <asm/nops.h>

#include <asm/orc_types.h>
#include <objtool/check.h>
#include <objtool/elf.h>
#include <objtool/arch.h>
#include <objtool/warn.h>
#include <objtool/endianness.h>
#include <objtool/builtin.h>
#include <arch/elf.h>

static int is_x86_64(const struct elf *elf)
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

unsigned long arch_dest_reloc_offset(int addend)
{
	return addend + 4;
}

unsigned long arch_jump_destination(struct instruction *insn)
{
	return insn->offset + insn->len + insn->immediate;
}

#define ADD_OP(op) \
	if (!(op = calloc(1, sizeof(*op)))) \
		return -1; \
	else for (list_add_tail(&op->list, ops_list); op; op = NULL)

/*
 * Helpers to decode ModRM/SIB:
 *
 * r/m| AX  CX  DX  BX |  SP |  BP |  SI  DI |
 *    | R8  R9 R10 R11 | R12 | R13 | R14 R15 |
 * Mod+----------------+-----+-----+---------+
 * 00 |    [r/m]       |[SIB]|[IP+]|  [r/m]  |
 * 01 |  [r/m + d8]    |[S+d]|   [r/m + d8]  |
 * 10 |  [r/m + d32]   |[S+D]|   [r/m + d32] |
 * 11 |                   r/ m               |
 */

#define mod_is_mem()	(modrm_mod != 3)
#define mod_is_reg()	(modrm_mod == 3)

#define is_RIP()   ((modrm_rm & 7) == CFI_BP && modrm_mod == 0)
#define have_SIB() ((modrm_rm & 7) == CFI_SP && mod_is_mem())

#define rm_is(reg) (have_SIB() ? \
		    sib_base == (reg) && sib_index == CFI_SP : \
		    modrm_rm == (reg))

#define rm_is_mem(reg)	(mod_is_mem() && !is_RIP() && rm_is(reg))
#define rm_is_reg(reg)	(mod_is_reg() && modrm_rm == (reg))

static bool has_notrack_prefix(struct insn *insn)
{
	int i;

	for (i = 0; i < insn->prefixes.nbytes; i++) {
		if (insn->prefixes.bytes[i] == 0x3e)
			return true;
	}

	return false;
}

int arch_decode_instruction(struct objtool_file *file, const struct section *sec,
			    unsigned long offset, unsigned int maxlen,
			    unsigned int *len, enum insn_type *type,
			    unsigned long *immediate,
			    struct list_head *ops_list)
{
	const struct elf *elf = file->elf;
	struct insn insn;
	int x86_64, ret;
	unsigned char op1, op2, op3, prefix,
		      rex = 0, rex_b = 0, rex_r = 0, rex_w = 0, rex_x = 0,
		      modrm = 0, modrm_mod = 0, modrm_rm = 0, modrm_reg = 0,
		      sib = 0, /* sib_scale = 0, */ sib_index = 0, sib_base = 0;
	struct stack_op *op = NULL;
	struct symbol *sym;
	u64 imm;

	x86_64 = is_x86_64(elf);
	if (x86_64 == -1)
		return -1;

	ret = insn_decode(&insn, sec->data->d_buf + offset, maxlen,
			  x86_64 ? INSN_MODE_64 : INSN_MODE_32);
	if (ret < 0) {
		WARN("can't decode instruction at %s:0x%lx", sec->name, offset);
		return -1;
	}

	*len = insn.length;
	*type = INSN_OTHER;

	if (insn.vex_prefix.nbytes)
		return 0;

	prefix = insn.prefixes.bytes[0];

	op1 = insn.opcode.bytes[0];
	op2 = insn.opcode.bytes[1];
	op3 = insn.opcode.bytes[2];

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
		modrm_reg = X86_MODRM_REG(modrm) + 8*rex_r;
		modrm_rm  = X86_MODRM_RM(modrm)  + 8*rex_b;
	}

	if (insn.sib.nbytes) {
		sib = insn.sib.bytes[0];
		/* sib_scale = X86_SIB_SCALE(sib); */
		sib_index = X86_SIB_INDEX(sib) + 8*rex_x;
		sib_base  = X86_SIB_BASE(sib)  + 8*rex_b;
	}

	switch (op1) {

	case 0x1:
	case 0x29:
		if (rex_w && rm_is_reg(CFI_SP)) {

			/* add/sub reg, %rsp */
			ADD_OP(op) {
				op->src.type = OP_SRC_ADD;
				op->src.reg = modrm_reg;
				op->dest.type = OP_DEST_REG;
				op->dest.reg = CFI_SP;
			}
		}
		break;

	case 0x50 ... 0x57:

		/* push reg */
		ADD_OP(op) {
			op->src.type = OP_SRC_REG;
			op->src.reg = (op1 & 0x7) + 8*rex_b;
			op->dest.type = OP_DEST_PUSH;
		}

		break;

	case 0x58 ... 0x5f:

		/* pop reg */
		ADD_OP(op) {
			op->src.type = OP_SRC_POP;
			op->dest.type = OP_DEST_REG;
			op->dest.reg = (op1 & 0x7) + 8*rex_b;
		}

		break;

	case 0x68:
	case 0x6a:
		/* push immediate */
		ADD_OP(op) {
			op->src.type = OP_SRC_CONST;
			op->dest.type = OP_DEST_PUSH;
		}
		break;

	case 0x70 ... 0x7f:
		*type = INSN_JUMP_CONDITIONAL;
		break;

	case 0x80 ... 0x83:
		/*
		 * 1000 00sw : mod OP r/m : immediate
		 *
		 * s - sign extend immediate
		 * w - imm8 / imm32
		 *
		 * OP: 000 ADD    100 AND
		 *     001 OR     101 SUB
		 *     010 ADC    110 XOR
		 *     011 SBB    111 CMP
		 */

		/* 64bit only */
		if (!rex_w)
			break;

		/* %rsp target only */
		if (!rm_is_reg(CFI_SP))
			break;

		imm = insn.immediate.value;
		if (op1 & 2) { /* sign extend */
			if (op1 & 1) { /* imm32 */
				imm <<= 32;
				imm = (s64)imm >> 32;
			} else { /* imm8 */
				imm <<= 56;
				imm = (s64)imm >> 56;
			}
		}

		switch (modrm_reg & 7) {
		case 5:
			imm = -imm;
			/* fallthrough */
		case 0:
			/* add/sub imm, %rsp */
			ADD_OP(op) {
				op->src.type = OP_SRC_ADD;
				op->src.reg = CFI_SP;
				op->src.offset = imm;
				op->dest.type = OP_DEST_REG;
				op->dest.reg = CFI_SP;
			}
			break;

		case 4:
			/* and imm, %rsp */
			ADD_OP(op) {
				op->src.type = OP_SRC_AND;
				op->src.reg = CFI_SP;
				op->src.offset = insn.immediate.value;
				op->dest.type = OP_DEST_REG;
				op->dest.reg = CFI_SP;
			}
			break;

		default:
			/* WARN ? */
			break;
		}

		break;

	case 0x89:
		if (!rex_w)
			break;

		if (modrm_reg == CFI_SP) {

			if (mod_is_reg()) {
				/* mov %rsp, reg */
				ADD_OP(op) {
					op->src.type = OP_SRC_REG;
					op->src.reg = CFI_SP;
					op->dest.type = OP_DEST_REG;
					op->dest.reg = modrm_rm;
				}
				break;

			} else {
				/* skip RIP relative displacement */
				if (is_RIP())
					break;

				/* skip nontrivial SIB */
				if (have_SIB()) {
					modrm_rm = sib_base;
					if (sib_index != CFI_SP)
						break;
				}

				/* mov %rsp, disp(%reg) */
				ADD_OP(op) {
					op->src.type = OP_SRC_REG;
					op->src.reg = CFI_SP;
					op->dest.type = OP_DEST_REG_INDIRECT;
					op->dest.reg = modrm_rm;
					op->dest.offset = insn.displacement.value;
				}
				break;
			}

			break;
		}

		if (rm_is_reg(CFI_SP)) {

			/* mov reg, %rsp */
			ADD_OP(op) {
				op->src.type = OP_SRC_REG;
				op->src.reg = modrm_reg;
				op->dest.type = OP_DEST_REG;
				op->dest.reg = CFI_SP;
			}
			break;
		}

		/* fallthrough */
	case 0x88:
		if (!rex_w)
			break;

		if (rm_is_mem(CFI_BP)) {

			/* mov reg, disp(%rbp) */
			ADD_OP(op) {
				op->src.type = OP_SRC_REG;
				op->src.reg = modrm_reg;
				op->dest.type = OP_DEST_REG_INDIRECT;
				op->dest.reg = CFI_BP;
				op->dest.offset = insn.displacement.value;
			}
			break;
		}

		if (rm_is_mem(CFI_SP)) {

			/* mov reg, disp(%rsp) */
			ADD_OP(op) {
				op->src.type = OP_SRC_REG;
				op->src.reg = modrm_reg;
				op->dest.type = OP_DEST_REG_INDIRECT;
				op->dest.reg = CFI_SP;
				op->dest.offset = insn.displacement.value;
			}
			break;
		}

		break;

	case 0x8b:
		if (!rex_w)
			break;

		if (rm_is_mem(CFI_BP)) {

			/* mov disp(%rbp), reg */
			ADD_OP(op) {
				op->src.type = OP_SRC_REG_INDIRECT;
				op->src.reg = CFI_BP;
				op->src.offset = insn.displacement.value;
				op->dest.type = OP_DEST_REG;
				op->dest.reg = modrm_reg;
			}
			break;
		}

		if (rm_is_mem(CFI_SP)) {

			/* mov disp(%rsp), reg */
			ADD_OP(op) {
				op->src.type = OP_SRC_REG_INDIRECT;
				op->src.reg = CFI_SP;
				op->src.offset = insn.displacement.value;
				op->dest.type = OP_DEST_REG;
				op->dest.reg = modrm_reg;
			}
			break;
		}

		break;

	case 0x8d:
		if (mod_is_reg()) {
			WARN("invalid LEA encoding at %s:0x%lx", sec->name, offset);
			break;
		}

		/* skip non 64bit ops */
		if (!rex_w)
			break;

		/* skip RIP relative displacement */
		if (is_RIP())
			break;

		/* skip nontrivial SIB */
		if (have_SIB()) {
			modrm_rm = sib_base;
			if (sib_index != CFI_SP)
				break;
		}

		/* lea disp(%src), %dst */
		ADD_OP(op) {
			op->src.offset = insn.displacement.value;
			if (!op->src.offset) {
				/* lea (%src), %dst */
				op->src.type = OP_SRC_REG;
			} else {
				/* lea disp(%src), %dst */
				op->src.type = OP_SRC_ADD;
			}
			op->src.reg = modrm_rm;
			op->dest.type = OP_DEST_REG;
			op->dest.reg = modrm_reg;
		}
		break;

	case 0x8f:
		/* pop to mem */
		ADD_OP(op) {
			op->src.type = OP_SRC_POP;
			op->dest.type = OP_DEST_MEM;
		}
		break;

	case 0x90:
		*type = INSN_NOP;
		break;

	case 0x9c:
		/* pushf */
		ADD_OP(op) {
			op->src.type = OP_SRC_CONST;
			op->dest.type = OP_DEST_PUSHF;
		}
		break;

	case 0x9d:
		/* popf */
		ADD_OP(op) {
			op->src.type = OP_SRC_POPF;
			op->dest.type = OP_DEST_MEM;
		}
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

		} else if (op2 == 0x1e) {

			if (prefix == 0xf3 && (modrm == 0xfa || modrm == 0xfb))
				*type = INSN_ENDBR;


		} else if (op2 == 0x38 && op3 == 0xf8) {
			if (insn.prefixes.nbytes == 1 &&
			    insn.prefixes.bytes[0] == 0xf2) {
				/* ENQCMD cannot be used in the kernel. */
				WARN("ENQCMD instruction at %s:%lx", sec->name,
				     offset);
			}

		} else if (op2 == 0xa0 || op2 == 0xa8) {

			/* push fs/gs */
			ADD_OP(op) {
				op->src.type = OP_SRC_CONST;
				op->dest.type = OP_DEST_PUSH;
			}

		} else if (op2 == 0xa1 || op2 == 0xa9) {

			/* pop fs/gs */
			ADD_OP(op) {
				op->src.type = OP_SRC_POP;
				op->dest.type = OP_DEST_MEM;
			}
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
		ADD_OP(op) {
			op->src.type = OP_SRC_REG;
			op->src.reg = CFI_BP;
			op->dest.type = OP_DEST_REG;
			op->dest.reg = CFI_SP;
		}
		ADD_OP(op) {
			op->src.type = OP_SRC_POP;
			op->dest.type = OP_DEST_REG;
			op->dest.reg = CFI_BP;
		}
		break;

	case 0xcc:
		/* int3 */
		*type = INSN_TRAP;
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

	case 0xc7: /* mov imm, r/m */
		if (!opts.noinstr)
			break;

		if (insn.length == 3+4+4 && !strncmp(sec->name, ".init.text", 10)) {
			struct reloc *immr, *disp;
			struct symbol *func;
			int idx;

			immr = find_reloc_by_dest(elf, (void *)sec, offset+3);
			disp = find_reloc_by_dest(elf, (void *)sec, offset+7);

			if (!immr || strcmp(immr->sym->name, "pv_ops"))
				break;

			idx = (immr->addend + 8) / sizeof(void *);

			func = disp->sym;
			if (disp->sym->type == STT_SECTION)
				func = find_symbol_by_offset(disp->sym->sec, disp->addend);
			if (!func) {
				WARN("no func for pv_ops[]");
				return -1;
			}

			objtool_pv_add(file, idx, func);
		}

		break;

	case 0xcf: /* iret */
		/*
		 * Handle sync_core(), which has an IRET to self.
		 * All other IRET are in STT_NONE entry code.
		 */
		sym = find_symbol_containing(sec, offset);
		if (sym && sym->type == STT_FUNC) {
			ADD_OP(op) {
				/* add $40, %rsp */
				op->src.type = OP_SRC_ADD;
				op->src.reg = CFI_SP;
				op->src.offset = 5*8;
				op->dest.type = OP_DEST_REG;
				op->dest.reg = CFI_SP;
			}
			break;
		}

		/* fallthrough */

	case 0xca: /* retf */
	case 0xcb: /* retf */
		*type = INSN_CONTEXT_SWITCH;
		break;

	case 0xe0: /* loopne */
	case 0xe1: /* loope */
	case 0xe2: /* loop */
		*type = INSN_JUMP_CONDITIONAL;
		break;

	case 0xe8:
		*type = INSN_CALL;
		/*
		 * For the impact on the stack, a CALL behaves like
		 * a PUSH of an immediate value (the return address).
		 */
		ADD_OP(op) {
			op->src.type = OP_SRC_CONST;
			op->dest.type = OP_DEST_PUSH;
		}
		break;

	case 0xfc:
		*type = INSN_CLD;
		break;

	case 0xfd:
		*type = INSN_STD;
		break;

	case 0xff:
		if (modrm_reg == 2 || modrm_reg == 3) {

			*type = INSN_CALL_DYNAMIC;
			if (has_notrack_prefix(&insn))
				WARN("notrack prefix found at %s:0x%lx", sec->name, offset);

		} else if (modrm_reg == 4) {

			*type = INSN_JUMP_DYNAMIC;
			if (has_notrack_prefix(&insn))
				WARN("notrack prefix found at %s:0x%lx", sec->name, offset);

		} else if (modrm_reg == 5) {

			/* jmpf */
			*type = INSN_CONTEXT_SWITCH;

		} else if (modrm_reg == 6) {

			/* push from mem */
			ADD_OP(op) {
				op->src.type = OP_SRC_CONST;
				op->dest.type = OP_DEST_PUSH;
			}
		}

		break;

	default:
		break;
	}

	*immediate = insn.immediate.nbytes ? insn.immediate.value : 0;

	return 0;
}

void arch_initial_func_cfi_state(struct cfi_init_state *state)
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
	state->regs[CFI_RA].base = CFI_CFA;
	state->regs[CFI_RA].offset = -8;
}

const char *arch_nop_insn(int len)
{
	static const char nops[5][5] = {
		{ BYTES_NOP1 },
		{ BYTES_NOP2 },
		{ BYTES_NOP3 },
		{ BYTES_NOP4 },
		{ BYTES_NOP5 },
	};

	if (len < 1 || len > 5) {
		WARN("invalid NOP size: %d\n", len);
		return NULL;
	}

	return nops[len-1];
}

#define BYTE_RET	0xC3

const char *arch_ret_insn(int len)
{
	static const char ret[5][5] = {
		{ BYTE_RET },
		{ BYTE_RET, 0xcc },
		{ BYTE_RET, 0xcc, BYTES_NOP1 },
		{ BYTE_RET, 0xcc, BYTES_NOP2 },
		{ BYTE_RET, 0xcc, BYTES_NOP3 },
	};

	if (len < 1 || len > 5) {
		WARN("invalid RET size: %d\n", len);
		return NULL;
	}

	return ret[len-1];
}

int arch_decode_hint_reg(u8 sp_reg, int *base)
{
	switch (sp_reg) {
	case ORC_REG_UNDEFINED:
		*base = CFI_UNDEFINED;
		break;
	case ORC_REG_SP:
		*base = CFI_SP;
		break;
	case ORC_REG_BP:
		*base = CFI_BP;
		break;
	case ORC_REG_SP_INDIRECT:
		*base = CFI_SP_INDIRECT;
		break;
	case ORC_REG_R10:
		*base = CFI_R10;
		break;
	case ORC_REG_R13:
		*base = CFI_R13;
		break;
	case ORC_REG_DI:
		*base = CFI_DI;
		break;
	case ORC_REG_DX:
		*base = CFI_DX;
		break;
	default:
		return -1;
	}

	return 0;
}

bool arch_is_retpoline(struct symbol *sym)
{
	return !strncmp(sym->name, "__x86_indirect_", 15);
}

bool arch_is_rethunk(struct symbol *sym)
{
	return !strcmp(sym->name, "__x86_return_thunk");
}

bool arch_is_embedded_insn(struct symbol *sym)
{
	return !strcmp(sym->name, "retbleed_return_thunk") ||
	       !strcmp(sym->name, "srso_safe_ret");
}
