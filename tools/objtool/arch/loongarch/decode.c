// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>
#include <objtool/check.h>
#include <objtool/warn.h>
#include <asm/inst.h>
#include <asm/orc_types.h>
#include <linux/objtool_types.h>

#ifndef EM_LOONGARCH
#define EM_LOONGARCH	258
#endif

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
	switch (sp_reg) {
	case ORC_REG_UNDEFINED:
		*base = CFI_UNDEFINED;
		break;
	case ORC_REG_SP:
		*base = CFI_SP;
		break;
	case ORC_REG_FP:
		*base = CFI_FP;
		break;
	default:
		return -1;
	}

	return 0;
}

static bool is_loongarch(const struct elf *elf)
{
	if (elf->ehdr.e_machine == EM_LOONGARCH)
		return true;

	WARN("unexpected ELF machine type %d", elf->ehdr.e_machine);
	return false;
}

#define ADD_OP(op) \
	if (!(op = calloc(1, sizeof(*op)))) \
		return -1; \
	else for (*ops_list = op, ops_list = &op->next; op; op = NULL)

static bool decode_insn_reg0i26_fomat(union loongarch_instruction inst,
				      struct instruction *insn)
{
	switch (inst.reg0i26_format.opcode) {
	case b_op:
		insn->type = INSN_JUMP_UNCONDITIONAL;
		insn->immediate = sign_extend64(inst.reg0i26_format.immediate_h << 16 |
						inst.reg0i26_format.immediate_l, 25);
		break;
	case bl_op:
		insn->type = INSN_CALL;
		insn->immediate = sign_extend64(inst.reg0i26_format.immediate_h << 16 |
						inst.reg0i26_format.immediate_l, 25);
		break;
	default:
		return false;
	}

	return true;
}

static bool decode_insn_reg1i21_fomat(union loongarch_instruction inst,
				      struct instruction *insn)
{
	switch (inst.reg1i21_format.opcode) {
	case beqz_op:
	case bnez_op:
	case bceqz_op:
		insn->type = INSN_JUMP_CONDITIONAL;
		insn->immediate = sign_extend64(inst.reg1i21_format.immediate_h << 16 |
						inst.reg1i21_format.immediate_l, 20);
		break;
	default:
		return false;
	}

	return true;
}

static bool decode_insn_reg2i12_fomat(union loongarch_instruction inst,
				      struct instruction *insn,
				      struct stack_op **ops_list,
				      struct stack_op *op)
{
	switch (inst.reg2i12_format.opcode) {
	case addid_op:
		if ((inst.reg2i12_format.rd == CFI_SP) || (inst.reg2i12_format.rj == CFI_SP)) {
			/* addi.d sp,sp,si12 or addi.d fp,sp,si12 */
			insn->immediate = sign_extend64(inst.reg2i12_format.immediate, 11);
			ADD_OP(op) {
				op->src.type = OP_SRC_ADD;
				op->src.reg = inst.reg2i12_format.rj;
				op->src.offset = insn->immediate;
				op->dest.type = OP_DEST_REG;
				op->dest.reg = inst.reg2i12_format.rd;
			}
		}
		break;
	case ldd_op:
		if (inst.reg2i12_format.rj == CFI_SP) {
			/* ld.d rd,sp,si12 */
			insn->immediate = sign_extend64(inst.reg2i12_format.immediate, 11);
			ADD_OP(op) {
				op->src.type = OP_SRC_REG_INDIRECT;
				op->src.reg = CFI_SP;
				op->src.offset = insn->immediate;
				op->dest.type = OP_DEST_REG;
				op->dest.reg = inst.reg2i12_format.rd;
			}
		}
		break;
	case std_op:
		if (inst.reg2i12_format.rj == CFI_SP) {
			/* st.d rd,sp,si12 */
			insn->immediate = sign_extend64(inst.reg2i12_format.immediate, 11);
			ADD_OP(op) {
				op->src.type = OP_SRC_REG;
				op->src.reg = inst.reg2i12_format.rd;
				op->dest.type = OP_DEST_REG_INDIRECT;
				op->dest.reg = CFI_SP;
				op->dest.offset = insn->immediate;
			}
		}
		break;
	case andi_op:
		if (inst.reg2i12_format.rd == 0 &&
		    inst.reg2i12_format.rj == 0 &&
		    inst.reg2i12_format.immediate == 0)
			/* andi r0,r0,0 */
			insn->type = INSN_NOP;
		break;
	default:
		return false;
	}

	return true;
}

