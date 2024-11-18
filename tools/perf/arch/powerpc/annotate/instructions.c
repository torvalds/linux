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
#define PPC_22_30(R)	(((R) >> 1) & 0x1ff)

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

/*
 * Arithmetic instructions which are having opcode as 31.
 * These instructions are tracked to save the register state
 * changes. Example:
 *
 * lwz	r10,264(r3)
 * add	r31, r3, r3
 * lwz	r9, 0(r31)
 *
 * Here instruction tracking needs to identify the "add"
 * instruction and save data type of r3 to r31. If a sample
 * is hit at next "lwz r9, 0(r31)", by this instruction tracking,
 * data type of r31 can be resolved.
 */
static struct insn_offset arithmetic_ins_op_31[] = {
	{ .name = "SUB_CARRY_XO_FORM",  .value = 8, },
	{ .name = "MUL_HDW_XO_FORM1",   .value = 9, },
	{ .name = "ADD_CARRY_XO_FORM",  .value = 10, },
	{ .name = "MUL_HW_XO_FORM1",    .value = 11, },
	{ .name = "SUB_XO_FORM",        .value = 40, },
	{ .name = "MUL_HDW_XO_FORM",    .value = 73, },
	{ .name = "MUL_HW_XO_FORM",     .value = 75, },
	{ .name = "SUB_EXT_XO_FORM",    .value = 136, },
	{ .name = "ADD_EXT_XO_FORM",    .value = 138, },
	{ .name = "SUB_ZERO_EXT_XO_FORM",       .value = 200, },
	{ .name = "ADD_ZERO_EXT_XO_FORM",       .value = 202, },
	{ .name = "SUB_EXT_XO_FORM2",   .value = 232, },
	{ .name = "MUL_DW_XO_FORM",     .value = 233, },
	{ .name = "ADD_EXT_XO_FORM2",   .value = 234, },
	{ .name = "MUL_W_XO_FORM",      .value = 235, },
	{ .name = "ADD_XO_FORM",	.value = 266, },
	{ .name = "DIV_DW_XO_FORM1",    .value = 457, },
	{ .name = "DIV_W_XO_FORM1",     .value = 459, },
	{ .name = "DIV_DW_XO_FORM",	.value = 489, },
	{ .name = "DIV_W_XO_FORM",	.value = 491, },
};

static struct insn_offset arithmetic_two_ops[] = {
	{ .name = "mulli",      .value = 7, },
	{ .name = "subfic",     .value = 8, },
	{ .name = "addic",      .value = 12, },
	{ .name = "addic.",     .value = 13, },
	{ .name = "addi",       .value = 14, },
	{ .name = "addis",      .value = 15, },
};

static int cmp_offset(const void *a, const void *b)
{
	const struct insn_offset *val1 = a;
	const struct insn_offset *val2 = b;

	return (val1->value - val2->value);
}

static struct ins_ops *check_ppc_insn(struct disasm_line *dl)
{
	int raw_insn = dl->raw.raw_insn;
	int opcode = PPC_OP(raw_insn);
	int mem_insn_31 = PPC_21_30(raw_insn);
	struct insn_offset *ret;
	struct insn_offset mem_insns_31_opcode = {
		"OP_31_INSN",
		mem_insn_31
	};
	char name_insn[32];

	/*
	 * Instructions with opcode 32 to 63 are memory
	 * instructions in powerpc
	 */
	if ((opcode & 0x20)) {
		/*
		 * Set name in case of raw instruction to
		 * opcode to be used in insn-stat
		 */
		if (!strlen(dl->ins.name)) {
			sprintf(name_insn, "%d", opcode);
			dl->ins.name = strdup(name_insn);
		}
		return &load_store_ops;
	} else if (opcode == 31) {
		/* Check for memory instructions with opcode 31 */
		ret = bsearch(&mem_insns_31_opcode, ins_array, ARRAY_SIZE(ins_array), sizeof(ins_array[0]), cmp_offset);
		if (ret) {
			if (!strlen(dl->ins.name))
				dl->ins.name = strdup(ret->name);
			return &load_store_ops;
		} else {
			mem_insns_31_opcode.value = PPC_22_30(raw_insn);
			ret = bsearch(&mem_insns_31_opcode, arithmetic_ins_op_31, ARRAY_SIZE(arithmetic_ins_op_31),
					sizeof(arithmetic_ins_op_31[0]), cmp_offset);
			if (ret != NULL)
				return &arithmetic_ops;
			/* Bits 21 to 30 has value 444 for "mr" insn ie, OR X form */
			if (PPC_21_30(raw_insn) == 444)
				return &arithmetic_ops;
		}
	} else {
		mem_insns_31_opcode.value = opcode;
		ret = bsearch(&mem_insns_31_opcode, arithmetic_two_ops, ARRAY_SIZE(arithmetic_two_ops),
				sizeof(arithmetic_two_ops[0]), cmp_offset);
		if (ret != NULL)
			return &arithmetic_ops;
	}

	return NULL;
}

/*
 * Instruction tracking function to track register state moves.
 * Example sequence:
 *    ld      r10,264(r3)
 *    mr      r31,r3
 *    <<after some sequence>
 *    ld      r9,312(r31)
 *
 * Previous instruction sequence shows that register state of r3
 * is moved to r31. update_insn_state_powerpc tracks these state
 * changes
 */
#ifdef HAVE_DWARF_SUPPORT
static void update_insn_state_powerpc(struct type_state *state,
		struct data_loc_info *dloc, Dwarf_Die * cu_die __maybe_unused,
		struct disasm_line *dl)
{
	struct annotated_insn_loc loc;
	struct annotated_op_loc *src = &loc.ops[INSN_OP_SOURCE];
	struct annotated_op_loc *dst = &loc.ops[INSN_OP_TARGET];
	struct type_state_reg *tsr;
	u32 insn_offset = dl->al.offset;

	if (annotate_get_insn_location(dloc->arch, dl, &loc) < 0)
		return;

	/*
	 * Value 444 for bits 21:30 is for "mr"
	 * instruction. "mr" is extended OR. So set the
	 * source and destination reg correctly
	 */
	if (PPC_21_30(dl->raw.raw_insn) == 444) {
		int src_reg = src->reg1;

		src->reg1 = dst->reg1;
		dst->reg1 = src_reg;
	}

	if (!has_reg_type(state, dst->reg1))
		return;

	tsr = &state->regs[dst->reg1];

	if (!has_reg_type(state, src->reg1) ||
			!state->regs[src->reg1].ok) {
		tsr->ok = false;
		return;
	}

	tsr->type = state->regs[src->reg1].type;
	tsr->kind = state->regs[src->reg1].kind;
	tsr->ok = true;

	pr_debug_dtp("mov [%x] reg%d -> reg%d",
			insn_offset, src->reg1, dst->reg1);
	pr_debug_type_name(&tsr->type, tsr->kind);
}
#endif /* HAVE_DWARF_SUPPORT */

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
