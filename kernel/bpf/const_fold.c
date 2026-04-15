// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf_verifier.h>

/*
 * Forward dataflow analysis to determine constant register values at every
 * instruction. Tracks 64-bit constant values in R0-R9 through the program,
 * using a fixed-point iteration in reverse postorder. Records which registers
 * hold known constants and their values in
 * env->insn_aux_data[].{const_reg_mask, const_reg_vals}.
 */

enum const_arg_state {
	CONST_ARG_UNVISITED,	/* instruction not yet reached */
	CONST_ARG_UNKNOWN,	/* register value not a known constant */
	CONST_ARG_CONST,	/* register holds a known 64-bit constant */
	CONST_ARG_MAP_PTR,	/* register holds a map pointer, map_index is set */
	CONST_ARG_MAP_VALUE,	/* register points to map value data, val is offset */
	CONST_ARG_SUBPROG,	/* register holds a subprog pointer, val is subprog number */
};

struct const_arg_info {
	enum const_arg_state state;
	u32 map_index;
	u64 val;
};

static bool ci_is_unvisited(const struct const_arg_info *ci)
{
	return ci->state == CONST_ARG_UNVISITED;
}

static bool ci_is_unknown(const struct const_arg_info *ci)
{
	return ci->state == CONST_ARG_UNKNOWN;
}

static bool ci_is_const(const struct const_arg_info *ci)
{
	return ci->state == CONST_ARG_CONST;
}

static bool ci_is_map_value(const struct const_arg_info *ci)
{
	return ci->state == CONST_ARG_MAP_VALUE;
}

/* Transfer function: compute output register state from instruction. */
static void const_reg_xfer(struct bpf_verifier_env *env, struct const_arg_info *ci_out,
			   struct bpf_insn *insn, struct bpf_insn *insns, int idx)
{
	struct const_arg_info unknown = { .state = CONST_ARG_UNKNOWN, .val = 0 };
	struct const_arg_info *dst = &ci_out[insn->dst_reg];
	struct const_arg_info *src = &ci_out[insn->src_reg];
	u8 class = BPF_CLASS(insn->code);
	u8 mode = BPF_MODE(insn->code);
	u8 opcode = BPF_OP(insn->code) | BPF_SRC(insn->code);
	int r;

	switch (class) {
	case BPF_ALU:
	case BPF_ALU64:
		switch (opcode) {
		case BPF_MOV | BPF_K:
			dst->state = CONST_ARG_CONST;
			dst->val = (s64)insn->imm;
			break;
		case BPF_MOV | BPF_X:
			*dst = *src;
			if (!insn->off)
				break;
			if (!ci_is_const(dst)) {
				*dst = unknown;
				break;
			}
			switch (insn->off) {
			case 8:  dst->val = (s8)dst->val; break;
			case 16: dst->val = (s16)dst->val; break;
			case 32: dst->val = (s32)dst->val; break;
			default: *dst = unknown; break;
			}
			break;
		case BPF_ADD | BPF_K:
			if (!ci_is_const(dst) && !ci_is_map_value(dst)) {
				*dst = unknown;
				break;
			}
			dst->val += insn->imm;
			break;
		case BPF_SUB | BPF_K:
			if (!ci_is_const(dst) && !ci_is_map_value(dst)) {
				*dst = unknown;
				break;
			}
			dst->val -= insn->imm;
			break;
		case BPF_AND | BPF_K:
			if (!ci_is_const(dst)) {
				if (!insn->imm) {
					dst->state = CONST_ARG_CONST;
					dst->val = 0;
				} else {
					*dst = unknown;
				}
				break;
			}
			dst->val &= (s64)insn->imm;
			break;
		case BPF_AND | BPF_X:
			if (ci_is_const(dst) && dst->val == 0)
				break; /* 0 & x == 0 */
			if (ci_is_const(src) && src->val == 0) {
				dst->state = CONST_ARG_CONST;
				dst->val = 0;
				break;
			}
			if (!ci_is_const(dst) || !ci_is_const(src)) {
				*dst = unknown;
				break;
			}
			dst->val &= src->val;
			break;
		default:
			*dst = unknown;
			break;
		}
		if (class == BPF_ALU) {
			if (ci_is_const(dst))
				dst->val = (u32)dst->val;
			else if (!ci_is_unknown(dst))
				*dst = unknown;
		}
		break;
	case BPF_LD:
		if (mode == BPF_ABS || mode == BPF_IND)
			goto process_call;
		if (mode != BPF_IMM || BPF_SIZE(insn->code) != BPF_DW)
			break;
		if (insn->src_reg == BPF_PSEUDO_FUNC) {
			int subprog = bpf_find_subprog(env, idx + insn->imm + 1);

			if (subprog >= 0) {
				dst->state = CONST_ARG_SUBPROG;
				dst->val = subprog;
			} else {
				*dst = unknown;
			}
		} else if (insn->src_reg == BPF_PSEUDO_MAP_VALUE ||
			   insn->src_reg == BPF_PSEUDO_MAP_IDX_VALUE) {
			dst->state = CONST_ARG_MAP_VALUE;
			dst->map_index = env->insn_aux_data[idx].map_index;
			dst->val = env->insn_aux_data[idx].map_off;
		} else if (insn->src_reg == BPF_PSEUDO_MAP_FD ||
			   insn->src_reg == BPF_PSEUDO_MAP_IDX) {
			dst->state = CONST_ARG_MAP_PTR;
			dst->map_index = env->insn_aux_data[idx].map_index;
		} else if (insn->src_reg == 0) {
			dst->state = CONST_ARG_CONST;
			dst->val = (u64)(u32)insn->imm | ((u64)(u32)insns[idx + 1].imm << 32);
		} else {
			*dst = unknown;
		}
		break;
	case BPF_LDX:
		if (!ci_is_map_value(src)) {
			*dst = unknown;
			break;
		}
		struct bpf_map *map = env->used_maps[src->map_index];
		int size = bpf_size_to_bytes(BPF_SIZE(insn->code));
		bool is_ldsx = mode == BPF_MEMSX;
		int off = src->val + insn->off;
		u64 val = 0;

		if (!bpf_map_is_rdonly(map) || !map->ops->map_direct_value_addr ||
		    map->map_type == BPF_MAP_TYPE_INSN_ARRAY ||
		    off < 0 || off + size > map->value_size ||
		    bpf_map_direct_read(map, off, size, &val, is_ldsx)) {
			*dst = unknown;
			break;
		}
		dst->state = CONST_ARG_CONST;
		dst->val = val;
		break;
	case BPF_JMP:
		if (opcode != BPF_CALL)
			break;
process_call:
		for (r = BPF_REG_0; r <= BPF_REG_5; r++)
			ci_out[r] = unknown;
		break;
	case BPF_STX:
		if (mode != BPF_ATOMIC)
			break;
		if (insn->imm == BPF_CMPXCHG)
			ci_out[BPF_REG_0] = unknown;
		else if (insn->imm == BPF_LOAD_ACQ)
			*dst = unknown;
		else if (insn->imm & BPF_FETCH)
			*src = unknown;
		break;
	}
}

