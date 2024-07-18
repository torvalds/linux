// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>

static struct ins_ops *powerpc__associate_instruction_ops(struct arch *arch, const char *name)
{
	int i;
	struct ins_ops *ops;

	/*
	 * - Interested only if instruction starts with 'b'.
	 * - Few start with 'b', but aren't branch instructions.
	 */
	if (name[0] != 'b'             ||
	    !strncmp(name, "bcd", 3)   ||
	    !strncmp(name, "brinc", 5) ||
	    !strncmp(name, "bper", 4))
		return NULL;

	ops = &jump_ops;

	i = strlen(name) - 1;
	if (i < 0)
		return NULL;

	/* ignore optional hints at the end of the instructions */
	if (name[i] == '+' || name[i] == '-')
		i--;

	if (name[i] == 'l' || (name[i] == 'a' && name[i-1] == 'l')) {
		/*
		 * if the instruction ends up with 'l' or 'la', then
		 * those are considered 'calls' since they update LR.
		 * ... except for 'bnl' which is branch if not less than
		 * and the absolute form of the same.
		 */
		if (strcmp(name, "bnl") && strcmp(name, "bnl+") &&
		    strcmp(name, "bnl-") && strcmp(name, "bnla") &&
		    strcmp(name, "bnla+") && strcmp(name, "bnla-"))
			ops = &call_ops;
	}
	if (name[i] == 'r' && name[i-1] == 'l')
		/*
		 * instructions ending with 'lr' are considered to be
		 * return instructions
		 */
		ops = &ret_ops;

	arch__associate_ins_ops(arch, name, ops);
	return ops;
}

#define PPC_OP(op)	(((op) >> 26) & 0x3F)
#define PPC_21_30(R)	(((R) >> 1) & 0x3ff)

struct insn_offset {
	const char	*name;
	int		value;
};

/*
 * There are memory instructions with opcode 31 which are
 * of X Form, Example:
 * ldx RT,RA,RB
 * ______________________________________
 * | 31 |  RT  |  RA |  RB |   21     |/|
 * --------------------------------------
 * 0    6     11    16    21         30 31
 *
 * But all instructions with opcode 31 are not memory.
 * Example: add RT,RA,RB
 *
 * Use bits 21 to 30 to check memory insns with 31 as opcode.
 * In ins_array below, for ldx instruction:
 * name => OP_31_XOP_LDX
 * value => 21
 */

