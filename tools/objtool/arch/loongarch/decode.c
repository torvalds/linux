// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>
#include <objtool/check.h>

int arch_ftrace_match(char *name)
{
	return !strcmp(name, "_mcount");
}

unsigned long arch_jump_destination(struct instruction *insn)
{
	return insn->offset + (insn->immediate << 2);
}

unsigned long arch_dest_reloc_offset(int addend)
{
	return addend;
}

bool arch_pc_relative_reloc(struct reloc *reloc)
{
	return false;
}

bool arch_callee_saved_reg(unsigned char reg)
{
	switch (reg) {
	case CFI_RA:
	case CFI_FP:
	case CFI_S0 ... CFI_S8:
		return true;
	default:
		return false;
	}
}

int arch_decode_hint_reg(u8 sp_reg, int *base)
{
	return 0;
}

int arch_decode_instruction(struct objtool_file *file, const struct section *sec,
			    unsigned long offset, unsigned int maxlen,
			    struct instruction *insn)
{
	return 0;
}

const char *arch_nop_insn(int len)
{
	return NULL;
}

const char *arch_ret_insn(int len)
{
	return NULL;
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
}