/* Join function: merge output state into a successor's input state. */
static bool const_reg_join(struct const_arg_info *ci_target,
			   struct const_arg_info *ci_out)
{
	bool changed = false;
	int r;

	for (r = 0; r < MAX_BPF_REG; r++) {
		struct const_arg_info *old = &ci_target[r];
		struct const_arg_info *new = &ci_out[r];

		if (ci_is_unvisited(old) && !ci_is_unvisited(new)) {
			ci_target[r] = *new;
			changed = true;
		} else if (!ci_is_unknown(old) && !ci_is_unvisited(old) &&
			   (new->state != old->state || new->val != old->val ||
			    new->map_index != old->map_index)) {
			old->state = CONST_ARG_UNKNOWN;
			changed = true;
		}
	}
	return changed;
}

int bpf_compute_const_regs(struct bpf_verifier_env *env)
{
	struct const_arg_info unknown = { .state = CONST_ARG_UNKNOWN, .val = 0 };
	struct bpf_insn_aux_data *insn_aux = env->insn_aux_data;
	struct bpf_insn *insns = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	struct const_arg_info (*ci_in)[MAX_BPF_REG];
	struct const_arg_info ci_out[MAX_BPF_REG];
	struct bpf_iarray *succ;
	bool changed;
	int i, r;

	/* kvzalloc zeroes memory, so all entries start as CONST_ARG_UNVISITED (0) */
	ci_in = kvzalloc_objs(*ci_in, insn_cnt, GFP_KERNEL_ACCOUNT);
	if (!ci_in)
		return -ENOMEM;

	/* Subprogram entries (including main at subprog 0): all registers unknown */
	for (i = 0; i < env->subprog_cnt; i++) {
		int start = env->subprog_info[i].start;

		for (r = 0; r < MAX_BPF_REG; r++)
			ci_in[start][r] = unknown;
	}

redo:
	changed = false;
	for (i = env->cfg.cur_postorder - 1; i >= 0; i--) {
		int idx = env->cfg.insn_postorder[i];
		struct bpf_insn *insn = &insns[idx];
		struct const_arg_info *ci = ci_in[idx];

		memcpy(ci_out, ci, sizeof(ci_out));

		const_reg_xfer(env, ci_out, insn, insns, idx);

		succ = bpf_insn_successors(env, idx);
		for (int s = 0; s < succ->cnt; s++)
			changed |= const_reg_join(ci_in[succ->items[s]], ci_out);
	}
	if (changed)
		goto redo;

	/* Save computed constants into insn_aux[] if they fit into 32-bit */
	for (i = 0; i < insn_cnt; i++) {
		u16 mask = 0, map_mask = 0, subprog_mask = 0;
		struct bpf_insn_aux_data *aux = &insn_aux[i];
		struct const_arg_info *ci = ci_in[i];

		for (r = BPF_REG_0; r < ARRAY_SIZE(aux->const_reg_vals); r++) {
			struct const_arg_info *c = &ci[r];

			switch (c->state) {
			case CONST_ARG_CONST: {
				u64 val = c->val;

				if (val != (u32)val)
					break;
				mask |= BIT(r);
				aux->const_reg_vals[r] = val;
				break;
			}
			case CONST_ARG_MAP_PTR:
				map_mask |= BIT(r);
				aux->const_reg_vals[r] = c->map_index;
				break;
			case CONST_ARG_SUBPROG:
				subprog_mask |= BIT(r);
				aux->const_reg_vals[r] = c->val;
				break;
			default:
				break;
			}
		}
		aux->const_reg_mask = mask;
		aux->const_reg_map_mask = map_mask;
		aux->const_reg_subprog_mask = subprog_mask;
	}

	kvfree(ci_in);
	return 0;
}

