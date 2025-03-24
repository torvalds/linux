// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Mapping of DWARF debug register numbers into register names.
 *
 * Copyright (C) 2010 Ian Munsie, IBM Corporation.
 */

#include <dwarf-regs.h>

#define PPC_OP(op)	(((op) >> 26) & 0x3F)
#define PPC_RA(a)	(((a) >> 16) & 0x1f)
#define PPC_RT(t)	(((t) >> 21) & 0x1f)
#define PPC_RB(b)	(((b) >> 11) & 0x1f)
#define PPC_D(D)	((D) & 0xfffe)
#define PPC_DS(DS)	((DS) & 0xfffc)
#define OP_LD	58
#define OP_STD	62

static int get_source_reg(u32 raw_insn)
{
	return PPC_RA(raw_insn);
}

static int get_target_reg(u32 raw_insn)
{
	return PPC_RT(raw_insn);
}

static int get_offset_opcode(u32 raw_insn)
{
	int opcode = PPC_OP(raw_insn);

	/* DS- form */
	if ((opcode == OP_LD) || (opcode == OP_STD))
		return PPC_DS(raw_insn);
	else
		return PPC_D(raw_insn);
}

/*
 * Fills the required fields for op_loc depending on if it
 * is a source or target.
 * D form: ins RT,D(RA) -> src_reg1 = RA, offset = D, dst_reg1 = RT
 * DS form: ins RT,DS(RA) -> src_reg1 = RA, offset = DS, dst_reg1 = RT
 * X form: ins RT,RA,RB -> src_reg1 = RA, src_reg2 = RB, dst_reg1 = RT
 */
void get_powerpc_regs(u32 raw_insn, int is_source,
		struct annotated_op_loc *op_loc)
{
	if (is_source)
		op_loc->reg1 = get_source_reg(raw_insn);
	else
		op_loc->reg1 = get_target_reg(raw_insn);

	if (op_loc->multi_regs)
		op_loc->reg2 = PPC_RB(raw_insn);

	/* TODO: Implement offset handling for X Form */
	if ((op_loc->mem_ref) && (PPC_OP(raw_insn) != 31))
		op_loc->offset = get_offset_opcode(raw_insn);
}