static struct insn_offset ins_array[] = {
	{ .name = "OP_31_XOP_LXSIWZX",  .value = 12, },
	{ .name = "OP_31_XOP_LWARX",	.value = 20, },
	{ .name = "OP_31_XOP_LDX",	.value = 21, },
	{ .name = "OP_31_XOP_LWZX",	.value = 23, },
	{ .name = "OP_31_XOP_LDUX",	.value = 53, },
	{ .name = "OP_31_XOP_LWZUX",	.value = 55, },
	{ .name = "OP_31_XOP_LXSIWAX",  .value = 76, },
	{ .name = "OP_31_XOP_LDARX",    .value = 84, },
	{ .name = "OP_31_XOP_LBZX",	.value = 87, },
	{ .name = "OP_31_XOP_LVX",      .value = 103, },
	{ .name = "OP_31_XOP_LBZUX",    .value = 119, },
	{ .name = "OP_31_XOP_STXSIWX",  .value = 140, },
	{ .name = "OP_31_XOP_STDX",	.value = 149, },
	{ .name = "OP_31_XOP_STWX",	.value = 151, },
	{ .name = "OP_31_XOP_STDUX",	.value = 181, },
	{ .name = "OP_31_XOP_STWUX",	.value = 183, },
	{ .name = "OP_31_XOP_STBX",	.value = 215, },
	{ .name = "OP_31_XOP_STVX",     .value = 231, },
	{ .name = "OP_31_XOP_STBUX",	.value = 247, },
	{ .name = "OP_31_XOP_LHZX",	.value = 279, },
	{ .name = "OP_31_XOP_LHZUX",	.value = 311, },
	{ .name = "OP_31_XOP_LXVDSX",   .value = 332, },
	{ .name = "OP_31_XOP_LWAX",	.value = 341, },
	{ .name = "OP_31_XOP_LHAX",	.value = 343, },
	{ .name = "OP_31_XOP_LWAUX",	.value = 373, },
	{ .name = "OP_31_XOP_LHAUX",	.value = 375, },
	{ .name = "OP_31_XOP_STHX",	.value = 407, },
	{ .name = "OP_31_XOP_STHUX",	.value = 439, },
	{ .name = "OP_31_XOP_LXSSPX",   .value = 524, },
	{ .name = "OP_31_XOP_LDBRX",	.value = 532, },
	{ .name = "OP_31_XOP_LSWX",	.value = 533, },
	{ .name = "OP_31_XOP_LWBRX",	.value = 534, },
	{ .name = "OP_31_XOP_LFSUX",    .value = 567, },
	{ .name = "OP_31_XOP_LXSDX",    .value = 588, },
	{ .name = "OP_31_XOP_LSWI",	.value = 597, },
	{ .name = "OP_31_XOP_LFDX",     .value = 599, },
	{ .name = "OP_31_XOP_LFDUX",    .value = 631, },
	{ .name = "OP_31_XOP_STXSSPX",  .value = 652, },
	{ .name = "OP_31_XOP_STDBRX",	.value = 660, },
	{ .name = "OP_31_XOP_STXWX",	.value = 661, },
	{ .name = "OP_31_XOP_STWBRX",	.value = 662, },
	{ .name = "OP_31_XOP_STFSX",	.value = 663, },
	{ .name = "OP_31_XOP_STFSUX",	.value = 695, },
	{ .name = "OP_31_XOP_STXSDX",   .value = 716, },
	{ .name = "OP_31_XOP_STSWI",	.value = 725, },
	{ .name = "OP_31_XOP_STFDX",	.value = 727, },
	{ .name = "OP_31_XOP_STFDUX",	.value = 759, },
	{ .name = "OP_31_XOP_LXVW4X",   .value = 780, },
	{ .name = "OP_31_XOP_LHBRX",	.value = 790, },
	{ .name = "OP_31_XOP_LXVD2X",   .value = 844, },
	{ .name = "OP_31_XOP_LFIWAX",	.value = 855, },
	{ .name = "OP_31_XOP_LFIWZX",	.value = 887, },
	{ .name = "OP_31_XOP_STXVW4X",  .value = 908, },
	{ .name = "OP_31_XOP_STHBRX",	.value = 918, },
	{ .name = "OP_31_XOP_STXVD2X",  .value = 972, },
	{ .name = "OP_31_XOP_STFIWX",	.value = 983, },
};

static int cmp_offset(const void *a, const void *b)
{
	const struct insn_offset *val1 = a;
	const struct insn_offset *val2 = b;

	return (val1->value - val2->value);
}

static struct ins_ops *check_ppc_insn(u32 raw_insn)
{
	int opcode = PPC_OP(raw_insn);
	int mem_insn_31 = PPC_21_30(raw_insn);
	struct insn_offset *ret;
	struct insn_offset mem_insns_31_opcode = {
		"OP_31_INSN",
		mem_insn_31
	};

	/*
	 * Instructions with opcode 32 to 63 are memory
	 * instructions in powerpc
	 */
	if ((opcode & 0x20)) {
		return &load_store_ops;
	} else if (opcode == 31) {
		/* Check for memory instructions with opcode 31 */
		ret = bsearch(&mem_insns_31_opcode, ins_array, ARRAY_SIZE(ins_array), sizeof(ins_array[0]), cmp_offset);
		if (ret != NULL)
			return &load_store_ops;
	}

	return NULL;
}

static int powerpc__annotate_init(struct arch *arch, char *cpuid __maybe_unused)
{
	if (!arch->initialized) {
		arch->initialized = true;
		arch->associate_instruction_ops = powerpc__associate_instruction_ops;
		arch->objdump.comment_char      = '#';
		annotate_opts.show_asm_raw = true;
	}

	return 0;
}