static bool decode_insn_reg2i14_fomat(union loongarch_instruction inst,
				      struct instruction *insn,
				      struct stack_op **ops_list,
				      struct stack_op *op)
{
	switch (inst.reg2i14_format.opcode) {
	case ldptrd_op:
		if (inst.reg2i14_format.rj == CFI_SP) {
			/* ldptr.d rd,sp,si14 */
			insn->immediate = sign_extend64(inst.reg2i14_format.immediate, 13);
			ADD_OP(op) {
				op->src.type = OP_SRC_REG_INDIRECT;
				op->src.reg = CFI_SP;
				op->src.offset = insn->immediate;
				op->dest.type = OP_DEST_REG;
				op->dest.reg = inst.reg2i14_format.rd;
			}
		}
		break;
	case stptrd_op:
		if (inst.reg2i14_format.rj == CFI_SP) {
			/* stptr.d ra,sp,0 */
			if (inst.reg2i14_format.rd == LOONGARCH_GPR_RA &&
			    inst.reg2i14_format.immediate == 0)
				break;

			/* stptr.d rd,sp,si14 */
			insn->immediate = sign_extend64(inst.reg2i14_format.immediate, 13);
			ADD_OP(op) {
				op->src.type = OP_SRC_REG;
				op->src.reg = inst.reg2i14_format.rd;
				op->dest.type = OP_DEST_REG_INDIRECT;
				op->dest.reg = CFI_SP;
				op->dest.offset = insn->immediate;
			}
		}
		break;
	default:
		return false;
	}

	return true;
}

static bool decode_insn_reg2i16_fomat(union loongarch_instruction inst,
				      struct instruction *insn)
{
	switch (inst.reg2i16_format.opcode) {
	case jirl_op:
		if (inst.reg2i16_format.rd == 0 &&
		    inst.reg2i16_format.rj == CFI_RA &&
		    inst.reg2i16_format.immediate == 0) {
			/* jirl r0,ra,0 */
			insn->type = INSN_RETURN;
		} else if (inst.reg2i16_format.rd == CFI_RA) {
			/* jirl ra,rj,offs16 */
			insn->type = INSN_CALL_DYNAMIC;
		} else if (inst.reg2i16_format.rd == CFI_A0 &&
			   inst.reg2i16_format.immediate == 0) {
			/*
			 * jirl a0,t0,0
			 * this is a special case in loongarch_suspend_enter,
			 * just treat it as a call instruction.
			 */
			insn->type = INSN_CALL_DYNAMIC;
		} else if (inst.reg2i16_format.rd == 0 &&
			   inst.reg2i16_format.immediate == 0) {
			/* jirl r0,rj,0 */
			insn->type = INSN_JUMP_DYNAMIC;
		} else if (inst.reg2i16_format.rd == 0 &&
			   inst.reg2i16_format.immediate != 0) {
			/*
			 * jirl r0,t0,12
			 * this is a rare case in JUMP_VIRT_ADDR,
			 * just ignore it due to it is harmless for tracing.
			 */
			break;
		} else {
			/* jirl rd,rj,offs16 */
			insn->type = INSN_JUMP_UNCONDITIONAL;
			insn->immediate = sign_extend64(inst.reg2i16_format.immediate, 15);
		}
		break;
	case beq_op:
	case bne_op:
	case blt_op:
	case bge_op:
	case bltu_op:
	case bgeu_op:
		insn->type = INSN_JUMP_CONDITIONAL;
		insn->immediate = sign_extend64(inst.reg2i16_format.immediate, 15);
		break;
	default:
		return false;
	}

	return true;
}

int arch_decode_instruction(struct objtool_file *file, const struct section *sec,
			    unsigned long offset, unsigned int maxlen,
			    struct instruction *insn)
{
	struct stack_op **ops_list = &insn->stack_ops;
	const struct elf *elf = file->elf;
	struct stack_op *op = NULL;
	union loongarch_instruction inst;

	if (!is_loongarch(elf))
		return -1;

	if (maxlen < LOONGARCH_INSN_SIZE)
		return 0;

	insn->len = LOONGARCH_INSN_SIZE;
	insn->type = INSN_OTHER;
	insn->immediate = 0;

	inst = *(union loongarch_instruction *)(sec->data->d_buf + offset);

	if (decode_insn_reg0i26_fomat(inst, insn))
		return 0;
	if (decode_insn_reg1i21_fomat(inst, insn))
		return 0;
	if (decode_insn_reg2i12_fomat(inst, insn, ops_list, op))
		return 0;
	if (decode_insn_reg2i14_fomat(inst, insn, ops_list, op))
		return 0;
	if (decode_insn_reg2i16_fomat(inst, insn))
		return 0;

	if (inst.word == 0)
		insn->type = INSN_NOP;
	else if (inst.reg0i15_format.opcode == break_op) {
		/* break */
		insn->type = INSN_BUG;
	} else if (inst.reg2_format.opcode == ertn_op) {
		/* ertn */
		insn->type = INSN_RETURN;
	}

	return 0;
}

const char *arch_nop_insn(int len)
{
	static u32 nop;

	if (len != LOONGARCH_INSN_SIZE)
		WARN("invalid NOP size: %d\n", len);

	nop = LOONGARCH_INSN_NOP;

	return (const char *)&nop;
}

const char *arch_ret_insn(int len)
{
	static u32 ret;

	if (len != LOONGARCH_INSN_SIZE)
		WARN("invalid RET size: %d\n", len);

	emit_jirl((union loongarch_instruction *)&ret, LOONGARCH_GPR_RA, LOONGARCH_GPR_ZERO, 0);

	return (const char *)&ret;
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
