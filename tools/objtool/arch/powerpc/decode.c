// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <objtool/check.h>
#include <objtool/elf.h>
#include <objtool/arch.h>
#include <objtool/warn.h>
#include <objtool/builtin.h>
#include <objtool/endianness.h>

int arch_ftrace_match(char *name)
{
	return !strcmp(name, "_mcount");
}

unsigned long arch_dest_reloc_offset(int addend)
{
	return addend;
}

bool arch_callee_saved_reg(unsigned char reg)
{
	return false;
}

int arch_decode_hint_reg(u8 sp_reg, int *base)
{
	exit(-1);
}

const char *arch_nop_insn(int len)
{
	exit(-1);
}

const char *arch_ret_insn(int len)
{
	exit(-1);
}

int arch_decode_instruction(struct objtool_file *file, const struct section *sec,
			    unsigned long offset, unsigned int maxlen,
			    struct instruction *insn)
{
	unsigned int opcode;
	enum insn_type typ;
	unsigned long imm;
	u32 ins;

	ins = bswap_if_needed(file->elf, *(u32 *)(sec->data->d_buf + offset));
	opcode = ins >> 26;
	typ = INSN_OTHER;
	imm = 0;

	switch (opcode) {
	case 18: /* b[l][a] */
		if (ins == 0x48000005)	/* bl .+4 */
			typ = INSN_OTHER;
		else if (ins & 1)	/* bl[a] */
			typ = INSN_CALL;
		else		/* b[a] */
			typ = INSN_JUMP_UNCONDITIONAL;

		imm = ins & 0x3fffffc;
		if (imm & 0x2000000)
			imm -= 0x4000000;
		imm |= ins & 2;	/* AA flag */
		break;
	}

	if (opcode == 1)
		insn->len = 8;
	else
		insn->len = 4;

	insn->type = typ;
	insn->immediate = imm;

	return 0;
}

unsigned long arch_jump_destination(struct instruction *insn)
{
	if (insn->immediate & 2)
		return insn->immediate & ~2;

	return insn->offset + insn->immediate;
}

bool arch_pc_relative_reloc(struct reloc *reloc)
{
	/*
	 * The powerpc build only allows certain relocation types, see
	 * relocs_check.sh, and none of those accepted are PC relative.
	 */
	return false;
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
	state->cfa.offset = 0;

	/* initial LR (return address) */
	state->regs[CFI_RA].base = CFI_CFA;
	state->regs[CFI_RA].offset = 0;
}

unsigned int arch_reloc_size(struct reloc *reloc)
{
	switch (reloc_type(reloc)) {
	case R_PPC_REL32:
	case R_PPC_ADDR32:
	case R_PPC_UADDR32:
	case R_PPC_PLT32:
	case R_PPC_PLTREL32:
		return 4;
	default:
		return 8;
	}
}