static int eval_const_branch(u8 opcode, u64 dst_val, u64 src_val)
{
	switch (BPF_OP(opcode)) {
	case BPF_JEQ:	return dst_val == src_val;
	case BPF_JNE:	return dst_val != src_val;
	case BPF_JGT:	return dst_val > src_val;
	case BPF_JGE:	return dst_val >= src_val;
	case BPF_JLT:	return dst_val < src_val;
	case BPF_JLE:	return dst_val <= src_val;
	case BPF_JSGT:	return (s64)dst_val > (s64)src_val;
	case BPF_JSGE:	return (s64)dst_val >= (s64)src_val;
	case BPF_JSLT:	return (s64)dst_val < (s64)src_val;
	case BPF_JSLE:	return (s64)dst_val <= (s64)src_val;
	case BPF_JSET:	return (bool)(dst_val & src_val);
	default:	return -1;
	}
}

/*
 * Rewrite conditional branches with constant outcomes into unconditional
 * jumps using register values resolved by bpf_compute_const_regs() pass.
 * This eliminates dead edges from the CFG so that compute_live_registers()
 * doesn't propagate liveness through dead code.
 */
int bpf_prune_dead_branches(struct bpf_verifier_env *env)
{
	struct bpf_insn_aux_data *insn_aux = env->insn_aux_data;
	struct bpf_insn *insns = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	bool changed = false;
	int i;

	for (i = 0; i < insn_cnt; i++) {
		struct bpf_insn_aux_data *aux = &insn_aux[i];
		struct bpf_insn *insn = &insns[i];
		u8 class = BPF_CLASS(insn->code);
		u64 dst_val, src_val;
		int taken;

		if (!bpf_insn_is_cond_jump(insn->code))
			continue;
		if (bpf_is_may_goto_insn(insn))
			continue;

		if (!(aux->const_reg_mask & BIT(insn->dst_reg)))
			continue;
		dst_val = aux->const_reg_vals[insn->dst_reg];

		if (BPF_SRC(insn->code) == BPF_K) {
			src_val = insn->imm;
		} else {
			if (!(aux->const_reg_mask & BIT(insn->src_reg)))
				continue;
			src_val = aux->const_reg_vals[insn->src_reg];
		}

		if (class == BPF_JMP32) {
			/*
			 * The (s32) cast maps the 32-bit range into two u64 sub-ranges:
			 * [0x00000000, 0x7FFFFFFF] -> [0x0000000000000000, 0x000000007FFFFFFF]
			 * [0x80000000, 0xFFFFFFFF] -> [0xFFFFFFFF80000000, 0xFFFFFFFFFFFFFFFF]
			 * The ordering is preserved within each sub-range, and
			 * the second sub-range is above the first as u64.
			 */
			dst_val = (s32)dst_val;
			src_val = (s32)src_val;
		}

		taken = eval_const_branch(insn->code, dst_val, src_val);
		if (taken < 0) {
			bpf_log(&env->log, "Unknown conditional jump %x\n", insn->code);
			return -EFAULT;
		}
		*insn = BPF_JMP_A(taken ? insn->off : 0);
		changed = true;
	}

	if (!changed)
		return 0;
	/* recompute postorder, since CFG has changed */
	kvfree(env->cfg.insn_postorder);
	env->cfg.insn_postorder = NULL;
	return bpf_compute_postorder(env);
}
